/* Explicit MCPX bus accesses for lifted DSOUND, independent of host faults. */
#ifndef MANHUNT_AUDIO_MMIO_H
#define MANHUNT_AUDIO_MMIO_H
#include <stdint.h>
#include <stddef.h>

extern ptrdiff_t g_xbox_mem_offset;
uint32_t manhunt_audio_mmio_read(uint32_t address, unsigned width);
void manhunt_audio_mmio_write(uint32_t address, uint32_t value, unsigned width);
int manhunt_audio_guest_gate(uint32_t entry);
/* Initialize before guest dispatch. One live model is idempotent; restart after
 * shutdown is unsupported. Explicit MMIO callers serialize with destruction.
 * Shutdown drains current calls, rejects later calls, and waits for concurrent
 * shutdown owners. This closes the APU only: it does not join guest workers or
 * make their guest RAM safe to release. The host entry-return path terminates
 * the process instead of attempting shared-runtime resource reclamation. */
int manhunt_audio_init(void *guest_base);
int manhunt_audio_configure(void);
void manhunt_audio_shutdown(void);
int manhunt_audio_enabled(void);
void *manhunt_audio_raw_ptr(uint32_t address, unsigned size);

/* GP/EP program/data apertures remain ordinary guest storage, as in the
 * upstream fault-based integration. Only PAPU/VP registers are dispatched. */
static inline int manhunt_audio_register(uint32_t a)
{
    return a - 0xFE800000u < 0x30000u;
}
static inline int manhunt_audio_bus_span(uint32_t a, unsigned n)
{
    uint64_t end = (uint64_t)a + n;
    return (end > 0xFE800000u && a < 0xFE830000u) ||
           (end > 0xFE83FFFCu && a < 0xFE840000u) ||
           (end > 0xFEC00000u && a < 0xFEC01000u) ||
           end > 0x100000000ULL;
}
#define MH_AUDIO_ACCESS(bits, type) \
static inline type manhunt_audio_read##bits(uint32_t a) { \
    if (manhunt_audio_bus_span(a, sizeof(type))) \
        return (type)manhunt_audio_mmio_read(a, sizeof(type)); \
    return *(volatile type *)((uintptr_t)a + g_xbox_mem_offset); \
} \
static inline void manhunt_audio_write##bits(uint32_t a, type v) { \
    if (manhunt_audio_bus_span(a, sizeof(type))) \
        manhunt_audio_mmio_write(a, v, sizeof(type)); \
    else *(volatile type *)((uintptr_t)a + g_xbox_mem_offset) = v; \
}
MH_AUDIO_ACCESS(8, uint8_t)
MH_AUDIO_ACCESS(16, uint16_t)
MH_AUDIO_ACCESS(32, uint32_t)
#undef MH_AUDIO_ACCESS
#endif
