/* Synthetic capability measurements; no guest assets or runtime semantics claim. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#define PAGE 0x1000u
static void report(const char *s) { printf("%s\n", s); svcOutputDebugString(s, strlen(s)); }
static int query(void *addr, u32 *type, u32 *perm) {
    MemoryInfo mi; u32 pi; Result rc = svcQueryMemory(&mi, &pi, (u64)addr);
    if (R_FAILED(rc)) return 0;
    *type = mi.type; *perm = mi.perm; return 1;
}
int main(void) {
    char msg[320]; int ok = 1, mapped = 0; Handle h = INVALID_HANDLE;
    const size_t size = 2 * PAGE;
    void *src = aligned_alloc(PAGE, size);
    virtmemLock();
    void *base = virtmemFindAslr(size, PAGE);
    VirtmemReservation *rv = base ? virtmemAddReservation(base, size) : NULL;
    virtmemUnlock();
    if (!src || !rv) { report("HZ-MEMORY-CAP FAIL allocation"); return 1; }
    memset(src, 0, size);
    Result create = svcCreateCodeMemory(&h, src, size);
    Result map = R_SUCCEEDED(create) ? svcControlCodeMemory(h, CodeMapOperation_MapOwner, base, size, Perm_Rw) : create;
    snprintf(msg, sizeof msg, "HZ-MEMORY-CAP create=%08X map=%08X", create, map); report(msg);
    if (R_FAILED(map)) { ok = 0; goto done; }
    mapped = 1;
    ((volatile u32 *)base)[0] = 0x1234ABCDu;
    ((volatile u32 *)((u8 *)base + PAGE))[0] = 0x5678EF90u;
    Result partial = svcControlCodeMemory(h, CodeMapOperation_UnmapOwner, (u8 *)base + PAGE, PAGE, 0);
    u32 t0 = 0, p0 = 0, t1 = 0, p1 = 0;
    int q = query(base, &t0, &p0) && query((u8 *)base + PAGE, &t1, &p1);
    snprintf(msg, sizeof msg, "HZ-MEMORY-CAP partial_unmap=%08X query=%d type=%u/%u perm=%u/%u", partial, q, t0, t1, p0, p1); report(msg);
    if (!q || R_SUCCEEDED(partial)) { ok = 0; goto done; }
    Result none = svcSetMemoryPermission((u8 *)base + PAGE, PAGE, Perm_None);
    q = query(base, &t0, &p0) && query((u8 *)base + PAGE, &t1, &p1);
    snprintf(msg, sizeof msg, "HZ-MEMORY-CAP partial_none=%08X query=%d type=%u/%u perm=%u/%u", none, q, t0, t1, p0, p1); report(msg);
    if (!q) { ok = 0; goto done; }
    if (R_SUCCEEDED(none)) {
        Result rw = svcSetMemoryPermission((u8 *)base + PAGE, PAGE, Perm_Rw);
        snprintf(msg, sizeof msg, "HZ-MEMORY-CAP restore_rw=%08X", rw); report(msg);
        if (R_FAILED(rw)) { ok = 0; goto done; }
    }
    Result unmap = svcControlCodeMemory(h, CodeMapOperation_UnmapOwner, base, size, 0);
    snprintf(msg, sizeof msg, "HZ-MEMORY-CAP whole_unmap=%08X", unmap); report(msg);
    if (R_FAILED(unmap)) { ok = 0; goto done; }
    mapped = 0;
    Result remap = svcControlCodeMemory(h, CodeMapOperation_MapOwner, base, size, Perm_Rw);
    if (R_SUCCEEDED(remap)) mapped = 1;
    int preserved = R_SUCCEEDED(remap) && ((volatile u32 *)base)[0] == 0x1234ABCDu
        && ((volatile u32 *)((u8 *)base + PAGE))[0] == 0x5678EF90u;
    snprintf(msg, sizeof msg, "HZ-MEMORY-CAP whole_remap=%08X preserved=%d", remap, preserved); report(msg);
    ok &= preserved;
done:
    if (mapped) {
        Result unmap = svcControlCodeMemory(h, CodeMapOperation_UnmapOwner, base, size, 0);
        snprintf(msg, sizeof msg, "HZ-MEMORY-CAP cleanup_unmap=%08X", unmap); report(msg);
        if (R_FAILED(unmap)) ok = 0; else mapped = 0;
    }
    /* Never release a backing allocation while its owner mapping is live. */
    if (!mapped) {
        if (h != INVALID_HANDLE) svcCloseHandle(h);
        free(src);
        virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock();
    }
    report(ok ? "HZ-MEMORY-CAP PASS measured code-memory granularity and remap" : "HZ-MEMORY-CAP FAIL capability probe incomplete");
    return !ok;
}
