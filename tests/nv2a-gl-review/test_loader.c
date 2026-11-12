#include "../../game/src/nv2a_gl/gl_api.h"
#include <assert.h>
#include <string.h>
static int omit_sync;
static void *resolve(const char *name) {
    if(!strcmp(name,"glBufferStorage") || (omit_sync && !strcmp(name,"glFenceSync")))return NULL;
    return (void *)(uintptr_t)1;
}
int main(void) {
    assert(nv2a_gl_load(resolve)==0 && !p_glBufferStorage && p_glFenceSync);
    omit_sync=1;assert(nv2a_gl_load(resolve)==1);
    return 0;
}
