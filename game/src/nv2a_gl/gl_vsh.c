/*
 * gl_vsh.c -- NV2A vertex microcode to GLSL.
 *
 * The translation mirrors the executor's CPU interpreter (vp_run in
 * nv2a_pb_exec.c) instruction for instruction: the same field decode (the
 * 128-bit layout documented by xemu's vsh.c), the same parallel issue -- MAC
 * and ILU read every source before either writes -- and the same output
 * registers. The two are kept deliberately alike so a picture that differs
 * between the CPU and GPU renderers points at the renderer, not at the
 * decode.
 *
 * All three transform paths end the same way. The shader produces a
 * position in real surface pixels with w still the clip w, and turns that
 * into GL clip space as (screen * w) mapped to NDC. For every path the
 * executor handles, screen*w is linear in the clip-space position, so the
 * GPU's own near-plane clipping is exact -- no CPU clipping is needed.
 *
 * Surface rows are top-first in guest memory and in the framebuffer objects
 * alike (y_ndc = 2y/H - 1), so a surface later sampled as a texture needs no
 * flip; only presentation flips.
 */
#include "gl_vsh.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *p;
    size_t left;
    int    overflow;
} Out;

static void emit(Out *o, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (o->overflow)
        return;
    va_start(ap, fmt);
    n = vsnprintf(o->p, o->left, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->left) {
        o->overflow = 1;
        return;
    }
    o->p += n;
    o->left -= (size_t)n;
}

static uint32_t f(const uint32_t *t, int dw, int pos, int bits)
{
    return (t[dw] >> pos) & ((1u << bits) - 1u);
}

static const char *const s_swz = "xyzw";
extern int nv2a_shader_vk;

/* Source A (0), B (1) or C (2) as a GLSL vec4 expression. */
static void src(Out *o, const uint32_t *t, int which, int consts_local)
{
    uint32_t mux, reg, neg, sw;
    char comp[5];

    switch (which) {
    case 0:  mux = f(t, 2, 26, 2); reg = f(t, 2, 28, 4);
             neg = f(t, 1, 8, 1);  sw = f(t, 1, 0, 8);  break;
    case 1:  mux = f(t, 2, 11, 2); reg = f(t, 2, 13, 4);
             neg = f(t, 2, 25, 1); sw = f(t, 2, 17, 8); break;
    default: mux = f(t, 3, 28, 2);
             reg = (f(t, 2, 0, 2) << 2) | f(t, 3, 30, 2);
             neg = f(t, 2, 10, 1); sw = f(t, 2, 2, 8);  break;
    }
    comp[0] = s_swz[(sw >> 6) & 3];
    comp[1] = s_swz[(sw >> 4) & 3];
    comp[2] = s_swz[(sw >> 2) & 3];
    comp[3] = s_swz[sw & 3];
    comp[4] = 0;

    emit(o, "%s", neg ? "-" : "");
    if (mux == 1) {
        if (reg < 12)
            emit(o, "R%u.%s", reg, comp);
        else if (reg == 12)
            emit(o, "oPos.%s", comp);
        else
            emit(o, "vec4(0.0).%s", comp);
    } else if (mux == 2) {
        emit(o, "v%u.%s", f(t, 1, 9, 4), comp);
    } else if (mux == 3) {
        uint32_t ci = f(t, 1, 13, 8);
        const char *arr = consts_local ? "cl" : "c";
        /* Vulkan: one 192-constant set per instance (nv2a_vk instancing). */
        const char *base = nv2a_shader_vk && !consts_local ? "nv2a_ib + " : "";
        if (f(t, 3, 1, 1))
            emit(o, "%s[%sclamp(%u + a0, 0, 191)].%s", arr, base, ci, comp);
        else
            emit(o, "%s[%s%u].%s", arr, base, ci < 192 ? ci : 191, comp);
    } else {
        emit(o, "vec4(0.0).%s", comp);
    }
}

/* ".xz" for mask bits 3=x .. 0=w; empty when nothing is written. */
static void mask_str(uint32_t mask, char out[6])
{
    int i, n = 0;

    out[n++] = '.';
    for (i = 0; i < 4; i++)
        if (mask & (8u >> i))
            out[n++] = s_swz[i];
    out[n] = 0;
}

/* dst.mask = val.mask; */
static void write_masked(Out *o, const char *dst, uint32_t mask, const char *val)
{
    char m[6];

    if (!mask)
        return;
    mask_str(mask, m);
    emit(o, "        %s%s = (%s)%s;\n", dst, m, val, m);
}

static const char *const s_out_names[13] = {
    "oPos", "o1", "o2", "oD0", "oD1", "oFog", "oPts", "oB0", "oB1",
    "oT0", "oT1", "oT2", "oT3",
};

