/* Synthetic ABI fixture executes the production camera wrapper. It does not
 * substitute for matched original-game frames, simulation hashes or edge pans. */
#include "recomp_types.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Alignas(8) unsigned char test_ram[8192];
uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp;
static float input_x, input_y;
static unsigned original_calls;
enum { STACK = 256, CAMERA = 1024 };
extern int manhunt_widescreen_configure(void);
extern int xbox_video_widescreen(void);
extern void __wrap_sub_001E80A0(void);
static float getf(unsigned a) { float f; memcpy(&f,test_ram+a,4); return f; }
static void putf(unsigned a,float f) { memcpy(test_ram+a,&f,4); }
void __real_sub_001E80A0(void)
{
    assert(g_esp==STACK);
    assert(MEM32(g_esp+4)==CAMERA);
    assert(MEM32(g_esp+8)==0xdecafbad);
    putf(CAMERA+0x68,input_x); putf(CAMERA+0x6c,input_y);
    putf(CAMERA+0x70,1/input_x); putf(CAMERA+0x74,1/input_y);
    g_eax=CAMERA; g_ecx=7; g_edx=11; g_esp+=4;
    ++original_calls;
}
static void camera(float x,float y,int widen)
{
    memset(test_ram,0xa5,sizeof test_ram);
    input_x=x;input_y=y;
    g_esp=STACK;g_ebx=17;g_esi=19;g_edi=23;
    MEM32(STACK)=0x12345678; MEM32(STACK+4)=CAMERA; MEM32(STACK+8)=0xdecafbad;
    unsigned char before[sizeof test_ram]; memcpy(before,test_ram,sizeof before);
    unsigned count=original_calls;
    __wrap_sub_001E80A0();
    assert(original_calls==count+1);
    assert(g_esp==STACK+4&&g_eax==CAMERA&&g_ecx==7&&g_edx==11);
    assert(g_ebx==17&&g_esi==19&&g_edi==23);
    const float expected=widen?x*(4.f/3.f):x;
    assert(fabsf(getf(CAMERA+0x68)-expected)<.000001f);
    assert(getf(CAMERA+0x6c)==y);
    assert(getf(CAMERA+0x70)==1/expected || fabsf(getf(CAMERA+0x70)-1/expected)<.000001f);
    assert(getf(CAMERA+0x74)==1/y);
    /* Only the four camera view/reciprocal floats may change. */
    for(unsigned i=0;i<sizeof test_ram;i++)
        if(i<CAMERA+0x68||i>=CAMERA+0x78) assert(test_ram[i]==before[i]);
    if(widen) {
        /* Independent analytic projection: vertical extent unchanged; every
         * ray in the old frustum remains in the new one, plus side rays. */
        assert(fabsf(atanf(getf(CAMERA+0x6c))-atanf(y))<.000001f);
        assert(getf(CAMERA+0x68)>x);
        for(int i=-100;i<=100;i++) {
            float ray=x*i/100.f;
            assert(fabsf(ray/getf(CAMERA+0x68))<=1.f);
        }
        assert(fabsf((x*1.1f)/x)>1.f);
        assert(fabsf((x*1.1f)/getf(CAMERA+0x68))<1.f);
    }
}
int main(int argc,char **argv)
{
    assert(argc==3);
    int valid=atoi(argv[1]), on=atoi(argv[2]);
    /* Query first exercises defensive lazy validation as well as startup API. */
    assert(xbox_video_widescreen()==on);
    assert(manhunt_widescreen_configure()==valid);
    assert(xbox_video_widescreen()==on);
    camera(.7f,.546875f,on);
    camera(.9333333333f,.546875f,0); /* already widened */
    camera(1,1,0);                  /* square camera */
    camera(.5f,1,0);               /* other aspect */
    camera(0,1,0); camera(-1,1,0);  /* invalid windows unchanged */
    puts("PASS production camera/config: ABI, guards, repeat, square, bounds and analytic FOV");
    return 0;
}
