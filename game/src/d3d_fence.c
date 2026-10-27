/*
 * Replacement for Manhunt's statically linked D3D_BlockOnTime (XDK 5849, LTCG).
 *
 * Without a GPU, D3D waits forever for the fence it just submitted: the title
 * stopped after its first Present (reports/FEASIBILITY.md). xboxrecomp's
 * documented fix for D3D8LTCG titles (docs/technical/d3d8ltcg-device-context.md
 * "When Something Does Read the Pushbuffer") is used as is:
 *   1. hand the GPU semaphore (*(device + 0x30)) to the pushbuffer executor,
 *      which writes BACK_END_WRITE_SEMAPHORE_RELEASE values there;
 *   2. do what the original does first: kick off the pushbuffer when the
 *      fence is the newest one (== device + 0x2C);
 *   3. wait until the semaphore reaches the fence, bounded at 250 ms.
 * Requires RECOMP_PB_EXEC=1 (the executor); without it the bound always runs
 * out, which is logged.
 * Differences from the original: the device+0x19FC callback and the
 * make-space branch (flags & 8) are not reproduced, a missed fence ends in a
 * logged 250 ms timeout instead of a stall, and eax returns the semaphore.
 *
 * Addresses: 0x0029DFB8 D3D_pDevice (.XTLID 0x80003); 0x00292200 BlockOnTime
 * (time, flags), stdcall ret 8, 8 callers; 0x00291F20 KickOff(flags), loads
 * the device itself. 0x0029222F is the original return site after its
 * KickOff call, used as the guest return address.
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "recomp_types.h"

#define D3D_PDEVICE        0x0029DFB8u
#define KICKOFF_RET_SITE   0x0029222Fu
#define WAIT_BOUND_NS      250000000ll

extern void sub_00291F20(void);                       /* D3D KickOff (lifted) */
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

/* D3D_BlockOnTime(DWORD time, DWORD flags) */
void sub_00292200(void)
{
    static uint32_t s_sem_registered;
    static unsigned s_timeouts;
    uint32_t fence = MEM32(g_esp + 4);
    uint32_t dev = MEM32(D3D_PDEVICE);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;
    long long deadline;

    if (sem && sem != s_sem_registered) {
        nv2a_pb_set_semaphore_target(sem);
        s_sem_registered = sem;
        printf("[D3D] GPU semaphore at 0x%08X (device 0x%08X)\n", sem, dev);
    }
    if (dev && fence == MEM32(dev + 0x2C)) {
        uint32_t saved_esi = g_esi;
        PUSH32(g_esp, 0);                  /* KickOff(0) */
        PUSH32(g_esp, KICKOFF_RET_SITE);
        sub_00291F20();
        g_esi = saved_esi;
    }
    if (sem) {
        deadline = now_ns() + WAIT_BOUND_NS;
        while ((int32_t)(MEM32(sem) - fence) < 0) {
            struct timespec pause = {0, 100000};
            if (now_ns() >= deadline) {
                if (s_timeouts++ < 16)
                    fprintf(stderr, "[D3D] BlockOnTime: fence %u not reached in 250 ms (semaphore %u)\n",
                            fence, MEM32(sem));
                break;
            }
            nanosleep(&pause, NULL);
        }
        g_eax = MEM32(sem);
    } else {
        g_eax = 0;
    }
    g_esp += 4 + 8;                        /* ret 8 */
}
