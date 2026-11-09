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
#include <unistd.h>

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

/* The runtime's profile counter, driven by hand. The adapter must only read it: taking a clock aborts. */
_Thread_local Nv2aProfile nv2a_profile;
static int g_profile_on;
int nv2a_profile_enabled(void) { return g_profile_on; }
uint64_t xbox_HostNowNs(void) { printf("the adapter read a clock\n"); abort(); }

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
    if (d3d8_format_dxt_block_bytes(t->color)) {                    /* the decoder the CPU renderer samples through */
        for (uint32_t y = 0; y < t->height; y++)
            for (uint32_t x = 0; x < t->width; x++)
                if (!d3d8_dxt_decode_texel(src, t->color, x, y, t->width, &out[(size_t)y * t->width + x])) return 0;
        return 1;
    }
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

static const char *const ALL[] = {"MANHUNT_DEKO_RECT", "MANHUNT_DEKO_CACHED", "MANHUNT_DEKO_TCACHE", "MANHUNT_DEKO_RESIDENT", "MANHUNT_DEKO_VERIFY", "MANHUNT_DEKO_DXT1", "MANHUNT_DEKO_X8", "MANHUNT_DEKO_WRAP", "MANHUNT_DEKO_SWZ"};

static void program(uint64_t seed, const Config *cfg, uint8_t *guest, Trace *tr)
{
    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);
    setenv("MANHUNT_DEKO_DXT1", "1", 1);                            /* every configuration draws DXT1 and 0x07 too */
    setenv("MANHUNT_DEKO_X8", "1", 1);
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

    Nv2aTexture tex[6] = {
        {.offset = 0x300000, .width = 8,  .height = 8,  .pitch = 80,  .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x310000, .width = 16, .height = 16, .pitch = 64,  .color = 0x06, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x320000, .width = 32, .height = 16, .pitch = 160, .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = MENU_FILTER},
        /* render-to-texture: lies inside surface S2, also used while drawing into S2 itself (feedback): the adapter
         * must bring RAM up to date before it decodes a texture that overlaps its resident surface */
        {.offset = S2 + 8 * PITCH, .width = 16, .height = 8, .pitch = PITCH, .color = 0x12, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x330000, .width = 16, .height = 8, .pitch = 0, .color = 0x0C, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
        {.offset = 0x338000, .width = 16, .height = 8, .pitch = 0, .color = 0x07, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0},
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
            uint32_t tris = 1 + rnd() % 4, count = tris * 3, ti = rnd() % 6, textured = rnd() % 2;
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
            uint32_t ti = rnd() % 5;
            if (ti >= 3) ti++;
            uint8_t *tp = guest + tex[ti].offset;
            uint32_t span = ti >= 4 ? (uint32_t)texture_bytes(&tex[ti]) : tex[ti].pitch * tex[ti].height;
            for (int k = 0; k < 6; k++) tp[rnd() % span] = (uint8_t)rnd();
        }
    }
    if (cfg->do_sync) manhunt_nv2a_deko3d_sync();
    observe(tr);
    tr->vcalls = s_vstat.calls; tr->vbad = s_vstat.bad_calls;
    memset(&s_vstat, 0, sizeof s_vstat);
    manhunt_nv2a_deko3d_shutdown();
}

static void raster_reset(void)
{
    memset(s_rj_combo, 0, sizeof s_rj_combo);
    s_rj_combo_other = s_rj_combo_other_ns = 0;
    s_rn_transform = s_rn_unattributed = 0;
    s_rn_to = &s_rn_unattributed;
    memset(s_rx, 0, sizeof s_rx);
    memset(s_rx_target, 0, sizeof s_rx_target);
    s_rx_other_ns = s_rx_evicted_ns = 0;
    memset(s_rx_fmt, 0, sizeof s_rx_fmt);
    s_rx_target_draws_lost = 0;
    s_rx_to = &s_rx_other_ns;
    memset(&nv2a_profile, 0, sizeof nv2a_profile);
    s_rn_seen = 0;
}
static void raster_spend(uint64_t ns) { nv2a_profile.ns[NV_PROFILE_TRIANGLE] += ns; }
static uint64_t raster_of(uint32_t mask)
{
    for (int i = 0; i < RJ_COMBOS; i++)
        if (s_rj_combo[i].mask == mask) return s_rj_combo[i].raster_ns;
    return UINT64_MAX;
}

/* Flips until the adapter's every-5-flips report, returning what it printed to stdout. */
static const char *flip_to_report(const Nv2aBackend *be)
{
    static char text[65536];
    FILE *t = tmpfile();
    fflush(stdout);
    int saved = dup(1);
    dup2(fileno(t), 1);
    do be->flip(); while (s_flips % 5);
    fflush(stdout);
    dup2(saved, 1);
    close(saved);
    rewind(t);
    text[fread(text, 1, sizeof text - 1, t)] = 0;
    fclose(t);
    return text;
}

/* Each draw's raster time must land in the bucket of the refusal that sent it to the CPU, and the buckets must
 * add up to the runtime's raster total. The time comes from the test, so this checks attribution only. */
