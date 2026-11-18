/*
 * gl_psh.c -- NV2A texture shader + register combiners as a GLSL fragment
 * shader.
 *
 * The register values are the XDK's pixel-shader tokens, which D3D writes to
 * the combiner registers unchanged, so the encoding is the XDK's:
 *
 *   input byte (ICW, final CW0/CW1): reg[3:0] | ALPHA 0x10 | mapping[7:5]
 *   colour/alpha OCW:  CD dst[3:0] AB dst[7:4] SUM dst[11:8]
 *                      CD_DOT 12, AB_DOT 13, MUX 14, scale/bias [17:15],
 *                      CD_BLUE_TO_ALPHA 18, AB_BLUE_TO_ALPHA 19
 *   CONTROL:           stage count [7:0], MUX_MSB 8, UNIQUE_C0 12, UNIQUE_C1 16
 *   final CW1:         E, F, G bytes; CLAMP_SUM 0x80, COMPLEMENT_V1 0x40,
 *                      COMPLEMENT_R0 0x20 in the low byte
 *   SHADER_STAGE_PROGRAM: 5 bits per texture stage (PS_TEXTUREMODES)
 *
 * Registers: 0 zero, 1 C0, 2 C1, 3 fog, 4 V0 (diffuse), 5 V1 (specular),
 * 8-11 T0-T3, 12 R0, 13 R1, and for the final combiner only 14 V1R0_SUM and
 * 15 EF_PROD. R0.a starts as T0.a.
 *
 * Texture modes (PS_TEXTUREMODES, xemu's numbering): 0 NONE, 1 PROJECT2D,
 * 2 PROJECT3D, 3 CUBEMAP, 4 PASSTHRU, 5 CLIPPLANE, 6 BUMPENVMAP,
 * 7 BUMPENVMAP_LUM, 8 BRDF, 9 DOT_ST, 10 DOT_ZW, 11 DOT_RFLCT_DIFF,
 * 12 DOT_RFLCT_SPEC, 13 DOT_STR_3D, 14 DOT_STR_CUBE, 15 DPNDNT_AR,
 * 16 DPNDNT_GB, 17 DOTPRODUCT, 18 DOT_RFLCT_SPEC_CONST. On GL, modes 2, 3,
 * 6, 7, 9-14, 17 and 18 follow xemu's psh.c (texshader_fetch below);
 * MANHUNT_GL_TEXSHADER=0 restores the old GL behaviour (cube maps black, the
 * rest a plain 2D sample). Not done: BRDF (xemu has none either), DOT_ZW's
 * depth replacement, volume textures (DOT_STR_3D samples the 2D image), and
 * the HILO hemisphere dot mappings 5-7 (plain rgb, as xemu).
 */
#include "gl_psh.h"

#include <stdlib.h>

#include <stdarg.h>
#include <stdio.h>
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

static const char *reg_name(uint32_t reg, int stage, const Nv2aPshKey *k,
                            char *buf, size_t n)
{
    switch (reg) {
    case 1:
        snprintf(buf, n, "u_c0[%d]", (k->control & (1u << 12)) ? stage : 0);
        return buf;
    case 2:
        snprintf(buf, n, "u_c1[%d]", (k->control & (1u << 16)) ? stage : 0);
        return buf;
    case 3:  return "FOG";
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    case 14: return "V1R0";
    case 15: return "EF";
    default: return "ZERO";
    }
}

/* One general-combiner input, mapped. `alpha_portion` selects the
 * float-valued (alpha) path, where the RGB channel bit means blue. */
static void input(Out *o, uint32_t b, int stage, int alpha_portion,
                  const Nv2aPshKey *k)
{
    char nb[16];
    /* V1R0_SUM and EF_PROD exist only in the final combiner. */
    const char *r = (b & 0xF) >= 14 ? "ZERO"
                                    : reg_name(b & 0xF, stage, k, nb, sizeof nb);
    const char *x;
    char val[40];
    int use_alpha = (b >> 4) & 1;
    uint32_t map = (b >> 5) & 7;

    if (alpha_portion)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    x = val;

    switch (map) {
    case 0: emit(o, "max(%s, 0.0)", x); break;                       /* unsigned */
    case 1: emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", x); break;        /* unsigned invert */
    case 2: emit(o, "(2.0 * max(%s, 0.0) - 1.0)", x); break;         /* expand normal */
    case 3: emit(o, "(1.0 - 2.0 * max(%s, 0.0))", x); break;         /* expand negate */
    case 4: emit(o, "(max(%s, 0.0) - 0.5)", x); break;               /* half-bias normal */
    case 5: emit(o, "(0.5 - max(%s, 0.0))", x); break;               /* half-bias negate */
    case 6: emit(o, "(%s)", x); break;                               /* signed identity */
    default: emit(o, "(-(%s))", x); break;                           /* signed negate */
    }
}

