/* Differential test of the REAL game/src/nv2a_deko3d.c on the host, against a software GPU stand-in
 * (stub_deko.c) behind the real deko3d.h. A seeded random program of GPU draws, GPU clears, operations the
 * CPU renderer takes (reject hooks + direct RAM writes), fences, presents, surface switches and texture
 * rewrites is run under several configurations; at every point where the title may read guest RAM the
 * surfaces are checksummed. Every configuration must give the same checksums as the conservative reference
 * (whole-surface transfers, uncached buffer, no texture cache, nothing resident).
 * Negative control: the fast path with the sync calls withheld MUST differ (otherwise this test could not
 * detect a missing sync). Scope: adapter logic only; no CPU cache, no GPU timing, no real GPU. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *stub_fopen(const char *p, const char *m)
{
    (void)p; (void)m;
    FILE *f = tmpfile();
    unsigned char b[64];
    memset(b, 0x11, sizeof b);
    fwrite(b, 1, sizeof b, f);
    rewind(f);
    return f;
}
#define fopen stub_fopen
#ifndef ADAPTER_SRC
#define ADAPTER_SRC "../../game/src/nv2a_deko3d.c"   /* the mutation script substitutes deliberately broken copies */
#endif
#include ADAPTER_SRC
#undef fopen

#define W 64
#define H 48
#define PITCH 320
#define S1 0x100000u
#define S2 0x200000u
#define GUEST_BYTES (4u << 20)
#define FILL_FROM 0x100000u
static uint8_t *g_cur;

ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)g_cur; }
int nv2a_backend_surface_writable(const Nv2aSurface *s) { (void)s; return 1; }
int nv2a_backend_decode_texture(const Nv2aTexture *t, uint32_t *out)
{
    const uint8_t *src = g_cur + t->offset;
    for (uint32_t y = 0; y < t->height; y++)
        for (uint32_t x = 0; x < t->width; x++) {
            uint32_t px;
            size_t at = t->color == 0x12 ? (size_t)y * t->pitch + (size_t)x * 4 : ((size_t)y * t->width + x) * 4;
            memcpy(&px, src + at, 4);
            out[(size_t)y * t->width + x] = px ^ 0x01020304u;       /* a decode that is not the identity */
        }
    return 1;
}

static uint64_t rng;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static float rndf(float a, float b) { return a + (b - a) * (float)(rnd() % 10001) / 10000.f; }

typedef struct { const char *name; const char *env[6]; int do_sync; int expect_equal; } Config;
#define MAXOBS 4096
typedef struct { uint32_t n; uint64_t sum[MAXOBS]; uint64_t vcalls, vbad; } Trace;

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
static void observe(Trace *t)
{
    if (t->n < MAXOBS)
        t->sum[t->n++] = fnv(g_cur + S1, (size_t)PITCH * H) * 31u ^ fnv(g_cur + S2, (size_t)PITCH * H);
}

static const char *const ALL[] = {"MANHUNT_DEKO_RECT", "MANHUNT_DEKO_CACHED", "MANHUNT_DEKO_TCACHE", "MANHUNT_DEKO_RESIDENT", "MANHUNT_DEKO_VERIFY"};

