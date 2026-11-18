/* Fixed-function spot lights and texture-coordinate generation on the
 * production vertex shader (llvmpipe), against a CPU reference written from
 * xemu's vsh-ff.c. The expected values follow the environment:
 * MANHUNT_GL_FF_SPOT=0 lights spots as point lights, MANHUNT_GL_FF_TEXGEN=0
 * passes texture coordinates through. */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../game/src/nv2a_gl/gl_lighting.h"
#include "../../game/src/nv2a_gl/gl_vsh.h"

static int env_is(const char *name, char c)
{
    const char *e = getenv(name);
    return e && *e == c;
}

static void reg_float(uint32_t *r, unsigned at, float value) { memcpy(r + at / 4, &value, 4); }
static float reg_get(const uint32_t *r, unsigned at) { float v; memcpy(&v, r + at / 4, 4); return v; }

static uint32_t s_rng = 0x5eed;
static float rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)(s_rng >> 8) / 16777216.0f;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float dot4(const float *a, const float *b) { return dot3(a, b) + a[3] * b[3]; }
static void normalize3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-8f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static GLuint program_tf(const char **src, int n, const char **vary, int nv)
{
    GLint ok;
    GLuint s = glCreateShader(GL_VERTEX_SHADER), p;
    glShaderSource(s, n, src, NULL); glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, NULL, log); puts(log); exit(1); }
    p = glCreateProgram(); glAttachShader(p, s);
    glTransformFeedbackVaryings(p, nv, vary, GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(p); glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log); puts(log); exit(1); }
    glDeleteShader(s);
    return p;
}

/* One point through transform feedback: vD0, vT0..vT3. */
static void run_point(float out[20])
{
    glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 80, out);
    assert(glGetError() == GL_NO_ERROR);
}

/* lit_color's local light plus xemu's LIGHT_SPOT when `cone`. Colour
 * material off, so the vertex colour plays no part. Returns which spot
 * region the last spot light hit: 0 inside, 1 ramp, 2 outside, -1 none. */
static int ref_light(const uint32_t *r, const float *pos, const float *nrm, int cone, float out[4], int region[3])
{
    const float *mv = (const float *)&r[0x0480 / 4];
    float e[3], n[3];
    for (int i = 0; i < 3; i++) {
        e[i] = mv[4 * i] * pos[0] + mv[4 * i + 1] * pos[1] + mv[4 * i + 2] * pos[2] + mv[4 * i + 3];
        n[i] = mv[4 * i] * nrm[0] + mv[4 * i + 1] * nrm[1] + mv[4 * i + 2] * nrm[2];
    }
    normalize3(n);
    out[0] = reg_get(r, 0x0A10); out[1] = reg_get(r, 0x0A14); out[2] = reg_get(r, 0x0A18);
    for (int i = 0; i < 8; i++) {
        uint32_t kind = (r[0x03BC / 4] >> (2 * i)) & 3, b = 0x1000 + (uint32_t)i * 0x80;
        float l[3], att = 1.0f, ndl;
        if (!kind) continue;
        if (kind == 1) {
            l[0] = reg_get(r, b + 0x34); l[1] = reg_get(r, b + 0x38); l[2] = reg_get(r, b + 0x3C);
        } else {
            float d;
            for (int k = 0; k < 3; k++) l[k] = reg_get(r, b + 0x5C + 4 * k) - e[k];
            d = sqrtf(dot3(l, l));
            if (d > reg_get(r, b + 0x24) && reg_get(r, b + 0x24) > 0.0f) continue;
            if (d > 1e-8f) { l[0] /= d; l[1] /= d; l[2] /= d; }
            att = reg_get(r, b + 0x68) + reg_get(r, b + 0x6C) * d + reg_get(r, b + 0x70) * d * d;
            att = att > 1e-8f ? 1.0f / att : 1.0f;
            if (kind == 3 && cone) {
                float sd[4];
                for (int k = 0; k < 4; k++) sd[k] = reg_get(r, b + 0x4C + 4 * k);
                float sl = sqrtf(dot3(sd, sd));
                if (sl > 0.0f) {
                    float inv = 1.0f / sl, cphi = -inv * sd[3], ctheta = inv + cphi;
                    float sdvp = dot3(sd, l), rho = inv * sdvp;
                    if (rho > ctheta) region[0]++;
                    else if (rho <= cphi) { att = 0.0f; region[2]++; }
                    else { att *= sdvp + sd[3]; region[1]++; }
                }
            }
        }
        ndl = dot3(n, l); if (ndl < 0.0f) ndl = 0.0f;
        for (int k = 0; k < 3; k++)
            out[k] += att * (reg_get(r, b + 4 * k) + reg_get(r, b + 0x0C + 4 * k) * ndl);
    }
    out[3] = reg_get(r, 0x03B4);
    for (int i = 0; i < 4; i++) out[i] = out[i] < 0 ? 0 : out[i] > 1 ? 1 : out[i];
    return 0;
}

