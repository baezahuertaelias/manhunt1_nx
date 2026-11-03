/* Synchronous menu subset: guest RAM stays authoritative between callbacks. */
#include "nv2a_deko3d.h"
#include <deko3d.h>
#include <switch.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_SIZE 65536u
#define CMD_SIZE 16384u
#define MENU_FILTER 0x02022000u
#define MAX_DIMENSION 2048u
#define MAX_VERTICES (65536u * 9u)

/* Why guest RAM was brought up to date with the resident GPU image (logged as counts). */
enum { SYNC_PRESENT, SYNC_FENCE, SYNC_REJECT, SYNC_OVERLAP, SYNC_SWITCH, SYNC_REASONS };
static void resident_invalidate(int reason);
static DkDevice s_device;
static DkQueue s_queue;
static ManhuntNv2aDeko3dStats s_stats;
static uint64_t s_flips;
extern ptrdiff_t xbox_GetMemoryOffset(void);

static uint32_t aligned(uint32_t n, uint32_t alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

static int ram_span(uint32_t va, uint64_t bytes)
{
    uint64_t end = (uint64_t)va + bytes;
    return bytes && ((va && end <= 0x04000000ull)
        || (va >= 0x80000000u && end <= 0x84000000ull));
}

static int power_of_two(uint32_t n) { return n && !(n & (n - 1)); }
static uint32_t swap_rb(uint32_t v)
{
    return (v & 0xFF00FF00u) | ((v & 255u) << 16) | ((v >> 16) & 255u);
}

static int surface_ok(const Nv2aSurface *s)
{
    return s && s->width && s->height && s->width <= MAX_DIMENSION
        && s->height <= MAX_DIMENSION && s->bytes_per_pixel == 4
        && s->aa_sx == 1 && s->aa_sy == 1 && !s->clip_x && !s->clip_y
        && (s->format_raw == 8 || s->format_raw == 0x124)
        && s->pitch >= (uint64_t)s->width * 4 && nv2a_backend_surface_writable(s);
}

/* Bytes of texel data a texture of an accepted format occupies (the span nv2a_backend_decode_texture reads;
 * a P8 texture's palette is separate). Must agree with texture_ok below. */
static uint64_t texture_bytes(const Nv2aTexture *t)
{
    if (t->color == 0x12) return (uint64_t)(t->height - 1) * t->pitch + (uint64_t)t->width * 4;
    if (t->color == 0x06) return (uint64_t)t->width * t->height * 4;
    if (t->color == 0x0B) return (uint64_t)t->width * t->height;
    if (t->color == 0x0E) return (uint64_t)(t->width / 4) * (t->height / 4) * 16;
    return 0;
}

static int texture_ok(const Nv2aTexture *t)
{
    uint64_t bytes;
    if (!t || !t->width || !t->height || t->width > MAX_DIMENSION
        || t->height > MAX_DIMENSION || t->levels != 1
        || t->addr_u != 3 || t->addr_v != 3
        || (t->filter != 0 && t->filter != MENU_FILTER)) return 0;
    if (t->color == 0x12) {
        if (t->pitch < (uint64_t)t->width * 4) return 0;
        bytes = (uint64_t)(t->height - 1) * t->pitch + (uint64_t)t->width * 4;
    } else {
        if (!power_of_two(t->width) || !power_of_two(t->height)) return 0;
        if (t->color == 0x06) bytes = (uint64_t)t->width * t->height * 4;
        else if (t->color == 0x0B) {
            if ((t->palette_entries != 32 && t->palette_entries != 64
                 && t->palette_entries != 128 && t->palette_entries != 256)
                || (t->palette_va & 63) || !ram_span(t->palette_va,
                    (uint64_t)t->palette_entries * 4)) return 0;
            bytes = (uint64_t)t->width * t->height;
        } else if (t->color == 0x0E) {
            if (t->width < 4 || t->height < 4) return 0;
            bytes = (uint64_t)(t->width / 4) * (t->height / 4) * 16;
        } else return 0;
    }
    if (!ram_span(t->offset, bytes)) return 0;
    if (t->color == 0x0B) {
        const uint8_t *indices = (const uint8_t *)xbox_GetMemoryOffset() + t->offset;
        for (uint64_t i = 0; i < bytes; i++) if (indices[i] >= t->palette_entries) return 0;
    }
    return 1;
}

static int factor(uint32_t nv, DkBlendFactor *out)
{
    switch (nv) {
    case 0: *out = DkBlendFactor_Zero; return 1;
    case 1: *out = DkBlendFactor_One; return 1;
    case 0x302: *out = DkBlendFactor_SrcAlpha; return 1;
    case 0x303: *out = DkBlendFactor_InvSrcAlpha; return 1;
    default: return 0;
    }
}

static int state_ok(const Nv2aRenderState *r)
{
    DkBlendFactor src, dst;
    return r && !r->depth_test_enable && !r->depth_write && !r->stencil_enable
        && !r->cull_enable && !(r->color_mask & ~0x01010101u)
        && (!r->alpha_test_enable || (r->alpha_func == 0x204
            && (r->alpha_ref == 0 || r->alpha_ref == 128)))
        && (!r->blend_enable || (r->blend_eq == 0x8006
            && factor(r->blend_src, &src) && factor(r->blend_dst, &dst)));
}

static int can_draw(const Nv2aSurface *s, const Nv2aRenderState *r,
                    const Nv2aDrawInfo *info)
{
    int ok = s_device && surface_ok(s) && state_ok(r)
        && (info->shading == NV2A_SHADE_DIFFUSE || info->shading == NV2A_SHADE_MODULATE)
        && (info->path == NV2A_DRAW_FFP || info->path == NV2A_DRAW_PROGRAM)
        && !(info->shading == NV2A_SHADE_MODULATE && !info->combiner_seen
            && info->texture && info->texture->filter != 0)
        && info->primitive >= 5 && info->primitive <= 10
        && (info->shading != NV2A_SHADE_MODULATE
            || ((info->path == NV2A_DRAW_PROGRAM || info->has_texcoords) && texture_ok(info->texture)));
    if (!ok) { s_stats.rejected_draws++; resident_invalidate(SYNC_REJECT); }
    return ok;
}

static int can_draw_transformed(const Nv2aSurface *s, const Nv2aDrawInfo *info,
                                const Nv2aTransformInfo *t)
{
    (void)s;
    const Nv2aTexture *texture = info->texture;
    double su = texture && texture->color != 0x12 ? texture->width : 1;
    double sv = texture && texture->color != 0x12 ? texture->height : 1;
    int ok = t->count && t->count <= 65536 && t->finite && t->positive_w
        && t->constant_w && t->w >= 1e-3f && t->w <= 1e6f
        && (info->shading != NV2A_SHADE_MODULATE || (texture && t->texture_q_one
            && isfinite(t->tex_u_abs_max) && isfinite(t->tex_v_abs_max)
            && t->tex_u_abs_max * su <= 1e6 && t->tex_v_abs_max * sv <= 1e6));
    if (!ok) { s_stats.rejected_transforms++; resident_invalidate(SYNC_REJECT); }
    return ok;
}

static int can_clear(const Nv2aSurface *s, const Nv2aRenderState *r,
                    uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    (void)r; (void)argb; (void)zstencil;
    int ok = s_device && surface_ok(s) && flags == 0xF0 && !s->clear_rect_set;
    if (!ok) { s_stats.rejected_clears++; resident_invalidate(SYNC_REJECT); }
    return ok;
}

static DkMemBlock memory(uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, s_device, aligned(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

static int shader(DkShader *out, DkMemBlock code, uint32_t *offset, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = 0;
    if (fseek(f, 0, SEEK_END)) goto done;
    long size = ftell(f);
    if (size <= 0 || size > CODE_SIZE || fseek(f, 0, SEEK_SET)) goto done;
    uint32_t padded = aligned((uint32_t)size, DK_SHADER_CODE_ALIGNMENT);
    if (*offset > CODE_SIZE - padded) goto done;
    if (fread((uint8_t *)dkMemBlockGetCpuAddr(code) + *offset, 1, (size_t)size, f) != (size_t)size)
        goto done;
    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, code, *offset);
    dkShaderInitialize(out, &maker);
    *offset += padded;
    ok = 1;
done:
    fclose(f);
    return ok;
}

static void fail_closed(void)
{
    const char *message = "[MH-DEKO] FAIL accepted GPU operation did not complete; stopping before guest fence release\n";
    svcOutputDebugString(message, strlen(message));
    printf("%s", message);
    fflush(stdout);
    abort();
}

/* ---------------------------------------------------------------------------------------------
 * GPU execution (HW-PERF-001). Measured on the console, the first version cost about 44 ms per
 * accepted draw: 6 to 8 GPU memory blocks created and destroyed, shaders re-read, the whole
 * 640x480 surface uploaded through uncached memory and read back one uncached load at a time,
 * and every texture decoded texel by texel. Output must stay pixel-identical. Pieces, each with
 * its own config switch (default on; "=0" turns it off, no rebuild):
 *   persistent memory blocks and shaders (always);
 *   MANHUNT_DEKO_CACHED   CPU-cached transfer buffer, flushed with dkMemBlockFlushCpuCache
 *                         (libnx: clean + invalidate) after the CPU writes and after the GPU wrote;
 *   MANHUNT_DEKO_RECT     only the rectangle a draw touches is transferred;
 *   MANHUNT_DEKO_TCACHE   decoded textures are kept on the GPU, keyed by address, format and a
 *                         64-bit hash of every byte the decode reads (texels and palette);
 *   MANHUNT_DEKO_RESIDENT the render target stays on the GPU across calls. Guest RAM is brought up
 *                         to date (a "sync") before anything else can read or overwrite the surface:
 *                         the present (horizon_compat.c calls manhunt_nv2a_deko3d_sync first), a
 *                         draw or clear the CPU renderer takes (the can_* callbacks), a texture that
 *                         overlaps the surface (render-to-texture), a change of surface.
 *   MANHUNT_DEKO_VERIFY=1 diagnostic: every call also runs through the conservative path on a host
 *                         shadow of the surface, and the two results are compared exactly.
 * Not covered by any hook: the guest's own CPU reading the surface mid-frame (it would see the
 * pre-sync pixels). */
typedef struct {
    DkMemBlock image_mem, read_mem, cmd_mem, data_mem, tex_mem, tex_up, desc_mem;
    uint32_t image_cap, read_cap, cmd_cap, data_cap, tex_cap, up_cap, desc_cap;
    int read_cached, read_req;           /* effective and requested mode of read_mem */
    uint32_t *scratch;
    size_t scratch_cap;
} GpuRes;
/* [0]: the real path. [1]: the verify mode's conservative path, so it can never touch the resident image. */
static GpuRes s_res[2];
static DkMemBlock s_code_mem;
static uint32_t s_code_cap;
static DkShader s_vs, s_fs_diffuse, s_fs_texture;
static int s_shaders_ready;
static int s_use_rect = 1, s_want_cached = 1, s_use_tcache = 1, s_use_resident = 1, s_verify;

typedef struct { uint64_t prep, build, gpu, readback, tex; uint32_t draws, clears, pixels, tex_hits, tex_misses, syncs, why[SYNC_REASONS]; } DekoTime;
static DekoTime s_time;
static uint64_t ticks_to_us(uint64_t t) { return t * 1000000ull / 19200000ull; }

typedef struct { int use_rect, want_cached, use_tcache, resident; uint8_t *pix0; int res; } GpuOpts;

/* Resident render target. valid: the GPU image holds the surface; dirty: it is newer than guest RAM
 * inside [dx0,dx1) x [dy0,dy1). */
static struct { int valid, dirty, cached; uint32_t va, pitch, w, h; uint8_t *pix0;
                uint32_t dx0, dy0, dx1, dy1; } R;

/* Verify mode: host shadow of the surface, driven through the conservative path. */
static uint8_t *s_shadow;
static size_t s_shadow_cap;
static struct { uint64_t calls, bad_calls, bad_pixels; uint32_t worst; } s_vstat;

/* Texture cache. */
#define TCACHE_N 24
typedef struct {
    int used;
    uint32_t offset, width, height, pitch, color, palette_va, palette_entries;
    uint64_t hash, last_use;
    DkMemBlock mem;
    uint32_t cap;
} TexEntry;
static TexEntry s_texc[TCACHE_N];
static uint64_t s_tex_clock;

static int ensure(DkMemBlock *blk, uint32_t *cap, uint32_t need, uint32_t flags)
{
    if (*blk && *cap >= need) return 1;
    if (*blk) { dkMemBlockDestroy(*blk); *blk = NULL; *cap = 0; }   /* the queue is idle: every call ends with dkQueueWaitIdle */
    *blk = memory(need, flags);
    if (!*blk) return 0;
    *cap = aligned(need, DK_MEMBLOCK_ALIGNMENT);
    return 1;
}

/* The transfer buffer: CPU-cached for fast loops; if the device refuses that combination, fall back
 * to the old uncached buffer (slow, still correct). Recreated only when the REQUESTED mode changes. */
static int ensure_read(GpuRes *r, uint32_t need, int want_cached)
{
    static int warned;
    if (r->read_mem && r->read_cap >= need && r->read_req == want_cached) return 1;
    if (r->read_mem) { dkMemBlockDestroy(r->read_mem); r->read_mem = NULL; r->read_cap = 0; }
    r->read_req = want_cached;
    r->read_mem = want_cached ? memory(need, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuUncached) : NULL;
    r->read_cached = r->read_mem != NULL;
    if (!r->read_mem) {
        if (want_cached && !warned) { warned = 1; printf("[MH-DEKO] cached transfer buffer unavailable: using the uncached one (slow)\n"); }
        r->read_mem = memory(need, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);
    }
    if (!r->read_mem) return 0;
    r->read_cap = aligned(need, DK_MEMBLOCK_ALIGNMENT);
    return 1;
}

static int ensure_shaders(void)
{
    if (s_shaders_ready) return 1;
    uint32_t offset = 0;
    if (!ensure(&s_code_mem, &s_code_cap, CODE_SIZE,
                DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code)) return 0;
    if (!shader(&s_vs, s_code_mem, &offset, "romfs:/shaders/nv2a_attribute_vsh.dksh")
        || !shader(&s_fs_diffuse, s_code_mem, &offset, "romfs:/shaders/nv2a_diffuse_fsh.dksh")
        || !shader(&s_fs_texture, s_code_mem, &offset, "romfs:/shaders/nv2a_texture_fsh.dksh")) return 0;
    s_shaders_ready = 1;
    printf("[MH-DEKO] fast path active: cached=%d rect=%d tcache=%d resident=%d verify=%d\n",
           s_want_cached, s_use_rect, s_use_tcache, s_use_resident, s_verify);
    return 1;
}

static void release_resources(void)
{
    for (unsigned c = 0; c < 2; c++) {
        GpuRes *r = &s_res[c];
        DkMemBlock *all[] = {&r->image_mem, &r->read_mem, &r->cmd_mem, &r->data_mem, &r->tex_mem, &r->tex_up, &r->desc_mem};
        for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
            if (*all[i]) { dkMemBlockDestroy(*all[i]); *all[i] = NULL; }
        free(r->scratch);
        memset(r, 0, sizeof *r);
    }
    if (s_code_mem) { dkMemBlockDestroy(s_code_mem); s_code_mem = NULL; }
    s_code_cap = 0;
    for (unsigned i = 0; i < TCACHE_N; i++)
        if (s_texc[i].mem) dkMemBlockDestroy(s_texc[i].mem);
    memset(s_texc, 0, sizeof s_texc);
    s_shaders_ready = 0;
    memset(&R, 0, sizeof R);
    free(s_shadow); s_shadow = NULL; s_shadow_cap = 0;
}

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h ^= v;
        h *= 0x100000001b3ull;
        h ^= h >> 29;
    }
    for (; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

/* Every byte nv2a_backend_decode_texture reads: the texels (for linear 0x12 only width*4 of each pitch
 * row) and, for P8 (0x0B), the palette. Formats are those texture_ok accepts. */
static uint64_t texture_hash(const Nv2aTexture *t, const uint8_t *guest)
{
    uint64_t h = 0xcbf29ce484222325ull ^ ((uint64_t)t->width << 32) ^ t->height ^ ((uint64_t)t->color << 48);
    if (t->color == 0x12) {
        for (uint32_t y = 0; y < t->height; y++)
            h = hash_bytes(h, guest + t->offset + (size_t)y * t->pitch, (size_t)t->width * 4);
    } else {
        h = hash_bytes(h, guest + t->offset, (size_t)texture_bytes(t));
    }
    if (t->color == 0x0B) h = hash_bytes(h, guest + t->palette_va, (size_t)t->palette_entries * 4);
    return h;
}

/* A cap on the GPU memory the texture cache holds: evict least-recently-used entries (never `keep`). */
#define TCACHE_MAX_BYTES (96u << 20)
static void tex_make_room(uint32_t need, const TexEntry *keep)
{
    for (;;) {
        uint64_t total = 0;
        TexEntry *lru = NULL;
        for (unsigned i = 0; i < TCACHE_N; i++) {
            TexEntry *e = &s_texc[i];
            if (!e->mem) continue;
            total += e->cap;
            if (e != keep && (!lru || e->last_use < lru->last_use)) lru = e;
        }
        if (total + need <= TCACHE_MAX_BYTES || !lru) return;
        dkMemBlockDestroy(lru->mem);
        lru->mem = NULL; lru->cap = 0; lru->used = 0; lru->hash = 0;
    }
}

static TexEntry *tex_lookup(const Nv2aTexture *t)
{
    TexEntry *victim = NULL;
    for (unsigned i = 0; i < TCACHE_N; i++) {
        TexEntry *e = &s_texc[i];
        if (e->used && e->offset == t->offset && e->width == t->width && e->height == t->height
            && e->pitch == t->pitch && e->color == t->color && e->palette_va == t->palette_va
            && e->palette_entries == t->palette_entries) return e;
        if (!victim || (!e->used && victim->used) || (e->used == victim->used && e->last_use < victim->last_use))
            victim = e;
    }
    victim->used = 1;
    victim->offset = t->offset; victim->width = t->width; victim->height = t->height;
    victim->pitch = t->pitch; victim->color = t->color;
    victim->palette_va = t->palette_va; victim->palette_entries = t->palette_entries;
    victim->hash = 0;                    /* contents unknown: force a decode and upload */
    return victim;
}

static void rows_to_buffer(const uint8_t *pix0, uint32_t pitch, uint32_t x0, uint32_t y0,
                           uint32_t x1, uint32_t y1, uint32_t *dst)
{
    const uint32_t rw = x1 - x0;
    for (uint32_t y = y0; y < y1; y++) {
        const uint8_t *src = pix0 + (size_t)y * pitch + (size_t)x0 * 4;
        uint32_t *d = dst + (size_t)(y - y0) * rw;
        for (uint32_t x = 0; x < rw; x++) {
            uint32_t argb;
            memcpy(&argb, src + (size_t)x * 4, 4);
            d[x] = swap_rb(argb);
        }
    }
}

static void buffer_to_rows(const uint32_t *src, uint8_t *pix0, uint32_t pitch, uint32_t x0, uint32_t y0,
                           uint32_t x1, uint32_t y1)
{
    const uint32_t rw = x1 - x0;
    for (uint32_t y = y0; y < y1; y++) {
        const uint32_t *s = src + (size_t)(y - y0) * rw;
        uint8_t *dst = pix0 + (size_t)y * pitch + (size_t)x0 * 4;
        for (uint32_t x = 0; x < rw; x++) {
            uint32_t argb = swap_rb(s[x]);
            memcpy(dst + (size_t)x * 4, &argb, 4);
        }
    }
}

static void target_layout(uint32_t w, uint32_t h, DkImageLayout *layout)
{
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = w; lm.dimensions[1] = h;
    dkImageLayoutInitialize(layout, &lm);
}

/* Bring guest RAM up to date with the resident GPU image (the dirty rectangle only). */
static void resident_flush_dirty(int reason)
{
    if (!R.valid || !R.dirty) return;
    GpuRes *r = &s_res[0];
    const uint64_t t0 = armGetSystemTick();
    const uint32_t rw = R.dx1 - R.dx0, rh = R.dy1 - R.dy0, rbytes = rw * rh * 4;
    DkImageLayout layout;
    target_layout(R.w, R.h, &layout);
    if (!ensure(&r->image_mem, &r->image_cap, aligned(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout)),
                DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        || !ensure_read(r, rbytes, R.cached)
        || !ensure(&r->cmd_mem, &r->cmd_cap, CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) fail_closed();
    DkImage image;
    dkImageInitialize(&image, &layout, r->image_mem, 0);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_device);
    DkCmdBuf cmd = dkCmdBufCreate(&cm);
    if (!cmd) fail_closed();
    dkCmdBufAddMemory(cmd, r->cmd_mem, 0, CMD_SIZE);
    DkImageRect rect = {R.dx0, R.dy0, 0, rw, rh, 1};
    DkCopyBuf transfer = {dkMemBlockGetGpuAddr(r->read_mem), 0, 0};
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &transfer, 0);
    DkCmdList list = dkCmdBufFinishList(cmd);
    dkQueueSubmitCommands(s_queue, list); dkQueueFlush(s_queue); dkQueueWaitIdle(s_queue);
    dkCmdBufDestroy(cmd);
    if (dkQueueIsInErrorState(s_queue)) fail_closed();
    dkMemBlockFlushCpuCache(r->read_mem, 0, rbytes);      /* drop stale cache lines: the GPU wrote this memory */
    buffer_to_rows(dkMemBlockGetCpuAddr(r->read_mem), R.pix0, R.pitch, R.dx0, R.dy0, R.dx1, R.dy1);
    if (s_verify && s_shadow && s_shadow_cap >= (size_t)R.pitch * R.h) {
        uint64_t bad = 0;
        uint32_t worst = 0;
        for (uint32_t y = R.dy0; y < R.dy1; y++)
            for (uint32_t x = R.dx0; x < R.dx1; x++) {
                uint32_t a, c;
                memcpy(&a, R.pix0 + (size_t)y * R.pitch + (size_t)x * 4, 4);
                memcpy(&c, s_shadow + (size_t)y * R.pitch + (size_t)x * 4, 4);
                if (a != c) {
                    bad++;
                    for (int k = 0; k < 4; k++) {
                        int d = (int)((a >> (8 * k)) & 255) - (int)((c >> (8 * k)) & 255);
                        if (d < 0) d = -d;
                        if ((uint32_t)d > worst) worst = (uint32_t)d;
                    }
                }
            }
        s_vstat.calls++;
        if (bad) { s_vstat.bad_calls++; s_vstat.bad_pixels += bad; if (worst > s_vstat.worst) s_vstat.worst = worst; }
    }
    R.dirty = 0;
    s_time.syncs++;
    s_time.why[reason]++;
    s_time.readback += armGetSystemTick() - t0;
}

/* The CPU renderer is about to read or write the surface, or the surface identity changes. */
static void resident_invalidate(int reason)
{
    resident_flush_dirty(reason);
    R.valid = 0;
}

/* Called by the present (horizon_compat.c) before it reads guest RAM. */
void manhunt_nv2a_deko3d_sync(void)
{
    if (s_device) resident_flush_dirty(SYNC_PRESENT);
}

/* Nv2aBackend.sync (patch 0018): the title is about to see the GPU as done (flush), or the CPU renderer
 * takes over after a batch was accepted (invalidate). */
static void backend_sync(int invalidate)
{
    if (!s_device) return;
    if (invalidate) resident_invalidate(SYNC_REJECT);
    else resident_flush_dirty(SYNC_FENCE);
}

static int spans_overlap(uint64_t a, uint64_t alen, uint64_t b, uint64_t blen)
{
    return a < b + blen && b < a + alen;
}

static void gpu_call(const Nv2aSurface *s, const Nv2aBatch *b, uint32_t clear_argb, const GpuOpts *o)
{
    GpuRes *r = &s_res[o->res];
    DkCmdBuf cmd = NULL;
    int ok = 0;
    const uint32_t w = s->width, h = s->height;
    const uint8_t *guest = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t x0 = 0, y0 = 0, x1 = w, y1 = h;       /* the rectangle this call draws into */
    const uint64_t t_start = armGetSystemTick();
    uint64_t t_after_prep = t_start, t_submit = t_start, t_after_gpu = t_start;

    if (b) {
        if (!b->count || b->count > MAX_VERTICES || b->count % 3 || !b->diffuse_float
            || (b->shading != NV2A_SHADE_DIFFUSE && b->shading != NV2A_SHADE_MODULATE)
            || (b->shading == NV2A_SHADE_MODULATE && !b->texture)) goto done;
        float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
        for (uint32_t i = 0; i < b->count; i++) {
            const Nv2aVertex *v = &b->vertices[i];
            if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z)
                || !isfinite(v->rhw) || v->rhw <= 0
                || (b->shading == NV2A_SHADE_MODULATE
                    && (!isfinite(v->u) || !isfinite(v->v)))) goto done;
            for (unsigned k = 0; k < 4; k++) if (!isfinite(b->diffuse_float[i][k])) goto done;
            if (v->x < minx) minx = v->x;
            if (v->x > maxx) maxx = v->x;
            if (v->y < miny) miny = v->y;
            if (v->y > maxy) maxy = v->y;
        }
        float fx0 = fmaxf(0.f, floorf(minx) - 2.f), fy0 = fmaxf(0.f, floorf(miny) - 2.f);
        float fx1 = fminf((float)w, ceilf(maxx) + 2.f), fy1 = fminf((float)h, ceilf(maxy) + 2.f);
        if (fx1 <= fx0 || fy1 <= fy0) { ok = 1; goto done; }   /* nothing inside the surface */
        if (o->use_rect) { x0 = (uint32_t)fx0; y0 = (uint32_t)fy0; x1 = (uint32_t)fx1; y1 = (uint32_t)fy1; }
    }
    const uint32_t rw = x1 - x0, rh = y1 - y0;

    /* Resident target: a different surface (or none yet) first flushes the old one, then uploads this one. */
    int upload_full = 0;
    if (o->resident) {
        int same = R.valid && R.va == s->color_va && R.pitch == s->pitch && R.w == w && R.h == h && R.pix0 == o->pix0;
        if (!same) { resident_invalidate(SYNC_SWITCH); upload_full = 1; }
        else if (b && b->shading == NV2A_SHADE_MODULATE && b->texture && R.dirty) {
            /* render-to-texture: the texture decode reads guest RAM, which may overlap the dirty surface */
            if (spans_overlap(b->texture->offset, texture_bytes(b->texture), s->color_va, (uint64_t)s->pitch * h))
                resident_flush_dirty(SYNC_OVERLAP);
        }
    }
    const int upload = !o->resident || upload_full;
    const uint32_t ux0 = o->resident ? 0 : x0, uy0 = o->resident ? 0 : y0;
    const uint32_t ux1 = o->resident ? w : x1, uy1 = o->resident ? h : y1;
    const uint32_t xfer_bytes = (o->resident ? w * h : rw * rh) * 4;

    DkImageLayout layout;
    target_layout(w, h, &layout);
    if (!ensure(&r->image_mem, &r->image_cap,
                aligned(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout)),
                DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        || !ensure_read(r, xfer_bytes, o->want_cached)
        || !ensure(&r->cmd_mem, &r->cmd_cap, CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
    DkImage image;
    dkImageInitialize(&image, &layout, r->image_mem, 0);
    DkImageView view;
    dkImageViewDefaults(&view, &image);

    uint32_t *pixels = dkMemBlockGetCpuAddr(r->read_mem);
    if (upload) {
        /* Guest RAM is authoritative: the GPU starts from it. */
        rows_to_buffer(o->pix0, s->pitch, ux0, uy0, ux1, uy1, pixels);
        dkMemBlockFlushCpuCache(r->read_mem, 0, (ux1 - ux0) * (uy1 - uy0) * 4);   /* CPU writes must reach memory before the GPU reads */
    }
    t_after_prep = armGetSystemTick();

    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_device);
    cmd = dkCmdBufCreate(&cm);
    if (!cmd) goto done;
    dkCmdBufAddMemory(cmd, r->cmd_mem, 0, CMD_SIZE);
    /* Vertex, uniform and descriptor memory is rewritten by the CPU for every call: make sure the GPU
     * does not read stale lines from its L2. */
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    DkCopyBuf transfer = {dkMemBlockGetGpuAddr(r->read_mem), 0, 0};
    DkImageRect up_rect = {ux0, uy0, 0, ux1 - ux0, uy1 - uy0, 1};
    if (upload) {
        dkCmdBufCopyBufferToImage(cmd, &transfer, &view, &up_rect, 0);
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    }
    dkCmdBufBindRenderTarget(cmd, &view, NULL);
    DkViewport viewport = {0, 0, w, h, 0, 1};
    DkScissor scissor = {x0, y0, rw, rh};
    dkCmdBufSetViewports(cmd, 0, &viewport, 1);
    dkCmdBufSetScissors(cmd, 0, &scissor, 1);
    if (!b) {
        dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA,
            ((clear_argb >> 16) & 255) / 255.f, ((clear_argb >> 8) & 255) / 255.f,
            (clear_argb & 255) / 255.f, (clear_argb >> 24) / 255.f);
    } else {
        uint32_t vertex_bytes = b->count * sizeof(Nv2aVertex);
        uint32_t colour_at = aligned(vertex_bytes, 16), colour_bytes = b->count * 4 * sizeof(float);
        uint32_t uniform_at = aligned(colour_at + colour_bytes, 256);
        if (!ensure_shaders()
            || !ensure(&r->data_mem, &r->data_cap, uniform_at + 256,
                       DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
        memcpy(dkMemBlockGetCpuAddr(r->data_mem), b->vertices, vertex_bytes);
        if (b->shading == NV2A_SHADE_DIFFUSE) {
            Nv2aVertex *vertices = dkMemBlockGetCpuAddr(r->data_mem);
            for (uint32_t i = 0; i < b->count; i++) vertices[i].u = vertices[i].v = 0;
        }
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(r->data_mem) + colour_at, b->diffuse_float, colour_bytes);
        const Nv2aTexture *t = b->shading == NV2A_SHADE_MODULATE ? b->texture : NULL;
        if (t && !texture_ok(t)) goto done;
        float settings[12] = {w, h, 0, 0, t ? t->width : 1, t ? t->height : 1,
                             b->state->alpha_test_enable, b->state->alpha_ref, t ? 1 : 2,
                             t && t->filter == MENU_FILTER};
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(r->data_mem) + uniform_at, settings, sizeof settings);
        DkShader const *shaders[] = {&s_vs, t ? &s_fs_texture : &s_fs_diffuse};
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Shader);
        dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
        if (t) {
            DkImageLayoutMaker tm;
            dkImageLayoutMakerDefaults(&tm, s_device);
            tm.flags = DkImageFlags_Usage2DEngine; tm.format = DkImageFormat_RGBA8_Unorm;
            tm.dimensions[0] = t->width; tm.dimensions[1] = t->height;
            DkImageLayout tl;
            dkImageLayoutInitialize(&tl, &tm);
            const uint32_t tex_bytes = t->width * t->height * 4;
            const uint32_t tex_mem_bytes = aligned(dkImageLayoutGetSize(&tl), dkImageLayoutGetAlignment(&tl));
            TexEntry *e = NULL;
            uint64_t hsh = 0;
            int hit = 0;
            DkMemBlock timg = NULL;
            if (o->use_tcache) {
                hsh = texture_hash(t, guest);
                e = tex_lookup(t);
                if (!e->mem || e->cap < tex_mem_bytes) tex_make_room(tex_mem_bytes, e);
                const DkMemBlock before = e->mem;
                if (!ensure(&e->mem, &e->cap, tex_mem_bytes, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)) goto done;
                if (e->mem != before) e->hash = 0;              /* new memory: nothing uploaded yet */
                hit = e->hash == hsh && e->hash != 0;
                e->last_use = ++s_tex_clock;
                timg = e->mem;
                if (hit) s_time.tex_hits++; else s_time.tex_misses++;
            } else {
                if (!ensure(&r->tex_mem, &r->tex_cap, tex_mem_bytes, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)) goto done;
                timg = r->tex_mem;
            }
            if (!ensure(&r->desc_mem, &r->desc_cap, 4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
            DkImage ti;
            dkImageInitialize(&ti, &tl, timg, 0);
            DkImageView tv;
            dkImageViewDefaults(&tv, &ti);
            if (!hit) {
                if (!ensure(&r->tex_up, &r->up_cap, tex_bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached)) goto done;
                if (r->scratch_cap < tex_bytes) {
                    uint32_t *grown = realloc(r->scratch, tex_bytes);
                    if (!grown) goto done;
                    r->scratch = grown;
                    r->scratch_cap = tex_bytes;
                }
                const uint64_t t_tex = armGetSystemTick();
                /* Decode and swap in ordinary memory; the uncached upload buffer is only written, in bulk. */
                if (!nv2a_backend_decode_texture(t, r->scratch)) goto done;
                for (uint32_t i = 0; i < t->width * t->height; i++) r->scratch[i] = swap_rb(r->scratch[i]);
                memcpy(dkMemBlockGetCpuAddr(r->tex_up), r->scratch, tex_bytes);
                s_time.tex += armGetSystemTick() - t_tex;
                DkCopyBuf source = {dkMemBlockGetGpuAddr(r->tex_up), 0, 0};
                DkImageRect tr = {0, 0, 0, t->width, t->height, 1};
                dkCmdBufCopyBufferToImage(cmd, &source, &tv, &tr, 0);
                if (e) e->hash = hsh;
            }
            DkSampler sampler;
            dkSamplerDefaults(&sampler);
            sampler.wrapMode[0] = sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
            if (t->filter == MENU_FILTER) sampler.minFilter = sampler.magFilter = DkFilter_Linear;
            dkImageDescriptorInitialize(dkMemBlockGetCpuAddr(r->desc_mem), &tv, false, false);
            dkSamplerDescriptorInitialize((DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(r->desc_mem) + 256), &sampler);
            dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
            dkCmdBufBindImageDescriptorSet(cmd, dkMemBlockGetGpuAddr(r->desc_mem), 1);
            dkCmdBufBindSamplerDescriptorSet(cmd, dkMemBlockGetGpuAddr(r->desc_mem) + 256, 1);
            DkResHandle handle = dkMakeTextureHandle(0, 0);
            dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &handle, 1);
        }
        DkRasterizerState raster;
        dkRasterizerStateDefaults(&raster); raster.cullMode = DkFace_None;
        DkDepthStencilState depth;
        dkDepthStencilStateDefaults(&depth);
        depth.depthTestEnable = false; depth.depthWriteEnable = false; depth.stencilTestEnable = false;
        DkColorState colour;
        dkColorStateDefaults(&colour);
        DkColorWriteState mask;
        dkColorWriteStateDefaults(&mask);
        const uint32_t nv_masks[4] = {0x10000, 0x100, 1, 0x1000000};
        unsigned bits = 0;
        for (unsigned i = 0; i < 4; i++) if (b->state->color_mask & nv_masks[i]) bits |= 1u << i;
        dkColorWriteStateSetMask(&mask, 0, bits);
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        if (b->state->blend_enable) {
            DkBlendFactor src, dst;
            if (!factor(b->state->blend_src, &src) || !factor(b->state->blend_dst, &dst)) goto done;
            dkColorStateSetBlendEnable(&colour, 0, true);
            dkBlendStateSetOps(&blend, DkBlendOp_Add, DkBlendOp_Add);
            dkBlendStateSetFactors(&blend, src, dst, src, dst);
        }
        dkCmdBufBindRasterizerState(cmd, &raster);
        dkCmdBufBindDepthStencilState(cmd, &depth);
        dkCmdBufBindColorState(cmd, &colour);
        dkCmdBufBindColorWriteState(cmd, &mask);
        dkCmdBufBindBlendStates(cmd, 0, &blend, 1);
        DkVtxAttribState attrs[] = {
            {.bufferId=0, .offset=offsetof(Nv2aVertex,x), .size=DkVtxAttribSize_4x32, .type=DkVtxAttribType_Float},
            {.bufferId=1, .offset=0, .size=DkVtxAttribSize_4x32, .type=DkVtxAttribType_Float},
            {.bufferId=0, .offset=offsetof(Nv2aVertex,u), .size=DkVtxAttribSize_2x32, .type=DkVtxAttribType_Float}};
        DkVtxBufferState vbs[] = {{sizeof(Nv2aVertex), 0}, {4 * sizeof(float), 0}};
        DkBufExtents vb[] = {{dkMemBlockGetGpuAddr(r->data_mem), vertex_bytes},
            {dkMemBlockGetGpuAddr(r->data_mem) + colour_at, colour_bytes}};
        DkBufExtents ub = {dkMemBlockGetGpuAddr(r->data_mem) + uniform_at, 256};
        dkCmdBufBindVtxAttribState(cmd, attrs, 3);
        dkCmdBufBindVtxBufferState(cmd, vbs, 2);
        dkCmdBufBindVtxBuffers(cmd, 0, vb, 2);
        dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, &ub, 1);
        dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, &ub, 1);
        dkCmdBufDraw(cmd, DkPrimitive_Triangles, b->count, 1, 0, 0);
    }
    if (!o->resident) {
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
        DkImageRect rb_rect = {x0, y0, 0, rw, rh, 1};
        dkCmdBufCopyImageToBuffer(cmd, &view, &rb_rect, &transfer, 0);
    }
    DkCmdList list = dkCmdBufFinishList(cmd);
    t_submit = armGetSystemTick();
    dkQueueSubmitCommands(s_queue, list); dkQueueFlush(s_queue); dkQueueWaitIdle(s_queue);
    t_after_gpu = armGetSystemTick();
    if (dkQueueIsInErrorState(s_queue)) goto done;
    if (o->resident) {
        /* The GPU image is now newer than guest RAM inside the touched rectangle. */
        if (upload_full) { R.valid = 1; R.va = s->color_va; R.pitch = s->pitch; R.w = w; R.h = h; R.pix0 = o->pix0; R.dirty = 0; }
        R.cached = o->want_cached;
        if (!R.dirty) { R.dx0 = x0; R.dy0 = y0; R.dx1 = x1; R.dy1 = y1; R.dirty = 1; }
        else {
            if (x0 < R.dx0) R.dx0 = x0;
            if (y0 < R.dy0) R.dy0 = y0;
            if (x1 > R.dx1) R.dx1 = x1;
            if (y1 > R.dy1) R.dy1 = y1;
        }
    } else {
        dkMemBlockFlushCpuCache(r->read_mem, 0, rw * rh * 4);   /* drop stale cache lines: the GPU wrote this memory */
        buffer_to_rows(pixels, o->pix0, s->pitch, x0, y0, x1, y1);
    }
    ok = 1;
    {
        const uint64_t t_end = armGetSystemTick();
        s_time.prep += t_after_prep - t_start;
        s_time.build += t_submit - t_after_prep;
        s_time.gpu += t_after_gpu - t_submit;
        if (!o->resident) s_time.readback += t_end - t_after_gpu;
        s_time.pixels += rw * rh;
        if (b) s_time.draws++; else s_time.clears++;
    }
