/* A software "GPU" behind the real deko3d.h (exact types, enums and inline helpers), so the REAL
 * game/src/nv2a_deko3d.c can run on the host. Commands are recorded and executed at submit. The
 * draw is not a rasteriser: for every triangle it changes each pixel of (triangle bounding box
 * intersected with the scissor and the image) as a function of the pixel's old value, the colour,
 * the first texel of the bound texture and the triangle index. That is enough to expose a stale
 * image, a wrong rectangle, a missed upload or a missed readback. It does NOT model the CPU cache,
 * GPU timing, tiling or any real GPU behaviour. */
#include <deko3d.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct tag_DkDevice { int unused; };
struct tag_DkQueue { int unused; };
struct tag_DkMemBlock { uint8_t *mem; uint32_t size; uint32_t flags; };
typedef struct { uint32_t w, h; } LayoutPriv;
typedef struct { uint8_t *base; uint32_t w, h; } ImagePriv;

enum { C_B2I, C_I2B, C_RT, C_SCISSOR, C_CLEAR, C_VTXBUF, C_VTXSTATE, C_DESC, C_TEX, C_DRAW };
typedef struct {
    int type;
    DkCopyBuf cb;
    DkImageRect rect;
    ImagePriv img;                 /* copied at record time, like the real command words */
    DkScissor sc;
    float color[4];
    DkBufExtents vb[2];
    uint32_t stride[2];
    DkGpuAddr desc;
    uint32_t count;
} Cmd;
struct tag_DkCmdBuf { Cmd *c; int n, cap; };

static ImagePriv img_of(DkImageView const *v) { ImagePriv p; memcpy(&p, ((DkImage *)v->pImage)->_storage, sizeof p); return p; }
static void push(DkCmdBuf o, Cmd c)
{
    if (o->n == o->cap) { o->cap = o->cap ? o->cap * 2 : 32; o->c = realloc(o->c, (size_t)o->cap * sizeof(Cmd)); }
    o->c[o->n++] = c;
}

DkDevice dkDeviceCreate(DkDeviceMaker const *m) { (void)m; return calloc(1, sizeof(struct tag_DkDevice)); }
void dkDeviceDestroy(DkDevice o) { free(o); }
DkQueue dkQueueCreate(DkQueueMaker const *m) { (void)m; return calloc(1, sizeof(struct tag_DkQueue)); }
void dkQueueDestroy(DkQueue o) { free(o); }
bool dkQueueIsInErrorState(DkQueue o) { (void)o; return false; }
void dkQueueFlush(DkQueue o) { (void)o; }
void dkQueueWaitIdle(DkQueue o) { (void)o; }

DkMemBlock dkMemBlockCreate(DkMemBlockMaker const *m)
{
    DkMemBlock b = calloc(1, sizeof *b);
    if (posix_memalign((void **)&b->mem, 4096, m->size)) { free(b); return NULL; }
    memset(b->mem, 0xCD, m->size);                 /* uninitialised memory must never be relied on */
    b->size = m->size;
    b->flags = m->flags;
    return b;
}
void dkMemBlockDestroy(DkMemBlock b) { if (b) { memset(b->mem, 0xDD, b->size); free(b->mem); free(b); } }
void *dkMemBlockGetCpuAddr(DkMemBlock b) { return b->mem; }
DkGpuAddr dkMemBlockGetGpuAddr(DkMemBlock b) { return (DkGpuAddr)(uintptr_t)b->mem; }
uint32_t dkMemBlockGetSize(DkMemBlock b) { return b->size; }
DkResult dkMemBlockFlushCpuCache(DkMemBlock b, uint32_t o, uint32_t s) { (void)b; (void)o; (void)s; return DkResult_Success; }

void dkShaderInitialize(DkShader *o, DkShaderMaker const *m) { (void)m; memset(o, 0, sizeof *o); }
void dkImageLayoutInitialize(DkImageLayout *o, DkImageLayoutMaker const *m)
{
    LayoutPriv *l = (LayoutPriv *)o->_storage;
    l->w = m->dimensions[0]; l->h = m->dimensions[1];
}
uint64_t dkImageLayoutGetSize(DkImageLayout const *o) { const LayoutPriv *l = (const LayoutPriv *)o->_storage; return (uint64_t)l->w * l->h * 4; }
uint32_t dkImageLayoutGetAlignment(DkImageLayout const *o) { (void)o; return 4096; }
void dkImageInitialize(DkImage *o, DkImageLayout const *l, DkMemBlock b, uint32_t off)
{
    const LayoutPriv *lp = (const LayoutPriv *)l->_storage;
    ImagePriv *ip = (ImagePriv *)o->_storage;
    ip->base = b->mem + off; ip->w = lp->w; ip->h = lp->h;
}
void dkImageDescriptorInitialize(DkImageDescriptor *o, DkImageView const *v, bool a, bool b)
{
    (void)a; (void)b;
    const ImagePriv p = img_of(v);          /* the descriptor holds the image information itself */
    memcpy(o->_storage, &p, sizeof p);
}
void dkSamplerDescriptorInitialize(DkSamplerDescriptor *o, DkSampler const *s) { (void)o; (void)s; }

