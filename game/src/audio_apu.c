/* Reuse xboxrecomp's Burnout 3-derived, xemu MCPX voice processor + SDL sink.
 * This glue implements no DirectSound objects or game audio logic.
 * Upstream GP/EP DSP effects are stubbed: full audio fidelity is UNVERIFIED.
 */
#include "audio_mmio.h"
#include "apu.h"
#include "apu_xaudio2.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#if defined(__SWITCH__)
/* Horizon uses explicit accesses; the host fault-hook module is not linked. */
MCPXAPUState *g_apu_state;
#else
extern MCPXAPUState *g_apu_state;
#endif
enum { AUDIO_OFF, AUDIO_RUNNING, AUDIO_CLOSING, AUDIO_CLOSED };
static atomic_int s_enabled;
static pthread_mutex_t s_access = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_closed = PTHREAD_COND_INITIALIZER;

int manhunt_audio_enabled(void) { return s_enabled == AUDIO_RUNNING; }

int manhunt_audio_configure(void)
{
    const char *e = getenv("MANHUNT_AUDIO_APU");
    if (!e || !*e || (e[0] == '0' && !e[1])) return 1;
    if (e[0] != '1' || e[1] || getenv("RECOMP_AC97_READY")) {
        fprintf(stderr, "[MH-AUDIO] FAIL: MANHUNT_AUDIO_APU must be 0/1; "
                        "APU=1 conflicts with RECOMP_AC97_READY\n");
        return 0;
    }
    return 1;
}

static int init_locked(void *guest_base)
{
    if (s_enabled == AUDIO_RUNNING) return 1;
    if (s_enabled != AUDIO_OFF) {
        fprintf(stderr, "[MH-AUDIO] cannot restart audio after shutdown began\n");
        return 0;
    }
    if (!manhunt_audio_configure()) return 0;
    const char *e = getenv("MANHUNT_AUDIO_APU");
    if (!e || e[0] != '1' || e[1]) return 1;
    /* Fail closed when SDL cannot open a device. A dummy device is useful
     * for automated tests, but its output must never be called audible. */
    if (!xa2_init()) {
        fprintf(stderr, "[MH-AUDIO] FAIL: cannot open native audio output\n");
        return 0;
    }
    g_apu_state = mcpx_apu_init_standalone(guest_base);
    if (!g_apu_state) {
        xa2_shutdown();
        return 0;
    }
    /* Codec-ready is separate from the APU register bus. Do not enable
     * RECOMP_AC97_READY: that asks upstream to fault/unmap the register
     * window, whereas this port dispatches accesses explicitly. */
    *(volatile uint32_t *)((uintptr_t)guest_base + 0xFEC00130u) = 0x100;
    s_enabled = AUDIO_RUNNING;
    fprintf(stderr, "[MH-AUDIO] MCPX APU enabled; GP/EP effects UNVERIFIED\n");
    return 1;
}

int manhunt_audio_init(void *guest_base)
{
    pthread_mutex_lock(&s_access);
    int ok = init_locked(guest_base);
    pthread_mutex_unlock(&s_access);
    return ok;
}

void manhunt_audio_shutdown(void)
{
    /* Taking the access mutex waits for every already-running modeled access.
     * Publish CLOSING before releasing it: subsequent accesses fail closed,
     * instead of dereferencing the state being destroyed or using RAM shadows.
     * Do not hold this mutex while joining the model's own frame thread.
     */
    pthread_mutex_lock(&s_access);
    while (s_enabled == AUDIO_CLOSING)
        pthread_cond_wait(&s_closed, &s_access);
    if (s_enabled != AUDIO_RUNNING) {
        pthread_mutex_unlock(&s_access);
        return;
    }
    s_enabled = AUDIO_CLOSING;
    MCPXAPUState *state = g_apu_state;
    pthread_mutex_unlock(&s_access);
    mcpx_apu_shutdown(state);
    pthread_mutex_lock(&s_access);
    g_apu_state = NULL;
    s_enabled = AUDIO_CLOSED;
    pthread_cond_broadcast(&s_closed);
    pthread_mutex_unlock(&s_access);
}

static void check_access(uint32_t address, unsigned width)
{
    /* The actual DSOUND register instructions are dword accesses. Refuse
     * unverified widths/alignment rather than use a silent register shadow. */
    if (!s_enabled || !g_apu_state || width != 4 || (address & 3) ||
        (!manhunt_audio_register(address) && address != 0xFE83FFFCu)) {
        fprintf(stderr, "[MH-AUDIO] unsupported MCPX access %08X width=%u\n",
                address, width);
        abort();
    }
}

