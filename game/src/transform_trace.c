/*
 * MANHUNT_TRANSFORM_TRACE=1 (default off): report the first invalid matrix
 * the title hands to D3D's SetTransform. Investigation only; it never
 * repairs or drops a matrix.
 *
 * Court-fight black screen: the NV2A modelview methods (0x0480..) carry
 * 0xFFC00000 once the fight starts (reports/BLACK_SCREEN_2026-11-11.md).
 * 0x0028DA30 copies the caller's 16 floats into device+0xC60+state*64, the
 * view and world matrices that 0x002946B0 later multiplies into that packet.
 * A nonfinite (or absurdly large) source here means the title produced it;
 * finite sources mean the fault lies between this copy and the packet.
 *
 * Hook: 0x0028DA30, cdecl (state, const D3DMATRIX *). Linked with
 * -Wl,--wrap=sub_0028DA30: it is defined in recomp_0057.c and all ten call
 * sites are in other object files. The guest return address at entry names
 * the call site.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "recomp_types.h"

extern void __real_sub_0028DA30(void);

#define TRACE_STATES 16u

static int trace_on(void)
{
    static atomic_int on = -1;
    int v = atomic_load_explicit(&on, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("MANHUNT_TRANSFORM_TRACE");
        v = e && e[0] == '1';
        atomic_store(&on, v);
        if (v)
            fprintf(stderr, "[MH-XFORM] SetTransform source check on (MANHUNT_TRANSFORM_TRACE)\n");
    }
    return v;
}

/* 0 finite and ordinary, 1 nonfinite, 2 finite but beyond 1e15. */
static int matrix_class(const uint32_t m[16], unsigned *at)
{
    int i;
    for (i = 0; i < 16; i++) {
        float f;
        memcpy(&f, &m[i], sizeof f);
        if ((m[i] & 0x7f800000u) == 0x7f800000u) { *at = (unsigned)i; return 1; }
        if (f > 1e15f || f < -1e15f) { *at = (unsigned)i; return 2; }
    }
    return 0;
}

static void dump(const char *what, uint32_t state, uint32_t ret, uint32_t src,
                 unsigned at, const uint32_t m[16], uint32_t calls)
{
    char line[512];
    int n = snprintf(line, sizeof line,
                     "[MH-XFORM] %s state %u caller 0x%08X src 0x%08X component %u call %u:",
                     what, state, ret, src, at, calls);
    for (int i = 0; i < 16 && n > 0 && (size_t)n < sizeof line; i++)
        n += snprintf(line + n, sizeof line - (size_t)n, " %08X", m[i]);
    fprintf(stderr, "%s\n", line);
}

void __wrap_sub_0028DA30(void)
{
    static atomic_uint s_logged, s_calls;
    static uint32_t s_last[TRACE_STATES][16];
    static uint32_t s_last_ret[TRACE_STATES];
    static uint8_t s_have[TRACE_STATES], s_bad[TRACE_STATES];

    if (trace_on()) {
        uint32_t ret = MEM32(g_esp);
        uint32_t state = MEM32(g_esp + 4);
        uint32_t src = MEM32(g_esp + 8);
        uint32_t calls = atomic_fetch_add_explicit(&s_calls, 1, memory_order_relaxed) + 1;
        uint32_t m[16];
        unsigned at = 0;
        int i, cls;

        for (i = 0; i < 16; i++)
            m[i] = MEM32(src + 4u * (uint32_t)i);
        cls = matrix_class(m, &at);
        if (state < TRACE_STATES) {
            if (cls && !s_bad[state] &&
                atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 24) {
                s_bad[state] = 1;
                dump(cls == 1 ? "FIRST NONFINITE" : "FIRST HUGE", state, ret, src, at, m, calls);
                if (s_have[state])
                    dump("previous finite", state, s_last_ret[state], 0, 0, s_last[state], calls);
                char stack[256];
                int n = snprintf(stack, sizeof stack, "[MH-XFORM] guest stack:");
                for (i = 3; i < 19 && n > 0 && (size_t)n < sizeof stack; i++)
                    n += snprintf(stack + n, sizeof stack - (size_t)n, " %08X", MEM32(g_esp + 4u * (uint32_t)i));
                fprintf(stderr, "%s\n", stack);
            } else if (!cls) {
                if (s_bad[state] &&
                    atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 24)
                    fprintf(stderr, "[MH-XFORM] state %u finite again, caller 0x%08X call %u\n",
                            state, ret, calls);
                s_bad[state] = 0;
                memcpy(s_last[state], m, sizeof m);
                s_last_ret[state] = ret;
                s_have[state] = 1;
            }
        }
    }
    __real_sub_0028DA30();
}