DkCmdBuf dkCmdBufCreate(DkCmdBufMaker const *m) { (void)m; return calloc(1, sizeof(struct tag_DkCmdBuf)); }
void dkCmdBufDestroy(DkCmdBuf o) { free(o->c); free(o); }
void dkCmdBufAddMemory(DkCmdBuf o, DkMemBlock m, uint32_t off, uint32_t s) { (void)o; (void)m; (void)off; (void)s; }
void dkCmdBufBarrier(DkCmdBuf o, DkBarrier m, uint32_t f) { (void)o; (void)m; (void)f; }
void dkCmdBufBindShaders(DkCmdBuf o, uint32_t m, DkShader const *const s[], uint32_t n) { (void)o; (void)m; (void)s; (void)n; }
void dkCmdBufBindUniformBuffers(DkCmdBuf o, DkStage st, uint32_t f, DkBufExtents const b[], uint32_t n) { (void)o; (void)st; (void)f; (void)b; (void)n; }
void dkCmdBufBindTextures(DkCmdBuf o, DkStage st, uint32_t f, DkResHandle const h[], uint32_t n) { (void)st; (void)f; (void)h; (void)n; Cmd c = {.type = C_TEX}; push(o, c); }
void dkCmdBufBindImageDescriptorSet(DkCmdBuf o, DkGpuAddr a, uint32_t n) { (void)n; Cmd c = {.type = C_DESC, .desc = a}; push(o, c); }
void dkCmdBufBindSamplerDescriptorSet(DkCmdBuf o, DkGpuAddr a, uint32_t n) { (void)o; (void)a; (void)n; }
void dkCmdBufBindRenderTargets(DkCmdBuf o, DkImageView const *const t[], uint32_t n, DkImageView const *d)
{ (void)n; (void)d; Cmd c = {.type = C_RT, .img = img_of(t[0])}; push(o, c); }
void dkCmdBufBindRasterizerState(DkCmdBuf o, DkRasterizerState const *s) { (void)o; (void)s; }
void dkCmdBufBindColorState(DkCmdBuf o, DkColorState const *s) { (void)o; (void)s; }
void dkCmdBufBindColorWriteState(DkCmdBuf o, DkColorWriteState const *s) { (void)o; (void)s; }
void dkCmdBufBindBlendStates(DkCmdBuf o, uint32_t f, DkBlendState const s[], uint32_t n) { (void)o; (void)f; (void)s; (void)n; }
void dkCmdBufBindDepthStencilState(DkCmdBuf o, DkDepthStencilState const *s) { (void)o; (void)s; }
void dkCmdBufBindVtxAttribState(DkCmdBuf o, DkVtxAttribState const a[], uint32_t n) { (void)o; (void)a; (void)n; }
void dkCmdBufBindVtxBufferState(DkCmdBuf o, DkVtxBufferState const b[], uint32_t n)
{ Cmd c = {.type = C_VTXSTATE}; for (uint32_t i = 0; i < n && i < 2; i++) c.stride[i] = b[i].stride; push(o, c); }
void dkCmdBufBindVtxBuffers(DkCmdBuf o, uint32_t f, DkBufExtents const b[], uint32_t n)
{ (void)f; Cmd c = {.type = C_VTXBUF}; for (uint32_t i = 0; i < n && i < 2; i++) c.vb[i] = b[i]; push(o, c); }
void dkCmdBufSetViewports(DkCmdBuf o, uint32_t f, DkViewport const v[], uint32_t n) { (void)o; (void)f; (void)v; (void)n; }
void dkCmdBufSetScissors(DkCmdBuf o, uint32_t f, DkScissor const s[], uint32_t n) { (void)f; (void)n; Cmd c = {.type = C_SCISSOR, .sc = s[0]}; push(o, c); }
void dkCmdBufClearColor(DkCmdBuf o, uint32_t t, uint32_t m, const void *d) { (void)t; (void)m; Cmd c = {.type = C_CLEAR}; memcpy(c.color, d, 16); push(o, c); }
void dkCmdBufCopyBufferToImage(DkCmdBuf o, DkCopyBuf const *s, DkImageView const *d, DkImageRect const *r, uint32_t f)
{ (void)f; Cmd c = {.type = C_B2I, .cb = *s, .rect = *r, .img = img_of(d)}; push(o, c); }
void dkCmdBufCopyImageToBuffer(DkCmdBuf o, DkImageView const *s, DkImageRect const *r, DkCopyBuf const *d, uint32_t f)
{ (void)f; Cmd c = {.type = C_I2B, .cb = *d, .rect = *r, .img = img_of(s)}; push(o, c); }
void dkCmdBufDraw(DkCmdBuf o, DkPrimitive p, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi)
{ (void)p; (void)ic; (void)fv; (void)fi; Cmd c = {.type = C_DRAW, .count = vc}; push(o, c); }
DkCmdList dkCmdBufFinishList(DkCmdBuf o) { return (DkCmdList)(uintptr_t)o; }

