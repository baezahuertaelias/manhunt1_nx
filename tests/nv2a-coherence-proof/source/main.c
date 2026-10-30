// Synthetic executor-to-GPU guest-memory coherence proof; no game inputs.
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
#define MEM_BYTES 0x40000u
#define W 32u
#define H 24u
#define PITCH (W*4+16)
#define SURF_A 0x10000u
#define SURF_B 0x20000u
static uint8_t s_mem[MEM_BYTES];
static DkDevice device;
static unsigned gpu_draws,gpu_clears,cpu_rejects,flips,presents;
static uint32_t present_va,present_pixel;
static int fail;
static void report(const char *s) { printf("%s\n",s);svcOutputDebugString(s,strlen(s)); }
static void fatal(const char *s) { report(s);exit(1); }
#define CHECK(c) do { if(!(c)) {printf("FAIL line %d: %s\n",__LINE__,#c);fail=1;} } while(0)
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

static ptrdiff_t s_guest_view_bias;
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)s_mem - s_guest_view_bias; }
uint32_t g_xbox_image_lo, g_xbox_image_hi;
uint32_t g_xbox_stack_base, g_xbox_stack_size;   /* #189 made the stack placement runtime state */
uint32_t xbox_ContiguousAllocatedBytes(void) { return 0; }
void xbox_FramebufferWindowSet(uint32_t va, uint32_t pitch) { (void)va; (void)pitch; }
void xbox_FramebufferWindowSetAA(uint32_t sy) { (void)sy; }
static uint32_t s_fence_va;                           /* the mirrored fence word, if any */
uint32_t xbox_Nv2aFenceWordVa(void) { return s_fence_va; }
int nv2a_pb_dma_walk(void) { return 1; }             /* RECOMP_PB_WALK=dma: 0x0310 is latched */
void xbox_FramebufferWindowStart(void) {}
void xbox_FramebufferWindowFrameStats(uint32_t draws) { (void)draws; }
void xbox_FramebufferWindowPresent(uint32_t va, uint32_t pitch);
void xbox_Nv2aFrameCounterFlip(void) {}
/* The flip hold (nv2a_flip_hold.c, linked in). No vblank moves here, so the
 * executor never holds a flip; the walk is never inside a pushbuffer CALL. */
int nv2a_pb_scan_in_call(void) { return 0; }
uint32_t xbox_VblankCount(void) { return 0; }
uint64_t xbox_HostNowNs(void) { return 0; }
uint32_t xbox_Nv2aFrameCountersOwned(void) { return 0; }

static volatile uint32_t s_pgraph_400b10;
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset)
{
    return offset == 0x400B10u ? &s_pgraph_400b10 : NULL;
}


static uint32_t get32(uint32_t at) { uint32_t v;memcpy(&v,s_mem+at,4);return v; }
static void put32(uint32_t at,uint32_t v) {memcpy(s_mem+at,&v,4);}
static uint32_t swap_rb(uint32_t v) {return (v&0xFF00FF00u)|((v&255)<<16)|((v>>16)&255);}
extern void nv2a_pb_exec_method(uint32_t,uint32_t,uint32_t);
static void m(uint32_t a,uint32_t b) {nv2a_pb_exec_method(0,a,b);}
static void mf(uint32_t a,float f) {uint32_t u;memcpy(&u,&f,4);m(a,u);}
void xbox_FramebufferWindowPresent(uint32_t va,uint32_t pitch) {
 CHECK(va==present_va && pitch==PITCH);CHECK(get32(va+PITCH*4+4*4)==present_pixel);
 CHECK(flips==0);presents++;
}
static int surface_ok(const Nv2aSurface *s) {
 return s->width==W && s->height==H && s->pitch==PITCH && s->bytes_per_pixel==4
 && s->aa_sx==1 && s->aa_sy==1 && s->format_raw==8 && !s->clip_x && !s->clip_y
 && !s->clear_rect_set && (s->color_va==SURF_A || s->color_va==SURF_B);
}
static int state_ok(const Nv2aRenderState *r) {
 return !r->blend_enable && !r->alpha_test_enable && !r->depth_test_enable && !r->depth_write
 && !r->stencil_enable && !r->cull_enable && r->color_mask==0x01010101;
}
static int texture_ok(const Nv2aTexture *t) {
 return t && t->width==W && t->height==H && t->pitch==PITCH && t->color==0x12
 && t->levels==1 && !t->filter && t->addr_u==3 && t->addr_v==3
 && (t->offset==SURF_A || t->offset==SURF_B);
}
/* shortcut: fixture-only unit-W FFP; extend vertex/state proofs before game registration. */
static int can_draw(const Nv2aSurface*s,const Nv2aRenderState*r,const Nv2aDrawInfo*i) {
 int ok=surface_ok(s)&&state_ok(r)&&i->path==NV2A_DRAW_FFP&&!i->combiner_seen
 && i->primitive==5 && (!i->has_texcoords || !i->texture || texture_ok(i->texture));
 if(!ok) cpu_rejects++;
 return ok;
}
static int can_clear(const Nv2aSurface*s,const Nv2aRenderState*r,uint32_t flags,uint32_t argb,uint32_t zs) {
 (void)r;(void)argb;(void)zs;int ok=surface_ok(s)&&flags==0xF0;
 if(!ok) cpu_rejects++;
 return ok;
}