done:
    if (cmd) dkCmdBufDestroy(cmd);
    if (!ok) fail_closed();
}

/* MANHUNT_DEKO_VERIFY=1: every call also runs through the conservative path (whole surface, uncached
 * buffer, no texture cache, nothing resident) on a host shadow of the surface; the two results are
 * compared exactly. Non-resident: after every call, over the whole surface. Resident: at every sync,
 * over the dirty rectangle (resident_flush_dirty). Slow; timings are not representative. */
static void execute(const Nv2aSurface *s, const Nv2aBatch *b, uint32_t clear_argb)
{
    uint8_t *guest = (uint8_t *)xbox_GetMemoryOffset();
    GpuOpts opt = {s_use_rect, s_want_cached, s_use_tcache, s_use_resident, guest + s->color_va, 0};
    if (!s_verify) { gpu_call(s, b, clear_argb, &opt); return; }
    const size_t bytes = (size_t)s->pitch * s->height;
    if (s_shadow_cap < bytes) {
        free(s_shadow);
        s_shadow = malloc(bytes);
        s_shadow_cap = s_shadow ? bytes : 0;
    }
    if (!s_shadow_cap) { gpu_call(s, b, clear_argb, &opt); return; }
    GpuOpts cons = {0, 0, 0, 0, s_shadow, 1};   /* its own GPU resources: it must never touch the resident image */
    if (!opt.resident) {
        memcpy(s_shadow, opt.pix0, bytes);                     /* same starting pixels */
        gpu_call(s, b, clear_argb, &opt);
        gpu_call(s, b, clear_argb, &cons);
        uint64_t bad = 0;
        uint32_t worst = 0;
        for (uint32_t y = 0; y < s->height; y++)
            for (uint32_t x = 0; x < s->width; x++) {
                uint32_t a, c;
                memcpy(&a, opt.pix0 + (size_t)y * s->pitch + (size_t)x * 4, 4);
                memcpy(&c, s_shadow + (size_t)y * s->pitch + (size_t)x * 4, 4);
                if (a != c) {
                    bad++;
                    for (int k = 0; k < 4; k++) {
                        int d = (int)((a >> (8 * k)) & 255) - (int)((c >> (8 * k)) & 255);
                        if (d < 0) d = -d;
                        if ((uint32_t)d > worst) worst = (uint32_t)d;
                    }
                }
            }
        s_vstat.calls++;
        if (bad) { s_vstat.bad_calls++; s_vstat.bad_pixels += bad; if (worst > s_vstat.worst) s_vstat.worst = worst; }
    } else {
        const int same = R.valid && R.va == s->color_va && R.pitch == s->pitch && R.w == s->width
                         && R.h == s->height && R.pix0 == opt.pix0;
        if (!same) {
            resident_invalidate(SYNC_SWITCH);                   /* compare and flush the old surface first */
            memcpy(s_shadow, opt.pix0, bytes);                 /* the shadow starts from what the GPU will upload */
        }
        gpu_call(s, b, clear_argb, &opt);
        gpu_call(s, b, clear_argb, &cons);
    }
}

