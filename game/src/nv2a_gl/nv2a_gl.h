/*
 * nv2a_gl.h -- OpenGL renderer for the NV2A pushbuffer executor.
 *
 * Call nv2a_gl_install() before the title starts rendering, with the
 * executor enabled (RECOMP_PB_EXEC). The window and GL context are created
 * on the executor's thread on first use.
 */
#ifndef NV2A_GL_H
#define NV2A_GL_H

#include <stdint.h>

void nv2a_gl_install(void);

/* Running totals since start (MANHUNT_PERF_LOG). Times in ns; flip_ns
 * includes swap_ns. All fields are uint64_t: nv2a_gl_perf_read copies them
 * as an array. max_frame_ns is the longest frame since the previous read. */
typedef struct {
    uint64_t frames, slow_frames, max_frame_ns;
    uint64_t draws, verts, indices, draw_ns, clear_ns, flip_ns, swap_ns;
    uint64_t texs, tex_bytes, tex_ns, progs, prog_ns;
    uint64_t lookup_ns, upload_ns, uniform_ns, bind_ns, state_ns, submit_ns, other_ns;
    uint64_t maps, uniform_calls, uniform_bytes, texture_binds, converted_attrs, converted_verts;
    uint64_t queue_attr_bytes, queue_attr_saved_bytes;
    uint64_t same_state_draws;  /* nothing but vertices changed since the previous draw */
    uint64_t kept_attr_draws;   /* drew with the previous attribute pointers and a base vertex */
    uint64_t const_reuses;      /* constants changed generation but not content: no upload */
    uint64_t converted_type[8]; /* attribute vertices by NV097 storage type */
    /* RECOMP_GL_THREAD=1, executor side: draws whose vertices went straight
     * into the direct ring and their bytes; draws that found it full; ring
     * wraps; time copying vertex data (ring or queue record) per draw. */
    uint64_t ring_draws, ring_bytes, ring_full, ring_wraps, queue_vertex_ns;
} Nv2aGlPerf;
void nv2a_gl_perf_read(Nv2aGlPerf *out);

#endif
