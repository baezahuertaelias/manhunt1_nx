// Synthetic Nv2aVertex upload/coordinate/interpolation proof; no game inputs.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <deko3d.h>
#include "nv2a_backend.h"

#define CODE_SIZE 65536u
#define CMD_SIZE 16384u
#define CLEAR_RGBA 0xFF110B07u
#define TOLERANCE 3

_Static_assert(sizeof(Nv2aVertex) == 28, "upstream vertex layout changed");
_Static_assert(offsetof(Nv2aVertex, diffuse) == 16, "upstream colour layout changed");
_Static_assert(offsetof(Nv2aVertex, u) == 20, "upstream UV layout changed");

static void report(const char *s)
{
    printf("%s\n", s);
    svcOutputDebugString(s, strlen(s));
}

static uint32_t align_size(uint32_t size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

static DkMemBlock memory(DkDevice device, uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, align_size(size, DK_MEMBLOCK_ALIGNMENT));
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
    uint32_t padded = align_size((uint32_t)size, DK_SHADER_CODE_ALIGNMENT);
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

static void barycentric(const Nv2aVertex v[3], double x, double y, double b[3])
{
    double area = (v[1].y - v[2].y) * (v[0].x - v[2].x)
                + (v[2].x - v[1].x) * (v[0].y - v[2].y);
    b[0] = ((v[1].y - v[2].y) * (x - v[2].x)
          + (v[2].x - v[1].x) * (y - v[2].y)) / area;
    b[1] = ((v[2].y - v[0].y) * (x - v[2].x)
          + (v[0].x - v[2].x) * (y - v[2].y)) / area;
    b[2] = 1.0 - b[0] - b[1];
}

// Independent screen-space reference: weights = barycentric * rhw / their sum.
static void expected(const Nv2aVertex v[3], const double b[3], int perspective, int rgba[4])
{
    double sum = 0, colour[4] = {0};
    const unsigned shifts[4] = {16, 8, 0, 24};
    for (int i = 0; i < 3; i++) {
        double weight = b[i] * (perspective ? v[i].rhw : 1.0);
        double c[4];
        for (int k = 0; k < 4; k++) c[k] = ((v[i].diffuse >> shifts[k]) & 255) / 255.0;
        c[0] = 0.75 * c[0] + 0.25 * v[i].u;
        c[1] = 0.75 * c[1] + 0.25 * v[i].v;
        for (int k = 0; k < 4; k++) colour[k] += weight * c[k];
        sum += weight;
    }
    for (int k = 0; k < 4; k++) rgba[k] = (int)floor(colour[k] / sum * 255 + 0.5);
}

static int verify(const uint32_t *pixels, uint32_t w, uint32_t h,
                  const Nv2aVertex v[3], char *msg, size_t n)
{
    unsigned interior = 0, outside = 0, bad = 0, distinguishes_affine = 0;
    int max_error = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            double b[3];
            barycentric(v, x + 0.5, y + 0.5, b);
            double min_b = fmin(b[0], fmin(b[1], b[2]));
            uint32_t pixel = pixels[y * w + x];
            if (min_b < -0.03) {
                outside++;
                bad += pixel != CLEAR_RGBA;
            } else if (min_b > 0.03) {
                int want[4], affine[4], differs = 0;
                expected(v, b, 1, want);
                expected(v, b, 0, affine);
                interior++;
                for (int k = 0; k < 4; k++) {
                    int err = abs((int)((pixel >> (k * 8)) & 255) - want[k]);
                    if (err > max_error) max_error = err;
                    bad += err > TOLERANCE;
                    if (abs(want[k] - affine[k]) > 4 * TOLERANCE) differs = 1;
                }
                distinguishes_affine += differs;
            }
        }
    }
    int pass = interior > w * h / 8 && outside > w * h / 3
            && distinguishes_affine > interior / 2 && bad == 0;
    snprintf(msg, n, "NV2A-BACKEND-CASE %s size=%lux%lu interior=%u outside=%u affine_distinct=%u bad=%u max_error=%d",
             pass ? "PASS" : "FAIL", (unsigned long)w, (unsigned long)h,
             interior, outside, distinguishes_affine, bad, max_error);
    return pass;
}

