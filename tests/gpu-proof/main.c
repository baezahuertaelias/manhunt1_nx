#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "GPU-PROOF FAIL line=%d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

static int software_renderer(const char *name) {
    return !name || strcasestr(name, "llvmpipe") || strcasestr(name, "softpipe") ||
           strcasestr(name, "swrast") || strcasestr(name, "software") ||
           strcasestr(name, "swiftshader");
}

static int correct_pixel(const unsigned char pixel[4]) {
    const int expected[] = {64, 128, 191, 255};
    for (int channel = 0; channel < 4; ++channel)
        if (abs((int)pixel[channel] - expected[channel]) > 1) return 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        REQUIRE(software_renderer(NULL));
        REQUIRE(software_renderer("LLVMpipe (LLVM 19)"));
        REQUIRE(software_renderer("softpipe"));
        REQUIRE(software_renderer("Software Rasterizer"));
        REQUIRE(software_renderer("SwiftShader"));
        REQUIRE(!software_renderer("Mesa Intel(R) Graphics"));
        REQUIRE(correct_pixel((unsigned char[]){64, 128, 191, 255}));
        for (int channel = 0; channel < 4; ++channel) {
            unsigned char wrong[] = {64, 128, 191, 255};
            wrong[channel] = 0;
            REQUIRE(!correct_pixel(wrong));
        }
        puts("GPU-PROOF classifier/pixel self-test PASS");
        return 0;
    }
    REQUIRE(argc == 1);
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    REQUIRE(fd >= 0);
    struct gbm_device *device = gbm_create_device(fd);
    REQUIRE(device);
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    REQUIRE(platform_display);
    EGLDisplay display = platform_display(EGL_PLATFORM_GBM_KHR, device, NULL);
    REQUIRE(display != EGL_NO_DISPLAY);
    REQUIRE(eglInitialize(display, NULL, NULL));
    REQUIRE(eglBindAPI(EGL_OPENGL_API));
    const EGLint attributes[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_DONT_CARE, EGL_NONE };
    EGLConfig config;
    EGLint count;
    REQUIRE(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    REQUIRE(context != EGL_NO_CONTEXT);
    REQUIRE(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    REQUIRE(renderer && *renderer);
    printf("renderer=%s vendor=%s\n", renderer, glGetString(GL_VENDOR));
    REQUIRE(!software_renderer(renderer));
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 8, 8);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixel[4];
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    REQUIRE(glGetError() == GL_NO_ERROR);
    REQUIRE(correct_pixel(pixel));
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    REQUIRE(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    REQUIRE(eglDestroyContext(display, context));
    REQUIRE(eglTerminate(display));
    gbm_device_destroy(device);
    close(fd);
    printf("GPU-PROOF PASS rgba=%u,%u,%u,%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    return 0;
}