static void draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    execute(s, b, 0);
    s_stats.draws++;
    if (s_stats.draws == 1) printf("[MH-DEKO] first GPU draw completed\n");
}

static void clear(const Nv2aSurface *s, const Nv2aRenderState *r,
                  uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    (void)r; (void)flags; (void)zstencil;
    execute(s, NULL, argb);
    s_stats.clears++;
}

static void flip(void)
{
    s_flips++;
    if (s_verify && s_flips % 30 == 0)
        printf("[MH-DEKO-V] compared %llu: %llu differ, %llu pixels differ in total, largest channel difference %u (fast path vs conservative path)\n",
            (unsigned long long)s_vstat.calls, (unsigned long long)s_vstat.bad_calls,
            (unsigned long long)s_vstat.bad_pixels, s_vstat.worst);
    if (s_flips % 30 == 0 && (s_time.draws + s_time.clears)) {
        uint32_t n = s_time.draws + s_time.clears;
        printf("[MH-DEKO-T] last 30 flips: %u draws %u clears; syncs %u (present %u fence %u reject %u overlap %u switch %u); per call us: upload %llu build %llu gpu %llu; readback total ms %llu; textures: decode total ms %llu hits %u misses %u; avg pixels %u (cached=%d rect=%d tcache=%d resident=%d)\n",
            s_time.draws, s_time.clears, s_time.syncs, s_time.why[SYNC_PRESENT], s_time.why[SYNC_FENCE],
            s_time.why[SYNC_REJECT], s_time.why[SYNC_OVERLAP], s_time.why[SYNC_SWITCH],
            (unsigned long long)(ticks_to_us(s_time.prep) / n), (unsigned long long)(ticks_to_us(s_time.build) / n),
            (unsigned long long)(ticks_to_us(s_time.gpu) / n), (unsigned long long)(ticks_to_us(s_time.readback) / 1000),
            (unsigned long long)(ticks_to_us(s_time.tex) / 1000), s_time.tex_hits, s_time.tex_misses,
            s_time.pixels / n, s_res[0].read_cached, s_use_rect, s_use_tcache, s_use_resident);
        memset(&s_time, 0, sizeof s_time);
    }
    if (s_flips <= 10 || s_flips % 60 == 0)
        printf("[MH-DEKO] flip=%llu draws=%llu clears=%llu reject_draw=%llu reject_clear=%llu reject_transforms=%llu\n",
            (unsigned long long)s_flips, (unsigned long long)s_stats.draws,
            (unsigned long long)s_stats.clears, (unsigned long long)s_stats.rejected_draws,
            (unsigned long long)s_stats.rejected_clears, (unsigned long long)s_stats.rejected_transforms);
}

