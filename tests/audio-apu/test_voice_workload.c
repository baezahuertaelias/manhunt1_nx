/* Deterministic VP workload: actual voice lists, PCM fetch/resampling, filters,
 * HRTF, gain ramps and mixbins. Synthetic inputs, not hardware throughput. */
#define _POSIX_C_SOURCE 200809L
#define MANHUNT_APU_MATH_CACHE_TEST 1
#ifndef APU_VP_SOURCE
#define APU_VP_SOURCE "../../game/build/xboxrecomp/src/apu/apu_vp.c"
#endif
#include APU_VP_SOURCE
#include <time.h>
MCPXAPUState *g_state;
struct McpxApuDebug g_dbg, g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4];
uint8_t *g_apu_ram_ptr;
ApuIrqStats apu_irq_stats;
static uint8_t ram[1 << 20];
static uint64_t resolutions;
uint8_t *mcpx_apu_phys(uint64_t at) {
    assert(at < sizeof ram); resolutions++; return ram + at;
}
const char *mcpx_apu_phys_class(uint64_t at) { (void)at; return "fixture"; }
int apu_env_on(const char *name) { const char *e = getenv(name); return e && *e && *e != '0'; }
#define VOICE_BASE 0x10000
#define SGE_BASE 0x20000
#define DATA_BASE 0x30000
static void setreg(unsigned v, unsigned reg, uint32_t value) { memcpy(ram + VOICE_BASE + v * NV_PAVS_SIZE + reg, &value, 4); }
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
static unsigned voice_id(unsigned i) { return i < 16 ? i : 64 + i - 16; }
int main(int argc, char **argv) {
    static MCPXAPUState d;
    unsigned frames = argc > 1 ? (unsigned)atoi(argv[1]) : 6000;
    FILE *output = argc > 2 ? fopen(argv[2], "wb") : NULL;
    assert(!getenv("RECOMP_APU_TRACE"));
    g_apu_ram_ptr = ram; d.regs[NV_PAPU_VPVADDR] = VOICE_BASE; d.regs[NV_PAPU_VPSGEADDR] = SGE_BASE;
    d.regs[NV_PAPU_TVL2D] = 0; d.regs[NV_PAPU_TVL3D] = d.regs[NV_PAPU_TVLMP] = 0xffff;
    for (unsigned page = 0; page < 4; page++) {
        uint32_t address = DATA_BASE + page * 4096; memcpy(ram + SGE_BASE + page * 8, &address, 4);
    }
    for (unsigned i = 0; i < 8192; i++) { int16_t sample = (int16_t)(i * 211 + 719); memcpy(ram + DATA_BASE + i * 2, &sample, 2); }
    for (unsigned n = 0; n < 48; n++) {
        unsigned v = voice_id(n), next = n + 1 < 48 ? voice_id(n + 1) : 0xffff;
        setreg(v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_LOOP | (1u << 28) | (1u << 30));
        setreg(v, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
        setreg(v, NV_PAVS_VOICE_PAR_NEXT, 8191); setreg(v, NV_PAVS_VOICE_TAR_PITCH_LINK, next | ((uint32_t)(uint16_t)(-512 + n * 17) << 16));
        setreg(v, NV_PAVS_VOICE_CFG_VBIN, (n % 16) | (((n + 1) % 16) << 5));
        setreg(v, NV_PAVS_VOICE_TAR_VOLA, 0x10001000); setreg(v, NV_PAVS_VOICE_TAR_VOLB, 0xffffffff); setreg(v, NV_PAVS_VOICE_TAR_VOLC, 0xffffffff);
        if (!(n % 4)) { setreg(v, NV_PAVS_VOICE_CFG_MISC, 1u << 16); setreg(v, NV_PAVS_VOICE_TAR_FCA, (0x4000u << 16) | (uint16_t)-4096); }
        if (v < 64) {
            float coeff[2][HRTF_NUM_TAPS];
            for (unsigned ch = 0; ch < 2; ch++) for (unsigned k = 0; k < HRTF_NUM_TAPS; k++) coeff[ch][k] = ((int)((k * 7 + ch * 13 + n) % 31) - 15) / 31.0f;
            hrtf_filter_set_target_params(&d.vp.filters[v].hrtf, coeff, (float)n - 7.25f);
            setreg(v, NV_PAVS_VOICE_CFG_HRTF_TARGET, n);
        }
    }
    for (unsigned i = 0; i < 4; i++) d.vp.hrtf_submix[i] = i;
    uint64_t hash = 1469598103934665603ull, started = now_ns();
    for (unsigned frame = 0; frame < frames; frame++) {
        float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = {{0}};
        if (!(frame % 43)) {
            unsigned v = voice_id((frame / 43) % 48);
            setreg(v, NV_PAVS_VOICE_TAR_VOLA, ((frame * 11) & 4095) << 4);
        }
        if (!(frame % 67)) d.vp.filters[(frame / 67) % 16].hrtf.itd_tar = (int)(frame % 83) - 41;
        /* Pause/resume, a finite voice ending and notifier delivery exercise
         * the source timing around convolution, not just steady-state PCM. */
        if (frame == 401) voice_set_mask(&d, 4, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_PAUSED, 1);
        if (frame == 421) voice_set_mask(&d, 4, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_PAUSED, 0);
        if (frame == 701) {
            voice_set_mask(&d, 15, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_LOOP, 0);
            uint32_t cbo = voice_get_mask(&d, 15, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO);
            voice_set_mask(&d, 15, NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EBO, cbo + 40);
        }
        mcpx_apu_vp_frame(&d, mixbins);
        const unsigned char *bytes = (const unsigned char *)mixbins;
        for (unsigned i = 0; i < sizeof mixbins; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
        if (output) assert(fwrite(mixbins, 1, sizeof mixbins, output) == sizeof mixbins);
    }
    uint64_t elapsed = now_ns() - started;
    if (output) {
        /* Include observable VP state, resampling/filter histories, guest
         * envelope/play cursors and pending IRQ/notifier state. */
        assert(fwrite(ram, 1, sizeof ram, output) == sizeof ram);
        for (unsigned n = 0; n < 48; n++) {
            MCPXAPUVoiceFilter f = d.vp.filters[voice_id(n)];
            f.svf[0].op = f.svf[1].op = NULL; f.resampler = NULL;
            assert(fwrite(&f, 1, sizeof f, output) == sizeof f);
        }
        assert(fwrite(d.regs, 1, sizeof d.regs, output) == sizeof d.regs);
        assert(fwrite(&d.set_irq, 1, sizeof d.set_irq, output) == sizeof d.set_irq);
        assert(!fclose(output));
    }
    printf("VP frames=%u voices=48 hrtf=16 wall_ms=%.3f ns_per_voice=%.1f phys=%llu hash=%016llx\n", frames, elapsed / 1e6,
           (double)elapsed / frames / 48, (unsigned long long)resolutions, (unsigned long long)hash);
    return 0;
}
