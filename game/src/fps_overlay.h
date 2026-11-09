/* On-screen flips-per-second counter (MANHUNT_FPS_OVERLAY=1). Draws on the host framebuffer only, never on guest
 * memory, so the game cannot see it and frame dumps do not contain it. */
#ifndef FPS_OVERLAY_H
#define FPS_OVERLAY_H

#include <stdint.h>

typedef struct {
    uint64_t t0_ns;       /* start of the current window, 0 before the first present */
    uint32_t presents;    /* presents since t0_ns */
    char text[16];        /* what to draw: "--" until a window of at least 1 s has ended */
} MhFps;

/* One present at time now_ns. At below 1 flip per second a window is simply one present long. */
void mh_fps_tick(MhFps *fps, uint64_t now_ns);

/* Draws text at the top left of a 32-bit framebuffer (stride in bytes), clipped to width and height. */
void mh_fps_draw(const MhFps *fps, uint32_t *dst, uint32_t stride, uint32_t width, uint32_t height);

#endif
