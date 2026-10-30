// Generic decoder-to-native-GPU composition through the real executor; no game inputs.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "nv2a_deko3d.h"
#define W 128u
#define H 96u
#define PITCH (W*4+16)
#define CPU_SURF 0x10000u
#define GPU_SURF 0x20000u
#define TEX 0x40000u
#define PALETTE 0x50000u
static uint8_t s_mem[0x80000];
static unsigned s_fail;
static unsigned s_expect_gpu = 1;
static unsigned s_gpu_cases;
static uint64_t s_guard_rejected_transforms;
static int s_near_zero_alpha, s_blend_override, s_expect_unchanged, s_huge_stale;
static uint32_t s_blend_src, s_blend_dst, s_alpha_ref;
static void report(const char *s) { printf("%s\n",s); svcOutputDebugString(s,strlen(s)); }
#define CHECK(c) do { if (!(c)) { char msg[200]; snprintf(msg,sizeof msg,"NV2A-MENU-CHECK FAIL line=%d %s",__LINE__,#c); report(msg);s_fail++; } } while(0)
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
void xbox_FramebufferWindowPresent(uint32_t va, uint32_t pitch)
{ (void)va; (void)pitch; CHECK(0 && "unexpected presentation in composition fixture"); }
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


extern void nv2a_pb_exec_method(uint32_t, uint32_t, uint32_t);
static void m(uint32_t method, uint32_t value) { nv2a_pb_exec_method(0, method, value); }
static void mf(uint32_t method, float value) { uint32_t u;memcpy(&u,&value,4);m(method,u); }
static void put32(uint32_t at, uint32_t value) { memcpy(s_mem+at,&value,4); }
static uint32_t get32(uint32_t at) { uint32_t value;memcpy(&value,s_mem+at,4);return value; }
static uint32_t inputs(uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{ return (a<<24)|(b<<16)|(c<<8)|d; }

/* Construct generic products and direct R0 output from documented input fields. */
static void combiner(int textured)
{
    enum { ZERO=0, V0=4, T0=8, R0=12, ALPHA=16, INVERT=32 };
    m(0x0AC0, inputs(textured?T0:V0,textured?V0:(ZERO|INVERT),ZERO,ZERO));
    m(0x0260, inputs(textured?(T0|ALPHA):(V0|ALPHA),textured?(V0|ALPHA):(ZERO|INVERT),ZERO,ZERO));
    m(0x1E40, R0<<8); m(0x0AA0,R0<<8);
    m(0x0288, R0); m(0x028C,(R0|ALPHA)<<8);
    m(0x1E70,textured?1:0);m(0x1E60,1);
}

static void surface(uint32_t va)
{
    m(0x0208,0x124);m(0x0200,W<<16);m(0x0204,H<<16);m(0x020C,PITCH);m(0x0210,va);
}
static const float xy[3][2]={{8,8},{120,16},{16,88}};
static void vertices(void)
{
    const float colours[3][4]={{.37f,.61f,.83f,.77f},{.91f,.29f,.53f,.77f},{.43f,.89f,.19f,.77f}};
    for(unsigned i=0;i<3;i++) {
        float pos[4]={xy[i][0],xy[i][1],0,1};
        float uv[4]={xy[i][0]/16-1,xy[i][1]/16-1,0,1};
        memcpy(s_mem+0x2000+i*16,pos,16);memcpy(s_mem+0x3000+i*16,colours[i],16);
        memcpy(s_mem+0x4000+i*16,uv,16);
    }
    m(0x1720,0x2000);m(0x1760,2|(4<<4)|(16<<8));
    m(0x172C,0x3000);m(0x176C,2|(4<<4)|(16<<8));
    m(0x1744,0x4000);m(0x1784,2|(4<<4)|(16<<8));
}
static void draw(void) {m(0x17FC,5);m(0x1810,2u<<24);m(0x17FC,0);}
static unsigned morton(unsigned x,unsigned y)
{ return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2); }
static void texture(unsigned format,int linear)
{
    const uint32_t rgba[]={s_near_zero_alpha?0x00F04020u:0x004080B0u,
        s_near_zero_alpha?0x01F04020u:0x4040C030u,
        s_near_zero_alpha?0x01F04020u:0x80D02070u,
        s_near_zero_alpha?0x01F04020u:0xFFA06020u};
    memset(s_mem+TEX,0,128);
    if(format==0x0B) {
        for(unsigned i=0;i<32;i++)put32(PALETTE+4*i,rgba[i&3]);
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)s_mem[TEX+morton(x,y)]=x;
    } else if(format==0x12) {
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)put32(TEX+y*32+x*4,rgba[x]);
    } else if(format==0x06) {
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)put32(TEX+4*morton(x,y),rgba[x]);
    } else {
        const uint8_t block[]={0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,
                               0xE0,0x07,0x00,0xF8,0xE4,0xE4,0xE4,0xE4};
        memcpy(s_mem+TEX,block,16);
    }
    m(0x1B00,TEX);m(0x1B04,(format<<8)|(1<<16)|(2<<20)|(2<<24));
    if(s_huge_stale)m(0x1B04,(format<<8)|(1<<16)|(15<<20)|(15<<24));
    m(0x1B08,0x303);m(0x1B10,format==0x12?32u<<16:0);
    if(format==0x12)m(0x1B1C,(4<<16)|4);
    m(0x1B20,PALETTE|(3<<2));
    m(0x1B14,linear?0x02022000:0);
}
static void state(int blend,int additive,int alpha,uint32_t mask)
{
    m(0x0304,blend);m(0x0344,s_blend_override?s_blend_src:additive?1:0x302);
    m(0x0348,s_blend_override?s_blend_dst:additive?1:0x303);m(0x0350,0x8006);
    m(0x0300,alpha);m(0x033C,0x204);m(0x0340,s_alpha_ref);m(0x0358,mask);
}
static void barycentric(float x,float y,double b[3])
{
    double area=(xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0]);
    b[0]=((xy[1][0]-x)*(xy[2][1]-y)-(xy[1][1]-y)*(xy[2][0]-x))/area;
    b[1]=((xy[2][0]-x)*(xy[0][1]-y)-(xy[2][1]-y)*(xy[0][0]-x))/area;
    b[2]=1-b[0]-b[1];
}
static void background(uint32_t va)
{
    memset(s_mem+va,0xA5,PITCH*H);
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++)put32(va+y*PITCH+x*4,0xE65A96D2u);
}
static void compare(const char *name)
{
    unsigned interior=0,outside=0,bad=0,changed=0;int max_error=0;
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
        double b[3];barycentric(x+.5f,y+.5f,b);double edge=fmin(b[0],fmin(b[1],b[2]));
        uint32_t cpu=get32(CPU_SURF+y*PITCH+x*4),gpu=get32(GPU_SURF+y*PITCH+x*4);
        if(edge<-.03) {outside++;bad+=cpu!=gpu || gpu!=0xE65A96D2u;continue;}
        if(edge<=.03)continue;
        interior++;changed+=cpu!=0xE65A96D2u;
        for(unsigned k=0;k<4;k++) {int e=abs((int)((cpu>>(k*8))&255)-(int)((gpu>>(k*8))&255));if(e>max_error)max_error=e;bad+=e>1;}
    }
    for(unsigned y=0;y<H;y++)for(unsigned x=W*4;x<PITCH;x++)bad+=s_mem[GPU_SURF+y*PITCH+x]!=0xA5;
    int identity=s_expect_unchanged||(s_blend_override&&s_blend_src==0&&s_blend_dst==1);
    int pass=interior>3000&&outside>7000&&(identity?changed==0:changed>100)&&bad==0;
    CHECK(pass);
    char msg[240];snprintf(msg,sizeof msg,"NV2A-MENU-CASE %s name=%s interior=%u outside=%u changed=%u bad=%u max_error=%d",pass?"PASS":"FAIL",name,interior,outside,changed,bad,max_error);report(msg);
}
static void run(const char *name,unsigned format,int filtered,int textured,int blend,int additive,int alpha,uint32_t mask)
{
    s_gpu_cases+=s_expect_gpu;
    texture(format,filtered);combiner(textured);state(blend,additive,alpha,mask);
    nv2a_backend_register(NULL);surface(CPU_SURF);background(CPU_SURF);draw();
    ManhuntNv2aDeko3dStats before,after;manhunt_nv2a_deko3d_stats(&before);
    nv2a_backend_register(manhunt_nv2a_deko3d_backend());surface(GPU_SURF);background(GPU_SURF);draw();
    manhunt_nv2a_deko3d_stats(&after);CHECK(after.draws==before.draws+s_expect_gpu);compare(name);
}

