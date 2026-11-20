/* Runs the production renderer on a real desktop GL driver. Synthetic pixels
 * only: no game assets, guest simulation, console FPS or gameplay acceptance. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
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
/* test_direct_ring.c: a hash of every whole presented frame, in order. */
static int swap_hash_on;
static unsigned swap_count;
static uint64_t swap_hashes[4096];
static void observed_swap(SDL_Window *window)
{
    int w,h;
    SDL_GL_GetDrawableSize(window,&w,&h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glReadPixels(w/2,h/2,1,1,GL_BGRA,GL_UNSIGNED_BYTE,&swap_pixel);
    if(swap_hash_on) {
        static uint8_t *px;static size_t cap;size_t n=(size_t)w*h*4;uint64_t hash=1469598103934665603ull;
        if(n>cap){free(px);px=malloc(n);cap=n;assert(px);}
        glReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,px);
        for(size_t i=0;i<n;i++)hash=(hash^px[i])*1099511628211ull;
        unsigned k=__atomic_load_n(&swap_count,__ATOMIC_RELAXED);
        if(k<4096)swap_hashes[k]=hash;
        __atomic_store_n(&swap_count,k+1,__ATOMIC_RELEASE);
    }
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
/* One textured triangle over the 8x8 surface with explicit stage-0 state
 * and per-vertex texcoords (texels for linear images, normalised else). */
static void tex_triangle(Nv2aSurface s,uint32_t va,uint32_t format,uint32_t addr,uint32_t filter,
                         uint32_t control0,uint32_t border,uint32_t rect,uint32_t pitch,const float tc[6])
{
    float attrs[3*64]={0};uint32_t indices[3]={0,1,2};Nv2aRawBatch b={0};
    const float pos[6]={0,0,8,0,0,8};
    for(unsigned v=0;v<3;v++){
        float *a=attrs+v*64;a[0]=pos[v*2];a[1]=pos[v*2+1];a[2]=0;a[3]=1;
        a[12]=1;a[15]=1;a[36]=tc[v*2];a[37]=tc[v*2+1];a[39]=1;
    }
    regs[0x288/4]=8;regs[0x28c/4]=0x18<<8;regs[0x1e70/4]=1;
    regs[0x1b00/4]=va;regs[0x1b04/4]=format;regs[0x1b08/4]=addr;regs[0x1b0c/4]=control0;
    regs[0x1b10/4]=pitch<<16;regs[0x1b14/4]=filter;regs[0x1b1c/4]=rect;regs[0x1b24/4]=border;
    b.surface=s;b.regs=regs;b.prim=5;b.vertex_count=b.index_count=3;b.indices=indices;b.attrs=attrs;
    b.attr_present=(1u<<0)|(1u<<3)|(1u<<9);b.tex_va[0]=va;
    gl_draw_raw(&b);
    regs[0x1b08/4]=0;regs[0x1b24/4]=0;
}
static void border_mip_test(void)
{
    Nv2aSurface s=surface(8,8,32);uint32_t *c=color_ram(&s);
    const float outside[6]={10,10,10,10,10,10};
    /* Linear 2x2 red, address mode 4 on both axes, sampled outside. */
    uint32_t *lin=(uint32_t*)(guest+0x2c0000);for(unsigned i=0;i<4;i++)lin[i]=0xffff0000;
    const uint32_t lfmt=(0x12u<<8)|(2u<<4),near=(1u<<16)|(1u<<24),addr4=4u|(4u<<8);
    memset(c,0,256);tex_triangle(s,0x2c0000,lfmt|8u,addr4,near,0x4003ffc0,0xff0000ff,(2u<<16)|2u,8,outside);
    assert(c[1*8+1]==0xff0000ff);                  /* BORDER_SOURCE colour: the register */
    memset(c,0,256);tex_triangle(s,0x2c0000,lfmt|8u,addr4,near,0x4003ffc0,0xff00ff00,(2u<<16)|2u,8,outside);
    assert(c[1*8+1]==0xff00ff00);                  /* a new border colour reaches the sampler */
    memset(c,0,256);tex_triangle(s,0x2c0000,lfmt,addr4,near,0x4003ffc0,0xff0000ff,(2u<<16)|2u,8,outside);
    assert(c[1*8+1]==0xffff0000);                  /* texture source: clamped to the edge */
    /* Swizzled 4x4 A8R8G8B8, three levels of one colour each, minified 4:1
     * (texcoords 0..8 over 8 pixels): nearest-LOD box filtering reads level 2. */
    uint32_t *sw=(uint32_t*)(guest+0x2d0000);
    for(unsigned i=0;i<16;i++)sw[i]=0xffff0000;
    for(unsigned i=0;i<4;i++)sw[16+i]=0xff00ff00;
    sw[20]=0xff0000ff;
    const uint32_t sfmt=(2u<<4)|(0x06u<<8)|(3u<<16)|(2u<<20)|(2u<<24);
    const float mini[6]={0,0,8,0,0,8};
    memset(c,0,256);tex_triangle(s,0x2d0000,sfmt,0,(3u<<16)|(1u<<24),0x4003ffc0,0,0,0,mini);
    assert(c[3*8+3]==0xff0000ff);
    /* MAX_LOD_CLAMP 0 (CONTROL0 bits 6-17) keeps it at level 0. */
    memset(c,0,256);tex_triangle(s,0x2d0000,sfmt,0,(3u<<16)|(1u<<24),0x40000000,0,0,0,mini);
    assert(c[3*8+3]==0xffff0000);
    /* LOD-0 box filtering ignores the levels. */
    memset(c,0,256);tex_triangle(s,0x2d0000,sfmt,0,(1u<<16)|(1u<<24),0x4003ffc0,0,0,0,mini);
    assert(c[3*8+3]==0xffff0000);
    puts("PASS real GL texture border colour and source bit, mip level selection and LOD clamp");
}
/* ── Texture-shader modes (gl_psh.c texshader_fetch) against C references ──
 * One draw over the 8x8 surface: per-stage texture state, texcoords constant
 * over the triangle, the final combiner outputting T<out>. Nearest filtering,
 * so each pixel is one texel. */
typedef struct { uint32_t va, format; } TsStage;
static uint32_t ts_draw(const TsStage st[4],uint32_t prog,uint32_t other,uint32_t dotmap,
                        const float tc[4][4],unsigned out)
{
    Nv2aSurface s=surface(8,8,32);uint32_t *c=color_ram(&s);
    float attrs[3*64]={0};uint32_t indices[3]={0,1,2};Nv2aRawBatch b={0};
    const float pos[6]={0,0,8,0,0,8};
    for(unsigned v=0;v<3;v++){
        float *a=attrs+v*64;a[0]=pos[v*2];a[1]=pos[v*2+1];a[2]=0;a[3]=1;a[12]=1;a[15]=1;
        for(unsigned t=0;t<4;t++)memcpy(a+36+4*t,tc[t],16);
    }
    regs[0x288/4]=8+out;regs[0x28c/4]=(0x10u|(8+out))<<8;regs[0x1e60/4]=0;
    regs[0x1e70/4]=prog;regs[0x1e78/4]=other;regs[0x1e74/4]=dotmap;
    for(unsigned t=0;t<4;t++){
        uint32_t base=(0x1b00+t*0x40)/4;
        regs[base]=st[t].va;regs[base+1]=st[t].format;regs[base+2]=0;
        regs[base+3]=st[t].va?0x40000000u:0;regs[base+4]=0;regs[base+5]=(1u<<16)|(1u<<24);regs[base+7]=0;
        b.tex_va[t]=st[t].va;
    }
    b.surface=s;b.regs=regs;b.prim=5;b.vertex_count=b.index_count=3;b.indices=indices;b.attrs=attrs;
    b.attr_present=(1u<<0)|(1u<<3)|(0xFu<<9);
    memset(c,0,256);gl_draw_raw(&b);
    uint32_t px=c[3*8+3];
    for(unsigned t=0;t<4;t++){uint32_t base=(0x1b00+t*0x40)/4;regs[base]=regs[base+1]=regs[base+3]=0;}
    regs[0x1e70/4]=regs[0x1e78/4]=regs[0x1e74/4]=0;
    return px;
}
/* Swizzled A8R8G8B8, n x n (power of two), log2 n in `l`. */
static uint32_t ts_fmt(unsigned l,unsigned levels,int cube)
{
    return (2u<<4)|(0x06u<<8)|(levels<<16)|(l<<20)|(l<<24)|(cube?4u:0u);
}
static float sign3(uint8_t x) {return x>=128?(x-256.0f)/127.0f:x/127.0f;}
static void ts_put(uint32_t va,unsigned n,unsigned x,unsigned y,uint32_t argb)
{
    ((uint32_t*)(guest+va))[swizzle_index(x,y,n,n)]=argb;
}
static uint32_t ts_texel(unsigned face,unsigned x,unsigned y)
{
    return 0xff000000u|((face*40u+20u)<<16)|((x*100u+10u)<<8)|(y*100u+30u);
}
/* CUBEMAP: six 2x2 faces of two levels each, faces 128 bytes apart (one
 * face's chain is 20 bytes, aligned up), against the CPU renderer's face
 * selection (rc_stage_fetch in nv2a_pb_exec.c, GL's cube rules). */
static void cube_mode_test(void)
{
    const uint32_t va=0x2e0000,stride=128;
    for(unsigned f=0;f<6;f++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)
        ts_put(va+f*stride,2,x,y,ts_texel(f,x,y));
    for(unsigned f=0;f<6;f++)ts_put(va+f*stride+16,1,0,0,0xff000000u); /* level 1 */
    const float dirs[][3]={{1,.3f,-.6f},{1,-.4f,.5f},{-1,.6f,.3f},{-1,-.2f,-.7f},{.3f,1,.6f},{-.5f,1,-.4f},
                           {.4f,-1,.3f},{-.6f,-1,-.2f},{.2f,.5f,1},{-.7f,-.3f,1},{.6f,.4f,-1},{-.3f,-.6f,-1}};
    TsStage st[4]={{va,ts_fmt(1,2,1)},{0,0},{0,0},{0,0}};
    for(unsigned d=0;d<sizeof dirs/sizeof dirs[0];d++){
        float x=dirs[d][0],y=dirs[d][1],z=dirs[d][2],ax=fabsf(x),ay=fabsf(y),az=fabsf(z),ma,sc,tcc;unsigned face;
        if(ax>=ay&&ax>=az){face=x>0?0:1;ma=ax;sc=x>0?-z:z;tcc=-y;}
        else if(ay>=az){face=y>0?2:3;ma=ay;sc=x;tcc=y>0?z:-z;}
        else{face=z>0?4:5;ma=az;sc=z>0?x:-x;tcc=-y;}
        unsigned u=(unsigned)((sc/ma+1)*0.5f*2),v=(unsigned)((tcc/ma+1)*0.5f*2);
        float tc[4][4]={{x,y,z,1},{0},{0},{0}};
        uint32_t px=ts_draw(st,3,0,0,tc,0);
        if(px!=ts_texel(face,u,v)){fprintf(stderr,"cube dir %u: got %08X want %08X (face %u %u,%u)\n",d,px,ts_texel(face,u,v),face,u,v);assert(0);}
    }
    /* A cube mode on a 2D texture reads black; MANHUNT_GL_TEXSHADER=0 is
     * covered by test_gl.c's source checks. */
    TsStage flat[4]={{va,ts_fmt(1,1,0)},{0,0},{0,0},{0,0}};
    float tc[4][4]={{1,.3f,-.6f,1},{0},{0},{0}};
    assert((ts_draw(flat,3,0,0,tc,0)&0xffffffu)==0);   /* no texture: GL's (0, 0, 0, 1) */
    puts("PASS real GL cube maps: 6 faces at the 128-byte-aligned mip-chain stride, face/texel selection matches the CPU reference");
}
/* BUMPENVMAP and BUMPENVMAP_LUM: stage 0 a constant (du, dv) map, stage 1 a
 * 4x4 colour texture at tc1 + M * (du, dv); D3D's convention u' = u + M00 du
 * + M10 dv, v' = v + M01 du + M11 dv. */
static void bump_mode_test(void)
{
    const uint32_t bva=0x2e1000,cva=0x2e2000;
    const uint8_t du=0x40,dv=0xC0,lum=0x80;
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)ts_put(bva,2,x,y,0xff000000u|((uint32_t)lum<<16)|((uint32_t)dv<<8)|du);
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)ts_put(cva,4,x,y,0xff000000u|((x*60u+20u)<<16)|((y*60u+20u)<<8)|0x80u);
    const float mats[][4]={{1,0,0,1},{0,1,1,0},{.5f,.25f,-.25f,.5f}};
    const float tcs[][2]={{.1f,.6f},{.6f,.1f},{.4f,.45f}};
    TsStage st[4]={{bva,ts_fmt(1,1,0)},{cva,ts_fmt(2,1,0)},{0,0},{0,0}};
    for(unsigned m=0;m<3;m++)for(unsigned lm=0;lm<2;lm++){
        const float *M=mats[m],scale=lm?1.0f:0,offset=lm?0.25f:0;
        memcpy(&regs[(0x1b68)/4],M,16);memcpy(&regs[0x1b78/4],&scale,4);memcpy(&regs[0x1b7c/4],&offset,4);
        float su=sign3(du),sv=sign3(dv);
        float u=tcs[m][0]+M[0]*su+M[2]*sv,v=tcs[m][1]+M[1]*su+M[3]*sv;
        unsigned x=(unsigned)(u*4),y=(unsigned)(v*4);
        float tc[4][4]={{.5f,.5f,0,1},{tcs[m][0],tcs[m][1],0,1},{0},{0}};
        uint32_t px=ts_draw(st,1u|((lm?7u:6u)<<5),0,0,tc,1);
        uint32_t want=0xff000000u|((x*60u+20u)<<16)|((y*60u+20u)<<8)|0x80u;
        if(lm){
            /* T1 *= scale * T0.r + offset: every channel, alpha included. */
            float f=scale*(lum/255.0f)+offset;uint32_t w2=0;
            for(int sh=0;sh<32;sh+=8){float ch=((want>>sh)&255)/255.0f*f;w2|=(uint32_t)(ch*255.0f+0.5f)<<sh;}
            for(int sh=0;sh<32;sh+=8){int d=(int)((px>>sh)&255)-(int)((w2>>sh)&255);
                if(d<-1||d>1){fprintf(stderr,"bump lum m%u: got %08X want %08X\n",m,px,w2);assert(0);}}
        } else if(px!=want){fprintf(stderr,"bump m%u: got %08X want %08X (%u,%u)\n",m,px,want,x,y);assert(0);}
    }
    memset(&regs[0x1b68/4],0,24);
    puts("PASS real GL BUMPENVMAP/_LUM: SET_TEXTURE_SET_BUMP_ENV_MAT/SCALE/OFFSET offsets and luminance match the D3D reference");
}
/* DOTPRODUCT + DOT_ST, dot mapping 3 (-1..1, xemu's sign3): T2 at
 * (dot(tc1, N), dot(tc2, N)), N from stage 0's texel. */
