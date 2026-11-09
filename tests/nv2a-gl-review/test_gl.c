/* Synthetic GL contract driver. Includes production renderer; fake GL tests
 * host ownership/error paths, never GPU rasterization or hardware FPS. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
static int fail_allocation;
static void *review_malloc(size_t n) {return fail_allocation?NULL:malloc(n);}
#ifndef NV2A_GL_SOURCE
#define NV2A_GL_SOURCE "../../game/src/nv2a_gl/nv2a_gl.c"
#endif
#define malloc review_malloc
#include NV2A_GL_SOURCE
#undef malloc

#define DEFINE_DEFAULT(ret,name,args) static ret fake_##name args { return (ret)0; } PFN_##name p_##name;
NV2A_GL_FUNCS(DEFINE_DEFAULT)
#undef DEFINE_DEFAULT

typedef struct { unsigned w,h; uint32_t *pixels; } Image;
static Image image[2048];
static unsigned next_id=1,bound_tex,framebuffer,attached[2048],depth_attached[2048],uploads,reads;
static int scissor_on,scissor_x,scissor_y,scissor_w,scissor_h;
static uint32_t clear_color;
static unsigned color_mask=0xffffffffu;
static int incomplete;
static uint32_t inject_ram_on_read;
static GLenum injected_error;
static GLenum get_error(void) {GLenum e=injected_error;injected_error=0;return e;}
static int unpack_row;
static uint8_t guest[4u<<20];
static const uint32_t *fake_live_regs;
static int fake_widescreen;
int gl_review_sdl_failure;
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)guest; }
int nv2a_backend_decode_texture(const Nv2aTexture *t,uint32_t *out) { (void)t;(void)out;return 0; }
int nv2a_backend_surface_writable(const Nv2aSurface *s) { return s->bytes_per_pixel==4 && s->pitch>=s->width*4 && s->color_va+(uint64_t)s->pitch*(s->height+s->clip_y)<=sizeof guest; }
static const Nv2aBackend *registered_backend;
void nv2a_backend_register(const Nv2aBackend *b) {registered_backend=b;}
void nv2a_pb_write_report(uint32_t a,uint32_t b) {(void)a;(void)b;}
uint8_t *nv2a_pb_reg_dirty(void) {static uint8_t r[128]; return r;}
const uint32_t *nv2a_pb_registers(void) {return fake_live_regs;}
int xbox_gamma_ramp(uint8_t r[256],uint8_t g[256],uint8_t b[256]) {(void)r;(void)g;(void)b;return 0;}
void xbox_nx_spread_thread(void) {}
void xbox_nx_track_thread(void *p) {(void)p;}
void xbox_vblank_report(double d,char *b,size_t n) {(void)d;if(n)*b=0;}
int xbox_video_widescreen(void) {return fake_widescreen;}
int nv2a_gl_load(void *(*f)(const char *)) {(void)f;return 1;}
static void gen(GLsizei n,GLuint *v) {while(n--)*v++=next_id++;assert(next_id<2048);}
static void bind(GLenum target,GLuint v) {(void)target;bound_tex=v;}
static void pixel_store(GLenum param,GLint v) {if(param==GL_UNPACK_ROW_LENGTH)unpack_row=v;}
static void upload(GLenum t,GLint level,GLint fmt,GLsizei w,GLsizei h,GLint border,GLenum ext,GLenum type,const void *pixels)
{
    (void)t;(void)level;(void)fmt;(void)border;(void)ext;(void)type;
    Image *im=&image[bound_tex]; free(im->pixels); im->w=w;im->h=h;
    im->pixels=calloc((size_t)w*h,4);assert(im->pixels);
    if(pixels)for(int y=0;y<h;y++)memcpy(im->pixels+(size_t)y*w,(const uint32_t*)pixels+(size_t)y*(unpack_row?unpack_row:w),(size_t)w*4);
    uploads++;
}
static void subupload(GLenum t,GLint level,GLint x,GLint y,GLsizei w,GLsizei h,GLenum ext,GLenum type,const void *pixels)
{
    (void)t;(void)level;(void)ext;(void)type;
    Image *im=&image[bound_tex];assert(x+w<=(int)im->w&&y+h<=(int)im->h);
    for(int j=0;j<h;j++)memcpy(im->pixels+(size_t)(y+j)*im->w+x,(const uint32_t*)pixels+(size_t)j*(unpack_row?unpack_row:w),(size_t)w*4);
    uploads++;
}
static void bindfb(GLenum target,GLuint f) {(void)target;framebuffer=f;}
static void attach(GLenum target,GLenum attachment,GLenum tex_target,GLuint tex,GLint level) {(void)target;(void)tex_target;(void)level;if(attachment==GL_COLOR_ATTACHMENT0)attached[framebuffer]=tex;else if(attachment==GL_DEPTH_STENCIL_ATTACHMENT)depth_attached[framebuffer]=tex;else assert(0);}
static GLenum complete(GLenum target) {(void)target;return incomplete?0:GL_FRAMEBUFFER_COMPLETE;}
static void readpixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum fmt,GLenum type,void *out)
{
    (void)type;Image *im=&image[fmt==GL_DEPTH_STENCIL?depth_attached[framebuffer]:attached[framebuffer]];assert(im->pixels);
    assert(x+w<=(int)im->w&&y+h<=(int)im->h);
    for(int j=0;j<h;j++)memcpy((uint32_t*)out+(size_t)j*w,im->pixels+(size_t)(y+j)*im->w+x,(size_t)w*4);
    reads++;
    if(inject_ram_on_read){guest[inject_ram_on_read]^=1;inject_ram_on_read=0;}
}
static void enable(GLenum cap) {if(cap==GL_SCISSOR_TEST)scissor_on=1;}
static void disable(GLenum cap) {if(cap==GL_SCISSOR_TEST)scissor_on=0;}
static void scissor(GLint x,GLint y,GLsizei w,GLsizei h) {scissor_x=x;scissor_y=y;scissor_w=w;scissor_h=h;}
static void color(GLfloat r,GLfloat g,GLfloat b,GLfloat a) {clear_color=((uint32_t)(a*255+0.5f)<<24)|((uint32_t)(r*255+0.5f)<<16)|((uint32_t)(g*255+0.5f)<<8)|(uint32_t)(b*255+0.5f);}
static void mask(GLboolean r,GLboolean g,GLboolean b,GLboolean a) {color_mask=(r?0xff0000u:0)|(g?0xff00u:0)|(b?0xffu:0)|(a?0xff000000u:0);}
static void clear(GLbitfield bits)
{
    if(!(bits&GL_COLOR_BUFFER_BIT))return;
    Image *im=&image[attached[framebuffer]];assert(im->pixels);
    for(unsigned y=0;y<im->h;y++)for(unsigned x=0;x<im->w;x++) {
        if(scissor_on&&((int)x<scissor_x||(int)y<scissor_y||(int)x>=scissor_x+scissor_w||(int)y>=scissor_y+scissor_h))continue;
        uint32_t *p=im->pixels+(size_t)y*im->w+x;*p=(*p&~color_mask)|(clear_color&color_mask);
    }
}
static void init_fake(void)
{
#define SET_DEFAULT(ret,name,args) p_##name=fake_##name;
    NV2A_GL_FUNCS(SET_DEFAULT)
#undef SET_DEFAULT
    p_glGenTextures=gen;p_glGenFramebuffers=gen;p_glGenRenderbuffers=gen;
    p_glBindTexture=bind;p_glPixelStorei=pixel_store;p_glTexImage2D=upload;p_glTexSubImage2D=subupload;
    p_glBindFramebuffer=bindfb;p_glFramebufferTexture2D=attach;p_glCheckFramebufferStatus=complete;p_glReadPixels=readpixels;
    p_glGetError=get_error;
    p_glEnable=enable;p_glDisable=disable;p_glScissor=scissor;p_glClearColor=color;p_glColorMask=mask;p_glClear=clear;
    s_state=1;s_frame=10;
}
static void cache_test(void)
{
    const unsigned va=0x10000,w=512,h=512;
    memset(guest+va,0,w*h*4);
    unsigned t=tex_get(va,0x06,w,h,0),before=uploads;
    guest[va+4]=0x5a;
    assert(tex_get(va,0x06,w,h,0)==t);
    assert(uploads>before); /* catches once-per-frame skip as well as sampled hash */
    assert(image[t].pixels[1]==0x5a);
    before=uploads;
    guest[va+12344]=0x36;
    s_frame++;
    tex_get(va,0x06,w,h,0);
    assert(uploads>before); /* old sample stride omitted this content */
    puts("PASS exact swizzled texture invalidation: byte4/same frame and unsampled/next frame");
}
static void palette_test(void)
{
    const unsigned va=0x180000,pal=0x1a0000;
    /* A 32-entry palette, indices selecting entry1. Change that entry within
     * the same executor frame and require the production upload to change. */
    memset(guest+va,1,16);memset(guest+pal,0,128);
    s_palette_reg=0x1000u|(3u<<2);s_palette_va=pal;
    unsigned t=tex_get(va,0x0b,4,4,0),before=uploads;
    guest[pal+4]=0x91;
    tex_get(va,0x0b,4,4,0);
    assert(uploads>before&&image[t].pixels[0]==0x91);
    puts("PASS palette change in same frame reaches decoded texture");
}
static void shader_modes_test(void)
{
    Nv2aPshKey k={0};char buf[65536];
    nv2a_shader_vk=0;
    for(unsigned stage=0;stage<4;stage++)for(unsigned mode=0;mode<32;mode++) {
        k.shader_program=mode<<(stage*5);
        int supported=mode==0||mode==1||mode==4||mode==5||((mode==15||mode==16)&&stage>0);
        int n=nv2a_gl_psh(&k,buf,sizeof buf);
        assert((n>=0)==supported);
    }
    puts("PASS all GL texture modes: implemented modes emit, other modes reject");
}
static void widescreen_classification_test(void)
{
    const uint64_t hud=0x43720A569E4D3893ull,movie=0xBA9E387C24FCECE1ull;
    const uint64_t overlay=0xD7829B66D519BE47ull,world=0x1122334455667788ull;
    GlSurf s={0};Nv2aRawBatch b={0};float attrs[2*64]={0},out[2];
    s.w=640;s.h=480;b.attrs=attrs;b.vertex_count=2;b.xform=2;
    attrs[0]=0;attrs[64]=640;fake_widescreen=1;
    assert(wide_prog_2d(hud)&&wide_prog_2d(movie)&&wide_prog_2d(overlay));
    wide_2d(&b,&s,movie,out);assert(out[0]==.75f&&out[1]==320);
    wide_2d(&b,&s,hud,out);assert(out[0]==0&&out[1]==0);
    wide_2d(&b,&s,overlay,out);assert(out[0]==0&&out[1]==0);
    attrs[0]=20;attrs[64]=100;
    wide_2d(&b,&s,hud,out);assert(out[0]==.75f&&out[1]==320);
    /* Reusing addresses sampled by known movie programs must never learn a
     * new program: classification uses only the immutable program allowlist. */
    for(unsigned i=0;i<8;i++){b.tex_va[0]=0x10000u+i*0x1000u;assert(!wide_prog_2d(world));}
    assert(!wide_prog_2d(0));
    s.w=320;wide_2d(&b,&s,hud,out);assert(out[0]==0&&out[1]==0);
    s.w=640;fake_widescreen=0;wide_2d(&b,&s,movie,out);assert(out[0]==0&&out[1]==0);
    assert(!wide_prog_2d(hud));
    puts("PASS widescreen immutable allowlist, reused addresses, full movie/HUD/overlay bounds and off/target resets");
}
static Nv2aSurface surface(void)
{
    Nv2aSurface s={0};s.color_va=0x200000;s.width=3;s.height=2;s.pitch=16;
    s.bytes_per_pixel=4;s.aa_sx=s.aa_sy=1;s.format_raw=0x128;
    return s;
}
static void surface_sync_test(void)
{
    Nv2aSurface sf=surface();uint32_t regs[2048]={0};regs[0x20c/4]=16u<<16;s_regs=regs;
    uint32_t *ram=(uint32_t*)(guest+sf.color_va),*z=(uint32_t*)(guest+0x210000);
    for(unsigned i=0;i<8;i++){ram[i]=0x10203040u+i;z[i]=0x12345600u+i;}
    GlSurf *s=surf_bind(&sf,0x210000);
    assert(!memcmp(image[s->tex].pixels,ram,32));
    assert(!memcmp(image[s->ds_on].pixels,z,32));
    image[s->tex].pixels[0]=0xaabbccddu;image[s->ds_on].pixels[1]=0x13579bdfu;
    s->dirty=1;unsigned before=reads;gl_sync(0);
    assert(reads==before+2&&ram[0]==0xaabbccddu&&z[1]==0x13579bdfu&&!s->dirty);
    assert(ram[3]==0x10203043u&&ram[7]==0x10203047u);
    ram[1]=0x99887766u;z[2]=0xfedcba98u;
    s=surf_bind(&sf,0x210000);
    assert(image[s->tex].pixels[1]==0x99887766u&&image[s->ds_on].pixels[2]==0xfedcba98u);
    /* Sampling must observe the CPU update through guest RAM, not the old FBO. */
    unsigned t=tex_get(sf.color_va,0x12,3,2,16);
    assert(image[t].pixels[1]==0x99887766u);
    assert(s_backend_gl.sync==gl_sync&&s_backend_q.sync==q_sync);
    s_regs=NULL;
    puts("PASS production color+Z24S8 RAM import/readback and CPU update/resample");
}
static void clear_pitch_test(void)
{
    Nv2aSurface sf=surface();uint32_t *ram=(uint32_t*)(guest+sf.color_va);
    for(unsigned i=0;i<8;i++)ram[i]=0x10203040u+i;
    s_regs=NULL;gl_clear(&sf,NULL,0xf0,0xff112233,0);
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++)assert(ram[y*4+x]==(x<3?0xff112233:0x10203040u+y*4+x));
    puts("PASS production clear preserves padded pitch bytes and syncs before return");
    /* Reuse retained larger storage for a smaller clip; only that clip may
     * change, including when the clear rectangle covers just one axis. */
    sf.width=2;sf.height=1;
    for(unsigned i=0;i<8;i++)ram[i]=0x10203040u+i;
    gl_clear(&sf,NULL,0xf0,0xff334455,0);
    for(unsigned i=0;i<8;i++)assert(ram[i]==(i<2?0xff334455:0x10203040u+i));
    sf=surface();sf.clear_rect_set=1;sf.clear_rect_h=1u|(1u<<16);
    for(unsigned i=0;i<8;i++)ram[i]=0x10203040u+i;
    gl_clear(&sf,NULL,0xf0,0xff556677,0);
    for(unsigned i=0;i<8;i++)assert(ram[i]==(i%4==1?0xff556677:0x10203040u+i));
    puts("PASS retained larger surface clip shrink and horizontal-only clear bounds");
}
static void queued_completion(void)
{
    Nv2aSurface sf=surface();Nv2aRenderState rs={0};uint32_t regs[2048]={0};rs.zeta_va=0x210000;
    uint32_t *ram=(uint32_t*)(guest+sf.color_va);
    uint32_t *z=(uint32_t*)(guest+rs.zeta_va);
    regs[0x1d98/4]=2u<<16;regs[0x1d9c/4]=1u<<16;regs[0x20c/4]=16u<<16;
    fake_live_regs=regs;s_q_live_regs=regs;memset(nv2a_pb_reg_dirty(),1,128);
    assert(q_start());s_q_on=1;
    for(unsigned n=0;n<16;n++) {
        for(unsigned i=0;i<8;i++){ram[i]=0xdeac0000u+n*16u+i;z[i]=0x13579b00u+i;}
        q_clear(&sf,&rs,0x10,0xff112233,0);
        for(unsigned i=0;i<8;i++){assert(ram[i]==(i%4<3?0xde110000u+n*16u+i:0xdeac0000u+n*16u+i));assert(z[i]==0x13579b00u+i);}
    }
    q_sync(1);
    assert(__atomic_load_n(&s_q_head,__ATOMIC_ACQUIRE)==__atomic_load_n(&s_q_tail,__ATOMIC_ACQUIRE));
    puts("PASS queued clears complete/read back before caller returns, CPU edits imported on next callback");
}
static void expect_success(void (*run)(void))
{
    fflush(NULL);pid_t child=fork();assert(child>=0);
    if(!child){run();fflush(NULL);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
static void unsupported_surface_failure(void) {Nv2aSurface sf=surface();sf.bytes_per_pixel=2;surf_bind(&sf,0);}
static void incomplete_surface_failure(void) {Nv2aSurface sf=surface();sf.color_va=0x220000;incomplete=1;surf_bind(&sf,0);}
static void scaled_surface_failure(void) {Nv2aSurface sf=surface();s_scale=2;surf_bind(&sf,0);}
static void swizzled_surface_failure(void) {Nv2aSurface sf=surface();sf.format_raw&=~0xf00u;surf_bind(&sf,0);}
static void context_failure(void)
{
    uint32_t regs[2048]={0};Nv2aRawBatch b={0};b.regs=regs;
    s_state=-1;gl_draw_raw(&b);
}
static void missing_entry_failure(void)
{
    uint32_t regs[2048]={0};Nv2aRawBatch b={0};b.regs=regs;
    s_state=0;gl_review_sdl_failure=0;gl_draw_raw(&b);
}
static void allocation_failure(void)
{
    fail_allocation=1;
    (void)tex_get(0x1c0000,0x06,16,16,0);
}
static GLuint make_shader(GLenum type) {(void)type;return next_id++;}
static GLuint make_program(void) {return next_id++;}
static void status_good(GLuint id,GLenum param,GLint *v) {(void)id;(void)param;*v=1;}
static void status_bad(GLuint id,GLenum param,GLint *v) {(void)id;(void)param;*v=0;}
static void info_log(GLuint id,GLsizei n,GLsizei *len,GLchar *out) {(void)id;if(n)*out=0;if(len)*len=0;}
static void failure_draw(void)
{
    Nv2aRawBatch b={0};uint32_t regs[2048]={0},idx[3]={0,1,2};float attrs[3*64]={0};
    b.surface=surface();b.regs=regs;b.prim=5;b.attrs=attrs;b.indices=idx;b.vertex_count=b.index_count=3;b.attr_present=1;
    gl_draw_raw(&b);
}
static void shader_compile_failure(void)
{
    p_glCreateShader=make_shader;p_glGetShaderiv=status_bad;p_glGetShaderInfoLog=info_log;
    failure_draw();
}
static void program_link_failure(void)
{
    p_glCreateShader=make_shader;p_glCreateProgram=make_program;p_glGetShaderiv=status_good;
    p_glGetProgramiv=status_bad;p_glGetShaderInfoLog=info_log;p_glGetProgramInfoLog=info_log;
    failure_draw();
}
static void unmap_failure(void) {uint32_t word=0;ring_done(&s_vring,0,4,&word,&word);}
static void buffer_error(GLenum target,GLintptr at,GLsizeiptr n,const void *p) {(void)target;(void)at;(void)n;(void)p;injected_error=0x0505;}
static void upload_failure(void) {uint32_t word=0;p_glBufferSubData=buffer_error;ring_done(&s_vring,0,4,NULL,&word);}
static void expect_abort(void (*run)(void))
{
    pid_t child=fork();assert(child>=0);
    if(!child){run();_exit(42);}
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
static void dirty_cpu_sync_conflict(void)
{
    Nv2aSurface sf=surface();Nv2aRenderState rs={0};
    gl_clear(&sf,&rs,0xf0,0xff112233,0);
    guest[sf.color_va]^=1;
    gl_sync(0);
}
static void dirty_cpu_bind_conflict(void)
{
    Nv2aSurface sf=surface();Nv2aRenderState rs={0};
    gl_clear(&sf,&rs,0xf0,0xff112233,0);
    guest[sf.color_va]^=1;
    surf_bind(&sf,0);
}
static void dirty_cpu_readback_conflict(void)
{
    Nv2aSurface sf=surface();Nv2aRenderState rs={0};
    gl_clear(&sf,&rs,0xf0,0xff112233,0);
    inject_ram_on_read=sf.color_va;
    gl_sync(0);
}
static void fenced_ownership_test(void)
{
    Nv2aSurface sf=surface();Nv2aRenderState rs={0};uint32_t *ram=(uint32_t*)(guest+sf.color_va);
    for(unsigned i=0;i<8;i++)ram[i]=0x10203040+i;
    unsigned before_reads=reads;
    gl_clear(&sf,&rs,0xf0,0xff112233,0);
    assert(ram[0]==0x10203040&&reads==before_reads&&s_last->dirty);
    unsigned before_uploads=uploads;
    gl_clear(&sf,&rs,0x20,0xff005500,0);
    assert(ram[0]==0x10203040&&reads==before_reads&&uploads==before_uploads);
    gl_sync(0);
    assert(ram[0]==0xff115533&&reads==before_reads+1&&!s_last->dirty);
    assert(ram[3]==0x10203043&&ram[7]==0x10203047);
    ram[0]=0x77223344;
    gl_clear(&sf,&rs,0x80,0x99000000,0);
    assert(ram[0]==0x77223344);
    gl_sync(1);assert(ram[0]==0x99223344&&!s_last->resident);
    expect_abort(dirty_cpu_sync_conflict);expect_abort(dirty_cpu_bind_conflict);
    expect_abort(dirty_cpu_readback_conflict);
    /* Primary and contiguous VAs name the same physical storage. */
    gl_clear(&sf,&rs,0xf0,0xff667788,0);
    texture_surface_sync(sf.color_va|0x80000000u,4);
    assert(ram[0]==0xff667788&&!s_last->dirty);
    Nv2aBackend b={0};b.flags=NV2A_BACKEND_DEFERRED_RAM;
    assert(!nv2a_backend_contract_valid(&b));b.draw_raw=gl_draw_raw;
    assert(!nv2a_backend_contract_valid(&b));b.sync=gl_sync;
    assert(nv2a_backend_contract_valid(&b));
    nv2a_gl_install();assert(registered_backend->flags&NV2A_BACKEND_DEFERRED_RAM);
    puts("PASS fenced callbacks defer RAM until explicit sync, retain GPU writes, import clean CPU edits, reject conflicts and flush physical texture aliases");
}
static void gpu_mode_test(void)
{
    Nv2aSurface sf=surface(),swz=surface();Nv2aRenderState rs={0};uint32_t *ram=(uint32_t*)(guest+sf.color_va);
    for(unsigned i=0;i<8;i++)ram[i]=0x10203040+i;
    gl_clear(&sf,&rs,0xf0,0xff112233,0);    /* creates the surface */
    unsigned before_reads=reads,before_uploads=uploads;
    gl_clear(&sf,&rs,0xf0,0xff445566,0);
    gl_sync(0);gl_sync(1);
    assert(ram[0]==0x10203040&&reads==before_reads&&uploads==before_uploads);
    swz.format_raw&=~0xf00u;
    assert(surf_bind(&swz,0));
    puts("PASS gpu mode leaves guest RAM alone, reads nothing back and accepts layouts the RAM-publishing modes refuse");
}
int main(int argc,char **argv)
{
    setenv("RECOMP_PROG_CACHE","0",1);
    if(argc>1&&!strcmp(argv[1],"gpu")){setenv("MANHUNT_GL_SYNC","gpu",1);init_fake();gpu_mode_test();return 0;}
    if(argc>1){setenv("MANHUNT_GL_SYNC","fenced",1);init_fake();fenced_ownership_test();return 0;}
    init_fake();cache_test();palette_test();shader_modes_test();
    widescreen_classification_test();
    surface_sync_test();clear_pitch_test();
    expect_success(queued_completion);
    expect_abort(context_failure);
    expect_abort(missing_entry_failure);
    expect_abort(allocation_failure);
    expect_abort(unsupported_surface_failure);expect_abort(incomplete_surface_failure);
    expect_abort(scaled_surface_failure);expect_abort(swizzled_surface_failure);
    expect_abort(shader_compile_failure);expect_abort(program_link_failure);
    expect_abort(unmap_failure);expect_abort(upload_failure);
    puts("PASS context failure aborts instead of consuming a silent draw");
    puts("PASS GL production-path synthetic contract tests (fake GL, not rasterizer proof)");
    return 0;
}