static int test_raster_attribution(uint8_t *guest)
{
    int bad = 0;
#define RCHECK(c) do { if (!(c)) { printf("raster attribution: %s is false\n", #c); bad++; } } while (0)
    const uint32_t A = 1u << RJ_SHADING, B = A | 1u << RJ_CULL;
    g_cur = guest;
    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    Nv2aSurface s = {.color_va = S1, .width = W, .height = H, .pitch = PITCH,
                     .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aRenderState cull = st;
    cull.cull_enable = 1;
    Nv2aDrawInfo good = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1, .shading = NV2A_SHADE_DIFFUSE};
    Nv2aDrawInfo refused = {.primitive = 5, .path = NV2A_DRAW_FFP, .shading = NV2A_SHADE_UNSUPPORTED};
    Nv2aTransformInfo no_vertices = {.count = 0};

    g_profile_on = 1;
    raster_reset();
    raster_spend(3);                                                    /* before any draw: nobody owns it */
    RCHECK(!be->can_draw(&s, &st, &refused));   raster_spend(100);
    RCHECK(!be->can_draw(&s, &cull, &refused)); raster_spend(1000);
    RCHECK(be->can_draw(&s, &st, &good));       raster_spend(7);        /* accepted: not any refusal's */
    RCHECK(!be->can_draw(&s, &st, &refused));   raster_spend(5);
    RCHECK(be->can_draw(&s, &st, &good));
    RCHECK(!be->can_draw_transformed(&s, &good, &no_vertices));  raster_spend(11);
    RCHECK(!be->can_draw(&s, &st, &refused));   raster_spend(2);
    RCHECK(!be->can_clear(&s, &st, 0x01, 0, 0)); raster_spend(13);      /* the raster test of a refused clear */
    RCHECK(!be->can_draw(&s, &cull, &refused)); raster_spend(20);
    const char *report = flip_to_report(be);                            /* the report must see this batch */
    char expected[512];
    snprintf(expected, sizeof expected,
        "[MH-DEKO-N] flip=%llu raster_ns=1161 unattributed=23 transform_refused=11 more_than_%d_combinations=0 combinations=2\n"
        "[MH-DEKO-N] flip=%llu ns=107 class=shading\n[MH-DEKO-N] flip=%llu ns=1020 class=cull+shading\n",
        (unsigned long long)s_flips, RJ_COMBOS, (unsigned long long)s_flips, (unsigned long long)s_flips);
    RCHECK(strstr(report, expected) != NULL);
    RCHECK(raster_of(A) == 107);
    RCHECK(raster_of(B) == 1020);
    RCHECK(s_rn_transform == 11);
    RCHECK(s_rn_unattributed == 3 + 7 + 13);
    RCHECK(raster_of(A) + raster_of(B) + s_rn_transform + s_rn_unattributed == nv2a_profile.ns[NV_PROFILE_TRIANGLE]);

    /* More combinations than the table holds: their time stays visible, and the total still adds up. */
    raster_reset();
    uint64_t expected_other = 0;
    for (uint32_t i = 0; i < 32; i++) {
        Nv2aRenderState v = st;
        v.depth_test_enable = i & 1; v.stencil_enable = (i >> 1) & 1; v.cull_enable = (i >> 2) & 1;
        if (i & 8) v.color_mask = 0xFFFFFFFFu;
        if (i & 16) v.alpha_test_enable = 1;
        RCHECK(!be->can_draw(&s, &v, &refused));
        raster_spend(1000 + i);
        if (i >= RJ_COMBOS) expected_other += 1000 + i;
    }
    be->flip();
    while (s_flips % 5) be->flip();                                     /* runs the print path under the sanitizers */
    uint64_t table = 0;
    for (int i = 0; i < RJ_COMBOS; i++) table += s_rj_combo[i].raster_ns;
    RCHECK(s_rj_combo_other == 32 - RJ_COMBOS);
    RCHECK(s_rj_combo_other_ns == expected_other);
    RCHECK(table + s_rj_combo_other_ns == nv2a_profile.ns[NV_PROFILE_TRIANGLE]);

    /* Profile off: nothing is attributed. */
    g_profile_on = 0;
    raster_reset();
    RCHECK(!be->can_draw(&s, &st, &refused));
    raster_spend(50);
    report = flip_to_report(be);
    RCHECK(strstr(report, "[MH-DEKO-C]") != NULL);                      /* the report did run */
    RCHECK(strstr(report, "[MH-DEKO-N]") == NULL);
    RCHECK(raster_of(A) == 0);
    RCHECK(s_rn_unattributed == 0);
#undef RCHECK
    manhunt_nv2a_deko3d_shutdown();
    printf("%-58s %s\n", "raster time per refusal class", bad ? "FAIL" : "PASS");
    return bad != 0;
}

/* Raster time must land on the texture that was refused, a texture rendered to must say so, and the printed report
 * must add up, past the table too. */
