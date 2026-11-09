/* Host test of game/src/fps_overlay.c: the rate, and which pixels the counter touches. Needs no console.
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I ../../game/src ../../game/src/fps_overlay.c test_fps_overlay.c -o /tmp/t && /tmp/t
 * Scope: the arithmetic and the drawing only. It says nothing about how the counter looks on the console. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fps_overlay.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)

#define W 320
#define H 80
#define STRIDE (W * 4 + 64)          /* a stride wider than the row, as the real framebuffer may have */
#define BG 0x12345678u

static uint8_t mem[STRIDE * H];

static uint32_t px(uint32_t x, uint32_t y)
{
    uint32_t v;
    memcpy(&v, mem + (size_t)y * STRIDE + (size_t)x * 4, 4);
    return v;
}

static void clear(void)
{
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < STRIDE / 4; x++) memcpy(mem + (size_t)y * STRIDE + (size_t)x * 4, &(uint32_t){BG}, 4);
}

int main(void)
{
    MhFps f;

    /* 30 presents in one second: one window, 30.00. The first present only starts the clock. */
    memset(&f, 0, sizeof f);
    const uint64_t t = 5000000000ull;
    mh_fps_tick(&f, t);
    CHECK(strcmp(f.text, "--") == 0);
    for (int i = 1; i < 30; i++) mh_fps_tick(&f, t + (uint64_t)i * 33333333ull);
    CHECK(strcmp(f.text, "--") == 0);
    mh_fps_tick(&f, t + 1000000000ull);
    CHECK(strcmp(f.text, "30.00") == 0);

    /* The slow scene: one present every 5.4 s gives 0.19, as the log measures. */
    memset(&f, 0, sizeof f);
    mh_fps_tick(&f, t);
    mh_fps_tick(&f, t + 5400000000ull);
    CHECK(strcmp(f.text, "0.19") == 0);
    mh_fps_tick(&f, t + 10800000000ull);
    CHECK(strcmp(f.text, "0.19") == 0);

    /* A clock that goes backwards restarts the window and keeps the last value. */
    mh_fps_tick(&f, t);
    CHECK(strcmp(f.text, "0.19") == 0);
    CHECK(f.presents == 0);

    /* Drawing: "1" lights known pixels, the box is black, and nothing outside it changes. */
    clear();
    memset(&f, 0, sizeof f);
    strcpy(f.text, "1");
    mh_fps_draw(&f, (uint32_t *)mem, STRIDE, W, H);
    CHECK(px(8 + 2 * 3, 8) == 0xFFFFFFFFu);           /* top of the 1's stem */
    CHECK(px(8, 8) == 0xFF000000u);                   /* box, not lit */
    CHECK(px(5, 5) == 0xFF000000u);                   /* box corner */
    CHECK(px(4, 5) == BG && px(5, 4) == BG);          /* just outside */
    CHECK(px(5 + 21, 5) == BG);                       /* just past one character */
    CHECK(px(100, 40) == BG);

    /* Each digit differs, and the dot and dash draw. */
    uint32_t seen[12];
    const char *chars = "0123456789.-";
    for (int i = 0; i < 12; i++) {
        clear();
        f.text[0] = chars[i]; f.text[1] = 0;
        mh_fps_draw(&f, (uint32_t *)mem, STRIDE, W, H);
        uint32_t h = 0, lit = 0;
        for (uint32_t y = 8; y < 8 + 21; y += 3)
            for (uint32_t x = 8; x < 8 + 15; x += 3) { h = h * 3 + (px(x, y) == 0xFFFFFFFFu); lit += px(x, y) == 0xFFFFFFFFu; }
        CHECK(lit > 0);
        seen[i] = h;
        for (int j = 0; j < i; j++) CHECK(seen[j] != h);
    }

    /* Clipped to the given size: a long value on a tiny framebuffer stays inside it (ASan would catch a stray write). */
    clear();
    strcpy(f.text, "12345.67");
    mh_fps_draw(&f, (uint32_t *)mem, STRIDE, 20, 12);
    CHECK(px(20, 8) == BG);
    CHECK(px(8, 12) == BG);
    CHECK(px(19, 8) != BG);

    /* Nothing to draw leaves it alone. */
    clear();
    f.text[0] = 0;
    mh_fps_draw(&f, (uint32_t *)mem, STRIDE, W, H);
    CHECK(px(5, 5) == BG);

    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", fails);
    return fails != 0;
}
