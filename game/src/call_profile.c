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

/* Intersection inputs. At the hook the caller has pushed its arguments and
 * return address, so arg k is MEM32(g_esp + 4k). 0x000E2F50 is called as
 * (query, element) from the loop at 0x000DC8B0, whose query floats are
 * compared with a frame position; 0x000E2430's layout is unknown. For both,
 * the first eight floats behind each of the first two pointer arguments are
 * checked. The first nonfinite input names the call site and the guest stack;
 * nothing is repaired. */
static int guest_data(uint32_t p)
{
    return (p >= 0x00010000u && p + 32u <= 0x04000000u) ||
           (p >= 0x80000000u && p + 32u <= 0x84000000u);
}

static void check_intsec_args(uint32_t va)
{
    static _Atomic uint32_t s_bad, s_logged;
    uint32_t ret = MEM32(g_esp);
    for (uint32_t k = 1; k <= 2; k++) {
        uint32_t p = MEM32(g_esp + 4u * k), w[8];
        int bad = -1;
        if (!guest_data(p)) continue;
        for (int j = 0; j < 8; j++) {
            w[j] = MEM32(p + 4u * (uint32_t)j);
            if (bad < 0 && (w[j] & 0x7f800000u) == 0x7f800000u) bad = j;
        }
        if (bad < 0) continue;
        uint32_t n = atomic_fetch_add_explicit(&s_bad, 1, memory_order_relaxed) + 1;
        if ((n & (n - 1)) != 0 || atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) >= 24)
            return;                                       /* log occurrences 1, 2, 4, 8, ... */
        char line[512];
        int len = snprintf(line, sizeof line,
                           "[MH-INTSEC] nonfinite #%u fn %08X<-%08X arg%u 0x%08X float %d:",
                           n, va, ret, k, p, bad);
        for (int j = 0; j < 8 && len > 0 && (size_t)len < sizeof line; j++)
            len += snprintf(line + len, sizeof line - (size_t)len, " %08X", w[j]);
        len += snprintf(line + len, sizeof line - (size_t)len, " | stack");
        for (uint32_t s = 3; s < 19 && len > 0 && (size_t)len < sizeof line; s++)
            len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(g_esp + 4u * s));
        fprintf(stderr, "%s\n", line);
        return;
    }
}

/* Writer search. The first nonfinite intersection input was the `this`
 * (ecx) of 0x000DCB90, called per entity from 0x000DA7A4. Objects seen there
 * while still finite are watched; every lifted call then re-reads their
 * first word, so the first call that sees one turned nonfinite brackets the
 * write between the previous call this thread made and this one. Logged once
 * per object with its last finite and its new contents. */
#define NAN_WATCH 48u
static _Atomic uint32_t s_nw_obj[NAN_WATCH];
static _Atomic uint32_t s_nw_state[NAN_WATCH];    /* 0 free, 1 watching, 2 reported */
static uint32_t s_nw_last[NAN_WATCH][8];          /* racy snapshot; evidence only */
static RECOMP_TLS uint32_t t_prev_va, t_prev_ret;
static _Atomic uint32_t s_nw_printed;

static int nonfinite(uint32_t w) { return (w & 0x7f800000u) == 0x7f800000u; }

