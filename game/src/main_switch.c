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
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdalign.h>
#include <netinet/in.h>
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
/* SD layout: everything lives in one folder next to the NRO.
 *   <base>/DEFAULT.xbe + audio/ levels/   game data
 *   <base>/save  <base>/frames            saves, frame dumps
 *   <base>/manhunt.env                    optional KEY=VALUE config
 * (The old Eden scripts used sdmc:/manhunt; override with -DMANHUNT_SD_BASE=.) */
#ifndef MANHUNT_SD_BASE
#define MANHUNT_SD_BASE "sdmc:/switch/manhunt1_nx"
#endif
#define GAME_DIR   MANHUNT_SD_BASE
#define SAVE_DIR   MANHUNT_SD_BASE "/save"
#define FRAME_DIR  MANHUNT_SD_BASE "/frames"
#define ENV_FILE   MANHUNT_SD_BASE "/manhunt.env"
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
static int s_log_sync;                    /* MANHUNT_LOG_SYNC=1: fsync after every line */
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
        if (s_log_sync)
            fsync(fileno(s_logfp));       /* survive a console crash: slow */
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

/* nxlink: when the NRO was started with "nxlink -s", every log line is also
 * sent to the PC. nxlinkStdio() would redirect fd 1/2, but log_init() replaces
 * those devoptabs, so the socket fd is kept and written to directly. */
static int s_nx_sock = -1;
static volatile uint32_t s_log_lines;     /* log lines written so far (watchdog progress counter) */

static void nx_send(const char *line, size_t len)
{
    int fd = s_nx_sock;
    char b[1024];
    if (fd < 0)
        return;
    if (len > sizeof b - 1)
        len = sizeof b - 1;
    memcpy(b, line, len);                 /* one write per line: lines do not interleave */
    b[len++] = '\n';
    if (write(fd, b, len) < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        s_nx_sock = -1;                   /* PC gone: stop trying. A full buffer (EAGAIN) drops the line. */
}

/* MANHUNT_LOG_TCP=<ip>:<port>: stream every log line (and the watchdog ticks)
 * to a listener on the PC, e.g. "nc -l 5555" there. Independent of the SD
 * card, which can be what is stuck. Ignored when nxlink already gave a socket. */
static void tcp_log_connect(void)
{
    const char *e = getenv("MANHUNT_LOG_TCP");
    char ip[64];
    const char *colon;
    if (!e || !*e || s_nx_sock >= 0 || !(colon = strchr(e, ':')) || (size_t)(colon - e) >= sizeof ip)
        return;
    memcpy(ip, e, (size_t)(colon - e));
    ip[colon - e] = 0;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)atoi(colon + 1));
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1)
        return;
    if (R_FAILED(socketInitializeDefault()))
        return;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    /* Non-blocking connect with a 2 s limit so an unreachable listener cannot
     * stall boot, and non-blocking sends so a stalled listener cannot stall the
     * game (a diagnostic must not be able to cause a freeze). */
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&a, sizeof a);
    if (rc != 0 && errno == EINPROGRESS) {
        fd_set w;
        struct timeval tv = {2, 0};
        FD_ZERO(&w);
        FD_SET(fd, &w);
        if (select(fd + 1, NULL, &w, NULL, &tv) == 1) {
            int err = 0;
            socklen_t el = sizeof err;
            rc = (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0) ? 0 : -1;
        } else {
            rc = -1;
        }
    }
    if (rc != 0) {
        close(fd);
        return;
    }
    s_nx_sock = fd;
}

