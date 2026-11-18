/* Fixed-function fog generation, clip depth, LIT and the alpha test, on the
 * production shader sources (llvmpipe). The expected values follow the
 * environment the run sets: MANHUNT_GL_FF_FOG=0, MANHUNT_GL_CLIP_CLAMP=1 and
 * MANHUNT_GL_SHADER_MINOR=0 each select their old form. */
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
#include "../../game/src/nv2a_gl/gl_psh.h"
#include "../../game/src/nv2a_gl/gl_vsh.h"

int nv2a_gl_gpu_mode(void) { return 1; }   /* nv2a_gl.c; gpu mode accepts every key */

static int env_is(const char *name, char c)
{
    const char *e = getenv(name);
    return e && *e == c;
}

static void reg_float(uint32_t *r, unsigned at, float value) { memcpy(r + at / 4, &value, 4); }

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

/* One point through transform feedback: gl_Position then vFog. */
static void run_point(float out[5])
{
    glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 20, out);
    assert(glGetError() == GL_NO_ERROR);
}

static int near(float a, float b) { return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b)); }

int main(void)
{
    const int ff_fog = !env_is("MANHUNT_GL_FF_FOG", '0');
    const int clip_clamp = env_is("MANHUNT_GL_CLIP_CLAMP", '1');
    const int minor = !env_is("MANHUNT_GL_SHADER_MINOR", '0');
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
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 64, NULL, GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tfb);
    glEnable(GL_RASTERIZER_DISCARD);

    /* The production fixed-function vertex shader, in nv2a_gl.c's order. */
    const char *ff_src[] = {nv2a_gl_vsh_prelude_mode(0), nv2a_gl_vsh_attributes(),
                            nv2a_gl_vsh_lighting(), nv2a_gl_vsh_fixed()};
    const char *vary[] = {"gl_Position", "vFog"};
    GLuint ff = program_tf(ff_src, 4, vary, 2);
    glUseProgram(ff);
    GLint u_fogm = glGetUniformLocation(ff, "u_fogm");
    assert(u_fogm >= 0);
    const float rows[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float surf[4] = {2.0f / 640, 2.0f / 480, 1.0f, 0.0f}, aa[2] = {1, 1}, zero4[4] = {0};
    glUniform4fv(glGetUniformLocation(ff, "u_m"), 4, &rows[0][0]);
    glUniform4fv(glGetUniformLocation(ff, "u_vpoff"), 1, zero4);
    glUniform4fv(glGetUniformLocation(ff, "u_surf"), 1, surf);
    glUniform2fv(glGetUniformLocation(ff, "u_aa"), 1, aa);
    glUniform1i(glGetUniformLocation(ff, "u_xform"), 1);

    /* Clip depth: z = 5 at w = 1 lies beyond the far plane. Linear clip z is
     * 2 * 5 - 1 = 9 (GL_DEPTH_CLAMP clamps per fragment); the old per-vertex
     * clamp gave 1. Inside the volume both agree. */
    {
        float out[5], fogm[5][4];
        uint32_t regs[0x2000 / 4] = {0};
        nv2a_gl_vsh_fog_uniforms(regs, fogm);
        glUniform4fv(u_fogm, 5, &fogm[0][0]);
        const float far_pos[4] = {10, 20, 5, 1}, in_pos[4] = {10, 20, 0.25f, 1};
        glVertexAttrib4fv(0, far_pos); run_point(out);
        if (!near(out[2], clip_clamp ? 1.0f : 9.0f)) {
            fprintf(stderr, "clip z %g, expected %g\n", out[2], clip_clamp ? 1.0f : 9.0f); return 1;
        }
        glVertexAttrib4fv(0, in_pos); run_point(out);
        assert(near(out[2], -0.5f) && near(out[3], 1.0f));
        assert(out[4] == 0.0f);                /* fog off: no coordinate */
        printf("PASS clip z beyond far %s (MANHUNT_GL_CLIP_CLAMP=%d)\n",
               clip_clamp ? "clamped per vertex" : "linear", clip_clamp);
    }

    /* Fog generation per SET_FOG_GEN_MODE against a CPU reference. */
    {
        static const uint32_t gens[] = {0, 1, 2, 3, 6, 5};
        uint32_t rng = 0x5eed;
        int cases = 0;
        float max_fog = 0.0f;
        for (int test = 0; test < 600; test++) {
            uint32_t regs[0x2000 / 4] = {0};
            float pos[4], spec[4], fogc[4], mv[16], plane[4], fogm[5][4], out[5], e[3], expected;
            uint32_t gen = gens[test % 6];
#define RND() ((rng = rng * 1664525u + 1013904223u), (float)(rng >> 8) / 16777216.0f)
            for (int k = 0; k < 4; k++) {
                pos[k] = RND() * 200 - 100; spec[k] = RND() * 1.5f - 0.25f;
                fogc[k] = RND() * 50; plane[k] = RND() * 2 - 1;
            }
            pos[3] = 1;
            for (int k = 0; k < 16; k++) mv[k] = RND() * 2 - 1;
            for (int k = 0; k < 16; k++) reg_float(regs, 0x0480 + 4 * k, mv[k]);
            for (int k = 0; k < 4; k++) reg_float(regs, 0x09D0 + 4 * k, plane[k]);
            regs[0x02A4 / 4] = 1;                  /* SET_FOG_ENABLE */
            regs[0x02A0 / 4] = gen;                /* SET_FOG_GEN_MODE */
            for (int i = 0; i < 3; i++)
                e[i] = mv[4 * i] * pos[0] + mv[4 * i + 1] * pos[1] + mv[4 * i + 2] * pos[2] + mv[4 * i + 3];
            switch (gen) {
            case 0:  expected = spec[3] < 0 ? 0 : spec[3] > 1 ? 1 : spec[3]; break;
            case 1:  expected = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]); break;
            case 2:  expected = plane[0] * e[0] + plane[1] * e[1] + plane[2] * e[2] + plane[3]; break;
            case 3:  expected = fabsf(plane[0] * e[0] + plane[1] * e[1] + plane[2] * e[2] + plane[3]); break;
            case 6:  expected = fogc[0]; break;
            default: expected = fabsf(e[2]); break;   /* unknown mode: |eye z| */
            }
            if (!ff_fog) expected = 0;
            nv2a_gl_vsh_fog_uniforms(regs, fogm);
            glUniform4fv(u_fogm, 5, &fogm[0][0]);
            glVertexAttrib4fv(0, pos); glVertexAttrib4fv(4, spec); glVertexAttrib4fv(5, fogc);
            run_point(out);
            if (!isfinite(out[4]) || fabsf(out[4] - expected) > 1e-3f * (1.0f + fabsf(expected))) {
                fprintf(stderr, "fog mismatch case %d gen %u: GPU %g expected %g\n", test, gen, out[4], expected);
                return 1;
            }
            if (fabsf(out[4]) > max_fog) max_fog = fabsf(out[4]);
            cases++;
        }
        /* The world at a distance: planar fog on eye z, identity model-view. */
        {
            uint32_t regs[0x2000 / 4] = {0};
            float fogm[5][4], out[5];
            const float pos[4] = {3, -2, 80, 1};
            for (int k = 0; k < 4; k++) reg_float(regs, 0x0480 + 20 * k, 1.0f);
            reg_float(regs, 0x09D8, 1.0f);           /* plane (0, 0, 1, 0) */
            regs[0x02A4 / 4] = 1; regs[0x02A0 / 4] = 2;
            nv2a_gl_vsh_fog_uniforms(regs, fogm);
            glUniform4fv(u_fogm, 5, &fogm[0][0]);
            glVertexAttrib4fv(0, pos); run_point(out);
            assert(near(out[4], ff_fog ? 80.0f : 0.0f));
        }
        printf("PASS fixed-function fog coordinate: %d cases, max |fog| %g, eye z 80 gives %s (MANHUNT_GL_FF_FOG=%d)\n",
               cases, max_fog, ff_fog ? "80" : "0", ff_fog);
    }

    /* LIT: z = pow(max(y, 0), w) while x > 0. */
    {
        const char *src[] = {nv2a_gl_vsh_prelude_mode(0),
            "uniform vec4 u_in;\nout vec4 result;\n"
            "void main() { result = nv2a_lit(u_in); gl_Position = vec4(0, 0, 0, 1); }\n"};
        const char *rv[] = {"result"};
        GLuint p = program_tf(src, 2, rv, 1);
        GLint u_in = glGetUniformLocation(p, "u_in");
        const struct { float in[4], z_new, z_old; } cases[] = {
            {{1, 0, 0, 0}, 1, 0}, {{1, -1, 0, 0}, 1, 0}, {{1, 0, 0, 2}, 0, 0},
            {{1, 0.5f, 0, 2}, 0.25f, 0.25f}, {{0, 0, 0, 0}, 0, 0}, {{-1, 0.5f, 0, 2}, 0, 0}};
        glUseProgram(p);
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            float out[4];
            glUniform4fv(u_in, 1, cases[i].in);
            glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
            glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 16, out);
            float want = minor ? cases[i].z_new : cases[i].z_old;
            if (!near(out[2], want) || out[0] != 1.0f || out[3] != 1.0f) {
                fprintf(stderr, "LIT case %u: z %g expected %g\n", i, out[2], want); return 1;
            }
        }
        printf("PASS LIT pow(0, 0) = %d (MANHUNT_GL_SHADER_MINOR=%d)\n", minor, minor);
    }

    /* Alpha test EQUAL on the real fragment shader: alpha 128.3 / 255 rounds
     * to the reference 128 and passes; unrounded it fails. */
    {
        Nv2aPshKey k;
        static char fsrc[65536];
        memset(&k, 0, sizeof k);
        k.final1 = 0x14u << 8;                 /* final alpha = V0.a */
        assert(nv2a_gl_psh(&k, fsrc, sizeof fsrc) > 0);
        const char *vsrc =
            "#version 330 core\nlayout(location = 0) in vec4 a;\n"
            "out vec4 vD0; out vec4 vD1; out vec4 vT0; out vec4 vT1;\n"
            "out vec4 vT2; out vec4 vT3; out float vFog;\n"
            "void main() { vD0 = a; vD1 = vec4(0); vT0 = vT1 = vT2 = vT3 = vec4(0); vFog = 0.0;\n"
            "              gl_Position = vec4(0, 0, 0, 1); gl_PointSize = 4.0; }\n";
        GLint ok;
        GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER), p;
        glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); assert(ok);
        const char *fp = fsrc;
        glShaderSource(fs, 1, &fp, NULL); glCompileShader(fs);
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[4096]; glGetShaderInfoLog(fs, sizeof log, NULL, log); puts(log); return 1; }
        p = glCreateProgram(); glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok); assert(ok);
        glUseProgram(p);
        glUniform1i(glGetUniformLocation(p, "u_alpha_func"), 0x202);
        glUniform1f(glGetUniformLocation(p, "u_alpha_ref"), 128.0f);
        glUniform4f(glGetUniformLocation(p, "u_fogp"), 0, 0, 0, 1);
        glDisable(GL_RASTERIZER_DISCARD);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glViewport(0, 0, 1, 1);
        int passed[2];
        const float alphas[2] = {128.3f / 255.0f, 128.0f / 255.0f};
        for (int i = 0; i < 2; i++) {
            unsigned char px[4];
            const float a[4] = {0, 0, 0, alphas[i]};
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
            glVertexAttrib4fv(0, a);
            glDrawArrays(GL_POINTS, 0, 1);
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            assert(glGetError() == GL_NO_ERROR);
            passed[i] = px[3] != 0;
        }
        if (passed[0] != minor) {
            fprintf(stderr, "alpha EQUAL with alpha 128.3/255: %s, expected %s\n",
                    passed[0] ? "passed" : "discarded", minor ? "passed" : "discarded");
            return 1;
        }
        assert(!minor || passed[1]);
        printf("PASS alpha test EQUAL on %s alpha (MANHUNT_GL_SHADER_MINOR=%d)\n",
               minor ? "8-bit rounded" : "unrounded", minor);
    }
    printf("PASS fog/clip/LIT/alpha shader contracts (%s)\n", glGetString(GL_RENDERER));
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    return 0;
}
