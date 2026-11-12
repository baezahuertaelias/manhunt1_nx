/*
 * MANHUNT_CALL_PROFILE=ON builds only (CMake option, default OFF): a sampling
 * time profile of the lifted code. Investigation only.
 *
 * The lifted code defines RECOMP_ABI_CALL to call recomp_call_profile_hit(va)
 * before each direct call and recomp_call_profile_ret() after it returns
 * (patch 0034) when compiled with -DRECOMP_CALL_PROFILE. Each guest thread
 * keeps a shadow stack of those calls. A sampler thread reads every stack
 * once per millisecond and, every 10 s, prints per thread the functions on
 * top (exclusive: their own code is running) and anywhere on the stack
 * (inclusive: they or their callees are running).
 *
 * Indirect calls are not hooked: time in a function reached through one
 * counts for the nearest directly called frame. A thread blocked in a wait
 * still has a stack, so waits show as the function that made them.
 * Frames left by a non-local exit (SEH unwind) are dropped at the next call
 * whose guest esp is at or above theirs.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>

#include "recomp_types.h"

#define DEPTH     256u
#define THREADS   16u
#define TOP       20
#define SLOTS     16384u                  /* distinct functions per window */
#define WINDOW_MS 10000u

typedef struct {
    _Atomic uint32_t depth;               /* may exceed DEPTH; read clamped */
    uint32_t va[DEPTH];
    uint32_t esp[DEPTH];
} Shadow;

static Shadow s_shadow[THREADS];
static _Atomic uint32_t s_nthreads;
static RECOMP_TLS Shadow *t_shadow;
static RECOMP_TLS int t_untracked;        /* more than THREADS threads */
static atomic_flag s_started = ATOMIC_FLAG_INIT;

static void *sampler(void *arg);

static Shadow *shadow_register(void)
{
    uint32_t i = atomic_fetch_add_explicit(&s_nthreads, 1, memory_order_relaxed);
    if (i >= THREADS) { t_untracked = 1; return NULL; }
    if (!atomic_flag_test_and_set(&s_started)) {
        pthread_t t;
        if (pthread_create(&t, NULL, sampler, NULL) == 0) pthread_detach(t);
    }
    return t_shadow = &s_shadow[i];
}

void recomp_call_profile_hit(uint32_t va)
{
    Shadow *s = t_shadow;
    if (!s) {
        if (t_untracked || !(s = shadow_register())) return;
    }
    uint32_t d = atomic_load_explicit(&s->depth, memory_order_relaxed);
    uint32_t e = g_esp;
    if (d > DEPTH) d = DEPTH;
    while (d && s->esp[d - 1] <= e) d--;
    if (d < DEPTH) { s->va[d] = va; s->esp[d] = e; }
    atomic_store_explicit(&s->depth, d + 1, memory_order_release);
}

void recomp_call_profile_ret(void)
{
    Shadow *s = t_shadow;
    if (!s) return;
    uint32_t d = atomic_load_explicit(&s->depth, memory_order_relaxed);
    if (d) atomic_store_explicit(&s->depth, d - 1, memory_order_release);
}

/* Sampler-only tallies: an open-addressing map from va to two counts. */
typedef struct { uint32_t va, excl, incl, seen; } Tally;
static Tally s_tally[THREADS][SLOTS];
static uint32_t s_samples[THREADS];
static uint32_t s_root[THREADS];
static uint32_t s_stamp;                  /* dedups inclusive within a sample */

static Tally *tally(uint32_t t, uint32_t va)
{
    uint32_t i = (va * 2654435761u) >> 18;
    for (uint32_t probe = 0; probe < SLOTS; probe++, i = (i + 1) & (SLOTS - 1)) {
        Tally *x = &s_tally[t][i];
        if (x->va == va) return x;
        if (!x->va) { x->va = va; return x; }
    }
    return NULL;
}

static void print_top(uint32_t t, int incl, double secs)
{
    Tally *top[TOP];
    int n = 0;
    for (uint32_t i = 0; i < SLOTS; i++) {
        Tally *x = &s_tally[t][i];
        uint32_t v = incl ? x->incl : x->excl;
        if (!x->va || !v) continue;
        if (n == TOP && v <= (incl ? top[TOP - 1]->incl : top[TOP - 1]->excl)) continue;
        int k = n < TOP ? n++ : TOP - 1;
        top[k] = x;
        for (; k > 0 && v > (incl ? top[k - 1]->incl : top[k - 1]->excl); k--) {
            Tally *tmp = top[k]; top[k] = top[k - 1]; top[k - 1] = tmp;
        }
    }
    char line[1024];
    int len = snprintf(line, sizeof line, "[MH-PROF] t=%.0fs thread %u %s |",
                       secs, t, incl ? "incl" : "excl");
    for (int k = 0; k < n && len > 0 && (size_t)len < sizeof line; k++)
        len += snprintf(line + len, sizeof line - (size_t)len, " %08X:%.1f%%", top[k]->va,
                        100.0 * (incl ? top[k]->incl : top[k]->excl) / s_samples[t]);
    fprintf(stderr, "%s\n", line);
}

static void *sampler(void *arg)
{
    (void)arg;
    struct timespec ms = { 0, 1000000 };
    uint32_t ticks = 0, windows = 0;
    for (;;) {
        nanosleep(&ms, NULL);
        uint32_t n = atomic_load_explicit(&s_nthreads, memory_order_relaxed);
        if (n > THREADS) n = THREADS;
        for (uint32_t t = 0; t < n; t++) {
            Shadow *s = &s_shadow[t];
            uint32_t d = atomic_load_explicit(&s->depth, memory_order_acquire);
            if (d > DEPTH) d = DEPTH;
            if (!d) continue;
            Tally *x = tally(t, s->va[d - 1]);
            if (!x) continue;
            x->excl++;
            s_root[t] = s->va[0];
            s_samples[t]++;
            s_stamp++;
            for (uint32_t k = 0; k < d; k++) {
                Tally *y = tally(t, s->va[k]);
                if (y && y->seen != s_stamp) { y->seen = s_stamp; y->incl++; }
            }
        }
        if (++ticks < WINDOW_MS) continue;
        ticks = 0;
        double secs = (double)++windows * WINDOW_MS / 1000.0;
        for (uint32_t t = 0; t < n; t++) {
            if (s_samples[t] < 100) continue;
            fprintf(stderr, "[MH-PROF] t=%.0fs thread %u root %08X samples %u\n",
                    secs, t, s_root[t], s_samples[t]);
            print_top(t, 0, secs);
            print_top(t, 1, secs);
        }
        memset(s_tally, 0, sizeof s_tally);
        memset(s_samples, 0, sizeof s_samples);
    }
    return NULL;
}