int nv2a_gl_vsh_program(const uint32_t (*prog)[4], uint32_t slots,
                        uint32_t start, char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    uint32_t pc;
    int writes_consts = 0;

    for (pc = start; pc < slots; pc++) {
        const uint32_t *t = prog[pc];
        if (f(t, 3, 12, 4) && !f(t, 3, 11, 1))
            writes_consts = 1;
        if (f(t, 3, 0, 1))
            break;
    }

    emit(&o, "void nv2a_program(void)\n{\n");
    emit(&o, "    int a0 = 0;\n");
    if (writes_consts)
        emit(&o, "    vec4 cl[192];\n    for (int i = 0; i < 192; i++) cl[i] = c[%si];\n",
             nv2a_shader_vk ? "nv2a_ib + " : "");

    for (pc = start; pc < slots; pc++) {
        const uint32_t *t = prog[pc];
        uint32_t mac = f(t, 1, 21, 4), ilu = f(t, 1, 25, 3);
        uint32_t omask = f(t, 3, 12, 4);
        uint32_t rd = f(t, 3, 20, 4);

        emit(&o, "    { /* %u */\n        vec4 A = ", pc);
        src(&o, t, 0, writes_consts);
        emit(&o, ";\n        vec4 B = ");
        src(&o, t, 1, writes_consts);
        emit(&o, ";\n        vec4 C = ");
        src(&o, t, 2, writes_consts);
        emit(&o, ";\n        vec4 m = vec4(0.0), l = vec4(0.0);\n");

        switch (mac) {
        case 1:  emit(&o, "        m = A;\n"); break;
        case 2:  emit(&o, "        m = A * B;\n"); break;
        case 3:  emit(&o, "        m = A + C;\n"); break;
        case 4:  emit(&o, "        m = A * B + C;\n"); break;
        case 5:  emit(&o, "        m = vec4(dot(A.xyz, B.xyz));\n"); break;
        case 6:  emit(&o, "        m = vec4(dot(A.xyz, B.xyz) + B.w);\n"); break;
        case 7:  emit(&o, "        m = vec4(dot(A, B));\n"); break;
        case 8:  emit(&o, "        m = vec4(1.0, A.y * B.y, A.z, B.w);\n"); break;
        case 9:  emit(&o, "        m = min(A, B);\n"); break;
        case 10: emit(&o, "        m = max(A, B);\n"); break;
        case 11: emit(&o, "        m = vec4(lessThan(A, B));\n"); break;
        case 12: emit(&o, "        m = vec4(greaterThanEqual(A, B));\n"); break;
        default: break;
        }
        switch (ilu) {
        case 1: emit(&o, "        l = C;\n"); break;
        case 2: emit(&o, "        l = vec4(nv2a_rcp(C.x));\n"); break;
        case 3: emit(&o, "        l = vec4(nv2a_rcc(C.x));\n"); break;
        case 4: emit(&o, "        l = vec4(nv2a_rcp(sqrt(abs(C.x))));\n"); break;
        case 5: emit(&o, "        l = nv2a_exp(C.x);\n"); break;
        case 6: emit(&o, "        l = nv2a_log(C.x);\n"); break;
        case 7: emit(&o, "        l = nv2a_lit(C);\n"); break;
        default: break;
        }

        if (mac == 13) {
            emit(&o, "        a0 = int(floor(A.x + 0.001));\n");
        } else if (mac && rd < 13) {
            char dst[8];
            if (rd == 12) snprintf(dst, sizeof dst, "oPos");
            else          snprintf(dst, sizeof dst, "R%u", rd);
            write_masked(&o, dst, f(t, 3, 24, 4), "m");
        }
        if (ilu) {
            uint32_t ld = mac ? 1 : rd;              /* paired ILU -> R1 */
            if (ld < 13) {
                char dst[8];
                if (ld == 12) snprintf(dst, sizeof dst, "oPos");
                else          snprintf(dst, sizeof dst, "R%u", ld);
                write_masked(&o, dst, f(t, 3, 16, 4), "l");
            }
        }
        if (omask) {
            const char *val = f(t, 3, 2, 1) ? "l" : "m";
            uint32_t addr = f(t, 3, 3, 8);
            if (f(t, 3, 11, 1)) {
                if (addr < 13)
                    write_masked(&o, s_out_names[addr], omask, val);
            } else {
                char dst[48];
                if (f(t, 3, 1, 1))
                    snprintf(dst, sizeof dst, "cl[clamp(%u + a0, 0, 191)]", addr);
                else
                    snprintf(dst, sizeof dst, "cl[%u]", addr < 192 ? addr : 191);
                write_masked(&o, dst, omask, val);
            }
        }
        emit(&o, "    }\n");
        if (f(t, 3, 0, 1))                          /* FINAL */
            break;
    }
    emit(&o, "}\n");
    return o.overflow ? -1 : (int)(cap - o.left);
}

/* The NV2A rasterizer takes screen positions as fixed point with a 4-bit
 * fraction, truncated (xemu's roundScreenCoords). Xbox D3D's viewport
 * offset carries a +0.53125 bias, so a clip-space full-screen pass starts at
 * 0.53125 -- 0.5 on the hardware, which covers row and column 0; unsnapped,
 * GL left them out and NFSU2's glow buffer kept a stale edge that its
 * composite added back as a light line at the top and left.
 * Under a render scale k (RECOMP_GL_SCALE) a title pixel is k x k real
 * pixels, and the first real centre of title pixel i sits at i + 0.5/k, not
 * i + 0.5: an edge snapped to 0.5 still missed real row/column 0. u_surf.w =
 * 0.5 - 0.5/k moves positions so that real centre lands on i + 0.5 (0 at
 * k = 1). */
/* LIT, as the interpreter's (nv2a_vsh_interp.c): z = pow(max(y, 0), w)
 * whenever x > 0, so pow(0, 0) is 1 there. exp2(w * log2(0)) is undefined in
 * GLSL, hence the y == 0 case apart; it keeps 0 for any other power. */
#define NV2A_LIT_GLSL \
    "vec4 nv2a_lit(vec4 s) {\n" \
    "    float lx = max(s.x, 0.0), ly = max(s.y, 0.0), w = clamp(s.w, -127.996, 127.996);\n" \
    "    float z = ly > 0.0 ? exp2(w * log2(ly)) : (w == 0.0 ? 1.0 : 0.0);\n" \
    "    return vec4(1.0, lx, lx > 0.0 ? z : 0.0, 1.0);\n" \
    "}\n"
/* The old form (MANHUNT_GL_SHADER_MINOR=0): 0 whenever y <= 0. */
#define NV2A_LIT_OLD_GLSL \
    "vec4 nv2a_lit(vec4 s) {\n" \
    "    float lx = max(s.x, 0.0), ly = max(s.y, 0.0), w = clamp(s.w, -127.996, 127.996);\n" \
    "    return vec4(1.0, lx, (lx > 0.0 && ly > 0.0) ? exp2(w * log2(ly)) : 0.0, 1.0);\n" \
    "}\n"
#define NV2A_SNAP_GLSL \
    "vec2 nv2a_snap(vec2 sw, float w) {\n" \
    "    return w > 0.0 ? (trunc(sw / w * 16.0) / 16.0 - u_surf.w) * w : sw;\n" \
    "}\n"
/* Vulkan: the scale may differ per axis (RECOMP_GL_SCALE_X), so y has its
 * own offset, u_snap_y. */
#define NV2A_SNAP_VK_GLSL \
    "vec2 nv2a_snap(vec2 sw, float w) {\n" \
    "    vec2 off = vec2(u_surf.w, u_snap_y);\n" \
    "    return w > 0.0 ? (trunc(sw / w * 16.0) / 16.0 - off) * w : sw;\n" \
    "}\n"

