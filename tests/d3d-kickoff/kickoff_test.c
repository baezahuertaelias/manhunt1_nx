/* Focused regression test for game/src/d3d_kickoff.c. The ack mode is read
 * once per process, so "fast" (default) and "orig" (MANHUNT_KICK_ACK=0) are
 * separate runs. The "GPU" that clears PFB_FLUSH busy is a thread. No game
 * data involved. */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

ptrdiff_t g_xbox_mem_offset;
__thread uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static int flip_held;
int nv2a_pb_exec_flip_held(void) { return __atomic_load_n(&flip_held, __ATOMIC_ACQUIRE); }
void sub_00292500(void);
/* The ack-loop wake (patch 0038): counted, and PUT as each wake saw it. */
static int wakes;
static uint32_t put_at_wake[8];
static uint32_t dma_va;
void xbox_Nv2aAckWake(void)
{
    if (wakes < 8)
        put_at_wake[wakes] = *(volatile uint32_t *)((uintptr_t)dma_va + g_xbox_mem_offset);
    __atomic_fetch_add(&wakes, 1, __ATOMIC_SEQ_CST);
}

#define M32(a) (*(volatile uint32_t *)((uintptr_t)(a) + g_xbox_mem_offset))
#define PDEVICE 0x0029DFB8u
#define KICK_IDLE 0x0029BB00u
#define DEV 0x00100000u
#define D3D 0x00110000u
#define REGS 0x00200000u
#define DMA 0x00130000u
#define NV 0x00140000u
#define FLUSH (REGS + 0x00100410u)
#define BUSY 0x00010000u
#define STACK 0x00300000u
#define DELAY_MS 150

static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }

static int helper_mode;          /* bit0 clear BUSY, bit1 equalise 0x3244 to 0x3240, bit2 clear 0x400700, bit3 equalise 0x3240 to 0x3244 */
static uint32_t put_seen_at_release;
static void *helper(void *arg) {
    (void)arg;
    struct timespec t = { 0, DELAY_MS * 1000000L };
    nanosleep(&t, NULL);
    put_seen_at_release = M32(DMA);
    if (helper_mode & 1) __atomic_fetch_and((uint32_t *)((uintptr_t)FLUSH + g_xbox_mem_offset), ~BUSY, __ATOMIC_SEQ_CST);
    if (helper_mode & 2) M32(NV + 0x3244) = M32(NV + 0x3240);
    if (helper_mode & 4) M32(NV + 0x400700) = 0;
    if (helper_mode & 8) M32(NV + 0x3240) = M32(NV + 0x3244);
    return NULL;
}

static void setup(uint32_t flags, uint32_t put0, uint32_t put770) {
    memset((void *)g_xbox_mem_offset, 0, 0x00800000);
    M32(PDEVICE) = D3D; M32(D3D + 0x1C28) = REGS; M32(D3D + 0x934) = NV;
    M32(DEV) = put0; M32(DEV + 8) = flags; M32(DEV + 0x770) = put770;
    M32(DEV + 0x1C20) = DMA - 0x40;                        /* PUT lands at DMA (base + 0x40) */
    M32(FLUSH) = 0x00000001u;                              /* unrelated bit must survive */
    flip_held = 0;
    wakes = 0; dma_va = DMA; memset(put_at_wake, 0, sizeof put_at_wake);
    g_esp = STACK; g_esp -= 4; M32(g_esp) = DEV; g_esp -= 4; M32(g_esp) = 0xDEADBEEF;
    g_eax = 0xA1; g_ecx = 0xC1; g_edx = 0xD1;
}

static int check(const char *name, int ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); return ok; }

/* One call; returns elapsed ms. helper_mode selects who releases it. */
static long long call(void) {
    pthread_t th; int started = 0;
    if (helper_mode) { pthread_create(&th, NULL, helper, NULL); started = 1; }
    long long t0 = ms_now();
    sub_00292500();
    long long dt = ms_now() - t0;
    if (started) pthread_join(th, NULL);
    return dt;
}