static void execute(const Nv2aSurface*s,const Nv2aBatch*b,uint32_t clear_argb) {
 report(b?"NV2A-COHERENCE GPU draw entry":"NV2A-COHERENCE GPU clear entry");
 DkMemBlock image_mem=NULL,code_mem=NULL,read_mem=NULL,cmd_mem=NULL,data_mem=NULL,tex_mem=NULL,desc=NULL;
 DkQueue queue=NULL;DkCmdBuf cmd=NULL;int ok=0;
 DkImageLayoutMaker lm;dkImageLayoutMakerDefaults(&lm,device);
 lm.flags=DkImageFlags_UsageRender|DkImageFlags_Usage2DEngine;lm.format=DkImageFormat_RGBA8_Unorm;
 lm.dimensions[0]=W;lm.dimensions[1]=H;DkImageLayout layout;dkImageLayoutInitialize(&layout,&lm);
 image_mem=memory(device,align_size(dkImageLayoutGetSize(&layout),dkImageLayoutGetAlignment(&layout)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
 code_mem=memory(device,CODE_SIZE,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|DkMemBlockFlags_Code);
 read_mem=memory(device,W*H*4,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached);
 cmd_mem=memory(device,CMD_SIZE,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
 data_mem=memory(device,4096,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
 if(!image_mem||!code_mem||!read_mem||!cmd_mem||!data_mem)goto done;
 DkImage image;dkImageInitialize(&image,&layout,image_mem,0);DkImageView view;dkImageViewDefaults(&view,&image);
 uint32_t *pixels=dkMemBlockGetCpuAddr(read_mem);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)pixels[y*W+x]=swap_rb(get32(s->color_va+y*s->pitch+x*4));
 DkCmdBufMaker cm;dkCmdBufMakerDefaults(&cm,device);cmd=dkCmdBufCreate(&cm);if(!cmd)goto done;
 dkCmdBufAddMemory(cmd,cmd_mem,0,CMD_SIZE);
 DkImageRect rect={0,0,0,W,H,1};DkCopyBuf transfer={dkMemBlockGetGpuAddr(read_mem),0,0};
 dkCmdBufCopyBufferToImage(cmd,&transfer,&view,&rect,0);
 dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
 dkCmdBufBindRenderTarget(cmd,&view,NULL);
 DkViewport viewport={0,0,W,H,0,1};DkScissor scissor={0,0,W,H};
 dkCmdBufSetViewports(cmd,0,&viewport,1);dkCmdBufSetScissors(cmd,0,&scissor,1);
 if(!b) {
  dkCmdBufClearColorFloat(cmd,0,DkColorMask_RGBA,((clear_argb>>16)&255)/255.f,
      ((clear_argb>>8)&255)/255.f,(clear_argb&255)/255.f,(clear_argb>>24)/255.f);
 } else {
  if(!b->count||b->count>6||b->count%3)goto done;
  for(unsigned i=0;i<b->count;i++) {
   const Nv2aVertex*v=&b->vertices[i];
   if((b->texture && v->diffuse!=0xFFFFFFFFu)||v->rhw!=1.f||!isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)||!isfinite(v->u)||!isfinite(v->v))goto done;
  }
  memcpy(dkMemBlockGetCpuAddr(data_mem),b->vertices,b->count*sizeof(Nv2aVertex));
  const Nv2aTexture*t=b->texture;float settings[12]={W,H,0,0,W,H,0,0,t?0:2};
  memcpy((uint8_t*)dkMemBlockGetCpuAddr(data_mem)+256,settings,sizeof settings);
  DkShader vs,fs;uint32_t offset=0;
  if(!shader(&vs,code_mem,&offset,"romfs:/shaders/attribute_vsh.dksh")
     ||!shader(&fs,code_mem,&offset,t?"romfs:/shaders/texture_fsh.dksh":"romfs:/shaders/diffuse_fsh.dksh"))goto done;
  DkShader const*shaders[]={&vs,&fs};dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,shaders,2);
  if(t) {
   if(!texture_ok(t))goto done;
   DkImageLayoutMaker tm;dkImageLayoutMakerDefaults(&tm,device);tm.flags=DkImageFlags_Usage2DEngine;
   tm.format=DkImageFormat_RGBA8_Unorm;tm.dimensions[0]=W;tm.dimensions[1]=H;
   DkImageLayout tl;dkImageLayoutInitialize(&tl,&tm);
   tex_mem=memory(device,align_size(dkImageLayoutGetSize(&tl),dkImageLayoutGetAlignment(&tl)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
   desc=memory(device,4096,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);if(!tex_mem||!desc)goto done;
   DkImage ti;dkImageInitialize(&ti,&tl,tex_mem,0);DkImageView tv;dkImageViewDefaults(&tv,&ti);
   uint32_t decoded[W*H];if(!nv2a_backend_decode_texture(t,decoded))goto done;
   uint32_t*upload=(uint32_t*)((uint8_t*)dkMemBlockGetCpuAddr(data_mem)+512);
   for(unsigned i=0;i<W*H;i++)upload[i]=swap_rb(decoded[i]);
   DkCopyBuf source={dkMemBlockGetGpuAddr(data_mem)+512,0,0};
   dkCmdBufCopyBufferToImage(cmd,&source,&tv,&rect,0);
   DkSampler sampler;dkSamplerDefaults(&sampler);sampler.wrapMode[0]=sampler.wrapMode[1]=DkWrapMode_ClampToEdge;
   dkImageDescriptorInitialize(dkMemBlockGetCpuAddr(desc),&tv,false,false);
   dkSamplerDescriptorInitialize((DkSamplerDescriptor*)((uint8_t*)dkMemBlockGetCpuAddr(desc)+256),&sampler);
   dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
   dkCmdBufBindImageDescriptorSet(cmd,dkMemBlockGetGpuAddr(desc),1);
   dkCmdBufBindSamplerDescriptorSet(cmd,dkMemBlockGetGpuAddr(desc)+256,1);
   DkResHandle handle=dkMakeTextureHandle(0,0);dkCmdBufBindTextures(cmd,DkStage_Fragment,0,&handle,1);
  }
  DkRasterizerState raster;dkRasterizerStateDefaults(&raster);raster.cullMode=DkFace_None;
  DkDepthStencilState depth;dkDepthStencilStateDefaults(&depth);depth.depthTestEnable=false;depth.depthWriteEnable=false;depth.stencilTestEnable=false;
  DkColorState colour;dkColorStateDefaults(&colour);DkColorWriteState mask;dkColorWriteStateDefaults(&mask);
  DkBlendState blend;dkBlendStateDefaults(&blend);
  dkCmdBufBindRasterizerState(cmd,&raster);dkCmdBufBindDepthStencilState(cmd,&depth);
  dkCmdBufBindColorState(cmd,&colour);dkCmdBufBindColorWriteState(cmd,&mask);dkCmdBufBindBlendStates(cmd,0,&blend,1);
  DkVtxAttribState attrs[]={
   {.bufferId=0,.offset=offsetof(Nv2aVertex,x),.size=DkVtxAttribSize_4x32,.type=DkVtxAttribType_Float},
   {.bufferId=0,.offset=offsetof(Nv2aVertex,diffuse),.size=DkVtxAttribSize_1x32,.type=DkVtxAttribType_Uint},
   {.bufferId=0,.offset=offsetof(Nv2aVertex,u),.size=DkVtxAttribSize_2x32,.type=DkVtxAttribType_Float}};
  DkVtxBufferState vbs={sizeof(Nv2aVertex),0};DkBufExtents vb={dkMemBlockGetGpuAddr(data_mem),b->count*sizeof(Nv2aVertex)};
  DkBufExtents ub={dkMemBlockGetGpuAddr(data_mem)+256,256};
  dkCmdBufBindVtxAttribState(cmd,attrs,3);dkCmdBufBindVtxBufferState(cmd,&vbs,1);dkCmdBufBindVtxBuffers(cmd,0,&vb,1);
  dkCmdBufBindUniformBuffers(cmd,DkStage_Vertex,0,&ub,1);dkCmdBufBindUniformBuffers(cmd,DkStage_Fragment,0,&ub,1);
  dkCmdBufDraw(cmd,DkPrimitive_Triangles,b->count,1,0,0);
 }
 dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_L2Cache);
 dkCmdBufCopyImageToBuffer(cmd,&view,&rect,&transfer,0);
 DkCmdList list=dkCmdBufFinishList(cmd);DkQueueMaker qm;dkQueueMakerDefaults(&qm,device);qm.flags=DkQueueFlags_Graphics;
 queue=dkQueueCreate(&qm);if(!queue)goto done;dkQueueSubmitCommands(queue,list);dkQueueFlush(queue);dkQueueWaitIdle(queue);
 if(dkQueueIsInErrorState(queue))goto done;
#ifndef COHERENCE_SKIP_READBACK
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)put32(s->color_va+y*s->pitch+x*4,swap_rb(pixels[y*W+x]));
#endif
 ok=1;
 report("NV2A-COHERENCE GPU callback committed");
done:
 if(queue)dkQueueDestroy(queue);
 if(cmd)dkCmdBufDestroy(cmd);
 if(desc)dkMemBlockDestroy(desc);
 if(tex_mem)dkMemBlockDestroy(tex_mem);
 if(data_mem)dkMemBlockDestroy(data_mem);
 if(cmd_mem)dkMemBlockDestroy(cmd_mem);
 if(read_mem)dkMemBlockDestroy(read_mem);
 if(code_mem)dkMemBlockDestroy(code_mem);
 if(image_mem)dkMemBlockDestroy(image_mem);
 if(!ok)fatal("NV2A-COHERENCE-PROOF FAIL accepted GPU callback failed before fence release");
}
static void draw(const Nv2aSurface*s,const Nv2aBatch*b) {execute(s,b,0);gpu_draws++;}
static void clear(const Nv2aSurface*s,const Nv2aRenderState*r,uint32_t f,uint32_t a,uint32_t z) {
 (void)r;(void)f;(void)z;execute(s,NULL,a);gpu_clears++;
}
static void flip(void) {CHECK(presents==1);flips++;}
static const Nv2aBackend backend={.clear=clear,.draw=draw,.flip=flip,.can_draw=can_draw,.can_clear=can_clear};

static void surface(uint32_t va) {m(0x0208,8);m(0x0200,W<<16);m(0x0204,H<<16);m(0x020C,PITCH);m(0x0210,va);}
static void rectangle(unsigned x0,unsigned y0,unsigned x1,unsigned y1,uint32_t colour) {
 const float xy[6][2]={{x0,y0},{x1,y0},{x0,y1},{x0,y1},{x1,y0},{x1,y1}};
 for(unsigned i=0;i<6;i++) {
  float xyz[3]={xy[i][0],xy[i][1],0};memcpy(s_mem+0x2000+12*i,xyz,sizeof xyz);put32(0x3000+4*i,colour);
  memcpy(s_mem+0x7000+8*i,xy[i],8);
 }
 m(0x17FC,5);m(0x1810,5u<<24);m(0x17FC,0);
}
static void verify_fill(uint32_t va,uint32_t background,uint32_t fill,unsigned x0,unsigned y0,unsigned x1,unsigned y1) {
 unsigned bad=0;
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)
  bad+=get32(va+y*PITCH+x*4)!=((x>=x0&&x<x1&&y>=y0&&y<y1)?fill:background);
 CHECK(bad==0);
 for(unsigned y=0;y<H;y++)for(unsigned x=W*4;x<PITCH;x++)CHECK(s_mem[va+y*PITCH+x]==0xA5);
 CHECK(s_mem[va-1]==0xA5&&s_mem[va+PITCH*H]==0xA5);
}
int main(void) {
 report("NV2A-COHERENCE start");
 if(R_FAILED(romfsInit()))fatal("NV2A-COHERENCE-PROOF FAIL romfs");
 report("NV2A-COHERENCE romfs ready");
 DkDeviceMaker dm;dkDeviceMakerDefaults(&dm);device=dkDeviceCreate(&dm);
 if(!device)fatal("NV2A-COHERENCE-PROOF FAIL device");
 report("NV2A-COHERENCE device ready");
 setenv("RECOMP_PB_EXEC","1",1);memset(s_mem,0xA5,sizeof s_mem);
 nv2a_backend_register(&backend);m(0x1E94,0);
 const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 for(unsigned i=0;i<16;i++)mf(0x0680+4*i,identity[i]);
 m(0x1720,0x2000);m(0x1760,2|(3<<4)|(12<<8));
 m(0x172C,0x3000);m(0x176C,0|(4<<4)|(4<<8));
 m(0x032C,0);m(0x0308,0);m(0x030C,0);m(0x0300,0);m(0x035C,0);
 m(0x0358,0x01010101);
 surface(SURF_A);m(0x1D90,0xFF17395Bu);m(0x1D94,0xF0);
 verify_fill(SURF_A,0xFF17395Bu,0,0,0,0,0);CHECK(gpu_clears==1);
 surface(SURF_B);m(0x1D90,0xFF6587A9u);m(0x1D94,0xF0);
 verify_fill(SURF_B,0xFF6587A9u,0,0,0,0,0);CHECK(gpu_clears==2);
 surface(SURF_A);rectangle(2,2,14,12,0xFFB13759u);
 verify_fill(SURF_A,0xFF17395Bu,0xFFB13759u,2,2,14,12);CHECK(gpu_draws==1);
 // CPU writes must be imported by the next callback, including untouched pixels.
 put32(SURF_A+PITCH*20+4*20,0xFF23A7D1u);
 uint8_t expected_a[PITCH*H];memcpy(expected_a,s_mem+SURF_A,sizeof expected_a);
 for(unsigned y=2;y<12;y++)for(unsigned x=16;x<28;x++) {
  const uint32_t c=0xFF49C17Bu;memcpy(expected_a+y*PITCH+x*4,&c,4);
 }
 rectangle(16,2,28,12,0xFF49C17Bu);
 CHECK(!memcmp(expected_a,s_mem+SURF_A,sizeof expected_a));
 CHECK(get32(SURF_A+PITCH*20+4*20)==0xFF23A7D1u);CHECK(get32(SURF_A+PITCH*4+4*4)==0xFFB13759u);
 CHECK(get32(SURF_A+PITCH*4+4*20)==0xFF49C17Bu);
 // Rendered guest RAM becomes a fresh linear texture on the next surface.
 m(0x1744,0x7000);m(0x1784,2|(2<<4)|(8<<8));
 m(0x1B00,SURF_A);m(0x1B04,(0x12<<8));m(0x1B08,0x303);m(0x1B10,PITCH<<16);m(0x1B1C,(W<<16)|H);m(0x1B14,0);
 surface(SURF_B);rectangle(0,0,W,H,0xFFFFFFFFu);
 for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)CHECK(get32(SURF_B+y*PITCH+x*4)==get32(SURF_A+y*PITCH+x*4));
 CHECK(gpu_draws==3);
 for(unsigned y=0;y<H;y++)for(unsigned x=W*4;x<PITCH;x++)CHECK(s_mem[SURF_B+y*PITCH+x]==0xA5);
 CHECK(s_mem[SURF_B-1]==0xA5&&s_mem[SURF_B+PITCH*H]==0xA5);
 nv2a_pb_set_semaphore_target(0x5000);put32(0x5000,0);
 CHECK(get32(SURF_B+PITCH*4+4*4)==0xFFB13759u);CHECK(get32(0x5000)==0);
 m(0x1D70,0x12345678u);CHECK(get32(0x5000)==0x12345678u);
 // Changing current target must not change which completed surface is presented.
 present_va=SURF_B;present_pixel=0xFFB13759u;surface(SURF_A);m(0x012C,0);m(0x0130,0);
 CHECK(presents==1&&flips==1);
 // Unsupported state must fall back explicitly and leave accepted-callback counts unchanged.
 m(0x1784,0);m(0x0344,1);m(0x0348,0);m(0x0350,0x8006);m(0x0304,1);
 unsigned before=gpu_draws,reject_before=cpu_rejects;rectangle(2,14,10,22,0xFF112233u);
 CHECK(gpu_draws==before&&cpu_rejects==reject_before+1);
 CHECK(get32(SURF_A+PITCH*16+4*4)==0xFF112233u);
 CHECK(get32(SURF_A+PITCH*20+4*20)==0xFF23A7D1u);
 for(unsigned y=0;y<H;y++)for(unsigned x=W*4;x<PITCH;x++)CHECK(s_mem[SURF_A+y*PITCH+x]==0xA5);
 // A depth-inclusive clear is rejected, and the CPU still clears colour.
 before=gpu_clears;reject_before=cpu_rejects;m(0x1D90,0xFF527496u);m(0x1D94,0xF3);
 CHECK(gpu_clears==before&&cpu_rejects==reject_before+1);
 verify_fill(SURF_A,0xFF527496u,0,0,0,0,0);
 nv2a_backend_register(NULL);dkDeviceDestroy(device);romfsExit();
 report(fail?"NV2A-COHERENCE-PROOF FAIL guest RAM ordering/readback":"NV2A-COHERENCE-PROOF PASS clear/draw/padding/two-surfaces/cpu-write/render-texture/fence/present/fallback");
 return fail;
}