/* Everything around the program body: inputs, the register file, the ILU
 * helpers (same constants as the interpreter), and the three position
 * paths. u_xform selects the path at run time so one program object serves
 * a vertex program under any surface. */
/* Vulkan GLSL (nv2a_vk): the same shader with explicit locations, the
 * uniforms in a std140 block (binding 0) and Vulkan's 0..1 clip depth. */
static const char s_vk_prelude[] =
    "#version 450\n"
    "layout(location = 0) in vec4 v0;\n"
    "layout(location = 1) in vec4 v1;\n"
    "layout(location = 2) in vec4 v2;\n"
    "layout(location = 3) in vec4 v3;\n"
    "layout(location = 4) in vec4 v4;\n"
    "layout(location = 5) in vec4 v5;\n"
    "layout(location = 6) in vec4 v6;\n"
    "layout(location = 7) in vec4 v7;\n"
    "layout(location = 8) in vec4 v8;\n"
    "layout(location = 9) in vec4 v9;\n"
    "layout(location = 10) in vec4 v10;\n"
    "layout(location = 11) in vec4 v11;\n"
    "layout(location = 12) in vec4 v12;\n"
    "layout(location = 13) in vec4 v13;\n"
    "layout(location = 14) in vec4 v14;\n"
    "layout(location = 15) in vec4 v15;\n"
    "layout(std140, set = 0, binding = 0) uniform VsU {\n"
    "    vec4 u_surf;\n"
    "    vec4 u_m[4];\n"
    "    vec4 u_vpoff;\n"
    "    vec2 u_aa;\n"
    "    int u_xform;\n"
    "    float u_snap_y;\n"
    "    vec2 u_wide;\n"
    "};\n"
    /* Transform constants, 192 per instance: an instanced draw (nv2a_vk.c)
     * repeats one mesh with each instance's own set. NV2A_VK_INSTANCES. */
    "layout(std140, set = 0, binding = 6) uniform VsC { vec4 c[192 * 16]; };\n"
    "int nv2a_ib;\n"
    "layout(location = 0) out vec4 vD0; layout(location = 1) out vec4 vD1;\n"
    "layout(location = 2) out vec4 vT0; layout(location = 3) out vec4 vT1;\n"
    "layout(location = 4) out vec4 vT2; layout(location = 5) out vec4 vT3;\n"
    "layout(location = 6) out float vFog;\n"
    "vec4 R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11;\n"
    "vec4 oPos, o1, o2, oD0, oD1, oFog, oPts, oB0, oB1, oT0, oT1, oT2, oT3;\n"
    "float nv2a_rcp(float x) { return x != 0.0 ? 1.0 / x : 1.884467e+19; }\n"
    "float nv2a_rcc(float x) {\n"
    "    float y = nv2a_rcp(x); float a = clamp(abs(y), 5.42101e-20, 1.884467e+19);\n"
    "    return y < 0.0 ? -a : a;\n"
    "}\n"
    "vec4 nv2a_exp(float x) { float f = floor(x); return vec4(exp2(f), x - f, exp2(x), 1.0); }\n"
    "vec4 nv2a_log(float x) {\n"
    "    float a = abs(x);\n"
    "    if (a == 0.0) return vec4(-1.884467e+19, 1.0, -1.884467e+19, 1.0);\n"
    "    float e = floor(log2(a));\n"
    "    return vec4(e, a / exp2(e), log2(a), 1.0);\n"
    "}\n"
    NV2A_LIT_GLSL
    NV2A_SNAP_VK_GLSL;

/* Vulkan clip depth. Without depthClamp: z clamped only in front of the eye
 * (w > 0). Behind it (w < 0) the vertex is clipped by x/y anyway and its true
 * z is about w: clamping it to 0 skewed the interpolated depth of triangles
 * crossing the eye plane (walls in hood view turned transparent, objects
 * showed through them). */
static const char s_vk_clip_clamp[] =
    "vec4 nv2a_clip(vec3 sw, float w) {\n"
    "    float z = sw.z * u_surf.z;\n"
    "    sw.xy = nv2a_snap(sw.xy, w);\n"
    "    return vec4(sw.x * u_surf.x - w, sw.y * u_surf.y - w,\n"
    "                w > 0.0 ? clamp(z, 0.0, w) : z, w);\n"
    "}\n";
/* With depthClamp the rasteriser clamps each fragment's depth, as the NV2A
 * does, and z stays linear in clip space. Any clamp per vertex bends the
 * depth plane of a triangle with a vertex near or behind the eye: Carbon's
 * front-end floor (a few huge triangles) came out deeper than the mirrored
 * car under it, and the translucent floor depth-failed over the
 * reflection, which then showed as a solid upside-down car. */
static const char s_vk_clip_free[] =
    "vec4 nv2a_clip(vec3 sw, float w) {\n"
    "    sw.xy = nv2a_snap(sw.xy, w);\n"
    "    return vec4(sw.x * u_surf.x - w, sw.y * u_surf.y - w, sw.z * u_surf.z, w);\n"
    "}\n";

/* GL clip depth, old form (MANHUNT_GL_CLIP_CLAMP=1): z clamped to the clip
 * volume per vertex. The NV2A clamps depth rather than clipping at the near
 * and far planes, and GL_DEPTH_CLAMP alone did not reach every driver -- on
 * Eden NFSU2's loading screen, drawn at z = 1.0, still vanished. */
static const char s_gl_clip_clamp[] =
    "vec4 nv2a_clip(vec3 sw, float w) {\n"
    "    float z = sw.z * u_surf.z * 2.0 - w;\n"
    "    sw.xy = nv2a_snap(sw.xy, w);\n"
    "    return vec4(sw.x * u_surf.x - w, sw.y * u_surf.y - w,\n"
    "                clamp(z, -abs(w), abs(w)), w);\n"
    "}\n";