static const char *dst_name(uint32_t reg)
{
    switch (reg) {
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    default: return NULL;           /* 0 discards; others are not writable */
    }
}

static const char *scale_bias(uint32_t op)
{
    switch (op) {
    case 1:  return "(%s - 0.5)";
    case 2:  return "(%s * 2.0)";
    case 3:  return "((%s - 0.5) * 2.0)";
    case 4:  return "(%s * 4.0)";
    case 6:  return "(%s * 0.5)";
    default: return "(%s)";
    }
}

static void stage(Out *o, int i, const Nv2aPshKey *k)
{
    uint32_t cicw = k->color_icw[i], cocw = k->color_ocw[i];
    uint32_t aicw = k->alpha_icw[i], aocw = k->alpha_ocw[i];
    const char *d;
    char e[64];

    emit(o, "    { /* stage %d */\n", i);
    /* Colour inputs */
    emit(o, "        vec3 cA = "); input(o, cicw >> 24, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cB = "); input(o, cicw >> 16, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cC = "); input(o, cicw >> 8, i, 0, k);  emit(o, ";\n");
    emit(o, "        vec3 cD = "); input(o, cicw, i, 0, k);       emit(o, ";\n");
    /* Alpha inputs */
    emit(o, "        float aA = "); input(o, aicw >> 24, i, 1, k); emit(o, ";\n");
    emit(o, "        float aB = "); input(o, aicw >> 16, i, 1, k); emit(o, ";\n");
    emit(o, "        float aC = "); input(o, aicw >> 8, i, 1, k);  emit(o, ";\n");
    emit(o, "        float aD = "); input(o, aicw, i, 1, k);       emit(o, ";\n");

    emit(o, "        vec3 cAB = %s;\n", (cocw & (1u << 13)) ? "vec3(dot(cA, cB))" : "cA * cB");
    emit(o, "        vec3 cCD = %s;\n", (cocw & (1u << 12)) ? "vec3(dot(cC, cD))" : "cC * cD");
    if (cocw & (1u << 14))
        emit(o, "        vec3 cS = %s ? cCD : cAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        vec3 cS = cAB + cCD;\n");
    emit(o, "        float aAB = aA * aB, aCD = aC * aD;\n");
    if (aocw & (1u << 14))
        emit(o, "        float aS = %s ? aCD : aAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        float aS = aAB + aCD;\n");

    /* Scale/bias, clamp to the signed range, write. */
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cAB");
    emit(o, "        cAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cCD");
    emit(o, "        cCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cS");
    emit(o, "        cS = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aAB");
    emit(o, "        aAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aCD");
    emit(o, "        aCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aS");
    emit(o, "        aS = clamp(%s, -1.0, 1.0);\n", e);

    if ((d = dst_name((cocw >> 4) & 0xF)) != NULL) emit(o, "        %s.rgb = cAB;\n", d);
    if ((d = dst_name(cocw & 0xF)) != NULL)        emit(o, "        %s.rgb = cCD;\n", d);
    if ((d = dst_name((cocw >> 8) & 0xF)) != NULL) emit(o, "        %s.rgb = cS;\n", d);
    if ((d = dst_name((aocw >> 4) & 0xF)) != NULL) emit(o, "        %s.a = aAB;\n", d);
    if ((d = dst_name(aocw & 0xF)) != NULL)        emit(o, "        %s.a = aCD;\n", d);
    if ((d = dst_name((aocw >> 8) & 0xF)) != NULL) emit(o, "        %s.a = aS;\n", d);
    /* Blue-to-alpha replaces the alpha written to the AB / CD destination. */
    if ((cocw & (1u << 19)) && (d = dst_name((cocw >> 4) & 0xF)) != NULL)
        emit(o, "        %s.a = cAB.b;\n", d);
    if ((cocw & (1u << 18)) && (d = dst_name(cocw & 0xF)) != NULL)
        emit(o, "        %s.a = cCD.b;\n", d);
    emit(o, "    }\n");
}