static void guard_cases(void)
{
    const Nv2aBackend *backend=manhunt_nv2a_deko3d_backend();
    Nv2aSurface s={.color_va=GPU_SURF,.width=W,.height=H,.pitch=PITCH,
        .bytes_per_pixel=4,.aa_sx=1,.aa_sy=1,.format_raw=0x124};
    Nv2aRenderState rs={.color_mask=0x01010101};
    Nv2aTexture t={.offset=TEX,.width=4,.height=4,.color=0x0B,
        .addr_u=3,.addr_v=3,.levels=1,.palette_va=PALETTE,.palette_entries=32};
    Nv2aDrawInfo info={.primitive=5,.path=NV2A_DRAW_FFP,.shading=NV2A_SHADE_MODULATE,
        .has_texcoords=1,.combiner_seen=1,.texture=&t};
    unsigned before=s_fail;
    texture(0x0B,0);
    CHECK(backend->can_draw(&s,&rs,&info));
    t.offset=UINT32_MAX;CHECK(!backend->can_draw(&s,&rs,&info));t.offset=TEX;
    t.palette_va=0x83FFFFC0u;CHECK(!backend->can_draw(&s,&rs,&info));t.palette_va=PALETTE;
    t.palette_entries=16;CHECK(!backend->can_draw(&s,&rs,&info));t.palette_entries=32;
    s_mem[TEX]=32;CHECK(!backend->can_draw(&s,&rs,&info));s_mem[TEX]=0;
    info.combiner_seen=0;t.filter=0x02022000;CHECK(!backend->can_draw(&s,&rs,&info));info.combiner_seen=1;
    CHECK(backend->can_draw(&s,&rs,&info));
    t.filter=1;CHECK(!backend->can_draw(&s,&rs,&info));t.filter=0;
    t.levels=2;CHECK(!backend->can_draw(&s,&rs,&info));t.levels=1;
    t.addr_u=1;CHECK(!backend->can_draw(&s,&rs,&info));t.addr_u=3;
    t.width=3;CHECK(!backend->can_draw(&s,&rs,&info));t.width=4;
    t.color=0x99;CHECK(!backend->can_draw(&s,&rs,&info));t.color=0x0B;
    s.pitch=1;CHECK(!backend->can_draw(&s,&rs,&info));s.pitch=PITCH;
    s.color_va=0;CHECK(!backend->can_draw(&s,&rs,&info));s.color_va=GPU_SURF;
    g_xbox_image_lo=GPU_SURF;g_xbox_image_hi=GPU_SURF+PITCH*H;
    CHECK(!backend->can_draw(&s,&rs,&info));g_xbox_image_lo=g_xbox_image_hi=0;
    rs.depth_test_enable=1;CHECK(!backend->can_draw(&s,&rs,&info));rs.depth_test_enable=0;
    rs.alpha_test_enable=1;rs.alpha_func=0x207;
    CHECK(!backend->can_draw(&s,&rs,&info));rs.alpha_test_enable=0;
    rs.blend_enable=1;rs.blend_src=0x304;rs.blend_dst=1;rs.blend_eq=0x8006;
    CHECK(!backend->can_draw(&s,&rs,&info));rs.blend_enable=0;
    info.shading=NV2A_SHADE_UNSUPPORTED;CHECK(!backend->can_draw(&s,&rs,&info));
    info.shading=NV2A_SHADE_DIFFUSE;t.offset=UINT32_MAX;
    CHECK(backend->can_draw(&s,&rs,&info));
    Nv2aTransformInfo transformed={.count=3,.finite=1,.positive_w=1,
        .constant_w=1,.texture_q_one=1,.w=1};
    CHECK(backend->can_draw_transformed(&s,&info,&transformed));
    transformed.tex_u_abs_max=1e38f;transformed.tex_v_abs_max=1e38f;
    CHECK(backend->can_draw_transformed(&s,&info,&transformed)); // unused diffuse UV
    info.shading=NV2A_SHADE_MODULATE;t.offset=TEX;
    CHECK(!backend->can_draw_transformed(&s,&info,&transformed));
    transformed.tex_u_abs_max=250000;transformed.tex_v_abs_max=250000;
    CHECK(backend->can_draw_transformed(&s,&info,&transformed));
    transformed.tex_u_abs_max=250001;CHECK(!backend->can_draw_transformed(&s,&info,&transformed));
    transformed.tex_u_abs_max=0;transformed.tex_v_abs_max=250001;
    CHECK(!backend->can_draw_transformed(&s,&info,&transformed));
    transformed.tex_v_abs_max=0;transformed.w=1e-40f;
    CHECK(!backend->can_draw_transformed(&s,&info,&transformed));
    transformed.w=1e7f;CHECK(!backend->can_draw_transformed(&s,&info,&transformed));
    ManhuntNv2aDeko3dStats stats;manhunt_nv2a_deko3d_stats(&stats);
    s_guard_rejected_transforms=stats.rejected_transforms;
    report(s_fail==before?"NV2A-MENU-GUARDS PASS invalid RAM/palette/index/filter/state and unused descriptor":"NV2A-MENU-GUARDS FAIL preflight");
}

