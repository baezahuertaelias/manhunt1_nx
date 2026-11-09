#include "fps_overlay.h"

#include <stdio.h>
#include <string.h>

#define GLYPH_W 5
#define GLYPH_H 7
#define SCALE 3
#define MARGIN 8

/* 5x7 glyphs, one byte per row, bit 4 is the left column. */
static const uint8_t *glyph(char c)
{
    static const uint8_t digits[10][GLYPH_H] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    };
    static const uint8_t dot[GLYPH_H] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
    static const uint8_t dash[GLYPH_H] = {0, 0, 0, 0x1F, 0, 0, 0};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    return c == '.' ? dot : c == '-' ? dash : NULL;
}

void mh_fps_tick(MhFps *fps, uint64_t now_ns)
{
    if (!fps->t0_ns || now_ns < fps->t0_ns) {
        fps->t0_ns = now_ns;
        fps->presents = 0;
        if (!fps->text[0]) strcpy(fps->text, "--");
        return;
    }
    fps->presents++;
    const uint64_t elapsed = now_ns - fps->t0_ns;
    if (elapsed < 1000000000ull) return;
    snprintf(fps->text, sizeof fps->text, "%.2f", (double)fps->presents * 1e9 / (double)elapsed);
    fps->t0_ns = now_ns;
    fps->presents = 0;
}

static void fill(uint32_t *dst, uint32_t stride, uint32_t width, uint32_t height,
                 uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color)
{
    for (uint32_t y = y0; y < y0 + h && y < height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * stride);
        for (uint32_t x = x0; x < x0 + w && x < width; x++) row[x] = color;
    }
}

void mh_fps_draw(const MhFps *fps, uint32_t *dst, uint32_t stride, uint32_t width, uint32_t height)
{
    const uint32_t advance = (GLYPH_W + 1) * SCALE, len = (uint32_t)strlen(fps->text);
    if (!len) return;
    fill(dst, stride, width, height, MARGIN - SCALE, MARGIN - SCALE, len * advance + SCALE, GLYPH_H * SCALE + 2 * SCALE, 0xFF000000u);
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t *g = glyph(fps->text[i]);
        if (!g) continue;
        for (uint32_t row = 0; row < GLYPH_H; row++)
            for (uint32_t col = 0; col < GLYPH_W; col++)
                if (g[row] & (0x10u >> col))
                    fill(dst, stride, width, height, MARGIN + i * advance + col * SCALE, MARGIN + row * SCALE, SCALE, SCALE, 0xFFFFFFFFu);
    }
}