/* GL clip depth, default: z left linear in clip space, as s_vk_clip_free.
 * nv2a_gl.c enables GL_DEPTH_CLAMP (core since GL 3.2; these shaders need
 * 3.3), so the rasteriser clamps each fragment's depth as the NV2A does.
 * The per-vertex clamp bent the depth plane of any triangle with a vertex
 * beside or behind the eye: near the camera a wall came out deeper than it
 * is, and what stood behind it drew over it. */
static const char s_gl_clip_free[] =
    "vec4 nv2a_clip(vec3 sw, float w) {\n"
    "    sw.xy = nv2a_snap(sw.xy, w);\n"
    "    return vec4(sw.x * u_surf.x - w, sw.y * u_surf.y - w,\n"
    "                sw.z * u_surf.z * 2.0 - w, w);\n"
    "}\n";

/* MANHUNT_GL_CLIP_CLAMP=1 restores the per-vertex clamp, for a driver that
 * ignores GL_DEPTH_CLAMP (Eden did; it cannot be detected from the guest). */
static int nv2a_gl_clip_clamp(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MANHUNT_GL_CLIP_CLAMP");
        on = e && *e == '1';
        fprintf(stderr, "[MH-GL] clip depth: %s (MANHUNT_GL_CLIP_CLAMP)\n",
                on ? "clamped per vertex" : "linear, GL_DEPTH_CLAMP");
    }
    return on;
}

/* MANHUNT_GL_SHADER_MINOR=0 restores two old shader details: LIT's
 * pow(0, 0) as 0 (here) and the alpha test on unrounded alpha (gl_psh.c). */
int nv2a_gl_shader_minor(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MANHUNT_GL_SHADER_MINOR");
        on = !(e && *e == '0');
        fprintf(stderr, "[MH-GL] LIT pow(0,0) = 1, alpha test on 8-bit alpha: %s (MANHUNT_GL_SHADER_MINOR)\n",
                on ? "on" : "off");
    }
    return on;
}

int nv2a_shader_vk;
int nv2a_shader_depth_clamp;

const char *nv2a_gl_vsh_prelude(void)
{
    if (nv2a_shader_vk) {
        static char *pre[2];
        int dc = nv2a_shader_depth_clamp != 0;
        if (!pre[dc]) {
            const char *clip = dc ? s_vk_clip_free : s_vk_clip_clamp;
            size_t a = sizeof s_vk_prelude - 1, c = strlen(clip);
            pre[dc] = (char *)malloc(a + c + 1);
            if (!pre[dc])
                return s_vk_prelude;
            memcpy(pre[dc], s_vk_prelude, a);
            memcpy(pre[dc] + a, clip, c + 1);
        }
        return pre[dc];
    }
    static const char gl[] =
        "#version 330 core\n"
        "layout(location = 0) in vec4 v0;\n"
        "layout(location = 1) in vec4 v1;\n"
        "layout(location = 2) in vec4 v2;\n"
        "layout(location = 3) in vec4 v3;\n"
        "layout(location = 4) in vec4 v4;\n"
        "layout(location = 5) in vec4 v5;\n"
        "layout(location = 6) in vec4 v6;\n"
        "layout(location = 7) in vec4 v7;\n"
        "layout(location = 8) in vec4 v8;\n"
        "layout(location = 9) in vec4 v9;\n"
        "layout(location = 10) in vec4 v10;\n"
        "layout(location = 11) in vec4 v11;\n"
        "layout(location = 12) in vec4 v12;\n"
        "layout(location = 13) in vec4 v13;\n"
        "layout(location = 14) in vec4 v14;\n"
        "layout(location = 15) in vec4 v15;\n"
        "#ifdef NV2A_GL_UBO\n"
        "layout(std140) uniform Nv2aConstants { vec4 c[192]; };\n"
        "#else\n"
        "uniform vec4 c[192];\n"
        "#endif\n"
        "uniform vec4 u_surf;      /* 2/W, 2/H, 1/zmax, scale offset */\n"
        "uniform vec2 u_aa;        /* logical -> real pixels */\n"
        "uniform vec4 u_m[4];      /* fixed-function composite rows */\n"
        "uniform vec4 u_vpoff;     /* fixed-function viewport offset */\n"
        "out vec4 vD0; out vec4 vD1; out vec4 vT0; out vec4 vT1;\n"
        "out vec4 vT2; out vec4 vT3; out float vFog;\n"
        "vec4 R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11;\n"
        "vec4 oPos, o1, o2, oD0, oD1, oFog, oPts, oB0, oB1, oT0, oT1, oT2, oT3;\n"
        "float nv2a_rcp(float x) { return x != 0.0 ? 1.0 / x : 1.884467e+19; }\n"
        "float nv2a_rcc(float x) {\n"
        "    float y = nv2a_rcp(x); float a = clamp(abs(y), 5.42101e-20, 1.884467e+19);\n"
        "    return y < 0.0 ? -a : a;\n"
        "}\n"
        "vec4 nv2a_exp(float x) { float f = floor(x); return vec4(exp2(f), x - f, exp2(x), 1.0); }\n"
        "vec4 nv2a_log(float x) {\n"
        "    float a = abs(x);\n"
        "    if (a == 0.0) return vec4(-1.884467e+19, 1.0, -1.884467e+19, 1.0);\n"
        "    float e = floor(log2(a));\n"
        "    return vec4(e, a / exp2(e), log2(a), 1.0);\n"
        "}\n";
    static const char snap[] =
        "/* screen*w (real pixels) and clip w to GL clip space. */\n"
        NV2A_SNAP_GLSL;
    static char *pre;
    if (!pre) {
        const char *lit = nv2a_gl_shader_minor() ? NV2A_LIT_GLSL : NV2A_LIT_OLD_GLSL;
        const char *clip = nv2a_gl_clip_clamp() ? s_gl_clip_clamp : s_gl_clip_free;
        size_t a = strlen(gl), l = strlen(lit), s = strlen(snap), c = strlen(clip);
        pre = (char *)malloc(a + l + s + c + 1);
        if (!pre)
            abort();
        memcpy(pre, gl, a);
        memcpy(pre + a, lit, l);
        memcpy(pre + a + l, snap, s);
        memcpy(pre + a + l + s, clip, c + 1);
    }
    return pre;
}