/* A final-combiner input: clamped to [0,1], optionally inverted. Its C0 and
 * C1 are its own (SPECULAR_FOG_FACTOR0/1), not a general stage's: read as
 * stage 0's, NFSU2's colour grading added its red channel mask to the whole
 * picture. */
static void final_input(Out *o, uint32_t b, int alpha_only, const Nv2aPshKey *k)
{
    char nb[16];
    const char *r = (b & 0xF) == 1 ? "u_fc0"
                  : (b & 0xF) == 2 ? "u_fc1"
                  : reg_name(b & 0xF, 0, k, nb, sizeof nb);
    int use_alpha = (b >> 4) & 1;
    int invert = (b >> 5) & 1;
    char val[40];

    if (alpha_only)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    if (invert)
        emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", val);
    else
        emit(o, "clamp(%s, 0.0, 1.0)", val);
}

/* The earlier stage a dependent mode reads (SHADER_OTHER_STAGE_INPUT):
 * stage 1 always reads T0; stage 2 selects with bits [19:16], stage 3 with
 * [23:20]. */
static int input_stage(const Nv2aPshKey *k, int t)
{
    int s = t <= 1 ? 0 : (int)((k->other_input >> (t == 2 ? 16 : 20)) & 0xF);
    return s < t ? s : t - 1;
}

/* MANHUNT_GL_TEXSHADER: the cube, bump-environment and dot-product texture
 * modes on GL. -1 until read; tests may set it. */
int nv2a_gl_texshader_on = -1;

int nv2a_gl_texshader(void)
{
    if (nv2a_gl_texshader_on < 0) {
        const char *e = getenv("MANHUNT_GL_TEXSHADER");
        nv2a_gl_texshader_on = !(e && e[0] == '0' && !e[1]);
        fprintf(stderr, "[MH-GL] cube/bump/dot-product texture modes: %s (MANHUNT_GL_TEXSHADER)\n",
                nv2a_gl_texshader_on ? "on" : "off");
    }
    return nv2a_gl_texshader_on;
}

int nv2a_gl_texshader_cube_mode(uint32_t mode)
{
    return mode == 3 || mode == 11 || mode == 12 || mode == 14 || mode == 18;
}

static uint32_t stage_mode(const Nv2aPshKey *k, int t)
{
    return (k->shader_program >> (5 * t)) & 0x1F;
}

static int mode_dot(uint32_t m)  { return (m >= 9 && m <= 14) || m == 17 || m == 18; }
static int mode_bump(uint32_t m) { return m == 6 || m == 7 || m == 18; }

/* Whether this GL program uses texshader_fetch at all: when it does not, the
 * shader source stays byte for byte what it was. */
static int texshader_active(const Nv2aPshKey *k)
{
    int t;
    if (nv2a_shader_vk || !nv2a_gl_texshader())
        return 0;
    for (t = 0; t < 4; t++) {
        uint32_t m = stage_mode(k, t);
        if (m == 2 || m == 3 || mode_bump(m) || mode_dot(m))
            return 1;
    }
    return 0;
}

static int texshader_any(const Nv2aPshKey *k, int (*pred)(uint32_t))
{
    int t;
    for (t = 0; t < 4; t++)
        if (pred(stage_mode(k, t)))
            return 1;
    return 0;
}

/* xemu's signed conversions and dot mappings. The mapping per stage 1-3 is
 * SET_DOT_RGBMAPPING, which the renderer folds into other_input [11:0]. */
