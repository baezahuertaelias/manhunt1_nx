/*
 * Gate (a)/(b) harness: run lifted XBE functions against the cases in
 * vectors.h and produce one line per case in reference.py's format.
 *
 * Guest memory is a 64 MiB arena wherever the host puts it. Every guest
 * access goes through XBOX_PTR (guest VA + g_xbox_mem_offset), so a wrong
 * translation shows up as a mismatch or a fault, never as a silent pass.
 *
 * Linux build: maps the XBE image into the arena and prints lines to stdout
 * for compare.py. Switch build (__SWITCH__): compares on device against the
 * embedded reference.h and reports through the console and
 * svcOutputDebugString. The proof functions only touch the stack and the data
 * scratch area (checked by the scr= hash), so the Switch build does not load
 * the XBE image.
 */
#include "recomp_types.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <sys/mman.h>
#endif

#define GUEST_MEM    0x04000000u
#define STACK_TOP    0x00D00000u
#define RET_SENTINEL 0x03FFF000u
#define DATA         0x00E00000u

struct proof_mem { uint32_t va, len; const uint8_t *data; };
struct proof_case {
    uint32_t fn; uint16_t cw; const char *label;
    uint32_t in_ecx, in_ebx, in_esi, in_edi;
    uint32_t npush; uint32_t push[8];
    uint32_t nmem; struct proof_mem mem[4];
    uint32_t out_va, out_len;
};
#include "vectors.h"

/* Runtime state the generated code expects (normally xboxrecomp's runtime). */
ptrdiff_t g_xbox_mem_offset;
int g_force_return;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top;
RECOMP_TLS uint32_t g_fs_base, g_seh_ebp, g_eflags, g_mxcsr;
RECOMP_TLS int g_df, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;

void sub_0001A9C0(void);
void sub_00089CF0(void);
void sub_000E3640(void);
void sub_00205A00(void);
void sub_00205D90(void);

static const struct { uint32_t va; void (*fn)(void); } lifted[] = {
    {0x0001A9C0u, sub_0001A9C0},
    {0x00089CF0u, sub_00089CF0},
    {0x000E3640u, sub_000E3640},
    {0x00205A00u, sub_00205A00},
    {0x00205D90u, sub_00205D90},
};

static uint8_t *arena;

static void (*lookup(uint32_t va))(void) {
    for (size_t i = 0; i < sizeof lifted / sizeof lifted[0]; i++)
        if (lifted[i].va == va) return lifted[i].fn;
    return NULL;
}

/* Run case i and format its result line into buf. Returns 0 on success. */
static int run_case(int i, char *buf, size_t size) {
    const struct proof_case *c = &cases[i];
    void (*fn)(void) = lookup(c->fn);
    if (!fn) { snprintf(buf, size, "%04d no lifted function for %08x", i, c->fn); return -1; }

    memset(arena + STACK_TOP - 0x1000, 0, 0x1000);
    memset(arena + DATA, 0, 0x2000);
    for (uint32_t j = 0; j < c->nmem; j++)
        memcpy((void *)XBOX_PTR(c->mem[j].va), c->mem[j].data, c->mem[j].len);
    g_esp = STACK_TOP;
    for (uint32_t j = 0; j < c->npush; j++) PUSH32(g_esp, c->push[j]);
    PUSH32(g_esp, RET_SENTINEL);
    g_eax = 0xEAEAEAEAu; g_edx = 0xEDEDEDEDu; g_ebp = 0xBEBEBEBEu;
    g_ecx = c->in_ecx; g_ebx = c->in_ebx; g_esi = c->in_esi; g_edi = c->in_edi;
    g_eflags = 0; g_df = 0;
    memset(g_fp_stack, 0, sizeof g_fp_stack);   /* as after fninit in reference.py */
    g_fp_control_word = c->cw;
    g_fp_top = 0;

    fn();

    /* FNV-1a 64 over the stack page and the data scratch area, as in
     * reference.py: catches stray writes outside the compared output. */
    uint64_t scr = 0xCBF29CE484222325ull;
    const uint8_t *regions[2] = {arena + STACK_TOP - 0x1000, arena + DATA};
    const uint32_t sizes[2] = {0x1000, 0x2000};
    for (int r = 0; r < 2; r++)
        for (uint32_t j = 0; j < sizes[r]; j++)
            scr = (scr ^ regions[r][j]) * 0x100000001B3ull;

    /* x87 TOP after the call; st(0) as double bits when the stack is not empty. */
    char st0[17] = "-";
    if (g_fp_top) {
        uint64_t bits;
        memcpy(&bits, &g_fp_stack[g_fp_top], sizeof bits);
        snprintf(st0, sizeof st0, "%016llx", (unsigned long long)bits);
    }
    int n = snprintf(buf, size,
        "%04d fn=%08x cw=%04x %s eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x esp=%08x top=%d st0=%s scr=%016llx mem=",
        i, c->fn, c->cw, c->label, g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp, g_fp_top, st0,
        (unsigned long long)scr);
    for (uint32_t j = 0; j < c->out_len && (size_t)n + 3 < size; j++)
        n += snprintf(buf + n, size - (size_t)n, "%02x", MEM8(c->out_va + j));
    return 0;
}

