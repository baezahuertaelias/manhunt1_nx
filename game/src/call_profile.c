/*
 * MANHUNT_CALL_PROFILE=ON builds only (CMake option, default OFF): count every
 * direct call the lifted code makes and print, every 2 s, the guest functions
 * called most in that window. Investigation only.
 *
 * Hunter-tutorial fault: the hunter fails to appear and guest time per frame
 * rises from ~12 to ~45 ms at unchanged draw counts (reports/
 * SWITCH_RUN_2026-11-11.md). Comparing windows before and during that rise
 * names the code that starts running. Calls are counted, not timed: a hot
 * loop shows as a count jump, a single slow function does not.
 *
 * The lifted code defines RECOMP_ABI_CALL to call recomp_call_profile_hit(va)
 * before each direct call when compiled with -DRECOMP_CALL_PROFILE.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#include "recomp_types.h"

#define SLOTS 65536u                     /* > 4x the ~14,800 lifted functions */
#define TOP   24

static _Atomic uint32_t s_va[SLOTS];
static _Atomic uint32_t s_count[SLOTS];
static uint32_t s_last[SLOTS];           /* dumper only */
static _Atomic uint32_t s_hits;
static atomic_flag s_dumping = ATOMIC_FLAG_INIT;
static uint64_t s_next_ns, s_t0_ns;      /* dumper only */

/* Call sites of the functions that start running when the hunter fails to
 * appear (RenderWare intersection tests, tool/intsec/rtintsec.c, and their
 * neighbours). The caller pushes its return address just before the hooked
 * call, so MEM32(g_esp) names the call site. */
static const uint32_t k_watch[] = { 0x000DC770u, 0x000E2F50u, 0x000E2430u,
                                    0x000D9620u, 0x000D9D20u, 0x000DCDA0u };
#define WATCH (sizeof k_watch / sizeof k_watch[0])
#define SITES 512u
static _Atomic uint32_t s_site_key[SITES];   /* return address mixed with callee */
static _Atomic uint32_t s_site_ret[SITES], s_site_fn[SITES], s_site_count[SITES];
static uint32_t s_site_last[SITES];          /* dumper only */

static void site_hit(uint32_t fn, uint32_t ret)
{
    uint32_t key = ret ^ (fn * 0x9E3779B1u), i = (key * 2654435761u) >> 23;
    if (!key) key = 1;
    for (uint32_t probe = 0; probe < SITES; probe++, i = (i + 1) & (SITES - 1)) {
        uint32_t cur = atomic_load_explicit(&s_site_key[i], memory_order_relaxed);
        if (cur == key) break;
        if (cur == 0) {
            uint32_t expected = 0;
            if (atomic_compare_exchange_strong(&s_site_key[i], &expected, key)) {
                atomic_store_explicit(&s_site_ret[i], ret, memory_order_relaxed);
                atomic_store_explicit(&s_site_fn[i], fn, memory_order_relaxed);
                break;
            }
            if (expected == key) break;
        }
    }
    atomic_fetch_add_explicit(&s_site_count[i], 1, memory_order_relaxed);
}

/* Every call site above 256 calls in the window: callee<-return address:count. */
static void dump_sites(void)
{
    char line[1024];
    int len = snprintf(line, sizeof line, "[MH-CALLSITES]");
    for (uint32_t i = 0; i < SITES; i++) {
        uint32_t c = atomic_load_explicit(&s_site_count[i], memory_order_relaxed);
        uint32_t d = c - s_site_last[i];
        s_site_last[i] = c;
        if (d < 256 || len <= 0 || (size_t)len >= sizeof line) continue;
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X<-%08X:%u",
                        atomic_load_explicit(&s_site_fn[i], memory_order_relaxed),
                        atomic_load_explicit(&s_site_ret[i], memory_order_relaxed), d);
    }
    fprintf(stderr, "%s\n", line);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void dump(uint64_t t)
{
    uint32_t top_i[TOP], top_d[TOP];
    uint64_t total = 0;
    int n = 0, k;
    char line[1024];
    int len;

    for (uint32_t i = 0; i < SLOTS; i++) {
        uint32_t c = atomic_load_explicit(&s_count[i], memory_order_relaxed);
        uint32_t d = c - s_last[i];
        s_last[i] = c;
        if (!d) continue;
        total += d;
        if (n < TOP) { top_i[n] = i; top_d[n] = d; n++; }
        else if (d <= top_d[TOP - 1]) continue;
        else { top_i[TOP - 1] = i; top_d[TOP - 1] = d; }
        for (k = n - 1; k > 0 && top_d[k] > top_d[k - 1]; k--) {
            uint32_t ti = top_i[k], td = top_d[k];
            top_i[k] = top_i[k - 1]; top_d[k] = top_d[k - 1];
            top_i[k - 1] = ti; top_d[k - 1] = td;
        }
    }
    len = snprintf(line, sizeof line, "[MH-CALLS] t=%.1fs calls %llu |",
                   (double)(t - s_t0_ns) / 1e9, (unsigned long long)total);
    for (k = 0; k < n && len > 0 && (size_t)len < sizeof line; k++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X:%u",
                        atomic_load_explicit(&s_va[top_i[k]], memory_order_relaxed), top_d[k]);
    fprintf(stderr, "%s\n", line);
    dump_sites();
}

void recomp_call_profile_hit(uint32_t va)
{
    uint32_t i = (va * 2654435761u) >> 16;
    for (uint32_t probe = 0; probe < SLOTS; probe++, i = (i + 1) & (SLOTS - 1)) {
        uint32_t cur = atomic_load_explicit(&s_va[i], memory_order_relaxed);
        if (cur == va) break;
        if (cur == 0) {
            uint32_t expected = 0;
            if (atomic_compare_exchange_strong(&s_va[i], &expected, va) || expected == va)
                break;
        }
    }
    atomic_fetch_add_explicit(&s_count[i], 1, memory_order_relaxed);
    for (uint32_t w = 0; w < WATCH; w++)
        if (va == k_watch[w]) { site_hit(va, MEM32(g_esp)); break; }

    if ((atomic_fetch_add_explicit(&s_hits, 1, memory_order_relaxed) & 0xFFFFu) == 0 &&
        !atomic_flag_test_and_set(&s_dumping)) {
        uint64_t t = now_ns();
        if (!s_t0_ns) { s_t0_ns = t; s_next_ns = t + 2000000000ull; }
        else if (t >= s_next_ns) { s_next_ns = t + 2000000000ull; dump(t); }
        atomic_flag_clear(&s_dumping);
    }
}
