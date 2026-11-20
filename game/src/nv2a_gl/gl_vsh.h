/* gl_vsh.h -- NV2A vertex transform as GLSL (see gl_vsh.c). */
#ifndef NV2A_GL_VSH_H
#define NV2A_GL_VSH_H

#include <stddef.h>
#include <stdint.h>

/* The body of a vertex program, as `void nv2a_program(void)`, from slot
 * `start` to the FINAL instruction. Returns bytes written, -1 on overflow. */
int nv2a_gl_vsh_program(const uint32_t (*prog)[4], uint32_t slots,
                        uint32_t start, char *buf, size_t cap);

/* Nonzero: emit Vulkan GLSL (nv2a_vk). Set once, before the first shader. */
extern int nv2a_shader_vk;
/* Nonzero (Vulkan): the device clamps depth per fragment (depthClamp), so
 * the vertex shader leaves clip z unclamped. Set before the first shader. */
extern int nv2a_shader_depth_clamp;

/* Declarations and helpers every vertex shader starts with. */
const char *nv2a_gl_vsh_prelude(void);
/* GL only: std140 constant/lighting blocks; Vulkan is unchanged. */
const char *nv2a_gl_vsh_prelude_mode(int ubo);
/* main() for a vertex-program shader (follows the program body). */
const char *nv2a_gl_vsh_main_program(void);
/* main() for fixed-function / pre-transformed batches (no program body). */
const char *nv2a_gl_vsh_fixed(void);
const char *nv2a_gl_vsh_lighting(void);
/* GL fixed-function fog generation uniform u_fogm[5] from the register file
 * (see gl_vsh.c). MANHUNT_GL_FF_FOG=0: all zero, no fog coordinate. */
void nv2a_gl_vsh_fog_uniforms(const uint32_t *r, float out[5][4]);
/* GL fixed-function spot lights and texture-coordinate generation: uniform
 * u_ffx[NV2A_FFX_ROWS] from the register file and the batch's lighting bits
 * (layout in gl_vsh.c). MANHUNT_GL_FF_SPOT=0 lights spots as point lights,
 * MANHUNT_GL_FF_TEXGEN=0 passes texture coordinates through unchanged. */
#define NV2A_FFX_ROWS 50
void nv2a_gl_vsh_ffx_uniforms(const uint32_t *r, uint32_t lighting,
                              float out[NV2A_FFX_ROWS][4]);
/* Nonzero unless MANHUNT_GL_SHADER_MINOR=0: LIT pow(0, 0) = 1 and the alpha
 * test on alpha rounded to 8 bits (the interpreter's and CPU path's forms). */
int nv2a_gl_shader_minor(void);
/* MANHUNT_GL_CLIP_CLAMP: 0 linear clip z (default), 1 clamped per vertex,
 * 2 (=alt) either, per the uniform u_clipalt. nv2a_gl_clip_alt_clamp(frame)
 * is u_clipalt for the frame that presents as number "frame": even frames
 * linear, odd frames clamped. */
int nv2a_gl_clip_mode(void);
int nv2a_gl_clip_alt_clamp(uint32_t frame);

const char *nv2a_gl_vsh_attributes(void);

#endif