static int test_raster_textures(uint8_t *guest)
{
    int bad = 0;
#define XCHECK(c) do { if (!(c)) { printf("raster textures: %s is false\n", #c); bad++; } } while (0)
    g_cur = guest;
    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    Nv2aSurface s = {.color_va = S1, .width = W, .height = H, .pitch = PITCH,
                     .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aTexture a = {.offset = 0x350000, .width = 64, .height = 64, .color = 0x07, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0};
    Nv2aTexture b = a;
    b.offset = 0x80000000u | S1;                                        /* the surface, through the RAM mirror */
    Nv2aTexture c = a;
    c.color = 0x02;                                                     /* same address and size as a, another format */
    Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1,
                         .shading = NV2A_SHADE_MODULATE, .texture = &a};
    Nv2aDrawInfo plain = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1, .shading = NV2A_SHADE_DIFFUSE};
    Nv2aDrawInfo unsupported = {.primitive = 5, .path = NV2A_DRAW_FFP, .shading = NV2A_SHADE_UNSUPPORTED};

    g_profile_on = 1;
    raster_reset();
    raster_spend(5);                                                    /* before any draw */
    info.texture = &a; XCHECK(!be->can_draw(&s, &st, &info)); raster_spend(100);
    info.texture = &b; XCHECK(!be->can_draw(&s, &st, &info)); raster_spend(1000);
    info.texture = &a; XCHECK(!be->can_draw(&s, &st, &info)); raster_spend(20);
    info.texture = &c; XCHECK(!be->can_draw(&s, &st, &info)); raster_spend(40);
    XCHECK(be->can_draw(&s, &st, &plain));                    raster_spend(7);    /* accepted: no texture's */
    XCHECK(!be->can_draw(&s, &st, &unsupported));             raster_spend(3);    /* refused with no texture */
    const char *report = flip_to_report(be);
    unsigned long long f = (unsigned long long)s_flips;
    char expected[2048];
    /* Costliest first. b is the surface itself. Drawn at flip f-5 at the latest: flip_to_report flips before it prints. */
    unsigned long long d = f - 5;
    snprintf(expected, sizeof expected,
        "[MH-DEKO-X] flip=%llu raster_ns=1175 other=15 evicted=0 not_printed=0 textures=3 of=3 target_draws_lost=0\n"
        "[MH-DEKO-X] flip=%llu ns=1000 draws=1 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x0 shading=2 combiner=1 offset=0x80100000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n"
        "[MH-DEKO-X] flip=%llu ns=120 draws=2 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x0 shading=2 combiner=1 offset=0x00350000 target=0 rendered_flip=-1 bound_flip=%llu reasons=tex_format\n"
        "[MH-DEKO-X] flip=%llu ns=40 draws=1 fmt=0x02 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x0 shading=2 combiner=1 offset=0x00350000 target=0 rendered_flip=-1 bound_flip=%llu reasons=tex_format\n",
        f, f, d, d, f, d, f, d);
    XCHECK(s_flips % 5 == 0);
    XCHECK(strstr(report, expected) != NULL);
    snprintf(expected, sizeof expected, "[MH-DEKO-F] flip=%llu fmt=0x02 ns=40 alone_ns=40 draws=1\n[MH-DEKO-F] flip=%llu fmt=0x07 ns=1120 alone_ns=1120 draws=3\n", f, f);
    XCHECK(strstr(report, expected) != NULL);

    /* As in the real runtime, surface and texture both in the 0x80000000 form; the stage program separates rows; a
     * later flip moves both flips; a texture inside a surface counts; a surface only ever cleared counts. */
    raster_reset();
    Nv2aSurface high = s;
    high.color_va = 0x80100000u;
    Nv2aTexture same = a, inside = a, cleared = a;
    same.offset = 0x80100000u;
    inside.offset = 0x80100000u + PITCH * 2;
    cleared.offset = 0x80200000u;
    Nv2aSurface only_cleared = s;
    only_cleared.color_va = 0x80200000u;
    XCHECK(be->can_clear(&only_cleared, &st, 0xF0, 0, 0));
    info.texture = &same; info.stage_program = 0x11;
    XCHECK(!be->can_draw(&high, &st, &info)); raster_spend(500);
    info.stage_program = 0x22;
    XCHECK(!be->can_draw(&high, &st, &info)); raster_spend(400);
    info.stage_program = 0;
    info.texture = &inside;
    XCHECK(!be->can_draw(&high, &st, &info)); raster_spend(300);
    info.texture = &cleared;
    XCHECK(!be->can_draw(&s, &st, &info));    raster_spend(200);
    report = flip_to_report(be);
    unsigned long long f1 = (unsigned long long)s_flips, d1 = f1 - 5;
    snprintf(expected, sizeof expected,
        "[MH-DEKO-X] flip=%llu ns=500 draws=1 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x11 shading=2 combiner=1 offset=0x80100000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n"
        "[MH-DEKO-X] flip=%llu ns=400 draws=1 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x22 shading=2 combiner=1 offset=0x80100000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n"
        "[MH-DEKO-X] flip=%llu ns=300 draws=1 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x0 shading=2 combiner=1 offset=0x%08X target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n"
        "[MH-DEKO-X] flip=%llu ns=200 draws=1 fmt=0x07 size=64x64 pitch=0 levels=1 filter=0x0 addr=3,3 blend=0x0/0x0 stages=0x0 shading=2 combiner=1 offset=0x80200000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n",
        f1, d1, d1, f1, d1, d1, f1, inside.offset, d1, d1, f1, d1, d1);
    XCHECK(strstr(report, expected) != NULL);
    /* Same texture sampled again later, while the surface it matches is not rendered again: the flips must differ. */
    info.texture = &cleared;
    XCHECK(!be->can_draw(&s, &st, &info));
    report = flip_to_report(be);
    unsigned long long f2 = (unsigned long long)s_flips;
    snprintf(expected, sizeof expected,
        "offset=0x80200000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n", d1, f2 - 5);
    XCHECK(strstr(report, expected) != NULL);
    /* Rendering it again moves rendered_flip. */
    XCHECK(be->can_clear(&only_cleared, &st, 0xF0, 0, 0));
    XCHECK(!be->can_draw(&s, &st, &info));
    report = flip_to_report(be);
    unsigned long long f3 = (unsigned long long)s_flips;
    snprintf(expected, sizeof expected,
        "offset=0x80200000 target=1 rendered_flip=%llu bound_flip=%llu reasons=tex_format\n", f3 - 5, f3 - 5);
    XCHECK(strstr(report, expected) != NULL);

    raster_reset();                                                     /* more rows than are printed: the costliest ones */
    for (uint32_t i = 0; i < RX_PRINT + 4; i++) {
        Nv2aTexture t = a;
        t.offset = 0x360000 + i * 0x1000;
        info.texture = &t;
        XCHECK(!be->can_draw(&s, &st, &info));
        raster_spend(10 + i);
    }
    report = flip_to_report(be);
    snprintf(expected, sizeof expected, "not_printed=%u textures=%d of=%d", 10 + 11 + 12 + 13, RX_PRINT, RX_PRINT + 4);
    XCHECK(strstr(report, expected) != NULL);

    raster_reset();                                                     /* same address and format, another size */
    Nv2aTexture tall = a;
    tall.height = 32;
    info.texture = &a;    XCHECK(!be->can_draw(&s, &st, &info));
    info.texture = &tall; XCHECK(!be->can_draw(&s, &st, &info));
    XCHECK(s_rx[0].draws == 1 && s_rx[1].draws == 1);

    raster_reset();                                                     /* same texture refused for different reasons */
    Nv2aRenderState culled = st;
    culled.cull_enable = 1;
    info.texture = &a; info.shading = NV2A_SHADE_MODULATE;
    XCHECK(!be->can_draw(&s, &st, &info));
    XCHECK(!be->can_draw(&s, &culled, &info));
    XCHECK(s_rx[0].draws == 1 && s_rx[1].draws == 1);
    XCHECK(s_rx[0].mask == 1u << RJ_TEX_FORMAT && s_rx[1].mask == ((1u << RJ_CULL) | (1u << RJ_TEX_FORMAT)));
    report = flip_to_report(be);
    XCHECK(strstr(report, "reasons=tex_format\n") != NULL);
    XCHECK(strstr(report, "reasons=cull+tex_format\n") != NULL);

    raster_reset();                                                     /* same reasons, different shading */
    Nv2aTexture plain_tex = a;
    plain_tex.color = 0x06;
    info.texture = &plain_tex;
    XCHECK(!be->can_draw(&s, &culled, &info));
    info.shading = NV2A_SHADE_DIFFUSE;
    XCHECK(!be->can_draw(&s, &culled, &info));
    XCHECK(s_rx[0].draws == 1 && s_rx[1].draws == 1);
    info.shading = NV2A_SHADE_MODULATE;

    raster_reset();                                                     /* table full: the cheapest rows make way */
    Nv2aTexture costly = a;
    costly.offset = 0x340000;
    info.texture = &costly;
    XCHECK(!be->can_draw(&s, &st, &info));
    raster_spend(100000);
    for (uint32_t i = 0; i < RX_TEXTURES + 3; i++) {                     /* cheapest first, so these are the first to go */
        Nv2aTexture t = a;
        t.offset = 0x360000 + i * 0x1000;
        info.texture = &t;
        XCHECK(!be->can_draw(&s, &st, &info));
        raster_spend(10 + i);
    }
    report = flip_to_report(be);
    XCHECK(s_rx_evicted_ns == 10 + 11 + 12 + 13);                       /* the table had 255 free after costly */
    XCHECK(s_rx_fmt[7].ns == 10 + 11 + 12 + 13 && s_rx_fmt[7].alone_ns == 10 + 11 + 12 + 13 && s_rx_fmt[7].draws == 4);
    XCHECK(strstr(report, "offset=0x00340000") != NULL);                /* the costly row survived and is printed */
    unsigned long long total = 100000;
    for (uint32_t i = 0; i < RX_TEXTURES + 3; i++) total += 10 + i;
    snprintf(expected, sizeof expected, "fmt=0x07 ns=%llu alone_ns=%llu draws=%u\n", total, total, RX_TEXTURES + 4);
    XCHECK(strstr(report, expected) != NULL);
    uint64_t rows = 0;
    for (int i = 0; i < RX_TEXTURES; i++) rows += s_rx[i].raster_ns;
    XCHECK(rows + s_rx_evicted_ns + s_rx_other_ns == nv2a_profile.ns[NV_PROFILE_TRIANGLE]);

    raster_reset();                                                     /* an evicted row refused for other reasons: not "alone" */
    Nv2aRenderState culled2 = st;
    culled2.cull_enable = 1;
    info.texture = &costly;
    XCHECK(!be->can_draw(&s, &culled2, &info));
    raster_spend(1);
    for (uint32_t i = 0; i < RX_TEXTURES; i++) {
        Nv2aTexture t = a;
        t.offset = 0x360000 + i * 0x1000;
        info.texture = &t;
        XCHECK(!be->can_draw(&s, &st, &info));
        raster_spend(1000);
    }
    XCHECK(s_rx_fmt[7].ns == 1 && s_rx_fmt[7].alone_ns == 0 && s_rx_fmt[7].draws == 1);

    raster_reset();                                                     /* more render targets than the table holds */
    for (uint32_t i = 0; i < RX_TARGETS + 3; i++) {
        Nv2aSurface v = s;
        v.color_va = S1 + i * 0x1000;
        be->can_draw(&v, &st, &plain);
    }
    XCHECK(s_rx_target_draws_lost == 3);

    g_profile_on = 0;                                                   /* profile off: nothing recorded */
    raster_reset();
    info.texture = &a;
    XCHECK(!be->can_draw(&s, &st, &info));
    raster_spend(50);
    report = flip_to_report(be);
    XCHECK(strstr(report, "[MH-DEKO-X]") == NULL);
    XCHECK(s_rx[0].draws == 0);
    XCHECK(s_rx_target[0].bytes == 0);