static const Nv2aBackend s_backend = {
    .clear = clear, .draw = draw, .flip = flip, .can_draw = can_draw, .can_clear = can_clear,
    .can_draw_transformed = can_draw_transformed, .wants_float_diffuse = 1,
    .sync = backend_sync
};

int manhunt_nv2a_deko3d_init(void)
{
    if (s_device) return 1;
    DkDeviceMaker maker;
    dkDeviceMakerDefaults(&maker);
    s_device = dkDeviceCreate(&maker);
    if (!s_device) return 0;
    /* A synchronous executor owns one queue for the device lifetime. */
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, s_device); qm.flags = DkQueueFlags_Graphics;
    s_queue = dkQueueCreate(&qm);
    if (!s_queue) { dkDeviceDestroy(s_device); s_device = NULL; return 0; }
    memset(&s_stats, 0, sizeof s_stats);
    s_flips = 0;
    {
        const char *e = getenv("MANHUNT_DEKO_RECT");
        s_use_rect = !(e && *e == '0');
        e = getenv("MANHUNT_DEKO_CACHED");
        s_want_cached = !(e && *e == '0');
        e = getenv("MANHUNT_DEKO_TCACHE");
        s_use_tcache = !(e && *e == '0');
        e = getenv("MANHUNT_DEKO_RESIDENT");
        s_use_resident = !(e && *e == '0');
        e = getenv("MANHUNT_DEKO_VERIFY");
        s_verify = e && *e == '1';
        if (s_verify) printf("[MH-DEKO] VERIFY mode: every call runs twice and is compared (slow; timings are not representative)\n");
    }
    return s_device != NULL;
}

const Nv2aBackend *manhunt_nv2a_deko3d_backend(void) { return &s_backend; }
void manhunt_nv2a_deko3d_stats(ManhuntNv2aDeko3dStats *out) { if (out) *out = s_stats; }
void manhunt_nv2a_deko3d_shutdown(void)
{
    if (s_queue) dkQueueWaitIdle(s_queue);
    release_resources();
    if (s_queue) { dkQueueDestroy(s_queue); s_queue = NULL; }
    if (s_device) dkDeviceDestroy(s_device);
    s_device = NULL;
}
