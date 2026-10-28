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

static int run_case(uint32_t w, uint32_t h, char *msg, size_t n)
{
    int pass = 0;
    DkDevice device = NULL;
    DkQueue queue = NULL;
    DkCmdBuf cmd = NULL;
    DkMemBlock image_mem = NULL, code_mem = NULL, read_mem = NULL, cmd_mem = NULL, data_mem = NULL;
    snprintf(msg, n, "NV2A-BACKEND-CASE FAIL size=%lux%lu resource/shader failure",
             (unsigned long)w, (unsigned long)h);
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    device = dkDeviceCreate(&dm);
    if (!device) goto done;
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
        !shader(&fs, code_mem, &offset, "romfs:/shaders/attribute_fsh.dksh")) goto done;
    Nv2aVertex vertices[3] = {
        {w * .125f, h * .125f, 1000, 1,    0x4019D23C, .1f, .2f},
        {w * .875f, h * .25f,  2000, .5f,  0xC8E62D87, .9f, .3f},
        {w * .25f,  h * .875f, 3000, .25f, 0x78285AF0, .2f, .8f},
    };
    memcpy(dkMemBlockGetCpuAddr(data_mem), vertices, sizeof vertices);
    // A separate aligned uniform buffer carries per-target dimensions.
    float dimensions[4] = {(float)w, (float)h, 0, 0};
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
    dkCmdBufBindVtxAttribState(cmd, attribs, 3);
    dkCmdBufBindVtxBufferState(cmd, &vertex_state, 1);
    dkCmdBufBindVtxBuffers(cmd, 0, &vb, 1);
    dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, &ub, 1);
    dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, 7.0f/255, 11.0f/255, 17.0f/255, 1);
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
    pass = verify(dkMemBlockGetCpuAddr(read_mem), w, h, vertices, msg, n);
done:
    if (queue) dkQueueDestroy(queue);
    if (cmd) dkCmdBufDestroy(cmd);
    if (data_mem) dkMemBlockDestroy(data_mem);
    if (cmd_mem) dkMemBlockDestroy(cmd_mem);
    if (read_mem) dkMemBlockDestroy(read_mem);
    if (code_mem) dkMemBlockDestroy(code_mem);
    if (image_mem) dkMemBlockDestroy(image_mem);
    if (device) dkDeviceDestroy(device);
    return pass;
}

int main(void)
{
    char msg[256];
    if (R_FAILED(romfsInit())) {
        report("NV2A-BACKEND-PROOF FAIL romfsInit");
        return 1;
    }
    int a = run_case(128, 96, msg, sizeof msg); report(msg);
    int b = run_case(192, 128, msg, sizeof msg); report(msg);
    romfsExit();
    report(a && b ? "NV2A-BACKEND-PROOF PASS cases=2" : "NV2A-BACKEND-PROOF FAIL cases=2");
    return !(a && b);
}