// Active state outside this proof's mapping subset is rejected.
typedef struct {
    const char *name;
    uint32_t wrap, alpha, blend, src, dst, equation, mask;
} StateCase;
static const StateCase state_cases[] = {
    {"clamp", 3, 0, 0, 0, 0, 0, 0x01010101},
    {"wrap", 1, 0, 0, 0, 0, 0, 0x01010101},
    {"alpha-gt128", 1, 1, 0, 0, 0, 0, 0x01010101},
    {"blend-add", 1, 0, 1, 0x302, 0x303, 0x8006, 0x01010101},
    {"blend-reverse", 3, 0, 1, 1, 1, 0x800B, 0x01010101},
    {"mask-r", 3, 0, 0, 0, 0, 0, 0x00010000},
    {"mask-g", 3, 0, 0, 0, 0, 0, 0x00000100},
    {"mask-b", 3, 0, 0, 0, 0, 0, 0x00000001},
    {"mask-a", 3, 0, 0, 0, 0, 0, 0x01000000},
    {"alpha-blend-rg", 1, 1, 1, 0x302, 0x303, 0x8006, 0x00010100},
};
// Already-decoded synthetic RGBA8 texels, row-major. Alpha straddles ref 128.
static const uint8_t texels[4][4] = {
    {40, 80, 180, 127}, {180, 20, 90, 128},
    {70, 210, 25, 129}, {220, 130, 60, 255},
};
static const unsigned nv_masks[4] = {0x00010000, 0x00000100, 0x00000001, 0x01000000};
#define STATE_CLEAR 0xE6D2965Au

static int factor(uint32_t nv, DkBlendFactor *dk)
{
    switch (nv) {
    case 0: *dk = DkBlendFactor_Zero; return 1;
    case 1: *dk = DkBlendFactor_One; return 1;
    case 0x302: *dk = DkBlendFactor_SrcAlpha; return 1;
    case 0x303: *dk = DkBlendFactor_InvSrcAlpha; return 1;
    default: return 0;
    }
}

static int map_state(const Nv2aTexture *texture, const Nv2aRenderState *rs,
                     DkSampler *sampler, DkColorState *colour,
                     DkColorWriteState *mask, DkBlendState *blend)
{
    if (!texture->width || !texture->height || texture->levels != 1 || texture->filter != 0
        || (texture->addr_u != 1 && texture->addr_u != 3)
        || (texture->addr_v != 1 && texture->addr_v != 3)
        || (rs->color_mask & ~0x01010101u) || rs->depth_test_enable || rs->depth_write
        || rs->stencil_enable || rs->cull_enable
        || (rs->alpha_test_enable && (rs->alpha_func != 0x204 || rs->alpha_ref > 255)))
        return 0;
    dkSamplerDefaults(sampler);
    sampler->wrapMode[0] = texture->addr_u == 1 ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
    sampler->wrapMode[1] = texture->addr_v == 1 ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
    dkColorStateDefaults(colour);
    dkColorWriteStateDefaults(mask);
    unsigned bits = 0;
    for (unsigned i = 0; i < 4; i++) if (rs->color_mask & nv_masks[i]) bits |= 1u << i;
    dkColorWriteStateSetMask(mask, 0, bits);
    dkBlendStateDefaults(blend);
    if (rs->blend_enable) {
        DkBlendFactor src, dst;
        if (!factor(rs->blend_src, &src) || !factor(rs->blend_dst, &dst)) return 0;
        DkBlendOp op;
        if (rs->blend_eq == 0x8006) op = DkBlendOp_Add;
        else if (rs->blend_eq == 0x800B) op = DkBlendOp_RevSub;
        else return 0;
        dkColorStateSetBlendEnable(colour, 0, true);
        dkBlendStateSetOps(blend, op, op);
        dkBlendStateSetFactors(blend, src, dst, src, dst);
    }
    return 1;
}