static void nan_watch_add(uint32_t obj)
{
    /* The main guest stack is reused constantly; watching it only reports
     * unrelated frames (run 20261112-003951-all). */
    if (!guest_data(obj) || nonfinite(MEM32(obj)) ||
        (obj >= 0x00780000u && obj < 0x00F80000u))
        return;
    for (uint32_t i = 0; i < NAN_WATCH; i++)
        if (atomic_load_explicit(&s_nw_obj[i], memory_order_relaxed) == obj)
            return;
    for (uint32_t i = 0; i < NAN_WATCH; i++) {
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong(&s_nw_state[i], &expected, 1)) {
            atomic_store_explicit(&s_nw_obj[i], obj, memory_order_relaxed);
            if (atomic_fetch_add_explicit(&s_nw_printed, 1, memory_order_relaxed) < 64)
                fprintf(stderr, "[MH-NANWATCH] watching 0x%08X (slot %u)\n", obj, i);
            return;
        }
    }
    /* Full: rotate, so an entity created late (the hunter) is still watched. */
    static _Atomic uint32_t s_rr;
    uint32_t i = atomic_fetch_add_explicit(&s_rr, 1, memory_order_relaxed) % NAN_WATCH;
    atomic_store_explicit(&s_nw_state[i], 0, memory_order_relaxed);
    atomic_store_explicit(&s_nw_obj[i], obj, memory_order_relaxed);
    for (int j = 0; j < 8; j++) s_nw_last[i][j] = MEM32(obj + 4u * (uint32_t)j);
    atomic_store_explicit(&s_nw_state[i], 1, memory_order_relaxed);
    if (atomic_fetch_add_explicit(&s_nw_printed, 1, memory_order_relaxed) < 64)
        fprintf(stderr, "[MH-NANWATCH] watching 0x%08X (slot %u, rotated)\n", obj, i);
}

static void nan_watch_check(uint32_t va, uint32_t ret)
{
    static _Atomic uint32_t s_ticks;
    int refresh = (atomic_fetch_add_explicit(&s_ticks, 1, memory_order_relaxed) & 255u) == 0;
    for (uint32_t i = 0; i < NAN_WATCH; i++) {
        if (atomic_load_explicit(&s_nw_state[i], memory_order_relaxed) != 1) continue;
        uint32_t obj = atomic_load_explicit(&s_nw_obj[i], memory_order_relaxed);
        if (!obj) continue;
        if (!nonfinite(MEM32(obj))) {
            if (refresh)
                for (int j = 0; j < 8; j++) s_nw_last[i][j] = MEM32(obj + 4u * (uint32_t)j);
            continue;
        }
        uint32_t expected = 1;
        if (!atomic_compare_exchange_strong(&s_nw_state[i], &expected, 2)) continue;
        char line[640];
        int len = snprintf(line, sizeof line,
                           "[MH-NANWATCH] 0x%08X nonfinite between %08X(ret %08X) and %08X(ret %08X) | was",
                           obj, t_prev_va, t_prev_ret, va, ret);
        for (int j = 0; j < 8 && len > 0 && (size_t)len < sizeof line; j++)
            len += snprintf(line + len, sizeof line - (size_t)len, " %08X", s_nw_last[i][j]);
        len += snprintf(line + len, sizeof line - (size_t)len, " | now");
        for (int j = 0; j < 8 && len > 0 && (size_t)len < sizeof line; j++)
            len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(obj + 4u * (uint32_t)j));
        len += snprintf(line + len, sizeof line - (size_t)len, " | stack");
        for (uint32_t s = 1; s < 13 && len > 0 && (size_t)len < sizeof line; s++)
            len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(g_esp + 4u * s));
        fprintf(stderr, "%s\n", line);
    }
}

/* Run 20261112-003951-all: the watched query turned nonfinite inside
 * RwV3dTransformPoints (0x001E3140 -> SSE 0x001E25E0, plain mul/add) called
 * from 0x000DE570, which transforms each 0x38-byte element's two points by
 * one matrix. Check that call's input point and matrix as it is made, and
 * watch both while finite so a later run brackets their writer. */
/* Matrices used by 0x000DE570's collision transforms (the hunter's among
 * them). The velocity watch below only follows objects whose frame is one of
 * these, so ordinary moving props cannot rotate the hunter out of the slots. */
#define ENTITY_MTX 32u
static _Atomic uint32_t s_entity_mtx[ENTITY_MTX];

static void remember_entity_matrix(uint32_t mtx)
{
    for (uint32_t i = 0; i < ENTITY_MTX; i++) {
        uint32_t cur = atomic_load_explicit(&s_entity_mtx[i], memory_order_relaxed);
        if (cur == mtx) return;
        if (!cur) {
            uint32_t expected = 0;
            if (atomic_compare_exchange_strong(&s_entity_mtx[i], &expected, mtx) || expected == mtx)
                return;
        }
    }
}