static uint8_t clampb(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

void dkQueueSubmitCommands(DkQueue q, DkCmdList list)
{
    (void)q;
    DkCmdBuf cb = (DkCmdBuf)(uintptr_t)list;
    ImagePriv rt_img = {0}, tex_img = {0};
    const ImagePriv *rt = NULL, *tex = NULL;
    DkScissor sc = {0, 0, 0, 0};
    DkGpuAddr desc = 0;
    int textured = 0;
    DkBufExtents vb[2] = {{0, 0}, {0, 0}};
    uint32_t stride[2] = {0, 0};
    for (int i = 0; i < cb->n; i++) {
        const Cmd *c = &cb->c[i];
        switch (c->type) {
        case C_B2I: {
            const uint32_t rw = c->cb.rowLength ? c->cb.rowLength : c->rect.width;
            for (uint32_t y = 0; y < c->rect.height; y++)
                memcpy(c->img.base + ((size_t)(c->rect.y + y) * c->img.w + c->rect.x) * 4,
                       (const uint8_t *)(uintptr_t)c->cb.addr + (size_t)y * rw * 4, (size_t)c->rect.width * 4);
            break; }
        case C_I2B: {
            const uint32_t rw = c->cb.rowLength ? c->cb.rowLength : c->rect.width;
            for (uint32_t y = 0; y < c->rect.height; y++)
                memcpy((uint8_t *)(uintptr_t)c->cb.addr + (size_t)y * rw * 4,
                       c->img.base + ((size_t)(c->rect.y + y) * c->img.w + c->rect.x) * 4, (size_t)c->rect.width * 4);
            break; }
        case C_RT: rt_img = c->img; rt = &rt_img; break;
        case C_SCISSOR: sc = c->sc; break;
        case C_VTXBUF: vb[0] = c->vb[0]; vb[1] = c->vb[1]; break;
        case C_VTXSTATE: stride[0] = c->stride[0]; stride[1] = c->stride[1]; break;
        case C_DESC: desc = c->desc; break;
        case C_TEX: textured = 1; memcpy(&tex_img, (const void *)(uintptr_t)desc, sizeof tex_img); tex = &tex_img; break;
        case C_CLEAR: {
            uint8_t px[4] = {clampb((int)(c->color[0] * 255.f + .5f)), clampb((int)(c->color[1] * 255.f + .5f)),
                             clampb((int)(c->color[2] * 255.f + .5f)), clampb((int)(c->color[3] * 255.f + .5f))};
            for (uint32_t y = sc.y; y < sc.y + sc.height && y < rt->h; y++)
                for (uint32_t x = sc.x; x < sc.x + sc.width && x < rt->w; x++)
                    memcpy(rt->base + ((size_t)y * rt->w + x) * 4, px, 4);
            break; }
        case C_DRAW: {
            for (uint32_t t = 0; t + 2 < c->count; t += 3) {
                float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
                float col[4] = {0, 0, 0, 0};
                for (uint32_t k = 0; k < 3; k++) {
                    const float *p = (const float *)(uintptr_t)(vb[0].addr + (uint64_t)(t + k) * stride[0]);
                    const float *cc = (const float *)(uintptr_t)(vb[1].addr + (uint64_t)(t + k) * stride[1]);
                    if (p[0] < minx) minx = p[0];
                    if (p[0] > maxx) maxx = p[0];
                    if (p[1] < miny) miny = p[1];
                    if (p[1] > maxy) maxy = p[1];
                    for (int m = 0; m < 4; m++) col[m] += cc[m] / 3.f;
                }
                uint8_t src[4];
                for (int m = 0; m < 4; m++) src[m] = clampb((int)(col[m] * 255.f));
                /* the texture is sampled per pixel (texel x mod w, y mod h): every texel matters */
                int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
                if (x0 < (int)sc.x) x0 = (int)sc.x;
                if (y0 < (int)sc.y) y0 = (int)sc.y;
                if (x1 > (int)(sc.x + sc.width)) x1 = (int)(sc.x + sc.width);
                if (y1 > (int)(sc.y + sc.height)) y1 = (int)(sc.y + sc.height);
                if (x1 > (int)rt->w) x1 = (int)rt->w;
                if (y1 > (int)rt->h) y1 = (int)rt->h;
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++) {
                        uint8_t *d = rt->base + ((size_t)y * rt->w + x) * 4;
                        const uint8_t *tp = (textured && tex) ? tex->base + ((size_t)((unsigned)y % tex->h) * tex->w + (unsigned)x % tex->w) * 4 : NULL;
                        for (int m = 0; m < 4; m++) {
                            int sv = tp ? src[m] * tp[m] / 255 : src[m];
                            d[m] = clampb((d[m] * 3 + sv + (int)(t / 3) + 1) / 4);
                        }
                    }
            }
            break; }
        }
    }
}