static void clear_case(void)
{
    const Nv2aBackend *backend=manhunt_nv2a_deko3d_backend();
    Nv2aSurface surface={.color_va=GPU_SURF,.width=W,.height=H,.pitch=PITCH,
        .bytes_per_pixel=4,.aa_sx=1,.aa_sy=1,.format_raw=0x124};
    Nv2aRenderState state={.color_mask=0x01010101};
    const uint32_t argb=0x3A7CB2E5u;
    unsigned before_fail=s_fail,bad=0;
    CHECK(!backend->can_clear(&surface,&state,0x10,argb,0));
    surface.clear_rect_set=1;
    CHECK(!backend->can_clear(&surface,&state,0xF0,argb,0));surface.clear_rect_set=0;
    surface.format_raw=0xDEAD;
    CHECK(!backend->can_clear(&surface,&state,0xF0,argb,0));surface.format_raw=0x124;
    CHECK(backend->can_clear(&surface,&state,0xF0,argb,0));
    background(GPU_SURF);
    ManhuntNv2aDeko3dStats before,after;manhunt_nv2a_deko3d_stats(&before);
    backend->clear(&surface,&state,0xF0,argb,0);
    manhunt_nv2a_deko3d_stats(&after);CHECK(after.clears==before.clears+1);
    for(unsigned y=0;y<H;y++) {
        for(unsigned x=0;x<W;x++)bad+=get32(GPU_SURF+y*PITCH+x*4)!=argb;
        for(unsigned x=W*4;x<PITCH;x++)bad+=s_mem[GPU_SURF+y*PITCH+x]!=0xA5;
    }
    CHECK(bad==0);
    char msg[160];snprintf(msg,sizeof msg,"NV2A-MENU-CLEAR %s pixels=%u padding_bytes=%u bad=%u",s_fail==before_fail?"PASS":"FAIL",W*H,(PITCH-W*4)*H,bad);report(msg);
}

