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
    abort();
}

static void execute(const Nv2aSurface *s, const Nv2aBatch *b, uint32_t clear_argb)
{
    DkMemBlock image_mem = NULL, code_mem = NULL, read_mem = NULL, cmd_mem = NULL;
    DkMemBlock data_mem = NULL, tex_mem = NULL, tex_upload = NULL, descriptors = NULL;
    DkCmdBuf cmd = NULL;
    int ok = 0;
    uint32_t w = s->width, h = s->height, pixel_bytes = w * h * 4;
    uint8_t *guest = (uint8_t *)xbox_GetMemoryOffset();
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = w; lm.dimensions[1] = h;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    image_mem = memory(aligned(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout)),
                       DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    read_mem = memory(pixel_bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);
    cmd_mem = memory(CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!image_mem || !read_mem || !cmd_mem) goto done;
    DkImage image;
    dkImageInitialize(&image, &layout, image_mem, 0);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    uint32_t *pixels = dkMemBlockGetCpuAddr(read_mem);
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        uint32_t argb;
        memcpy(&argb, guest + s->color_va + (size_t)y * s->pitch + x * 4, 4);
        pixels[y * w + x] = swap_rb(argb);
    }
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_device);
    cmd = dkCmdBufCreate(&cm);
    if (!cmd) goto done;
    dkCmdBufAddMemory(cmd, cmd_mem, 0, CMD_SIZE);
    DkImageRect rect = {0, 0, 0, w, h, 1};
    DkCopyBuf transfer = {dkMemBlockGetGpuAddr(read_mem), 0, 0};
    dkCmdBufCopyBufferToImage(cmd, &transfer, &view, &rect, 0);
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    dkCmdBufBindRenderTarget(cmd, &view, NULL);
    DkViewport viewport = {0, 0, w, h, 0, 1};
    DkScissor scissor = {0, 0, w, h};
    dkCmdBufSetViewports(cmd, 0, &viewport, 1);
    dkCmdBufSetScissors(cmd, 0, &scissor, 1);
    if (!b) {
        dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA,
            ((clear_argb >> 16) & 255) / 255.f, ((clear_argb >> 8) & 255) / 255.f,
            (clear_argb & 255) / 255.f, (clear_argb >> 24) / 255.f);
    } else {
        if (!b->count || b->count > MAX_VERTICES || b->count % 3 || !b->diffuse_float
            || (b->shading != NV2A_SHADE_DIFFUSE && b->shading != NV2A_SHADE_MODULATE)
            || (b->shading == NV2A_SHADE_MODULATE && !b->texture)) goto done;
        for (uint32_t i = 0; i < b->count; i++) {
            const Nv2aVertex *v = &b->vertices[i];
            if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z)
                || !isfinite(v->rhw) || v->rhw <= 0
                || (b->shading == NV2A_SHADE_MODULATE
                    && (!isfinite(v->u) || !isfinite(v->v)))) goto done;
            for (unsigned k = 0; k < 4; k++) if (!isfinite(b->diffuse_float[i][k])) goto done;
        }
        uint32_t vertex_bytes = b->count * sizeof(Nv2aVertex);
        uint32_t colour_at = aligned(vertex_bytes, 16), colour_bytes = b->count * 4 * sizeof(float);
        uint32_t uniform_at = aligned(colour_at + colour_bytes, 256);
        code_mem = memory(CODE_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        data_mem = memory(uniform_at + 256, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!code_mem || !data_mem) goto done;
        memcpy(dkMemBlockGetCpuAddr(data_mem), b->vertices, vertex_bytes);
        if (b->shading == NV2A_SHADE_DIFFUSE) {
            Nv2aVertex *vertices = dkMemBlockGetCpuAddr(data_mem);
            for (uint32_t i = 0; i < b->count; i++) vertices[i].u = vertices[i].v = 0;
        }
        if (!b->diffuse_float) goto done;
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(data_mem) + colour_at, b->diffuse_float, colour_bytes);
        const Nv2aTexture *t = b->shading == NV2A_SHADE_MODULATE ? b->texture : NULL;
        if (t && !texture_ok(t)) goto done;
        float settings[12] = {w, h, 0, 0, t ? t->width : 1, t ? t->height : 1,
                             b->state->alpha_test_enable, b->state->alpha_ref, t ? 1 : 2,
                             t && t->filter == MENU_FILTER};
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(data_mem) + uniform_at, settings, sizeof settings);
        DkShader vs, fs;
        uint32_t offset = 0;
        if (!shader(&vs, code_mem, &offset, "romfs:/shaders/nv2a_attribute_vsh.dksh")
            || !shader(&fs, code_mem, &offset, t ? "romfs:/shaders/nv2a_texture_fsh.dksh"
                                                : "romfs:/shaders/nv2a_diffuse_fsh.dksh")) goto done;
        DkShader const *shaders[] = {&vs, &fs};
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Shader);
        dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
        if (t) {
            DkImageLayoutMaker tm;
            dkImageLayoutMakerDefaults(&tm, s_device);
            tm.flags = DkImageFlags_Usage2DEngine; tm.format = DkImageFormat_RGBA8_Unorm;
            tm.dimensions[0] = t->width; tm.dimensions[1] = t->height;
            DkImageLayout tl;
            dkImageLayoutInitialize(&tl, &tm);
            tex_mem = memory(aligned(dkImageLayoutGetSize(&tl), dkImageLayoutGetAlignment(&tl)),
                             DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
            tex_upload = memory(t->width * t->height * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);
            descriptors = memory(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            if (!tex_mem || !tex_upload || !descriptors) goto done;
            DkImage ti;
            dkImageInitialize(&ti, &tl, tex_mem, 0);
            DkImageView tv;
            dkImageViewDefaults(&tv, &ti);
            uint32_t *upload = dkMemBlockGetCpuAddr(tex_upload);
            if (!nv2a_backend_decode_texture(t, upload)) goto done;
            for (uint32_t i = 0; i < t->width * t->height; i++) upload[i] = swap_rb(upload[i]);
            DkCopyBuf source = {dkMemBlockGetGpuAddr(tex_upload), 0, 0};
            DkImageRect tr = {0, 0, 0, t->width, t->height, 1};
            dkCmdBufCopyBufferToImage(cmd, &source, &tv, &tr, 0);
            DkSampler sampler;
            dkSamplerDefaults(&sampler);
            sampler.wrapMode[0] = sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
            if (t->filter == MENU_FILTER) sampler.minFilter = sampler.magFilter = DkFilter_Linear;
            dkImageDescriptorInitialize(dkMemBlockGetCpuAddr(descriptors), &tv, false, false);
            dkSamplerDescriptorInitialize((DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(descriptors) + 256), &sampler);
            dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
            dkCmdBufBindImageDescriptorSet(cmd, dkMemBlockGetGpuAddr(descriptors), 1);
            dkCmdBufBindSamplerDescriptorSet(cmd, dkMemBlockGetGpuAddr(descriptors) + 256, 1);
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
        DkBufExtents vb[] = {{dkMemBlockGetGpuAddr(data_mem), vertex_bytes},
            {dkMemBlockGetGpuAddr(data_mem) + colour_at, colour_bytes}};
        DkBufExtents ub = {dkMemBlockGetGpuAddr(data_mem) + uniform_at, 256};
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
    dkQueueSubmitCommands(s_queue, list); dkQueueFlush(s_queue); dkQueueWaitIdle(s_queue);
    if (dkQueueIsInErrorState(s_queue)) goto done;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        uint32_t argb = swap_rb(pixels[y * w + x]);
        memcpy(guest + s->color_va + (size_t)y * s->pitch + x * 4, &argb, 4);
    }
    ok = 1;
done:
    if (cmd) dkCmdBufDestroy(cmd);
    if (descriptors) dkMemBlockDestroy(descriptors);
    if (tex_upload) dkMemBlockDestroy(tex_upload);
    if (tex_mem) dkMemBlockDestroy(tex_mem);
    if (data_mem) dkMemBlockDestroy(data_mem);
    if (cmd_mem) dkMemBlockDestroy(cmd_mem);
    if (read_mem) dkMemBlockDestroy(read_mem);
    if (code_mem) dkMemBlockDestroy(code_mem);
    if (image_mem) dkMemBlockDestroy(image_mem);
    if (!ok) fail_closed();
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
    return s_device != NULL;
}

const Nv2aBackend *manhunt_nv2a_deko3d_backend(void) { return &s_backend; }
void manhunt_nv2a_deko3d_stats(ManhuntNv2aDeko3dStats *out) { if (out) *out = s_stats; }
void manhunt_nv2a_deko3d_shutdown(void)
{
    if (s_queue) { dkQueueWaitIdle(s_queue); dkQueueDestroy(s_queue); s_queue = NULL; }
    if (s_device) dkDeviceDestroy(s_device);
    s_device = NULL;
}
