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
    if (!ok) s_stats.rejected_draws++;
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
    if (!ok) s_stats.rejected_transforms++;
    return ok;
}

static int can_clear(const Nv2aSurface *s, const Nv2aRenderState *r,
                    uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    (void)r; (void)argb; (void)zstencil;
    int ok = s_device && surface_ok(s) && flags == 0xF0 && !s->clear_rect_set;
    if (!ok) s_stats.rejected_clears++;
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

/* Stage 1 of the adapter's speed-up (HW-PERF-001). Measured on the console, every
 * draw cost about 44 ms: it created and destroyed 6 to 8 GPU memory blocks,
 * re-read the shader files, uploaded the whole 640x480 surface through uncached
 * memory and read the whole surface back one uncached 4-byte load at a time.
 * The output must stay pixel-identical, so only the cost changes:
 *   - memory blocks and shaders live for the process lifetime (grow only; every
 *     execute() ends with dkQueueWaitIdle, so nothing is in flight when one grows);
 *   - the transfer buffer is CPU-cached; the CPU cache is flushed (clean and
 *     invalidate) after the CPU writes and again after the GPU wrote;
 *   - textures are decoded into ordinary memory and copied in bulk;
 *   - only the rectangle a draw touches is uploaded, drawn (scissor) and read back.
 * Guest RAM still holds the authoritative pixels after every call. */
static DkMemBlock s_image_mem, s_read_mem, s_cmd_mem, s_code_mem, s_data_mem;
static DkMemBlock s_tex_mem, s_tex_up, s_desc_mem;
static uint32_t s_image_cap, s_read_cap, s_cmd_cap, s_code_cap, s_data_cap, s_tex_cap, s_up_cap, s_desc_cap;
static DkShader s_vs, s_fs_diffuse, s_fs_texture;
static int s_shaders_ready;
static uint32_t *s_scratch;
static size_t s_scratch_cap;
static int s_read_cached = 1;
/* Bisect switches (config only, no rebuild): MANHUNT_DEKO_RECT=0 transfers the whole
 * surface for every draw (rectangle logic off); MANHUNT_DEKO_CACHED=0 uses the old
 * uncached transfer buffer (cache logic off). Both default on. */
static int s_use_rect = 1, s_want_cached = 1;
/* Phase timing (armGetSystemTick, 19.2 MHz): accumulated per execute() call and
 * printed with the flip statistics, so one run shows what dominates after the fix. */
typedef struct { uint64_t prep, build, gpu, readback, tex; uint32_t draws, clears, pixels; } DekoTime;
static DekoTime s_time;
static uint64_t ticks_to_us(uint64_t t) { return t * 1000000ull / 19200000ull; }

static int ensure(DkMemBlock *blk, uint32_t *cap, uint32_t need, uint32_t flags)
{
    if (*blk && *cap >= need) return 1;
    if (*blk) { dkMemBlockDestroy(*blk); *blk = NULL; *cap = 0; }
    *blk = memory(need, flags);
    if (!*blk) return 0;
    *cap = aligned(need, DK_MEMBLOCK_ALIGNMENT);
    return 1;
}

/* The transfer buffer: CPU-cached for fast loops; if the device refuses that
 * combination, fall back to the old uncached buffer (slow, still correct). */
static int ensure_read(uint32_t need, int want_cached)
{
    if (s_read_mem && s_read_cap >= need && s_read_cached == want_cached) return 1;
    if (s_read_mem) { dkMemBlockDestroy(s_read_mem); s_read_mem = NULL; s_read_cap = 0; }
    s_read_mem = want_cached ? memory(need, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuUncached) : NULL;
    s_read_cached = s_read_mem != NULL;
    if (!s_read_mem) {
        if (want_cached) printf("[MH-DEKO] cached transfer buffer unavailable: using the uncached one (slow)\n");
        s_read_mem = memory(need, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);
    }
    if (!s_read_mem) return 0;
    s_read_cap = aligned(need, DK_MEMBLOCK_ALIGNMENT);
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
    printf("[MH-DEKO] persistent resources, cached transfer buffer and rectangle transfers active\n");
    return 1;
}

static void release_resources(void)
{
    DkMemBlock *all[] = {&s_image_mem, &s_read_mem, &s_cmd_mem, &s_code_mem, &s_data_mem,
                         &s_tex_mem, &s_tex_up, &s_desc_mem};
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
        if (*all[i]) { dkMemBlockDestroy(*all[i]); *all[i] = NULL; }
    s_image_cap = s_read_cap = s_cmd_cap = s_code_cap = s_data_cap = s_tex_cap = s_up_cap = s_desc_cap = 0;
    s_shaders_ready = 0;
    free(s_scratch);
    s_scratch = NULL;
    s_scratch_cap = 0;
}

static void execute_with(const Nv2aSurface *s, const Nv2aBatch *b, uint32_t clear_argb,
                         int use_rect, int want_cached)
{
    DkCmdBuf cmd = NULL;
    int ok = 0;
    uint32_t w = s->width, h = s->height;
    uint8_t *guest = (uint8_t *)xbox_GetMemoryOffset();
    uint32_t x0 = 0, y0 = 0, x1 = w, y1 = h;       /* the rectangle uploaded, drawn and read back */
    const uint64_t t_start = armGetSystemTick();
    uint64_t t_after_prep = t_start, t_after_gpu = t_start;

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
        if (use_rect) { x0 = (uint32_t)fx0; y0 = (uint32_t)fy0; x1 = (uint32_t)fx1; y1 = (uint32_t)fy1; }
    }
    const uint32_t rw = x1 - x0, rh = y1 - y0, rbytes = rw * rh * 4;

    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = w; lm.dimensions[1] = h;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    if (!ensure(&s_image_mem, &s_image_cap,
                aligned(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout)),
                DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        || !ensure_read(rbytes, want_cached)
        || !ensure(&s_cmd_mem, &s_cmd_cap, CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
    DkImage image;
    dkImageInitialize(&image, &layout, s_image_mem, 0);
    DkImageView view;
    dkImageViewDefaults(&view, &image);

    /* Upload the rectangle: guest RAM is authoritative, so the GPU starts from it. */
    uint32_t *pixels = dkMemBlockGetCpuAddr(s_read_mem);
    for (uint32_t y = y0; y < y1; y++) {
        const uint8_t *src = guest + s->color_va + (size_t)y * s->pitch + (size_t)x0 * 4;
        uint32_t *dst = pixels + (size_t)(y - y0) * rw;
        for (uint32_t x = 0; x < rw; x++) {
            uint32_t argb;
            memcpy(&argb, src + (size_t)x * 4, 4);
            dst[x] = swap_rb(argb);
        }
    }
    dkMemBlockFlushCpuCache(s_read_mem, 0, rbytes);     /* the CPU's writes must reach memory before the GPU reads */
    t_after_prep = armGetSystemTick();

    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_device);
    cmd = dkCmdBufCreate(&cm);
    if (!cmd) goto done;
    dkCmdBufAddMemory(cmd, s_cmd_mem, 0, CMD_SIZE);
    DkImageRect rect = {x0, y0, 0, rw, rh, 1};
    DkCopyBuf transfer = {dkMemBlockGetGpuAddr(s_read_mem), 0, 0};
    dkCmdBufCopyBufferToImage(cmd, &transfer, &view, &rect, 0);
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
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
            || !ensure(&s_data_mem, &s_data_cap, uniform_at + 256,
                       DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
        memcpy(dkMemBlockGetCpuAddr(s_data_mem), b->vertices, vertex_bytes);
        if (b->shading == NV2A_SHADE_DIFFUSE) {
            Nv2aVertex *vertices = dkMemBlockGetCpuAddr(s_data_mem);
            for (uint32_t i = 0; i < b->count; i++) vertices[i].u = vertices[i].v = 0;
        }
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(s_data_mem) + colour_at, b->diffuse_float, colour_bytes);
        const Nv2aTexture *t = b->shading == NV2A_SHADE_MODULATE ? b->texture : NULL;
        if (t && !texture_ok(t)) goto done;
        float settings[12] = {w, h, 0, 0, t ? t->width : 1, t ? t->height : 1,
                             b->state->alpha_test_enable, b->state->alpha_ref, t ? 1 : 2,
                             t && t->filter == MENU_FILTER};
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(s_data_mem) + uniform_at, settings, sizeof settings);
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
            if (!ensure(&s_tex_mem, &s_tex_cap, aligned(dkImageLayoutGetSize(&tl), dkImageLayoutGetAlignment(&tl)),
                        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
                || !ensure(&s_tex_up, &s_up_cap, tex_bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached)
                || !ensure(&s_desc_mem, &s_desc_cap, 4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) goto done;
            if (s_scratch_cap < tex_bytes) {
                uint32_t *grown = realloc(s_scratch, tex_bytes);
                if (!grown) goto done;
                s_scratch = grown;
                s_scratch_cap = tex_bytes;
            }
            DkImage ti;
            dkImageInitialize(&ti, &tl, s_tex_mem, 0);
            DkImageView tv;
            dkImageViewDefaults(&tv, &ti);
            /* Decode and swap in ordinary memory; the uncached upload buffer is only written, in bulk. */
            const uint64_t t_tex = armGetSystemTick();
            if (!nv2a_backend_decode_texture(t, s_scratch)) goto done;
            for (uint32_t i = 0; i < t->width * t->height; i++) s_scratch[i] = swap_rb(s_scratch[i]);
            memcpy(dkMemBlockGetCpuAddr(s_tex_up), s_scratch, tex_bytes);
            s_time.tex += armGetSystemTick() - t_tex;
            DkCopyBuf source = {dkMemBlockGetGpuAddr(s_tex_up), 0, 0};
            DkImageRect tr = {0, 0, 0, t->width, t->height, 1};
            dkCmdBufCopyBufferToImage(cmd, &source, &tv, &tr, 0);
            DkSampler sampler;
            dkSamplerDefaults(&sampler);
            sampler.wrapMode[0] = sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
            if (t->filter == MENU_FILTER) sampler.minFilter = sampler.magFilter = DkFilter_Linear;
            dkImageDescriptorInitialize(dkMemBlockGetCpuAddr(s_desc_mem), &tv, false, false);
            dkSamplerDescriptorInitialize((DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(s_desc_mem) + 256), &sampler);
            dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
            dkCmdBufBindImageDescriptorSet(cmd, dkMemBlockGetGpuAddr(s_desc_mem), 1);
            dkCmdBufBindSamplerDescriptorSet(cmd, dkMemBlockGetGpuAddr(s_desc_mem) + 256, 1);
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
        DkBufExtents vb[] = {{dkMemBlockGetGpuAddr(s_data_mem), vertex_bytes},
            {dkMemBlockGetGpuAddr(s_data_mem) + colour_at, colour_bytes}};
        DkBufExtents ub = {dkMemBlockGetGpuAddr(s_data_mem) + uniform_at, 256};
        dkCmdBufBindVtxAttribState(cmd, attrs, 3);
        dkCmdBufBindVtxBufferState(cmd, vbs, 2);
        dkCmdBufBindVtxBuffers(cmd, 0, vb, 2);
        dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, &ub, 1);
        dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, &ub, 1);
        dkCmdBufDraw(cmd, DkPrimitive_Triangles, b->count, 1, 0, 0);
    }
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &transfer, 0);
    DkCmdList list = dkCmdBufFinishList(cmd);
    const uint64_t t_submit = armGetSystemTick();
    dkQueueSubmitCommands(s_queue, list); dkQueueFlush(s_queue); dkQueueWaitIdle(s_queue);
    t_after_gpu = armGetSystemTick();
    if (dkQueueIsInErrorState(s_queue)) goto done;
    dkMemBlockFlushCpuCache(s_read_mem, 0, rbytes);     /* drop stale cache lines: the GPU wrote this memory */
    for (uint32_t y = y0; y < y1; y++) {
        const uint32_t *src = pixels + (size_t)(y - y0) * rw;
        uint8_t *dst = guest + s->color_va + (size_t)y * s->pitch + (size_t)x0 * 4;
        for (uint32_t x = 0; x < rw; x++) {
            uint32_t argb = swap_rb(src[x]);
            memcpy(dst + (size_t)x * 4, &argb, 4);
        }
    }
    ok = 1;
    {
        const uint64_t t_end = armGetSystemTick();
        s_time.prep += t_after_prep - t_start;
        s_time.build += t_submit - t_after_prep;
        s_time.gpu += t_after_gpu - t_submit;
        s_time.readback += t_end - t_after_gpu;
        s_time.pixels += rw * rh;
        if (b) s_time.draws++; else s_time.clears++;
    }