#define NV2A_TEXSHADER_GLSL \
    "float nv2a_s1(float x) { x *= 255.0; return (x - 128.0) / 127.0; }\n" \
    "float nv2a_s2(float x) { x *= 255.0; return x >= 128.0 ? (x - 255.5) / 127.5 : (x + 0.5) / 127.5; }\n" \
    "float nv2a_s3(float x) { x *= 255.0; return x >= 128.0 ? (x - 256.0) / 127.0 : x / 127.0; }\n" \
    "vec3 nv2a_dotmap(vec4 c, int m) {\n" \
    "    if (m == 1) return vec3(nv2a_s1(c.r), nv2a_s1(c.g), nv2a_s1(c.b));\n" \
    "    if (m == 2) return vec3(nv2a_s2(c.r), nv2a_s2(c.g), nv2a_s2(c.b));\n" \
    "    if (m == 3) return vec3(nv2a_s3(c.r), nv2a_s3(c.g), nv2a_s3(c.b));\n" \
    "    if (m == 4) {\n" \
    "        uint hi = (uint(c.a * 255.0) << 8) | uint(c.r * 255.0);\n" \
    "        uint lo = (uint(c.g * 255.0) << 8) | uint(c.b * 255.0);\n" \
    "        return vec3(float(hi) / 65535.0, float(lo) / 65535.0, 1.0);\n" \
    "    }\n" \
    "    return c.rgb;\n" \
    "}\n"

static int dot_map(const Nv2aPshKey *k, int t)
{
    return t < 1 ? 0 : (int)((k->other_input >> (4 * (t - 1))) & 0xF);
}

static int input_stage(const Nv2aPshKey *k, int t);

/* ndT = dot(vTT.xyz, map(T<input stage>)) */
static void emit_dot(Out *o, int t, const Nv2aPshKey *k)
{
    emit(o, "    nd%d = dot(vT%d.xyz, nv2a_dotmap(T%d, %d));\n", t, t, input_stage(k, t), dot_map(k, t));
}

/* One stage of the modes xemu's psh.c has beyond the basic ones. Returns 0
 * for a mode it leaves to tex_fetch. Cube lookups take the direction as it
 * is; 2D ones the stage's texcoord scale (texels for linear images). A mode
 * at a stage position the hardware cannot run reads zero. */
static int texshader_fetch(Out *o, int t, uint32_t mode, const Nv2aPshKey *k)
{
    uint32_t prev = t > 0 ? stage_mode(k, t - 1) : 0;
    int dots3 = t == 3 && stage_mode(k, 1) == 17 &&
                (stage_mode(k, 2) == 17 || stage_mode(k, 2) == 11);

    switch (mode) {
    case 2:                                          /* PROJECT3D on a 2D image */
        emit(o, "    vec4 T%d = texture(t%d, (abs(vT%d.w) > 1e-8 ? vT%d.xy / vT%d.w : vT%d.xy)"
                " * u_tscale[%d]);\n", t, t, t, t, t, t, t);
        return 1;
    case 3:                                          /* CUBEMAP: (s, t, r), no divide */
        emit(o, "    vec4 T%d = texture(t%d, vT%d.xyz);\n", t, t, t);
        return 1;
    case 6:                                          /* BUMPENVMAP */
    case 7:                                          /* BUMPENVMAP_LUM */
        if (t == 0)
            break;
        {
            int s = input_stage(k, t);
            emit(o, "    vec2 ds%d = mat2(u_bump[%d].xy, u_bump[%d].zw)"
                    " * vec2(nv2a_s3(T%d.b), nv2a_s3(T%d.g));\n", t, 2 * t, 2 * t, s, s);
            emit(o, "    vec4 T%d = texture(t%d, (vT%d.xy + ds%d) * u_tscale[%d]);\n", t, t, t, t, t);
            if (mode == 7)
                emit(o, "    T%d *= u_bump[%d].x * T%d.r + u_bump[%d].y;\n", t, 2 * t + 1, s, 2 * t + 1);
        }
        return 1;
    case 17:                                         /* DOTPRODUCT */
        if (t != 1 && t != 2)
            break;
        emit_dot(o, t, k);
        emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        return 1;
    case 9:                                          /* DOT_ST */
        if (t < 2 || prev != 17)
            break;
        emit_dot(o, t, k);
        emit(o, "    vec4 T%d = texture(t%d, vec2(nd%d, nd%d) * u_tscale[%d]);\n", t, t, t - 1, t, t);
        return 1;
    case 10:                                         /* DOT_ZW: depth not replaced */
        if (t < 2 || prev != 17)
            break;
        emit_dot(o, t, k);
        emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        return 1;
    case 11:                                         /* DOT_RFLCT_DIFF */
        if (t != 2 || prev != 17)
            break;
        emit_dot(o, t, k);
        if (input_stage(k, 3) < 2)
            emit(o, "    float nd2n = dot(vT3.xyz, nv2a_dotmap(T%d, %d));\n",
                 input_stage(k, 3), dot_map(k, 3));
        else
            emit(o, "    float nd2n = 0.0;\n");
        emit(o, "    vec4 T%d = texture(t%d, vec3(nd1, nd2, nd2n));\n", t, t);
        return 1;
    case 12:                                         /* DOT_RFLCT_SPEC */
    case 18:                                         /* DOT_RFLCT_SPEC_CONST */
        if (!dots3)
            break;
        emit_dot(o, t, k);
        emit(o, "    {\n"
                "        vec3 n = vec3(nd1, nd2, nd3), e = %s;\n"
                "        float nn = dot(n, n);\n"
                "        rv3 = nn != 0.0 ? 2.0 * n * dot(n, e) / nn - e : -e;\n"
                "    }\n", mode == 12 ? "vec3(vT1.w, vT2.w, vT3.w)" : "u_eye.xyz");
        emit(o, "    vec4 T%d = texture(t%d, rv3);\n", t, t);
        return 1;
    case 13:                                         /* DOT_STR_3D, on the 2D image */
        if (!dots3)
            break;
        emit_dot(o, t, k);
        emit(o, "    vec4 T%d = texture(t%d, vec2(nd1, nd2) * u_tscale[%d]);\n", t, t, t);
        return 1;
    case 14:                                         /* DOT_STR_CUBE */
        if (!dots3)
            break;
        emit_dot(o, t, k);
        emit(o, "    vec4 T%d = texture(t%d, vec3(nd1, nd2, nd3));\n", t, t);
        return 1;
    case 8:                                          /* BRDF: not done (nor in xemu) */
        break;
    default:
        return 0;
    }
    emit(o, "    vec4 T%d = vec4(0.0);\n", t);
    return 1;
}

