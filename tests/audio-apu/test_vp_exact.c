/* The voice processor before and after a change that must not change its
 * output (patch 0037: register block, SGE and sample data resolved once per
 * voice or page, the decoded ADPCM block kept across calls, zero-gain mix
 * bins skipped).
 *
 * run-vp-exact.sh builds this file twice, once against each apu_vp.c, and
 * compares the two dumps byte for byte: every frame's mixbins, then the guest
 * RAM (voice registers, notifiers), the APU registers, the stream list state
 * and each voice's filter state.
 *
 * The fake physical memory is deliberately not linear: physical page P lives
 * at host page P ^ 5, and some pages change host region partway through
 * (split_at: SPLIT in the voice array, a third of the sample-data pages).
 * Code that stepped a host pointer past what mcpx_apu_phys_span vouched for
 * would read other bytes than mcpx_apu_phys does. The voice array straddles
 * both a page boundary (voice 7) and SPLIT (voice 22), so those two voices
 * take the per-field fallback.
 *
 * Voices: PCM 8/16/24/32-bit, mono and stereo, looping and ending; ADPCM
 * from SGE buffers (mono and stereo, one to three blocks per sample block)
 * and from stream segments; a PCM stream; a voice in its envelope delay (zero
 * gain on every bin); a multipass voice. SGE entries are not page aligned,
 * so a linear page spans two physical pages. During the run the ADPCM data
 * under the play cursors is rewritten, a voice restarts on another buffer,
 * one changes from mono to stereo over the same bytes, a voice pauses, the
 * voice array moves, and (x86) FTZ/DAZ are on for a while. */
#define _POSIX_C_SOURCE 200809L
#define MANHUNT_APU_MATH_CACHE_TEST 1
#ifndef APU_VP_SOURCE
#error "build with -DAPU_VP_SOURCE=\"path/to/apu_vp.c\""
#endif
#include APU_VP_SOURCE
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

MCPXAPUState *g_state;
struct McpxApuDebug g_dbg, g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4];
uint8_t *g_apu_ram_ptr;
ApuIrqStats apu_irq_stats;
int apu_env_on(const char *name) { (void)name; return 0; }
const char *mcpx_apu_phys_class(uint64_t at) { (void)at; return "fixture"; }

#define RAM_SIZE  0x400000u
#define SPLIT     0x407A0u          /* a mapping change inside a page */
/* Other pages that change mapping partway, at split_at(page). */
static uint32_t split_at(uint32_t page)
{
    if (page == (SPLIT >> 12)) return SPLIT & 0xFFFu;
    if (page >= 0x100 && page < 0x300 && page % 3 == 0) return 0x100u + (page * 0x2C4u) % 0xE00u;
    return 0;
}
static uint8_t ram[2 * RAM_SIZE + 0x2000];
static uint64_t n_phys, n_span;

static size_t host_off(uint32_t a)
{
    uint32_t page = a >> 12, s = split_at(page);
    assert(a < RAM_SIZE);
    if (s && (a & 0xFFFu) >= s)
        return RAM_SIZE + ((size_t)page << 12) + (a & 0xFFFu);
    return ((size_t)(page ^ 5u) << 12) | (a & 0xFFFu);
}

uint8_t *mcpx_apu_phys(uint64_t at)
{
    n_phys++;
    return ram + host_off((uint32_t)at);
}

uint8_t *mcpx_apu_phys_span(uint64_t at, uint32_t *avail)
{
    uint32_t a = (uint32_t)at, end = (a | 0xFFFu) + 1u;
    uint32_t s = split_at(a >> 12);
    n_span++;
    if (s && (a & 0xFFFu) < s) end = (a & ~0xFFFu) + s;
    *avail = end - a;
    return ram + host_off(a);
}

static void wr8(uint32_t a, uint8_t v) { ram[host_off(a)] = v; }
static void wr32(uint32_t a, uint32_t v)
{
    for (int i = 0; i < 4; i++) wr8(a + i, (uint8_t)(v >> (8 * i)));
}
static uint32_t rd32(uint32_t a)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)ram[host_off(a + i)] << (8 * i);
    return v;
}

