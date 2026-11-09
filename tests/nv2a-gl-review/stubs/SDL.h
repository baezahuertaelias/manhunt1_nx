#ifndef GL_REVIEW_SDL_H
#define GL_REVIEW_SDL_H
#include <stddef.h>
#include <stdint.h>
extern int gl_review_sdl_failure;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef void *SDL_GLContext;
typedef struct { unsigned type; } SDL_Event;
#define SDL_INIT_VIDEO 1
#define SDL_GL_CONTEXT_PROFILE_MASK 2
#define SDL_GL_CONTEXT_PROFILE_CORE 3
#define SDL_GL_CONTEXT_MAJOR_VERSION 4
#define SDL_GL_CONTEXT_MINOR_VERSION 5
#define SDL_GL_DOUBLEBUFFER 6
#define SDL_WINDOW_OPENGL 1
#define SDL_WINDOW_RESIZABLE 2
#define SDL_WINDOWPOS_CENTERED 0
static inline int SDL_InitSubSystem(unsigned x) { (void)x; return gl_review_sdl_failure?-1:0; }
static inline int SDL_GL_SetAttribute(int x,int y) { (void)x;(void)y;return 0; }
static inline SDL_Window *SDL_CreateWindow(const char *t,int x,int y,int w,int h,unsigned f) {(void)t;(void)x;(void)y;(void)w;(void)h;(void)f;return (SDL_Window*)(uintptr_t)1;}
static inline SDL_GLContext SDL_GL_CreateContext(SDL_Window *w) {(void)w;return (void*)(uintptr_t)1;}
static inline int SDL_GL_MakeCurrent(SDL_Window *w,SDL_GLContext c) {(void)w;(void)c;return -1;}
static inline const char *SDL_GetError(void) {return "synthetic SDL failure";}
static inline int SDL_GL_SetSwapInterval(int x) {(void)x;return 0;}
static inline void *SDL_GL_GetProcAddress(const char *n) {(void)n;return NULL;}
static inline void SDL_GL_GetDrawableSize(SDL_Window *w,int *x,int *y) {(void)w;*x=640;*y=480;}
static inline void SDL_GL_SwapWindow(SDL_Window *w) {(void)w;}
static inline int SDL_PollEvent(SDL_Event *e) {(void)e;return 0;}
#endif
