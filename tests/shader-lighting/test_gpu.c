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

void reference_lighting(const uint32_t *, const float *, const float *, const float *, int, float *);
static uint32_t rng = 0x124f00d;
static float random_float(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
static void reg_float(uint32_t *r, unsigned at, float value) { memcpy(r + at / 4, &value, 4); }

int main(void)
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    assert(eglInitialize(display, &major, &minor));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
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
    /* Compile the complete production fixed-function vertex shader as well
     * as the transform-feedback numeric fixture below. */
    const char *fixed_sources[] = {nv2a_gl_vsh_prelude(), nv2a_gl_vsh_lighting(), nv2a_gl_vsh_fixed()};
    GLuint fixed_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(fixed_shader, 3, fixed_sources, NULL); glCompileShader(fixed_shader);
    GLint fixed_ok;
    glGetShaderiv(fixed_shader, GL_COMPILE_STATUS, &fixed_ok);
    if (!fixed_ok) { char log[4096]; glGetShaderInfoLog(fixed_shader, sizeof log, NULL, log); puts(log); return 1; }
    glDeleteShader(fixed_shader);
    const char *sources[] = {"#version 330 core\nlayout(location=0) in vec4 pos;\n"
        "layout(location=1) in vec4 normal;\nlayout(location=2) in vec4 vc;\nout vec4 result;\n",
        nv2a_gl_vsh_lighting(),
        "void main() { result = nv2a_ffp_diffuse(pos, normal, vc); gl_Position = vec4(0,0,0,1); }\n"};
    GLuint shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shader, 3, sources, NULL); glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(shader, sizeof log, NULL, log); puts(log); return 1; }
    GLuint program = glCreateProgram(); glAttachShader(program, shader);
    const char *varying = "result";
    glTransformFeedbackVaryings(program, 1, &varying, GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program); glGetProgramiv(program, GL_LINK_STATUS, &ok); assert(ok);
    glUseProgram(program);
    GLint uniform = glGetUniformLocation(program, "u_light"); assert(uniform >= 0);
    GLuint vao, buffer;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &buffer); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 16, NULL, GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
    glEnable(GL_RASTERIZER_DISCARD);
    float max_error = 0;
    const int flip = getenv("RECOMP_FFP_FLIP_NORMALS") != NULL;
    for (int test = 0; test < 2048; ++test) {
        uint32_t regs[0x2000 / 4] = {0};
        float pos[4], normal[4], vc[4], expected[4], actual[4], light[46][4];
        for (int k = 0; k < 4; ++k) {
            pos[k] = random_float() * 4 - 2;
            normal[k] = test % 17 ? random_float() * 2 - 1 : 0;
            vc[k] = random_float();
        }
        regs[0x0314 / 4] = 1;
        regs[0x0298 / 4] = (test % 4) << 2 | ((test / 4) % 4) << 4;
        for (int k = 0; k < 12; ++k) reg_float(regs, 0x0480 + 4 * k, random_float() * 2 - 1);
        for (int k = 0; k < 3; ++k) reg_float(regs, 0x0A10 + k * 4, random_float() * 0.3f - 0.1f);
        reg_float(regs, 0x03B4, random_float() * 2 - 0.5f);
        for (int i = 0; i < 8; ++i) {
            unsigned b = 0x1000 + i * 0x80;
            regs[0x03BC / 4] |= ((test + i) % 4) << (2 * i);
            for (int k = 0; k < 3; ++k) {
                reg_float(regs, b + k * 4, random_float() * 0.1f);
                reg_float(regs, b + 0x0C + k * 4, random_float() * 0.3f);
                reg_float(regs, b + 0x34 + k * 4, random_float() * 2 - 1);
                reg_float(regs, b + 0x5C + k * 4, random_float() * 8 - 4);
                reg_float(regs, b + 0x68 + k * 4, test % 13 ? random_float() : 0);
            }
            reg_float(regs, b + 0x24, test % 7 ? random_float() * 10 : 0);
        }
        int have_vc = test % 3 != 0;
        reference_lighting(regs, pos, normal, vc, have_vc, expected);
        nv2a_gl_light_uniforms(regs, 1u | (flip ? 2u : 0u) | (have_vc ? 4u : 0u), light);
        glUniform4fv(uniform, 46, &light[0][0]);
        glVertexAttrib4fv(0, pos); glVertexAttrib4fv(1, normal); glVertexAttrib4fv(2, vc);
        glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 16, actual);
        assert(glGetError() == GL_NO_ERROR);
        for (int k = 0; k < 4; ++k) {
            float error = fabsf(expected[k] - actual[k]);
            if (error > max_error) max_error = error;
            if (!isfinite(actual[k]) || error > 0.0001f) {
                fprintf(stderr, "lighting mismatch case %d component %d CPU %g GPU %g\n", test, k, expected[k], actual[k]);
                return 1;
            }
        }
    }
    /* A disabled batch following lit draws restores the incoming colour. */
    float disabled[46][4];
    uint32_t empty_regs[0x2000 / 4] = {0};
    nv2a_gl_light_uniforms(empty_regs, 0, disabled);
    glUniform4fv(uniform, 46, &disabled[0][0]);
    const float unlit[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float result[4];
    glVertexAttrib4fv(2, unlit);
    glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 16, result);
    for (int k = 0; k < 4; ++k) assert(result[k] == unlit[k]);
    printf("PASS GPU lighting versus runtime lit_color: 2048 cases, flip %d, max error %g (%s)\n",
           flip, max_error, glGetString(GL_RENDERER));
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
}
