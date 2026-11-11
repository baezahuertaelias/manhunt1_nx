/* Validation observes bad inputs without altering game/render state. */
#define main renderer_review_main
#include "test_gl.c"
#undef main
static unsigned checks;
static GLenum validation_error(void) {checks++;return checks==1?0x0502:0;}
int main(int argc,char **argv)
{
    (void)argv;setenv("MANHUNT_GL_VALIDATE",argc>1?"1":"0",1);init_fake();
    p_glGetError=validation_error;
    validate_frame();validate_frame();assert(checks==(argc>1?2u:0u));
    Nv2aRawBatch b={0};b.xform=1;b.vertex_count=3;
    uint32_t nan=0xffc00000;memcpy(&b.composite[2],&nan,4);
    Nv2aRawBatch before=b;
    for(unsigned i=0;i<12;i++)validate_transform(&b);
    assert(!memcmp(&b,&before,sizeof b));
    puts("PASS optional frame error check and nonfinite input observation preserve batch");
}
