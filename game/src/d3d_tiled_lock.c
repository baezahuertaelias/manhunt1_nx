/*
 * Switch only: D3D surface locks that ask for the tiled aperture get the
 * contiguous alias instead.
 *
 * Manhunt's statically linked D3D lock (0x00295EC0, stdcall, 4 stack
 * arguments, ret 0x10; callers 0x0028CE50, 0x0028CED0, 0x00291E60) ORs
 * 0xF0000000 into the surface pointer when the flags argument (4th, byte)
 * has 0x40, D3DLOCK_TILED (0x00295F03..0x00295F0C). It then stores pitch and
 * pointer at [esi] and [esi + 4]. The XMV movie decoder locks this way and
 * writes each frame through that pointer: run 24 faulted at guest 0xF0BEF000
 * in an MMX store at 0x00242CED (reports/AUDIO_APU.md).
 *
 * On the console, 0xF0000000 + P and 0x80000000 + P are the same bytes. The
 * host build maps the tiled aperture as a second view of the contiguous
 * window (xbox_memory_layout.c). Horizon cannot map memory twice, so the
 * tiled aperture is absent there ("tiled aperture at 0xF0000000 failed") and
 * any write through it faults. Handing back 0x80000000 + P is therefore what
 * the host build already does, expressed as an address instead of a mapping.
 * Not reproduced, here or on the host: the NV2A tile-region layout a real
 * tiled write would apply.
 *
 * Linked with -Wl,--wrap=sub_00295EC0: every caller of the lifted function is
 * in another object file (recomp_0057.c, recomp_dispatch.c), so all of them
 * reach this wrapper.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "recomp_types.h"

#define D3DLOCK_TILED   0x40u
#define TILED_BASE      0xF0000000u
#define CONTIG_BASE     0x80000000u
#define RAM_BYTES       0x04000000u

extern void __real_sub_00295EC0(void);

void __wrap_sub_00295EC0(void)
{
    static atomic_uint s_logged;
    uint32_t flags = MEM8(g_esp + 0x10);  /* 4th argument, read as the original does */
    uint32_t out = g_esi;                 /* callee-saved; the original writes [esi], [esi + 4] */
    uint32_t p;

    __real_sub_00295EC0();
    if (!(flags & D3DLOCK_TILED))
        return;
    p = MEM32(out + 4);
    if ((p & TILED_BASE) != TILED_BASE)
        return;
    /* Only the 64 MiB tiled aperture aliases contiguous RAM. A pointer in
     * the rest of the high-nibble range must not be turned into a bogus
     * contiguous pointer. Stop rather than conceal an invalid original result. */
    if (p - TILED_BASE >= RAM_BYTES) {
        fprintf(stderr, "[MH-D3D] FAIL tiled lock outside 64 MiB aperture: 0x%08X\n", p);
        abort();
    }
    MEM32(out + 4) = p - (TILED_BASE - CONTIG_BASE);
    if (atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 4) {
        fprintf(stderr, "[MH-D3D] tiled lock 0x%08X -> 0x%08X (no tiled aperture on Horizon)\n",
                p, p - (TILED_BASE - CONTIG_BASE));
    }
}