/* Each combination of these modes once, so a console log shows what a title
 * uses without RECOMP_GL_TRACE. */
static void texshader_note(const Nv2aPshKey *k)
{
    static uint32_t seen[32];
    static unsigned nseen;
    unsigned i;
    for (i = 0; i < nseen && seen[i] != k->shader_program; i++) { }
    if (i < nseen || nseen >= 32)
        return;
    seen[nseen++] = k->shader_program;
    fprintf(stderr, "[MH-GL] texture modes 0x%05X (stages %u %u %u %u) other 0x%08X\n",
            (unsigned)k->shader_program, (unsigned)stage_mode(k, 0), (unsigned)stage_mode(k, 1),
            (unsigned)stage_mode(k, 2), (unsigned)stage_mode(k, 3), (unsigned)k->other_input);
}

static void tex_fetch(Out *o, int t, uint32_t mode, const Nv2aPshKey *k)
{
    if (texshader_active(k) && texshader_fetch(o, t, mode, k))
        return;
    switch (mode) {
    case 15:                                         /* DEPENDENT_AR */
    case 16:                                         /* DEPENDENT_GB */
        /* A lookup indexed by an earlier stage's colour -- NFSU2's colour
         * grading does red through one and green/blue through the other.
         * The coordinates are already normalised. Sampled at the stage's own
         * texcoords, the green/blue lookup read black and races were red. */
        if (t == 0) {
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
            break;
        }
        emit(o, "    vec4 T%d = texture(t%d, T%d.%s);\n", t, t, input_stage(k, t),
             mode == 15 ? "ar" : "gb");
        break;
    case 0:                                          /* NONE */
        emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    case 4:                                          /* PASS_THROUGH */
        emit(o, "    vec4 T%d = clamp(vT%d, 0.0, 1.0);\n", t, t);
        break;
    case 5:                                          /* CLIP_PLANE */
        emit(o, "    if (any(lessThan(vT%d, vec4(0.0)))) discard;\n"
                "    vec4 T%d = vec4(0.0);\n", t, t);
        break;
    case 3:                                          /* CUBE_MAP */
        /* Car reflections. The direction is (s, t, r), not divided by q
         * (xemu does the same). Vulkan only: nv2a_vk binds cube views,
         * nv2a_gl does not, so GL still reads black. */
        if (nv2a_shader_vk)
            emit(o, "    vec4 T%d = texture(t%d, vT%d.xyz);\n", t, t, t);
        else
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    default: {                                       /* 2D (+ the rest, for now) */
        emit(o, "    vec2 tc%d = (abs(vT%d.w) > 1e-8 && %d == 1) ? vT%d.xy / vT%d.w : vT%d.xy;\n",
             t, t, mode == 1 ? 1 : 0, t, t, t);
        emit(o, "    vec4 T%d = texture(t%d, tc%d * u_tscale[%d]);\n", t, t, t, t);
        break;
    }
    }
}