static ssize_t debug_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r; (void)fd;
    mutexLock(&s_log_lock);
    for (size_t i = 0; i < len; i++) {
        char c = ptr[i];
        if (c == '\n' || s_len == sizeof s_line - 1) {
            __atomic_add_fetch(&s_log_lines, 1, __ATOMIC_RELAXED);
            svcOutputDebugString(s_line, s_len);
            nx_send(s_line, s_len);
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
    /* Sockets are initialised only when needed (nxlink or MANHUNT_LOG_TCP), in
     * main() after the env file is read. */
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
    const char *sync = getenv("MANHUNT_LOG_SYNC");
    int want = path && *path;

    s_log_sync = sync && *sync == '1';

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

/* ---- lock-free watchdog (MANHUNT_WD=1) ------------------------------------ */
/* The first console runs stopped logging after a file open, so the log (which
 * takes s_log_lock and writes the SD card) cannot say why. This thread writes
 * to its own file with its own descriptor and no lock. Every 3 s it records
 * whether any log line appeared; after two quiet ticks it pauses each
 * registered thread and records pc/lr and a frame-pointer backtrace as offsets
 * from the NRO base (resolve with aarch64-none-elf-addr2line -e manhunt.elf). */
#define WD_MAX 8
static Handle s_wd_h[WD_MAX];
static const char *s_wd_name[WD_MAX];
static volatile int s_wd_n;
static int s_wd_fd = -1;

static void wd_register(const char *name)
{
    int i = __atomic_fetch_add(&s_wd_n, 1, __ATOMIC_SEQ_CST);
    if (i < WD_MAX) {
        s_wd_name[i] = name;
        s_wd_h[i] = threadGetCurHandle();
    }
}

static void wd_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void wd_line(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof b - 2)
        n = (int)sizeof b - 2;
    nx_send(b, (size_t)n);                    /* network first: the SD card may be what is stuck */
    b[n++] = '\n';
    if (s_wd_fd >= 0) {
        if (write(s_wd_fd, b, (size_t)n) > 0)
            fsync(s_wd_fd);
    }
}

static void wd_dump(void)
{
    extern char __start__[];
    uintptr_t code = (uintptr_t)__start__;
    int n = s_wd_n < WD_MAX ? s_wd_n : WD_MAX;
    for (int i = 0; i < n; i++) {
        ThreadContext c;
        if (R_FAILED(svcSetThreadActivity(s_wd_h[i], ThreadActivity_Paused))) {
            wd_line("[MH-WD] %s: cannot pause", s_wd_name[i]);
            continue;
        }
        if (R_SUCCEEDED(svcGetThreadContext3(&c, s_wd_h[i]))) {
            wd_line("[MH-WD] %s: pc +0x%llx lr +0x%llx sp 0x%llx", s_wd_name[i],
                    (unsigned long long)(c.pc.x - code), (unsigned long long)(c.lr - code),
                    (unsigned long long)c.sp);
            uint64_t *fr = (uint64_t *)c.fp;
            for (int k = 0; k < 16 && fr && !((uintptr_t)fr & 7); k++) {
                MemoryInfo mi;
                u32 pg;
                svcQueryMemory(&mi, &pg, (uintptr_t)fr);
                if (!(mi.perm & Perm_R))
                    break;
                wd_line("[MH-WD]   %s from +0x%llx", s_wd_name[i], (unsigned long long)(fr[1] - code));
                fr = (uint64_t *)fr[0];
            }
        } else {
            wd_line("[MH-WD] %s: no context", s_wd_name[i]);
        }
        svcSetThreadActivity(s_wd_h[i], ThreadActivity_Runnable);
    }
}

static void wd_main(void *unused)
{
    uint32_t last = s_log_lines;
    int stale = 0, ticks = 0;
    const char *wde = getenv("MANHUNT_WD_DUMP");
    const int wd_dump_on = wde && *wde == '1';
    (void)unused;
    wd_line("[MH-WD] thread up, lines=%u", (unsigned)last);
    for (;;) {
        /* 4 ticks a second for the first 10 s (the console has frozen ~2 s in),
         * then every 3 s. Each tick records the process's memory use. */
        svcSleepThread(ticks++ < 40 ? 250000000ll : 3000000000ll);
        uint32_t now = s_log_lines;
        u64 used = 0, total = 0;
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        wd_line("[MH-WD] t=%d lines=%u %s mem=%lluMB/%lluMB focus=%d", ticks, (unsigned)now,
                now == last ? "STALE" : "ok", (unsigned long long)(used >> 20),
                (unsigned long long)(total >> 20), (int)appletGetFocusState());
        if (now == last) {
            stale++;
            /* Pausing threads can deadlock the watchdog on a lock a paused
             * thread holds: only with MANHUNT_WD_DUMP=1. */
            if ((stale == 8 || stale == 20 || stale == 40) && wd_dump_on)
                wd_dump();
        } else {
            stale = 0;
        }
        last = now;
    }
}

static void wd_start(void)
{
    const char *e = getenv("MANHUNT_WD");
    if (!e || *e != '1')
        return;
    s_wd_fd = open(MANHUNT_SD_BASE "/wd.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (s_wd_fd < 0) {
        printf("[MH-BOOT] watchdog: cannot open wd.txt (errno %d)\n", errno);
        return;
    }
    wd_line("[MH-WD] start");
    /* Horizon does not time-slice threads of different priority: guest threads
     * spinning on every application core can starve a default-priority thread
     * completely. Run the watchdog at the highest priority the process allows
     * (lower number = higher priority), on any core. */
    static Thread wd_thread;
    static const int prios[] = {0x10, 0x18, 0x20, 0x2B};
    for (unsigned i = 0; i < sizeof prios / sizeof prios[0]; i++) {
        if (R_SUCCEEDED(threadCreate(&wd_thread, wd_main, NULL, NULL, 0x10000, prios[i], -2))
            && R_SUCCEEDED(threadStart(&wd_thread))) {
            wd_line("[MH-WD] thread priority 0x%x", prios[i]);
            return;
        }
    }
    wd_line("[MH-WD] cannot start thread");
}

/* ---- exit tracing -------------------------------------------------------- */
/* A silent end of the process looks like a short black screen and then the
 * menu. Say when exit() or abort() is reached, on every lock-free channel. */
static void exit_note(const char *what, int status)
{
    char b[96];
    int n = snprintf(b, sizeof b, "[MH-EXIT] %s status=%d", what, status);
    svcOutputDebugString(b, (size_t)n);
    nx_send(b, (size_t)n);
    if (s_wd_fd >= 0) {
        b[n++] = '\n';
        if (write(s_wd_fd, b, (size_t)n) > 0)
            fsync(s_wd_fd);
    }
}

static void on_exit_cb(int status, void *arg)
{
    (void)arg;
    exit_note("exit() reached", status);
}

static void on_sigabrt(int sig)
{
    exit_note("abort() / signal", sig);
    svcExitProcess();
}

/* ---- crash report ------------------------------------------------------- */
/* libnx runs this on a CPU exception (data abort, bad instruction, ...) in the
 * faulting process, on the exception stack below. It writes where the fault
 * was (offsets are relative to the NRO's load address: use
 * aarch64-none-elf-addr2line -e game/build/switch/manhunt.elf) to every log
 * channel without taking s_log_lock (the faulting thread may hold it), then
 * ends the process. Because the handler ends the process itself, libnx's default
 * path (and with it an Atmosphere crash report for this process) does not run. */
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof __nx_exception_stack;
u32 __nx_exception_ignoredebug = 1;

static void crash_line(const char *line, size_t len)
{
    svcOutputDebugString(line, len);
    nx_send(line, len);
    if (s_logfp) {
        /* Raw write on the descriptor: fwrite/fflush would take the FILE lock,
         * which the faulting thread may hold. Lines are already flushed per
         * line by log_file_line(), so the descriptor is current. */
        char b[256];
        if (len > sizeof b - 1)
            len = sizeof b - 1;
        memcpy(b, line, len);
        b[len++] = '\n';
        int fd = fileno(s_logfp);
        if (fd >= 0)
            write(fd, b, len);
    }
}

void __libnx_exception_handler(ThreadExceptionDump *d)
{
    static int entered;
    if (__atomic_exchange_n(&entered, 1, __ATOMIC_SEQ_CST)) {
        svcSleepThread(3000000000ll);     /* a second fault: let the first report finish */
        svcExitProcess();
    }
    /* __start__ is an absolute symbol (value 0) in this NRO build, so the module
     * base comes from the memory region that holds the faulting pc: the start
     * of the NRO text segment. Offsets are then valid for addr2line on
     * game/build/switch/manhunt.elf. */
    uintptr_t code = 0;
    {
        MemoryInfo mi;
        u32 pg;
        if (R_SUCCEEDED(svcQueryMemory(&mi, &pg, (uintptr_t)d->pc.x)) && mi.type == MemType_CodeStatic)
            code = (uintptr_t)mi.addr;
    }
    char b[200];
    int n = snprintf(b, sizeof b, "[MH-CRASH] kind 0x%x far 0x%llx pc 0x%llx(+0x%llx) lr +0x%llx sp 0x%llx base 0x%llx",
                     (unsigned)d->error_desc, (unsigned long long)d->far.x, (unsigned long long)d->pc.x,
                     (unsigned long long)(d->pc.x - code), (unsigned long long)(d->lr.x - code),
                     (unsigned long long)d->sp.x, (unsigned long long)code);
    crash_line(b, (size_t)n);
    n = snprintf(b, sizeof b, "[MH-CRASH] x0 0x%llx x1 0x%llx x2 0x%llx x3 0x%llx",
                 (unsigned long long)d->cpu_gprs[0].x, (unsigned long long)d->cpu_gprs[1].x,
                 (unsigned long long)d->cpu_gprs[2].x, (unsigned long long)d->cpu_gprs[3].x);
    crash_line(b, (size_t)n);
    uint64_t *frame = (uint64_t *)d->fp.x;
    for (int i = 0; i < 24 && frame && !((uintptr_t)frame & 7); i++) {
        MemoryInfo info;
        u32 page;
        svcQueryMemory(&info, &page, (uintptr_t)frame);
        if (!(info.perm & Perm_R))
            break;
        n = snprintf(b, sizeof b, "[MH-CRASH]   from +0x%llx", (unsigned long long)(frame[1] - code));
        crash_line(b, (size_t)n);
        frame = (uint64_t *)frame[0];
    }
    if (s_logfp)
        fsync(fileno(s_logfp));
    svcExitProcess();                     /* replaces libnx's default; no Atmosphere crash report is made for this process */
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

    FILE *f = fopen(ENV_FILE, "r");
    char line[4096];
    while (f && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') {
            /* Longer than the buffer: a cut-off value must not be applied (a 512-byte
             * buffer once silently truncated a long MANHUNT_PAD_SCRIPT). */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n')
                ;
            printf("[MH-BOOT] config line longer than %zu bytes ignored (starts \"%.40s\")\n",
                   sizeof line - 1, line);
            continue;
        }
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
    wd_register("dumper");
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

    wd_register("guest");
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
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault()))
        s_nx_sock = nxlinkStdio();        /* started by "nxlink -s": keep its stream */
    tcp_log_connect();
    log_file_init();
    wd_register("main");
    wd_start();
    on_exit(on_exit_cb, NULL);
    signal(SIGABRT, on_sigabrt);
    {
        /* MANHUNT_FOCUS=nosuspend: never let the OS suspend the process when the
         * applet loses focus. Default (unset): libnx's default, unchanged. */
        const char *fm = getenv("MANHUNT_FOCUS");
        if (fm && !strcmp(fm, "nosuspend")) {
            appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
            printf("[MH-BOOT] focus handling: NoSuspend\n");
        }
        printf("[MH-BOOT] applet focus state %d\n", (int)appletGetFocusState());
        AppletType at = appletGetAppletType();
        printf("[MH-BOOT] applet type %d (%s)\n", (int)at,
               at == AppletType_Application ? "title takeover"
                                            : "WARNING applet mode: little memory, start hbmenu by holding R while opening a game");
    }

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
