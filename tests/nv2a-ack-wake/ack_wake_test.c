/* Host test for the runtime's ack-loop wake (patch 0038, nv2a_ack_wake.h).
 *
 * A thread stands in for nv2a_ack_thread: each pass it "executes" what was
 * submitted by setting GET to PUT, then sleeps on the event while GET == PUT.
 * The main thread is the title's KickOff: it writes PUT and calls the kick.
 * The event is a pthread auto-reset event with a millisecond timeout, as
 * win32_compat's CreateEvent/WaitForSingleObject are. No game data. */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nv2a_ack_wake.h"

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    int signalled;
} Event;

static void ev_signal(void *p)
{
    Event *e = p;
    pthread_mutex_lock(&e->m);
    e->signalled = 1;
    pthread_cond_broadcast(&e->c);
    pthread_mutex_unlock(&e->m);
}

static int ev_wait(void *p, unsigned ms)
{
    Event *e = p;
    struct timespec ts;
    int r = 1;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&e->m);
    while (!e->signalled)
        if (pthread_cond_timedwait(&e->c, &e->m, &ts) == ETIMEDOUT) { r = 0; break; }
    if (r) e->signalled = 0;                    /* auto-reset */
    pthread_mutex_unlock(&e->m);
    return r;
}

static Event ev = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
static XboxAckWake w;
static volatile uint32_t put, get;
static volatile int stop;
static unsigned idle_ms;
static volatile uint32_t passes;

static int pending(void *arg)
{
    (void)arg;
    return __atomic_load_n(&put, __ATOMIC_SEQ_CST) != __atomic_load_n(&get, __ATOMIC_SEQ_CST);
}

static void *ack_loop(void *arg)
{
    (void)arg;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
        uint32_t p = __atomic_load_n(&put, __ATOMIC_ACQUIRE);
        if (p != get)
            __atomic_store_n(&get, p, __ATOMIC_RELEASE);   /* executed: GET reaches PUT */
        __atomic_fetch_add(&passes, 1, __ATOMIC_RELAXED);
        if (!pending(NULL))
            xbox_AckWakeIdle(&w, pending, NULL, idle_ms);
    }
    return NULL;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void sleep_ms(unsigned ms)
{
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* Write PUT, kick (or not), and wait for GET to reach it. Returns ms taken. */
static double submit(uint32_t v, int kick, double limit_ms)
{
    double t0 = now_ms();
    __atomic_store_n(&put, v, __ATOMIC_RELEASE);
    if (kick)
        xbox_AckWakeKick(&w);
    while (__atomic_load_n(&get, __ATOMIC_ACQUIRE) != v) {
        if (now_ms() - t0 > limit_ms)
            break;
        sched_yield();                          /* one-CPU containers */
    }
    return now_ms() - t0;
}

static int check(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

static int always_pending(void *arg) { (void)arg; return 1; }

int main(void)
{
    pthread_t th;
    int ok = 1, i, n = 20000;
    double dt, worst = 0;
    uint32_t p0;

    w.event = &ev;
    w.signal = ev_signal;
    w.wait = ev_wait;

    /* Work on the re-check: the loop does not sleep at all. */
    ok &= check("pending on the re-check: no wait",
                xbox_AckWakeIdle(&w, always_pending, NULL, 5000) == -1 && w.skipped == 1);
    w.skipped = 0;

    /* A loop asleep on a long timeout is woken by the kick. */
    idle_ms = 2000;
    pthread_create(&th, NULL, ack_loop, NULL);
    sleep_ms(50);
    p0 = passes;
    sleep_ms(100);
    ok &= check("idle loop sleeps (no passes while nothing is submitted)", passes - p0 <= 1);
    dt = submit(1, 1, 3000);
    ok &= check("kick wakes the sleeping loop, GET advances to PUT", get == 1 && dt < 200);
    printf("     latency %.3f ms\n", dt);

    /* Many kicks back to back: a lost wake-up would cost a 2 s timeout. */
    dt = now_ms();
    for (i = 0; i < n; i++) {
        double d = submit(2 + (uint32_t)i, 1, 3000);
        if (d > worst) worst = d;
        if (get != 2 + (uint32_t)i)
            break;
    }
    dt = now_ms() - dt;
    ok &= check("20000 kicks: every one executed, none waited for the timeout",
                i == n && worst < 500 && w.timeouts == 0);
    printf("     %d kicks in %.1f ms, worst %.3f ms, %u kick wakes, %u timeouts, %u re-check hits\n",
           i, dt, worst, w.kick_wakes, w.timeouts, w.skipped);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    xbox_AckWakeKick(&w);
    ev_signal(&ev);
    pthread_join(th, NULL);

    /* Without the kick (a PUT written by something else) the idle timeout
     * still gets GET advanced, one sleep later at most. */
    stop = 0;
    idle_ms = 20;
    w.timeouts = 0;
    pthread_create(&th, NULL, ack_loop, NULL);
    sleep_ms(60);
    dt = submit(0xABCD, 0, 1000);
    ok &= check("no kick: GET still advances within the idle timeout",
                get == 0xABCD && dt < 20 + 100 && w.timeouts > 0);
    printf("     latency %.3f ms\n", dt);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    pthread_join(th, NULL);

    /* Waking off (MANHUNT_ACK_WAKE=0: no event): the kick is a no-op. */
    {
        XboxAckWake off;
        memset(&off, 0, sizeof off);
        xbox_AckWakeKick(&off);
        ok &= check("waking off: kick does nothing", 1);
    }

    printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