static uint32_t read_locked(uint32_t address, unsigned width)
{
    if (!s_enabled && (width == 1 || width == 2 || width == 4)) {
        uintptr_t p = (uintptr_t)address + g_xbox_mem_offset;
        if (width == 1) return *(volatile uint8_t *)p;
        if (width == 2) return *(volatile uint16_t *)p;
        return *(volatile uint32_t *)p;
    }
    /* XDK uses byte TEST on FECTL (guest 0x0022C8E3). Read the modeled
     * register once and select its little-endian byte/word lane. */
    if (s_enabled && manhunt_audio_register(address) &&
        (width == 1 || width == 2) && address % width == 0) {
        uint32_t aligned = address & ~3u;
        check_access(aligned, 4);
        uint32_t value = (uint32_t)mcpx_apu_mmio_read(g_apu_state,
                                                    aligned - 0xFE800000u, 4);
        return (value >> ((address & 3) * 8)) & (width == 1 ? 0xFFu : 0xFFFFu);
    }
    if (s_enabled && address - 0xFEC00000u < 0x1000u &&
        (width == 1 || width == 2 || width == 4) &&
        address % width == 0 && (uint64_t)address + width <= 0xFEC01000u) {
        uintptr_t p = (uintptr_t)address + g_xbox_mem_offset;
        if (width == 1) return *(volatile uint8_t *)p;
        if (width == 2) return *(volatile uint16_t *)p;
        return *(volatile uint32_t *)p;
    }
    check_access(address, width);
    if (address == 0xFE83FFFCu)
        return *(volatile uint32_t *)((uintptr_t)address + g_xbox_mem_offset);
    return (uint32_t)mcpx_apu_mmio_read(g_apu_state, address - 0xFE800000u, width);
}

static void write_locked(uint32_t address, uint32_t value, unsigned width)
{
    if (!s_enabled && (width == 1 || width == 2 || width == 4)) {
        uintptr_t p = (uintptr_t)address + g_xbox_mem_offset;
        if (width == 1) *(volatile uint8_t *)p = value;
        else if (width == 2) *(volatile uint16_t *)p = value;
        else *(volatile uint32_t *)p = value;
        return;
    }
    if (s_enabled && address - 0xFEC00000u < 0x1000u &&
        (width == 1 || width == 2 || width == 4) &&
        address % width == 0 && (uint64_t)address + width <= 0xFEC01000u) {
        uintptr_t p = (uintptr_t)address + g_xbox_mem_offset;
        if (width == 1) *(volatile uint8_t *)p = value;
        else if (width == 2) *(volatile uint16_t *)p = value;
        else *(volatile uint32_t *)p = value;
        /* Same RR self-clear as xbox_memory_layout.c's AC97 trap. Apply
         * only to channel controls overlapped by this store. */
        for (uint32_t off = 0x10B; off < 0x180; off += 0x10) {
            uint32_t control = 0xFEC00000u + off;
            if (control >= address && control < (uint64_t)address + width)
                *(volatile uint8_t *)((uintptr_t)control + g_xbox_mem_offset) &= ~2u;
        }
        return;
    }
    check_access(address, width);
    if (address == 0xFE83FFFCu)
        *(volatile uint32_t *)((uintptr_t)address + g_xbox_mem_offset) = value;
    mcpx_apu_mmio_write(g_apu_state, address - 0xFE800000u, value, width);
}

void *manhunt_audio_raw_ptr(uint32_t address, unsigned size)
{
    if (s_enabled && manhunt_audio_bus_span(address, size)) {
        fprintf(stderr, "[MH-AUDIO] unsupported raw MCPX access %08X size=%u\n",
                address, size);
        abort();
    }
    return (void *)((uintptr_t)address + g_xbox_mem_offset);
}

/* All g_apu_state borrows are serialized with closing. RAM-only accesses from
 * the generated helpers do not borrow the APU and do not take this lock. */
static void access_begin(void)
{
    pthread_mutex_lock(&s_access);
    if (s_enabled == AUDIO_CLOSING || s_enabled == AUDIO_CLOSED) {
        fprintf(stderr, "[MH-AUDIO] MMIO after audio shutdown began -- stopped\n");
        abort();
    }
}
uint32_t manhunt_audio_mmio_read(uint32_t address, unsigned width)
{
    access_begin();
    uint32_t value = read_locked(address, width);
    pthread_mutex_unlock(&s_access);
    return value;
}
void manhunt_audio_mmio_write(uint32_t address, uint32_t value, unsigned width)
{
    access_begin();
    write_locked(address, value, width);
    pthread_mutex_unlock(&s_access);
}