static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

#define SGE_BASE   0x01000u
#define SSL_BASE   0x03000u
#define VOICE_A    0x3FC40u         /* voice 7 crosses 0x40000, 22 crosses SPLIT */
#define VOICE_B    0x52040u         /* where the array moves to */
#define FEN_BASE   0x300000u
#define DATA_BASE  0x100000u        /* SGE-mapped data, 256 linear pages */
#define STREAM_BASE 0x220000u       /* stream segments */

static MCPXAPUState d;
static uint32_t s_vbase = VOICE_A;

static void setreg(unsigned v, unsigned reg, uint32_t value) { wr32(s_vbase + v * NV_PAVS_SIZE + reg, value); }
static uint32_t getreg(unsigned v, unsigned reg) { return rd32(s_vbase + v * NV_PAVS_SIZE + reg); }

/* Physical address of linear buffer address lin, as get_data_ptr makes it. */
static uint32_t lin_phys(uint32_t lin)
{
    return rd32(SGE_BASE + (lin / 4096) * 8) + lin % 4096;
}

static void rewrite_linear(uint32_t lin, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) wr8(lin_phys(lin + i), (uint8_t)rnd());
}

enum { FMT_U8 = 0, FMT_S16 = 1, FMT_S24 = 2, FMT_S32 = 3 };
enum { CS_1 = 0, CS_2 = 1, CS_ADPCM = 2, CS_4 = 3 };

static uint32_t fmt(unsigned ss, unsigned cs, unsigned spb, int stereo, int loop,
                    int stream)
{
    return ((uint32_t)cs << 30) | ((uint32_t)ss << 28) | ((spb - 1u) << 16) |
           (stereo ? NV_PAVS_VOICE_CFG_FMT_STEREO : 0) |
           (loop ? NV_PAVS_VOICE_CFG_FMT_LOOP : 0) |
           (stream ? (NV_PAVS_VOICE_CFG_FMT_DATA_TYPE | NV_PAVS_VOICE_CFG_FMT_PERSIST) : 0);
}

static unsigned s_list[32], s_nlist;

/* BINS(v0, ..., v5); v6 and v7 (derived) go to the format register. */
#define BINS(a, b, c, e, f, g) ((uint64_t)(a) | ((uint64_t)(b) << 5) | ((uint64_t)(c) << 10) | \
    ((uint64_t)(e) << 16) | ((uint64_t)(f) << 21) | ((uint64_t)(g) << 26) | \
    ((uint64_t)(((a) + 7) % 32) << 32) | ((uint64_t)(((b) + 9) % 32) << 37))