/* The fog unit (xemu's vsh.c fog modes), here per pixel on the interpolated
 * fog coordinate. u_fogp: see nv2a_fog_params. */
#define NV2A_FOG_GLSL \
    "float nv2a_fog(float d) {\n" \
    "    if (u_fogp.w < 0.5) return clamp(d, 0.0, 1.0);\n" \
    "    if (u_fogp.w < 1.5) return 1.0;\n" \
    "    int m = int(u_fogp.z + 0.5);\n" \
    "    float f;\n" \
    "    if (isinf(d)) d = 0.0;\n" \
    "    if (m == 0 || m == 3) f = u_fogp.x + d * u_fogp.y - 1.0;\n" \
    "    else if (m == 1 || m == 4) f = u_fogp.x + exp2(d * u_fogp.y * 16.0) - 1.5;\n" \
    "    else f = u_fogp.x + exp2(-d * d * u_fogp.y * u_fogp.y * 32.0) - 1.5;\n" \
    "    if (m >= 3) f = abs(f);\n" \
    "    return clamp(f, 0.0, 1.0);\n" \
    "}\n"

void nv2a_fog_params(const uint32_t *regs, uint32_t xform, float out[4])
{
    union { uint32_t u; float f; } p0, p1;

    p0.u = regs[0x09C0 / 4];                    /* SET_FOG_PARAMS */
    p1.u = regs[0x09C4 / 4];
    out[0] = p0.f;
    out[1] = p1.f;
    switch (regs[0x029C / 4]) {                 /* SET_FOG_MODE */
    case 0x0800: out[2] = 1.0f; break;          /* EXP */
    case 0x0801: out[2] = 2.0f; break;          /* EXP2 */
    case 0x0802: out[2] = 4.0f; break;          /* EXP_ABS */
    case 0x0803: out[2] = 5.0f; break;          /* EXP2_ABS */
    case 0x0804: out[2] = 3.0f; break;          /* LINEAR_ABS */
    default:     out[2] = 0.0f; break;          /* LINEAR (0x2601) */
    }
    /* A vertex program's oFog.x is the fog distance. The fixed-function
     * path generates one per SET_FOG_GEN_MODE (gl_vsh.c, u_fogm); with
     * MANHUNT_GL_FF_FOG=0 it is 0, as this runtime's CPU path still has it.
     * Factor 1 when fog is off; RECOMP_GL_FOG_RAW=1 keeps the renderer's
     * original raw read (and no generated coordinate). */
    static int fog_raw = -1;
    if (fog_raw < 0) {
        const char *e = getenv("RECOMP_GL_FOG_RAW");
        fog_raw = e && *e == '1';
        fprintf(stderr, "[MH-GL] fixed-function fog: %s\n", fog_raw ? "raw read (RECOMP_GL_FOG_RAW=1)" : "as the CPU's fog_factor");
    }
    if (xform != 2 && fog_raw)
        out[3] = 0.0f;
    else
        out[3] = regs[0x02A4 / 4] ? 2.0f : 1.0f; /* SET_FOG_ENABLE */
}

int nv2a_gl_gpu_mode(void);            /* nv2a_gl.c */
int nv2a_gl_shader_minor(void);        /* gl_vsh.c */

/* Whether texshader_fetch runs mode m at stage i (the positions xemu
 * accepts; the dot-product chains need their DOTPRODUCT stages). */
static int texshader_supported(const Nv2aPshKey *k, unsigned i, unsigned m)
{
    uint32_t m1 = stage_mode(k, 1), m2 = stage_mode(k, 2);
    uint32_t prev = i ? stage_mode(k, (int)i - 1) : 0;
    if (nv2a_shader_vk || !nv2a_gl_texshader())
        return 0;
    switch (m) {
    case 2: case 3:  return 1;
    case 6: case 7:  return i >= 1;
    case 17:         return i == 1 || i == 2;
    case 9: case 10: return i >= 2 && prev == 17;
    case 11:         return i == 2 && prev == 17;
    case 12: case 13: case 14: case 18:
        return i == 3 && m1 == 17 && (m2 == 17 || m2 == 11);
    default:         return 0;
    }
}