const char *nv2a_gl_vsh_prelude_mode(int ubo)
{
    const char *pre = nv2a_gl_vsh_prelude();
    static char block_pre[32768];
    if (!ubo || nv2a_shader_vk) return pre;
    if (!block_pre[0]) {
        const char *body = strchr(pre, '\n');
        if (!body || snprintf(block_pre, sizeof block_pre,
                "#version 330 core\n#define NV2A_GL_UBO 1\n%s", body + 1) >= (int)sizeof block_pre)
            abort();
    }
    return block_pre;
}

#define NV2A_VSH_MAIN(set_ib, decl) \
    decl \
    "void main() {\n" \
    "    R0 = R1 = R2 = R3 = R4 = R5 = R6 = R7 = R8 = R9 = R10 = R11 = vec4(0.0);\n" \
    "    oPos = o1 = o2 = oFog = oPts = vec4(0.0);\n" \
    "    oD0 = oD1 = oB0 = oB1 = vec4(0.0, 0.0, 0.0, 1.0);\n" \
    "    oT0 = oT1 = oT2 = oT3 = vec4(0.0, 0.0, 0.0, 1.0);\n" \
    set_ib \
    "    nv2a_program();\n" \
    "    float w = abs(oPos.w) < 1e-6 ? 1e-6 : oPos.w;\n" \
    "    vec3 s = vec3(oPos.xy * u_aa, oPos.z);\n" \
    "    if (u_wide.x > 0.0) s.x = u_wide.y + (s.x - u_wide.y) * u_wide.x;\n" \
    "    else if (u_wide.x < 0.0) s.y = u_wide.y + (s.y - u_wide.y) * -u_wide.x;\n" \
    "    gl_Position = nv2a_clip(s * w, w);\n" \
    "    vD0 = clamp(oD0, 0.0, 1.0); vD1 = clamp(oD1, 0.0, 1.0);\n" \
    "    vT0 = oT0; vT1 = oT1; vT2 = oT2; vT3 = oT3; vFog = oFog.x;\n" \
    "}\n"

const char *nv2a_gl_vsh_main_program(void)
{
    if (nv2a_shader_vk)
        return NV2A_VSH_MAIN("    nv2a_ib = gl_InstanceIndex * 192;\n", "");
    /* Vulkan has u_wide in the uniform block. */
    return NV2A_VSH_MAIN("", "uniform vec2 u_wide;      /* widescreen 2D: x scale (<0: y scale -x), centre (0: off) */\n");
}

/* Mirrors the runtime's lit_color for infinite and local lights. Spot
 * lights (kind 3) add xemu's cone (vsh-ff.c, LIGHT_SPOT) on GL: u_ffx[2 + i]
 * is SET_LIGHT_SPOT_DIRECTION, which Xbox D3D writes as -dir * s with
 * w = -s * cos(phi/2), s = 1 / (cos(theta/2) - cos(phi/2)). Inside the inner
 * cone the light is unchanged, outside the outer cone it is 0, and between
 * the attenuation is scaled by the linear ramp dot(spot, L) + w.
 * SET_LIGHT_SPOT_FALLOFF is not used, as in xemu (its FIXME): falloff 1.
 * u_ffx[0].x is 0 with MANHUNT_GL_FF_SPOT=0: spots lit as point lights, the
 * old form, which the runtime's lit_color still uses. A zero direction also
 * keeps the point-light form. */
#define NV2A_LIGHT_GLSL(decl, spot) \
    "#ifdef NV2A_GL_UBO\n" \
    "layout(std140) uniform Nv2aLighting { vec4 u_light[46]; };\n" \
    "#else\n" \
    "uniform vec4 u_light[46];\n" \
    "#endif\n" \
    decl \
    "vec4 nv2a_ffp_diffuse(vec4 pos, vec4 normal, vec4 vc) {\n" \
    "    if (u_light[0].x == 0.0) return vc;\n" \
    "    vec3 e, n;\n" \
    "    for (int k = 0; k < 3; ++k) {\n" \
    "        e[k] = dot(u_light[2+k].xyz, pos.xyz) + u_light[2+k].w;\n" \
    "        n[k] = dot(u_light[2+k].xyz, normal.xyz);\n" \
    "    }\n" \
    "    float len = length(n);\n" \
    "    if (len > 1e-8) n /= len;\n" \
    "    if (u_light[0].w != 0.0 && dot(n,e) > 0.0) n = -n;\n" \
    "    vec3 color = u_light[1].rgb;\n" \
    "    for (int i = 0; i < 8; ++i) {\n" \
    "        int b = 6 + i * 5;\n" \
    "        int kind = int(u_light[b].w);\n" \
    "        if (kind == 0) continue;\n" \
    "        vec3 l; float att = 1.0;\n" \
    "        if (kind == 1) l = u_light[b+2].xyz;\n" \
    "        else {\n" \
    "            l = u_light[b+3].xyz - e;\n" \
    "            float d = length(l);\n" \
    "            float range = u_light[b+1].w;\n" \
    "            if (d > range && range > 0.0) continue;\n" \
    "            if (d > 1e-8) l /= d;\n" \
    "            vec3 a = u_light[b+4].xyz;\n" \
    "            att = a.x + a.y*d + a.z*d*d;\n" \
    "            att = att > 1e-8 ? 1.0 / att : 1.0;\n" \
    spot \
    "        }\n" \
    "        float ndl = max(dot(n,l), 0.0);\n" \
    "        vec3 amb = u_light[b].rgb, dif = u_light[b+1].rgb;\n" \
    "        if (u_light[0].y != 0.0) amb *= vc.rgb;\n" \
    "        if (u_light[0].z != 0.0) dif *= vc.rgb;\n" \
    "        color += att * (amb + dif * ndl);\n" \
    "    }\n" \
    "    return clamp(vec4(color, u_light[0].z != 0.0 ? vc.a : u_light[1].a), 0.0, 1.0);\n" \
    "}\n" \
    "#define NV2A_DIFFUSE nv2a_ffp_diffuse(v0, v2, v3)\n"

#define NV2A_STR2(x) #x
#define NV2A_STR(x) NV2A_STR2(x)
/* GL: u_ffx is declared with the lighting, which nv2a_gl.c puts before the
 * fixed-function main that reads it as well. */
