#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include "win32_compat.h"
#include "switch.h"

#define PAGE 4096u
#define ARENA_SIZE (256u * 1024u * 1024u)
enum { FAIL_ALLOC = 1, FAIL_CREATE, FAIL_MAP, FAIL_UNMAP, FAIL_RESERVE, FAIL_QUERY };
static unsigned fail_kind, fail_at, hits[7], checks;
static uintptr_t arena;
static size_t requested, peak;
static struct { void *p; size_t size; } allocations[8192];
static VirtmemReservation *reservations;
static struct { void *data; size_t size; uintptr_t mapped; } handles[2048];
static unsigned live_handles, live_maps;
static DWORD error;
static void test_pause_create(void);
static int injection(unsigned kind)
{ return ++hits[kind] == fail_at && fail_kind == kind; }
static void *record_alloc(void *p, size_t size)
{
    if (!p) return NULL;
    for (unsigned i = 0; i < 8192; i++) if (!allocations[i].p) {
        allocations[i].p = p; allocations[i].size = size;
        requested += size; if (requested > peak) peak = requested;
        return p;
    }
    abort();
}
static void *test_malloc(size_t n) { return injection(FAIL_ALLOC) ? NULL : record_alloc(malloc(n), n); }
static void *test_calloc(size_t n, size_t size) { return injection(FAIL_ALLOC) ? NULL : record_alloc(calloc(n, size), n * size); }
static void *test_aligned_alloc(size_t align, size_t n) { return injection(FAIL_ALLOC) ? NULL : record_alloc(aligned_alloc(align, n), n); }
static void test_free(void *p)
{
    if (!p) return;
    for (unsigned i = 0; i < 8192; i++) if (allocations[i].p == p) {
        requested -= allocations[i].size; allocations[i].p = NULL; free(p); return;
    }
    abort();
}