/* xemu vsh-ff.c texgen then texture matrix, for stage s. The eye normal is
 * the model-view rows times the normal, normalised (as the lighting). */
static void ref_tex(const uint32_t *r, int s, const float *pos, const float *nrm, const float *t, float o[4])
{
    const float *mv = (const float *)&r[0x0480 / 4];
    float ep[4], en[3], u[3], rf[3], m, tmp[4];
    for (int i = 0; i < 4; i++) ep[i] = dot4(mv + 4 * i, pos);
    for (int i = 0; i < 3; i++) en[i] = dot3(mv + 4 * i, nrm);
    normalize3(en);
    memcpy(u, ep, 12);
    if (dot3(u, u) > 0) normalize3(u); else { u[0] = u[1] = 0; u[2] = -1; }
    float dn = dot3(en, u);
    for (int i = 0; i < 3; i++) rf[i] = u[i] - 2.0f * dn * en[i];
    m = sqrtf(rf[0] * rf[0] + rf[1] * rf[1] + (rf[2] + 1) * (rf[2] + 1));
    m = m > 0 ? 1.0f / (2.0f * m) : 0.0f;
    for (int c = 0; c < 4; c++) {
        uint32_t g = r[(0x03C0 + s * 0x10 + c * 4) / 4];
        const float *plane = (const float *)&r[(0x0840 + s * 0x40 + c * 0x10) / 4];
        switch (g) {
        case 0x2400: o[c] = dot4(plane, ep); break;
        case 0x2401: o[c] = dot4(plane, pos); break;
        case 0x2402: o[c] = c < 2 ? rf[c] * m + 0.5f : t[c]; break;
        case 0x8511: o[c] = c < 3 ? en[c] : t[c]; break;
        case 0x8512: o[c] = c < 3 ? rf[c] : t[c]; break;
        default:     o[c] = t[c]; break;
        }
    }
    if (r[(0x0420 + s * 4) / 4]) {
        const float *tm = (const float *)&r[(0x06C0 + s * 0x40) / 4];
        for (int j = 0; j < 4; j++) tmp[j] = dot4(tm + 4 * j, o);
        memcpy(o, tmp, 16);
    }
}

static int close_to(float a, float b, float tol) { return isfinite(a) && fabsf(a - b) <= tol * (1.0f + fabsf(b)); }