#undef XCHECK
    manhunt_nv2a_deko3d_shutdown();
    printf("%-58s %s\n", "raster time per refused texture", bad ? "FAIL" : "PASS");
    return bad != 0;
}

/* 0x07 stays refused unless asked for, and is then sized and hashed as 4 bytes a texel. */
static int test_x8(uint8_t *guest)
{
    int bad = 0;
#define VCHECK(c) do { if (!(c)) { printf("x8: %s is false\n", #c); bad++; } } while (0)
    g_cur = guest;
    Nv2aSurface s = {.color_va = S1, .width = W, .height = H, .pitch = PITCH,
                     .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aTexture t = {.offset = 0x340000, .width = 64, .height = 32, .color = 0x07, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0};
    Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1,
                         .shading = NV2A_SHADE_MODULATE, .texture = &t};
    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);              /* off unless asked for */
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    VCHECK(!be->can_draw(&s, &st, &info));
    VCHECK(s_rj_any[RJ_TEX_FORMAT] == 1);
    manhunt_nv2a_deko3d_shutdown();

    setenv("MANHUNT_DEKO_DXT1", "1", 1);                            /* the other switch must not turn this one on */
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    be = manhunt_nv2a_deko3d_backend();
    VCHECK(!be->can_draw(&s, &st, &info));
    manhunt_nv2a_deko3d_shutdown();
    unsetenv("MANHUNT_DEKO_DXT1");

    setenv("MANHUNT_DEKO_X8", "1", 1);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    VCHECK(be->can_draw(&s, &st, &info));
    VCHECK(texture_bytes(&t) == 8192);                              /* 64 x 32 x 4 */
    Nv2aRenderState culled = st;
    culled.cull_enable = 1;
    VCHECK(!be->can_draw(&s, &culled, &info));                      /* refused for cull, and not for its format */
    VCHECK(s_rj_any[RJ_TEX_FORMAT] == 0);
    Nv2aTexture v = t;
    info.texture = &v;
    v.width = 48;  VCHECK(!be->can_draw(&s, &st, &info));           /* not a power of two */
    v = t; v.levels = 2; VCHECK(!be->can_draw(&s, &st, &info));
    v = t; v.offset = 0x04000000u - 4096;                           /* half of it fits in RAM */
    VCHECK(!be->can_draw(&s, &st, &info));
    /* The whole texture is hashed, up to its last byte. */
    uint8_t *last = guest + t.offset + 8191;
    uint64_t before = texture_hash(&t, guest);
    *last ^= 0x5A;
    VCHECK(texture_hash(&t, guest) != before);
    *last ^= 0x5A;
    manhunt_nv2a_deko3d_shutdown();
