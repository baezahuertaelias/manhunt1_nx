/* P7: the direct vertex ring under RECOMP_GL_THREAD=1, on a real GL driver
 * (llvmpipe). Many batches in several vertex layouts -- interleaved direct
 * attributes, a padded stride, CPU-converted attributes, all converted, and
 * pre-transformed draws that must keep the queue path -- through a small
 * ring, so it wraps and fills. The guest vertex bytes are overwritten right
 * after each q_draw, as a title reusing its buffer would. The runner compares
 * the output with MANHUNT_GL_DIRECT_RING=0 (the old two-copy path).
 *
 * Usage: test-direct-ring strict|gpu <out> [ring KB]
 *   strict: every frame's surface RAM after q_sync
 *   gpu:    a hash of every presented frame (executor runs up to two frames
 *           ahead of the GL thread, so the ring is shared across threads) */
#define main renderer_real_main
#include "test_real_gl.c"
#undef main
#include <time.h>

static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }
static void put_f(uint8_t *p, float v) { memcpy(p, &v, 4); }

int main(int argc, char **argv)
{
    assert(argc >= 3);
    int gpu = !strcmp(argv[1], "gpu");
    FILE *out = fopen(argv[2], "wb"); assert(out);
    setenv("RECOMP_PROG_CACHE", "0", 1); setenv("RECOMP_GL_SCALE", "1", 1);
    setenv("MANHUNT_GL_SYNC", gpu ? "gpu" : "strict", 1);
    setenv("MANHUNT_GL_PERSISTENT", "1", 1); setenv("MANHUNT_PERF_LOG", "2", 1);
    unsetenv("RECOMP_GL_THREAD");
    s_xr.cap = (size_t)(argc > 3 ? atoi(argv[3]) : 64) << 10;
    swap_hash_on = gpu;
    queued_mode = s_q_on = 1; assert(q_start());

    Nv2aSurface sf = surface(64, 64, 256);
    Nv2aRenderState rs = {0};
    static float constants[192][4], attrs[96 * 64];
    static uint32_t program[136][4], indices[96];
    /* MOV oPos, v0; MOV oD0, v3 (FINAL). */
    program[0][1] = (1u << 21) | 0x1b; program[0][2] = 2u << 26; program[0][3] = (15u << 12) | (1u << 11);
    program[1][1] = (1u << 21) | (3u << 9) | 0x1b; program[1][2] = 2u << 26;
    program[1][3] = (15u << 12) | (1u << 11) | (3u << 3) | 1;
    regs[0x288 / 4] = 4; regs[0x28c / 4] = 0x14 << 8;
    const unsigned frames = 120, per_frame = 24;
    uint8_t *vb = guest + 0x380000;
    unsigned nonblank = 0;

    for (unsigned f = 0; f < frames; f++) {
        q_clear(&sf, &rs, 0xf0, 0xff000000u | f, 0);
        for (unsigned d = 0; d < per_frame; d++) {
            unsigned layout = ((f * per_frame + d) / 3) % 5;
            unsigned nv = 3 * (1 + rnd(30)), v;
            Nv2aRawBatch b = {0};
            b.surface = sf; b.regs = regs; b.prim = 5; b.vertex_count = b.index_count = nv;
            b.indices = indices; b.attrs = attrs;
            b.vp_program = (const uint32_t (*)[4])program; b.vp_slots = 2; b.vp_consts = constants;
            b.vp_const_count = 192; b.vp_prog_gen = b.vp_const_gen = b.attr_const_gen = 1;
            b.xform = layout == 3 ? 0 : 2;
            b.attr_present = 9;
            for (v = 0; v < nv; v++) indices[v] = v;
            for (v = 0; v + 1 < nv; v++) {               /* shuffle within each triangle order */
                uint32_t j = v + rnd(nv - v), t = indices[v]; indices[v] = indices[j]; indices[j] = t;
            }
            for (v = 0; v < nv; v++) {
                float x = (float)rnd(65), y = (float)rnd(65);
                uint32_t c = 0xff000000u | rnd(0x1000000);
                float *a = attrs + v * 64;
                switch (layout) {
                case 0: case 3:                           /* pos3f + D3DCOLOR, stride 16 */
                    put_f(vb + v * 16, x); put_f(vb + v * 16 + 4, y); put_f(vb + v * 16 + 8, 0);
                    memcpy(vb + v * 16 + 12, &c, 4);
                    break;
                case 1:                                   /* D3DCOLOR, gap, pos3f, gap: stride 24 */
                    memcpy(vb + v * 24, &c, 4);
                    put_f(vb + v * 24 + 8, x); put_f(vb + v * 24 + 12, y); put_f(vb + v * 24 + 16, 0);
                    break;
                case 2:                                   /* pos3f direct, colour converted */
                    put_f(vb + v * 12, x); put_f(vb + v * 12 + 4, y); put_f(vb + v * 12 + 8, 0);
                    a[12] = (float)((c >> 16) & 255) / 255.0f; a[13] = (float)((c >> 8) & 255) / 255.0f;
                    a[14] = (float)(c & 255) / 255.0f; a[15] = 1;
                    break;
                default:                                  /* all converted */
                    a[0] = x; a[1] = y; a[2] = 0; a[3] = 1;
                    a[12] = (float)((c >> 16) & 255) / 255.0f; a[13] = (float)((c >> 8) & 255) / 255.0f;
                    a[14] = (float)(c & 255) / 255.0f; a[15] = 1;
                    break;
                }
            }
            switch (layout) {
            case 0: case 3:
                b.attr_direct = 9;
                b.direct[0].ptr = vb; b.direct[0].type = 2; b.direct[0].size = 3; b.direct[0].stride = 16;
                b.direct[3].ptr = vb + 12; b.direct[3].type = 0; b.direct[3].size = 4; b.direct[3].stride = 16;
                break;
            case 1:
                b.attr_direct = 9;
                b.direct[3].ptr = vb; b.direct[3].type = 0; b.direct[3].size = 4; b.direct[3].stride = 24;
                b.direct[0].ptr = vb + 8; b.direct[0].type = 2; b.direct[0].size = 3; b.direct[0].stride = 24;
                break;
            case 2:
                b.attr_direct = 1;
                b.direct[0].ptr = vb; b.direct[0].type = 2; b.direct[0].size = 3; b.direct[0].stride = 12;
                break;
            default:
                b.attr_direct = 0;
                break;
            }
            q_draw(&b);
            memset(vb, 0xff, 96 * 24);                    /* the title reuses its buffer */
            memset(attrs, 0xff, sizeof attrs);
        }
        q_flip();
        if (!gpu) {
            q_sync(1);
            const uint32_t *c = color_ram(&sf);
            for (unsigned i = 0; i < 64 * 64; i++) if ((c[i] & 0xffffff) > 0xff) { nonblank++; break; }
            assert(fwrite(c, 1, 64 * 64 * 4, out) == 64 * 64 * 4);
        }
    }
    if (gpu) {
        struct timespec ts = { 0, 1000000 };
        unsigned spins = 0;
        while (__atomic_load_n(&swap_count, __ATOMIC_ACQUIRE) < frames && spins++ < 20000) nanosleep(&ts, NULL);
        assert(swap_count == frames);
        for (unsigned i = 0; i < frames; i++) {
            if (i && swap_hashes[i] != swap_hashes[i - 1]) nonblank++;
            assert(fwrite(&swap_hashes[i], 8, 1, out) == 1);
        }
    }
    assert(!fclose(out));
    assert(nonblank >= frames / 2);
    Nv2aGlPerf p; nv2a_gl_perf_read(&p);
    printf("direct ring %s: %s, cap %zu KB: %u frames, %llu draws, ring draws %llu (%llu KB), full %llu, wraps %llu, kept %llu\n",
           argv[1], s_xr.ready ? "on" : "off", s_xr.cap >> 10, frames, (unsigned long long)p.draws,
           (unsigned long long)p.ring_draws, (unsigned long long)(p.ring_bytes >> 10),
           (unsigned long long)p.ring_full, (unsigned long long)p.ring_wraps,
           (unsigned long long)p.kept_attr_draws);
    const char *e = getenv("MANHUNT_GL_DIRECT_RING");
    if (e && !strcmp(e, "0")) {
        assert(!s_xr.ready && !p.ring_draws);
    } else {
        assert(s_xr.ready && p.ring_draws > 0 && p.ring_wraps > 0);
        /* Pre-transformed draws (a fifth of them) never take the ring. */
        assert(p.ring_draws + p.ring_full <= p.draws - frames * per_frame / 5 + 3);
    }
    fflush(stdout); _Exit(0);
}
