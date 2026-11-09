/*
 * MANHUNT_GPU_BACKEND=2: the OpenGL NV2A renderer (nv2a_gl/). A timeboxed proof.
 *
 * The window and context are made here on the main thread, then released for the executor's thread to adopt on its
 * first call, as burnout3-sw does with its loading screen.
 */
#include <SDL.h>
#include <malloc.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "nv2a_gl/nv2a_gl.h"

int manhunt_gl_active;
static SDL_Window *s_win;
static SDL_GLContext s_ctx;

int manhunt_gl_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        printf("[MH-GL] SDL video: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    s_win = SDL_CreateWindow("NV2A", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL);
    if (!s_win) {
        printf("[MH-GL] window: %s\n", SDL_GetError());
        return 0;
    }
    s_ctx = SDL_GL_CreateContext(s_win);
    if (!s_ctx) {
        printf("[MH-GL] context: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_MakeCurrent(s_win, NULL);     /* the executor's thread takes it */
    manhunt_gl_active = 1;
    nv2a_gl_install();
    struct mallinfo mi = mallinfo();
    printf("[MH-GL] window and context made; heap in use %zu MB, free in the arena %zu MB\n",
           (size_t)mi.uordblks >> 20, (size_t)mi.fordblks >> 20);
    return 1;
}

int nv2a_gl_adopt_window(void **win, void **ctx)
{
    if (!s_win)
        return 0;
    *win = s_win;
    *ctx = s_ctx;
    return 1;
}

/* What the renderer links against and this runtime does not have. Each is harmless here and documented:
 * RECOMP_FPS_LOG's vblank text is optional, and the two thread hooks only serve the off-by-default
 * RECOMP_GL_THREAD=1 mode, where they place a thread on another core and list it for reports.
 * xbox_video_widescreen is in widescreen.c (MANHUNT_WIDESCREEN). */
void xbox_vblank_report(double dt, char *buf, size_t n) { (void)dt; if (n) buf[0] = 0; }
void xbox_nx_spread_thread(void) {}
void xbox_nx_track_thread(void *entry) { (void)entry; }
