#include "xbox_gamma.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);

/* Manhunt's statically linked D3D (XDK 5849), read from the lifted code:
 *   0x0029DFB8  D3D_pDevice (as in d3d_fence.c)
 *   0x0028D560  D3DDevice_SetGammaRamp: compares the new 768-byte ramp with the current one, copies it to the other of
 *               two buffers at device + 0x1C28 + 0x1DC + i * 0x300, and stores i at device + 0x240C
 *   0x0029845E  the VBlank writer: 256 entries to NV_PRMDIO_PALETTE_DATA (0x6813C9), red from +0, green from +0x100,
 *               blue from +0x200 */
#define D3D_PDEVICE      0x0029DFB8u
#define RAMP_INDEX       0x240Cu
#define RAMP_BASE        (0x1C28u + 0x1DCu)
#define RAMP_BYTES       0x300u
#define GUEST_RAM_BYTES  0x04000000u

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MANHUNT_GAMMA");
        on = !(e && *e == '0');
    }
    return on;
}

int xbox_gamma_ramp(uint8_t r[256], uint8_t g[256], uint8_t b[256])
{
    static uint32_t logged_hash, logs;
    if (!enabled()) return 0;
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t dev;
    memcpy(&dev, mem + D3D_PDEVICE, 4);
    dev &= 0x7FFFFFFFu;                              /* the device lives in the 0x80000000 window or low RAM */
    if (!dev || dev >= GUEST_RAM_BYTES || dev + RAMP_BASE + 2 * RAMP_BYTES > GUEST_RAM_BYTES) return 0;
    uint32_t index;
    memcpy(&index, mem + dev + RAMP_INDEX, 4);
    const uint8_t *ramp = mem + dev + RAMP_BASE + (index & 1u) * RAMP_BYTES;
    int identity = 1, set = 0;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 256; i++) {
        r[i] = ramp[i]; g[i] = ramp[0x100 + i]; b[i] = ramp[0x200 + i];
        identity &= r[i] == i && g[i] == i && b[i] == i;
        set |= r[i] | g[i] | b[i];
        hash = (hash ^ r[i] ^ (uint32_t)g[i] << 8 ^ (uint32_t)b[i] << 16) * 16777619u;
    }
    if (hash != logged_hash && logs < 20) {
        logged_hash = hash;
        logs++;
        printf("[MH-GAMMA] ramp %u: r 0,64,128,192,255 = %u,%u,%u,%u,%u g = %u,%u,%u,%u,%u b = %u,%u,%u,%u,%u%s\n",
               (unsigned)(index & 1u), r[0], r[64], r[128], r[192], r[255], g[0], g[64], g[128], g[192], g[255],
               b[0], b[64], b[128], b[192], b[255], !set ? " (never set: not applied)" : identity ? " (identity)" : "");
    }
    return set && !identity;
}
