/* Production state/cache contracts; synthetic GL call counters. */
#define main existing_review_main
#include "test_gl.c"
#undef main

static unsigned state_calls, color_calls, depth_calls, stencil_calls, active_calls, bind_calls;
static void count_cap(GLenum cap) { (void)cap; state_calls++; }
static void count_pair(GLenum a, GLenum b) { (void)a;(void)b;state_calls++; }
static void count_enum(GLenum a) { (void)a;state_calls++; }
static void count_color(GLfloat r,GLfloat g,GLfloat b,GLfloat a) {(void)r;(void)g;(void)b;(void)a;state_calls++;}
static void count_mask(GLboolean r,GLboolean g,GLboolean b,GLboolean a) {(void)r;(void)g;(void)b;(void)a;state_calls++;color_calls++;}
static void count_depth(GLenum f) {(void)f;state_calls++;depth_calls++;}
static void count_depthmask(GLboolean b) {(void)b;state_calls++;}
static void count_stencilmask(GLuint b) {(void)b;state_calls++;}
static void count_stencilfunc(GLenum a,GLint b,GLuint c) {(void)a;(void)b;(void)c;state_calls++;}
static void count_stencilop(GLenum a,GLenum b,GLenum c) {(void)a;(void)b;(void)c;state_calls++;stencil_calls++;}
static void count_scissor(GLint a,GLint b,GLsizei c,GLsizei d) {(void)a;(void)b;(void)c;(void)d;state_calls++;}
static void count_active(GLenum a) {(void)a;active_calls++;}
static void count_bind(GLenum a,GLuint b) {(void)a;(void)b;bind_calls++;}
int main(int argc,char **argv)
{
    (void)argv;
    const int improved = argc > 1;
    setenv("MANHUNT_GL_STATE_CACHE", improved ? "1" : "0", 1);
    setenv("RECOMP_GL_NO_CULL", "0", 1);
    init_fake();
    p_glEnable=count_cap;p_glDisable=count_cap;
    p_glBlendFunc=count_pair;p_glBlendEquation=count_enum;p_glBlendColor=count_color;
    p_glDepthFunc=count_depth;p_glDepthMask=count_depthmask;p_glColorMask=count_mask;
    p_glStencilMask=count_stencilmask;p_glStencilFunc=count_stencilfunc;p_glStencilOp=count_stencilop;
    p_glCullFace=count_enum;p_glFrontFace=count_enum;p_glScissor=count_scissor;
    uint32_t r[0x2000/4]={0};
    GlSurf sf={0};sf.w=640;sf.h=480;sf.pw=640;sf.ph=480;sf.aa_sx=sf.aa_sy=1;
    r[0x200/4]=640u<<16;r[0x204/4]=480u<<16;
    r[0x304/4]=r[0x30c/4]=r[0x32c/4]=r[0x308/4]=1;
    r[0x344/4]=GL_ONE;r[0x348/4]=GL_ZERO;r[0x350/4]=GL_FUNC_ADD;
    r[0x358/4]=0x01010101;r[0x35c/4]=1;r[0x354/4]=0x203;
    state_dirty();apply_state(r,1,&sf);
    unsigned full=state_calls;assert(full>=15);
    apply_state(r,1,&sf);assert(state_calls==full);
    r[0x358/4]^=1;apply_state(r,1,&sf);
    assert(color_calls==2);
    if(improved)assert(state_calls==full+1);else assert(state_calls==2*full);
    unsigned last=state_calls;
    r[0x354/4]=0x201;apply_state(r,1,&sf);
    assert(depth_calls==(improved?2:3));
    if(improved)assert(state_calls==last+1);
    last=state_calls;r[0x370/4]=0x1e01;apply_state(r,1,&sf);
    if(improved)assert(state_calls==last+1);
    /* All external users (clears, surfaces, gamma) enter this invalidator. */
    last=state_calls;state_dirty();apply_state(r,1,&sf);assert(state_calls==last+full);
    /* Disabling a group, then changing its parameters while disabled and
     * enabling it again must restore all its parameters. */
    r[0x304/4]=0;apply_state(r,1,&sf);r[0x344/4]=0x302;apply_state(r,1,&sf);
    last=state_calls;r[0x304/4]=1;apply_state(r,1,&sf);
    if(improved)assert(state_calls==last+4);
    /* Disabled stages already bound to zero need no active-unit churn. */
    memset(r,0,sizeof r);p_glActiveTexture=count_active;p_glBindTexture=count_bind;
    tex_bind_forget();float scale[2];
    for(int i=0;i<4;i++)bind_stage(r,i,scale);
    assert(active_calls==4&&bind_calls==4);
    for(int i=0;i<4;i++)bind_stage(r,i,scale);
    assert(bind_calls==4);assert(active_calls==(improved?4:8));
    tex_bind_forget();for(int i=0;i<4;i++)bind_stage(r,i,scale);
    assert(bind_calls==8);
    /* Texture object identity reuse must drop sampler state (font safety). */
    s_tex_param[99].id=99;s_tex_param[99].key=0x123;
    tex_param_forget(99);assert(s_tex_param[99].id==0);
    s_tex_param[99].id=99;tex_param_forget(99+TEX_PARAM_SLOTS);assert(s_tex_param[99].id==99);
    printf("PASS %s state cache: unchanged draw, partial changes, invalidation, disabled-stage bindings, sampler identity\n",improved?"incremental":"legacy");
    return 0;
}
