/* Focused regression test for game/src/d3d_fence.c (LEVEL-LOAD-001).
 * With RECOMP_PB_EXEC set, D3D_BlockOnTime must not return before the GPU
 * semaphore reaches the fence (the executor here is a thread that releases
 * it after DELAY_MS, well past the old 250 ms bound). Without it, the old
 * bounded wait is kept. No game data involved. */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

ptrdiff_t g_xbox_mem_offset;
__thread uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static int kickoffs, sem_registered;
void sub_00291F20(void) { kickoffs++; g_esp += 4 + 4; }   /* ret 4 */
void nv2a_pb_set_semaphore_target(uint32_t va) { (void)va; sem_registered++; }
void sub_00292200(void);

#define DEV 0x00100000u
#define SEM 0x00200000u
#define STACK 0x00300000u
#define M32(a) (*(volatile uint32_t *)((uintptr_t)(a) + g_xbox_mem_offset))
static int delay_ms;
static uint32_t release_to;
static void *executor(void *arg) {
    (void)arg;
    struct timespec t = { delay_ms / 1000, (delay_ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
    M32(SEM) = release_to;
    return NULL;
}
static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }

static int run_case(const char *name, int exec, uint32_t fence, uint32_t newest, uint32_t sem0, int delay, long long min_ms, long long max_ms, uint32_t want_eax) {
    pthread_t th;
    if (exec) setenv("RECOMP_PB_EXEC", "1", 1); else unsetenv("RECOMP_PB_EXEC");
    M32(0x0029DFB8u) = DEV; M32(DEV + 0x30) = SEM; M32(DEV + 0x2C) = newest; M32(SEM) = sem0;
    delay_ms = delay; release_to = fence;
    g_esp = STACK; g_esp -= 4; M32(g_esp) = 2; g_esp -= 4; M32(g_esp) = fence; g_esp -= 4; M32(g_esp) = 0xDEADBEEF;
    uint32_t esp0 = g_esp;
    pthread_create(&th, NULL, executor, NULL);
    long long t0 = ms_now();
    sub_00292200();
    long long dt = ms_now() - t0;
    pthread_join(th, NULL);
    int ok = dt >= min_ms && dt <= max_ms && g_eax == want_eax && g_esp == esp0 + 12;
    printf("%s %s: returned after %lld ms (want %lld..%lld), eax=%u (want %u), esp delta=%u\n",
           ok ? "PASS" : "FAIL", name, dt, min_ms, max_ms, g_eax, want_eax, g_esp - esp0);
    return ok;
}

int main(int argc, char **argv) {
    int noexec = argc > 1 && strcmp(argv[1], "noexec") == 0;
    uint8_t *m = mmap(NULL, 0x00400000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return 2;
    g_xbox_mem_offset = (ptrdiff_t)m;
    int ok = 1;
    if (noexec) {   /* the switch is read once per process, so its own run */
        ok &= run_case("no executor: keeps 250 ms bound", 0, 400, 405, 397, 1500, 240, 600, 397);
        ok &= kickoffs == 0;
        printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
        return ok ? 0 : 1;
    }
    /* Executor lags 1500 ms (> old 250 ms bound and > 2 s report interval not needed). */
    ok &= run_case("executor: waits for lagging semaphore (old fence)", 1, 100, 105, 97, 1500, 1500, 3000, 100);
    ok &= run_case("executor: waits past 2 s report interval", 1, 200, 200, 197, 2600, 2600, 4000, 200);
    ok &= run_case("executor: semaphore released at once", 1, 300, 300, 297, 0, 0, 200, 300);
    /* Original 0x0029221E: a fence outside (semaphore, newest] returns at once,
     * without a kickoff; with the executor this used to wait forever. */
    ok &= run_case("executor: fence beyond newest returns at once", 1, 500, 490, 488, 1500, 0, 200, 488);
    ok &= run_case("executor: fence already passed returns at once", 1, 480, 490, 485, 1500, 0, 200, 485);
    printf("kickoffs=%d (want 2: fence == newest in cases 2 and 3) sem_registered=%d\n", kickoffs, sem_registered);
    /* Kickoff happens when fence == newest: cases 2 and 3. */
    ok &= kickoffs == 2;
    printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