static void program(uint64_t seed, const Config *cfg, uint8_t *guest, Trace *tr)
{
    for (unsigned i = 0; i < 5; i++) unsetenv(ALL[i]);
    for (unsigned i = 0; cfg->env[i]; i++) {
        char k[64];
        snprintf(k, sizeof k, "%s", cfg->env[i]);
        char *eq = strchr(k, '=');
        *eq = 0;
        setenv(k, eq + 1, 1);
    }
    g_cur = guest;
    rng = seed * 2654435761u + 88172645463325252ull;
    for (size_t i = FILL_FROM; i < GUEST_BYTES; i++) guest[i] = (uint8_t)rnd();   /* surfaces and textures start random */
    tr->n = 0;
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();

    Nv2aTexture tex[4] = {
        {.offset = 0x300000, .width = 8,  .height = 8,  .pitch = 80,  .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x310000, .width = 16, .height = 16, .pitch = 64,  .color = 0x06, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x320000, .width = 32, .height = 16, .pitch = 160, .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = MENU_FILTER},
        /* render-to-texture: lies inside surface S2, also used while drawing into S2 itself (feedback): the adapter
         * must bring RAM up to date before it decodes a texture that overlaps its resident surface */
        {.offset = S2 + 8 * PITCH, .width = 16, .height = 8, .pitch = PITCH, .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
    };
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    int cur = 0;
    for (int op = 0; op < 400; op++) {
        if (rnd() % 5 == 0) cur ^= 1;                                  /* surface switch */
        Nv2aSurface s = {.color_va = cur ? S2 : S1, .width = W, .height = H, .pitch = PITCH,
                         .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
        uint8_t *surf = guest + s.color_va;
        uint32_t r = rnd() % 100;
        if (r < 50) {                                                   /* GPU draw */
            Nv2aVertex v[12];
            float dif[12][4];
            uint32_t tris = 1 + rnd() % 4, count = tris * 3, ti = rnd() % 4, textured = rnd() % 2;
            for (uint32_t i = 0; i < count; i++) {
                v[i] = (Nv2aVertex){.x = rndf(-10, W + 10), .y = rndf(-10, H + 10), .z = .5f, .rhw = 1.f,
                                    .diffuse = rnd(), .u = rndf(0, 8), .v = rndf(0, 8)};
                for (int k = 0; k < 4; k++) dif[i][k] = rndf(0, 1);
            }
            st.blend_enable = rnd() % 2; st.blend_src = 0x302; st.blend_dst = 0x303; st.blend_eq = 0x8006;
            Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1,
                                 .shading = textured ? NV2A_SHADE_MODULATE : NV2A_SHADE_DIFFUSE,
                                 .texture = textured ? &tex[ti] : NULL};
            Nv2aTransformInfo xf = {.count = count, .finite = 1, .positive_w = 1, .constant_w = 1, .texture_q_one = 1, .w = 1,
                                    .tex_u_abs_max = 8, .tex_v_abs_max = 8};
            if (!be->can_draw_transformed(&s, &info, &xf) || !be->can_draw(&s, &st, &info)) { printf("draw unexpectedly rejected\n"); exit(2); }
            Nv2aBatch b = {.vertices = v, .count = count, .texture = textured ? &tex[ti] : NULL, .state = &st,
                           .shading = info.shading, .diffuse_float = (const float (*)[4])dif};
            be->draw(&s, &b);
        } else if (r < 60) {                                            /* GPU clear */
            uint32_t argb = rnd();
            if (!be->can_clear(&s, &st, 0xF0, argb, 0)) { printf("clear unexpectedly rejected\n"); exit(2); }
            be->clear(&s, &st, 0xF0, argb, 0);
        } else if (r < 72) {                                            /* the CPU renderer takes an operation */
            uint32_t how = rnd() % 3;
            Nv2aDrawInfo bad = {.primitive = 5, .path = NV2A_DRAW_FFP, .shading = NV2A_SHADE_UNSUPPORTED};
            if (how == 0) { if (be->can_clear(&s, &st, 0xF3, 0, 0)) { printf("expected a rejection\n"); exit(2); } }
            else if (how == 1) { if (be->can_draw(&s, &st, &bad)) { printf("expected a rejection\n"); exit(2); } }
            else if (be->sync) be->sync(1);                              /* a failed batch preparation */
            uint32_t x0 = rnd() % W, y0 = rnd() % H, x1 = x0 + rnd() % (W - x0), y1 = y0 + rnd() % (H - y0);
            uint32_t px = rnd();
            for (uint32_t y = y0; y <= y1; y++) for (uint32_t x = x0; x <= x1; x++) memcpy(surf + (size_t)y * PITCH + x * 4, &px, 4);
            if (rnd() % 2) observe(tr);
        } else if (r < 80) {                                            /* fence: the title sees the GPU as done */
            if (cfg->do_sync && be->sync) be->sync(0);
            observe(tr);
        } else if (r < 92) {                                            /* present */
            if (cfg->do_sync) manhunt_nv2a_deko3d_sync();
            observe(tr);
            be->flip();
        } else {                                                        /* the title rewrites a texture */
            uint32_t ti = rnd() % 3;
            uint8_t *tp = guest + tex[ti].offset;
            for (int k = 0; k < 6; k++) tp[rnd() % (tex[ti].pitch * tex[ti].height)] = (uint8_t)rnd();
        }
    }
    if (cfg->do_sync) manhunt_nv2a_deko3d_sync();
    observe(tr);
    tr->vcalls = s_vstat.calls; tr->vbad = s_vstat.bad_calls;
    memset(&s_vstat, 0, sizeof s_vstat);
    manhunt_nv2a_deko3d_shutdown();
}

int main(int argc, char **argv)
{
    const int seeds = argc > 1 ? atoi(argv[1]) : 60;
    static const Config ref = {"reference (all fast pieces off, nothing resident)",
        {"MANHUNT_DEKO_RECT=0", "MANHUNT_DEKO_CACHED=0", "MANHUNT_DEKO_TCACHE=0", "MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1};
    static const Config cfgs[] = {
        {"all on", {NULL}, 1, 1},
        {"RECT=0", {"MANHUNT_DEKO_RECT=0", NULL}, 1, 1},
        {"CACHED=0", {"MANHUNT_DEKO_CACHED=0", NULL}, 1, 1},
        {"TCACHE=0", {"MANHUNT_DEKO_TCACHE=0", NULL}, 1, 1},
        {"RESIDENT=0", {"MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1},
        {"VERIFY=1, all on", {"MANHUNT_DEKO_VERIFY=1", NULL}, 1, 1},
        {"VERIFY=1, RESIDENT=0", {"MANHUNT_DEKO_VERIFY=1", "MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1},
        {"NEGATIVE CONTROL: all on, sync calls withheld (must differ)", {NULL}, 0, 0},
    };
    uint8_t *ga = malloc(GUEST_BYTES), *gb = malloc(GUEST_BYTES);
    static Trace A, B;
    int failures = 0;
    for (unsigned c = 0; c < sizeof cfgs / sizeof cfgs[0]; c++) {
        int differing_seeds = 0, observations = 0;
        uint64_t vcalls = 0, vbad = 0;
        for (int seed = 1; seed <= seeds; seed++) {
            program((uint64_t)seed, &ref, ga, &A);
            program((uint64_t)seed, &cfgs[c], gb, &B);
            int differs = A.n != B.n;
            for (uint32_t i = 0; i < A.n && i < B.n; i++) { observations++; if (A.sum[i] != B.sum[i]) differs = 1; }
            differing_seeds += differs;
            vcalls += B.vcalls; vbad += B.vbad;
        }
        int ok = cfgs[c].expect_equal ? differing_seeds == 0 : differing_seeds > 0;
        if (cfgs[c].env[0] && strstr(cfgs[c].env[0], "VERIFY")) ok = ok && vbad == 0 && vcalls > 0;
        printf("%-58s %s  (%d seeds, %d observations, %d seeds differ from the reference%s)\n", cfgs[c].name,
               ok ? "PASS" : "FAIL", seeds, observations, differing_seeds,
               strstr(cfgs[c].name, "VERIFY") ? (vbad ? ", verify mismatches!" : ", verify saw no mismatch") : "");
        failures += !ok;
    }
    printf(failures ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", failures);
    return failures != 0;
}
