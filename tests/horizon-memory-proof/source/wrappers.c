/* Actual Horizon services with the same runtime memory implementation. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "win32_compat.h"

typedef enum { K_FILEMAP } w32_kind;
typedef struct w32_object { w32_kind kind; LONG refcount; int fd; SIZE_T map_size; void *map_view; } w32_object;
static DWORD last_error;
void SetLastError(DWORD error) { last_error = error; }
DWORD GetLastError(void) { return last_error; }
LONG InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedDecrement(volatile LONG *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
static int hz_mapping_unref(w32_object *o);
static w32_object *obj_alloc(w32_kind kind)
{ w32_object *o = calloc(1, sizeof *o); if (o) { o->kind = kind; o->refcount = 1; } return o; }
static void obj_release(w32_object *o) { if (hz_mapping_unref(o)) free(o); }
BOOL CloseHandle(HANDLE h) { obj_release(h); return TRUE; }
#include "concurrent.h"
static Result controlled_create(Handle *h, void *src, size_t size)
{ test_pause_create(); return svcCreateCodeMemory(h, src, size); }
#define svcCreateCodeMemory controlled_create
#define pthread_mutex_lock test_registry_lock
#include "win32_compat_horizon.inc"
#undef svcCreateCodeMemory
#undef pthread_mutex_lock
SIZE_T VirtualQuery(LPCVOID a, PMEMORY_BASIC_INFORMATION b, SIZE_T n) { return hz_virtual_query(a, b, n); }

static unsigned checks;
static void report(const char *s) { puts(s); svcOutputDebugString(s, strlen(s)); }
static int failed(unsigned line, const char *condition)
{
    char msg[256]; snprintf(msg, sizeof msg, "HZ-MEMORY-WRAPPERS FAIL line=%u %s", line, condition); report(msg); return 1;
}
#define CHECK(c) do { checks++; if (!(c)) return failed(__LINE__, #c); } while (0)
static int registries_empty(void)
{
    for (int i = 0; i < HZ_MAX_RES; i++) if (s_res[i].base) return 0;
    for (int i = 0; i < HZ_MAX_COMMIT; i++) if (s_commit[i].base) return 0;
    for (int i = 0; i < HZ_MAX_MAP; i++) if (s_mapping[i].owner) return 0;
    return 1;
}
int main(void)
{
    report("HZ-MEMORY-WRAPPERS start");
    MEMORY_BASIC_INFORMATION info;
    uint8_t *p = VirtualAlloc(NULL, 3 * HZ_PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(p);
    for (unsigned i = 0; i < 3; i++) memset(p + i * HZ_PAGE, i + 1, HZ_PAGE);
    CHECK(VirtualQuery(p + HZ_PAGE, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT && info.AllocationBase == p);
    CHECK(VirtualFree(p + HZ_PAGE + 1, HZ_PAGE - 1, MEM_DECOMMIT));
    MemoryInfo native; u32 pi;
    CHECK(R_SUCCEEDED(svcQueryMemory(&native, &pi, (u64)(p + HZ_PAGE))) && native.type == MemType_Unmapped);
    CHECK(p[0] == 1 && p[2 * HZ_PAGE] == 3);
    CHECK(VirtualQuery(p + HZ_PAGE, &info, sizeof info) == sizeof info && info.State == MEM_RESERVE);
    CHECK(VirtualAlloc(p + HZ_PAGE + 1, HZ_PAGE - 1, MEM_COMMIT, PAGE_READWRITE) == p + HZ_PAGE);
    for (unsigned i = 0; i < HZ_PAGE; i++) CHECK(p[HZ_PAGE + i] == 0);
    CHECK(VirtualFree(p, HZ_PAGE, MEM_RELEASE));
    CHECK(VirtualQuery(p + HZ_PAGE, &info, sizeof info) == sizeof info && info.AllocationBase == p);
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); CHECK(registries_empty());
    report("HZ-MEMORY-WRAPPERS private/decommit/recommit/prefix-release/query PASS");

    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 2 * HZ_PAGE, NULL);
    CHECK(mapping); p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0); CHECK(p);
    memset(p, 0x37, 2 * HZ_PAGE);
    CHECK(!MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    CHECK(UnmapViewOfFile(p));
    p = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0); CHECK(p);
    CHECK(p[0] == 0x37 && p[2 * HZ_PAGE - 1] == 0x37);
    CHECK(VirtualQuery(p, &info, sizeof info) == sizeof info && info.Type == MEM_MAPPED && info.AllocationBase == p);
    CHECK(CloseHandle(mapping)); CHECK(p[0] == 0x37); CHECK(UnmapViewOfFile(p)); CHECK(registries_empty());
    CHECK(test_close_race()); CHECK(registries_empty());
    report("HZ-MEMORY-WRAPPERS map/remap/preserve/close-before-unmap PASS");

    p = VirtualAlloc(NULL, 64u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); CHECK(p);
    p[0] = 0x17; p[(64u << 20) - 1] = 0x93;
    CHECK(VirtualFree(p + HZ_PAGE, HZ_PAGE, MEM_DECOMMIT));
    CHECK(R_SUCCEEDED(svcQueryMemory(&native, &pi, (u64)(p + HZ_PAGE))) && native.type == MemType_Unmapped);
    CHECK(p[0] == 0x17 && p[(64u << 20) - 1] == 0x93);
    CHECK(VirtualAlloc(p + HZ_PAGE, HZ_PAGE, MEM_COMMIT, PAGE_READWRITE) == p + HZ_PAGE);
    CHECK(p[HZ_PAGE] == 0 && p[0] == 0x17 && p[(64u << 20) - 1] == 0x93);
    CHECK(VirtualFree(p, 0, MEM_RELEASE)); CHECK(registries_empty());
    char msg[256]; snprintf(msg, sizeof msg, "HZ-MEMORY-WRAPPERS PASS checks=%u split64MiB/remap/ownership/query", checks); report(msg);
    return 0;
}
