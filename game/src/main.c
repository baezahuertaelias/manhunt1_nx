/*
 * Manhunt (Xbox) static recompilation: POSIX host boot.
 *
 * Adapted from xboxrecomp templates/new-game/src/main.c (MIT) for Linux:
 * no dbghelp, a SA_SIGINFO fault handler instead of a Win32 VEH, and
 * load_xbe() actually returns its buffer. Boot order follows the template:
 * memory layout -> kernel -> paths -> bridge -> dispatch -> entry point.
 *
 * Usage: manhunt <game-dir>
 *   <game-dir> holds DEFAULT.xbe and the disc files (D:\ maps to it).
 * Boot milestones are printed as "[MH-BOOT] ..." lines for scripted checks.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <execinfo.h>
#include <sys/mman.h>

#include <xbox/xboxrecomp.h>

#define MANHUNT_ENTRY_POINT 0x001FE908u   /* reports/XBE_HEADER.md */

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t g_xbox_code_lo, g_xbox_code_hi;
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;

extern int recomp_dispatch_init(void);
extern void xbox_OhciInit(void);      /* src/usb/ohci.h; opt-in via RECOMP_USB */
extern void manhunt_input_init(void);  /* xpp_input.c */
extern void xbox_path_init(const char *game_dir, const char *save_dir);
void xbe_entry_point(void);

static bool load_xbe(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f) {
        fprintf(stderr, "Cannot open XBE: %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = size > 0 ? malloc((size_t)size) : NULL;
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return true;
}

static void print_guest_stack(void)
{
    const uint32_t *sp;
    int i, shown = 0;

    if (!g_xbox_mem_offset || !g_esp)
        return;
    sp = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
    fprintf(stderr, "  guest stack (likely return addresses, innermost first):\n");
    for (i = 0; i < 256 && shown < 24; i++) {
        uint32_t v = sp[i];
        if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
            fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
            shown++;
        }
    }
    fprintf(stderr, "  recent ICALL targets:");
    for (i = 0; i < 16; i++)
        fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + i) & 15]);
    fprintf(stderr, "\n");
}

static void fault_handler(int sig, siginfo_t *si, void *ucv)
{
    uintptr_t fault = (uintptr_t)si->si_addr;
    void *frames[32];
    int n;

    if (xbox_PosixMmioFault(ucv, fault))
        return;                                /* trapped device register: resume */

    fprintf(stderr, "[CRASH] signal %d at host address %p\n", sig, si->si_addr);
    if (g_xbox_mem_offset && fault >= (uintptr_t)g_xbox_mem_offset)
        fprintf(stderr, "  guest VA of fault: 0x%08X\n",
                (uint32_t)(fault - (uintptr_t)g_xbox_mem_offset));
    fprintf(stderr, "  guest regs: eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp);
    print_guest_stack();
    n = backtrace(frames, 32);
    fprintf(stderr, "  host backtrace (resolve with addr2line -f -e manhunt):\n");
    backtrace_symbols_fd(frames, n, 2);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char **argv)
{
    const char *game_dir = argc > 1 ? argv[1] : "game";
    char xbe_path[4096];
    void *xbe_data = NULL;
    size_t xbe_size = 0;
    struct sigaction sa;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("[MH-BOOT] start game_dir=%s\n", game_dir);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    snprintf(xbe_path, sizeof(xbe_path), "%s/DEFAULT.xbe", game_dir);
    if (!load_xbe(xbe_path, &xbe_data, &xbe_size))
        return 1;
    printf("[MH-BOOT] xbe loaded bytes=%zu\n", xbe_size);

    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fprintf(stderr, "Failed to initialize the Xbox memory layout\n");
        return 1;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("[MH-BOOT] memory offset=0x%llx\n", (unsigned long long)g_xbox_mem_offset);

    /* Debug: make the 64 MiB RAM mirrors (guest 0x04000000-0x74000000)
     * inaccessible, so any read or write through a mirror faults with its
     * guest address. Horizon has no RW aliasing (tests/horizon-probe), so this
     * measures whether the Switch build needs software address wrapping. */
    if (getenv("MANHUNT_MIRROR_TRAP")) {
        uintptr_t lo = (uintptr_t)g_xbox_mem_offset + 0x04000000u;
        size_t len = 0x70000000u;
        int rc = mprotect((void *)lo, len, PROT_NONE);
        printf("[MH-BOOT] mirror trap %s (guest 0x04000000-0x74000000)\n", rc == 0 ? "on" : "FAILED");
    }
    /* Same for the tiled aperture (guest 0xF0000000, 64 MiB), which the
     * runtime maps as an alias of the contiguous window. */
    if (getenv("MANHUNT_TILED_TRAP")) {
        int rc = mprotect((void *)((uintptr_t)g_xbox_mem_offset + 0xF0000000u), 0x04000000u, PROT_NONE);
        printf("[MH-BOOT] tiled-aperture trap %s (guest 0xF0000000-0xF4000000)\n", rc == 0 ? "on" : "FAILED");
    }

    /* The OHCI model (pads through XAPI) is the title's to start; nothing in
     * the runtime calls it. It stays off unless RECOMP_USB is set. */
    xbox_OhciInit();
    manhunt_input_init();

    xbox_kernel_init();
    xbox_path_init(game_dir, getenv("MANHUNT_SAVE_DIR"));
    xbox_kernel_bridge_init();
    g_esp = XBOX_STACK_TOP;

    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] flat dispatch unavailable; indirect calls use binary search\n");
    xbox_WatchdogStart();

    printf("[MH-BOOT] entry 0x%08X esp=0x%08X\n", MANHUNT_ENTRY_POINT, g_esp);
    xbe_entry_point();

    printf("[MH-BOOT] entry point returned\n");
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