static int rejection_checks(void)
{
    Nv2aTexture texture = {.width=2, .height=2, .addr_u=3, .addr_v=3, .levels=1};
    Nv2aRenderState rs = {.color_mask=0x01010101};
    DkSampler sampler; DkColorState colour; DkColorWriteState mask; DkBlendState blend;
    if (!map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    texture.addr_u = 2;
    if (map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    texture.addr_u = 3; rs.alpha_test_enable = 1; rs.alpha_func = 0x201;
    if (map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    rs.alpha_test_enable = 0; rs.blend_enable = 1; rs.blend_src = 0x304;
    rs.blend_dst = 1; rs.blend_eq = 0x8006;
    if (map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    rs.blend_src = 1; rs.blend_eq = 0x8008;
    if (map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    rs.blend_enable = 0; rs.color_mask = 2;
    if (map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) return 0;
    return 1;
}

static int verify_state(const uint32_t *pixels, uint32_t w, uint32_t h,
                        const Nv2aVertex v[3], const StateCase *test, char *msg, size_t n)
{
    unsigned checked = 0, bad = 0, discarded = 0, kept = 0, seen[4] = {0};
    int max_error = 0;
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
        double b[3];
        barycentric(v, x + .5, y + .5, b);
        double min_b = fmin(b[0], fmin(b[1], b[2]));
        uint32_t pixel = pixels[y * w + x];
        if (min_b < -.03) { checked++; bad += pixel != STATE_CLEAR; continue; }
        if (min_b <= .03) continue;
        double u = 0, vv = 0, sum = 0;
        for (int i = 0; i < 3; i++) {
            double weight = b[i] * v[i].rhw;
            u += weight * v[i].u; vv += weight * v[i].v; sum += weight;
        }
        u /= sum; vv /= sum;
        // Nearest sample boundaries are excluded, independently of triangle edges.
        double fu = u - floor(u), fv = vv - floor(vv);
        if (fu < .02 || fu > .98 || fv < .02 || fv > .98) continue;
        int tx = (int)floor(u), ty = (int)floor(vv);
        if (test->wrap == 1) { tx = ((tx % 2) + 2) % 2; ty = ((ty % 2) + 2) % 2; }
        else { tx = tx < 0 ? 0 : tx > 1 ? 1 : tx; ty = ty < 0 ? 0 : ty > 1 ? 1 : ty; }
        const uint8_t *src = texels[ty * 2 + tx];
        seen[ty * 2 + tx]++;
        int reject = test->alpha && src[3] <= 128;
        if (reject) discarded++; else kept++;
        checked++;
        for (int k = 0; k < 4; k++) {
            int want = (STATE_CLEAR >> (8 * k)) & 255;
            if (!reject && (test->mask & nv_masks[k])) {
                double out = src[k];
                if (test->blend) {
                    if (test->equation == 0x800B) out = want - out;
                    else out = out * (src[3] / 255.0) + want * (1.0 - src[3] / 255.0);
                }
                out = fmax(0, fmin(255, out));
                want = (int)floor(out + .5);
            }
            int err = abs((int)((pixel >> (8 * k)) & 255) - want);
            if (err > max_error) max_error = err;
            bad += err > TOLERANCE;
        }
    }
    int pass = checked > w * h / 2 && bad == 0 && kept > 100;
    for (int i = 0; i < 4; i++) pass &= seen[i] > 50;
    if (test->alpha) pass &= discarded > 100;
    snprintf(msg, n, "NV2A-STATE-CASE %s name=%s checked=%u kept=%u discarded=%u texels=%u/%u/%u/%u bad=%u max_error=%d",
             pass ? "PASS" : "FAIL", test->name, checked, kept, discarded,
             seen[0], seen[1], seen[2], seen[3], bad, max_error);
    return pass;
}

static int run_case(DkDevice device, uint32_t w, uint32_t h, const StateCase *test, char *msg, size_t n)
{
    int pass = 0;
    DkQueue queue = NULL;
    DkCmdBuf cmd = NULL;
    DkMemBlock image_mem = NULL, code_mem = NULL, read_mem = NULL, cmd_mem = NULL, data_mem = NULL;
    DkMemBlock tex_mem = NULL, descriptors = NULL;
    snprintf(msg, n, "NV2A-BACKEND-CASE FAIL size=%lux%lu resource/shader failure",
             (unsigned long)w, (unsigned long)h);
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = w; lm.dimensions[1] = h;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    image_mem = memory(device, align_size(dkImageLayoutGetSize(&layout),
                       dkImageLayoutGetAlignment(&layout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    code_mem = memory(device, CODE_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    read_mem = memory(device, w * h * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached);
    cmd_mem = memory(device, CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    data_mem = memory(device, 4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!image_mem || !code_mem || !read_mem || !cmd_mem || !data_mem) goto done;
    DkImage image;
    dkImageInitialize(&image, &layout, image_mem, 0);
    DkShader vs, fs;
    uint32_t offset = 0;
    if (!shader(&vs, code_mem, &offset, "romfs:/shaders/attribute_vsh.dksh") ||
        !shader(&fs, code_mem, &offset, test ? "romfs:/shaders/texture_fsh.dksh" : "romfs:/shaders/attribute_fsh.dksh")) goto done;
    Nv2aVertex vertices[3] = {
        {w * .125f, h * .125f, 1000, 1,    0x4019D23C, .1f, .2f},
        {w * .875f, h * .25f,  2000, .5f,  0xC8E62D87, .9f, .3f},
        {w * .25f,  h * .875f, 3000, .25f, 0x78285AF0, .2f, .8f},
    };
    if (test) {
        vertices[0].u = -.5f; vertices[0].v = -.5f;
        vertices[1].u = 6; vertices[1].v = -.5f;
        vertices[2].u = -.5f; vertices[2].v = 8;
    }
    memcpy(dkMemBlockGetCpuAddr(data_mem), vertices, sizeof vertices);
    // A separate aligned uniform buffer carries per-target dimensions.
    float dimensions[8] = {(float)w, (float)h, 0, 0, 2, 2, test ? (float)test->alpha : 0, 128};
    memcpy((uint8_t *)dkMemBlockGetCpuAddr(data_mem) + 256, dimensions, sizeof dimensions);
    memset(dkMemBlockGetCpuAddr(read_mem), 0xA5, w * h * 4);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, device);
    cmd = dkCmdBufCreate(&cm);
    if (!cmd) goto done;
    dkCmdBufAddMemory(cmd, cmd_mem, 0, CMD_SIZE);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkViewport viewport = {0, 0, (float)w, (float)h, 0, 1};
    DkScissor scissor = {0, 0, w, h};
    DkRasterizerState raster;
    dkRasterizerStateDefaults(&raster);
    raster.cullMode = DkFace_None;
    DkDepthStencilState depth;
    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = false; depth.depthWriteEnable = false; depth.stencilTestEnable = false;
    DkColorState colour;
    dkColorStateDefaults(&colour);
    DkColorWriteState mask;
    dkColorWriteStateDefaults(&mask);
    DkBlendState blend;
    dkBlendStateDefaults(&blend);
    Nv2aTexture texture = {.width=2, .height=2, .addr_u=test ? test->wrap : 3,
                          .addr_v=test ? test->wrap : 3, .levels=1};
    Nv2aRenderState rs = {0};
    DkImage tex_image;
    DkImageView tex_view;
    DkSampler sampler;
    if (test) {
        rs.color_mask = test->mask; rs.alpha_test_enable = test->alpha;
        rs.alpha_func = 0x204; rs.alpha_ref = 128;
        rs.blend_enable = test->blend; rs.blend_src = test->src;
        rs.blend_dst = test->dst; rs.blend_eq = test->equation;
        if (!map_state(&texture, &rs, &sampler, &colour, &mask, &blend)) goto done;
        DkImageLayoutMaker tlm;
        dkImageLayoutMakerDefaults(&tlm, device);
        tlm.flags = DkImageFlags_Usage2DEngine;
        tlm.format = DkImageFormat_RGBA8_Unorm;
        tlm.dimensions[0] = 2; tlm.dimensions[1] = 2;
        DkImageLayout tex_layout;
        dkImageLayoutInitialize(&tex_layout, &tlm);
        tex_mem = memory(device, align_size(dkImageLayoutGetSize(&tex_layout),
                         dkImageLayoutGetAlignment(&tex_layout)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        descriptors = memory(device, 4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!tex_mem || !descriptors) goto done;
        dkImageInitialize(&tex_image, &tex_layout, tex_mem, 0);
        dkImageViewDefaults(&tex_view, &tex_image);
        dkImageDescriptorInitialize(dkMemBlockGetCpuAddr(descriptors), &tex_view, false, false);
        dkSamplerDescriptorInitialize((DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(descriptors) + 256), &sampler);
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(data_mem) + 512, texels, sizeof texels);
        DkCopyBuf upload = {dkMemBlockGetGpuAddr(data_mem) + 512, 0, 0};
        DkImageRect tex_rect = {0, 0, 0, 2, 2, 1};
        dkCmdBufCopyBufferToImage(cmd, &upload, &tex_view, &tex_rect, 0);
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        dkCmdBufBindImageDescriptorSet(cmd, dkMemBlockGetGpuAddr(descriptors), 1);
        dkCmdBufBindSamplerDescriptorSet(cmd, dkMemBlockGetGpuAddr(descriptors) + 256, 1);
        DkResHandle handle = dkMakeTextureHandle(0, 0);
        dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &handle, 1);
    }
    DkShader const *shaders[] = {&vs, &fs};
    DkVtxAttribState attribs[] = {
        {.bufferId=0, .offset=offsetof(Nv2aVertex, x), .size=DkVtxAttribSize_4x32, .type=DkVtxAttribType_Float},
        {.bufferId=0, .offset=offsetof(Nv2aVertex, diffuse), .size=DkVtxAttribSize_1x32, .type=DkVtxAttribType_Uint},
        {.bufferId=0, .offset=offsetof(Nv2aVertex, u), .size=DkVtxAttribSize_2x32, .type=DkVtxAttribType_Float},
    };
    DkVtxBufferState vertex_state = {sizeof(Nv2aVertex), 0};
    DkBufExtents vb = {dkMemBlockGetGpuAddr(data_mem), sizeof vertices};
    DkBufExtents ub = {dkMemBlockGetGpuAddr(data_mem) + 256, 256};
    dkCmdBufBindRenderTarget(cmd, &view, NULL);
    dkCmdBufSetViewports(cmd, 0, &viewport, 1);
    dkCmdBufSetScissors(cmd, 0, &scissor, 1);
    dkCmdBufBindRasterizerState(cmd, &raster);
    dkCmdBufBindDepthStencilState(cmd, &depth);
    dkCmdBufBindColorState(cmd, &colour);
    dkCmdBufBindColorWriteState(cmd, &mask);
    dkCmdBufBindBlendStates(cmd, 0, &blend, 1);
    dkCmdBufBindVtxAttribState(cmd, attribs, 3);
    dkCmdBufBindVtxBufferState(cmd, &vertex_state, 1);
    dkCmdBufBindVtxBuffers(cmd, 0, &vb, 1);
    dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, &ub, 1);
    dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, &ub, 1);
    if (test) dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, 90.0f/255, 150.0f/255, 210.0f/255, 230.0f/255);
    else dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, 7.0f/255, 11.0f/255, 17.0f/255, 1);
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    DkImageRect rect = {0, 0, 0, w, h, 1};
    DkCopyBuf dest = {dkMemBlockGetGpuAddr(read_mem), 0, 0};
    dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &dest, 0);
    DkCmdList list = dkCmdBufFinishList(cmd);
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, device);
    qm.flags = DkQueueFlags_Graphics;
    queue = dkQueueCreate(&qm);
    if (!queue) goto done;
    dkQueueSubmitCommands(queue, list);
    dkQueueFlush(queue);
    dkQueueWaitIdle(queue);
    if (dkQueueIsInErrorState(queue)) goto done;
    pass = test ? verify_state(dkMemBlockGetCpuAddr(read_mem), w, h, vertices, test, msg, n)
                : verify(dkMemBlockGetCpuAddr(read_mem), w, h, vertices, msg, n);
done:
    if (queue) dkQueueDestroy(queue);
    if (cmd) dkCmdBufDestroy(cmd);
    if (descriptors) dkMemBlockDestroy(descriptors);
    if (tex_mem) dkMemBlockDestroy(tex_mem);
    if (data_mem) dkMemBlockDestroy(data_mem);
    if (cmd_mem) dkMemBlockDestroy(cmd_mem);
    if (read_mem) dkMemBlockDestroy(read_mem);
    if (code_mem) dkMemBlockDestroy(code_mem);
    if (image_mem) dkMemBlockDestroy(image_mem);
    return pass;
}

int main(void)
{
    char msg[256];
    if (R_FAILED(romfsInit())) {
        report("NV2A-BACKEND-PROOF FAIL romfsInit");
        return 1;
    }
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    DkDevice device = dkDeviceCreate(&dm);
    if (!device) {
        report("NV2A-BACKEND-PROOF FAIL device creation");
        romfsExit();
        return 1;
    }
    int a = run_case(device, 128, 96, NULL, msg, sizeof msg); report(msg);
    int b = run_case(device, 192, 128, NULL, msg, sizeof msg); report(msg);
    int states = rejection_checks();
    report(states ? "NV2A-STATE-REJECT PASS cases=5" : "NV2A-STATE-REJECT FAIL cases=5");
    for (unsigned i = 0; i < sizeof state_cases / sizeof state_cases[0]; i++) {
        int ok = run_case(device, 128, 96, &state_cases[i], msg, sizeof msg);
        report(msg);
        states &= ok;
    }
    dkDeviceDestroy(device);
    romfsExit();
    report(a && b && states ? "NV2A-BACKEND-PROOF PASS coordinate_cases=2 state_cases=10" : "NV2A-BACKEND-PROOF FAIL coordinate_cases=2 state_cases=10");
    return !(a && b && states);
}