static int is_entity_matrix(uint32_t mtx)
{
    for (uint32_t i = 0; i < ENTITY_MTX; i++)
        if (atomic_load_explicit(&s_entity_mtx[i], memory_order_relaxed) == mtx) return 1;
    return 0;
}

static void check_transform_args(uint32_t ret)
{
    static _Atomic uint32_t s_logged;
    uint32_t out = MEM32(g_esp + 4), in = MEM32(g_esp + 8);
    uint32_t count = MEM32(g_esp + 12), mtx = MEM32(g_esp + 16);
    int bad_in = -1, bad_m = -1;
    if (!guest_data(in) || !guest_data(mtx) || !guest_data(mtx + 32))
        return;
    for (int j = 0; j < 3; j++)
        if (bad_in < 0 && nonfinite(MEM32(in + 4u * (uint32_t)j))) bad_in = j;
    for (int j = 0; j < 16; j++)
        if (bad_m < 0 && nonfinite(MEM32(mtx + 4u * (uint32_t)j))) bad_m = j;
    if (bad_in < 0 && bad_m < 0) {
        if (ret == 0x000DE5ABu || ret == 0x000DE5BFu) {
            nan_watch_add(in);
            /* Run 20261112-004702-all: this matrix (RwMatrix: right, up, at,
             * pos at +0x30) kept a finite rotation while pos went 0xFFC00000,
             * so watch the position x, not word 0. */
            nan_watch_add(mtx + 0x30u);
            remember_entity_matrix(mtx);
        }
        return;
    }
    if (atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) >= 16)
        return;
    char line[700];
    int len = snprintf(line, sizeof line,
                       "[MH-XFORMPTS] ret %08X out %08X in %08X count %u matrix %08X bad in %d matrix %d | in",
                       ret, out, in, count, mtx, bad_in, bad_m);
    for (int j = 0; j < 3 && len > 0 && (size_t)len < sizeof line; j++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(in + 4u * (uint32_t)j));
    len += snprintf(line + len, sizeof line - (size_t)len, " | matrix");
    for (int j = 0; j < 16 && len > 0 && (size_t)len < sizeof line; j++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(mtx + 4u * (uint32_t)j));
    len += snprintf(line + len, sizeof line - (size_t)len, " | stack");
    for (uint32_t s = 5; s < 17 && len > 0 && (size_t)len < sizeof line; s++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X", MEM32(g_esp + 4u * s));
    fprintf(stderr, "%s\n", line);
}

/* Run 20261112-005348-all: the hunter matrix position turned 0xFFC00000 in
 * 0x00169370, a per-object step: if (obj+0x34) { tmp = obj+0x28 (velocity)
 * scaled by the float at 0x309F28 (dt); frame (obj+8) matrix pos (+0x30) +=
 * tmp }. Log a nonfinite velocity or dt at that call and watch every
 * object's velocity while finite, to bracket its writer. */
static void check_velocity_step(void)
{
    static _Atomic uint32_t s_logged;
    uint32_t obj = g_ecx, dt = MEM32(0x309F28u);
    if (!guest_data(obj) || !MEM32(obj + 0x34u))
        return;
    uint32_t v[3] = { MEM32(obj + 0x28u), MEM32(obj + 0x2Cu), MEM32(obj + 0x30u) };
    if (!nonfinite(v[0]) && !nonfinite(v[1]) && !nonfinite(v[2]) && !nonfinite(dt)) {
        if (is_entity_matrix(MEM32(obj + 8u)))
            nan_watch_add(obj + 0x28u);
        return;
    }
    if (atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 16)
        fprintf(stderr, "[MH-VELSTEP] obj %08X ret %08X velocity %08X %08X %08X dt %08X | obj+0..+0x40:"
                " %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
                obj, MEM32(g_esp), v[0], v[1], v[2], dt,
                MEM32(obj), MEM32(obj + 4), MEM32(obj + 8), MEM32(obj + 12),
                MEM32(obj + 16), MEM32(obj + 20), MEM32(obj + 24), MEM32(obj + 28),
                MEM32(obj + 32), MEM32(obj + 36), MEM32(obj + 40), MEM32(obj + 44),
                MEM32(obj + 48), MEM32(obj + 52), MEM32(obj + 56), MEM32(obj + 60));
}