#define NV2A_FFX_DECL_GLSL \
    "#ifndef NV2A_FFX\n" \
    "#define NV2A_FFX 1\n" \
    "uniform vec4 u_ffx[" NV2A_STR(NV2A_FFX_ROWS) "]; /* spot + texgen, see nv2a_gl_vsh_ffx_uniforms */\n" \
    "#endif\n"
/* xemu's LIGHT_SPOT, with l the normalised vertex-to-light vector (VP). */
#define NV2A_SPOT_GLSL \
    "            if (kind == 3 && u_ffx[0].x != 0.0) {\n" \
    "                vec4 sd = u_ffx[2 + i];\n" \
    "                float sl = length(sd.xyz);\n" \
    "                if (sl > 0.0) {\n" \
    "                    float inv = 1.0 / sl;\n" \
    "                    float cphi = -inv * sd.w;\n" \
    "                    float ctheta = inv + cphi;\n" \
    "                    float sdvp = dot(sd.xyz, l);\n" \
    "                    float rho = inv * sdvp;\n" \
    "                    if (!(rho > ctheta)) {\n" \
    "                        if (rho <= cphi) att = 0.0;\n" \
    "                        else att *= sdvp + sd.w;\n" \
    "                    }\n" \
    "                }\n" \
    "            }\n"

const char *nv2a_gl_vsh_lighting(void)
{
    /* Vulkan (nv2a_vk) takes no plain uniforms: the old local-light form. */
    if (nv2a_shader_vk)
        return NV2A_LIGHT_GLSL("", "");
    return NV2A_LIGHT_GLSL(NV2A_FFX_DECL_GLSL, NV2A_SPOT_GLSL);
}

const char *nv2a_gl_vsh_fixed(void)
{
    /* Vulkan: u_xform is a member of the prelude's uniform block. */
    /* GL only (skipped under Vulkan): these uniforms and the generated fog
     * coordinate and texture coordinates. */
#define NV2A_FIXED_DECL \
        "uniform int u_xform;      /* 0 pre-transformed, 1 fixed-function */\n" \
        "uniform vec2 u_wide;      /* widescreen 2D: x scale (<0: y scale -x), centre (0: off) */\n" \
        "uniform vec4 u_fogm[5];   /* fog generation, see nv2a_gl_vsh_fog_uniforms */\n" \
        "#define NV2A_FF_FOG 1\n" \
        NV2A_FFX_DECL_GLSL \
        "#define NV2A_FF_TEX 1\n"
    static const char decl[] = NV2A_FIXED_DECL;
    static const char body[] =
        NV2A_FIXED_DECL
        "#ifndef NV2A_DIFFUSE\n#define NV2A_DIFFUSE v3\n#endif\n"
        /* The fog coordinate the fixed-function transform generates
         * (SET_FOG_GEN_MODE, as xemu's vsh-ff.c): from the eye-space
         * position (model-view matrix 0), the fog plane, specular alpha or
         * the fog-coordinate attribute. Mode 0: none, the old vFog = 0. */
        "#ifdef NV2A_FF_FOG\n"
        "float nv2a_ff_fog() {\n"
        "    int g = int(u_fogm[4].x + 0.5);\n"
        "    if (g == 0) return 0.0;\n"
        "    if (g == 4) return clamp(v4.a, 0.0, 1.0);\n"
        "    if (g == 5) return v5.x;\n"
        "    vec4 p = v0;\n"
        "    vec3 e = vec3(dot(u_fogm[0], p), dot(u_fogm[1], p), dot(u_fogm[2], p));\n"
        "    if (g == 1) return length(e);\n"
        "    float d = dot(u_fogm[3].xyz, e) + u_fogm[3].w;\n"
        "    return g == 3 ? abs(d) : d;\n"
        "}\n"
        "#else\n"
        "float nv2a_ff_fog() { return 0.0; }\n"
        "#endif\n"
        /* Texture-coordinate generation and the texture matrix per stage
         * (SET_TEXGEN_S/T/R/Q, SET_TEXTURE_MATRIX_ENABLE, as xemu's
         * vsh-ff.c), from the eye-space position ep and normal en. Mode
         * codes from nv2a_gl_vsh_ffx_uniforms: 0 the attribute, 1 eye
         * linear, 2 object linear, 3 sphere map, 4 normal map, 5 reflection
         * map. */
        "#ifdef NV2A_FF_TEX\n"
        "vec4 nv2a_ff_tex(int s, vec4 t, vec4 ep, vec3 en) {\n"
        "    int b = 14 + s * 9;\n"
        "    vec4 mode = u_ffx[b];\n"
        "    vec4 o = t;\n"
        "    if (mode != vec4(0.0)) {\n"
        "        vec3 u = dot(ep.xyz, ep.xyz) > 0.0 ? normalize(ep.xyz) : vec3(0.0, 0.0, -1.0);\n"
        "        vec3 rf = reflect(u, en);\n"
        "        float rm = length(rf + vec3(0.0, 0.0, 1.0));\n"
        "        float im = rm > 0.0 ? 1.0 / (2.0 * rm) : 0.0;\n"
        "        for (int j = 0; j < 4; ++j) {\n"
        "            int m = int(mode[j] + 0.5);\n"
        "            if (m == 1) o[j] = dot(u_ffx[b + 1 + j], ep);\n"
        "            else if (m == 2) o[j] = dot(u_ffx[b + 1 + j], v0);\n"
        "            else if (m == 3) o[j] = rf[j] * im + 0.5;\n"
        "            else if (m == 4) o[j] = en[j];\n"
        "            else if (m == 5) o[j] = rf[j];\n"
        "        }\n"
        "    }\n"
        "    if (u_ffx[1][s] != 0.0)\n"
        "        o = vec4(dot(u_ffx[b + 5], o), dot(u_ffx[b + 6], o),\n"
        "                 dot(u_ffx[b + 7], o), dot(u_ffx[b + 8], o));\n"
        "    return o;\n"
        "}\n"
        "void nv2a_ff_texgen(inout vec4 t0, inout vec4 t1, inout vec4 t2, inout vec4 t3) {\n"
        "    if (u_ffx[0].y == 0.0) return;\n"
        "    vec4 ep = vec4(dot(u_ffx[10], v0), dot(u_ffx[11], v0),\n"
        "                   dot(u_ffx[12], v0), dot(u_ffx[13], v0));\n"
        "    vec3 en = vec3(dot(u_ffx[10].xyz, v2.xyz), dot(u_ffx[11].xyz, v2.xyz),\n"
        "                   dot(u_ffx[12].xyz, v2.xyz));\n"
        "    float nl = length(en);\n"
        "    if (nl > 1e-8) en /= nl;\n"
        "    t0 = nv2a_ff_tex(0, t0, ep, en); t1 = nv2a_ff_tex(1, t1, ep, en);\n"
        "    t2 = nv2a_ff_tex(2, t2, ep, en); t3 = nv2a_ff_tex(3, t3, ep, en);\n"
        "}\n"
        "#else\n"
        "void nv2a_ff_texgen(inout vec4 t0, inout vec4 t1, inout vec4 t2, inout vec4 t3) { }\n"
        "#endif\n"
        "void main() {\n"
        "    float fog = 0.0;\n"
        "    vec4 t0 = v9, t1 = v10, t2 = v11, t3 = v12;\n"
        "    if (u_xform == 1) {\n"
        "        vec4 clip = vec4(dot(u_m[0], v0), dot(u_m[1], v0),\n"
        "                         dot(u_m[2], v0), dot(u_m[3], v0));\n"
        "        float w = abs(clip.w) < 1e-6 ? 1e-6 : clip.w;\n"
        "        vec3 sw = clip.xyz + u_vpoff.xyz * w;\n"
        "        sw.xy *= u_aa;\n"
        "        if (u_wide.x > 0.0) sw.x = u_wide.y * w + (sw.x - u_wide.y * w) * u_wide.x;\n"
        "        else if (u_wide.x < 0.0) sw.y = u_wide.y * w + (sw.y - u_wide.y * w) * -u_wide.x;\n"
        "        gl_Position = nv2a_clip(sw, w);\n"
        "        fog = nv2a_ff_fog();\n"
        "        nv2a_ff_texgen(t0, t1, t2, t3);\n"
        "    } else {\n"
        "        float w = (v0.w > 0.0) ? 1.0 / v0.w : 1.0;\n"
        "        vec3 p = v0.xyz;\n"
        "        if (u_wide.x > 0.0) p.x = u_wide.y + (p.x - u_wide.y) * u_wide.x;\n"
        "        else if (u_wide.x < 0.0) p.y = u_wide.y + (p.y - u_wide.y) * -u_wide.x;\n"
        "        gl_Position = nv2a_clip(p * w, w);\n"
        "    }\n"
        "    vD0 = clamp(NV2A_DIFFUSE, 0.0, 1.0); vD1 = clamp(v4, 0.0, 1.0);\n"
        "    vT0 = t0; vT1 = t1; vT2 = t2; vT3 = t3; vFog = fog;\n"
        "}\n";
    return nv2a_shader_vk ? body + sizeof decl - 1 : body;
}

