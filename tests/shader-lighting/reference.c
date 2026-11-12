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

void reference_cmp(uint32_t packed, float out[4])
{
    memcpy(vertices + 4, &packed, 4);
    VertexAttr a = {0};
    a.offset = 16; a.type = 6; a.size = 3; a.stride = 4;
    if (!fetch_attr(&a, 0, out)) abort();
}

void reference_cmp_direct_contract(void)
{
    Nv2aBackend backend = {0};
    const Nv2aBackend *saved_backend = s_backend;
    s_backend = &backend;
    memset(s_gpu.attr, 0, sizeof s_gpu.attr);
    VertexAttr *a = &s_gpu.attr[2];
    a->type = 6; a->size = 3; a->stride = 4; a->offset = 16;
    s_gpu.inline_active = 0;
    Nv2aRawBatch b = {0};
    if (raw_direct_attrs(&b, 4, 0, 1)) abort(); /* capability is mandatory */
    backend.flags = NV2A_BACKEND_RAW_CMP;
    if (raw_direct_attrs(&b, 4, 0, 1) != 4 || b.direct[2].ptr != (const uint8_t *)vertices + 16) abort();
    a->offset = 0x03fffffcu;
    if (raw_direct_attrs(&b, 4, 0, 1) != 4) abort(); /* last valid word */
    if (raw_direct_attrs(&b, 4, 0, 2)) abort();
    a->offset = 0x03fffffeu;
    if (raw_direct_attrs(&b, 4, 0, 1)) abort();
    a->offset = 16; a->size = 0;
    if (raw_direct_attrs(&b, 4, 0, 1)) abort();
    a->size = 3; a->stride = 0;
    if (raw_direct_attrs(&b, 4, 0, 1)) abort();
    a->stride = 4; a->offset = 0; s_gpu.inline_active = 1; s_gpu.inline_count = 1;
    if (raw_direct_attrs(&b, 4, 0, 1) != 4 || b.direct[2].ptr != (const uint8_t *)s_gpu.inline_buf) abort();
    if (raw_direct_attrs(&b, 4, 0, 2)) abort();
    s_gpu.inline_count = 0;
    if (raw_direct_attrs(&b, 4, 0, 1)) abort();
    s_gpu.inline_active = 0; s_backend = saved_backend;
}
