/* Scene ambient calibration preserves light terms, alpha and unlit batches. */
#define main renderer_review_main
#include "test_gl.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);float want=strtof(argv[1],NULL);assert(ambient_level()==want);
    uint32_t regs[0x2000/4]={0};float original[46][4],actual[46][4];
    const float ambient[4]={.1f,.2f,.3f,.7f};
    memcpy(regs+0x0a10/4,ambient,12);memcpy(regs+0x03b4/4,ambient+3,4);
    nv2a_gl_light_uniforms(regs,1,original);light_uniforms(regs,1,actual);
    for(unsigned r=0;r<46;r++)for(unsigned c=0;c<4;c++)
        assert(actual[r][c]==((want>=0 && r==1 && c<3)?want:original[r][c]));
    light_uniforms(regs,0,actual);nv2a_gl_light_uniforms(regs,0,original);
    assert(!memcmp(actual,original,sizeof actual));
    puts("PASS ambient configuration, RGB-only override, alpha and unlit preservation");
}