/* CMP arrives as four exact, unnormalised bytes. Keeping vec4 inputs avoids
 * shader variants and integer/float VAO type changes between adjacent draws.
 * Append after the input declarations so only subsequent references decode. */
const char *nv2a_gl_vsh_attributes(void)
{
    return
        "uniform int u_cmp_mask;\n"
        "vec4 nv2a_attr(vec4 a, int slot) {\n"
        "    if ((u_cmp_mask & (1 << slot)) == 0) return a;\n"
        "    uvec4 b = uvec4(a);\n"
        "    uint v = b.x | (b.y << 8) | (b.z << 16) | (b.w << 24);\n"
        "    return vec4(float(int(v << 21) >> 21) / 1023.0,\n"
        "                float(int(v << 10) >> 21) / 1023.0,\n"
        "                float(int(v) >> 22) / 511.0, 1.0);\n"
        "}\n"
        "#define v0 nv2a_attr(v0, 0)\n"
        "#define v1 nv2a_attr(v1, 1)\n"
        "#define v2 nv2a_attr(v2, 2)\n"
        "#define v3 nv2a_attr(v3, 3)\n"
        "#define v4 nv2a_attr(v4, 4)\n"
        "#define v5 nv2a_attr(v5, 5)\n"
        "#define v6 nv2a_attr(v6, 6)\n"
        "#define v7 nv2a_attr(v7, 7)\n"
        "#define v8 nv2a_attr(v8, 8)\n"
        "#define v9 nv2a_attr(v9, 9)\n"
        "#define v10 nv2a_attr(v10, 10)\n"
        "#define v11 nv2a_attr(v11, 11)\n"
        "#define v12 nv2a_attr(v12, 12)\n"
        "#define v13 nv2a_attr(v13, 13)\n"
        "#define v14 nv2a_attr(v14, 14)\n"
        "#define v15 nv2a_attr(v15, 15)\n"
        ;
}

/* u_fogm for a fixed-function batch: rows 0-2 model-view matrix 0
 * (SET_MODEL_VIEW_MATRIX0, rows as lit_color reads them), row 3
 * SET_FOG_PLANE, row 4 x the generation mode -- 0 none, 1 radial, 2 planar,
 * 3 absolute planar, 4 specular alpha, 5 fog-coordinate attribute.
 * SET_FOG_GEN_MODE (0x02A0) values are xemu's NV097_SET_FOG_GEN_MODE_V_*.
 * All zero while fog is off (nothing to generate, and no uniform churn), with
 * MANHUNT_GL_FF_FOG=0 (the old vFog = 0) or under RECOMP_GL_FOG_RAW=1 (the
 * renderer's original raw read, which had no generated coordinate). */