#undef VCHECK
    printf("%-58s %s\n", "X8R8G8B8 gate and size", bad ? "FAIL" : "PASS");
    return bad != 0;
}

/* The Morton index worked out here bit by bit, not with the adapter's masks: x on even bits, y on odd bits. */
static uint32_t morton_by_hand(uint32_t x, uint32_t y)
{
    uint32_t m = 0;
    for (uint32_t b = 0; b < 8; b++) m |= ((x >> b) & 1u) << (2 * b) | ((y >> b) & 1u) << (2 * b + 1);
    return m;
}

/* Swizzled surfaces stay refused unless asked for, and a draw on one must give, at each pixel's Morton index, what the
 * same draw gives at that pixel of a linear surface holding the same starting pixels. */
static int test_swizzled_surface(uint8_t *guest)
{
    int bad = 0;
#define ZCHECK(c) do { if (!(c)) { printf("swz: %s is false\n", #c); bad++; } } while (0)
    g_cur = guest;
    enum { ZW = 64, ZH = 64, ZPITCH = ZW * 4, LIN = 0x3A0000, ZED = 0x3B0000 };
    Nv2aSurface lin = {.color_va = LIN, .width = ZW, .height = ZH, .pitch = ZPITCH,
                       .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aSurface zed = lin;
    zed.color_va = ZED;
    zed.format_raw = 0x06060224u;                                   /* colour 4, zeta 2, type 2, 2^6 x 2^6 */
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1, .shading = NV2A_SHADE_DIFFUSE};
    Nv2aRenderState culled = st, inv = st, alpha = st;
    culled.cull_enable = 1;
    inv.blend_enable = 1; inv.blend_eq = 0x8006; inv.blend_src = 0x307; inv.blend_dst = 0;
    alpha.alpha_test_enable = 1; alpha.alpha_func = 0x204; alpha.alpha_ref = 37;

    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);              /* off unless asked for */
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    ZCHECK(!be->can_draw(&zed, &st, &info));
    ZCHECK(s_rj_any[RJ_SURFACE] == 1);
    ZCHECK(!be->can_draw(&lin, &culled, &info));
    ZCHECK(!be->can_draw(&lin, &inv, &info));
    ZCHECK(!be->can_draw(&lin, &alpha, &info));
    ZCHECK(be->can_draw(&lin, &st, &info));
    manhunt_nv2a_deko3d_shutdown();

    setenv("MANHUNT_DEKO_SWZ", "1", 1);
    for (int resident = 0; resident < 2; resident++) {
        setenv("MANHUNT_DEKO_RESIDENT", resident ? "1" : "0", 1);
        if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
        be = manhunt_nv2a_deko3d_backend();
        memset(s_rj_any, 0, sizeof s_rj_any);
        ZCHECK(be->can_draw(&zed, &st, &info));
        ZCHECK(be->can_draw(&lin, &culled, &info));
        ZCHECK(s_rj_any[RJ_CULL] == 0);
        ZCHECK(be->can_draw(&lin, &inv, &info));
        ZCHECK(be->can_draw(&lin, &alpha, &info));
        Nv2aSurface w = zed;                                        /* the whole surface, or not at all */
        w.width = 32;    ZCHECK(!be->can_draw(&w, &st, &info));
        w = zed; w.height = 32; ZCHECK(!be->can_draw(&w, &st, &info));
        w = zed; w.pitch = ZPITCH + 64; ZCHECK(!be->can_draw(&w, &st, &info));
        w = zed; w.format_raw = 0x06060204u; ZCHECK(!be->can_draw(&w, &st, &info));   /* another zeta */
        w = zed; w.format_raw = 0x06061224u; ZCHECK(!be->can_draw(&w, &st, &info));   /* anti-aliased */

        for (uint32_t y = 0; y < ZH; y++)
            for (uint32_t x = 0; x < ZW; x++) {
                const uint32_t px = 0x10000000u + (y << 16) + (x << 8) + (x ^ y);
                memcpy(guest + LIN + (size_t)y * ZPITCH + (size_t)x * 4, &px, 4);
                memcpy(guest + ZED + (size_t)morton_by_hand(x, y) * 4, &px, 4);
            }
        Nv2aVertex v[3] = {{.x = 3, .y = 5, .z = .5f, .rhw = 1.f}, {.x = 50, .y = 9, .z = .5f, .rhw = 1.f}, {.x = 20, .y = 60, .z = .5f, .rhw = 1.f}};
        float dif[3][4] = {{.5f, .25f, .75f, 1.f}, {.5f, .25f, .75f, 1.f}, {.5f, .25f, .75f, 1.f}};
        Nv2aBatch b = {.vertices = v, .count = 3, .state = &st, .shading = NV2A_SHADE_DIFFUSE, .diffuse_float = (const float (*)[4])dif};
        be->draw(&lin, &b);
        manhunt_nv2a_deko3d_sync();
        be->draw(&zed, &b);
        manhunt_nv2a_deko3d_sync();
        uint32_t differ = 0, changed = 0;
        for (uint32_t y = 0; y < ZH; y++)
            for (uint32_t x = 0; x < ZW; x++) {
                uint32_t l, z;
                memcpy(&l, guest + LIN + (size_t)y * ZPITCH + (size_t)x * 4, 4);
                memcpy(&z, guest + ZED + (size_t)morton_by_hand(x, y) * 4, 4);
                differ += l != z;
                changed += l != 0x10000000u + (y << 16) + (x << 8) + (x ^ y);
            }
        ZCHECK(differ == 0);
        ZCHECK(changed > 100);                                      /* the draw did something, and not everywhere */
        ZCHECK(changed < ZW * ZH);
        manhunt_nv2a_deko3d_shutdown();
    }
