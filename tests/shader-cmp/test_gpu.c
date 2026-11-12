#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../game/src/nv2a_gl/gl_vsh.h"
void reference_cmp(uint32_t, float *);
void reference_cmp_direct_contract(void);
int main(void) {
    reference_cmp_direct_contract();
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

    const char *sources[] = {nv2a_gl_vsh_prelude(), nv2a_gl_vsh_attributes(),
        "out vec4 result;\nvoid main() { result = v2; gl_Position = vec4(0,0,0,1); }\n"};
    GLuint shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shader, 3, sources, NULL); glCompileShader(shader);
    GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(shader, sizeof log, NULL, log); puts(log); return 1; }
    GLuint program = glCreateProgram(); glAttachShader(program, shader);
    const char *varying = "result";
    glTransformFeedbackVaryings(program, 1, &varying, GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program); glGetProgramiv(program, GL_LINK_STATUS, &ok); assert(ok);
    glUseProgram(program);
    GLint mask = glGetUniformLocation(program, "u_cmp_mask"); assert(mask >= 0);
    enum { COUNT = 2048 + 2048 + 1024 + 8192 };
    uint32_t packed[COUNT]; float actual[COUNT][4];
    int n = 0;
    /* Exhaust every signed component encoding with nonzero neighbours, then
     * random complete words. Components decode independently: this covers
     * all encodings of each field, including -1024/-512 and byte crossings. */
    for (uint32_t i=0;i<2048;i++) packed[n++] = 0xa5b6c000u | i;
    for (uint32_t i=0;i<2048;i++) packed[n++] = (0xa5800123u & ~(2047u<<11)) | (i<<11);
    for (uint32_t i=0;i<1024;i++) packed[n++] = 0x002a5123u | (i<<22);
    uint32_t rng=0x12a5cfeu;
    for (int i=0;i<8192;i++) { rng=rng*1664525u+1013904223u; packed[n++]=rng; }
    assert(n==COUNT);
    GLuint vao, input, output;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&input);glBindBuffer(GL_ARRAY_BUFFER,input);
    glBufferData(GL_ARRAY_BUFFER,sizeof packed,packed,GL_STATIC_DRAW);
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,4,NULL);glEnableVertexAttribArray(2);
    glGenBuffers(1,&output);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,sizeof actual,NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output);glEnable(GL_RASTERIZER_DISCARD);
    glUniform1i(mask,1<<2);
    glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,COUNT);glEndTransformFeedback();
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof actual,actual);
    assert(glGetError()==GL_NO_ERROR);
    for (int i=0;i<COUNT;i++) {
        float expected[4];reference_cmp(packed[i],expected);
        if (memcmp(expected,actual[i],16)) {
            fprintf(stderr,"CMP mismatch word %08x CPU %.9g %.9g %.9g GPU %.9g %.9g %.9g\n",packed[i],expected[0],expected[1],expected[2],actual[i][0],actual[i][1],actual[i][2]);return 1;
        }
    }
    /* Same program/VAO must immediately restore ordinary attributes. */
    glUniform1i(mask,0);glDisableVertexAttribArray(2);
    float ordinary[4]={.25f,-.5f,.75f,1};glVertexAttrib4fv(2,ordinary);
    glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,16,actual);
    assert(!memcmp(actual,ordinary,16));
    printf("PASS CMP shader vs fetch_attr: %d exact words, every component encoding, mixed words and mask reset (%s)\n",COUNT,glGetString(GL_RENDERER));
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