int nv2a_gl_psh_supported(const Nv2aPshKey *k)
{
    for (unsigned i = 0; i < 4; i++) {
        unsigned m = (k->shader_program >> (5 * i)) & 31u;
        if (m != 0 && m != 1 && m != 4 && m != 5 && m != 15 && m != 16 &&
            !texshader_supported(k, i, m))
            return 0;
        if ((m == 15 || m == 16) && (i == 0 ||
            (i > 1 && ((k->other_input >> (i == 2 ? 16 : 20)) & 15u) >= i)))
            return 0;
    }
    return 1;
}

int nv2a_gl_psh(const Nv2aPshKey *k, char *buf, size_t cap)
{
    if (!nv2a_shader_vk && !nv2a_gl_gpu_mode() && !nv2a_gl_psh_supported(k)) return -1;
    Out o = { buf, cap, 0 };
    uint32_t count = k->control & 0xFF;
    uint32_t i;

    if (count > 8)
        count = 8;
    if (nv2a_shader_vk) {
        /* Vulkan (nv2a_vk): std140 block at binding 1 -- vec2 array
         * elements take 16 bytes -- and samplers at bindings 2..5,
         * samplerCube for a CUBE_MAP stage. */
        emit(&o,
            "#version 450\n"
            "layout(location = 0) in vec4 vD0; layout(location = 1) in vec4 vD1;\n"
            "layout(location = 2) in vec4 vT0; layout(location = 3) in vec4 vT1;\n"
            "layout(location = 4) in vec4 vT2; layout(location = 5) in vec4 vT3;\n"
            "layout(location = 6) in float vFog;\n");
        for (i = 0; i < 4; i++)
            emit(&o, "layout(set = 0, binding = %u) uniform %s t%u;\n", i + 2,
                 ((k->shader_program >> (5 * i)) & 0x1F) == 3 ? "samplerCube" : "sampler2D",
                 i);
        emit(&o,
            "layout(std140, set = 0, binding = 1) uniform FsU {\n"
            "    vec2 u_tscale[4];\n"
            "    vec4 u_c0[8]; vec4 u_c1[8];\n"
            "    vec4 u_fc0; vec4 u_fc1;\n"
            "    vec4 u_fogcolor;\n"
            "    int u_alpha_func; float u_alpha_ref;\n"
            "    vec4 u_fogp;\n"
            "};\n"
            "layout(location = 0) out vec4 fragColor;\n"
            NV2A_FOG_GLSL
            "void main() {\n");
    } else if (texshader_active(k)) {
        /* Cube-sampling stages take a samplerCube; the bump and constant-eye
         * modes their registers (u_bump[2i]: stage i's matrix 00 01 10 11,
         * [2i + 1]: luminance scale and offset). */
        emit(&o,
            "#version 330 core\n"
            "in vec4 vD0; in vec4 vD1; in vec4 vT0; in vec4 vT1;\n"
            "in vec4 vT2; in vec4 vT3; in float vFog;\n");
        for (i = 0; i < 4; i++)
            emit(&o, "uniform %s t%u;\n",
                 nv2a_gl_texshader_cube_mode(stage_mode(k, (int)i)) ? "samplerCube" : "sampler2D", i);
        if (texshader_any(k, mode_bump))
            emit(&o, "uniform vec4 u_bump[8]; uniform vec4 u_eye;\n");
        if (texshader_any(k, mode_bump) || texshader_any(k, mode_dot))
            emit(&o, NV2A_TEXSHADER_GLSL);
        emit(&o,
            "uniform vec2 u_tscale[4];\n"
            "uniform vec4 u_c0[8]; uniform vec4 u_c1[8];\n"
            "uniform vec4 u_fc0; uniform vec4 u_fc1;\n"
            "uniform vec4 u_fogcolor;\n"
            "uniform int u_alpha_func; uniform float u_alpha_ref;\n"
            "uniform vec4 u_fogp;\n"
            "out vec4 fragColor;\n"
            NV2A_FOG_GLSL
            "void main() {\n");
        if (texshader_any(k, mode_dot))
            emit(&o, "    float nd0 = 0.0, nd1 = 0.0, nd2 = 0.0, nd3 = 0.0;\n"
                     "    vec3 rv3 = vec3(0.0);\n");
        texshader_note(k);
    } else
    emit(&o,
        "#version 330 core\n"
        "in vec4 vD0; in vec4 vD1; in vec4 vT0; in vec4 vT1;\n"
        "in vec4 vT2; in vec4 vT3; in float vFog;\n"
        "uniform sampler2D t0; uniform sampler2D t1;\n"
        "uniform sampler2D t2; uniform sampler2D t3;\n"
        "uniform vec2 u_tscale[4];\n"
        "uniform vec4 u_c0[8]; uniform vec4 u_c1[8];\n"
        "uniform vec4 u_fc0; uniform vec4 u_fc1;\n"
        "uniform vec4 u_fogcolor;\n"
        "uniform int u_alpha_func; uniform float u_alpha_ref;\n"
        "uniform vec4 u_fogp;\n"
        "out vec4 fragColor;\n"
        NV2A_FOG_GLSL
        "void main() {\n");
    for (i = 0; i < 4; i++)
        tex_fetch(&o, (int)i, (k->shader_program >> (5 * i)) & 0x1F, k);
    emit(&o,
        "    vec4 ZERO = vec4(0.0);\n"
        "    vec4 V0 = vD0, V1 = vD1;\n"
        "    vec4 R0 = vec4(0.0, 0.0, 0.0, T0.a), R1 = vec4(0.0);\n"
        "    vec4 FOG = vec4(u_fogcolor.rgb, nv2a_fog(vFog));\n");
    for (i = 0; i < count; i++)
        stage(&o, (int)i, k);

    /* Final combiner. */
    {
        uint32_t cw0 = k->final0, cw1 = k->final1;
        emit(&o, "    vec4 V1R0 = vec4(%s + %s, 0.0);\n",
             (cw1 & 0x40) ? "(1.0 - clamp(V1.rgb, 0.0, 1.0))" : "clamp(V1.rgb, 0.0, 1.0)",
             (cw1 & 0x20) ? "(1.0 - clamp(R0.rgb, 0.0, 1.0))" : "clamp(R0.rgb, 0.0, 1.0)");
        if (cw1 & 0x80)
            emit(&o, "    V1R0 = clamp(V1R0, 0.0, 1.0);\n");
        emit(&o, "    vec4 EF = vec4(");
        final_input(&o, cw1 >> 24, 0, k);
        emit(&o, " * ");
        final_input(&o, cw1 >> 16, 0, k);
        emit(&o, ", 0.0);\n");
        emit(&o, "    vec3 fA = "); final_input(&o, cw0 >> 24, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fB = "); final_input(&o, cw0 >> 16, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fC = "); final_input(&o, cw0 >> 8, 0, k);  emit(&o, ";\n");
        emit(&o, "    vec3 fD = "); final_input(&o, cw0, 0, k);       emit(&o, ";\n");
        emit(&o, "    float fG = "); final_input(&o, cw1 >> 8, 1, k); emit(&o, ";\n");
        emit(&o, "    fragColor = vec4(fA * fB + (1.0 - fA) * fC + fD, fG);\n");
    }
    /* The alpha test compares 8-bit alpha with the 8-bit reference, as the
     * CPU path's alpha_test_pass does (rounded): unrounded, EQUAL almost
     * never passed. MANHUNT_GL_SHADER_MINOR=0: the old unrounded compare. */
    emit(&o,
        "    fragColor = clamp(fragColor, 0.0, 1.0);\n"
        "    float ar = %s;\n"
        "    bool pass = true;\n",
        nv2a_gl_shader_minor() ? "floor(fragColor.a * 255.0 + 0.5)" : "fragColor.a * 255.0");
    emit(&o,
        "    if (u_alpha_func == 0x200) pass = false;\n"
        "    else if (u_alpha_func == 0x201) pass = ar <  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x202) pass = ar == u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x203) pass = ar <= u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x204) pass = ar >  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x205) pass = ar != u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x206) pass = ar >= u_alpha_ref;\n"
        "    if (!pass) discard;\n"
        "}\n");
    return o.overflow ? -1 : (int)(cap - o.left);
}