void nv2a_gl_vsh_fog_uniforms(const uint32_t *r, float out[5][4])
{
    static int on = -1;
    uint32_t gen;

    memset(out, 0, 5 * 4 * sizeof(float));
    if (on < 0) {
        const char *e = getenv("MANHUNT_GL_FF_FOG");
        const char *raw = getenv("RECOMP_GL_FOG_RAW");
        on = !(e && *e == '0') && !(raw && *raw == '1');
        fprintf(stderr, "[MH-GL] fixed-function fog coordinate: %s (MANHUNT_GL_FF_FOG)\n",
                on ? "generated (SET_FOG_GEN_MODE)" : "none, 0");
    }
    if (!on || !r[0x02A4 / 4])                  /* SET_FOG_ENABLE */
        return;
    memcpy(out[0], r + 0x0480 / 4, 3 * 16);     /* SET_MODEL_VIEW_MATRIX0 */
    memcpy(out[3], r + 0x09D0 / 4, 16);         /* SET_FOG_PLANE */
    gen = r[0x02A0 / 4];                        /* SET_FOG_GEN_MODE */
    switch (gen) {
    case 0:  out[4][0] = 4.0f; break;           /* V_SPEC_ALPHA */
    case 1:  out[4][0] = 1.0f; break;           /* V_RADIAL */
    case 2:  out[4][0] = 2.0f; break;           /* V_PLANAR */
    case 3:  out[4][0] = 3.0f; break;           /* V_ABS_PLANAR */
    case 6:  out[4][0] = 5.0f; break;           /* V_FOG_X */
    default:                                    /* unknown: |eye z| */
        memset(out[3], 0, 16);
        out[3][2] = 1.0f;
        out[4][0] = 3.0f;
        break;
    }
}

/* NV097_SET_TEXGEN_S/T/R/Q value to nv2a_ff_tex's mode code, for component
 * c (0 S .. 3 Q). The values are GL's enums. Sphere map takes S and T only,
 * normal and reflection map S, T and R (xemu asserts the same); anything
 * else -- emboss (0x855F), which xemu does not generate either, or a mode on
 * a component it cannot feed -- passes the attribute through. */
static float texgen_code(uint32_t v, int c)
{
    switch (v) {
    case 0x2400: return 1.0f;                       /* EYE_LINEAR */
    case 0x2401: return 2.0f;                       /* OBJECT_LINEAR */
    case 0x2402: return c < 2 ? 3.0f : 0.0f;        /* SPHERE_MAP */
    case 0x8511: return c < 3 ? 4.0f : 0.0f;        /* NORMAL_MAP */
    case 0x8512: return c < 3 ? 5.0f : 0.0f;        /* REFLECTION_MAP */
    default:     return 0.0f;                       /* DISABLE, EMBOSS */
    }
}

/* u_ffx for a fixed-function batch:
 *   row 0      x spot cones on, y texgen/texture matrix used by any stage
 *   row 1      SET_TEXTURE_MATRIX_ENABLE per stage (x..w = stage 0..3)
 *   rows 2-9   SET_LIGHT_SPOT_DIRECTION of light 0-7, spot lights only
 *   rows 10-13 SET_MODEL_VIEW_MATRIX0, rows as lit_color reads them
 *   rows 14 + 9s .. 22 + 9s, stage s:
 *              mode codes S, T, R, Q (texgen_code)
 *              SET_TEXGEN_PLANE_S, _T, _R, _Q
 *              SET_TEXTURE_MATRIX rows 0-3 (out_j = dot(row j, t), the way
 *              the model-view rows are applied; xemu's oT * texMat)
 * Rows a draw does not use stay zero, so most draws upload nothing new.
 * Method offsets (NV097, xemu nv2a_regs.h): SET_TEXGEN_S 0x03C0 (T +4, R +8,
 * Q +0xC, stage stride 0x10), SET_TEXTURE_MATRIX_ENABLE 0x0420 (stride 4),
 * SET_TEXTURE_MATRIX 0x06C0 (stride 0x40), SET_TEXGEN_PLANE_S 0x0840 (T, R, Q
 * at +0x10 each, stage stride 0x40), SET_LIGHT_SPOT_DIRECTION light base
 * 0x1000 + 0x80 i, +0x4C (4 floats). */
void nv2a_gl_vsh_ffx_uniforms(const uint32_t *r, uint32_t lighting,
                              float out[NV2A_FFX_ROWS][4])
{
    static int spot_on = -1, tex_on = -1;
    int s, c, i, any = 0;

    memset(out, 0, NV2A_FFX_ROWS * 4 * sizeof(float));
    if (spot_on < 0) {
        const char *e = getenv("MANHUNT_GL_FF_SPOT");
        spot_on = !(e && *e == '0');
        e = getenv("MANHUNT_GL_FF_TEXGEN");
        tex_on = !(e && *e == '0');
        fprintf(stderr, "[MH-GL] fixed-function spot lights: %s (MANHUNT_GL_FF_SPOT)\n",
                spot_on ? "cone (SET_LIGHT_SPOT_DIRECTION)" : "lit as point lights");
        fprintf(stderr, "[MH-GL] fixed-function texgen and texture matrices: %s (MANHUNT_GL_FF_TEXGEN)\n",
                tex_on ? "on" : "off, coordinates passed through");
    }
    if (spot_on && (lighting & 1u)) {
        const uint32_t mask = r[0x03BC / 4];        /* SET_LIGHT_ENABLE_MASK */
        for (i = 0; i < 8; i++)
            if (((mask >> (2 * i)) & 3u) == 3u) {   /* V_SPOT */
                memcpy(out[2 + i], r + (0x1000 + i * 0x80 + 0x4C) / 4, 16);
                out[0][0] = 1.0f;
            }
    }
    if (!tex_on)
        return;
    for (s = 0; s < 4; s++) {
        float *mode = out[14 + 9 * s];
        int used = 0;
        for (c = 0; c < 4; c++) {
            mode[c] = texgen_code(r[(0x03C0 + s * 0x10 + c * 4) / 4], c);
            used |= mode[c] != 0.0f;
        }
        if (used)
            memcpy(out[15 + 9 * s], r + (0x0840 + s * 0x40) / 4, 4 * 16);
        if (r[(0x0420 + s * 4) / 4]) {
            out[1][s] = 1.0f;
            memcpy(out[19 + 9 * s], r + (0x06C0 + s * 0x40) / 4, 4 * 16);
            used = 1;
        }
        any |= used;
    }
    if (any) {
        out[0][1] = 1.0f;
        memcpy(out[10], r + 0x0480 / 4, 4 * 16);
    }
}
