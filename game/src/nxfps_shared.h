#ifndef MANHUNT_NXFPS_SHARED_H
#define MANHUNT_NXFPS_SHARED_H
#include <stddef.h>
#include <stdint.h>

/* Status Monitor Overlay, source/Utils.hpp: packed NxFpsSharedBlock. */
enum {
    NXFPS_MAGIC = 0x465053, NXFPS_SIZE = 174,
    NXFPS_FPS = 4, NXFPS_AVG = 5, NXFPS_ACTIVE = 9, NXFPS_TICKS = 15,
    NXFPS_BUFFERS = 55, NXFPS_SET_BUFFERS = 56, NXFPS_ACTIVE_BUFFERS = 57, NXFPS_FRAME = 165,
};

/* Same first-magic rule as the overlay, with a complete-block bound check. */
static inline int manhunt_nxfps_first(const volatile uint8_t *page, size_t size)
{
    for (size_t at = 0; at + 4 <= size; at += 4) {
        uint32_t magic = (uint32_t)page[at] | (uint32_t)page[at + 1] << 8 |
                         (uint32_t)page[at + 2] << 16 | (uint32_t)page[at + 3] << 24;
        if (magic == NXFPS_MAGIC)
            return at + NXFPS_SIZE <= size ? (int)at : -2;
    }
    return -1;
}

/* Read only producer-owned timing fields. pluginActive is cleared by readers,
 * so it cannot establish whether a producer is still running. */
static inline void manhunt_nxfps_timing(const volatile uint8_t *b, uint8_t out[48])
{
    for (int i = 0; i < 40; ++i) out[i] = b[NXFPS_TICKS + i];
    for (int i = 0; i < 8; ++i) out[40 + i] = b[NXFPS_FRAME + i];
}
#endif