int main(int argc, char **argv) {
    int orig = argc > 1 && strcmp(argv[1], "orig") == 0;
    uint8_t *m = mmap(NULL, 0x00800000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return 2;
    g_xbox_mem_offset = (ptrdiff_t)m;
    int ok = 1; long long dt; uint32_t esp0;

    if (!orig) {
        setup(0, 0xA0001234u, 0x0BADBAD0u); esp0 = g_esp; helper_mode = 0;
        dt = call();
        ok &= check("fast, no flip: returns at once", dt < DELAY_MS / 3);
        ok &= check("fast, no flip: busy bit cleared, other flush bits kept", M32(FLUSH) == 1u);
        ok &= check("fast, no flip: PUT = dev[0] & 0x0FFFFFFF", M32(DMA) == 0x1234u);
        ok &= check("fast: ret 4 pops return + arg", g_esp == esp0 + 8);
        ok &= check("fast: eax/ecx/edx untouched", g_eax == 0xA1 && g_ecx == 0xC1 && g_edx == 0xD1);
        ok &= check("fast: ack loop woken once, after PUT was written", wakes == 1 && put_at_wake[0] == 0x1234u);

        setup(4, 0x1111u, 0x0BADBAD0u); helper_mode = 0; call();
        ok &= check("flags bit 2: PUT from dev+0x770", M32(DMA) == 0x0BADBAD0u);

        setup(0, 0x2222u, 0); helper_mode = 1; flip_held = 1;
        dt = call();
        ok &= check("flip held: waits for the ack loop to clear busy", dt >= DELAY_MS - 10 && dt < 3000);
        ok &= check("flip held: busy cleared on return, PUT written after", (M32(FLUSH) & BUSY) == 0 && M32(DMA) == 0x2222u);
        ok &= check("flip held: PUT not written before the flush was acknowledged", put_seen_at_release == 0);
        ok &= check("flip held: ack loop woken before the spin and after PUT",
                    wakes == 2 && put_at_wake[0] == 0 && put_at_wake[1] == 0x2222u);
    } else {
        setup(0, 0x3333u, 0); helper_mode = 1; flip_held = 0;
        dt = call();
        ok &= check("MANHUNT_KICK_ACK=0: spins until the ack loop clears busy", dt >= DELAY_MS - 10 && dt < 3000);
        ok &= check("MANHUNT_KICK_ACK=0: PUT written after the ack", put_seen_at_release == 0 && M32(DMA) == 0x3333u);
        ok &= check("MANHUNT_KICK_ACK=0: ack loop woken before the spin and after PUT",
                    wakes == 2 && put_at_wake[0] == 0 && put_at_wake[1] == 0x3333u);
    }

    /* Wait-for-idle after the kick (guest flag 0x0029BB00), both modes. */
    setup(0, 0x4444u, 0); M32(KICK_IDLE) = 1; M32(NV + 0x3240) = 7; M32(NV + 0x3244) = 3; helper_mode = 2;
    if (orig) helper_mode = 3;   /* orig also spins on busy: the helper clears both */
    dt = call();
    ok &= check("idle wait: blocks until NV regs match", dt >= DELAY_MS - 10 && dt < 3000);
    /* fast: busy needs no wait, so PUT is out before the idle spin. orig: the
     * helper releases busy and idle together, so only the final PUT is checkable. */
    ok &= check(orig ? "idle wait: PUT written" : "idle wait: PUT already written while waiting",
                orig ? M32(DMA) == 0x4444u : put_seen_at_release == 0x4444u);
    ok &= check("idle wait: sets device flag 0x2000", (M32(DEV + 8) & 0x2000u) != 0);

    /* Each idle condition alone must hold the kick: 0x3240 lagging, 0x400700 busy. */
    setup(0, 0x4445u, 0); M32(KICK_IDLE) = 1; M32(NV + 0x3240) = 3; M32(NV + 0x3244) = 7; helper_mode = 8 | (orig ? 1 : 0);
    dt = call();
    ok &= check("idle wait: holds while only 0x3240 differs", dt >= DELAY_MS - 10 && dt < 3000);
    setup(0, 0x4446u, 0); M32(KICK_IDLE) = 1; M32(NV + 0x3240) = 5; M32(NV + 0x3244) = 5; M32(NV + 0x400700) = 1; helper_mode = 4 | (orig ? 1 : 0);
    dt = call();
    ok &= check("idle wait: holds while only 0x400700 is nonzero", dt >= DELAY_MS - 10 && dt < 3000);
    /* PUT masking: high bits must be dropped on both PUT sources and in the 0x2000 path. */
    setup(4, 0, 0xF5000678u); helper_mode = 0; if (orig) { helper_mode = 1; }
    call();
    ok &= check("flags bit 2: PUT masked to 0x0FFFFFFF", M32(DMA) == 0x05000678u);
    setup(0x2000, 0xE0000789u, 0); M32(DEV + 0x30) = 0x00150000u; helper_mode = 0;
    call();
    ok &= check("flags 0x2000: PUT masked to 0x0FFFFFFF", M32(0x0029DFB0u) == 0x00000789u && M32(0x0029DFB4u) == 0x00000789u);

    /* Device flag 0x2000: original early path, flush register untouched. */
    setup(0x2000, 0x5555u, 0); M32(DEV + 0x2478) = 0x77u; M32(DEV + 0x2C) = 10; M32(DEV + 0x30) = 0x00150000u; esp0 = g_esp; helper_mode = 0;
    call();
    ok &= check("flags 0x2000: early return, flush register untouched", M32(FLUSH) == 1u);
    ok &= check("flags 0x2000: no ack-loop wake (fake register block)", wakes == 0);
    ok &= check("flags 0x2000: DMA block rewired, dev+0x1DE4 copied, ret 4",
                M32(DEV + 0x1C20) == 0x0029DFB0u - 0x40u && M32(0x0029DFB0u) == 0x5555u && M32(DEV + 0x1DE4) == 0x77u
                && M32(0x00150000u) == 8u && g_esp == esp0 + 8);

    printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