/* Run 20261112-005919-all: the hunter physics object's velocity (+0x28) went
 * from zero to 0xFFC00000 through SetVelocity (0x001691C0) called from the
 * movement update 0x000BC610, whose vector is V3dAdd (0x00020950) of a local
 * and entity data, after RwV3dTransformVectors (0x001E31D0) on two paths.
 * At those call sites, dump the first three pointer arguments' first four
 * floats the first times any is nonfinite, with the last finite dump of the
 * same site, so one failing run shows which input goes bad first. */
static const uint32_t k_trace_sites[] = {
    0x000BC7CFu, 0x000BC867u,   /* RwV3dTransformVectors(out, in, count, matrix) */
    0x000BC8DAu,                /* V3dAdd(out, a, b) */
    0x000BC98Cu, 0x000BC9BBu,   /* SetVelocity(this=ecx, v) */
};
#define TRACE_SITES (sizeof k_trace_sites / sizeof k_trace_sites[0])
static uint32_t s_site_good[TRACE_SITES][12];   /* racy snapshot; evidence only */
static _Atomic uint32_t s_site_bad_logs[TRACE_SITES];

static void dump_site_args(uint32_t ret)
{
    uint32_t si = 0, w[12], args[3];
    int bad = 0;
    while (si < TRACE_SITES && k_trace_sites[si] != ret) si++;
    if (si == TRACE_SITES) return;
    for (uint32_t k = 0; k < 3; k++) {
        args[k] = MEM32(g_esp + 4u * (k + 1));
        for (uint32_t j = 0; j < 4; j++) {
            w[k * 4 + j] = guest_data(args[k]) ? MEM32(args[k] + 4u * j) : 0;
            if (guest_data(args[k]) && nonfinite(w[k * 4 + j])) bad = 1;
        }
    }
    if (!bad) { memcpy(s_site_good[si], w, sizeof w); return; }
    if (atomic_fetch_add_explicit(&s_site_bad_logs[si], 1, memory_order_relaxed) >= 4) return;
    char line[700];
    int len = snprintf(line, sizeof line, "[MH-MOVE] ret %08X ecx %08X args %08X %08X %08X | now",
                       ret, g_ecx, args[0], args[1], args[2]);
    for (int j = 0; j < 12 && len > 0 && (size_t)len < sizeof line; j++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X", w[j]);
    len += snprintf(line + len, sizeof line - (size_t)len, " | last finite");
    for (int j = 0; j < 12 && len > 0 && (size_t)len < sizeof line; j++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X", s_site_good[si][j]);
    fprintf(stderr, "%s\n", line);
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
    {
        uint32_t ret = MEM32(g_esp);
        if (va == 0x000DCB90u) nan_watch_add(g_ecx);
        if (va == 0x001E3140u) check_transform_args(ret);
        if (va == 0x00169370u) check_velocity_step();
        if (ret >= 0x000BC610u && ret < 0x000BCA00u) dump_site_args(ret);
        nan_watch_check(va, ret);
        t_prev_va = va;
        t_prev_ret = ret;
    }
    for (uint32_t w = 0; w < WATCH; w++)
        if (va == k_watch[w]) {
            site_hit(va, MEM32(g_esp));
            if (va == 0x000E2F50u || va == 0x000E2430u) check_intsec_args(va);
            break;
        }

    if ((atomic_fetch_add_explicit(&s_hits, 1, memory_order_relaxed) & 0xFFFFu) == 0 &&
        !atomic_flag_test_and_set(&s_dumping)) {
        uint64_t t = now_ns();
        if (!s_t0_ns) { s_t0_ns = t; s_next_ns = t + 2000000000ull; }
        else if (t >= s_next_ns) { s_next_ns = t + 2000000000ull; dump(t); }
        atomic_flag_clear(&s_dumping);
    }
}
