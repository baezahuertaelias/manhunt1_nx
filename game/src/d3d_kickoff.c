/*
 * Replacement for Manhunt's statically linked D3D push-buffer kick
 * (0x00292500, XDK 5849 LTCG; stdcall, one argument: the device).
 *
 * The original sets PFB 0x100410 bit 16 (write-combine flush) and spins until
 * the GPU clears it, then writes DMA_PUT. Here the "GPU" clearing it is the
 * runtime's NV2A ack loop (xbox_memory_layout.c), which passes every ~200 us
 * plus whatever push-buffer walk it is in. Profile run 20261112-113535-prof:
 * the guest thread spent 32.5% of its samples in this function, which does
 * nothing else that takes time but that spin. Flip holds (the one reason the
 * ack is withheld, to pace FLIP_STALL on the guest vblank) totalled 60-170 ms
 * per 10 s window, so the spin was almost all ack latency.
 *
 * Memory is coherent here, so the flush itself has nothing to wait for:
 * the bit is acknowledged at once unless a flip is held, and then the wait is
 * the original's (the ack loop clears it on release), so frame pacing is
 * unchanged. MANHUNT_KICK_ACK=0 restores the original spin for A/B runs.
 *
 * Everything else is the original: the device+8 bit 13 path that writes
 * PUT/GET to the fake register block, PUT from device+0x770 when flags bit 2
 * is set, and the optional wait-for-idle when 0x0029BB00 is nonzero.
 * eax/ecx/edx are left as they were (stdcall scratch, no caller reads them).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "recomp_types.h"

#define D3D_PDEVICE       0x0029DFB8u
#define D3D_KICK_IDLE     0x0029BB00u     /* nonzero: wait for idle after a kick */
#define PFB_FLUSH         0x00100410u
#define PFB_FLUSH_BUSY    0x00010000u

extern int nv2a_pb_exec_flip_held(void);

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* D3D KickOff inner (CDevice *dev) */
void sub_00292500(void)
{
    static int s_fast = -1;
    static uint64_t s_report_ns, s_wait_ns;
    static uint32_t s_kicks, s_waits;
    uint32_t dev = MEM32(g_esp + 4);
    uint32_t flags = MEM32(dev + 8);
    uint32_t put = (flags & 4u) ? MEM32(dev + 0x770) : MEM32(dev);

    if (flags & 0x2000u) {
        MEM32(dev + 0x1C20) = 0x0029DFB0u - 0x40u;
        uint32_t p = MEM32(dev) & 0x0FFFFFFFu;
        MEM32(MEM32(dev + 0x1C20) + 0x40) = p;
        MEM32(MEM32(dev + 0x1C20) + 0x44) = p;
        MEM32(MEM32(dev + 0x30)) = MEM32(dev + 0x2C) - 2u;
        MEM32(dev + 0x1DE4) = MEM32(dev + 0x2478);
        g_esp += 8;                            /* ret 4 */
        return;
    }

    if (s_fast < 0) {
        const char *e = getenv("MANHUNT_KICK_ACK");
        s_fast = !(e && e[0] == '0');
        fprintf(stderr, "[D3D] kickoff flush ack: %s\n", s_fast ? "immediate unless a flip is held" : "original spin");
    }
    uint32_t d3d = MEM32(D3D_PDEVICE);
    uint32_t regs = MEM32(d3d + 0x1C28);
    uint32_t *flush = (uint32_t *)&MEM32(regs + PFB_FLUSH);
    __atomic_fetch_or(flush, PFB_FLUSH_BUSY, __ATOMIC_SEQ_CST);
    s_kicks++;
    if (s_fast && !nv2a_pb_exec_flip_held()) {
        __atomic_fetch_and(flush, ~PFB_FLUSH_BUSY, __ATOMIC_SEQ_CST);
    } else if (__atomic_load_n(flush, __ATOMIC_ACQUIRE) & PFB_FLUSH_BUSY) {
        uint64_t t0 = now_ns();
        while (__atomic_load_n(flush, __ATOMIC_ACQUIRE) & PFB_FLUSH_BUSY)
            ;
        s_wait_ns += now_ns() - t0;
        s_waits++;
    }

    MEM32(MEM32(dev + 0x1C20) + 0x40) = put & 0x0FFFFFFFu;
    if (MEM32(D3D_KICK_IDLE)) {
        uint32_t nv = MEM32(d3d + 0x934);
        while (MEM32(nv + 0x3240) != MEM32(nv + 0x3244) || MEM32(nv + 0x400700))
            ;
        MEM32(dev + 8) |= 0x2000u;
    }

    uint64_t t = now_ns();
    if (!s_report_ns) s_report_ns = t;
    if (t - s_report_ns >= 10000000000ull) {
        fprintf(stderr, "[D3D] kickoff: %u in %.1f s, %u waited %.1f ms\n",
                s_kicks, (double)(t - s_report_ns) / 1e9, s_waits, (double)s_wait_ns / 1e6);
        s_report_ns = t;
        s_kicks = s_waits = 0;
        s_wait_ns = 0;
    }
    g_esp += 8;                                /* ret 4 */
}
