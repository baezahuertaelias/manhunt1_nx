/*
 * Manhunt (Xbox) static recompilation: Horizon (Nintendo Switch) boot.
 *
 * Same boot order as main.c (host): memory layout -> input -> kernel ->
 * paths -> bridge -> dispatch -> entry point. Differences:
 *   - stdout/stderr go to svcOutputDebugString, so every [MH-BOOT] line and
 *     every runtime log reaches the emulator log (scripts/emu-run.sh);
 *   - the runtime switches are set with setenv (there is no shell);
 *   - game data is read from sdmc:/manhunt (DEFAULT.xbe + disc files);
 *   - guest code runs on a thread with a large stack, because every guest
 *     call is a host call in the lifted code;
 *   - an evidence thread dumps the guest framebuffer as BMP to
 *     sdmc:/manhunt-frames every MANHUNT_DUMP_SECS seconds.
 * Memory: win32_compat_horizon.inc (patch 0003) backs the guest window with
 * code-memory mappings; no RAM mirrors (not needed to boot: FEASIBILITY.md).
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <switch.h>

/* Only the kernel and memory-layout declarations: the umbrella xboxrecomp.h
 * pulls the NV2A QEMU shim, whose MemoryRegion clashes with libnx. */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "recomp_types.h"
#include "nv2a_deko3d.h"

#define MANHUNT_ENTRY_POINT 0x001FE908u
#define GAME_DIR  "sdmc:/manhunt"
#define SAVE_DIR  "sdmc:/manhunt-save"
#define FRAME_DIR "sdmc:/manhunt-frames"
#define GUEST_THREAD_STACK (64u << 20)

extern RECOMP_TLS uint32_t g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern int recomp_dispatch_init(void);

static void gpu_backend_init(void)
{
    const char *option = getenv("MANHUNT_GPU_BACKEND");
    if (!option || strcmp(option, "0") == 0)
        return;
    if (strcmp(option, "1") != 0) {
        printf("[MH-BOOT] FAIL MANHUNT_GPU_BACKEND must be 0 or 1\n");
        exit(1);
    }
    if (R_FAILED(romfsInit()) || !manhunt_nv2a_deko3d_init()) {
        printf("[MH-BOOT] FAIL native GPU initialization\n");
        exit(1);
    }
    nv2a_backend_register(manhunt_nv2a_deko3d_backend());
    printf("[MH-GPU] enabled synchronous deko3d menu backend\n");
}
extern void xbox_path_init(const char *game_dir, const char *save_dir);
extern void manhunt_input_init(void);
extern void xbox_FramebufferDumpBmp(const char *prefix);
void xbe_entry_point(void);

/* ---- stdout/stderr -> svcOutputDebugString, one call per line ----------- */

static char s_line[1024];
static size_t s_len;
static Mutex s_log_lock;

/* Optional copy of the log on the SD card (MANHUNT_LOG_FILE=sdmc:/<file>,
 * default off). The debug channel is only visible under an emulator; a
 * physical console needs the file. Lines logged before the file is opened
 * (boot and config lines) are kept in a small buffer and written first. All
 * state is guarded by s_log_lock. The file stops at LOG_FILE_MAX bytes. */
#define LOG_FILE_MAX (32u << 20)
static FILE *s_logfp;
static int s_log_decided;                 /* 0 until log_file_init() has run */
static char s_early[8192];
static size_t s_early_len, s_log_bytes;
static int s_early_dropped;               /* early buffer filled up: later early lines were not kept */

static void log_file_line(const char *line, size_t len)
{
    if (s_logfp) {
        if (s_log_bytes + len + 1 > LOG_FILE_MAX) {
            fputs("[MH-LOG] size limit reached, file log stopped\n", s_logfp);
            fclose(s_logfp);
            s_logfp = NULL;
            return;
        }
        fwrite(line, 1, len, s_logfp);
        fputc('\n', s_logfp);
        fflush(s_logfp);
        if (ferror(s_logfp)) {            /* SD full or removed: say so once, on the debug channel */
            static const char msg[] = "[MH-LOG] file write failed, file log stopped";
            fclose(s_logfp);
            s_logfp = NULL;
            svcOutputDebugString(msg, sizeof msg - 1);
            return;
        }
        s_log_bytes += len + 1;
    } else if (!s_log_decided) {
        if (!s_early_dropped && s_early_len + len + 1 <= sizeof s_early) {
            memcpy(s_early + s_early_len, line, len);
            s_early_len += len;
            s_early[s_early_len++] = '\n';
        } else {
            s_early_dropped = 1;          /* keep the buffer gap-free: stop buffering */
        }
    }
}

