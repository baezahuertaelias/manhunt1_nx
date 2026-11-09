/* Host test of game/src/xbox_gamma.c: it must read the ramp the title's SetGammaRamp left, pick the buffer the device
 * marks as current, and leave the picture alone when no ramp is set, the ramp is the identity, or the device pointer
 * is not in RAM. No console and no game data. MANHUNT_GAMMA=0 is read once per process and is not tested here.
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I ../../game/src ../../game/src/xbox_gamma.c test_gamma.c -o /tmp/g && /tmp/g
 * Scope: the reading only. Whether these offsets hold for the title is from reading its lifted code, and whether the
 * picture is then right is a console question. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xbox_gamma.h"

static uint8_t *ram;
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)ram; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)

enum { DEV = 0x00200000, RAMPS = DEV + 0x1C28 + 0x1DC };

static void put32(uint32_t va, uint32_t v) { memcpy(ram + va, &v, 4); }

int main(void)
{
    uint8_t r[256], g[256], b[256];
    ram = calloc(1, 0x04000000 + 0x4000);                   /* 16 KiB past RAM, readable, for the bounds case */
    if (!ram) return 2;
    unsetenv("MANHUNT_GAMMA");

    CHECK(!xbox_gamma_ramp(r, g, b));                       /* no device yet */
    put32(0x0029DFB8, 0x80000000u | DEV);                   /* D3D_pDevice, in the 0x80000000 window */
    CHECK(!xbox_gamma_ramp(r, g, b));                       /* device, but no ramp ever set: all zero */

    for (int i = 0; i < 256; i++) {                         /* buffer 0: identity; buffer 1: a curve */
        ram[RAMPS + i] = ram[RAMPS + 0x100 + i] = ram[RAMPS + 0x200 + i] = (uint8_t)i;
        ram[RAMPS + 0x300 + i] = (uint8_t)(i / 2 + 64);
        ram[RAMPS + 0x400 + i] = (uint8_t)(255 - i);
        ram[RAMPS + 0x500 + i] = (uint8_t)(i | 1);
    }
    put32(DEV + 0x240C, 0);
    CHECK(!xbox_gamma_ramp(r, g, b));                       /* identity: nothing to apply */
    put32(DEV + 0x240C, 1);
    CHECK(xbox_gamma_ramp(r, g, b));
    CHECK(r[0] == 64 && r[255] == 191 && r[128] == 128);    /* red from +0 of buffer 1 */
    CHECK(g[0] == 255 && g[255] == 0);                      /* green from +0x100 */
    CHECK(b[0] == 1 && b[254] == 255);                      /* blue from +0x200 */
    put32(DEV + 0x240C, 3);                                 /* only the low bit selects the buffer */
    CHECK(xbox_gamma_ramp(r, g, b) && r[0] == 64);

    /* A device whose ramps would lie past RAM: refused, even though the bytes there look like a ramp. */
    memset(ram + 0x04000000, 0x55, 0x4000);
    put32(0x0029DFB8, 0x03FFF000u);
    CHECK(!xbox_gamma_ramp(r, g, b));

    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", fails);
    free(ram);
    return fails != 0;
}