#undef ZCHECK
    printf("%-58s %s\n", "swizzled surface", bad ? "FAIL" : "PASS");
    return bad != 0;
}

/* Wrap, mirror and the tent-mip filter stay refused unless asked for; border stays refused always. */
static int test_wrap_gate(uint8_t *guest)
{
    int bad = 0;
#define WCHECK(c) do { if (!(c)) { printf("wrap: %s is false\n", #c); bad++; } } while (0)
    g_cur = guest;
    Nv2aSurface s = {.color_va = S1, .width = W, .height = H, .pitch = PITCH,
                     .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aTexture t = {.offset = 0x340000, .width = 64, .height = 32, .color = 0x06, .addr_u = 1, .addr_v = 1, .levels = 1, .filter = 0};
    Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1,
                         .shading = NV2A_SHADE_MODULATE, .texture = &t};
    Nv2aTexture f = t;
    f.addr_u = f.addr_v = 3; f.filter = TENT_MIP_FILTER;
    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);              /* off unless asked for */
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    WCHECK(!be->can_draw(&s, &st, &info));
    WCHECK(s_rj_any[RJ_TEX_ADDR] == 1);
    info.texture = &f;
    WCHECK(!be->can_draw(&s, &st, &info));
    WCHECK(s_rj_any[RJ_TEX_FILTER] == 1);
    WCHECK(!filter_is_tent(TENT_MIP_FILTER));
    manhunt_nv2a_deko3d_shutdown();

    setenv("MANHUNT_DEKO_WRAP", "1", 1);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    WCHECK(be->can_draw(&s, &st, &info));                           /* tent-mip */
    WCHECK(filter_is_tent(TENT_MIP_FILTER) && filter_is_tent(MENU_FILTER));
    info.texture = &t;
    for (uint32_t u = 0; u <= 5; u++)
        for (uint32_t v = 0; v <= 5; v++) {
            t.addr_u = u; t.addr_v = v;
            const int ok = u >= 1 && u <= 3 && v >= 1 && v <= 3;
            WCHECK(be->can_draw(&s, &st, &info) == ok);
        }
    t.addr_u = t.addr_v = 1;
    Nv2aRenderState culled = st;
    culled.cull_enable = 1;
    memset(s_rj_any, 0, sizeof s_rj_any);
    WCHECK(!be->can_draw(&s, &culled, &info));                      /* refused for cull, and not for its addressing */
    WCHECK(s_rj_any[RJ_TEX_ADDR] == 0);
    t.filter = 0x03000000u;
    memset(s_rj_any, 0, sizeof s_rj_any);
    WCHECK(!be->can_draw(&s, &st, &info));                          /* other filters stay refused */
    WCHECK(s_rj_any[RJ_TEX_FILTER] == 1);
    manhunt_nv2a_deko3d_shutdown();