static ssize_t debug_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r; (void)fd;
    mutexLock(&s_log_lock);
    for (size_t i = 0; i < len; i++) {
        char c = ptr[i];
        if (c == '\n' || s_len == sizeof s_line - 1) {
            svcOutputDebugString(s_line, s_len);
            log_file_line(s_line, s_len);
            s_len = 0;
            if (c == '\n')
                continue;
        }
        s_line[s_len++] = c;
    }
    mutexUnlock(&s_log_lock);
    return (ssize_t)len;
}

static const devoptab_t s_debug_devoptab = {
    .name = "dbg",
    .write_r = debug_write,
};

static void log_init(void)
{
    mutexInit(&s_log_lock);
    devoptab_list[STD_OUT] = &s_debug_devoptab;
    devoptab_list[STD_ERR] = &s_debug_devoptab;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* Called once after config_init(): opens MANHUNT_LOG_FILE if set. The path
 * must start with sdmc:/. The result is logged after the lock is released
 * (printf takes it, and it is not recursive). */
static void log_file_init(void)
{
    const char *path = getenv("MANHUNT_LOG_FILE");
    int want = path && *path;

    mutexLock(&s_log_lock);
    if (want && strncmp(path, "sdmc:/", 6) == 0 && (s_logfp = fopen(path, "w")) != NULL) {
        fwrite(s_early, 1, s_early_len, s_logfp);
        if (s_early_dropped)
            fputs("[MH-LOG] early lines beyond 8 KiB were not kept\n", s_logfp);
        fflush(s_logfp);
        if (ferror(s_logfp)) {
            fclose(s_logfp);
            s_logfp = NULL;
        } else {
            s_log_bytes = s_early_len;
        }
    }
    s_log_decided = 1;
    s_early_len = 0;
    mutexUnlock(&s_log_lock);
    if (want)
        printf("[MH-BOOT] log file %s: %s\n", path,
               s_logfp ? "open" : "FAILED (path must start with sdmc:/ and be writable)");
}

/* ---- configuration ------------------------------------------------------ */

/* The Linux boot configuration (reports/FEASIBILITY.md). A file
 * sdmc:/manhunt.env with KEY=VALUE lines can add or override. */
static void config_init(void)
{
    static const char *defaults[][2] = {
        {"RECOMP_PB_EXEC", "1"}, {"RECOMP_PB_WALK", "dma"},
        {"RECOMP_TITLE_KEVENTS", "1"}, {"RECOMP_VBLANK", "paced"},
        {"RECOMP_DISPATCH_GATE", "1"}, {"RECOMP_FLIP_PACING", "1"},
        {"MANHUNT_DUMP_SECS", "15"},
    };
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        setenv(defaults[i][0], defaults[i][1], 1);

    FILE *f = fopen("sdmc:/manhunt.env", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq)
            continue;
        *eq = 0;
        line[strcspn(eq + 1, "\r\n") + (eq + 1 - line)] = 0;
        setenv(line, eq + 1, 1);
        printf("[MH-BOOT] config %s=%s\n", line, eq + 1);
    }
    if (f)
        fclose(f);
}

static void *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    void *data = NULL;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = data ? (size_t)n : 0;
    return data;
}

/* ---- evidence: periodic framebuffer dumps -------------------------------- */

static volatile int s_running = 1;

static void *frame_dumper(void *unused)
{
    int secs = atoi(getenv("MANHUNT_DUMP_SECS") ? getenv("MANHUNT_DUMP_SECS") : "0");
    (void)unused;
    if (secs <= 0)
        return NULL;
    mkdir(FRAME_DIR, 0777);
    while (s_running) {
        for (int i = 0; i < secs * 10 && s_running; i++)
            svcSleepThread(100000000ll);
        if (s_running) {
            xbox_FramebufferDumpBmp(FRAME_DIR "/fb");
            printf("[MH-FRAME] dumped\n");
        }
    }
    return NULL;
}

