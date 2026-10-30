#ifndef HZ_TEST_SWITCH_H
#define HZ_TEST_SWITCH_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t Result;
typedef uint32_t Handle;
typedef struct { u64 addr, size; u32 type, perm; } MemoryInfo;
typedef struct VirtmemReservation { uintptr_t base; size_t size; struct VirtmemReservation *next; } VirtmemReservation;
enum { MemType_Unmapped, MemType_CodeWritable = 21, Perm_Rw = 3 };
enum { CodeMapOperation_MapOwner = 2, CodeMapOperation_UnmapOwner = 3 };
#define R_FAILED(r) ((r) != 0)
#define INVALID_HANDLE 0
Result svcQueryMemory(MemoryInfo *, u32 *, u64);
Result svcCreateCodeMemory(Handle *, void *, size_t);
Result svcControlCodeMemory(Handle, u32, void *, size_t, u32);
Result svcCloseHandle(Handle);
void virtmemLock(void);
void virtmemUnlock(void);
void *virtmemFindAslr(size_t, size_t);
VirtmemReservation *virtmemAddReservation(void *, size_t);
void virtmemRemoveReservation(VirtmemReservation *);
#endif
