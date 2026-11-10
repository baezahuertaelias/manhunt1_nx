/* Compare GPU output against the actual, unchanged CPU lit_color function. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef LIGHTING_EXEC_SOURCE
#define LIGHTING_EXEC_SOURCE "../../game/build/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#endif
#include LIGHTING_EXEC_SOURCE

static float vertices[64];
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)vertices; }

void reference_lighting(const uint32_t *regs, const float *pos, const float *normal,
                        const float *vc, int have_vc, float out[4])
{
    memcpy(s_reg, regs, 0x2000);
    memset(s_gpu.attr, 0, sizeof s_gpu.attr);
    memcpy(vertices + 4, pos, 16);
    memcpy(vertices + 8, normal, 16);
    memcpy(vertices + 12, vc, 16);
    const unsigned slots[] = {0, 2, 3};
    for (int i = 0; i < 3; ++i) {
        VertexAttr *a = &s_gpu.attr[slots[i]];
        a->offset = (4 + i * 4) * sizeof(float);
        a->type = 2; a->size = 4; a->stride = 16;
    }
    if (!have_vc) s_gpu.attr[3].stride = 0;
    if (!lit_color(0, out)) abort();
}
