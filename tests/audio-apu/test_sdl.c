#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "../../game/src/audio_sdl.c"
/* Reproduce the executable's >64 KiB static TLS requirement. */
_Thread_local volatile char large_tls[512 * 1024];
int main(void)
{
    large_tls[sizeof large_tls - 1] = 1;
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(xa2_init());
    SDL_PauseAudioDevice(device, 1);
    int16_t samples[CAPACITY * 2], result[CAPACITY * 2];
    for (int i = 0; i < CAPACITY * 2; ++i) samples[i] = (int16_t)(i + 1);
    assert(xa2_submit_samples(samples, CAPACITY));
    assert(!xa2_submit_samples(samples, 1));
    assert(xa2_queued_samples() == CAPACITY);
    pull(NULL, (Uint8 *)result, 7000 * FRAME_BYTES);
    assert(!memcmp(result, samples, 7000 * FRAME_BYTES));
    assert(xa2_samples_played() == 7000);
    assert(xa2_submit_samples(samples, 7000)); /* ring wraps */
    pull(NULL, (Uint8 *)result, CAPACITY * FRAME_BYTES);
    assert(!memcmp(result, samples + 7000 * 2, (CAPACITY - 7000) * FRAME_BYTES));
    assert(!memcmp(result + (CAPACITY - 7000) * 2, samples, 7000 * FRAME_BYTES));
    assert(!xa2_queued_samples());
    memset(result, 1, 32);
    pull(NULL, (Uint8 *)result, 32);
    for (int i = 0; i < 16; ++i) assert(!result[i]);
    assert(xa2_underruns() == 1);
    pull(NULL, (Uint8 *)result, 32);
    assert(xa2_underruns() == 1);
    assert(xa2_submit_samples(samples, 1));
    xa2_expect_drain();
    pull(NULL, (Uint8 *)result, 32);
    assert(xa2_underruns() == 1);
    xa2_shutdown();
    assert(!xa2_is_active() && !xa2_submit_samples(samples, 1));
    assert(xa2_init());
    assert(xa2_submit_samples(samples, 512));
    for (int i = 0; i < 100 && !xa2_samples_played(); ++i) SDL_Delay(5);
    assert(xa2_samples_played() > 0); /* real dummy-device callback drains */
    xa2_shutdown();
    puts("PASS SDL callback sink: large TLS, exact samples, wrap, overflow, silence, drain, reopen and active shutdown");
    return 0;
}