static void voice(unsigned v, uint32_t f, uint32_t ba, uint32_t ebo, uint32_t lbo,
                  int pitch, uint64_t bins, uint32_t vola, uint32_t volb,
                  uint32_t volc)
{
    setreg(v, NV_PAVS_VOICE_CFG_FMT, f | (uint32_t)((bins >> 32) & 0x3FFu));
    setreg(v, NV_PAVS_VOICE_CFG_VBIN, (uint32_t)bins);
    setreg(v, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
    setreg(v, NV_PAVS_VOICE_CUR_PSL_START, ba);
    setreg(v, NV_PAVS_VOICE_CUR_PSH_SAMPLE, lbo);
    setreg(v, NV_PAVS_VOICE_PAR_NEXT, ebo);
    setreg(v, NV_PAVS_VOICE_PAR_OFFSET, 0);
    setreg(v, NV_PAVS_VOICE_TAR_VOLA, vola);
    setreg(v, NV_PAVS_VOICE_TAR_VOLB, volb);
    setreg(v, NV_PAVS_VOICE_TAR_VOLC, volc);
    setreg(v, NV_PAVS_VOICE_TAR_PITCH_LINK, (uint32_t)(uint16_t)pitch << 16);
    s_list[s_nlist++] = v;
}

static void link_list(void)
{
    d.regs[NV_PAPU_TVL2D] = s_list[0];
    for (unsigned i = 0; i < s_nlist; i++) {
        uint32_t r = getreg(s_list[i], NV_PAVS_VOICE_TAR_PITCH_LINK) & 0xFFFF0000u;
        setreg(s_list[i], NV_PAVS_VOICE_TAR_PITCH_LINK,
               r | (i + 1 < s_nlist ? s_list[i + 1] : 0xFFFFu));
    }
}

/* SSL page p: a segment of len samples at phys, container cs. */
static void ssl_page(unsigned p, uint32_t phys, unsigned len, unsigned cs)
{
    wr32(SSL_BASE + p * 8, phys);
    wr32(SSL_BASE + p * 8 + 4, (len & 0xFFFFu) | ((uint32_t)cs << 16));
}

static void dump_state(FILE *out)
{
    assert(fwrite(ram, 1, sizeof ram, out) == sizeof ram);
    assert(fwrite(d.regs, 1, sizeof d.regs, out) == sizeof d.regs);
    assert(fwrite(&d.set_irq, 1, sizeof d.set_irq, out) == sizeof d.set_irq);
    assert(fwrite(d.vp.ssl, 1, sizeof d.vp.ssl, out) == sizeof d.vp.ssl);
    for (unsigned v = 0; v < MCPX_HW_MAX_VOICES; v++) {
        MCPXAPUVoiceFilter *f = &d.vp.filters[v];
        sv_filter svf[2];
        memcpy(svf, f->svf, sizeof svf);
        svf[0].op = svf[1].op = NULL;
        assert(fwrite(f->rs_last, 1, sizeof f->rs_last, out) == sizeof f->rs_last);
        assert(fwrite(f->rs_next, 1, sizeof f->rs_next, out) == sizeof f->rs_next);
        assert(fwrite(&f->rs_pos, 1, sizeof f->rs_pos, out) == sizeof f->rs_pos);
        assert(fwrite(&f->rs_have, 1, sizeof f->rs_have, out) == sizeof f->rs_have);
        assert(fwrite(svf, 1, sizeof svf, out) == sizeof svf);
        assert(fwrite(&f->hrtf, 1, sizeof f->hrtf, out) == sizeof f->hrtf);
        assert(fwrite(f->gain_prev, 1, sizeof f->gain_prev, out) == sizeof f->gain_prev);
        assert(fwrite(&f->gain_valid, 1, sizeof f->gain_valid, out) == sizeof f->gain_valid);
    }
}

int main(int argc, char **argv)
{
    unsigned frames = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    FILE *out = argc > 2 ? fopen(argv[2], "wb") : NULL;
    uint64_t zero_bins = 0, total_bins = 0;

    if (argc > 2 && !out) { perror(argv[2]); return 2; }
    g_apu_ram_ptr = ram;
    for (size_t i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)rnd();
    mcpx_apu_vp_reset(&d);
    for (uint32_t a = VOICE_A; a < VOICE_A + 256 * NV_PAVS_SIZE; a++) wr8(a, 0);

    d.regs[NV_PAPU_VPVADDR] = s_vbase;
    d.regs[NV_PAPU_VPSGEADDR] = SGE_BASE;
    d.regs[NV_PAPU_VPSSLADDR] = SSL_BASE;
    d.regs[NV_PAPU_FENADDR] = FEN_BASE;
    d.regs[NV_PAPU_TVL3D] = d.regs[NV_PAPU_TVLMP] = 0xFFFF;
    for (unsigned i = 0; i < 4; i++) d.vp.hrtf_submix[i] = (uint8_t)(i + 8);
    for (unsigned b = 0; b < NUM_MIXBINS; b++) d.vp.submix_headroom[b] = (uint8_t)(b % 3);
    d.vp.hrtf_headroom = 1;

    /* SGE: linear page i at a scattered physical page, some not aligned. */
    for (unsigned i = 0; i < 256; i++) {
        uint32_t page = (i * 97u) % 256u;
        wr32(SGE_BASE + i * 8, DATA_BASE + page * 0x1000u + (i % 3u) * 0x123u);
        wr32(SGE_BASE + i * 8 + 4, 0);
    }

    /* vol: 0xFFF is silence. */
    const uint32_t loud = 0x20001000u, half = 0x0FF00400u, mute = 0xFFFFFFFFu;

    voice(3,   fmt(FMT_S16, CS_2, 1, 0, 1, 0), 0x01234, 20000, 100,  -700, BINS(1, 2, 3, 4, 5, 6), loud, mute, mute);
    voice(7,   fmt(FMT_U8,  CS_1, 2, 1, 1, 0), 0x20001, 30000, 7,     900, BINS(0, 1, 2, 3, 4, 5), half, 0x00000000u, mute);
    voice(22,  fmt(FMT_S16, CS_2, 2, 1, 0, 0), 0x30002, 3000,  0,     300, BINS(2, 3, 4, 5, 6, 7), loud, mute, 0x00000000u);
    voice(70,  fmt(FMT_S24, CS_4, 1, 0, 1, 0), 0x40000, 9000,  50,  -1500, BINS(0, 1, 2, 3, 4, 5), loud, loud, mute);
    voice(71,  fmt(FMT_S32, CS_4, 2, 1, 1, 0), 0x50000, 7000,  10,   2500, BINS(6, 7, 8, 9, 10, 11), half, mute, mute);
    voice(80,  fmt(FMT_S16, CS_ADPCM, 1, 0, 1, 0), 0x60010, 64 * 300 - 1, 64 * 5, -300, BINS(0, 1, 2, 3, 4, 5), loud, mute, mute);
    voice(81,  fmt(FMT_S16, CS_ADPCM, 2, 1, 1, 0), 0x70000, 64 * 200 - 1, 64 * 3, 1200, BINS(12, 13, 0, 1, 2, 3), loud, half, mute);
    voice(82,  fmt(FMT_S16, CS_ADPCM, 2, 0, 1, 0), 0x80008, 64 * 150 - 1, 0,      0,    BINS(0, 1, 2, 3, 4, 5), half, mute, mute);
    voice(83,  fmt(FMT_S16, CS_ADPCM, 3, 0, 1, 0), 0x90004, 64 * 100 - 1, 64,    -2048, BINS(0, 1, 2, 3, 4, 5), loud, mute, mute);
    voice(10,  fmt(FMT_S16, CS_ADPCM, 1, 0, 1, 0), 0xA0000, 64 * 250 - 1, 0,      600, BINS(0, 1, 2, 3, 14, 15), loud, loud, mute);
    voice(90,  fmt(FMT_S16, CS_ADPCM, 1, 0, 0, 1), 0, 0, 0,                         -100, BINS(0, 1, 2, 3, 4, 5), loud, mute, mute);
    voice(91,  fmt(FMT_S16, CS_ADPCM, 2, 1, 0, 1), 0, 0, 0,                          400, BINS(0, 1, 2, 3, 4, 5), loud, mute, mute);
    voice(92,  fmt(FMT_S16, CS_2, 1, 0, 0, 1), 0, 0, 0,                             -900, BINS(0, 1, 2, 3, 4, 5), half, mute, mute);
    voice(100, fmt(FMT_S16, CS_2, 1, 0, 1, 0), 0xB0000, 9000, 0,                       0, BINS(0, 1, 2, 3, 4, 5), loud, loud, loud);
    voice(101, NV_PAVS_VOICE_CFG_FMT_MULTIPASS | (5u << 16) | NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX,
          0, 0, 0, 0, BINS(16, 17, 18, 19, 20, 21), loud, half, mute);
    link_list();

    /* Voice 100 sits in its amplitude envelope's delay: gain 0 everywhere. */
    setreg(100, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE |
           ((uint32_t)NV_PAVS_VOICE_PAR_STATE_EFCUR_DELAY << 28));
    setreg(100, NV_PAVS_VOICE_CUR_ECNT, 0xFFFFu);            /* EACOUNT */
    setreg(100, NV_PAVS_VOICE_CFG_MISC, 1u << 16);           /* low-pass */
    setreg(100, NV_PAVS_VOICE_TAR_FCA, (0x4000u << 16) | (uint16_t)-4096);
    setreg(3, NV_PAVS_VOICE_CFG_HRTF_TARGET, 1);
    setreg(10, NV_PAVS_VOICE_CFG_HRTF_TARGET, 2);
    setreg(7, NV_PAVS_VOICE_CFG_HRTF_TARGET, 3);
    setreg(22, NV_PAVS_VOICE_CFG_HRTF_TARGET, 4);
    for (unsigned v = 0; v < 64; v++) {
        float coeff[2][HRTF_NUM_TAPS];
        for (unsigned ch = 0; ch < 2; ch++)
            for (unsigned k = 0; k < HRTF_NUM_TAPS; k++)
                coeff[ch][k] = ((int)((k * 7 + ch * 13 + v) % 31) - 15) / 31.0f;
        hrtf_filter_set_target_params(&d.vp.filters[v].hrtf, coeff, (float)v - 7.25f);
    }

    /* Streams: voice 90 pages 0-2 / 3-4, voice 91 pages 5-6 / 7, voice 92
     * pages 8 / 9-10. ADPCM segments are whole 64-sample blocks. */
    for (unsigned p = 0; p < 11; p++) {
        uint32_t phys = STREAM_BASE + p * 0x2345u + 0x10u;
        if (p < 5) ssl_page(p, phys, 64 * (3 + p), CS_ADPCM);
        else if (p < 8) ssl_page(p, phys, 64 * (2 + p % 3), CS_ADPCM);
        else ssl_page(p, phys, 700 + 37 * p, CS_2);
    }
    d.vp.ssl[90].base[0] = 0; d.vp.ssl[90].count[0] = 3;
    d.vp.ssl[90].base[1] = 3; d.vp.ssl[90].count[1] = 2;
    d.vp.ssl[91].base[0] = 5; d.vp.ssl[91].count[0] = 2;
    d.vp.ssl[91].base[1] = 7; d.vp.ssl[91].count[1] = 1;
    d.vp.ssl[92].base[0] = 8; d.vp.ssl[92].count[0] = 1;
    d.vp.ssl[92].base[1] = 9; d.vp.ssl[92].count[1] = 2;

    for (unsigned frame = 0; frame < frames; frame++) {
        float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
        memset(mixbins, 0, sizeof mixbins);

        /* New ADPCM data under and just ahead of the play cursors. */
        if (frame % 7 == 3) {
            static const unsigned adpcm_v[] = { 80, 81, 82, 10 };
            for (unsigned i = 0; i < 4; i++) {
                unsigned v = adpcm_v[i];
                uint32_t f = getreg(v, NV_PAVS_VOICE_CFG_FMT);
                uint32_t bs = 36u * (((f >> 16) & 0x1Fu) + 1u);
                uint32_t cbo = getreg(v, NV_PAVS_VOICE_PAR_OFFSET) & 0xFFFFFFu;
                uint32_t ba = getreg(v, NV_PAVS_VOICE_CUR_PSL_START) & 0xFFFFFFu;
                uint32_t at = ba + (cbo / 64) * bs + (frame % 3) * 5;
                rewrite_linear(at, (frame % 5) + 1);
            }
            for (unsigned k = 0; k < 6; k++)
                wr8(STREAM_BASE + (rnd() % (11 * 0x2345u)), (uint8_t)rnd());
        }
        /* Same bytes written again: content unchanged. */
        if (frame % 11 == 5) {
            uint32_t cbo = getreg(80, NV_PAVS_VOICE_PAR_OFFSET) & 0xFFFFFFu;
            uint32_t at = lin_phys(0x60010 + (cbo / 64) * 36);
            wr8(at, ram[host_off(at)]);
        }
        if (frame == 300) setreg(3, NV_PAVS_VOICE_PAR_STATE, getreg(3, NV_PAVS_VOICE_PAR_STATE) | NV_PAVS_VOICE_PAR_STATE_PAUSED);
        if (frame == 320) setreg(3, NV_PAVS_VOICE_PAR_STATE, getreg(3, NV_PAVS_VOICE_PAR_STATE) & ~NV_PAVS_VOICE_PAR_STATE_PAUSED);
        if (frame == 500) {           /* restart on another buffer */
            setreg(80, NV_PAVS_VOICE_CUR_PSL_START, 0xC0004);
            setreg(80, NV_PAVS_VOICE_PAR_OFFSET, 17);
        }
        if (frame == 650) {           /* the same position again */
            setreg(81, NV_PAVS_VOICE_PAR_OFFSET, getreg(81, NV_PAVS_VOICE_PAR_OFFSET) & 0xFF000000u);
        }
        if (frame == 900) {           /* mono to stereo over the same bytes */
            setreg(82, NV_PAVS_VOICE_CFG_FMT, getreg(82, NV_PAVS_VOICE_CFG_FMT) | NV_PAVS_VOICE_CFG_FMT_STEREO);
        }
        if (frame == 1100) {          /* a copy of the same data elsewhere */
            uint32_t ba = getreg(81, NV_PAVS_VOICE_CUR_PSL_START) & 0xFFFFFFu;
            for (uint32_t i = 0; i < 72u * 200u; i++)
                wr8(lin_phys(0xD0000 + i), ram[host_off(lin_phys(ba + i))]);
            setreg(81, NV_PAVS_VOICE_CUR_PSL_START, 0xD0000);
        }
        if (frame == 1300) {          /* the voice array moves */
            for (uint32_t i = 0; i < 256 * NV_PAVS_SIZE; i++)
                wr8(VOICE_B + i, ram[host_off(VOICE_A + i)]);
            s_vbase = VOICE_B;
            d.regs[NV_PAPU_VPVADDR] = VOICE_B;
        }
        if (frame == 1800) {          /* restart voice 22, which ended */
            setreg(22, NV_PAVS_VOICE_PAR_OFFSET, 0);
            setreg(22, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
        }
        /* Bins ramping down to zero and up from it. */
        if (frame % 13 == 0)
            setreg(70, NV_PAVS_VOICE_TAR_VOLB, (frame / 13) % 2 ? mute : loud);
        if (frame % 17 == 0)
            setreg(81, NV_PAVS_VOICE_TAR_VOLC, (frame / 17) % 2 ? mute : half);
        if (frame % 41 == 0) {
            unsigned v = s_list[(frame / 41) % s_nlist];
            setreg(v, NV_PAVS_VOICE_TAR_VOLA, ((frame * 11) & 4095) << 4 | ((frame * 7) & 4095) << 20);
        }
#if defined(__x86_64__) || defined(__i386__)
        if (frame == 1500) _mm_setcsr(_mm_getcsr() | 0x8040u);
        if (frame == 1700) _mm_setcsr(_mm_getcsr() & ~0x8040u);
#endif
        mcpx_apu_vp_frame(&d, mixbins);
        if (out) assert(fwrite(mixbins, 1, sizeof mixbins, out) == sizeof mixbins);
        for (unsigned i = 0; i < s_nlist; i++) {
            MCPXAPUVoiceFilter *f = &d.vp.filters[s_list[i]];
            for (int b = 0; b < 8; b++) { total_bins++; zero_bins += f->gain_prev[b] == 0.0f; }
        }
    }
    if (out) {
        dump_state(out);
        assert(!fclose(out));
    }
    printf("vp-exact frames=%u voices=%u phys=%llu span=%llu zero-gain bins=%llu/%llu\n",
           frames, s_nlist, (unsigned long long)n_phys, (unsigned long long)n_span,
           (unsigned long long)zero_bins, (unsigned long long)total_bins);
    return 0;
}
