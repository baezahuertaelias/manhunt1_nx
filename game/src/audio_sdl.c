/* SDL sink for the reused MCPX emulator's xa2_* contract.
 * Use an explicit callback: SDL2's queue mode forces a 64 KiB pthread stack,
 * smaller than this title's static TLS on Linux. No guest code runs here.
 */
#include "apu_xaudio2.h"
#include "audio_period.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CAPACITY = 8192, CHANNELS = 2, FRAME_BYTES = 4 };
static SDL_AudioDeviceID device;
static int16_t ring[CAPACITY][CHANNELS];
static unsigned read_at, queued, underruns;
static uint64_t consumed;
static int started, dry, owns_audio;
static int buffer_frames = 512;

static void pull(void *ctx, Uint8 *stream, int bytes)
{
    (void)ctx;
    memset(stream, 0, (size_t)bytes);
    unsigned need = (unsigned)bytes / FRAME_BYTES;
    unsigned take = queued < need ? queued : need;
    for (unsigned i = 0; i < take; ++i) {
        memcpy(stream + i * FRAME_BYTES, ring[read_at], FRAME_BYTES);
        read_at = (read_at + 1) % CAPACITY;
    }
    queued -= take;
    consumed += take;
    if (take < need && started && !dry) { ++underruns; dry = 1; }
}

int xa2_init(void)
{
    if (device) return 1;
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    const char *headless = getenv("RECOMP_HEADLESS");
    if (headless && *headless && *headless != '0')
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    owns_audio = !SDL_WasInit(SDL_INIT_AUDIO);
    if (owns_audio && SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    SDL_AudioSpec want = {0}, have = {0};
    /* MANHUNT_AUDIO_FRAMES: device period, 256..2048 in powers of two.
     * A longer period gives SDL's output thread more slack when the CPU is
     * busy, at the cost of latency; anything else is refused visibly and
     * keeps the default. Switch defaults to 2048: at 512 the console's
     * device pulled about 85% of real time with our ring never dry (run 26,
     * choppy); at 2048 it kept 48 kHz (run 27). */
#ifdef MANHUNT_HORIZON
    int frames = 2048;
#else
    int frames = 512;
#endif
    const char *fe = getenv("MANHUNT_AUDIO_FRAMES");
    int valid;
    int fallback = frames;
    frames = manhunt_audio_period(fe, fallback, &valid);
    if (!valid)
        fprintf(stderr, "[MH-AUDIO] MANHUNT_AUDIO_FRAMES=%s refused (256..2048, power of two); using %d\n", fe, fallback);
    want.freq = 48000; want.format = AUDIO_S16SYS;
    want.channels = CHANNELS; want.samples = (Uint16)frames; want.callback = pull;
    read_at = queued = underruns = 0; consumed = 0; started = dry = 0;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!device) {
        fprintf(stderr, "[MH-AUDIO] SDL callback device failed: %s\n", SDL_GetError());
        if (owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_audio = 0;
        return 0;
    }
    /* SDL may adjust the callback buffer even with format changes disabled.
     * Keep four pacing periods inside our bounded queue. */
    if (!have.samples || have.samples > CAPACITY / 4) {
        fprintf(stderr, "[MH-AUDIO] unsupported SDL buffer: %u frames\n", have.samples);
        xa2_shutdown();
        return 0;
    }
    buffer_frames = have.samples;
    const char *driver = SDL_GetCurrentAudioDriver();
    fprintf(stderr, "[MH-AUDIO] SDL callback output: %s, %d Hz stereo s16, %u-frame period (asked %d)%s\n",
            driver ? driver : "?", have.freq, have.samples, frames,
            driver && !strcmp(driver, "dummy") ? " (SILENT TEST DEVICE)" : "");
    SDL_PauseAudioDevice(device, 0);
    return 1;
}
void xa2_shutdown(void)
{
    if (!device) return;
    SDL_CloseAudioDevice(device); /* waits for callback before clearing state */
    device = 0;
    if (owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    owns_audio = 0;
}
int xa2_is_active(void) { return device != 0; }
int xa2_get_buffer_size(void) { return buffer_frames; }
int xa2_get_buffer_count(void) { return 4; }
int xa2_submit_samples(const int16_t *samples, int count)
{
    if (!device || !samples || count <= 0 || count > CAPACITY) return 0;
    SDL_LockAudioDevice(device);
    if ((unsigned)count > CAPACITY - queued) {
        SDL_UnlockAudioDevice(device);
        return 0;
    }
    unsigned write_at = (read_at + queued) % CAPACITY;
    for (int i = 0; i < count; ++i) {
        memcpy(ring[write_at], samples + i * CHANNELS, FRAME_BYTES);
        write_at = (write_at + 1) % CAPACITY;
    }
    queued += (unsigned)count; started = 1; dry = 0;
    SDL_UnlockAudioDevice(device);
    return 1;
}
int xa2_queued_samples(void)
{
    if (!device) return 0;
    SDL_LockAudioDevice(device); int n = (int)queued; SDL_UnlockAudioDevice(device); return n;
}
void xa2_expect_drain(void)
{
    if (!device) return;
    SDL_LockAudioDevice(device); dry = 1; SDL_UnlockAudioDevice(device);
}
unsigned xa2_underruns(void)
{
    if (!device) return 0;
    SDL_LockAudioDevice(device); unsigned n = underruns; SDL_UnlockAudioDevice(device); return n;
}
uint64_t xa2_samples_played(void)
{
    if (!device) return 0;
    SDL_LockAudioDevice(device); uint64_t n = consumed; SDL_UnlockAudioDevice(device); return n;
}