static void dot_mode_test(void)
{
    const uint32_t nva=0x2e3000,cva=0x2e4000;
    const uint8_t nb[3]={0x40,0xC0,0x7F};                  /* r, g, b */
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)ts_put(nva,2,x,y,0xff000000u|((uint32_t)nb[0]<<16)|((uint32_t)nb[1]<<8)|nb[2]);
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)ts_put(cva,4,x,y,0xff000000u|((x*60u+20u)<<16)|((y*60u+20u)<<8)|0x11u);
    const float tc1s[][3]={{.2f,.1f,.3f},{.9f,.3f,.1f},{.1f,-.4f,.2f}};
    const float tc2s[][3]={{.5f,.2f,.4f},{.1f,.1f,.6f},{.3f,-.6f,.1f}};
    TsStage st[4]={{nva,ts_fmt(1,1,0)},{0,0},{cva,ts_fmt(2,1,0)},{0,0}};
    for(unsigned i=0;i<3;i++){
        float n[3]={sign3(nb[0]),sign3(nb[1]),sign3(nb[2])};
        float d1=tc1s[i][0]*n[0]+tc1s[i][1]*n[1]+tc1s[i][2]*n[2];
        float d2=tc2s[i][0]*n[0]+tc2s[i][1]*n[1]+tc2s[i][2]*n[2];
        unsigned x=(unsigned)(d1*4),y=(unsigned)(d2*4);
        assert(d1>0&&d1<1&&d2>0&&d2<1);
        float tc[4][4]={{.5f,.5f,0,1},{tc1s[i][0],tc1s[i][1],tc1s[i][2],1},{tc2s[i][0],tc2s[i][1],tc2s[i][2],1},{0}};
        uint32_t px=ts_draw(st,1u|(17u<<5)|(9u<<10),0,0x033,tc,2);
        uint32_t want=0xff000000u|((x*60u+20u)<<16)|((y*60u+20u)<<8)|0x11u;
        if(px!=want){fprintf(stderr,"dot_st %u: got %08X want %08X (%.3f %.3f)\n",i,px,want,d1,d2);assert(0);}
    }
    /* The other chains compile, link and draw (strict mode aborts on a
     * failure); stage 3 / stage 2 sample the cube from cube_mode_test.
     * DOT_STR_CUBE with N = (0, 0, 1) mapped 0..1 reads direction
     * (tc1.z, tc2.z, tc3.z) = (1, 0.3, -0.6): face 0 texel (1, 0). */
    const uint32_t cube=0x2e0000;
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)ts_put(nva,2,x,y,0xff0000ffu);
    const uint32_t chains[][2]={{1u|(17u<<5)|(17u<<10)|(14u<<15),1},{1u|(17u<<5)|(17u<<10)|(12u<<15),1},
        {1u|(17u<<5)|(11u<<10)|(12u<<15),1},{1u|(17u<<5)|(17u<<10)|(18u<<15),1},
        {1u|(17u<<5)|(17u<<10)|(13u<<15),0},{1u|(17u<<5)|(10u<<10),0},{2u,0}};
    for(unsigned i=0;i<sizeof chains/sizeof chains[0];i++){
        TsStage cs[4]={{nva,ts_fmt(1,1,0)},{0,0},{chains[i][0]==(1u|(17u<<5)|(11u<<10)|(12u<<15))?cube:cva,
                       chains[i][0]==(1u|(17u<<5)|(11u<<10)|(12u<<15))?ts_fmt(1,2,1):ts_fmt(2,1,0)},
                       {chains[i][1]?cube:cva,chains[i][1]?ts_fmt(1,2,1):ts_fmt(2,1,0)}};
        float tc[4][4]={{.5f,.5f,0,1},{0,0,1,1},{0,0,.3f,1},{0,0,-.6f,1}};
        uint32_t px=ts_draw(cs,chains[i][0],0,0,tc,3);
        if(i==0&&px!=ts_texel(0,1,0)){fprintf(stderr,"dot_str_cube: got %08X want %08X\n",px,ts_texel(0,1,0));assert(0);}
    }
    puts("PASS real GL DOTPRODUCT + DOT_ST with SET_DOT_RGBMAPPING -1..1 matches the CPU reference;"
         " DOT_STR_CUBE reads the reference texel; RFLCT_DIFF/SPEC/SPEC_CONST, STR_3D, ZW, PROJECT3D compile and draw");
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
/* Item 13: the flip shows the surface the title flipped (the executor's
 * address, nv2a_gl_note_flip), not the last one drawn or cleared; s_last
 * when no address came or no surface has it, or MANHUNT_PRESENT_FLIPPED=0.
 * Direct: the registered backend's flip; queued: the address rides the
 * flip record. */
