/* Runs the production renderer on a real desktop GL driver. Synthetic pixels
 * only: no game assets, guest simulation, console FPS or gameplay acceptance. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
static void observed_swap(SDL_Window *window);
#define SDL_GL_SwapWindow observed_swap
#ifndef NV2A_GL_SOURCE
#define NV2A_GL_SOURCE "../../game/src/nv2a_gl/nv2a_gl.c"
#endif
#include NV2A_GL_SOURCE
#undef SDL_GL_SwapWindow
static uint32_t swap_pixel;
static void observed_swap(SDL_Window *window)
{
    int w,h;
    SDL_GL_GetDrawableSize(window,&w,&h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glReadPixels(w/2,h/2,1,1,GL_BGRA,GL_UNSIGNED_BYTE,&swap_pixel);
    assert(glGetError()==GL_NO_ERROR);
    SDL_GL_SwapWindow(window);
}
static uint8_t guest[4u<<20];
static uint32_t regs[2048];
static int wide;
static int queued_mode;
static uint32_t sample_va=0x280000,sample_w=2,sample_h=2,sample_pitch=8;
ptrdiff_t xbox_GetMemoryOffset(void) {return (ptrdiff_t)guest;}
int nv2a_backend_decode_texture(const Nv2aTexture *t,uint32_t *out) {(void)t;(void)out;return 0;}
int nv2a_backend_surface_writable(const Nv2aSurface *s) {return s&&s->bytes_per_pixel==4&&s->pitch>=s->width*4&&s->color_va+(uint64_t)s->pitch*(s->height+s->clip_y)<=sizeof guest;}
void nv2a_backend_register(const Nv2aBackend *b) {(void)b;}
void nv2a_pb_write_report(uint32_t a,uint32_t b) {(void)a;(void)b;}
uint8_t *nv2a_pb_reg_dirty(void) {static uint8_t d[128];memset(d,1,sizeof d);return d;}
const uint32_t *nv2a_pb_registers(void) {return regs;}
void xbox_nx_spread_thread(void) {}
void xbox_nx_track_thread(void *p) {(void)p;}
void xbox_vblank_report(double dt,char *b,size_t n) {(void)dt;if(n)*b=0;}
int xbox_video_widescreen(void) {return wide;}
int xbox_gamma_ramp(uint8_t r[256],uint8_t g[256],uint8_t b[256]) {(void)r;(void)g;(void)b;return 0;}
static Nv2aSurface surface(unsigned w,unsigned h,unsigned pitch)
{
    Nv2aSurface s={0};s.color_va=0x10000;s.width=w;s.height=h;s.pitch=pitch;
    s.bytes_per_pixel=4;s.aa_sx=s.aa_sy=1;s.format_raw=0x128;
    regs[0x208/4]=s.format_raw;regs[0x20c/4]=(pitch<<16)|pitch;
    regs[0x200/4]=w<<16;regs[0x204/4]=h<<16;regs[0x358/4]=0x01010101;
    return s;
}
static uint32_t *color_ram(const Nv2aSurface *s) {return (uint32_t*)(guest+s->color_va);}
static void clear_only_present_test(void)
{
    Nv2aSurface s=surface(8,8,32);Nv2aRenderState rs={0};uint32_t *c=color_ram(&s);
    wide=0;
    if(queued_mode)q_clear(&s,&rs,0xf0,0xff112233,0);else gl_clear(&s,&rs,0xf0,0xff112233,0);
    if(queued_mode)q_flip();else gl_flip();assert(swap_pixel==0xff112233);
    if(queued_mode)q_sync(1);else gl_sync(1);
    for(unsigned i=0;i<64;i++)c[i]=0xff334455;
    if(queued_mode)q_flip();else gl_flip();assert(swap_pixel==0xff334455);
    puts("PASS real GL clear-only and subsequent CPU-only output presented before the first draw");
}
static void clear_depth_test(void)
{
    Nv2aSurface s=surface(3,2,16);Nv2aRenderState rs={0};rs.zeta_va=0x300000;
    uint32_t *c=color_ram(&s),*d=(uint32_t*)(guest+rs.zeta_va);
    for(unsigned i=0;i<8;i++){c[i]=0x20304050u+i;d[i]=0x12345600u+i;}
    gl_clear(&s,&rs,0xf0,0xff112233,0);
    for(unsigned i=0;i<8;i++){assert(c[i]==(i%4<3?0xff112233:0x20304050u+i));assert(d[i]==0x12345600u+i);}
    gl_clear(&s,&rs,0x03,0,0x3579bda5);
    for(unsigned i=0;i<8;i++)assert(d[i]==(i%4<3?0x3579bda5:0x12345600u+i));
    puts("PASS real GL color/depth/stencil clear, guest readback, pitch canaries");
}
static void linear_color_formats_test(void)
{
    const unsigned format[]={4,5,8};
    for(unsigned f=0;f<3;f++) {
        Nv2aSurface s=surface(3,2,16);Nv2aRenderState rs={0};uint32_t *c=color_ram(&s);
        s.format_raw=(s.format_raw&~15u)|format[f];regs[0x208/4]=s.format_raw;
        for(unsigned i=0;i<8;i++)c[i]=0x37556677u+i;
        gl_clear(&s,&rs,0x70,0xaa112233,0);
        for(unsigned i=0;i<8;i++)assert(c[i]==(i%4<3?0x37112233u:0x37556677u+i));
        gl_clear(&s,&rs,0x80,0x99555555,0);
        for(unsigned i=0;i<8;i++)assert(c[i]==(i%4<3?0x99112233u:0x37556677u+i));
    }
    puts("PASS real GL linear color4/5/8 channel masks, exact RAM bytes and pitch canaries");
}
static void triangle(Nv2aSurface s,int textured,float x0,float y0,float x1,float y1,float x2,float y2)
{
    float attrs[3*64]={0};uint32_t indices[3]={0,1,2};Nv2aRawBatch b={0};
    const float pos[6]={x0,y0,x1,y1,x2,y2};
    for(unsigned v=0;v<3;v++){
        float *a=attrs+v*64;a[0]=pos[v*2];a[1]=pos[v*2+1];a[2]=0;a[3]=1;
        a[12]=1;a[15]=1;a[36]=.5f;a[37]=.5f;a[39]=1;
    }
    regs[0x288/4]=textured?8:4;regs[0x28c/4]=(textured?0x18:0x14)<<8;
    regs[0x1e70/4]=textured?1:0;
    regs[0x1b00/4]=textured?sample_va:0;regs[0x1b04/4]=0x12u<<8;
    regs[0x1b0c/4]=textured?0x40000000:0;regs[0x1b10/4]=sample_pitch<<16;
    regs[0x1b14/4]=(1u<<16)|(1u<<24);regs[0x1b1c/4]=(sample_w<<16)|sample_h;
    b.surface=s;b.regs=regs;b.prim=5;b.vertex_count=b.index_count=3;b.indices=indices;b.attrs=attrs;
    b.attr_present=(1u<<0)|(1u<<3)|(textured?(1u<<9):0);b.tex_va[0]=sample_va;
    if(queued_mode)q_draw(&b);else gl_draw_raw(&b);
}
static void draw_sample_test(void)
{
    Nv2aSurface s=surface(8,8,32);uint32_t *c=color_ram(&s);
    memset(c,0,8*8*4);triangle(s,0,0,0,8,0,0,8);
    assert(c[1*8+1]==0xffff0000&&c[7*8+7]==0);
    uint32_t *tex=(uint32_t*)(guest+0x280000);for(unsigned i=0;i<4;i++)tex[i]=0xffff0000;
    memset(c,0,8*8*4);triangle(s,1,0,0,8,0,0,8);assert(c[1*8+1]==0xffff0000);
    tex[0]=0xff00ff00;triangle(s,1,0,0,8,0,0,8);assert(c[1*8+1]==0xff00ff00);
    puts("PASS real GL production triangle/combiner, CPU same-frame texture mutation, resampling");
}
static void render_target_feedback_test(void)
{
    Nv2aSurface src=surface(8,8,32);uint32_t *c=color_ram(&src);
    memset(c,0,8*8*4);triangle(src,0,0,0,8,0,0,8);assert(c[0]==0xffff0000);
    Nv2aSurface dst=surface(8,8,32);dst.color_va=0x240000;uint32_t *out=color_ram(&dst);
    sample_va=src.color_va;sample_w=sample_h=8;sample_pitch=32;
    memset(out,0,8*8*4);triangle(dst,1,0,0,8,0,0,8);assert(out[9]==0xffff0000);
    c[0]=0xff0000ff;triangle(dst,1,0,0,8,0,0,8);assert(out[9]==0xff0000ff);
    sample_va=0x280000;sample_w=sample_h=2;sample_pitch=8;
    puts("PASS real GL render-target feedback observes guest CPU overwrite instead of stale FBO");
}
static void wide_reset_test(void)
{
    Nv2aSurface s=surface(640,480,2560);uint32_t *c=color_ram(&s);
    memset(c,0,640*480*4);wide=1;triangle(s,0,20,20,100,20,20,100);
    assert(c[25*640+25]==0&&c[25*640+100]==0xffff0000);
    memset(c,0,640*480*4);wide=0;triangle(s,0,20,20,100,20,20,100);
    assert(c[25*640+25]==0xffff0000&&c[25*640+100]==0);
    puts("PASS real GL widescreen 2D uniform applied and reset when disabled");
}
static void cpu_only_present_test(void)
{
    Nv2aSurface s=surface(8,8,32);uint32_t *c=color_ram(&s);
    wide=0;memset(c,0,8*8*4);triangle(s,0,0,0,8,0,0,8);
    if(queued_mode)q_sync(1);else gl_sync(1);
    for(unsigned i=0;i<64;i++)c[i]=0xff00ff00;
    if(queued_mode)q_flip();else gl_flip();assert(swap_pixel==0xff00ff00);
    for(unsigned i=0;i<64;i++)c[i]=0xff0000ff;
    if(queued_mode)q_flip();else gl_flip();assert(swap_pixel==0xff0000ff);
    puts("PASS real GL presentation observes CPU-only writes after sync(1), without a new draw");
}
static void complete(void) {if(queued_mode)q_sync(0);else gl_sync(0);}
static void fenced_real_test(void)
{
    Nv2aSurface s=surface(8,8,32);Nv2aRenderState rs={0};uint32_t *c=color_ram(&s);
    memset(c,0,64*4);triangle(s,0,0,0,8,0,0,8);
    assert(c[9]==0);complete();assert(c[9]==0xffff0000);
    c[9]=0x7f123456;
    if(queued_mode)q_clear(&s,&rs,0x80,0x99000000,0);else gl_clear(&s,&rs,0x80,0x99000000,0);
    assert(c[9]==0x7f123456);complete();assert(c[9]==0x99123456);
    memset(c,0,64*4);triangle(s,0,0,0,8,0,0,8);
    Nv2aSurface dst=surface(8,8,32);dst.color_va=0x240000;uint32_t *out=color_ram(&dst);
    sample_va=s.color_va;sample_w=sample_h=8;sample_pitch=32;
    memset(out,0,64*4);triangle(dst,1,0,0,8,0,0,8);
    assert(c[9]==0xffff0000&&out[9]==0);complete();assert(out[9]==0xffff0000);
    c[0]=0xff0000ff;triangle(dst,1,0,0,8,0,0,8);complete();assert(out[9]==0xff0000ff);
    sample_va=0x280000;sample_w=sample_h=2;sample_pitch=8;
    puts("PASS real GL fenced raw draw/publish boundaries, clean CPU edits and render-target feedback");
    clear_only_present_test();cpu_only_present_test();
}
/* Full shader assembly + direct CMP queue copy + combined ring submission.
 * Alternating CMP and float batches must reset the program's decode mask. */