static void quantization_cases(void)
{
    s_near_zero_alpha=1;
    for(unsigned i=0;i<3;i++) {
        const float uv[4]={1.1f/4,1.0f/4,0,1};
        memcpy(s_mem+0x4000+i*16,uv,16);
    }
    /* RC bilinear output stays float: .6 alpha byte * .77 rounds to zero. */
    s_expect_unchanged=1;
    run("nearzero-bilinear-p8-alpha",0x0B,1,1,0,0,1,0x01010101);
    run("nearzero-bilinear-argb-alpha",0x06,1,1,0,0,1,0x01010101);
    s_expect_unchanged=0;
    for(unsigned i=0;i<3;i++) {
        const float uv[4]={1.2f/4,1.0f/4,0,1};
        memcpy(s_mem+0x4000+i*16,uv,16);
    }
    run("nearzero-bilinear-argb-alpha-keep",0x06,1,1,0,0,1,0x01010101);
    s_near_zero_alpha=0;
    const float uv[3][4]={{.1f/4,.1f/4,0,1},{3.9f/4,.3f/4,0,1},{.3f/4,3.9f/4,0,1}};
    const float colours[3][4]={{1.37f,.61f,.83f,.37f},{.91f,1.29f,.53f,.73f},{.43f,.89f,1.19f,.91f}};
    for(unsigned i=0;i<3;i++) {
        memcpy(s_mem+0x4000+i*16,uv[i],16);memcpy(s_mem+0x3000+i*16,colours[i],16);
    }
    const uint32_t factors[]={0,1,0x302,0x303};
    s_blend_override=1;
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<4;j++) {
        char name[80];snprintf(name,sizeof name,"blend-quantization-%X-%X",factors[i],factors[j]);
        s_blend_src=factors[i];s_blend_dst=factors[j];
        run(name,0x06,1,1,1,0,0,0x01010101);
    }
    s_blend_override=0;vertices();
}
int main(void)
{
    report("NV2A-MENU start");
    if(R_FAILED(romfsInit())||!manhunt_nv2a_deko3d_init()) {report("NV2A-MENU-PROOF FAIL initialization");return 1;}
    memset(s_mem,0,sizeof s_mem);setenv("RECOMP_PB_EXEC","1",1);m(0x1E94,0);
    /* Nonunit, constant W: resulting surface coordinates stay unchanged. */
    const float matrix[16]={6,0,0,0,0,6,0,0,0,0,6,0,0,0,0,6};
    for(unsigned i=0;i<16;i++)mf(0x0680+i*4,matrix[i]);
    vertices();m(0x032C,0);m(0x030C,0);m(0x035C,0);m(0x0308,0);guard_cases();
#ifdef MENU_PROOF_NO_RC
    setenv("RECOMP_NO_COMBINERS","1",1);s_expect_gpu=0;
    run("nonRC-filtered-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    ManhuntNv2aDeko3dStats no_rc;manhunt_nv2a_deko3d_stats(&no_rc);
    CHECK(no_rc.draws==0&&no_rc.rejected_draws>0);
    nv2a_backend_register(NULL);manhunt_nv2a_deko3d_shutdown();romfsExit();
    report(s_fail?"NV2A-MENU-PROOF FAIL nonRC filtered fallback":"NV2A-MENU-PROOF PASS nonRC filtered fallback");
    return s_fail!=0;
#endif
    clear_case();
    run("p8-raw",0x0B,0,1,0,0,0,0x01010101);
    run("p8-bilinear-alpha-blend",0x0B,1,1,1,0,1,0x01010101);
    run("p8-bilinear-alpha-no-blend",0x0B,1,1,0,0,1,0x01010101);
    run("argb-bilinear-alpha-blend",0x06,1,1,1,0,1,0x01010101);
    run("linear-argb-bilinear-alpha-blend",0x12,1,1,1,0,1,0x01010101);
    run("dxt23-bilinear-additive",0x0E,1,1,1,1,1,0x01010101);
    run("diffuse-ignores-bound-texture",0x06,1,0,1,0,1,0x01010101);
    run("argb-bilinear-mask-rg",0x06,1,1,1,0,1,0x00010100);
    s_alpha_ref=128;
    run("p8-bilinear-alpha128",0x0B,1,1,1,0,1,0x01010101);
    run("argb-bilinear-alpha128",0x06,1,1,1,0,1,0x01010101);
    run("diffuse-alpha128",0x06,1,0,1,0,1,0x01010101);
    s_alpha_ref=0;
    quantization_cases();
    s_huge_stale=1;
    for(unsigned i=0;i<3;i++) { const float uv[4]={1e38f,1e38f,0,1};memcpy(s_mem+0x4000+i*16,uv,16); }
    run("diffuse-unused-overflowing-UV",0x06,1,0,1,0,1,0x01010101);
    s_huge_stale=0;vertices();
    /* Repeat real executor draws to exercise queue lifetime beyond menu OOM count. */
    for (unsigned repeat=0; repeat<640; repeat++)
        run("queue-lifetime-repeat",0x06,1,1,1,0,1,0x01010101);
    s_expect_gpu=0;
    /* Change W while preserving screen positions: fallback must use already-transformed CPU vertices. */
    for(unsigned i=0;i<3;i++) {
        float w=i==2?2:1;float p[4]={xy[i][0]*w,xy[i][1]*w,0,w};
        memcpy(s_mem+0x2000+i*16,p,16);
    }
    run("varying-W-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    vertices();float q=2;memcpy(s_mem+0x4000+2*16+12,&q,4);
    run("projected-Q-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    vertices();
    for(unsigned i=0;i<3;i++) { const float uv[4]={4e5f,4e5f,0,1};memcpy(s_mem+0x4000+i*16,uv,16); }
    run("large-sampled-UV-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    vertices();
    for(unsigned i=0;i<3;i++) { const float p[4]={xy[i][0]*1e-4f,xy[i][1]*1e-4f,0,1e-4f};memcpy(s_mem+0x2000+i*16,p,16); }
    s_expect_unchanged=1;
    run("tiny-common-W-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    s_expect_unchanged=0;
    for(unsigned i=0;i<3;i++) { const float p[4]={xy[i][0]*1e7f,xy[i][1]*1e7f,0,1e7f};memcpy(s_mem+0x2000+i*16,p,16); }
    run("large-common-W-CPU-fallback",0x06,1,1,1,0,1,0x01010101);
    ManhuntNv2aDeko3dStats stats;manhunt_nv2a_deko3d_stats(&stats);CHECK(stats.draws==s_gpu_cases&&stats.clears==1&&stats.rejected_transforms==s_guard_rejected_transforms+5);
    nv2a_backend_register(NULL);manhunt_nv2a_deko3d_shutdown();romfsExit();
    char result[160];snprintf(result,sizeof result,"NV2A-MENU-PROOF %s decoder/native composition clears=1 gpu_cases=%u fallback_cases=5",s_fail?"FAIL":"PASS",s_gpu_cases);report(result);
    return s_fail!=0;
}