done:
    if (cmd) dkCmdBufDestroy(cmd);
    if (!ok) fail_closed();
}

/* MANHUNT_DEKO_VERIFY=1: run every call twice from the same starting pixels, once with the
 * optimised settings and once with the conservative ones (whole surface, uncached buffer), and
 * compare the two results exactly. Slow (diagnostic): it proves the optimisation does not change
 * the pixels, independent of any animation. The conservative result is what stays in guest RAM. */
static int s_verify;
static uint8_t *s_vsave, *s_vresult;
static size_t s_vcap;
static struct { uint64_t calls, bad_calls, bad_pixels; uint32_t worst; } s_vstat;

static void execute(const Nv2aSurface *s, const Nv2aBatch *b, uint32_t clear_argb)
{
    if (!s_verify) { execute_with(s, b, clear_argb, s_use_rect, s_want_cached); return; }
    uint8_t *guest = (uint8_t *)xbox_GetMemoryOffset();
    size_t row = (size_t)s->width * 4, bytes = row * s->height;
    if (s_vcap < bytes) {
        free(s_vsave); free(s_vresult);
        s_vsave = malloc(bytes); s_vresult = malloc(bytes);
        s_vcap = (s_vsave && s_vresult) ? bytes : 0;
    }
    if (!s_vcap) { execute_with(s, b, clear_argb, s_use_rect, s_want_cached); return; }
    for (uint32_t y = 0; y < s->height; y++) memcpy(s_vsave + y * row, guest + s->color_va + (size_t)y * s->pitch, row);
    execute_with(s, b, clear_argb, 1, 1);                                  /* optimised */
    for (uint32_t y = 0; y < s->height; y++) memcpy(s_vresult + y * row, guest + s->color_va + (size_t)y * s->pitch, row);
    for (uint32_t y = 0; y < s->height; y++) memcpy(guest + s->color_va + (size_t)y * s->pitch, s_vsave + y * row, row);
    execute_with(s, b, clear_argb, 0, 0);                                  /* conservative */
    uint64_t bad = 0;
    uint32_t worst = 0;
    for (uint32_t y = 0; y < s->height; y++) {
        const uint8_t *a = s_vresult + y * row, *c = guest + s->color_va + (size_t)y * s->pitch;
        for (uint32_t x = 0; x < s->width; x++) {
            uint32_t pa, pc;
            memcpy(&pa, a + (size_t)x * 4, 4);
            memcpy(&pc, c + (size_t)x * 4, 4);
            if (pa != pc) {
                bad++;
                for (int k = 0; k < 4; k++) {
                    int d = (int)((pa >> (8 * k)) & 255) - (int)((pc >> (8 * k)) & 255);
                    if (d < 0) d = -d;
                    if ((uint32_t)d > worst) worst = (uint32_t)d;
                }
            }
        }
    }
    s_vstat.calls++;
    if (bad) { s_vstat.bad_calls++; s_vstat.bad_pixels += bad; if (worst > s_vstat.worst) s_vstat.worst = worst; }
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
        printf("[MH-DEKO-V] verified %llu calls: %llu differ, %llu pixels differ in total, largest channel difference %u (optimised vs conservative path)\n",
            (unsigned long long)s_vstat.calls, (unsigned long long)s_vstat.bad_calls,
            (unsigned long long)s_vstat.bad_pixels, s_vstat.worst);
    if (s_flips % 30 == 0 && (s_time.draws + s_time.clears)) {
        uint32_t n = s_time.draws + s_time.clears;
        printf("[MH-DEKO-T] last 30 flips: %u draws %u clears; per call us: upload %llu build %llu gpu %llu readback %llu; textures total ms %llu; avg pixels %u (cached=%d rect=%d)\n",
            s_time.draws, s_time.clears,
            (unsigned long long)(ticks_to_us(s_time.prep) / n), (unsigned long long)(ticks_to_us(s_time.build) / n),
            (unsigned long long)(ticks_to_us(s_time.gpu) / n), (unsigned long long)(ticks_to_us(s_time.readback) / n),
            (unsigned long long)(ticks_to_us(s_time.tex) / 1000), s_time.pixels / n, s_read_cached, s_use_rect);
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
    .can_draw_transformed = can_draw_transformed, .wants_float_diffuse = 1
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