#undef WCHECK
    printf("%-58s %s\n", "wrap and filter gate", bad ? "FAIL" : "PASS");
    return bad != 0;
}

/* DXT1 stays refused unless asked for, odd shapes stay refused, and the decoder gives the texels a hand-built block
 * should. The differential test covers cache and sync logic only: its stub GPU ignores UVs, sampler, filter, alpha test,
 * blend state and the shader, and its reference is the same stub, so nothing here compares with the CPU renderer. */
static int test_dxt1(uint8_t *guest)
{
    int bad = 0;
#define DCHECK(c) do { if (!(c)) { printf("dxt1: %s is false\n", #c); bad++; } } while (0)
    g_cur = guest;
    Nv2aSurface s = {.color_va = S1, .width = W, .height = H, .pitch = PITCH,
                     .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1, .format_raw = 0x124};
    Nv2aRenderState st;
    memset(&st, 0, sizeof st);
    st.color_mask = 0x01010101u;
    Nv2aTexture t = {.offset = 0x340000, .width = 16, .height = 16, .color = 0x0C, .addr_u = 3, .addr_v = 3, .levels = 1, .filter = 0};
    Nv2aDrawInfo info = {.primitive = 5, .path = NV2A_DRAW_FFP, .combiner_seen = 1, .has_texcoords = 1,
                         .shading = NV2A_SHADE_MODULATE, .texture = &t};

    for (unsigned i = 0; i < 9; i++) unsetenv(ALL[i]);              /* off unless asked for */
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    const Nv2aBackend *be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    DCHECK(!be->can_draw(&s, &st, &info));
    DCHECK(s_rj_any[RJ_TEX_FORMAT] == 1);
    manhunt_nv2a_deko3d_shutdown();

    setenv("MANHUNT_DEKO_DXT1", "0", 1);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    be = manhunt_nv2a_deko3d_backend();
    DCHECK(!be->can_draw(&s, &st, &info));
    manhunt_nv2a_deko3d_shutdown();

    setenv("MANHUNT_DEKO_DXT1", "1", 1);
    if (!manhunt_nv2a_deko3d_init()) { printf("init failed\n"); exit(2); }
    be = manhunt_nv2a_deko3d_backend();
    memset(s_rj_any, 0, sizeof s_rj_any);
    DCHECK(be->can_draw(&s, &st, &info));
    DCHECK(texture_bytes(&t) == 128);                               /* 4 x 4 blocks of 8 bytes */
    Nv2aRenderState culled = st;
    culled.cull_enable = 1;
    DCHECK(!be->can_draw(&s, &culled, &info));                      /* refused for cull, and not for its format */
    DCHECK(s_rj_any[RJ_TEX_FORMAT] == 0);
    Nv2aTexture v = t;
    info.texture = &v;
    v.width = 2;    DCHECK(be->can_draw(&s, &st, &info));           /* smaller than a block: one block a row, as the decoder walks */
    DCHECK(texture_bytes(&v) == 32);                                /* 1 x 4 blocks */
    v = t; v.height = 2; DCHECK(be->can_draw(&s, &st, &info));
    DCHECK(texture_bytes(&v) == 32);                                /* 4 x 1 blocks: the 16x2 of run 13 */
    v = t; v.width = 12; DCHECK(!be->can_draw(&s, &st, &info));     /* not a power of two */
    v = t; v.levels = 2; DCHECK(!be->can_draw(&s, &st, &info));
    v = t; v.offset = 0x04000000u - 8;                              /* one block fits in RAM, sixteen do not */
    DCHECK(!be->can_draw(&s, &st, &info));
    v = t; v.color = 0x0F; DCHECK(!be->can_draw(&s, &st, &info));   /* DXT5 stays refused */
    manhunt_nv2a_deko3d_shutdown();

    /* A change to any byte of a DXT1 texture must change its hash, or the GPU draws a stale cached copy. Spans are
     * literal, not from texture_bytes, and include a tall shape: 16x8 and 8x16 are both 64 bytes. */
    static const uint32_t shapes[][2] = {{16, 8}, {8, 16}, {4, 4}, {16, 2}};
    static const uint32_t spans[] = {64, 64, 8, 32};
    for (unsigned i = 0; i < 4; i++) {
        Nv2aTexture h = t;
        h.width = shapes[i][0]; h.height = shapes[i][1];
        for (uint32_t at = 0; at < spans[i]; at++) {
            uint8_t *byte = guest + h.offset + at;
            uint64_t before = texture_hash(&h, guest);
            *byte ^= 0x5A;
            DCHECK(texture_hash(&h, guest) != before);
            *byte ^= 0x5A;
        }
    }

    /* Index 0, 1, 2, 3 across each row. Red and blue, in both orders. */
    static const uint8_t blocks[16] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4,
                                       0x1F, 0x00, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4};
    memcpy(guest + t.offset, blocks, sizeof blocks);
    Nv2aTexture d = t;
    d.width = 8; d.height = 4;
    uint32_t px[32];
    DCHECK(nv2a_backend_decode_texture(&d, px));
    static const uint32_t expected[8] = {0xFFFF0000u, 0xFF0000FFu, 0xFFAA0055u, 0xFF5500AAu,   /* c0 > c1: four colours */
                                         0xFF0000FFu, 0xFFFF0000u, 0xFF7F007Fu, 0x00000000u};  /* c0 < c1: index 3 is transparent */
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 8; x++)
            if (px[y * 8 + x] != expected[x]) { printf("dxt1: texel %u,%u is %08X, expected %08X\n", x, y, px[y * 8 + x], expected[x]); bad++; }