static void packed_cmp_draw_test(void)
{
    Nv2aSurface sf=surface(8,8,32);
    Nv2aRenderState rs={0};
    if(queued_mode)q_clear(&sf,&rs,0xf0,0,0);else gl_clear(&sf,&rs,0xf0,0,0);
    float attrs[3*64]={0}; const float pos[6]={0,0,8,0,0,8};
    uint32_t indices[3]={0,1,2}, packed[3]={1023,1023,1023};
    for(unsigned i=0;i<3;i++) {attrs[i*64]=pos[i*2];attrs[i*64+1]=pos[i*2+1];attrs[i*64+3]=1;}
    regs[0x288/4]=4;regs[0x28c/4]=0x14<<8;regs[0x1e70/4]=0;regs[0x1b0c/4]=0;
    Nv2aRawBatch b={0};b.surface=sf;b.regs=regs;b.prim=5;b.vertex_count=b.index_count=3;
    b.indices=indices;b.attrs=attrs;b.attr_present=9;b.attr_direct=8;
    b.direct[3].ptr=(const uint8_t *)packed;b.direct[3].type=6;b.direct[3].size=3;b.direct[3].stride=4;
    for(int pass=0;pass<5;pass++) {
        for(int i=0;i<3;i++)packed[i]=(pass%2)?(1023u<<11):1023;
        if(queued_mode)q_draw(&b);else gl_draw_raw(&b);
        complete(); assert(color_ram(&sf)[9]==((pass%2)?0xff00ff00u:0xffff0000u));
        if(queued_mode)q_flip();else gl_flip();
    }
    triangle(sf,0,0,0,8,0,0,8);complete();assert(color_ram(&sf)[9]==0xffff0000);
    Nv2aGlPerf perf;nv2a_gl_perf_read(&perf);
    if(perf_detail()) {
        uint64_t sum=perf.lookup_ns+perf.upload_ns+perf.uniform_ns+perf.bind_ns+perf.state_ns+perf.submit_ns+perf.other_ns;
        assert(sum<=perf.draw_ns && sum>=perf.draw_ns*9/10);
        assert(perf.submit_ns && perf.uniform_calls && perf.maps);
    }
    puts("PASS real GL direct CMP production shaders/queue, alternating colors, float reset, segmented streaming and profiling sum");
}
int main(int argc,char **argv)
{
#if defined(__linux__)
    assert(prctl(PR_SET_DUMPABLE,0)==0); /* Bound expected-abort negative controls. */
#endif
    setenv("RECOMP_GL_SCALE","1",1);setenv("RECOMP_PROG_CACHE","0",1);unsetenv("RECOMP_GL_THREAD");
    if(argc>1&&!strncmp(argv[1],"fenced",6)){
        setenv("MANHUNT_GL_SYNC","fenced",1);
        if(strstr(argv[1],"queued")){queued_mode=s_q_on=1;assert(q_start());}
        else assert(ready());
        fenced_real_test();packed_cmp_draw_test();puts("PASS experimental fenced ownership synthetic proof; arbitrary unsynchronized CPU access and hardware performance UNVERIFIED");
        fflush(stdout);_Exit(0);
    }
    if(argc>1){
        queued_mode=s_q_on=1;assert(q_start());clear_only_present_test();cpu_only_present_test();packed_cmp_draw_test();
        puts("PASS real GL queued draw/sync/presentation returns after consuming guest RAM");
        fflush(stdout);_Exit(0);
    }
    assert(ready());
    clear_only_present_test();clear_depth_test();linear_color_formats_test();draw_sample_test();render_target_feedback_test();wide_reset_test();cpu_only_present_test();
    packed_cmp_draw_test();
    puts("PASS real GL production-path synthetic suite; gameplay/hardware parity UNVERIFIED");
    return 0;
}
