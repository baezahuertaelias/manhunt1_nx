/*
 * Movie / file-open trace (always on; a few lines per run).
 *
 * Investigation of "the Rockstar logo plays but later videos or scenes
 * break". Logs, to stderr (the normal log sink):
 *   [MH-FILE] every NtCreateFile whose path looks like a movie (.xmv, .bik,
 *             .wmv, "movie", "video") and every open that fails, plus the
 *             first 64 opens of any kind, with the NTSTATUS returned.
 * The tiled-lock side is logged in d3d_tiled_lock.c ([MH-D3D] movie lock).
 *
 * Linked with -Wl,--wrap=xbox_NtCreateFile. Observation only.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdatomic.h>

#include "kernel.h"

extern NTSTATUS __real_xbox_NtCreateFile(
    PHANDLE, ACCESS_MASK, PXBOX_OBJECT_ATTRIBUTES, PXBOX_IO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG);

static int has_ci(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == needle[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

NTSTATUS __stdcall __wrap_xbox_NtCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
    PXBOX_OBJECT_ATTRIBUTES oa, PXBOX_IO_STATUS_BLOCK iosb,
    PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
    ULONG CreateDisposition, ULONG CreateOptions)
{
    static atomic_uint seq;
    char name[160] = "?";
    NTSTATUS st = __real_xbox_NtCreateFile(FileHandle, DesiredAccess, oa, iosb,
        AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions);
    unsigned n = atomic_fetch_add_explicit(&seq, 1, memory_order_relaxed);

    if (oa && oa->ObjectName && oa->ObjectName->Buffer) {
        size_t len = oa->ObjectName->Length;
        if (len >= sizeof name) len = sizeof name - 1;
        memcpy(name, oa->ObjectName->Buffer, len);
        name[len] = 0;
    }
    if (n < 64 || (int)st < 0 || has_ci(name, ".xmv") || has_ci(name, ".bik") ||
        has_ci(name, ".wmv") || has_ci(name, "movie") || has_ci(name, "video"))
        fprintf(stderr, "[MH-FILE] #%u open '%s' access=0x%X disp=%u opts=0x%X -> status=0x%08X\n",
                n, name, (unsigned)DesiredAccess, (unsigned)CreateDisposition,
                (unsigned)CreateOptions, (unsigned)st);
    return st;
}