#undef DCHECK
    printf("%-58s %s\n", "DXT1 gate and decode", bad ? "FAIL" : "PASS");
    return bad != 0;
}

int main(int argc, char **argv)
{
    const int seeds = argc > 1 ? atoi(argv[1]) : 60;
    static const Config ref = {"reference (all fast pieces off, nothing resident)",
        {"MANHUNT_DEKO_RECT=0", "MANHUNT_DEKO_CACHED=0", "MANHUNT_DEKO_TCACHE=0", "MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1};
    static const Config cfgs[] = {
        {"defaults (resident off)", {NULL}, 1, 1},
        {"all on", {"MANHUNT_DEKO_RESIDENT=1", NULL}, 1, 1},
        {"RECT=0", {"MANHUNT_DEKO_RECT=0", "MANHUNT_DEKO_RESIDENT=1", NULL}, 1, 1},
        {"CACHED=0", {"MANHUNT_DEKO_CACHED=0", "MANHUNT_DEKO_RESIDENT=1", NULL}, 1, 1},
        {"TCACHE=0", {"MANHUNT_DEKO_TCACHE=0", "MANHUNT_DEKO_RESIDENT=1", NULL}, 1, 1},
        {"RESIDENT=0", {"MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1},
        {"VERIFY=1, all on", {"MANHUNT_DEKO_VERIFY=1", "MANHUNT_DEKO_RESIDENT=1", NULL}, 1, 1},
        {"VERIFY=1, RESIDENT=0", {"MANHUNT_DEKO_VERIFY=1", "MANHUNT_DEKO_RESIDENT=0", NULL}, 1, 1},
        {"NEGATIVE CONTROL: all on, sync calls withheld (must differ)", {"MANHUNT_DEKO_RESIDENT=1", NULL}, 0, 0},
    };
    uint8_t *ga = malloc(GUEST_BYTES), *gb = malloc(GUEST_BYTES);
    static Trace A, B;
    int failures = 0;
    /* DEKO_FAIL_FAST (mutation runs): stop at the first failure, quick tests first. Only a surviving mutant runs it all. */
    const int fail_fast = getenv("DEKO_FAIL_FAST") != NULL;
    if (fail_fast) {
        failures += test_raster_attribution(ga);
        failures += test_raster_textures(ga);
        failures += test_dxt1(ga);
        failures += test_x8(ga);
        failures += test_wrap_gate(ga);
        failures += test_swizzled_surface(ga);
        if (failures) { printf("RESULT: %d FAILED\n", failures); return 1; }
    }
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
            if (fail_fast && differs && cfgs[c].expect_equal) break;
        }
        int ok = cfgs[c].expect_equal ? differing_seeds == 0 : differing_seeds > 0;
        if (cfgs[c].env[0] && strstr(cfgs[c].env[0], "VERIFY")) ok = ok && vbad == 0 && vcalls > 0;
        printf("%-58s %s  (%d seeds, %d observations, %d seeds differ from the reference%s)\n", cfgs[c].name,
               ok ? "PASS" : "FAIL", seeds, observations, differing_seeds,
               strstr(cfgs[c].name, "VERIFY") ? (vbad ? ", verify mismatches!" : ", verify saw no mismatch") : "");
        failures += !ok;
        if (fail_fast && failures) { printf("RESULT: %d FAILED\n", failures); return 1; }
    }
    if (!fail_fast) {
        failures += test_raster_attribution(ga);
        failures += test_raster_textures(ga);
        failures += test_dxt1(ga);
        failures += test_x8(ga);
        failures += test_wrap_gate(ga);
        failures += test_swizzled_surface(ga);
    }
    printf(failures ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", failures);
    free(ga);
    free(gb);
    return failures != 0;
}