static void be_flip(void) {if(queued_mode)q_flip();else s_backend_gl.flip();}
static void flipped_present_test(void)
{
    Nv2aSurface a=surface(8,8,32),b=surface(8,8,32);Nv2aRenderState rs={0};
    b.color_va=0x240000;wide=0;
    if(queued_mode){q_clear(&a,&rs,0xf0,0xff112233,0);q_clear(&b,&rs,0xf0,0xff445566,0);}
    else{gl_clear(&a,&rs,0xf0,0xff112233,0);gl_clear(&b,&rs,0xf0,0xff445566,0);}
    nv2a_gl_note_flip(a.color_va);be_flip();assert(swap_pixel==0xff112233);
    be_flip();assert(swap_pixel==0xff445566);                 /* no address: the last target */
    nv2a_gl_note_flip(0x3f0000);be_flip();assert(swap_pixel==0xff445566);   /* no surface there */
    nv2a_gl_note_flip(b.color_va);be_flip();assert(swap_pixel==0xff445566);
    /* Queued: the address of each flip, not of a later one noted meanwhile. */
    nv2a_gl_note_flip(a.color_va);be_flip();nv2a_gl_note_flip(b.color_va);
    assert(swap_pixel==0xff112233);be_flip();assert(swap_pixel==0xff445566);
    s_present_flipped=0;
    nv2a_gl_note_flip(a.color_va);be_flip();assert(swap_pixel==0xff445566);
    s_present_flipped=1;
    nv2a_gl_note_flip(a.color_va);be_flip();assert(swap_pixel==0xff112233);
    puts("PASS real GL presents the flipped surface, falls back to the last target when unknown, and MANHUNT_PRESENT_FLIPPED=0 restores it");
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
    clear_only_present_test();cpu_only_present_test();flipped_present_test();
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
    setenv("RECOMP_GL_SCALE","1",1);setenv("MANHUNT_GL_MIPMAPS","1",1);setenv("RECOMP_PROG_CACHE","0",1);unsetenv("RECOMP_GL_THREAD");
    if(argc>1&&!strncmp(argv[1],"fenced",6)){
        setenv("MANHUNT_GL_SYNC","fenced",1);
        if(strstr(argv[1],"queued")){queued_mode=s_q_on=1;assert(q_start());}
        else assert(ready());
        fenced_real_test();packed_cmp_draw_test();puts("PASS experimental fenced ownership synthetic proof; arbitrary unsynchronized CPU access and hardware performance UNVERIFIED");
        fflush(stdout);_Exit(0);
    }
    if(argc>1){
        queued_mode=s_q_on=1;assert(q_start());clear_only_present_test();cpu_only_present_test();flipped_present_test();packed_cmp_draw_test();
        puts("PASS real GL queued draw/sync/presentation returns after consuming guest RAM");
        fflush(stdout);_Exit(0);
    }
    assert(ready());
    clear_only_present_test();clear_depth_test();linear_color_formats_test();draw_sample_test();border_mip_test();cube_mode_test();bump_mode_test();dot_mode_test();render_target_feedback_test();wide_reset_test();cpu_only_present_test();flipped_present_test();
    packed_cmp_draw_test();
    puts("PASS real GL production-path synthetic suite; gameplay/hardware parity UNVERIFIED");
    return 0;
}