#ifdef __SWITCH__

#include "reference.h"

/* Same gating rule as compare.py: "nan" cases and x87 cases at 64-bit
 * precision control are reported, not gating. */
static int gating(const struct proof_case *c) {
    if (strcmp(c->label, "nan") == 0) return 0;
    if (strcmp(c->label, "x87") == 0 && c->cw == 0x037F) return 0;
    return 1;
}

static void report(const char *s) {
    printf("%s\n", s);
    svcOutputDebugString(s, strlen(s));
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char line[512];
    arena = aligned_alloc(0x1000, GUEST_MEM);
    if (!arena) {
        report("RECOMP-PROOF FAIL: cannot allocate 64 MiB guest arena");
    } else {
        g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)arena;
        snprintf(line, sizeof line, "RECOMP-PROOF arena=%p cases=%d", (void *)arena, CASE_COUNT);
        report(line);
        int exact = 0, gate_total = 0, gate_bad = 0;
        for (int i = 0; i < CASE_COUNT; i++) {
            run_case(i, line, sizeof line);
            int ok = strcmp(line, reference_lines[i]) == 0;
            exact += ok;
            if (gating(&cases[i])) { gate_total++; gate_bad += !ok; }
            if (!ok && gating(&cases[i])) report(line);
        }
        /* No gating cases would make PASS vacuous. */
        snprintf(line, sizeof line, "RECOMP-PROOF %s exact=%d/%d gating_mismatches=%d/%d",
                 (gate_bad || gate_total == 0) ? "FAIL" : "PASS", exact, CASE_COUNT, gate_bad, gate_total);
        report(line);
    }
    consoleUpdate(NULL);

    for (int frame = 0; frame < 600 && appletMainLoop(); frame++) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}

#else

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Map the XBE headers and sections at their original guest VAs. */
static void load_xbe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *x = malloc((size_t)size);
    if (!x || fread(x, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "read %s failed\n", path); exit(2); }
    fclose(f);
    if (memcmp(x, "XBEH", 4) != 0) { fprintf(stderr, "%s: not an XBE\n", path); exit(2); }
    uint32_t base = rd32(x + 0x104), hdr = rd32(x + 0x108);
    uint32_t nsec = rd32(x + 0x11C), sec = rd32(x + 0x120) - base;
    memcpy(arena + base, x, hdr);
    for (uint32_t i = 0; i < nsec; i++) {
        const uint8_t *s = x + sec + i * 56;
        uint32_t va = rd32(s + 4), raw = rd32(s + 12), rsize = rd32(s + 16);
        if ((uint64_t)va + rsize > GUEST_MEM || (uint64_t)raw + rsize > (uint64_t)size) {
            fprintf(stderr, "section %u out of range\n", i); exit(2);
        }
        memcpy(arena + va, x + raw, rsize);
    }
    free(x);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s DEFAULT.xbe\n", argv[0]); return 2; }
    arena = mmap(NULL, GUEST_MEM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) { perror("mmap"); return 2; }
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)arena;
    fprintf(stderr, "arena=%p offset=0x%llx\n", (void *)arena, (unsigned long long)g_xbox_mem_offset);
    load_xbe(argv[1]);

    char line[512];
    for (int i = 0; i < CASE_COUNT; i++) {
        if (run_case(i, line, sizeof line) != 0) { fprintf(stderr, "%s\n", line); return 2; }
        printf("%s\n", line);
    }
    return 0;
}

#endif
