/* Integration proof for actual production program assembly, UBO upload,
 * program switches, and queued constant snapshots. Synthetic frames only. */
#define main renderer_real_main
#include "test_real_gl.c"
#undef main
static void regf(unsigned method, float value) { memcpy(regs + method / 4, &value, 4); }
static void constant_program(uint32_t program[136][4], unsigned row)
{
    memset(program, 0, 136 * 16);
    /* MOV oPos, v0; MOV oD0, c[row], FINAL. Identity source swizzles. */
    program[0][1] = (1u << 21) | 0x1b;
    program[0][2] = 2u << 26;
    program[0][3] = (15u << 12) | (1u << 11);
    program[1][1] = (1u << 21) | (row << 13) | 0x1b;
    program[1][2] = 3u << 26;
    program[1][3] = (15u << 12) | (1u << 11) | (3u << 3) | 1;
}
static uint32_t rgba_pixel(const float color[4])
{
    uint32_t v[4]; for (int i = 0; i < 4; i++) v[i] = (unsigned)(color[i] * 255.0f + .5f);
    return (v[3] << 24) | (v[0] << 16) | (v[1] << 8) | v[2];
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *out = fopen(argv[2], "wb"); assert(out);
    setenv("RECOMP_PROG_CACHE", "0", 1); setenv("MANHUNT_GL_SYNC", "strict", 1);
    setenv("MANHUNT_GL_PERSISTENT", "0", 1); setenv("MANHUNT_GL_CMP", "0", 1);
    setenv("MANHUNT_GL_STATE_CACHE", "0", 1); setenv("RECOMP_GL_SCALE", "1", 1);
    unsetenv("RECOMP_GL_THREAD");
    queued_mode = !strcmp(argv[1], "queued");
    if (queued_mode) { s_q_on = 1; assert(q_start()); } else assert(ready());
    Nv2aSurface sf = surface(8, 8, 32);
    float attrs[3 * 64] = {0}, constants[192][4]; uint32_t programs[2][136][4];
    uint32_t indices[3] = {0, 1, 2};
    constant_program(programs[0], 0); constant_program(programs[1], 191);
    const float xy[6] = {0, 0, 8, 0, 0, 8};
    for (unsigned v = 0; v < 3; v++) {
        attrs[v * 64] = xy[v * 2]; attrs[v * 64 + 1] = xy[v * 2 + 1]; attrs[v * 64 + 3] = 1;
        attrs[v * 64 + 10] = 1; attrs[v * 64 + 15] = 1;
    }
    regs[0x288 / 4] = 4; regs[0x28c / 4] = 0x14 << 8;
    Nv2aRawBatch b = {0}; b.surface = sf; b.regs = regs; b.prim = 5;
    b.vertex_count = b.index_count = 3; b.attrs = attrs; b.indices = indices;
    b.attr_present = (1u << 0) | (1u << 2) | (1u << 3);
    b.vp_consts = constants; b.vp_const_count = 192; b.vp_slots = 2;
    b.vp_const_gen = b.vp_prog_gen = b.attr_const_gen = 1;
    unsigned wraps = 0; GLintptr previous_off = 0;
    for (unsigned draw = 0; draw < 6000; draw++) {
        /* Hold complete constant generations across different programs, and
         * alternate fixed lighting and unlit batches between updates. */
        unsigned phase = draw % 8;
        if (phase == 0) {
            b.vp_const_gen++;
            for (unsigned row = 0; row < 192; row++) {
                constants[row][0] = ((draw / 8 + row * 3) % 16) / 15.0f;
                constants[row][1] = ((draw / 8 + row * 5 + 1) % 16) / 15.0f;
                constants[row][2] = ((draw / 8 + row * 7 + 2) % 16) / 15.0f;
                constants[row][3] = 1;
            }
        }
        b.vp_program = programs[phase & 1]; b.vp_prog_gen++;
        b.xform = phase < 4 ? 2 : 0;
        regs[0x1e60 / 4] = ((draw / 8) % 4) << 12; /* different production cache keys */
        float expected[4];
        if (phase < 4) memcpy(expected, constants[(phase & 1) ? 191 : 0], sizeof expected);
        else {
            float color[4] = {((draw / 8 + phase) % 16) / 15.0f, ((draw / 8 + phase * 3) % 16) / 15.0f,
                              ((draw / 8 + phase * 5) % 16) / 15.0f, 1};
            b.lighting = phase & 1 ? 0 : 1;
            for (unsigned v = 0; v < 3; v++) memcpy(attrs + v * 64 + 12, color, sizeof color);
            regf(0x0480, 1); regf(0x0494, 1); regf(0x04a8, 1);
            regs[0x03bc / 4] = phase == 6 ? 1 : 0;
            /* Phase 4: scene ambient. Phase 6: directional diffuse, testing
             * the production normal transform and light loop as well. */
            for (unsigned k = 0; k < 3; k++) {
                regf(0x0a10 + k * 4, phase == 6 ? 0 : color[k]);
                regf(0x100c + k * 4, color[k]);
            }
            regf(0x103c, 1); regf(0x03b4, 1);
            memcpy(expected, color, sizeof expected);
            if (b.lighting && ambient_level() >= 0.0f)
                for (unsigned k = 0; k < 3; k++) {
                    float value = ambient_level() + (phase == 6 ? color[k] : 0.0f);
                    expected[k] = value > 1.0f ? 1.0f : value;
                }
        }
        if (queued_mode) q_draw(&b); else gl_draw_raw(&b);
        complete();
        uint32_t actual = color_ram(&sf)[9], want = rgba_pixel(expected);
        if (actual != want) {
            fprintf(stderr, "mismatch draw %u phase %u UBO %d expected %08x actual %08x\n", draw, phase, gl_ubo(), want, actual);
            return 1;
        }
        assert(fwrite(color_ram(&sf), 1, 8 * 8 * 4, out) == 8 * 8 * 4);
        if (s_ubo.off < previous_off) wraps++;
        previous_off = s_ubo.off;
    }
    assert(!fclose(out));
    /* UBO draw phases above produce natural storage exhaustion; don't rely
     * on tests mutating the production allocator's private cursor. */
    if (gl_ubo()) assert(wraps >= 1);
    printf("PASS production GL UBO integration: 6000 draws, %u programs, %u natural UBO wraps, %s, UBO %d\n",
           s_prog_live, wraps, queued_mode ? "queued" : "direct", gl_ubo());
    fflush(stdout); _Exit(0);
}
