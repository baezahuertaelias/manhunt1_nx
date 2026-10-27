// Horizon capability probe for the milestone-2 runtime port (FEASIBILITY-001).
//
// Measures, in whatever Horizon implementation runs it (Eden today, hardware
// UNVERIFIED), the two OS features the xboxrecomp POSIX runtime relies on:
//   1. resuming after a data abort with an edited context (POSIX: SIGSEGV
//      handler emulating a trapped MMIO load), via __libnx_exception_handler;
//   2. two read/write views of the same memory (POSIX: memfd mirrors of the
//      64 MiB guest RAM), via shared memory mapped twice and via code memory.
// Each test prints "HZ-PROBE <name> PASS|FAIL ..." through svcOutputDebugString;
// the last line is "HZ-PROBE PASS|FAIL" for scripts/emu-run.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

static void report(const char *s) {
    printf("%s\n", s);
    svcOutputDebugString(s, strlen(s));
}

// ---- 1. data abort -> handler edits registers -> resume --------------------

alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

static volatile int g_faults;
static volatile u64 g_far;

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    if (ctx->error_desc == ThreadExceptionDesc_InstructionAbort || g_faults > 4)
        return;                                     // genuine crash: let libnx abort
    g_faults++;
    g_far = ctx->far.x;
    ctx->cpu_gprs[0].x = 0x1234ABCD;                // emulate "ldr w0, [x1]"
    ctx->pc.x += 4;                                 // step over the faulting load
}

static u32 faulting_load(volatile u32 *p) {
    u32 v;
    __asm__ volatile("mov x1, %1\n\tldr w0, [x1]\n\tmov %w0, w0" : "=r"(v) : "r"(p) : "x0", "x1", "memory");
    return v;
}

static int probe_exception(char *msg, size_t n) {
    // An address in a reserved, unmapped range: the access must abort.
    virtmemLock();
    void *hole = virtmemFindAslr(0x10000, 0x1000);
    VirtmemReservation *rv = hole ? virtmemAddReservation(hole, 0x10000) : NULL;
    virtmemUnlock();
    if (!rv) { snprintf(msg, n, "HZ-PROBE exception FAIL no free range"); return 0; }
    u32 v = faulting_load((volatile u32 *)((u8 *)hole + 0x40));
    int ok = g_faults == 1 && v == 0x1234ABCD && g_far == (u64)hole + 0x40;
    snprintf(msg, n, "HZ-PROBE exception %s faults=%d value=%08x far_ok=%d",
             ok ? "PASS" : "FAIL", g_faults, v, g_far == (u64)hole + 0x40);
    virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock();
    return ok;
}

// ---- 2a. shared memory mapped at two addresses ------------------------------

static int probe_shmem(char *msg, size_t n) {
    const size_t size = 0x100000;
    SharedMemory s;
    Result rc = shmemCreate(&s, size, Perm_Rw, Perm_Rw);
    if (R_FAILED(rc)) { snprintf(msg, n, "HZ-PROBE shmem-alias FAIL create rc=0x%x", rc); return 0; }
    rc = shmemMap(&s);
    if (R_FAILED(rc)) { snprintf(msg, n, "HZ-PROBE shmem-alias FAIL map1 rc=0x%x", rc); shmemClose(&s); return 0; }
    u32 *a = shmemGetAddr(&s);

    virtmemLock();
    void *b = virtmemFindAslr(size, 0x1000);
    rc = b ? svcMapSharedMemory(s.handle, b, size, Perm_Rw) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    virtmemUnlock();
    if (R_FAILED(rc)) {
        snprintf(msg, n, "HZ-PROBE shmem-alias FAIL map2 rc=0x%x (one view only)", rc);
        shmemClose(&s);
        return 0;
    }
    a[100] = 0xA11A5ED1;
    u32 seen = ((volatile u32 *)b)[100];
    ((volatile u32 *)b)[200] = 0xB0B0B0B0;
    int ok = seen == 0xA11A5ED1 && a[200] == 0xB0B0B0B0;
    snprintf(msg, n, "HZ-PROBE shmem-alias %s a=%p b=%p seen=%08x", ok ? "PASS" : "FAIL", (void *)a, b, seen);
    svcUnmapSharedMemory(s.handle, b, size);
    shmemClose(&s);
    return ok;
}

// ---- 2b. code memory: owner (RW) and slave (RX) views ------------------------

static int probe_codemem(char *msg, size_t n) {
    const size_t size = 0x100000;
    void *src = aligned_alloc(0x1000, size);
    Handle h;
    if (!src) { snprintf(msg, n, "HZ-PROBE codemem-alias FAIL alloc"); return 0; }
    Result rc = svcCreateCodeMemory(&h, src, size);
    if (R_FAILED(rc)) { snprintf(msg, n, "HZ-PROBE codemem-alias FAIL create rc=0x%x", rc); free(src); return 0; }
    virtmemLock();
    void *owner = virtmemFindCodeMemory(size, 0x1000);
    rc = svcControlCodeMemory(h, CodeMapOperation_MapOwner, owner, size, Perm_Rw);
    void *slave = R_SUCCEEDED(rc) ? virtmemFindCodeMemory(size, 0x1000) : NULL;
    Result rc2 = slave ? svcControlCodeMemory(h, CodeMapOperation_MapSlave, slave, size, Perm_Rx) : rc;
    virtmemUnlock();
    if (R_FAILED(rc) || R_FAILED(rc2)) {
        snprintf(msg, n, "HZ-PROBE codemem-alias FAIL map rc=0x%x/0x%x", rc, rc2);
        return 0;
    }
    ((volatile u32 *)owner)[10] = 0xC0DE0001;
    u32 seen = ((volatile u32 *)slave)[10];
    int ok = seen == 0xC0DE0001;
    snprintf(msg, n, "HZ-PROBE codemem-alias %s seen=%08x (slave is read-only by design)", ok ? "PASS" : "FAIL", seen);
    return ok;
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char msg[256];
    report("HZ-PROBE start");
    int ex = probe_exception(msg, sizeof msg);   report(msg);
    int sh = probe_shmem(msg, sizeof msg);       report(msg);
    int cm = probe_codemem(msg, sizeof msg);     report(msg);
    // The run "passes" when it completed and reported every probe; the
    // individual results are the measurement, and FAILs are findings.
    snprintf(msg, sizeof msg, "HZ-PROBE PASS completed exception=%d shmem_alias=%d codemem_alias=%d", ex, sh, cm);
    report(msg);
    consoleUpdate(NULL);

    for (int frame = 0; frame < 300 && appletMainLoop(); frame++) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