int main(void)
{
    const int spot = !env_is("MANHUNT_GL_FF_SPOT", '0');
    const int texgen = !env_is("MANHUNT_GL_FF_TEXGEN", '0');
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor_v, count;
    assert(eglInitialize(display, &major, &minor_v));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                             EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    assert(eglChooseConfig(display, config_attrs, &config, 1, &count) && count);
    EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs);
    assert(context != EGL_NO_CONTEXT);
    EGLint surface_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
    assert(surface != EGL_NO_SURFACE);
    assert(eglMakeCurrent(display, surface, surface, context));

    GLuint vao, tfb;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &tfb); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, tfb);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 128, NULL, GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tfb);
    glEnable(GL_RASTERIZER_DISCARD);

    /* The production fixed-function vertex shader, in nv2a_gl.c's order. */
    const char *ff_src[] = {nv2a_gl_vsh_prelude_mode(0), nv2a_gl_vsh_attributes(),
                            nv2a_gl_vsh_lighting(), nv2a_gl_vsh_fixed()};
    const char *vary[] = {"vD0", "vT0", "vT1", "vT2", "vT3"};
    GLuint ff = program_tf(ff_src, 4, vary, 5);
    glUseProgram(ff);
    GLint u_ffx = glGetUniformLocation(ff, "u_ffx"), u_light = glGetUniformLocation(ff, "u_light");
    GLint u_xform = glGetUniformLocation(ff, "u_xform");
    assert(u_ffx >= 0 && u_light >= 0 && u_xform >= 0);
    const float rows[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float surf[4] = {2.0f / 640, 2.0f / 480, 1.0f, 0.0f}, aa[2] = {1, 1}, zero4[4] = {0};
    glUniform4fv(glGetUniformLocation(ff, "u_m"), 4, &rows[0][0]);
    glUniform4fv(glGetUniformLocation(ff, "u_vpoff"), 1, zero4);
    glUniform4fv(glGetUniformLocation(ff, "u_surf"), 1, surf);
    glUniform2fv(glGetUniformLocation(ff, "u_aa"), 1, aa);
    glUniform1i(u_xform, 1);

    /* Spot cones: lights aimed near the vertex so every region is hit. */
    {
        int region[3] = {0, 0, 0}, cases = 0;
        float max_err = 0;
        for (int test = 0; test < 2000; test++) {
            uint32_t regs[0x2000 / 4] = {0};
            float pos[4], nrm[4], vc[4] = {1, 1, 1, 1}, expected[4], out[20];
            float light[46][4], ffx[NV2A_FFX_ROWS][4];
            for (int k = 0; k < 3; k++) { pos[k] = rnd() * 4 - 2; nrm[k] = rnd() * 2 - 1; }
            pos[3] = 1; nrm[3] = 0;
            regs[0x0314 / 4] = 1;
            for (int k = 0; k < 12; k++) reg_float(regs, 0x0480 + 4 * k, (k % 5 == 0 ? 1.0f : 0.0f) + rnd() * 0.4f - 0.2f);
            for (int k = 0; k < 3; k++) reg_float(regs, 0x0A10 + 4 * k, rnd() * 0.1f);
            reg_float(regs, 0x03B4, rnd());
            const float *mv = (const float *)&regs[0x0480 / 4];
            float e[3];
            for (int i = 0; i < 3; i++) e[i] = dot3(mv + 4 * i, pos) + mv[4 * i + 3];
            for (int i = 0; i < 8; i++) {
                unsigned b = 0x1000 + i * 0x80;
                uint32_t kind = (test + i) % 5 == 0 ? 2 : 3;   /* mostly spots */
                if ((test + i) % 11 == 0) kind = 1;
                regs[0x03BC / 4] |= kind << (2 * i);
                float lp[3], dir[3];
                for (int k = 0; k < 3; k++) {
                    reg_float(regs, b + 4 * k, rnd() * 0.1f);
                    reg_float(regs, b + 0x0C + 4 * k, rnd() * 0.3f);
                    reg_float(regs, b + 0x34 + 4 * k, rnd() * 2 - 1);
                    lp[k] = e[k] + rnd() * 6 - 3;
                    reg_float(regs, b + 0x5C + 4 * k, lp[k]);
                    reg_float(regs, b + 0x68 + 4 * k, k == 0 ? 1.0f : rnd() * 0.1f);
                    /* Toward the vertex plus a random tilt. */
                    dir[k] = e[k] - lp[k] + (rnd() * 2 - 1) * 2.5f;
                }
                normalize3(dir);
                float theta = rnd() * 1.5f + 0.05f, phi = theta + rnd() * 1.2f + 0.05f;
                float ct = cosf(theta * 0.5f), cp = cosf(phi * 0.5f), sc = 1.0f / (ct - cp);
                for (int k = 0; k < 3; k++) reg_float(regs, b + 0x4C + 4 * k, -dir[k] * sc);
                reg_float(regs, b + 0x4C + 12, -sc * cp);
                reg_float(regs, b + 0x40, 1.0f);              /* falloff: unused */
                reg_float(regs, b + 0x24, test % 7 ? 0.0f : rnd() * 6);
            }
            ref_light(regs, pos, nrm, spot, expected, region);
            nv2a_gl_light_uniforms(regs, 1u, light);
            nv2a_gl_vsh_ffx_uniforms(regs, 1u, ffx);
            glUniform4fv(u_light, 46, &light[0][0]);
            glUniform4fv(u_ffx, NV2A_FFX_ROWS, &ffx[0][0]);
            glVertexAttrib4fv(0, pos); glVertexAttrib4fv(2, nrm); glVertexAttrib4fv(3, vc);
            run_point(out);
            for (int k = 0; k < 4; k++) {
                float err = fabsf(out[k] - expected[k]);
                if (err > max_err) max_err = err;
                if (!close_to(out[k], expected[k], 1e-4f)) {
                    fprintf(stderr, "spot mismatch case %d comp %d: GPU %g CPU %g\n", test, k, out[k], expected[k]);
                    return 1;
                }
            }
            cases++;
        }
        if (spot && (!region[0] || !region[1] || !region[2])) {
            fprintf(stderr, "spot regions not all covered: %d %d %d\n", region[0], region[1], region[2]);
            return 1;
        }
        /* A light on the axis vs off it: identity model-view, spot at the
         * origin pointing down +z, inner 20 deg, outer 40 deg. */
        {
            uint32_t regs[0x2000 / 4] = {0};
            float light[46][4], ffx[NV2A_FFX_ROWS][4], out[20];
            const float nrm[4] = {0, 0, -1, 0}, on_axis[4] = {0, 0, 5, 1}, off_axis[4] = {5, 0, 5, 1};
            for (int k = 0; k < 4; k++) reg_float(regs, 0x0480 + 20 * k, 1.0f);
            regs[0x03BC / 4] = 3;
            reg_float(regs, 0x100C, 1.0f);                     /* diffuse red */
            reg_float(regs, 0x1068, 1.0f);                     /* attenuation 1 */
            float ct = cosf(10.0f * 3.14159265f / 180), cp = cosf(20.0f * 3.14159265f / 180), sc = 1 / (ct - cp);
            reg_float(regs, 0x1054, -sc);                      /* -dir.z * sc */
            reg_float(regs, 0x1058, -sc * cp);
            nv2a_gl_light_uniforms(regs, 1u, light);
            nv2a_gl_vsh_ffx_uniforms(regs, 1u, ffx);
            glUniform4fv(u_light, 46, &light[0][0]);
            glUniform4fv(u_ffx, NV2A_FFX_ROWS, &ffx[0][0]);
            glVertexAttrib4fv(2, nrm);
            glVertexAttrib4fv(0, on_axis); run_point(out);
            assert(close_to(out[0], 1.0f, 1e-4f));
            glVertexAttrib4fv(0, off_axis); run_point(out);
            if (!close_to(out[0], spot ? 0.0f : 0.70710678f, 1e-4f)) {
                fprintf(stderr, "off-axis spot red %g\n", out[0]); return 1;
            }
        }
        printf("PASS fixed-function spot lights: %d cases, inside %d ramp %d outside %d, max error %g (MANHUNT_GL_FF_SPOT=%d)\n",
               cases, region[0], region[1], region[2], max_err, spot);
    }

    /* Texgen modes and texture matrices, every stage. Lighting off. */
    {
        static const uint32_t modes[] = {0, 0x2400, 0x2401, 0x2402, 0x8511, 0x8512, 0x855F};
        float light[46][4], max_err = 0;
        uint32_t none[0x2000 / 4] = {0};
        int cases = 0, mode_hits[7] = {0};
        nv2a_gl_light_uniforms(none, 0, light);
        glUniform4fv(u_light, 46, &light[0][0]);
        for (int test = 0; test < 1500; test++) {
            uint32_t regs[0x2000 / 4] = {0};
            float pos[4], nrm[4], t[4][4], expected[4][4], out[20], ffx[NV2A_FFX_ROWS][4];
            for (int k = 0; k < 4; k++) {
                pos[k] = rnd() * 20 - 10; nrm[k] = rnd() * 2 - 1;
                for (int s = 0; s < 4; s++) t[s][k] = rnd() * 4 - 2;
            }
            pos[3] = test % 3 ? 1.0f : rnd() + 0.5f;
            for (int k = 0; k < 16; k++) reg_float(regs, 0x0480 + 4 * k, rnd() * 2 - 1);
            if (test % 4 == 0) {                                /* affine model-view */
                reg_float(regs, 0x04B0, 0); reg_float(regs, 0x04B4, 0);
                reg_float(regs, 0x04B8, 0); reg_float(regs, 0x04BC, 1);
            }
            for (int s = 0; s < 4; s++) {
                for (int c = 0; c < 4; c++) {
                    uint32_t m = modes[(s * 4 + c + test) % 7];
                    if ((test + s) % 9 == 0) m = 0;
                    regs[(0x03C0 + s * 0x10 + c * 4) / 4] = m;
                    for (int k = 0; k < 4; k++) reg_float(regs, 0x0840 + s * 0x40 + c * 0x10 + 4 * k, rnd() * 2 - 1);
                }
                regs[(0x0420 + s * 4) / 4] = (test + s) % 3 == 0;
                for (int k = 0; k < 16; k++) reg_float(regs, 0x06C0 + s * 0x40 + 4 * k, rnd() * 2 - 1);
            }
            for (int s = 0; s < 4; s++) {
                if (texgen) ref_tex(regs, s, pos, nrm, t[s], expected[s]);
                else memcpy(expected[s], t[s], 16);
                for (int c = 0; c < 4; c++)
                    for (int i = 0; i < 7; i++)
                        if (regs[(0x03C0 + s * 0x10 + c * 4) / 4] == modes[i]) mode_hits[i]++;
            }
            nv2a_gl_vsh_ffx_uniforms(regs, 0, ffx);
            glUniform4fv(u_ffx, NV2A_FFX_ROWS, &ffx[0][0]);
            glVertexAttrib4fv(0, pos); glVertexAttrib4fv(2, nrm);
            for (int s = 0; s < 4; s++) glVertexAttrib4fv(9 + s, t[s]);
            run_point(out);
            for (int s = 0; s < 4; s++)
                for (int c = 0; c < 4; c++) {
                    float got = out[4 + 4 * s + c], want = expected[s][c];
                    float err = fabsf(got - want) / (1.0f + fabsf(want));
                    if (err > max_err) max_err = err;
                    if (!close_to(got, want, 2e-4f)) {
                        fprintf(stderr, "texgen mismatch case %d stage %d comp %d mode 0x%x matrix %u: GPU %g CPU %g\n",
                                test, s, c, regs[(0x03C0 + s * 0x10 + c * 4) / 4], regs[(0x0420 + s * 4) / 4], got, want);
                        return 1;
                    }
                }
            cases++;
        }
        for (int i = 0; i < 7; i++) assert(mode_hits[i] > 0);
        /* Known value: sphere map S/T of a vertex straight ahead, normal to
         * the eye, is the centre (0.5, 0.5); a stage matrix that swaps S and
         * T and adds 1 to T moves it. Pre-transformed draws pass through. */
        {
            uint32_t regs[0x2000 / 4] = {0};
            float out[20], ffx[NV2A_FFX_ROWS][4];
            const float pos[4] = {0, 0, -3, 1}, nrm[4] = {0, 0, 1, 0}, t0[4] = {7, 8, 9, 1};
            for (int k = 0; k < 4; k++) reg_float(regs, 0x0480 + 20 * k, 1.0f);
            regs[0x03C0 / 4] = regs[0x03C4 / 4] = 0x2402;
            regs[0x0424 / 4] = 1;                               /* stage 1 matrix */
            regs[0x03D0 / 4] = regs[0x03D4 / 4] = 0x2402;
            reg_float(regs, 0x0700 + 4 * 1, 1.0f);              /* row 0 = (0, 1, 0, 0) */
            reg_float(regs, 0x0710 + 4 * 0, 1.0f);              /* row 1 = (1, 0, 0, 1) */
            reg_float(regs, 0x0710 + 4 * 3, 1.0f);
            reg_float(regs, 0x0720 + 4 * 2, 1.0f);
            reg_float(regs, 0x0730 + 4 * 3, 1.0f);
            nv2a_gl_vsh_ffx_uniforms(regs, 0, ffx);
            glUniform4fv(u_ffx, NV2A_FFX_ROWS, &ffx[0][0]);
            glVertexAttrib4fv(0, pos); glVertexAttrib4fv(2, nrm);
            glVertexAttrib4fv(9, t0); glVertexAttrib4fv(10, t0);
            run_point(out);
            if (texgen) {
                assert(close_to(out[4], 0.5f, 1e-5f) && close_to(out[5], 0.5f, 1e-5f));
                assert(out[6] == 9.0f && out[7] == 1.0f);
                assert(close_to(out[8], 0.5f, 1e-5f) && close_to(out[9], 1.5f, 1e-5f));
            } else {
                assert(out[4] == 7.0f && out[5] == 8.0f && out[8] == 7.0f && out[9] == 8.0f);
            }
            glUniform1i(u_xform, 0);
            run_point(out);
            assert(out[4] == 7.0f && out[5] == 8.0f && out[6] == 9.0f && out[7] == 1.0f);
            glUniform1i(u_xform, 1);
        }
        printf("PASS fixed-function texgen + texture matrix: %d cases x 4 stages, max rel error %g (MANHUNT_GL_FF_TEXGEN=%d)\n",
               cases, max_err, texgen);
    }
    printf("PASS spot/texgen shader contracts (%s)\n", glGetString(GL_RENDERER));
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    return 0;
}