Result svcQueryMemory(MemoryInfo *mi, u32 *pi, u64 address)
{
    (void)pi;
    if (injection(FAIL_QUERY)) return 1;
    uintptr_t a = address, lo = arena, end = arena + ARENA_SIZE;
    if (a < lo || a >= end) return 1;
    for (unsigned i = 1; i < 2048; i++) if (handles[i].mapped) {
        uintptr_t b = handles[i].mapped, e = b + handles[i].size;
        if (a >= b && a < e) { *mi = (MemoryInfo){b, handles[i].size, MemType_CodeWritable, Perm_Rw}; return 0; }
        if (b > a && b < end) end = b;
        if (e <= a && e > lo) lo = e;
    }
    *mi = (MemoryInfo){lo, end - lo, MemType_Unmapped, 0}; return 0;
}
Result svcCreateCodeMemory(Handle *out, void *src, size_t n)
{
    (void)src;
    test_pause_create();
    if (injection(FAIL_CREATE)) return 1;
    for (unsigned i = 1; i < 2048; i++) if (!handles[i].data) {
        void *data = malloc(n); if (!data) return 1;
        memset(data, 0xFF, n);
        handles[i].data = data; handles[i].size = n; *out = i; live_handles++; return 0;
    }
    return 1;
}
Result svcControlCodeMemory(Handle h, u32 operation, void *address, size_t n, u32 permissions)
{
    (void)permissions;
    if (!h || h >= 2048 || !handles[h].data || handles[h].size != n) return 1;
    uintptr_t a = (uintptr_t)address;
    if (a < arena || n > arena + ARENA_SIZE - a || (a & (PAGE - 1))) return 1;
    if (operation == CodeMapOperation_MapOwner) {
        if (injection(FAIL_MAP) || handles[h].mapped) return 1;
        for (unsigned i = 1; i < 2048; i++) if (handles[i].mapped && a < handles[i].mapped + handles[i].size && handles[i].mapped < a + n) return 1;
        void *p = mmap(address, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED) return 1;
        memcpy(p, handles[h].data, n); handles[h].mapped = a; live_maps++; return 0;
    }
    if (operation == CodeMapOperation_UnmapOwner) {
        if (injection(FAIL_UNMAP) || handles[h].mapped != a) return 1;
        memcpy(handles[h].data, address, n);
        if (mmap(address, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return 1;
        handles[h].mapped = 0; live_maps--; return 0;
    }
    return 1;
}
Result svcCloseHandle(Handle h)
{
    if (!h || h >= 2048 || !handles[h].data || handles[h].mapped) return 1;
    free(handles[h].data); memset(&handles[h], 0, sizeof handles[h]); live_handles--; return 0;
}
void virtmemLock(void) {}
void virtmemUnlock(void) {}
void *virtmemFindAslr(size_t n, size_t alignment)
{
    uintptr_t a = (arena + alignment - 1) & ~(uintptr_t)(alignment - 1);
    for (;;) {
        uintptr_t next = a;
        for (VirtmemReservation *r = reservations; r; r = r->next)
            if (a < r->base + r->size && r->base < a + n && r->base + r->size > next) next = r->base + r->size;
        if (next == a) return n <= arena + ARENA_SIZE - a ? (void *)a : NULL;
        a = (next + alignment - 1) & ~(uintptr_t)(alignment - 1);
    }
}
VirtmemReservation *virtmemAddReservation(void *p, size_t n)
{
    if (injection(FAIL_RESERVE)) return NULL;
    VirtmemReservation *r = malloc(sizeof *r); if (!r) return NULL;
    *r = (VirtmemReservation){(uintptr_t)p, n, reservations}; reservations = r; return r;
}
void virtmemRemoveReservation(VirtmemReservation *r)
{
    VirtmemReservation **p = &reservations;
    while (*p && *p != r) p = &(*p)->next;
    assert(*p); *p = r->next; free(r);
}
void SetLastError(DWORD e) { error = e; }
DWORD GetLastError(void) { return error; }
typedef enum { K_FILEMAP } w32_kind;
typedef struct w32_object { w32_kind kind; LONG refcount; int fd; SIZE_T map_size; void *map_view; } w32_object;
static int hz_mapping_unref(w32_object *o);
static w32_object *obj_alloc(w32_kind kind)
{ w32_object *o = calloc(1, sizeof *o); if (o) { o->kind = kind; o->refcount = 1; } return o; }
LONG InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedDecrement(volatile LONG *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
static _Thread_local int test_actor;
static void obj_release(w32_object *o) {
#ifdef HZ_TEST_CLOSE_BEFORE_LOCK
    if (test_actor == 2) InterlockedDecrement(&o->refcount);
#endif
    if (hz_mapping_unref(o)) free(o);
}
BOOL CloseHandle(HANDLE h) { obj_release(h); return TRUE; }
#include "../source/concurrent.h"

#define malloc test_malloc
#define calloc test_calloc
#define aligned_alloc test_aligned_alloc
#define free test_free
#define pthread_mutex_lock test_registry_lock
#include "win32_compat_horizon.inc"
#undef malloc
#undef calloc
#undef aligned_alloc
#undef free
#undef pthread_mutex_lock
SIZE_T VirtualQuery(LPCVOID a, PMEMORY_BASIC_INFORMATION b, SIZE_T n) { return hz_virtual_query(a, b, n); }
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
static void inject(unsigned kind, unsigned nth) { fail_kind = kind; fail_at = nth; memset(hits, 0, sizeof hits); }
static void verify_pages(uint8_t *p, unsigned n)
{ for (unsigned i = 0; i < n; i++) for (unsigned j = 0; j < PAGE; j++) CHECK(p[i * PAGE + j] == (uint8_t)(i + 1)); }
static void empty(void)
{
    CHECK(!live_handles && !live_maps && !requested && !reservations);
    for (unsigned i = 0; i < HZ_MAX_RES; i++) CHECK(!s_res[i].base);
    for (unsigned i = 0; i < HZ_MAX_COMMIT; i++) CHECK(!s_commit[i].base);
}
int main(void)
{
    void *range = mmap(NULL, ARENA_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(range != MAP_FAILED); arena = (uintptr_t)range;
    MEMORY_BASIC_INFORMATION info;
    uint8_t *p = VirtualAlloc(NULL, 3 * PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(p); memset(p, 1, PAGE); memset(p + PAGE, 2, PAGE); memset(p + 2 * PAGE, 3, PAGE);
    CHECK(VirtualQuery(p + PAGE, &info, sizeof info) == sizeof info && info.AllocationBase == p && info.State == MEM_COMMIT);
    CHECK(VirtualFree(p + PAGE, PAGE, MEM_DECOMMIT));
    CHECK(VirtualQuery(p + PAGE, &info, sizeof info) == sizeof info && info.State == MEM_RESERVE);
    CHECK(p[0] == 1 && p[2 * PAGE] == 3);
    CHECK(VirtualAlloc(p + PAGE + 1, PAGE - 1, MEM_COMMIT, PAGE_READWRITE) == p + PAGE);
    for (unsigned j = 0; j < PAGE; j++) CHECK(!p[PAGE + j]);
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();

    /* Releasing the prefix must not lose the remaining allocation's identity. */
    p = VirtualAlloc(NULL, 3 * PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); CHECK(p);
    CHECK(VirtualFree(p, PAGE, MEM_RELEASE));
    CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.State == MEM_FREE);
    CHECK(VirtualQuery(p + PAGE, &info, sizeof info) == sizeof info && info.AllocationBase == p);
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();

    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 2 * PAGE, NULL);
    CHECK(mapping); p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0); CHECK(p);
    memset(p, 0x37, 2 * PAGE);
    CHECK(!MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    CHECK(UnmapViewOfFile(p));
    p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0); CHECK(p);
    CHECK(p[0] == 0x37 && p[2 * PAGE - 1] == 0x37);
    CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.Type == MEM_MAPPED);
    CHECK(CloseHandle(mapping)); CHECK(p[0] == 0x37); CHECK(UnmapViewOfFile(p)); empty();
    CHECK(test_close_race()); empty();

    for (unsigned kind = FAIL_ALLOC; kind <= FAIL_UNMAP; kind++) {
        for (unsigned nth = 1; nth <= 16; nth++) {
            inject(0, 0);
            p = VirtualAlloc(NULL, 3 * PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); CHECK(p);
            for (unsigned i = 0; i < 3; i++) memset(p + i * PAGE, i + 1, PAGE);
            unsigned before_handles = live_handles, before_maps = live_maps; size_t before_bytes = requested;
            inject(kind, nth);
            BOOL ok = VirtualFree(p + PAGE, PAGE, MEM_DECOMMIT);
            unsigned operations = hits[kind]; inject(0, 0);
            if (!ok) {
                verify_pages(p, 3);
                CHECK(live_handles == before_handles && live_maps == before_maps && requested == before_bytes);
                CHECK(VirtualQuery(p + PAGE, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT);
            } else CHECK(p[0] == 1 && p[2 * PAGE] == 3);
            CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();
            if (nth > operations) break;
        }
    }

    /* A failed multi-hole commit must remove only this call's new mappings. */
    for (unsigned kind = FAIL_ALLOC; kind <= FAIL_QUERY; kind++) {
        for (unsigned nth = 1; nth <= 24; nth++) {
            inject(0, 0);
            p = VirtualAlloc(NULL, 5 * PAGE, MEM_RESERVE, PAGE_READWRITE); CHECK(p);
            CHECK(VirtualAlloc(p + PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE));
            CHECK(VirtualAlloc(p + 3 * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE));
            memset(p + PAGE, 0x31, PAGE); memset(p + 3 * PAGE, 0x73, PAGE);
            unsigned before_handles = live_handles, before_maps = live_maps; size_t before_bytes = requested;
            inject(kind, nth);
            void *committed = VirtualAlloc(p, 5 * PAGE, MEM_COMMIT, PAGE_READWRITE);
            unsigned operations = hits[kind]; inject(0, 0);
            CHECK(p[PAGE] == 0x31 && p[2 * PAGE - 1] == 0x31 && p[3 * PAGE] == 0x73 && p[4 * PAGE - 1] == 0x73);
            if (!committed) {
                CHECK(live_handles == before_handles && live_maps == before_maps && requested == before_bytes);
                CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.State == MEM_RESERVE);
                CHECK(VirtualQuery(p + 2 * PAGE, &info, sizeof info) == sizeof info && info.State == MEM_RESERVE);
            } else {
                CHECK(committed == p);
                for (unsigned i = 0; i < 5; i += 2) for (unsigned j = 0; j < PAGE; j++) CHECK(!p[i * PAGE + j]);
            }
            CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();
            if (nth > operations) break;
        }
    }

    /* Whole release rolls back earlier successful unmaps if a later one fails. */
    for (unsigned nth = 1; nth <= 3; nth++) {
        p = VirtualAlloc(NULL, 3 * PAGE, MEM_RESERVE, PAGE_READWRITE); CHECK(p);
        for (unsigned i = 0; i < 3; i++) { CHECK(VirtualAlloc(p + i * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE)); memset(p + i * PAGE, i + 1, PAGE); }
        inject(FAIL_UNMAP, nth); CHECK(!VirtualFree(p, 0, MEM_RELEASE)); inject(0, 0);
        verify_pages(p, 3); CHECK(live_maps == 3 && live_handles == 3);
        CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();
    }

    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, PAGE, NULL); CHECK(mapping);
    p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0); CHECK(p); p[0] = 0x49;
    inject(FAIL_UNMAP, 1); CHECK(!UnmapViewOfFile(p)); inject(0, 0);
    CHECK(p[0] == 0x49 && live_maps == 1 && live_handles == 1);
    CHECK(CloseHandle(mapping)); CHECK(UnmapViewOfFile(p)); empty();

    /* Exhausting the registry must not begin a split requiring an extra slot. */
    p = VirtualAlloc(NULL, (HZ_MAX_COMMIT + 2) * PAGE, MEM_RESERVE, PAGE_READWRITE); CHECK(p);
    CHECK(VirtualAlloc(p, 3 * PAGE, MEM_COMMIT, PAGE_READWRITE));
    memset(p, 0x5A, 3 * PAGE);
    for (unsigned i = 1; i < HZ_MAX_COMMIT; i++) CHECK(VirtualAlloc(p + (i + 2) * PAGE, PAGE, MEM_COMMIT, PAGE_READWRITE));
    CHECK(live_maps == HZ_MAX_COMMIT && live_handles == HZ_MAX_COMMIT);
    size_t before_bytes = requested;
    CHECK(!VirtualFree(p + PAGE, PAGE, MEM_DECOMMIT));
    CHECK(live_maps == HZ_MAX_COMMIT && live_handles == HZ_MAX_COMMIT && requested == before_bytes);
    CHECK(p[0] == 0x5A && p[PAGE] == 0x5A && p[3 * PAGE - 1] == 0x5A);
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();

    peak = requested;
    p = VirtualAlloc(NULL, 64u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); CHECK(p);
    p[0] = 0x17; p[(64u << 20) - 1] = 0x93;
    CHECK(VirtualFree(p + PAGE, PAGE, MEM_DECOMMIT));
    CHECK(p[0] == 0x17 && p[(64u << 20) - 1] == 0x93);
    size_t split_peak = peak;
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); empty();
    printf("HZ-MEMORY-HOST split64MiB_peak_requested=%zu registry_capacity=%u\n", split_peak, HZ_MAX_COMMIT);
    printf("HZ-MEMORY-HOST PASS checks=%u peak_requested=%zu\n", checks, peak);
    CHECK(munmap(range, ARENA_SIZE) == 0);
    return 0;
}
