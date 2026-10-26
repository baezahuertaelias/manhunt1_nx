// Gate (d): minimal deko3d rendering, verified by reading pixels back.
//
// Renders a solid red triangle over a blue clear into an offscreen RGBA8
// target, copies the image to a CPU-visible buffer, and checks exact pixel
// values plus triangle coverage. Results go to the console and to
// svcOutputDebugString ("DEKO3D-PROOF PASS|FAIL ...") for scripts/emu-run.sh.
// Based on the devkitPro deko_basic example; API checked against deko3d 0.5.0.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <deko3d.h>

#define W 1280
#define H 720
#define CODEMEMSIZE (64 * 1024)
#define CMDMEMSIZE  (16 * 1024)
#define READBACK_SIZE ((W * H * 4 + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1))

#define CLEAR_RGBA 0xFFFF0000u  // R=0 G=0 B=255 A=255, little-endian RGBA8 read as uint32
#define RED_RGBA   0xFF0000FFu  // R=255 G=0 B=0 A=255

static void report(const char *s) {
    printf("%s\n", s);
    svcOutputDebugString(s, strlen(s));
}

static int loadShader(DkShader *shader, DkMemBlock code, uint32_t *offset, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    rewind(f);
    uint32_t at = *offset;
    *offset += (size + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
    if (*offset > CODEMEMSIZE || fread((uint8_t *)dkMemBlockGetCpuAddr(code) + at, size, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, code, at);
    dkShaderInitialize(shader, &maker);
    return 0;
}

// Returns 0 on PASS; writes the result line to msg.
static int renderAndVerify(char *msg, size_t size) {
    DkDeviceMaker deviceMaker;
    dkDeviceMakerDefaults(&deviceMaker);
    DkDevice device = dkDeviceCreate(&deviceMaker);

    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, device);
    layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layoutMaker.format = DkImageFormat_RGBA8_Unorm;
    layoutMaker.dimensions[0] = W;
    layoutMaker.dimensions[1] = H;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);
    uint32_t imageSize = dkImageLayoutGetSize(&layout);
    uint32_t imageAlign = dkImageLayoutGetAlignment(&layout);
    imageSize = (imageSize + imageAlign - 1) & ~(imageAlign - 1);

    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, device, (imageSize + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1));
    memMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    DkMemBlock imageMem = dkMemBlockCreate(&memMaker);
    DkImage image;
    dkImageInitialize(&image, &layout, imageMem, 0);

    dkMemBlockMakerDefaults(&memMaker, device, CODEMEMSIZE);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
    DkMemBlock codeMem = dkMemBlockCreate(&memMaker);
    uint32_t codeOffset = 0;
    DkShader vsh, fsh;
    if (loadShader(&vsh, codeMem, &codeOffset, "romfs:/shaders/triangle_vsh.dksh") ||
        loadShader(&fsh, codeMem, &codeOffset, "romfs:/shaders/red_fsh.dksh")) {
        snprintf(msg, size, "DEKO3D-PROOF FAIL cannot load shaders from romfs");
        return 1;
    }

    // Tightly packed CPU-visible buffer ({addr, 0, 0}, as in deko_console).
    dkMemBlockMakerDefaults(&memMaker, device, READBACK_SIZE);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached;
    DkMemBlock readMem = dkMemBlockCreate(&memMaker);
    memset(dkMemBlockGetCpuAddr(readMem), 0xA5, READBACK_SIZE);

    dkMemBlockMakerDefaults(&memMaker, device, CMDMEMSIZE);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    DkMemBlock cmdMem = dkMemBlockCreate(&memMaker);
    DkCmdBufMaker cmdMaker;
    dkCmdBufMakerDefaults(&cmdMaker, device);
    DkCmdBuf cmdbuf = dkCmdBufCreate(&cmdMaker);
    dkCmdBufAddMemory(cmdbuf, cmdMem, 0, CMDMEMSIZE);

    DkImageView view;
    dkImageViewDefaults(&view, &image);
    DkViewport viewport = {0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f};
    DkScissor scissor = {0, 0, W, H};
    DkShader const *shaders[] = {&vsh, &fsh};
    DkRasterizerState rasterizer;
    DkColorState color;
    DkColorWriteState colorWrite;
    dkRasterizerStateDefaults(&rasterizer);
    dkColorStateDefaults(&color);
    dkColorWriteStateDefaults(&colorWrite);

    dkCmdBufBindRenderTarget(cmdbuf, &view, NULL);
    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 1.0f, 1.0f);
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindRasterizerState(cmdbuf, &rasterizer);
    dkCmdBufBindColorState(cmdbuf, &color);
    dkCmdBufBindColorWriteState(cmdbuf, &colorWrite);
    dkCmdBufDraw(cmdbuf, DkPrimitive_Triangles, 3, 1, 0, 0);
    dkCmdBufBarrier(cmdbuf, DkBarrier_Full, DkInvalidateFlags_L2Cache);
    DkImageRect rect = {0, 0, 0, W, H, 1};
    DkCopyBuf dst = {dkMemBlockGetGpuAddr(readMem), 0, 0};
    dkCmdBufCopyImageToBuffer(cmdbuf, &view, &rect, &dst, 0);
    DkCmdList list = dkCmdBufFinishList(cmdbuf);

    DkQueueMaker queueMaker;
    dkQueueMakerDefaults(&queueMaker, device);
    queueMaker.flags = DkQueueFlags_Graphics;
    DkQueue queue = dkQueueCreate(&queueMaker);
    dkQueueSubmitCommands(queue, list);
    dkQueueFlush(queue);
    dkQueueWaitIdle(queue);

    // Triangle (0,+1), (-1,-1), (+1,-1) in NDC covers half the target.
    const uint32_t *px = dkMemBlockGetCpuAddr(readMem);
    uint32_t center = px[(H / 2) * W + W / 2];   // inside the triangle
    uint32_t corner = px[0];                      // outside it
    uint32_t red = 0, clear = 0, other = 0;
    for (uint32_t i = 0; i < W * H; i++) {
        if (px[i] == RED_RGBA) red++;
        else if (px[i] == CLEAR_RGBA) clear++;
        else other++;
    }
    double coverage = 100.0 * red / (W * H);
    int pass = center == RED_RGBA && corner == CLEAR_RGBA && other == 0 && coverage > 49.5 && coverage < 50.5;
    snprintf(msg, size, "DEKO3D-PROOF %s center=%08lx corner=%08lx red=%lu clear=%lu other=%lu coverage=%.3f%%",
             pass ? "PASS" : "FAIL", (unsigned long)center, (unsigned long)corner,
             (unsigned long)red, (unsigned long)clear, (unsigned long)other, coverage);

    dkQueueDestroy(queue);
    dkCmdBufDestroy(cmdbuf);
    dkMemBlockDestroy(cmdMem);
    dkMemBlockDestroy(readMem);
    dkMemBlockDestroy(codeMem);
    dkMemBlockDestroy(imageMem);
    dkDeviceDestroy(device);
    return !pass;
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char msg[256];
    if (R_FAILED(romfsInit())) {
        snprintf(msg, sizeof msg, "DEKO3D-PROOF FAIL romfsInit");
    } else {
        renderAndVerify(msg, sizeof msg);
        romfsExit();
    }
    report(msg);
    consoleUpdate(NULL);

    for (int frame = 0; frame < 600 && appletMainLoop(); frame++) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
