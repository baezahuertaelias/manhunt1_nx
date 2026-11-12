/* Bitwise differential proof, including full filter state and aliasing. */
#define _DEFAULT_SOURCE
#include "apu_state.h"
#include <time.h>
static uint32_t rng = 0x719a3c5b;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng; }
static float value(void) { return ((int32_t)(next() >> 8) - 0x800000) / 8388608.0f; }
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
int main(void) {
    unsigned cases = 0;
    for (unsigned pos = 0; pos < HRTF_BUFLEN; pos++) for (unsigned delay = 0; delay <= 84; delay++) {
        HrtfFilter a = {0}, b;
        a.buf_pos = pos; a.itd_cur = (int)delay - 42; a.itd_tar = ((int)(next() % 3361) - 1680) / 40.0f;
        for (unsigned c = 0; c < 2; c++) {
            for (unsigned i = 0; i < HRTF_BUFLEN; i++) a.ch[c].buf[i] = value();
            for (unsigned k = 0; k < HRTF_NUM_TAPS; k++) { a.ch[c].hrir_coeff_cur[k] = value() / 31; a.ch[c].hrir_coeff_tar[k] = value() / 31; }
        }
        b = a;
        for (unsigned frame = 0; frame < 3; frame++) {
            float input[NUM_SAMPLES_PER_FRAME][2], legacy[NUM_SAMPLES_PER_FRAME][2], fast[NUM_SAMPLES_PER_FRAME][2];
            for (unsigned n = 0; n < NUM_SAMPLES_PER_FRAME; n++) for (unsigned c = 0; c < 2; c++) input[n][c] = value();
            if (frame & 1) {
                memcpy(legacy, input, sizeof input); memcpy(fast, input, sizeof input);
                hrtf_filter_process(&a, legacy, legacy); hrtf_filter_process_fast(&b, fast, fast);
            } else { hrtf_filter_process(&a, input, legacy); hrtf_filter_process_fast(&b, input, fast); }
            assert(!memcmp(legacy, fast, sizeof legacy)); assert(!memcmp(&a, &b, sizeof a)); cases++;
        }
    }
    /* Signed zero, stationary parameters and subnormals preserve all bits. */
    for (unsigned special = 0; special < 4; special++) {
        HrtfFilter a = {0}, b; float x[NUM_SAMPLES_PER_FRAME][2], y[NUM_SAMPLES_PER_FRAME][2];
        a.itd_cur = a.itd_tar = special & 1 ? 42 : -42;
        for (unsigned n = 0; n < NUM_SAMPLES_PER_FRAME; n++) x[n][0] = x[n][1] = special & 2 ? 0x1p-140f : -0.0f;
        memcpy(y, x, sizeof x); b = a; hrtf_filter_process(&a, x, x); hrtf_filter_process_fast(&b, y, y);
        assert(!memcmp(x, y, sizeof x)); assert(!memcmp(&a, &b, sizeof a));
    }
    /* Time the same 64-voice workload both ways; checksums prevent elision. */
    HrtfFilter filters[64] = {{0}}; float io[NUM_SAMPLES_PER_FRAME][2]; uint64_t ns[2]; volatile float checksum = 0;
    for (unsigned mode = 0; mode < 2; mode++) {
        memset(filters, 0, sizeof filters);
        for (unsigned v = 0; v < 64; v++) {
            filters[v].itd_tar = (int)v - 31.5f;
            for (unsigned c = 0; c < 2; c++) for (unsigned k = 0; k < HRTF_NUM_TAPS; k++) filters[v].ch[c].hrir_coeff_tar[k] = (k + c + 1) / 1024.0f;
        }
        uint64_t started = now();
        for (unsigned frame = 0; frame < 1000; frame++) for (unsigned v = 0; v < 64; v++) {
            for (unsigned n = 0; n < NUM_SAMPLES_PER_FRAME; n++) io[n][0] = io[n][1] = (float)(n + frame) / 2048;
            if (mode) hrtf_filter_process_fast(filters + v, io, io); else hrtf_filter_process(filters + v, io, io);
            checksum += io[0][0];
        }
        ns[mode] = now() - started;
    }
    printf("PASS exact HRTF PCM/state: %u frames, all %d ring positions x85 signed delays, changing targets, aliasing/zeros/subnormals\n", cases, HRTF_BUFLEN);
    printf("HRTF microbench original_ms=%.3f fast_ms=%.3f ratio=%.3f checksum=%g\n", ns[0] / 1e6, ns[1] / 1e6, (double)ns[1] / ns[0], checksum);
}
