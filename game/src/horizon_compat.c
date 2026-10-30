/*
 * Horizon (libnx) glue for the xboxrecomp runtime subset the Switch build
 * links (game/CMakeLists.txt):
 *   - the framebuffer-window API the pushbuffer executor presents through
 *     (src/video is SDL-based and not linked): frames are scaled into a
 *     libnx 1280x720 RGBA framebuffer, counted, and dumpable as BMP;
 *   - host FMV playback reports "not playing" (XMV is decoded by the
 *     title's own lifted code);
 *   - newlib gap: sysconf (allocation compatibility lives in horizon_alloc.c).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <switch.h>

extern ptrdiff_t g_xbox_mem_offset;

#define SCREEN_W 1280
#define SCREEN_H 720

static Framebuffer s_fb;
static int s_fb_state;                 /* 0 untried, 1 ready, -1 unavailable */
static volatile uint32_t s_last_va, s_last_pitch;
static volatile uint64_t s_presents;

static const uint8_t *guest_ptr(uint32_t va)
{
    return (const uint8_t *)((uintptr_t)g_xbox_mem_offset + va);
}

/* Guest surfaces are A8R8G8B8 (bytes B,G,R,A); width from the pitch. */
static void surface_dims(uint32_t pitch, uint32_t *w, uint32_t *h)
{
    *w = pitch / 4;
    *h = *w == 640 ? 480 : *w * 3 / 4;
}

void xbox_FramebufferWindowStart(void) {}
void xbox_FramebufferWindowSet(uint32_t fb_va, uint32_t pitch) { s_last_va = fb_va; s_last_pitch = pitch; }
void xbox_FramebufferWindowSetAA(uint32_t sy) { (void)sy; }
void xbox_FramebufferWindowSetTitle(const uint16_t *name, int max_chars) { (void)name; (void)max_chars; }
void xbox_FramebufferWindowFrameStats(uint32_t draws) { (void)draws; }

void xbox_FramebufferWindowPresent(uint32_t fb_va, uint32_t pitch)
{
    uint32_t w, h, x, y, out_w, x0;
    s_last_va = fb_va;
    s_last_pitch = pitch;
    s_presents++;
    if (s_presents == 1 || s_presents % 60 == 0)
        printf("[MH-FRAME] present %llu surface=0x%08X pitch=%u\n",
               (unsigned long long)s_presents, fb_va, pitch);

    if (s_fb_state == 0) {
        s_fb_state = R_SUCCEEDED(framebufferCreate(&s_fb, nwindowGetDefault(), SCREEN_W, SCREEN_H,
                                                   PIXEL_FORMAT_RGBA_8888, 2))
                     && R_SUCCEEDED(framebufferMakeLinear(&s_fb)) ? 1 : -1;
        printf("[MH-FRAME] screen %s\n", s_fb_state > 0 ? "ready" : "unavailable");
    }
    if (s_fb_state < 0 || !pitch)
        return;
    surface_dims(pitch, &w, &h);
    out_w = w * SCREEN_H / h;                      /* keep 4:3, pillarbox */
    x0 = (SCREEN_W - out_w) / 2;
    u32 stride;
    uint32_t *dst = framebufferBegin(&s_fb, &stride);
    for (y = 0; y < SCREEN_H; y++) {
        const uint8_t *src = guest_ptr(fb_va + (y * h / SCREEN_H) * pitch);
        uint32_t *row = (uint32_t *)((uint8_t *)dst + y * stride);
        memset(row, 0, x0 * 4);
        for (x = 0; x < out_w; x++) {
            const uint8_t *p = src + (x * w / out_w) * 4;
            row[x0 + x] = 0xFF000000u | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
        memset(row + x0 + out_w, 0, (SCREEN_W - x0 - out_w) * 4);
    }
    framebufferEnd(&s_fb);
}

/* BMP of the last presented surface: <prefix>NNNNN.bmp. Returns 1 on success. */
int xbox_FramebufferDumpBmp(const char *prefix)
{
    static int seq;
    uint32_t va = s_last_va, pitch = s_last_pitch, w, h, y;
    char path[256];
    FILE *f;
    if (!va || !pitch)
        return 0;
    surface_dims(pitch, &w, &h);
    snprintf(path, sizeof path, "%s%05d.bmp", prefix, seq++);
    f = fopen(path, "wb");
    if (!f)
        return 0;
    uint32_t row = w * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * h;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, sizeof hdr, f);
    uint8_t *line = malloc(row + pad);
    memset(line, 0, row + pad);
    for (y = 0; y < h; y++) {                       /* bottom-up */
        const uint8_t *src = guest_ptr(va + (h - 1 - y) * pitch);
        for (uint32_t x = 0; x < w; x++)
            memcpy(line + x * 3, src + x * 4, 3);   /* B,G,R */
        fwrite(line, 1, row + pad, f);
    }
    free(line);
    fclose(f);
    printf("[MH-FRAME] dump %s (presents=%llu)\n", path, (unsigned long long)s_presents);
    return 1;
}

int xbox_VideoPlayFile(const char *host_path) { (void)host_path; return 0; }
int xbox_VideoIsPlaying(void) { return 0; }

long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE: return 0x1000;
    case _SC_NPROCESSORS_ONLN:
    case _SC_NPROCESSORS_CONF: return 3;     /* cores available to applications */
    default: errno = EINVAL; return -1;
    }
}

/* The watchdog closes the APU's WAV capture before dumping; there is no APU
 * (and no capture) in the Switch build. */
int apu_wav_close_nowait(int wait_ms) { (void)wait_ms; return 0; }