/* ---- guest ---------------------------------------------------------------- */

/* All runtime set-up runs here, on the thread that then runs guest code:
 * guest registers and the TIB pointer (g_esp, g_fs_base, ...) are
 * thread-local, as on the host build where one thread does both. */
static void *guest_main(void *xbe_arg)
{
    struct { void *data; size_t size; } *xbe = xbe_arg;

    /* Guest main thread: core 0 preferred, may run on all application cores
     * (see nx_spread_thread in patch 0003 for why this matters on Horizon). */
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 0x7);

    if (!xbox_MemoryLayoutInit(xbe->data, xbe->size)) {
        printf("[MH-BOOT] FAIL memory layout\n");
        return NULL;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("[MH-BOOT] memory offset=0x%llx\n", (unsigned long long)g_xbox_mem_offset);
    {   /* Diagnostic: device apertures must start zeroed, as on the host. */
        static const uint32_t probe[] = {0xFE800000u, 0xFED00000u, 0xFED00004u, 0xFED08004u, 0xFD000000u};
        for (unsigned i = 0; i < sizeof probe / sizeof probe[0]; i++) {
            uintptr_t host = (uintptr_t)g_xbox_mem_offset + probe[i];
            MemoryInfo mi; u32 pi;
            svcQueryMemory(&mi, &pi, host);
            printf("[MH-BOOT] aperture 0x%08X = %08X (host type %u perm %u, region 0x%llx+0x%llx)\n",
                   probe[i], *(volatile uint32_t *)host, mi.type, mi.perm,
                   (unsigned long long)mi.addr, (unsigned long long)mi.size);
        }
    }

    gpu_backend_init();
    manhunt_input_init();
    xbox_kernel_init();
    mkdir(SAVE_DIR, 0777);
    xbox_path_init(GAME_DIR, SAVE_DIR);
    xbox_kernel_bridge_init();
    g_esp = XBOX_STACK_TOP;
    if (!recomp_dispatch_init())
        printf("[MH-BOOT] flat dispatch unavailable; indirect calls use binary search\n");
    xbox_WatchdogStart();                     /* RECOMP_WATCHDOG_SECS, as on the host */

    pthread_t dumper;
    pthread_create(&dumper, NULL, frame_dumper, NULL);
    pthread_detach(dumper);

    printf("[MH-BOOT] entry 0x%08X esp=0x%08X\n", MANHUNT_ENTRY_POINT, g_esp);
    xbe_entry_point();
    printf("[MH-BOOT] entry point returned\n");
    return NULL;
}

int main(void)
{
    struct { void *data; size_t size; } xbe;
    pthread_t guest;
    pthread_attr_t attr;

    log_init();
    printf("[MH-BOOT] start (Horizon) game_dir=%s\n", GAME_DIR);
    config_init();
    log_file_init();

    xbe.data = load_file(GAME_DIR "/DEFAULT.xbe", &xbe.size);
    if (!xbe.data) {
        printf("[MH-BOOT] FAIL cannot read %s/DEFAULT.xbe (errno %d)\n", GAME_DIR, errno);
        static const char *dirs[] = {"sdmc:/", GAME_DIR};
        for (int i = 0; i < 2; i++) {            /* what the title can see */
            DIR *d = opendir(dirs[i]);
            struct dirent *e;
            printf("[MH-BOOT] listing %s: %s\n", dirs[i], d ? "" : strerror(errno));
            while (d && (e = readdir(d)))
                printf("[MH-BOOT]   %s\n", e->d_name);
            if (d)
                closedir(d);
        }
        return 1;
    }
    printf("[MH-BOOT] xbe loaded bytes=%zu\n", xbe.size);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GUEST_THREAD_STACK);
    if (pthread_create(&guest, &attr, guest_main, &xbe) != 0) {
        printf("[MH-BOOT] FAIL guest thread\n");
        return 1;
    }
    pthread_join(guest, NULL);
    s_running = 0;
    return 0;
}
