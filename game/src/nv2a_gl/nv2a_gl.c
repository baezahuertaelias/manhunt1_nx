/*
 * nv2a_gl.c -- an OpenGL 3.3 core renderer for the pushbuffer executor.
 *
 * Registered as the executor's back end (nv2a_backend.h, draw_raw). The
 * executor decodes the pushbuffer, gathers vertices and keeps the method
 * shadow; this file turns that state into GL:
 *
 *   surfaces   one framebuffer object per colour-surface address, rows
 *              top-first like guest memory. A texture whose address is a
 *              surface is imported before a callback and read back before
 *              return. Textures always sample the current guest bytes.
 *   textures   decoded from guest memory by the executor's own decoder
 *              (swizzled, linear, DXT), cached by address and format, and
 *              re-uploaded when an exact byte snapshot changes.
 *   shaders    vertex programs and the fixed-function transform (gl_vsh.c),
 *              register combiners and texture modes (gl_psh.c), linked in
 *              pairs and cached by what generated them.
 *   state      blend, depth, stencil, cull and colour mask straight from the
 *              method shadow; alpha test in the fragment shader.
 *
 * Everything runs on the executor's thread, which is where the GL context is
 * created (lazily, on the first call) and where the window is presented and
 * its events pumped, on flip.
 *
 * Runtime switches:
 *   RECOMP_GL_DUMP=<prefix>[,every]  write presented frames as BMP
 *   RECOMP_GL_TRACE=1                shader sources and link errors
 *   RECOMP_GL_WATCH=<hex va>         state, vertices and pixels read back for
 *                                    the first draws into / sampling that VA
 *   RECOMP_GL_SCALE=<k>              render surfaces at k times the title's
 *                                    resolution, fractions allowed (1.5;
 *                                    0.5..4, default 1)
 *   RECOMP_GL_SHARED_Z=0             depth buffer per colour surface instead of
 *                                    per zeta address (the old behaviour)
 */
#include "nv2a_gl.h"
#include "gl_api.h"
#include "gl_psh.h"
#include "gl_vsh.h"
#include "nv2a_backend.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);

/* Accepted raw callbacks cannot request replay or silently lose a draw. */
static _Noreturn void gl_fail(const char *why)
{
    fprintf(stderr, "[MH-GL] FAIL: %s\n", why);
    abort();
}

/* MANHUNT_GL_SYNC. gpu (default): surfaces live on the GPU and are sampled
 * directly, as in the renderer's upstream design and in every console run that
 * reached playable speed (runs 18 to 38: 35 to 44 flips/s). The title's guest
 * RAM is not updated with rendered pixels, and CPU writes into a surface are
 * not imported. strict (3 FPS) and fenced (7 FPS) publish RAM; both were
 * measured on the intro in reports/REVIEW_FIXES_2026-11-06.md. */
enum { GL_SYNC_STRICT = 0, GL_SYNC_FENCED = 1, GL_SYNC_GPU = 2 };
static atomic_int s_gl_mode = -1;
static int gl_mode(void)
{
    int mode = atomic_load_explicit(&s_gl_mode, memory_order_acquire);
    if (mode < 0) {
        const char *e = getenv("MANHUNT_GL_SYNC");
        if (!e || !*e || !strcmp(e, "gpu")) mode = GL_SYNC_GPU;
        else if (!strcmp(e, "strict")) mode = GL_SYNC_STRICT;
        else if (!strcmp(e, "fenced")) mode = GL_SYNC_FENCED;
        else gl_fail("MANHUNT_GL_SYNC must be gpu, strict or fenced");
        atomic_store_explicit(&s_gl_mode, mode, memory_order_release);
    }
    return mode;
}
static int gl_gpu(void) { return gl_mode() == GL_SYNC_GPU; }
static int gl_fenced(void) { return gl_mode() == GL_SYNC_FENCED; }
int nv2a_gl_gpu_mode(void) { return gl_gpu(); }

static void gl_check(const char *where)
{
    /* In gpu mode a GL error is not fatal: it was not checked in the measured
     * runs, and glGetError is a round trip on every draw. */
    if (gl_gpu())
        return;
    if (glGetError() != GL_NO_ERROR)
        gl_fail(where);
}

static int ram_span(uint32_t va, size_t bytes)
{
    uint64_t end = (uint64_t)va + bytes;
    return bytes && ((va < 0x04000000u && end <= 0x04000000ull) ||
        (va >= 0x80000000u && va < 0x84000000u && end <= 0x84000000ull));
}
static void gl_sync(int invalidate);

int nv2a_gl_adopt_window(void **win, void **ctx);
int nv2a_gl_draw_placeholder(void);
int nv2a_gl_screen_size(int *w, int *h);
static SDL_Window   *s_win;
static SDL_GLContext s_ctx;
static int           s_state;           /* 0 untried, 1 ready, -1 failed */
static int           s_trace;
static int           s_finish = -1;      /* RECOMP_GL_FINISH */
static int           s_q_on;

static int           s_new_obj;          /* this operation uses something new */

/* RECOMP_GL_FINISH: wait for the GPU after every operation, so a hard GPU
 * hang happens at the operation that causes it. Only operations that touch
 * something new (a shader just compiled, a surface or texture just made)
 * are logged -- the rest is thousands of lines a frame, far too slow for a
 * synchronous log on an SD card. */
static void gl_step_done(const char *what)
{
    int upload = what[0] == 't';          /* "texture upload": part of a draw */
    if (s_finish < 0)
        s_finish = getenv("RECOMP_GL_FINISH") != NULL;
    if (s_finish) {
        glFinish();
        if (s_new_obj)
            fprintf(stderr, "  [GL] %s done\n", what);
        else if (what[0] == 's') {        /* "swap": one line a frame, a heartbeat */
            static unsigned swaps;
            fprintf(stderr, "  [GL] frame %u swapped\n", ++swaps);
        }
    }
    if (!upload)
        s_new_obj = 0;                    /* the draw that uses it reports too */
}
static GLuint        s_vao, s_vbo, s_ibo;
/* Streaming vertex / index buffers (see ring_alloc). */
typedef struct { GLuint buf; GLenum target; GLsizeiptr cap, off; } GlRing;
static GlRing s_vring = { 0, GL_ARRAY_BUFFER, 16 << 20, 0 };
static GlRing s_iring = { 0, GL_ELEMENT_ARRAY_BUFFER, 4 << 20, 0 };
static const uint32_t *s_regs;          /* the executor's method shadow */
static uint32_t      s_frame;
/* Hitch accounting (gl_flip logs frames over 50 ms): per frame, programs
 * compiled and textures uploaded, and the time spent on each. */
static uint32_t s_hz_progs, s_hz_texs, s_hz_draws;
static uint64_t s_hz_prog_ns, s_hz_tex_ns, s_hz_tex_bytes;
static uint64_t hz_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static GLuint        s_cur_prog;         /* what glUseProgram last got */
static void state_dirty(void);
/* Last sampler state set on a GL texture (bind_stage). */
typedef struct { GLuint id; uint32_t key; } TexParam;
#define TEX_PARAM_SLOTS 4096
static TexParam s_tex_param[TEX_PARAM_SLOTS];
static void tex_param_forget(GLuint id)
{
    TexParam *tp = &s_tex_param[id % TEX_PARAM_SLOTS];
    if (tp->id == id)
        tp->id = 0;
}

/* Texture bindings per unit, so a draw rebinds only what changed. -1: not
 * known (after anything outside the draw path may have bound textures). */
static int    s_active_unit = -1;
static GLuint s_bound[4];
static int    s_bound_known;
static void tex_bind_forget(void) { s_active_unit = -1; s_bound_known = 0; }
static void active_unit(int i)
{
    if (s_active_unit != i) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        s_active_unit = i;
    }
}
/* Bind on the active unit (known after active_unit). */
static void bind_tex(GLuint tex)
{
    int u = s_active_unit;
    if (u >= 0 && u < 4 && (s_bound_known & (1 << u)) && s_bound[u] == tex)
        return;
    glBindTexture(GL_TEXTURE_2D, tex);
    if (u >= 0 && u < 4) {
        s_bound[u] = tex;
        s_bound_known |= 1 << u;
    }
}

/* ── Surfaces ──────────────────────────────────────────────────────── */

/* w, h are the surface in the title's pixels (anti-aliasing included) --
 * what the shaders, clip rectangles and texture lookups work in. pw, ph are
 * the GL storage: w, h times the render scale. */
typedef struct {
    uint32_t va, w, h, pw, ph;
    GLuint   fbo, tex, ds;
    GLuint   ds_on;                     /* depth renderbuffer attached now */
    uint32_t used;
    uint32_t aa_sx, aa_sy;
    Nv2aSurface guest;
    uint32_t zeta_va, zeta_pitch;
    uint8_t *color_copy, *depth_copy;
    int dirty;
    uint8_t *color_baseline, *depth_baseline;
    int resident;
} GlSurf;

/* Depth buffers belong to the title's zeta address, not to a colour surface:
 * NFSU2's light flares (eRenderLightFlares, sub_000AC560) switch colour
 * target but keep the scene's Z buffer, and with depth per colour surface
 * they tested against an empty buffer -- car lights, neon and street lamps
 * showed through walls and cars. Keyed by (zeta, stored size); a zeta used
 * with a colour surface of another size gets a buffer of its own. */
typedef struct {
    uint32_t va, pw, ph;
    GLuint   rb;
    uint32_t used;
} GlDepth;

#define GL_MAX_DEPTH 16
static GlDepth s_depth[GL_MAX_DEPTH];
static int     s_shared_z = -1;         /* RECOMP_GL_SHARED_Z=0: old behaviour */

/* RECOMP_GL_SCALE: render resolution multiple (0.5..4, fractions allowed).
 * Everything the title sees stays at its own size; only the pixels behind a
 * surface grow, so the viewport, clear rectangles, read-backs and the
 * present blit scale and nothing else does. Capped per surface by the
 * driver's size limit. */
static double s_scale = 1.0;
static GLint  s_max_size = 4096;

/* v title pixels of a surface l wide, in the p stored pixels behind it. */
static GLint to_stored(uint32_t v, uint32_t p, uint32_t l)
{
    return (GLint)(((uint64_t)v * p + l / 2) / l);
}

#define GL_MAX_SURF 32
static GlSurf  s_surf[GL_MAX_SURF];
static GlSurf *s_last;                  /* last surface drawn or cleared */
static int     s_drew_any;              /* the title has drawn something */

static int s_watch_hit;

static GlSurf *surf_find(uint32_t va)
{
    int i;
    for (i = 0; i < GL_MAX_SURF; i++)
        if (s_surf[i].fbo && s_surf[i].va == va)
            return &s_surf[i];
    return NULL;
}

/* A baseline is exact pitched guest content at the last import/completion.
 * It detects stable CPU writes during GPU ownership, not arbitrary concurrent
 * readers/writers. Fenced mode requires guest accesses ordered by completion. */
static int rows_match(uint32_t va, uint32_t pitch, const uint8_t *copy,
                      uint32_t w, uint32_t h)
{
    if (!copy) return 0;
    for (uint32_t y = 0; y < h; y++)
        if (memcmp((const uint8_t *)xbox_GetMemoryOffset() + va + (size_t)y*pitch,
                   copy + (size_t)y*w*4, (size_t)w*4)) return 0;
    return 1;
}
static void rows_snapshot(uint32_t va, uint32_t pitch, uint8_t *copy,
                           uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; y++)
        memcpy(copy + (size_t)y*w*4,
               (const uint8_t *)xbox_GetMemoryOffset() + va + (size_t)y*pitch,
               (size_t)w*4);
}
static int surf_ram_matches(const GlSurf *s)
{
    return rows_match(s->guest.color_va, s->guest.pitch, s->color_baseline, s->w, s->h)
        && (!s->zeta_va || rows_match(s->zeta_va,s->zeta_pitch,s->depth_baseline,s->w,s->h));
}
static void surf_check_conflict(const GlSurf *s)
{
    if (gl_fenced() && s->dirty && (!s->resident || !surf_ram_matches(s)))
        gl_fail("CPU write conflicts with pending GPU surface ownership");
}
static int physical_overlap(uint32_t a, size_t an, uint32_t b, size_t bn)
{
    uint64_t x=a&0x03FFFFFFu, y=b&0x03FFFFFFu;
    return x < y+bn && y < x+an;
}
static void texture_surface_sync(uint32_t va,size_t bytes)
{
    if (!gl_fenced()) return;
    for (unsigned i=0;i<GL_MAX_SURF;i++) {
        GlSurf *s=&s_surf[i];
        if (!s->fbo || !s->dirty) continue;
        if (physical_overlap(va,bytes,s->guest.color_va,(size_t)s->guest.pitch*s->h) ||
            (s->zeta_va && physical_overlap(va,bytes,s->zeta_va,(size_t)s->zeta_pitch*s->h))) {
            gl_sync(0); return;
        }
    }
}

static GlSurf *surf_get(uint32_t va, uint32_t w, uint32_t h,
                        uint32_t aa_sx, uint32_t aa_sy)
{
    GlSurf *s = surf_find(va), *victim = NULL;
    int i;

    /* h is the surface clip, not the allocation: NFSU2's split screen clips
     * the back buffer to 640x240 for each player's view and back to 640x480
     * for the HUD, every frame. Rebuilding (and clearing) the surface on
     * each change left only the HUD, over a black 3D world. Keep the
     * surface while the clip fits; grow it when it does not. */
    if (s && s->w == w && s->h >= h) {
        s->used = s_frame;
        return s;
    }
    if (s && s->w == w && s->h > h)
        h = s->h;
    if (!s) {
        for (i = 0; i < GL_MAX_SURF; i++) {
            if (!s_surf[i].fbo) { victim = &s_surf[i]; break; }
            if (!victim || s_surf[i].used < victim->used) victim = &s_surf[i];
        }
        s = victim;
    }
    if (s->fbo) {
        gl_sync(0);
        free(s->color_copy);
        free(s->depth_copy);
        free(s->color_baseline);
        free(s->depth_baseline);
        glDeleteFramebuffers(1, &s->fbo);
        glDeleteTextures(1, &s->tex);
        tex_bind_forget();
        glDeleteTextures(1, &s->ds);
    }
    memset(s, 0, sizeof *s);
    s_new_obj = 1;
    s->va = va; s->w = w; s->h = h; s->used = s_frame;
    s->aa_sx = aa_sx; s->aa_sy = aa_sy;
    {
        double k = s_scale, m = (double)(w > h ? w : h);
        if (m * k > (double)s_max_size)
            k = (double)s_max_size / m;
        s->pw = (uint32_t)(w * k + 0.5); s->ph = (uint32_t)(h * k + 0.5);
        if (!s->pw) s->pw = 1;
        if (!s->ph) s->ph = 1;
    }
    glGenTextures(1, &s->tex);
    tex_param_forget(s->tex);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->pw, (GLsizei)s->ph, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenTextures(1, &s->ds);
    glBindTexture(GL_TEXTURE_2D, s->ds);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (GLsizei)s->pw, (GLsizei)s->ph,
                 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    tex_bind_forget();
    glGenFramebuffers(1, &s->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, s->ds, 0);
    s->ds_on = s->ds;
    if (!s->tex || !s->ds || !s->fbo ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        if (!gl_gpu())
            gl_fail("surface allocation or incomplete framebuffer");
        fprintf(stderr, "  [GL] surface 0x%08X %ux%u incomplete\n", va, s->pw, s->ph);
    }
    gl_check("surface allocation");
    glViewport(0, 0, (GLsizei)s->pw, (GLsizei)s->ph);
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClearStencil(0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glDepthMask(1);
    glStencilMask(0xFF);
    state_dirty();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (s_trace)
        fprintf(stderr, "  [GL] surface 0x%08X %ux%u (aa %ux%u, stored %ux%u)\n",
                va, w, h, aa_sx, aa_sy, s->pw, s->ph);
    return s;
}

static GLuint depth_get(uint32_t va, uint32_t pw, uint32_t ph, int *fresh)
{
    GlDepth *d, *victim = NULL;
    int i;

    for (i = 0; i < GL_MAX_DEPTH; i++) {
        if (s_depth[i].rb && s_depth[i].va == va
            && s_depth[i].pw == pw && s_depth[i].ph == ph) {
            s_depth[i].used = s_frame;
            return s_depth[i].rb;
        }
        if (!victim || (victim->rb && (!s_depth[i].rb || s_depth[i].used < victim->used)))
            victim = &s_depth[i];
    }
    d = victim;
    if (d->rb) {
        /* Surfaces still pointing at it re-attach on their next bind. */
        for (i = 0; i < GL_MAX_SURF; i++)
            if (s_surf[i].ds_on == d->rb)
                s_surf[i].ds_on = 0;
        glDeleteTextures(1, &d->rb);
    }
    s_new_obj = 1;
    *fresh = 1;
    d->va = va; d->pw = pw; d->ph = ph; d->used = s_frame;
    glGenTextures(1, &d->rb);
    glBindTexture(GL_TEXTURE_2D, d->rb);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (GLsizei)pw, (GLsizei)ph,
                 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    tex_bind_forget();
    if (!d->rb) gl_fail("depth allocation");
    gl_check("depth allocation");
    if (s_trace)
        fprintf(stderr, "  [GL] depth 0x%08X %ux%u\n", va, pw, ph);
    return d->rb;
}

static GlSurf *surf_bind(const Nv2aSurface *sf, uint32_t zeta_va)
{
    uint32_t bpp = sf->bytes_per_pixel ? sf->bytes_per_pixel : 4;
    uint32_t w = sf->pitch ? sf->pitch / bpp : sf->width;
    uint32_t h = sf->clip_y + sf->height;   /* the clip need not start at 0 */
    GlSurf *s;
    GLuint ds;
    int fresh = 0, keep = 0;
    uint32_t zeta_pitch = zeta_va && s_regs ? s_regs[0x20C / 4] >> 16 : 0;

    if (w < sf->clip_x + sf->width) w = sf->clip_x + sf->width;
    /* Linear 32-bit BGRA storage: X8/Z8 (4), X8/O8 (5) and ARGB8 (8).
     * All use RGBA8 storage in xemu's GL surface map as well. Keep the
     * guest-programmed channel masks; do not invent an alpha conversion.
     * Packed integer Z24S8 is the supported depth storage. */
    if (gl_gpu()) {
        if (!w || !h || !sf->color_va)
            return NULL;
    } else if (!nv2a_backend_surface_writable(sf) || sf->bytes_per_pixel != 4 ||
        ((sf->format_raw & 15u) != 4u && (sf->format_raw & 15u) != 5u &&
         (sf->format_raw & 15u) != 8u) || ((sf->format_raw >> 8) & 15u) != 1u ||
        (sf->pitch & 3u) || !w || !h || w > 4096 || h > 4096 || s_scale != 1.0) {
        fprintf(stderr, "[MH-GL] rejected surface va=%08X format=%08X bpp=%u pitch=%u clip=%ux%u+%u+%u scale=%g\n",
                sf->color_va, sf->format_raw, sf->bytes_per_pixel, sf->pitch,
                sf->width, sf->height, sf->clip_x, sf->clip_y, s_scale);
        gl_fail("unsupported surface layout/format/scale");
    }
    if (!gl_gpu() && zeta_va && (((sf->format_raw >> 4) & 15u) != 2u ||
        !s_regs || (s_regs[0x290 / 4] & (1u << 12))))
        gl_fail("unsupported depth format (requires integer Z24S8)");
    if (gl_fenced()) {
        GlSurf *old = surf_find(sf->color_va);
        /* Switching/reshaping a target may reuse depth, color or aliased
         * physical memory. Publish before changing its ownership metadata. */
        if (s_last && (old != s_last || old->w != w || old->h < h ||
            old->guest.pitch != sf->pitch || old->guest.format_raw != sf->format_raw ||
            old->zeta_va != zeta_va || old->zeta_pitch != zeta_pitch)) gl_sync(1);
        if (old) surf_check_conflict(old);
    }
    s = surf_get(sf->color_va, w, h, sf->aa_sx ? sf->aa_sx : 1,
                 sf->aa_sy ? sf->aa_sy : 1);
    if (gl_fenced() && s->resident && s->guest.pitch == sf->pitch &&
        s->guest.format_raw == sf->format_raw && s->zeta_va == zeta_va &&
        s->zeta_pitch == zeta_pitch && surf_ram_matches(s)) keep = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    if (s_shared_z < 0) {
        const char *e = getenv("RECOMP_GL_SHARED_Z");
        s_shared_z = !(e && e[0] == '0');
    }
    ds = zeta_va && s_shared_z ? depth_get(zeta_va, s->pw, s->ph, &fresh) : s->ds;
    if (s->ds_on != ds) {
        keep = 0;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, ds, 0);
        s->ds_on = ds;
    }
    if (fresh) {                        /* new storage is undefined */
        keep = 0;
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(1);
        glStencilMask(0xFF);
        glClearDepth(1.0);
        glClearStencil(0);
        state_dirty();
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    if (!gl_gpu() && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        gl_fail("depth attachment incomplete");
    s->guest = *sf;
    s->zeta_va = zeta_va;
    s->zeta_pitch = zeta_pitch;
    if (gl_gpu()) {                     /* no RAM import, no staging copies */
        glViewport(0, 0, (GLsizei)s->pw, (GLsizei)s->ph);
        s_last = s;
        return s;
    }
    if (!ram_span(sf->color_va, (size_t)sf->pitch * s->h))
        gl_fail("color storage outside RAM");
    if (zeta_va && (s->zeta_pitch < s->w * 4u || (s->zeta_pitch & 3u) ||
        !ram_span(zeta_va, (size_t)s->zeta_pitch * s->h)))
        gl_fail("depth storage outside supported RAM/pitch");
    if (!s->color_copy) s->color_copy = malloc((size_t)s->w * s->h * 4);
    if (zeta_va && !s->depth_copy) s->depth_copy = malloc((size_t)s->w * s->h * 4);
    if (!s->color_copy || (zeta_va && !s->depth_copy)) gl_fail("surface staging allocation");
    if (gl_fenced()) {
        size_t bytes=(size_t)s->w*s->h*4;
        if (!s->color_baseline) s->color_baseline=malloc(bytes);
        if (zeta_va && !s->depth_baseline) s->depth_baseline=malloc(bytes);
        if (!s->color_baseline || (zeta_va && !s->depth_baseline)) gl_fail("surface baseline allocation");
    }
    /* Strict preserves per-callback RAM completion. Fenced keeps ownership
     * until the explicit runtime completion hook; unchanged RAM is not uploaded
     * over newer GPU pixels. Clean CPU edits are imported on the next use. */
    if (!keep) {
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(sf->pitch / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h,
                    GL_BGRA, GL_UNSIGNED_BYTE,
                    (const uint8_t *)xbox_GetMemoryOffset() + sf->color_va);
    if (zeta_va) {
        glBindTexture(GL_TEXTURE_2D, ds);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(s->zeta_pitch / 4));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h,
                        GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                        (const uint8_t *)xbox_GetMemoryOffset() + zeta_va);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    tex_bind_forget();
    gl_check("surface RAM import");
    if (gl_fenced()) {
        rows_snapshot(sf->color_va,sf->pitch,s->color_baseline,s->w,s->h);
        if (zeta_va) rows_snapshot(zeta_va,s->zeta_pitch,s->depth_baseline,s->w,s->h);
        s->resident=1;
    }
    }
    glViewport(0, 0, (GLsizei)s->pw, (GLsizei)s->ph);
    s_last = s;
    return s;
}

static void gl_sync(int invalidate)
{
    unsigned i, y;
    /* Fenced completion is the public RAM-visibility boundary. Stable CPU
     * writes during GPU ownership are rejected before any overwrite. */
    if (s_state != 1 || gl_gpu()) return;
    for (i = 0; i < GL_MAX_SURF; i++) {
        GlSurf *s = &s_surf[i];
        if (!s->fbo) continue;
        if (!s->dirty) { if (invalidate) s->resident=0; continue; }
        surf_check_conflict(s);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h,
                     GL_BGRA, GL_UNSIGNED_BYTE, s->color_copy);
        if (s->zeta_va)
            glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h,
                         GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, s->depth_copy);
        gl_check("surface readback");
        /* Readback can block. Detect a stable CPU edit made during that
         * wait as well, before replacing guest bytes. This is detection,
         * not a general atomic memory-access trap. */
        surf_check_conflict(s);
        for (y = 0; y < s->h; y++) {
            memcpy((uint8_t *)xbox_GetMemoryOffset() + s->guest.color_va + (size_t)y * s->guest.pitch,
                   s->color_copy + (size_t)y * s->w * 4, (size_t)s->w * 4);
            if (s->zeta_va)
                memcpy((uint8_t *)xbox_GetMemoryOffset() + s->zeta_va + (size_t)y * s->zeta_pitch,
                       s->depth_copy + (size_t)y * s->w * 4, (size_t)s->w * 4);
        }
        s->dirty = 0;
        if (gl_fenced()) {
            memcpy(s->color_baseline,s->color_copy,(size_t)s->w*s->h*4);
            if (s->zeta_va) memcpy(s->depth_baseline,s->depth_copy,(size_t)s->w*s->h*4);
            s->resident = !invalidate;
        }
    }
    if (s_last) glBindFramebuffer(GL_FRAMEBUFFER, s_last->fbo);
    state_dirty();
    /* The caller publishes GET/semaphore completion after this returns.
     * Keep guest-RAM stores before that publication on weakly ordered CPUs. */
    atomic_thread_fence(memory_order_release);
}

/* ── Textures ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t va, color, w, h, pitch, hash, bytes;
    uint32_t pal;               /* palette VA for 0x0B, else 0 */
    GLuint   tex;
    uint32_t used, checked;
    uint8_t *source;
    size_t source_bytes;
    uint32_t palette_word;
} GlTex;

#define GL_MAX_TEX 1024
static GlTex     s_tex[GL_MAX_TEX];
/* (va, format, size) -> s_tex index, chained; entries hold index + 1. A
 * draw binds up to four textures, and a scan of all 1024 entries per stage
 * was the biggest CPU cost of a draw after the driver. */
#define TEX_BUCKETS 2048
static uint16_t s_tex_head[TEX_BUCKETS], s_tex_next[GL_MAX_TEX];
static uint32_t tex_bucket(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    uint32_t k = va ^ (color << 24) ^ (w << 12) ^ (h << 4);
    k ^= k >> 15; k *= 0x2C1B3C6Du; k ^= k >> 12;
    return k & (TEX_BUCKETS - 1);
}
static void tex_index_add(GlTex *t)
{
    uint32_t b = tex_bucket(t->va, t->color, t->w, t->h);
    uint16_t i = (uint16_t)(t - s_tex);
    s_tex_next[i] = s_tex_head[b];
    s_tex_head[b] = (uint16_t)(i + 1);
}
static void tex_index_del(GlTex *t)
{
    uint16_t *at = &s_tex_head[tex_bucket(t->va, t->color, t->w, t->h)];
    uint16_t i = (uint16_t)(t - s_tex);
    while (*at) {
        if (*at == i + 1) {
            *at = s_tex_next[i];
            return;
        }
        at = &s_tex_next[*at - 1];
    }
}
static uint32_t *s_decode;
static size_t    s_decode_cap;

static int tex_size_from_format(uint32_t color)
{
    /* Swizzled (0x00-0x0B, 0x19, 0x1A, 0x27-0x3F except linear) and DXT
     * carry their size in the format word; linear images use IMAGE_RECT. */
    if (color >= 0x0C && color <= 0x0F)
        return 1;
    switch (color) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x16: case 0x17: case 0x18: case 0x1B: case 0x1C: case 0x1D:
    case 0x1E: case 0x1F: case 0x20: case 0x24: case 0x25: case 0x26:
    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x34: case 0x35:
    case 0x36: case 0x37: case 0x40: case 0x41: case 0x43: case 0x44:
    case 0x45: case 0x46:
        return 0;
    default:
        return 1;
    }
}

/* gpu mode only: the change detection of the measured runs. Exact compares of
 * every texture at every bind are a large part of the strict path's cost. */
static uint32_t bytes_hash(uint32_t va, uint32_t bytes)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset() + va;
    uint32_t h = 2166136261u ^ bytes, step, i;

    if (!bytes)
        return h;
    step = bytes / 256;
    if (step < 4) step = 4;
    step &= ~3u;
    for (i = 0; i + 4 <= bytes; i += step) {
        uint32_t w;
        memcpy(&w, mem + i, 4);
        h = (h ^ w) * 16777619u;
    }
    memcpy(&i, mem + bytes - 4, 4);
    return (h ^ i) * 16777619u;
}

/* Every byte, for linear images. A sampled hash steps bytes/256, which for a
 * power-of-two pitch is a whole number of rows: every sample lands in the same
 * column. Carbon's EA logo movie (512x256 A8R8G8B8, pitch 2048, a ring of
 * three) is black down that column, so its frames were never re-uploaded and
 * the logo cycled through the first three. Linear images are few (movies,
 * CPU-written pictures), so reading all of them once a frame is cheap. */
static uint32_t bytes_hash_full(uint32_t va, uint32_t bytes)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset() + va;
    uint64_t a = 14695981039346656037ull ^ bytes, b = 1099511628211ull, x, y;
    uint32_t i;

    for (i = 0; i + 16 <= bytes; i += 16) {
        memcpy(&x, mem + i, 8);
        memcpy(&y, mem + i + 8, 8);
        a = (a ^ x) * 1099511628211ull;
        b = (b ^ y) * 1099511628211ull;
    }
    for (; i < bytes; i++)
        a = (a ^ mem[i]) * 1099511628211ull;
    a ^= b * 31u;
    return (uint32_t)(a ^ (a >> 32));
}

static uint32_t tex_bytes(uint32_t color, uint32_t w, uint32_t h, uint32_t pitch)
{
    uint64_t n;
    unsigned bpp;
    if (gl_gpu()) {
        if (color == 0x0C) return ((w + 3) / 4) * ((h + 3) / 4) * 8;
        if (color == 0x0E || color == 0x0F) return ((w + 3) / 4) * ((h + 3) / 4) * 16;
        if (pitch) return pitch * h;
        return w * h * 4;      /* upper bound for swizzled formats */
    }
    if (!w || !h || w > 4096 || h > 4096) gl_fail("texture dimensions");
    if (color == 0x0C) n = (uint64_t)((w + 3) / 4) * ((h + 3) / 4) * 8;
    else if (color == 0x0E || color == 0x0F) n = (uint64_t)((w + 3) / 4) * ((h + 3) / 4) * 16;
    else {
        switch (color) {
        case 0x00: case 0x0B: case 0x13: case 0x19: case 0x1F: bpp=1; break;
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11:
        case 0x1C: case 0x1D: case 0x24: case 0x25: bpp=2; break;
        case 0x06: case 0x07: case 0x12: case 0x1E: case 0x3F: case 0x40: case 0x41: bpp=4; break;
        default: gl_fail("unsupported texture format");
        }
        if (pitch && pitch < w * bpp) gl_fail("short texture pitch");
        if (!pitch && ((w & (w-1)) || (h & (h-1)))) gl_fail("non-power-of-two swizzled texture");
        n = pitch ? (uint64_t)pitch * h : (uint64_t)w * h * bpp;
    }
    if (n > UINT32_MAX) gl_fail("texture span overflow");
    return (uint32_t)n;
}

/* Xbox swizzle: texel (u, v) of a power-of-two image sits at the index made
 * by interleaving the coordinate bits, u in the even positions and v in the
 * odd ones, until the smaller dimension runs out; the larger one's remaining
 * bits follow on top. */
static uint32_t swizzle_index(uint32_t u, uint32_t v, uint32_t w, uint32_t h)
{
    uint32_t out = 0, bit = 0, mw = w - 1, mh = h - 1, m;

    for (m = 1; mw >= m || mh >= m; m <<= 1) {
        if (mw >= m) { if (u & m) out |= 1u << bit; bit++; }
        if (mh >= m) { if (v & m) out |= 1u << bit; bit++; }
    }
    return out;
}

/* SZ_I8_A8R8G8B8: 8-bit indices, swizzled, into an A8R8G8B8 palette named by
 * SET_TEXTURE_PALETTE (offset in the upper bits, length code in [3:2]:
 * 256, 128, 64 or 32 entries). The executor's decoder has no palette state,
 * so this format is decoded here. */
static uint32_t s_palette_reg, s_palette_va;   /* stage being bound */
static const Nv2aRawBatch *s_batch;          /* draw being executed */

static int decode_indexed(uint32_t va, uint32_t w, uint32_t h, uint32_t *out)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t pal_va = s_palette_va;
    uint32_t entries = 256u >> ((s_palette_reg >> 2) & 3);
    const uint8_t *idx = mem + va;
    uint32_t x, y;

    if (!(s_palette_reg & ~0x3Fu))
        return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t i = idx[swizzle_index(x, y, w, h)];
            uint32_t c;
            if (i >= entries)
                i = 0;
            memcpy(&c, mem + pal_va + i * 4, 4);
            out[y * w + x] = c;
        }
    return 1;
}

/* GPU memory held by the texture cache, and its budget. The cache was
 * bounded by entries (GL_MAX_TEX) only; 1024 large textures is gigabytes,
 * and a console out of GPU memory does not fail an allocation -- it hangs.
 * Over budget, the least recently used textures go. RECOMP_GL_TEX_MB sets
 * it (default 256). */
static uint64_t s_tex_bytes;

uint64_t nv2a_gl_texture_bytes(void) { return s_tex_bytes; }
uint32_t nv2a_gl_frame_count(void) { return s_frame; }

static void tex_account(GlTex *t, uint32_t bytes)
{
    s_hz_texs++;
    s_hz_tex_bytes += bytes;
    static uint64_t budget;
    if (!budget) {
        const char *e = getenv("RECOMP_GL_TEX_MB");
        budget = (uint64_t)(e && atoi(e) > 0 ? atoi(e) : 256) << 20;
    }
    s_tex_bytes += (uint64_t)bytes - t->bytes;
    t->bytes = bytes;
    while (s_tex_bytes > budget) {
        GlTex *lru = NULL;
        uint32_t i;
        for (i = 0; i < GL_MAX_TEX; i++) {
            GlTex *c = &s_tex[i];
            if (c->tex && c != t && c->used != s_frame && (!lru || c->used < lru->used))
                lru = c;
        }
        if (!lru)
            break;                      /* everything left is in use this frame */
        glDeleteTextures(1, &lru->tex);
        tex_bind_forget();
        s_tex_bytes -= lru->bytes;
        tex_index_del(lru);
        free(lru->source);
        memset(lru, 0, sizeof *lru);
    }
}

static GLuint tex_get(uint32_t va, uint32_t color, uint32_t w, uint32_t h,
                      uint32_t pitch)
{
    GlTex *t = NULL, *victim = NULL;
    uint32_t hash = 0;
    uint32_t i, bytes = tex_bytes(color, w, h, pitch);
    uint32_t pal_bytes = color == 0x0B ? (256u >> ((s_palette_reg >> 2) & 3u)) * 4u : 0;
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    Nv2aTexture nt;
    if (!gl_gpu() && (!ram_span(va, bytes) || (pal_bytes && !ram_span(s_palette_va, pal_bytes))))
        gl_fail("texture or palette outside RAM");
    /* Manhunt port addition: each texture format once, with a size and address, so a run shows which formats a
     * screen uses (the settings page's text draws as solid blocks in this renderer). */
    {
        static uint8_t seen[128];
        if (!seen[color & 127]) {
            seen[color & 127] = 1;
            fprintf(stderr, "  [GL] texture format 0x%02X first used: %ux%u pitch %u at 0x%08X\n",
                    (unsigned)color, (unsigned)w, (unsigned)h, (unsigned)pitch, (unsigned)va);
        }
    }
    /* An indexed texture is one image per palette: Burnout 3's traffic
     * shares its paint indices and gives each car a palette. Keyed by
     * address alone, every car took the palette of the first one drawn
     * that frame, and the colours swapped from frame to frame (#11). */
    uint32_t pal = color == 0x0B ? s_palette_va : 0;

    for (i = s_tex_head[tex_bucket(va, color, w, h)]; i; i = s_tex_next[i - 1]) {
        GlTex *c = &s_tex[i - 1];
        if (c->tex && c->va == va && c->color == color && c->w == w && c->h == h
            && c->pal == pal) {
            t = c;
            break;
        }
    }
    if (!t)                                  /* a new texture: find a slot */
        for (i = 0; i < GL_MAX_TEX; i++) {
            GlTex *c = &s_tex[i];
            if (!c->tex) { if (!victim || victim->tex) victim = c; }
            else if (!victim || (victim->tex && c->used < victim->used)) victim = c;
        }
    /* No probabilistic hash and no once-per-frame exemption: guest writes
     * can change any byte between draws, including within a frame. */
    if (gl_gpu()) {
        /* Once per frame per texture is enough to notice an update. */
        if (t && t->checked == s_frame) {
            t->used = s_frame;
            return t->tex;
        }
        hash = pitch ? bytes_hash_full(va, bytes) : bytes_hash(va, bytes);
        if (color == 0x0B)                   /* the palette is content too */
            hash ^= bytes_hash(s_palette_va, 1024) * 31u;
        if (t && t->hash == hash) {
            t->used = t->checked = s_frame;
            return t->tex;
        }
    } else if (t && t->pitch == pitch && t->palette_word == s_palette_reg &&
        t->source_bytes == (size_t)bytes + pal_bytes && t->source &&
        !memcmp(t->source, mem + va, bytes) &&
        (!pal_bytes || !memcmp(t->source + bytes, mem + s_palette_va, pal_bytes))) {
        t->used = t->checked = s_frame;
        return t->tex;
    }
    if (!t) {
        t = victim;
        if (t->tex) {
            glDeleteTextures(1, &t->tex);
            tex_bind_forget();
            s_tex_bytes -= t->bytes;
            tex_index_del(t);
        }
        free(t->source);
        memset(t, 0, sizeof *t);
        t->va = va; t->color = color; t->w = w; t->h = h; t->pal = pal;
        tex_index_add(t);
        glGenTextures(1, &t->tex);
        tex_param_forget(t->tex);
    } else if (gl_gpu() && color == 0x12 && t->pitch == pitch && pitch && !(pitch & 3)
               && pitch / 4 >= w) {
        /* A linear A8R8G8B8 image whose contents changed -- every frame of a
         * movie. That is GL's BGRA/UNSIGNED_BYTE byte for byte: update the
         * existing storage straight from guest memory, no decode, no
         * reallocation. */
        t->hash = hash; t->used = t->checked = s_frame;
        bind_tex(t->tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 4));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA,
                        GL_UNSIGNED_BYTE, (const uint8_t *)xbox_GetMemoryOffset() + va);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl_step_done("texture upload");
        return t->tex;
    }
    t->va = va; t->color = color; t->w = w; t->h = h; t->pitch = pitch;
    t->hash = hash;
    t->used = t->checked = s_frame;
    if (!gl_gpu()) {
        free(t->source);
        t->source_bytes = (size_t)bytes + pal_bytes;
        t->source = malloc(t->source_bytes);
        if (!t->source) gl_fail("texture snapshot allocation");
        memcpy(t->source, mem + va, bytes);
        if (pal_bytes) memcpy(t->source + bytes, mem + s_palette_va, pal_bytes);
        t->palette_word = s_palette_reg;
    }
    s_new_obj = 1;
    /* Manhunt port addition: every upload with its frame, so opening a screen shows which textures it brings in.
     * One write per line, since the log splits lines written in pieces. Bounded. */
    {
        static uint32_t logged;
        if (logged < 600) {
            char line[128];
            logged++;
            snprintf(line, sizeof line, "  [GL] texture upload frame %u format 0x%02X %ux%u pitch %u at 0x%08X\n",
                     (unsigned)s_frame, (unsigned)color, (unsigned)w, (unsigned)h, (unsigned)pitch, (unsigned)va);
            fputs(line, stderr);
        }
    }
    if (s_trace)
        fprintf(stderr, "  [GL] texture 0x%08X format 0x%02X %ux%u pitch %u\n",
                va, color, w, h, pitch);

    if (color == 0x12 && pitch && !(pitch & 3) && pitch / 4 >= w) {
        bind_tex(t->tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 4));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, (const uint8_t *)xbox_GetMemoryOffset() + va);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        tex_account(t, w * h * 4);
        gl_check("linear texture upload");
        return t->tex;
    }

    if (color == 0x0C || color == 0x0E || color == 0x0F) {
        /* DXT: the Switch's GPU samples it as it is. Uploaded compressed:
         * no per-texel decode (it cost ~70 ms per MB of textures on the
         * console, the race's mid-lap hitches) and 4-8x less to copy. Falls
         * back to the decoder if the driver says no. RECOMP_GL_DXT=0 off. */
        static int s3tc = -1;
        if (s3tc < 0) {
            const char *e = getenv("RECOMP_GL_DXT");
            s3tc = !(e && *e == '0');
        }
        if (s3tc) {
            GLenum fmt = color == 0x0C ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                       : color == 0x0E ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                       : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            uint32_t size = tex_bytes(color, w, h, 0);
            bind_tex(t->tex);
            while (glGetError() != GL_NO_ERROR) { }
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, (GLsizei)w, (GLsizei)h, 0,
                                   (GLsizei)size, (const uint8_t *)xbox_GetMemoryOffset() + va);
            if (glGetError() == GL_NO_ERROR) {
                tex_account(t, size);
                gl_step_done("texture upload");
                return t->tex;
            }
            s3tc = 0;
            fprintf(stderr, "  [GL] compressed DXT upload refused; decoding DXT on the CPU\n");
        }
    }
    if ((size_t)w * h > s_decode_cap) {
        free(s_decode);
        s_decode_cap = (size_t)w * h;
        s_decode = (uint32_t *)malloc(s_decode_cap * 4);
    }
    memset(&nt, 0, sizeof nt);
    nt.offset = va; nt.width = w; nt.height = h; nt.pitch = pitch; nt.color = color;
    nt.addr_u = nt.addr_v = 1;
    bind_tex(t->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (s_decode && (color == 0x06 || color == 0x07) && w >= 1 && h >= 1) {
        /* Swizzled A8R8G8B8 / X8R8G8B8: un-Morton the texels in a loop
         * instead of a sampler call per texel. */
        const uint32_t *src = (const uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + va);
        uint32_t x, y, fill = color == 0x07 ? 0xFF000000u : 0;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                s_decode[(size_t)y * w + x] = src[swizzle_index(x, y, w, h)] | fill;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, s_decode);
        tex_account(t, w * h * 4);
        gl_step_done("texture upload");
    } else if (s_decode && (color == 0x0B ? decode_indexed(va, w, h, s_decode)
                                    : nv2a_backend_decode_texture(&nt, s_decode))) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, s_decode);
        tex_account(t, w * h * 4);
        gl_step_done("texture upload");
    } else if (gl_gpu()) {
        static const uint32_t magenta = 0xFFFF00FFu;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, &magenta);
        tex_account(t, 4);
        if (s_trace)
            fprintf(stderr, "  [GL] texture 0x%08X format 0x%02X not decodable\n",
                    va, color);
    } else {
        gl_fail("texture decode or allocation failed");
    }
    gl_check("texture upload");
    return t->tex;
}

static GLint wrap_mode(uint32_t m)
{
    switch (m) {
    case 2:  return GL_MIRRORED_REPEAT;
    case 3:  return GL_CLAMP_TO_EDGE;
    case 4:  return GL_CLAMP_TO_BORDER;
    case 5:  return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

/* A surface kept taller than its clip (surf_get) and sampled as a texture
 * of the clip's size: texcoords cover the w x h texels at the top left, not
 * the whole surface. A texture of the surface's logical or real size keeps
 * the old full-surface mapping (anti-aliased surfaces). */
static void rt_scale(uint32_t w, uint32_t h, uint32_t sw, uint32_t sh,
                     uint32_t aa_sx, uint32_t aa_sy, float scale[2])
{
    uint32_t lw = sw / (aa_sx ? aa_sx : 1), lh = sh / (aa_sy ? aa_sy : 1);

    if (w < lw) scale[0] *= (float)w / (float)lw;
    if (h < lh) scale[1] *= (float)h / (float)lh;
}

/* Bind stage `i` from the method shadow; returns the texcoord scale. */
static void bind_stage(const uint32_t *regs, int i, float scale[2])
{
    uint32_t base = (0x1B00u + (uint32_t)i * 0x40u) / 4;
    uint32_t control0 = regs[base + 3];
    uint32_t format = regs[base + 1];
    uint32_t color = (format >> 8) & 0xFF;
    uint32_t va, w, h, pitch = 0;
    GLuint tex = 0;
    GlSurf *rt;

    scale[0] = scale[1] = 1.0f;
    active_unit(i);
    if (!(control0 & 0x40000000u) || !regs[base]) {
        bind_tex(0);
        return;
    }
    va = s_batch->tex_va[i];
    if (tex_size_from_format(color)) {
        w = 1u << ((format >> 20) & 0xF);
        h = 1u << ((format >> 24) & 0xF);
    } else {
        uint32_t rect = regs[base + 7];
        w = rect >> 16;
        h = rect & 0xFFFF;
        pitch = regs[base + 4] >> 16;
        if (w) scale[0] = 1.0f / (float)w;
        if (h) scale[1] = 1.0f / (float)h;
    }
    if (!w || !h || w > 4096 || h > 4096) {
        bind_tex(0);
        return;
    }
    s_palette_reg = regs[base + 8];          /* SET_TEXTURE_PALETTE */
    s_palette_va = s_batch->pal_va[i];
    if (gl_gpu() && (rt = surf_find(va)) != NULL) {
        tex = rt->tex;
        /* The image the title samples is the logical one; the FBO holds it
         * at the anti-aliased size. Normalised coordinates cover both. */
        rt_scale(w, h, rt->w, rt->h, rt->aa_sx, rt->aa_sy, scale);
    } else {
        /* Prior callbacks publish their pixels to RAM; do not reuse a GPU
         * surface by address (CPU writes or allocation reuse can replace it). */
        texture_surface_sync(va,tex_bytes(color,w,h,pitch));
        if (color == 0x0B)
            texture_surface_sync(s_palette_va,(256u >> ((s_palette_reg >> 2)&3u))*4u);
        /* A source overlap flush can change framebuffer bindings/state. */
        tex = tex_get(va, color, w, h, pitch);
    }
    bind_tex(tex);
    {
        /* Sampler state lives in the texture object: set it only when it
         * differs from what that texture last got. A texture id reused for a
         * new texture starts from GL's defaults, so the cache entry is keyed
         * by id and invalidated where textures are created (tex_param_forget). */
        uint32_t addr = regs[base + 2];
        uint32_t filter = regs[base + 5];
        GLint mag = ((filter >> 24) & 0xF) == 1 ? GL_NEAREST : GL_LINEAR;
        GLint min = ((filter >> 16) & 0xFF) == 1 ? GL_NEAREST : GL_LINEAR;
        uint32_t key = (addr & 0xF0F) | ((uint32_t)(mag == GL_NEAREST) << 16)
                     | ((uint32_t)(min == GL_NEAREST) << 17) | 0x80000000u;
        TexParam *tp = &s_tex_param[tex % TEX_PARAM_SLOTS];
        if (tp->id != tex || tp->key != key) {
            tp->id = tex;
            tp->key = key;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode(addr & 0xF));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode((addr >> 8) & 0xF));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
        }
    }
}

/* ── Programs ─────────────────────────────────────────────────────── */

typedef struct {
    uint64_t   vkey;            /* 0: fixed-function; else program hash */
    Nv2aPshKey pkey;
    GLuint     prog;
    GLint      u_c, u_surf, u_aa, u_m, u_vpoff, u_xform, u_wide;
    GLint      u_t[4], u_tscale, u_c0, u_c1, u_fc0, u_fc1, u_fogcolor, u_afunc, u_aref, u_fogp;
    uint32_t   used;
    int        vpc_valid;       /* vpc holds what this program's "c" was given */
    uint32_t   vpc_gen;         /* ... as of the executor's constant version */
    int        vpc_rows;        /* c[i]'s location is u_c + i: rows can go alone */
    uint32_t   hash;            /* bucket of (vkey, pkey) */
    int        next;            /* chain in s_prog_head, index + 1 */
    float      vpc[192][4];
    int        uni_valid;       /* uni holds the rest of its uniforms */
    struct GlUni {
        float surf[4], aa[2], m[16], vpoff[4], tscale[4][2];
        float c0[8][4], c1[8][4], fog[4], fc[2][4], aref, fogp[4], wide[2];
        int   xform, afunc;
    } uni;
} GlProg;

#define GL_MAX_PROG 1024
static GlProg s_prog[GL_MAX_PROG];
static uint32_t s_prog_made, s_prog_live;
#define PROG_BUCKETS 1024
static int s_prog_head[PROG_BUCKETS];      /* index + 1 */
static uint32_t prog_bucket(uint64_t vkey, const Nv2aPshKey *pk)
{
    const uint32_t *w = (const uint32_t *)pk;
    uint64_t h = vkey ^ 1469598103934665603ull;
    size_t i;
    for (i = 0; i < sizeof *pk / 4; i++)
        h = (h ^ w[i]) * 1099511628211ull;
    return (uint32_t)(h ^ (h >> 32)) & (PROG_BUCKETS - 1);
}
static void prog_unlink(GlProg *p)
{
    int *at = &s_prog_head[p->hash], i = (int)(p - s_prog) + 1;
    while (*at) {
        if (*at == i) { *at = p->next; return; }
        at = &s_prog[*at - 1].next;
    }
}

uint32_t nv2a_gl_program_count(void) { return s_prog_live; }

static GLuint compile(GLenum type, const char *const *src, int n);

/* Manhunt port addition: the title's gamma ramp at present (xbox_gamma.c). The Xbox applies it at scanout, so a
 * blit alone shows the picture before it. One pass from the surface texture through a 256x1 lookup. */
#include "../xbox_gamma.h"
static GLuint s_gamma_prog, s_gamma_lut, s_gamma_vao;
static GLint  s_gamma_rect = -1;
static int    s_gamma_failed;
static int gamma_pass(GLuint src, int pw, int ph, int cy, int dx, int dy, int dw, int dh)
{
    uint8_t r[256], g[256], b[256], lut[256 * 4];
    if (s_gamma_failed) {
        if (gl_gpu()) return 0;
        gl_fail("gamma program unavailable");
    }
    if (!xbox_gamma_ramp(r, g, b)) return 0;
    if (!s_gamma_prog) {
        static const char *vs[] = {"#version 330 core\n"
            "out vec2 uv;\n"
            "void main() { vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);\n"
            "    uv = (p + 1.0) * 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n"};
        static const char *fs[] = {"#version 330 core\n"
            "in vec2 uv; out vec4 o; uniform sampler2D src; uniform sampler2D lut; uniform vec4 rect;\n"
            "void main() { vec3 c = texture(src, vec2(mix(rect.x, rect.z, uv.x), mix(rect.y, rect.w, uv.y))).rgb;\n"
            "    vec3 i = (floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5) + 0.5) / 256.0;\n"
            "    o = vec4(texture(lut, vec2(i.r, 0.5)).r, texture(lut, vec2(i.g, 0.5)).g, texture(lut, vec2(i.b, 0.5)).b, 1.0); }\n"};
        GLuint v = compile(GL_VERTEX_SHADER, vs, 1), f = compile(GL_FRAGMENT_SHADER, fs, 1);
        GLint ok = 0;
        if (v && f) {
            s_gamma_prog = glCreateProgram();
            if (!s_gamma_prog) gl_fail("gamma program allocation failed");
            glAttachShader(s_gamma_prog, v);
            glAttachShader(s_gamma_prog, f);
            glLinkProgram(s_gamma_prog);
            glGetProgramiv(s_gamma_prog, GL_LINK_STATUS, &ok);
        }
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        if (!ok) {
            fprintf(stderr, "  [GL] gamma pass unavailable: its shaders did not build; the ramp is not applied\n");
            if (s_gamma_prog) glDeleteProgram(s_gamma_prog);
            s_gamma_prog = 0;
            s_gamma_failed = 1;
            if (gl_gpu()) return 0;
            gl_fail("gamma shader compilation/link failed");
        }
        glUseProgram(s_gamma_prog);
        glUniform1i(glGetUniformLocation(s_gamma_prog, "src"), 0);
        glUniform1i(glGetUniformLocation(s_gamma_prog, "lut"), 1);
        s_gamma_rect = glGetUniformLocation(s_gamma_prog, "rect");
        glGenTextures(1, &s_gamma_lut);
        glGenVertexArrays(1, &s_gamma_vao);
        fprintf(stderr, "  [GL] gamma pass ready: the title's ramp is applied at present\n");
    }
    for (int i = 0; i < 256; i++) { lut[i * 4] = r[i]; lut[i * 4 + 1] = g[i]; lut[i * 4 + 2] = b[i]; lut[i * 4 + 3] = 255; }
    /* Rows are stored top first, as the blit's flip shows: the bottom of the picture reads row ph - cy. */
    const float rect[4] = {0.0f, (float)(ph - cy) / (float)ph, 1.0f, (float)cy / (float)ph};
    glUseProgram(s_gamma_prog);
    s_cur_prog = s_gamma_prog;
    glUniform4fv(s_gamma_rect, 1, rect);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, s_gamma_lut);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lut);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    tex_param_forget(src);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glViewport(dx, dy, dw, dh);
    glBindVertexArray(s_gamma_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(s_vao);
    glViewport(0, 0, (GLsizei)pw, (GLsizei)ph);
    state_dirty();                       /* blend, depth, cull and texture bindings are re-applied by the next draw */
    return 1;
}

static GLuint compile(GLenum type, const char *const *src, int n)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;

    if (!sh) gl_fail("shader allocation failed");

    glShaderSource(sh, n, src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        int i;
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] %s shader failed:\n%s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        if (s_trace)
            for (i = 0; i < n; i++)
                fprintf(stderr, "%s", src[i]);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static uint64_t prog_hash(const Nv2aRawBatch *b)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t pc, k;

    for (pc = b->vp_start; pc < b->vp_slots; pc++) {
        for (k = 0; k < 4; k++)
            h = (h ^ b->vp_program[pc][k]) * 1099511628211ull;
        if (b->vp_program[pc][3] & 1)           /* FINAL */
            break;
    }
    return h ? h : 1;
}

/* ── Programs remembered across runs ─────────────────────────────────
 *
 * Mesa compiles a program the first time a draw needs it, ~40 ms each on
 * the Switch; a race start needs ~40 new ones and stalled 1.5 s over two
 * frames. Each program compiled is appended to a file (what building it
 * takes: transform mode, program slots, combiner key), and at start-up the
 * renderer compiles everything in it before the title draws, so a later run
 * finds them ready. RECOMP_PROG_CACHE=<path> moves the file, =0 disables. */
#define PROG_REC_MAGIC 0x4350564Eu          /* "NVPC" */
typedef struct {
    uint32_t   magic, xform, vp_start, slots;
    Nv2aPshKey pk;
    uint32_t   prog[136][4];
} ProgRec;
static int s_prewarming;

static const char *prog_cache_path(void)
{
    const char *e = getenv("RECOMP_PROG_CACHE");
    if (e && *e == '0' && !e[1])
        return NULL;
    if (e && *e)
        return e;
#if defined(__SWITCH__)
    return "sdmc:/switch/manhunt1_nx/progcache.bin";
#else
    return "progcache.bin";
#endif
}

static void prog_cache_append(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    const char *path = prog_cache_path();
    ProgRec rec;
    FILE *f;
    if (s_prewarming || !path)
        return;
    memset(&rec, 0, sizeof rec);
    rec.magic = PROG_REC_MAGIC;
    rec.xform = b->xform == 2 ? 2 : 1;
    rec.pk = *pk;
    if (rec.xform == 2 && b->vp_program && b->vp_slots <= 136) {
        rec.vp_start = b->vp_start;
        rec.slots = b->vp_slots;
        memcpy(rec.prog, b->vp_program, (size_t)b->vp_slots * 16);
    }
    f = fopen(path, "ab");
    if (f) {
        fwrite(&rec, sizeof rec, 1, f);
        fclose(f);
    }
}

static GlProg *prog_get(const Nv2aRawBatch *b, const Nv2aPshKey *pk);

static void prog_cache_prewarm(void)
{
    const char *path = prog_cache_path();
    struct timespec t0, t1;
    ProgRec rec;
    unsigned n = 0, bad = 0;
    FILE *f;
    if (!path || !(f = fopen(path, "rb")))
        return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    s_prewarming = 1;
    while (fread(&rec, sizeof rec, 1, f) == 1) {
        Nv2aRawBatch bb;
        if (rec.magic != PROG_REC_MAGIC || rec.slots > 136) {
            bad++;
            break;
        }
        memset(&bb, 0, sizeof bb);
        bb.xform = rec.xform;
        bb.vp_program = (const uint32_t (*)[4])rec.prog;
        bb.vp_slots = rec.slots ? rec.slots : 136;
        bb.vp_start = rec.vp_start;
        bb.vp_prog_gen = 0x80000000u + n;    /* hash it: not an executor version */
        if (prog_get(&bb, &rec.pk))
            n++;
    }
    s_prewarming = 0;
    fclose(f);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "  [GL] %u shader programs precompiled from %s in %.0f ms%s\n", n, path,
            (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6,
            bad ? " (file ends in a bad record)" : "");
}

static GlProg *prog_get(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    static uint32_t last_gen;
    static uint64_t last_vkey;
    static GlProg *last;
    uint64_t vkey = 0;
    uint32_t bucket;
    GlProg *p = NULL, *victim = NULL;
    static char vbody[256 * 1024], fsrc[64 * 1024];
    const char *vs[4];
    int nvs = 0, i;
    GLuint v, fr;
    GLint ok = 0;

    if (b->xform == 2) {
        /* The program's hash walks up to 136 slots: redo it only when the
         * executor says the program changed. */
        if (b->vp_prog_gen != last_gen || !last_vkey) {
            last_vkey = prog_hash(b);
            last_gen = b->vp_prog_gen;
        }
        vkey = last_vkey;
    }
    /* Runs of draws share their program. */
    if (last && last->prog && last->vkey == vkey && !memcmp(&last->pkey, pk, sizeof *pk)) {
        last->used = s_frame;
        return last;
    }
    bucket = prog_bucket(vkey, pk);
    for (i = s_prog_head[bucket]; i; i = s_prog[i - 1].next) {
        GlProg *c = &s_prog[i - 1];
        if (c->prog && c->vkey == vkey && !memcmp(&c->pkey, pk, sizeof *pk)) {
            c->used = s_frame;
            last = c;
            return c;
        }
    }
    for (i = 0; i < GL_MAX_PROG; i++) {
        GlProg *c = &s_prog[i];
        if (!c->prog) { if (!victim || victim->prog) victim = c; }
        else if (!victim || (victim->prog && c->used < victim->used)) victim = c;
    }
    p = victim;
    last = NULL;
    if (p->prog)
        prog_unlink(p);
    if (p->prog) {
        /* Delete it, not just forget it: an evicted program left alive
         * keeps its code in the driver's GPU code heap for good, and on the
         * Switch's Mesa a full code heap is a hung GPU. */
        glUseProgram(0);
        s_cur_prog = 0;
        glDeleteProgram(p->prog);
        s_prog_live--;
    }
    memset(p, 0, sizeof *p);
    if ((++s_prog_made % 100) == 0)
        fprintf(stderr, "  [GL] %u shader programs compiled (%u live)\n",
                s_prog_made, s_prog_live + 1);

    vs[nvs++] = nv2a_gl_vsh_prelude();
    if (vkey) {
        if (nv2a_gl_vsh_program(b->vp_program, b->vp_slots, b->vp_start,
                                vbody, sizeof vbody) < 0)
            return NULL;
        vs[nvs++] = vbody;
        vs[nvs++] = nv2a_gl_vsh_main_program();
    } else {
        vs[nvs++] = nv2a_gl_vsh_fixed();
    }
    if (nv2a_gl_psh(pk, fsrc, sizeof fsrc) < 0)
        return NULL;
    v = compile(GL_VERTEX_SHADER, vs, nvs);
    {
        const char *fs[1] = { fsrc };
        fr = compile(GL_FRAGMENT_SHADER, fs, 1);
    }
    if (!v || !fr)
        return NULL;
    p->prog = glCreateProgram();
    if (!p->prog) gl_fail("program allocation failed");
    glAttachShader(p->prog, v);
    glAttachShader(p->prog, fr);
    glLinkProgram(p->prog);
    glDeleteShader(v);
    glDeleteShader(fr);
    glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p->prog, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] link failed: %s\n", log);
        glDeleteProgram(p->prog);
        p->prog = 0;
        return NULL;
    }
    s_prog_live++;
    s_new_obj = 1;
    prog_cache_append(b, pk);
    p->vkey = vkey;
    p->pkey = *pk;
    p->used = s_frame;
    p->hash = bucket;
    p->next = s_prog_head[bucket];
    s_prog_head[bucket] = (int)(p - s_prog) + 1;
    last = p;
    p->u_c = glGetUniformLocation(p->prog, "c");
    p->vpc_rows = p->u_c >= 0 && glGetUniformLocation(p->prog, "c[191]") == p->u_c + 191;
    p->u_surf = glGetUniformLocation(p->prog, "u_surf");
    p->u_aa = glGetUniformLocation(p->prog, "u_aa");
    p->u_m = glGetUniformLocation(p->prog, "u_m");
    p->u_vpoff = glGetUniformLocation(p->prog, "u_vpoff");
    p->u_xform = glGetUniformLocation(p->prog, "u_xform");
    p->u_wide = glGetUniformLocation(p->prog, "u_wide");
    p->u_tscale = glGetUniformLocation(p->prog, "u_tscale");
    p->u_c0 = glGetUniformLocation(p->prog, "u_c0");
    p->u_c1 = glGetUniformLocation(p->prog, "u_c1");
    p->u_fogcolor = glGetUniformLocation(p->prog, "u_fogcolor");
    p->u_fogp = glGetUniformLocation(p->prog, "u_fogp");
    p->u_fc0 = glGetUniformLocation(p->prog, "u_fc0");
    p->u_fc1 = glGetUniformLocation(p->prog, "u_fc1");
    p->u_afunc = glGetUniformLocation(p->prog, "u_alpha_func");
    p->u_aref = glGetUniformLocation(p->prog, "u_alpha_ref");
    glUseProgram(p->prog);
    s_cur_prog = p->prog;
    for (i = 0; i < 4; i++) {
        char n[4] = { 't', (char)('0' + i), 0, 0 };
        p->u_t[i] = glGetUniformLocation(p->prog, n);
        if (p->u_t[i] >= 0)
            glUniform1i(p->u_t[i], i);
    }
    if (s_trace) {
        static int shown;
        if (shown++ < 4)
            fprintf(stderr, "  [GL] program %016llX linked\n", (unsigned long long)vkey);
    }
    return p;
}

/* ── Context ──────────────────────────────────────────────────────── */

static int ready(void)
{
    if (s_state)
        return s_state > 0;
    s_state = -1;
    s_trace = getenv("RECOMP_GL_TRACE") != NULL;
    if (nv2a_gl_adopt_window((void **)&s_win, (void **)&s_ctx)) {
        /* The host already made the window and context (a loading screen)
         * and has let go of the context: take it on this thread. */
        if (SDL_GL_MakeCurrent(s_win, s_ctx) != 0) {
            fprintf(stderr, "  [GL] adopting the window: %s\n", SDL_GetError());
            return 0;
        }
    } else {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "  [GL] SDL video: %s\n", SDL_GetError());
            return 0;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        s_win = SDL_CreateWindow("NV2A", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!s_win) {
            fprintf(stderr, "  [GL] window: %s\n", SDL_GetError());
            return 0;
        }
        s_ctx = SDL_GL_CreateContext(s_win);
        if (!s_ctx) {
            fprintf(stderr, "  [GL] context: %s\n", SDL_GetError());
            return 0;
        }
    }
    SDL_GL_SetSwapInterval(0);
    if (nv2a_gl_load(SDL_GL_GetProcAddress) != 0)
        return 0;
    fprintf(stderr, "  [GL] %s / %s / %s\n", (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        GLint mt = 0, mr = 0;
        double k = e ? strtod(e, NULL) : 1.0;
        if (!(k > 0.0)) k = 1.0;                    /* unset, 0 or garbage */
        s_scale = k < 0.5 ? 0.5 : k > 4.0 ? 4.0 : k;
        glGetIntegerv(0x0D33, &mt);                 /* GL_MAX_TEXTURE_SIZE */
        glGetIntegerv(0x84E8, &mr);                 /* GL_MAX_RENDERBUFFER_SIZE */
        s_max_size = mt > 0 && mr > 0 ? (mt < mr ? mt : mr) : 4096;
        if (s_scale != 1.0)
            fprintf(stderr, "  [GL] rendering at %gx (RECOMP_GL_SCALE), surfaces up to %d\n",
                    s_scale, s_max_size);
    }
    {
        /* Which size presenting will use. SDL reports the window as created;
         * a host with a better answer (the Switch's EGL surface, which
         * follows the display) supplies it through nv2a_gl_screen_size. */
        int dw = 0, dh = 0, sw = 0, sh = 0;
        SDL_GL_GetDrawableSize(s_win, &dw, &dh);
        if (!nv2a_gl_screen_size(&sw, &sh)) { sw = dw; sh = dh; }
        fprintf(stderr, "  [GL] screen %dx%d, SDL drawable %dx%d\n", sw, sh, dw, dh);
    }
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glGenBuffers(1, &s_ibo);
    s_vring.buf = s_vbo;
    s_iring.buf = s_ibo;
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, s_vring.cap, NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, s_iring.cap, NULL, GL_STREAM_DRAW);
    s_state = 1;
    prog_cache_prewarm();
    return 1;
}

/* Called once, when the renderer first needs the display. A host that shows
 * something of its own while the title boots (the Switch's loading screen)
 * overrides this: it stops drawing, releases its GL context, and hands back
 * its SDL window and context (as void *) for the renderer to use. Returning
 * 0 lets the renderer create its own. */
__attribute__((weak)) int nv2a_gl_adopt_window(void **win, void **ctx)
{
    (void)win; (void)ctx;
    return 0;
}

/* Called on a present with nothing of the title's to show. Returns 1 if the
 * host drew something (its loading screen) into the default framebuffer. */
__attribute__((weak)) int nv2a_gl_draw_placeholder(void)
{
    return 0;
}

/* The on-screen size, if the host knows it better than SDL. */
__attribute__((weak)) int nv2a_gl_screen_size(int *w, int *h)
{
    (void)w; (void)h;
    return 0;
}

/* ── Render state ─────────────────────────────────────────────────── */

static float byte_f(uint32_t v, int shift) { return (float)((v >> shift) & 0xFF) / 255.0f; }

static void argb_vec4(uint32_t v, float out[4])
{
    out[0] = byte_f(v, 16); out[1] = byte_f(v, 8);
    out[2] = byte_f(v, 0);  out[3] = byte_f(v, 24);
}

static GLenum blend_eq(uint32_t v)
{
    switch (v) {
    case 0x800A: case 0x800B: case 0x8007: case 0x8008: return v;
    default: return GL_FUNC_ADD;             /* also the signed variants */
    }
}

/* The render state as apply_state sets it. Consecutive draws mostly share
 * it, and re-setting ~20 pieces of GL state per draw -- about 900 draws a
 * frame in a race -- is driver work for nothing. Anything else that touches
 * this state (clears, presents, new surfaces) calls state_dirty(). */
typedef struct {
    uint32_t blend, bsrc, bdst, beq, bcolor;
    uint32_t depth, dfunc, dmask;
    uint32_t stencil, smask, sfunc, sref, sread, sop[3];
    uint32_t cull, cullface, front, cm;
    GLint    sc[4];                     /* scissor, stored pixels; sc[2] 0 = off */
} GlState;
static GlState s_st;
static int s_st_valid;

static void state_dirty(void) { s_st_valid = 0; tex_bind_forget(); }

/* SET_SURFACE_CLIP as a scissor in s's stored pixels (sc[2] = 0: the clip
 * covers the surface). NFSU2's split screen draws each player's world with
 * the clip set to that player's half; the vertex programs place it there
 * but do not stop at the edge, so without the scissor each view spilled
 * over the other. xemu scissors the same way. */
static void surface_scissor(const uint32_t *r, const GlSurf *s, GLint sc[4])
{
    uint32_t x0 = (r[0x200 / 4] & 0xFFFF) * s->aa_sx, x1 = x0 + (r[0x200 / 4] >> 16) * s->aa_sx;
    uint32_t y0 = (r[0x204 / 4] & 0xFFFF) * s->aa_sy, y1 = y0 + (r[0x204 / 4] >> 16) * s->aa_sy;

    sc[0] = sc[1] = sc[2] = sc[3] = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0 || (x0 == 0 && y0 == 0 && x1 == s->w && y1 == s->h))
        return;
    sc[0] = to_stored(x0, s->pw, s->w);
    sc[1] = to_stored(y0, s->ph, s->h);
    sc[2] = to_stored(x1, s->pw, s->w) - sc[0];
    sc[3] = to_stored(y1, s->ph, s->h) - sc[1];
}

static void apply_state(const uint32_t *r, int has_depth, const GlSurf *s)
{
    uint32_t cm = r[0x358 / 4];
    GlState want;

    memset(&want, 0, sizeof want);
    surface_scissor(r, s, want.sc);
    if (r[0x304 / 4]) {
        want.blend = 1; want.bsrc = r[0x344 / 4]; want.bdst = r[0x348 / 4];
        want.beq = r[0x350 / 4]; want.bcolor = r[0x34C / 4];
    }
    if (has_depth && r[0x30C / 4]) {
        want.depth = 1; want.dfunc = r[0x354 / 4];
    }
    want.dmask = has_depth && r[0x35C / 4];
    if (has_depth && r[0x32C / 4]) {
        want.stencil = 1; want.smask = r[0x360 / 4]; want.sfunc = r[0x364 / 4];
        want.sref = r[0x368 / 4]; want.sread = r[0x36C / 4];
        want.sop[0] = r[0x370 / 4]; want.sop[1] = r[0x374 / 4]; want.sop[2] = r[0x378 / 4];
    }
    {
        /* RECOMP_GL_NO_CULL=1: draw both sides, as this runtime's CPU renderer does. */
        static int no_cull = -1;
        if (no_cull < 0) {
            const char *e = getenv("RECOMP_GL_NO_CULL");
            no_cull = e && *e == '1';
            fprintf(stderr, "[MH-GL] culling: %s\n", no_cull ? "off (RECOMP_GL_NO_CULL=1)" : "as the title sets it");
        }
        if (r[0x308 / 4] && !no_cull) {
            want.cull = 1; want.cullface = r[0x39C / 4]; want.front = r[0x3A0 / 4];
        }
    }
    want.cm = cm;
    if (s_st_valid && !memcmp(&want, &s_st, sizeof want))
        return;
    s_st = want;
    s_st_valid = 1;

    if (r[0x304 / 4]) {
        float bc[4];
        glEnable(GL_BLEND);
        glBlendFunc(r[0x344 / 4], r[0x348 / 4]);
        glBlendEquation(blend_eq(r[0x350 / 4]));
        argb_vec4(r[0x34C / 4], bc);
        glBlendColor(bc[0], bc[1], bc[2], bc[3]);
    } else {
        glDisable(GL_BLEND);
    }
    if (has_depth && r[0x30C / 4]) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(r[0x354 / 4] ? r[0x354 / 4] : 0x203);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(has_depth && r[0x35C / 4] ? 1 : 0);
    if (has_depth && r[0x32C / 4]) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(r[0x360 / 4] & 0xFF);
        glStencilFunc(r[0x364 / 4] ? r[0x364 / 4] : GL_ALWAYS, (GLint)(r[0x368 / 4] & 0xFF),
                      r[0x36C / 4] & 0xFF);
        glStencilOp(r[0x370 / 4] ? r[0x370 / 4] : GL_KEEP,
                    r[0x374 / 4] ? r[0x374 / 4] : GL_KEEP,
                    r[0x378 / 4] ? r[0x378 / 4] : GL_KEEP);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    if (r[0x308 / 4]) {
        glEnable(GL_CULL_FACE);
        glCullFace(r[0x39C / 4] ? r[0x39C / 4] : GL_BACK);
        /* Rows are top-first here, which mirrors the winding. */
        glFrontFace(r[0x3A0 / 4] == GL_CCW ? GL_CW : GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glColorMask((cm & 0x00010000u) != 0, (cm & 0x00000100u) != 0,
                (cm & 0x00000001u) != 0, (cm & 0x01000000u) != 0);
    if (want.sc[2] > 0) {               /* rows are top-first, as in clears */
        glEnable(GL_SCISSOR_TEST);
        glScissor(want.sc[0], want.sc[1], want.sc[2], want.sc[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    /* The NV2A does not clip against the near and far planes; it clamps
     * depth. GL clips there, so a screen quad placed at the far plane
     * (NFSU2's loading screen, z = 1.0 through its vertex program) vanished
     * whenever rounding put it a hair beyond. */
    glEnable(0x864F);                               /* GL_DEPTH_CLAMP */
}

static void psh_key(const uint32_t *r, Nv2aPshKey *k)
{
    int i;

    memset(k, 0, sizeof *k);
    for (i = 0; i < 8; i++) {
        k->color_icw[i] = r[0xAC0 / 4 + i];
        k->color_ocw[i] = r[0x1E40 / 4 + i];
        k->alpha_icw[i] = r[0x260 / 4 + i];
        k->alpha_ocw[i] = r[0xAA0 / 4 + i];
    }
    k->control = r[0x1E60 / 4];
    k->final0 = r[0x288 / 4];
    k->final1 = r[0x28C / 4];
    k->shader_program = r[0x1E70 / 4];
    k->other_input = r[0x1E78 / 4];
}

/* NV097 primitive -> GL, rewriting the ones core GL lacks. */
static uint32_t *s_prim_idx;
static size_t    s_prim_cap;

static GLenum gl_prim(const Nv2aRawBatch *b, const uint32_t **idx, uint32_t *n)
{
    uint32_t i, m = 0;

    *idx = b->indices;
    *n = b->index_count;
    switch (b->prim) {
    case 1: return 0x0000;             /* POINTS */
    case 2: return 0x0001;             /* LINES */
    case 3: return 0x0002;             /* LINE_LOOP */
    case 4: return 0x0003;             /* LINE_STRIP */
    case 5: return GL_TRIANGLES;
    case 6: return 0x0005;             /* TRIANGLE_STRIP */
    case 7: case 10: return 0x0006;    /* TRIANGLE_FAN, POLYGON */
    case 9: return 0x0005;             /* QUAD_STRIP == triangle strip */
    case 8: {                          /* QUADS -> triangles */
        size_t need = (size_t)b->index_count / 4 * 6;
        if (need > s_prim_cap) {
            free(s_prim_idx);
            s_prim_cap = need + 1024;
            s_prim_idx = (uint32_t *)malloc(s_prim_cap * 4);
        }
        if (!s_prim_idx) gl_fail("primitive index allocation failed");
        for (i = 0; i + 3 < b->index_count; i += 4) {
            const uint32_t *q = b->indices + i;
            s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[1]; s_prim_idx[m++] = q[2];
            s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[2]; s_prim_idx[m++] = q[3];
        }
        *idx = s_prim_idx;
        *n = m;
        return GL_TRIANGLES;
    }
    default:
        if (gl_gpu()) { *n = 0; return GL_TRIANGLES; }
        gl_fail("unsupported raw primitive");
    }
}

/* ── Back end entry points ────────────────────────────────────────── */

static uint32_t zmax_of(const uint32_t *r)
{
    return (((r[0x208 / 4] >> 4) & 0xF) == 1) ? 0xFFFFu : 0xFFFFFFu;
}

/* Streaming vertex and index data.
 *
 * glBufferData per draw re-specifies the buffer every time -- a driver
 * allocation, twice per draw, ~900 draws a frame. Instead each buffer is one
 * large allocation filled front to back: a draw maps its own range without
 * synchronisation (nothing the GPU may still read is ever overwritten) and
 * the buffer is orphaned only when it is full. */

/* Space for `size` bytes; returns the offset, and a pointer to write them to
 * (NULL if mapping failed: then ring_put the data instead). */
static GLintptr ring_alloc(GlRing *g, GLsizeiptr size, void **ptr)
{
    GLintptr at;

    glBindBuffer(g->target, g->buf);
    if (size > g->cap) {
        g->cap = size * 2;
        g->off = g->cap;                    /* forces the orphan below */
    }
    at = (g->off + 63) & ~(GLintptr)63;
    if (at + size > g->cap) {
        glBufferData(g->target, g->cap, NULL, GL_STREAM_DRAW);
        at = 0;
    }
    g->off = at + size;
    *ptr = glMapBufferRange(g->target, at, size,
                            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
                            );
    /* No GL_MAP_INVALIDATE_RANGE_BIT: nothing the GPU may still read is
     * ever overwritten, and with it nouveau gave each map a staging buffer
     * of its own (nouveau_bo_new + map + free: ~3% of the GL thread). */
    return at;
}

static void ring_done(GlRing *g, GLintptr at, GLsizeiptr size, void *ptr,
                      const void *fallback)
{
    if (ptr) {
        if (!glUnmapBuffer(g->target) && !gl_gpu()) gl_fail("vertex/index buffer data invalidated");
    }
    else if (fallback)
        glBufferSubData(g->target, at, size, fallback);
    else if (!gl_gpu())
        gl_fail("missing vertex/index upload data");
    gl_check("vertex/index buffer upload");
}

/* Which vertex attribute arrays are enabled in s_vao (only this file
 * touches it): enable and disable only on change. */
static uint32_t s_attr_on;
static void attr_enable(uint32_t a, int on)
{
    uint32_t bit = 1u << a;
    if (!!(s_attr_on & bit) == !!on)
        return;
    if (on) { glEnableVertexAttribArray(a); s_attr_on |= bit; }
    else    { glDisableVertexAttribArray(a); s_attr_on &= ~bit; }
}

/* Vertex data for one draw, in one ring allocation.
 *
 * Attributes in b->attr_direct go up as the title stored them: every NV097
 * type but the packed CMP normal is a GL vertex format with the same
 * meaning (D3DCOLOR is GL_BGRA; S1 normalises with GL's max(c/32767, -1)).
 * Attributes that share a vertex buffer -- interleaved, the usual case --
 * are copied once, as the byte range they span together, from a 4-aligned
 * start so each keeps its alignment. The rest (CMP, and every attribute for
 * a back end without direct batches) are packed float4 from b->attrs. */
/* The byte ranges the direct attributes of `b` span, merged where they
 * overlap (interleaved attributes share one), each from a 4-aligned start;
 * of_run[a] says which range attribute a is in. Returns the range count. */
typedef struct { const uint8_t *lo, *hi; GLintptr at; } VRun;
static uint32_t vertex_runs(const Nv2aRawBatch *b, VRun *run, int *of_run)
{
    uint32_t nv = b->vertex_count, nrun = 0, a, i;

    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        const uint8_t *lo, *hi;
        uint32_t elem;
        if (!(b->attr_direct & (1u << a)))
            continue;
        switch (b->direct[a].type) {
        case 0: elem = 4; break;
        case 2: elem = 4 * b->direct[a].size; break;
        case 4: elem = b->direct[a].size; break;
        default: elem = 2 * b->direct[a].size; break;
        }
        lo = (const uint8_t *)((uintptr_t)b->direct[a].ptr & ~(uintptr_t)3);
        hi = b->direct[a].ptr + (size_t)(nv - 1) * b->direct[a].stride + elem;
        for (i = 0; i < nrun; i++)
            if (lo <= run[i].hi && run[i].lo <= hi)
                break;
        if (i == nrun) {
            run[nrun].lo = lo;
            run[nrun].hi = hi;
            nrun++;
        } else {
            if (lo < run[i].lo) run[i].lo = lo;
            if (hi > run[i].hi) run[i].hi = hi;
        }
        of_run[a] = (int)i;
    }
    /* A widened run can now overlap another: merge until none do. */
    for (i = 0; i + 1 < nrun; ) {
        uint32_t j;
        for (j = i + 1; j < nrun; j++)
            if (run[j].lo <= run[i].hi && run[i].lo <= run[j].hi)
                break;
        if (j == nrun) { i++; continue; }
        /* Fold run j into run i, then move the last run into slot j. */
        if (run[j].lo < run[i].lo) run[i].lo = run[j].lo;
        if (run[j].hi > run[i].hi) run[i].hi = run[j].hi;
        for (a = 0; a < NV2A_RAW_ATTRS; a++) {
            if (!(b->attr_direct & (1u << a)))
                continue;
            if (of_run[a] == (int)j)
                of_run[a] = (int)i;
            else if (of_run[a] == (int)(nrun - 1))
                of_run[a] = (int)j;
        }
        run[j] = run[nrun - 1];
        nrun--;
    }
    return nrun;
}

static int upload_vertices(const Nv2aRawBatch *b)
{
    VRun run[NV2A_RAW_ATTRS];
    int of_run[NV2A_RAW_ATTRS];
    uint32_t slot[NV2A_RAW_ATTRS], np = 0, nrun = 0, a, i, v;
    uint32_t nv = b->vertex_count;
    uint16_t conv = b->attr_present & (uint16_t)~b->attr_direct;
    GLsizeiptr bytes = 0, pack_at;
    GLintptr at;
    uint8_t *dst;
    void *map;
    static uint8_t *staging;
    static size_t staging_cap;

    if (!nv)
        return 0;
    nrun = vertex_runs(b, run, of_run);
    for (i = 0; i < nrun; i++) {
        run[i].at = bytes;
        bytes += ((GLsizeiptr)(run[i].hi - run[i].lo) + 15) & ~(GLsizeiptr)15;
    }
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if (conv & (1u << a))
            slot[np++] = a;
    pack_at = bytes;
    bytes += (GLsizeiptr)nv * np * 4 * sizeof(float);
    if (!bytes)
        return 0;

    at = ring_alloc(&s_vring, bytes, &map);
    dst = (uint8_t *)map;
    if (!dst) {
        if ((size_t)bytes > staging_cap) {
            free(staging);
            staging_cap = (size_t)bytes + 65536;
            staging = (uint8_t *)malloc(staging_cap);
            if (!staging) { staging_cap = 0; return 0; }
        }
        dst = staging;
    }
    for (i = 0; i < nrun; i++)
        memcpy(dst + run[i].at, run[i].lo, (size_t)(run[i].hi - run[i].lo));
    if (np) {
        float *out = (float *)(dst + pack_at);
        for (v = 0; v < nv; v++)
            for (i = 0; i < np; i++)
                memcpy(out + ((size_t)v * np + i) * 4,
                       b->attrs + ((size_t)v * NV2A_RAW_ATTRS + slot[i]) * 4,
                       4 * sizeof(float));
    }
    ring_done(&s_vring, at, bytes, map, staging);

    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        GLintptr off;
        GLint size;
        GLenum type;
        GLboolean norm;
        if (!(b->attr_present & (1u << a))) {
            attr_enable(a, 0);
            continue;
        }
        attr_enable(a, 1);
        if (!(b->attr_direct & (1u << a)))
            continue;
        off = at + run[of_run[a]].at + (b->direct[a].ptr - run[of_run[a]].lo);
        size = (GLint)b->direct[a].size;
        switch (b->direct[a].type) {
        case 0:  size = GL_BGRA; type = GL_UNSIGNED_BYTE; norm = GL_TRUE; break;
        case 1:  type = GL_SHORT; norm = GL_TRUE; break;
        case 2:  type = GL_FLOAT; norm = GL_FALSE; break;
        case 4:  type = GL_UNSIGNED_BYTE; norm = GL_TRUE; break;
        default: type = GL_SHORT; norm = GL_FALSE; break;
        }
        glVertexAttribPointer(a, size, type, norm, (GLsizei)b->direct[a].stride,
                              (const void *)(uintptr_t)off);
    }
    for (i = 0; i < np; i++)
        glVertexAttribPointer(slot[i], 4, GL_FLOAT, GL_FALSE,
                              (GLsizei)(np * 4 * sizeof(float)),
                              (const void *)(uintptr_t)(at + pack_at + i * 4 * sizeof(float)));
    return 1;
}

/* ── Visibility tests ─────────────────────────────────────────────────
 *
 * As in nv2a_vk.c: every draw made with SET_ZPASS_PIXEL_COUNT_ENABLE on
 * runs in a GL_SAMPLES_PASSED query, GET_REPORT queues a report summing the
 * queries since CLEAR_REPORT_VALUE, and each flip writes the reports whose
 * queries have finished, oldest first (Burnout 3's sun, issue #10). All on
 * the thread that owns the context. Queries are a ring, counted ever. */
#define GL_VIS_QUERIES 512
#define GL_VIS_REPORTS 64
static GLuint   s_vis_q[GL_VIS_QUERIES];
static float    s_vis_scale[GL_VIS_QUERIES];
static uint32_t s_vis_head;             /* queries begun */
static uint32_t s_vis_q0;               /* first query of the count in progress */
static struct { uint32_t va, q0, q1; } s_vis_rep[GL_VIS_REPORTS];
static uint32_t s_vis_rhead, s_vis_rtail;

/* Room for another query: none of the ring is still wanted. */
static int vis_room(void)
{
    uint32_t oldest = s_vis_rhead != s_vis_rtail
                    ? s_vis_rep[s_vis_rtail % GL_VIS_REPORTS].q0 : s_vis_q0;
    if (!s_vis_q[0])
        glGenQueries(GL_VIS_QUERIES, s_vis_q);
    return s_vis_head - oldest < GL_VIS_QUERIES;
}

static void vis_poll(void)
{
    while (s_vis_rhead != s_vis_rtail) {
        uint32_t k = s_vis_rtail % GL_VIS_REPORTS, q;
        GLuint done = 1, n;
        double sum = 0;
        if (s_vis_rep[k].q1 != s_vis_rep[k].q0)
            glGetQueryObjectuiv(s_vis_q[(s_vis_rep[k].q1 - 1) % GL_VIS_QUERIES],
                                GL_QUERY_RESULT_AVAILABLE, &done);
        if (!done)
            break;                              /* they finish in order */
        for (q = s_vis_rep[k].q0; q != s_vis_rep[k].q1; q++) {
            glGetQueryObjectuiv(s_vis_q[q % GL_VIS_QUERIES], GL_QUERY_RESULT, &n);
            sum += (double)n * s_vis_scale[q % GL_VIS_QUERIES];
        }
        nv2a_pb_write_report(s_vis_rep[k].va, (uint32_t)(sum + 0.5));
        s_vis_rtail++;
    }
}

static void gl_report(uint32_t op, uint32_t va)
{
    gl_sync(0);
    if (op == NV2A_REPORT_CLEAR) {
        s_vis_q0 = s_vis_head;
    } else if (s_vis_rhead - s_vis_rtail < GL_VIS_REPORTS) {
        uint32_t k = s_vis_rhead++ % GL_VIS_REPORTS;
        s_vis_rep[k].va = va;
        s_vis_rep[k].q0 = s_vis_q0;
        s_vis_rep[k].q1 = s_vis_head;
    } else if (!gl_gpu()) {           /* gpu: it stays incomplete */
        gl_fail("visibility report queue full");
    }
    vis_poll();
}

/* MANHUNT_WIDESCREEN (widescreen.c): a pre-transformed draw on the 640x480
 * target is 2D -- HUD, menus, text -- and is squeezed to the centre 4:3 so
 * it keeps its shape on the 16:9 screen. One that spans the full width
 * (a fade, an overlay) stays full width, unless it uses the explicit movie
 * shader recorded in the existing title-specific allowlist, which then
 * shows at 4:3 between bars. Positions are attribute 0, in real pixels. */
int xbox_video_widescreen(void);

/* Horizontal extent of a draw in real pixels, from attribute 0 as the
 * draw's transform path places it (xform 0 and 2: already screen x;
 * xform 1: composite, divide, viewport offset). 0 if it cannot be read. */
static int wide_xrange(const Nv2aRawBatch *b, float *mn, float *mx)
{
    float minx = 1e30f, maxx = -1e30f;
    uint32_t v;

    if (!b->vertex_count)
        return 0;
    for (v = 0; v < b->vertex_count; v++) {
        float x, q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (b->attr_direct & 1u) {
            uint32_t n = b->direct[0].size < 4 ? b->direct[0].size : 4;
            if (b->direct[0].type != 2)
                return 0;
            memcpy(q, b->direct[0].ptr + (size_t)v * b->direct[0].stride, n * sizeof(float));
        } else {
            memcpy(q, b->attrs + (size_t)v * NV2A_RAW_ATTRS * 4, sizeof q);
        }
        x = q[0];
        if (b->xform == 1) {
            const float *m = b->composite;
            float cw = m[12] * q[0] + m[13] * q[1] + m[14] * q[2] + m[15] * q[3];
            if (cw == 0.0f)
                return 0;
            x = ((m[0] * q[0] + m[1] * q[1] + m[2] * q[2] + m[3] * q[3]) / cw + b->vp_offset[0])
                * (b->aa_sx > 0 ? b->aa_sx : 1.0f);
        }
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
    }
    *mn = minx;
    *mx = maxx;
    return 1;
}

static int wide_target(const Nv2aRawBatch *b, const GlSurf *s)
{
    return (float)s->w / (b->aa_sx > 0 ? b->aa_sx : 1.0f) == 640.0f &&
           (float)s->h / (b->aa_sy > 0 ? b->aa_sy : 1.0f) == 480.0f;
}

static void wide_2d(const Nv2aRawBatch *b, const GlSurf *s, uint64_t vkey, float out[2])
{
    float minx, maxx, w = (float)s->w;

    out[0] = out[1] = 0.0f;
    if (!xbox_video_widescreen() || !wide_target(b, s) || !wide_xrange(b, &minx, &maxx))
        return;
    if (minx <= 2.0f && maxx >= w - 2.0f && vkey != 0xBA9E387C24FCECE1ull)
        return;
    out[0] = 0.75f;
    out[1] = w * 0.5f;
}

/* Xbox D3D draws pre-transformed (screen-space) vertices through a small
 * vertex program, not the pass-through path (run 32: the movie quad came as
 * xform 2). Classification uses only the immutable program allowlist below;
 * sampling an address never teaches or changes a program's classification.
 * Attribute 0 is then still a
 * screen position, which wide_2d reads for its full-width test. */
#define WIDE_PROGS 8
/* The title's screen-space programs, by hash of their microcode (stable
 * across runs: the movie's was the same in runs 33 and 37). Run 37: the menu
 * draws 2D through 43720A56... and AA8C1020..., the movie through
 * BA9E387C..., and a full-width overlay through D7829B66... . Hypothesis
 * until the in-level capture confirms the HUD uses them and the world does
 * not. */
static const uint64_t s_wide_prog[WIDE_PROGS] = {
    0x43720A569E4D3893ull, 0xAA8C102096129062ull,
    0xBA9E387C24FCECE1ull, 0xD7829B66D519BE47ull,
};
static const unsigned s_wide_nprog = 4;
static uint32_t s_wide_frame_draws, s_wide_dumps, s_wide_dump_at, s_wide_dump_ff;
static int s_wide_dumping;
static int wide_prog_2d(uint64_t vkey)
{
    unsigned i;

    if (!xbox_video_widescreen() || !vkey)
        return 0;
    for (i = 0; i < s_wide_nprog; i++)
        if (s_wide_prog[i] == vkey)
            return 1;
    return 0;
}

/* MANHUNT_PAGE_CAPTURE=1: every draw of each new low-draw page (a menu, the
 * settings page), to see how a page is drawn. A page is told apart by a
 * signature of its programs and textures. Bounded. Off by default. */
#define PAGE_MAX_DRAWS 200
#define PAGE_MAX_DUMPS 24
static int s_page_on = -1, s_page_dumping;
volatile int nv2a_gl_page_request;    /* ZL+ZR, set in xpp_input.c: every draw of the next frame */
static uint32_t s_page_draws, s_page_sig, s_page_dumps;
static uint32_t s_page_seen[PAGE_MAX_DUMPS];

/* What a stage-0 image's own bytes say about transparency. */
static void page_alpha(const Nv2aRawBatch *b, uint32_t color, uint32_t w, uint32_t h,
                       char *out, size_t n)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t va = b->tex_va[0], i, lo = 255, hi = 0;

    snprintf(out, n, "-");
    if (!va)
        return;
    if (color == 0x0B && b->pal_va[0]) {
        const uint8_t *pal = mem + b->pal_va[0];
        for (i = 0; i < 256; i++) {
            if (pal[i * 4 + 3] < lo) lo = pal[i * 4 + 3];
            if (pal[i * 4 + 3] > hi) hi = pal[i * 4 + 3];
        }
        snprintf(out, n, "pal-alpha %u..%u", lo, hi);
    } else if (color == 0x06 && w && h && w <= 2048 && h <= 2048) {
        for (i = 0; i < w * h; i++) {
            uint8_t a = mem[va + i * 4 + 3];
            if (a < lo) lo = a;
            if (a > hi) hi = a;
        }
        snprintf(out, n, "tex-alpha %u..%u", lo, hi);
    } else if (color == 0x0C && w && h && w <= 2048 && h <= 2048) {
        uint32_t blocks = ((w + 3) / 4) * ((h + 3) / 4), keyed = 0;
        for (i = 0; i < blocks; i++) {
            uint16_t c0, c1;
            memcpy(&c0, mem + va + i * 8, 2);
            memcpy(&c1, mem + va + i * 8 + 2, 2);
            if (c0 <= c1) keyed++;
        }
        snprintf(out, n, "dxt1 keyed blocks %u of %u", keyed, blocks);
    }
}

static void page_note(const Nv2aRawBatch *b, const GlSurf *s, const GlProg *p, const Nv2aPshKey *pk)
{
    const uint32_t *r = b->regs;
    uint32_t color = (r[0x1B04 / 4] >> 8) & 0xFF;
    uint32_t w = 1u << ((r[0x1B04 / 4] >> 20) & 15), h = 1u << ((r[0x1B04 / 4] >> 24) & 15);
    float mn = -1.0f, mx = -1.0f;
    char alpha[48];

    if (s_page_on < 0) {
        const char *e = getenv("MANHUNT_PAGE_CAPTURE");
        s_page_on = e && *e == '1';
    }
    if (!s_page_on)
        return;
    if (s_page_dumping == 2) {          /* on request: every draw on every target */
        if (!wide_xrange(b, &mn, &mx))
            mn = mx = -1.0f;
        page_alpha(b, color, w, h, alpha, sizeof alpha);
        fprintf(stderr, "[MH-REQ] surf %ux%u xf %u prog %016llX prim %u n %u tex 0x%08X fmt %02X %ux%u ctl 0x%08X tex1 0x%08X"
                " x %.1f..%.1f atest %u ref %u blend %u %X %X sp %08X cc %08X rgb0 %08X/%08X a0 %08X/%08X final %08X/%08X %s\n",
                s->w, s->h, b->xform, (unsigned long long)p->vkey, b->prim, b->vertex_count, b->tex_va[0], color, w, h,
                r[0x1B0C / 4], b->tex_va[1], mn, mx, r[0x300 / 4], r[0x340 / 4] & 0xFF, r[0x304 / 4], r[0x344 / 4],
                r[0x348 / 4], pk->shader_program, pk->control, pk->color_icw[0], pk->color_ocw[0],
                pk->alpha_icw[0], pk->alpha_ocw[0], pk->final0, pk->final1, alpha);
        return;
    }
    if ((float)s->w / (b->aa_sx > 0 ? b->aa_sx : 1.0f) != 640.0f)
        return;
    /* Every texture a 2D draw uses, once. A page with many draws behind it
     * (the settings page over a paused level) never counts as low-draw. */
    if (b->tex_va[0] && (b->xform == 0 || (b->xform == 2 && wide_prog_2d(p->vkey)))) {
        static uint32_t seen[512];
        static unsigned nseen;
        unsigned i;
        for (i = 0; i < nseen && seen[i] != b->tex_va[0]; i++) { }
        if (i == nseen && nseen < 512) {
            seen[nseen++] = b->tex_va[0];
            if (!wide_xrange(b, &mn, &mx))
                mn = mx = -1.0f;
            page_alpha(b, color, w, h, alpha, sizeof alpha);
            fprintf(stderr, "[MH-2D] frame %u tex 0x%08X fmt %02X %ux%u xf %u prog %016llX prim %u n %u x %.1f..%.1f"
                    " ctl 0x%08X atest %u ref %u blend %u %X %X sp %08X %s\n",
                    s_frame, b->tex_va[0], color, w, h, b->xform, (unsigned long long)p->vkey, b->prim,
                    b->vertex_count, mn, mx, r[0x1B0C / 4], r[0x300 / 4], r[0x340 / 4] & 0xFF,
                    r[0x304 / 4], r[0x344 / 4], r[0x348 / 4], pk->shader_program, alpha);
        }
    }
    s_page_draws++;
    s_page_sig = (s_page_sig + ((uint32_t)p->vkey ^ (b->tex_va[0] * 16777619u) ^ color)) * 31u;
    if (!s_page_dumping)
        return;
    if (!wide_xrange(b, &mn, &mx))
        mn = mx = -1.0f;
    page_alpha(b, color, w, h, alpha, sizeof alpha);
    fprintf(stderr, "[MH-PAGE] xf %u prog %016llX prim %u n %u tex 0x%08X fmt %02X %ux%u ctl 0x%08X pal 0x%08X"
            " x %.1f..%.1f atest %u %X ref %u blend %u %X %X cc %08X sp %08X %s\n",
            b->xform, (unsigned long long)p->vkey, b->prim, b->vertex_count, b->tex_va[0], color, w, h,
            r[0x1B0C / 4], b->pal_va[0], mn, mx, r[0x300 / 4], r[0x33C / 4], r[0x340 / 4] & 0xFF,
            r[0x304 / 4], r[0x344 / 4], r[0x348 / 4], pk->control, pk->shader_program, alpha);
}

static void page_flip(void)
{
    uint32_t sig = s_page_sig, n = s_page_draws, i;

    if (s_page_dumping == 1)
        fprintf(stderr, "[MH-PAGE] end frame %u: %u draws\n", s_frame, n);
    if (s_page_dumping == 2)
        fprintf(stderr, "[MH-REQ] end frame %u\n", s_frame);
    s_page_dumping = 0;
    s_page_sig = s_page_draws = 0;
    if (nv2a_gl_page_request) {
        nv2a_gl_page_request = 0;
        s_page_dumping = 2;
        fprintf(stderr, "[MH-REQ] capture frame %u\n", s_frame + 1);
        return;
    }
    if (n < 4 || n > PAGE_MAX_DRAWS || s_page_dumps >= PAGE_MAX_DUMPS)
        return;
    for (i = 0; i < s_page_dumps; i++)
        if (s_page_seen[i] == sig)
            return;
    s_page_seen[s_page_dumps++] = sig;
    s_page_dumping = 1;
    fprintf(stderr, "[MH-PAGE] capture %u: frame %u, %u draws\n", s_page_dumps, s_frame + 1, n);
}

static void gl_draw_raw(const Nv2aRawBatch *b)
{
    GlSurf *s;
    GlProg *p;
    Nv2aPshKey pk;
    const uint32_t *r = b->regs;
    const uint32_t *idx;
    uint32_t n, a, i;
    GLintptr idx_at;
    GLenum mode, idx_type = GL_UNSIGNED_INT;
    float tscale[4][2], c0[8][4], c1[8][4], fog[4], surf[4], aa[2], m[16];

    if (!ready()) {
        if (gl_gpu()) return;
        gl_fail("GL context or entry-point initialization failed");
    }
    s_regs = r;
    s_batch = b;
    s = surf_bind(&b->surface, b->zeta_va);
    if (!s) {
        if (gl_gpu()) return;
        gl_fail("surface binding failed");
    }
    s_drew_any = 1;
    psh_key(r, &pk);
    if (!gl_gpu() && !nv2a_gl_psh_supported(&pk)) gl_fail("unsupported texture shader mode");
    if (s_trace) {
        /* Texture modes not yet done in gl_psh.c: bump, dot product, cube. */
        static uint32_t seen[32];
        static int nseen;
        uint32_t sp = pk.shader_program, t, odd = 0;
        int k2;
        for (t = 0; t < 4; t++) {
            uint32_t m = (sp >> (5 * t)) & 0x1F;
            if (m == 3 || (m >= 6 && m <= 14) || m == 17)
                odd = 1;
        }
        for (k2 = 0; k2 < nseen && seen[k2] != sp; k2++) { }
        if (odd && k2 == nseen && nseen < 32) {
            seen[nseen++] = sp;
            fprintf(stderr, "  [GL] texture modes 0x%05X (stages %u %u %u %u) prim %u n %u"
                    " fmt %02X %02X %02X %02X bumpmat1 %08X %08X %08X %08X\n",
                    sp, sp & 0x1F, (sp >> 5) & 0x1F, (sp >> 10) & 0x1F, (sp >> 15) & 0x1F,
                    b->prim, b->index_count,
                    (r[0x1B04 / 4] >> 8) & 0xFF, (r[0x1B44 / 4] >> 8) & 0xFF,
                    (r[0x1B84 / 4] >> 8) & 0xFF, (r[0x1BC4 / 4] >> 8) & 0xFF,
                    r[0x1B68 / 4], r[0x1B6C / 4], r[0x1B70 / 4], r[0x1B74 / 4]);
        }
    }
    {
        uint32_t made = s_prog_made;
        uint64_t t0 = hz_now();
        p = prog_get(b, &pk);
        if (s_prog_made != made) {
            s_hz_progs += s_prog_made - made;
            s_hz_prog_ns += hz_now() - t0;
        }
    }
    if (!p) {
        if (gl_gpu()) return;
        gl_fail("shader compilation/link failed");
    }
    s_hz_draws++;
    page_note(b, s, p, &pk);
    if (p->prog != s_cur_prog) {
        glUseProgram(p->prog);
        s_cur_prog = p->prog;
    }

    {
        uint32_t texs = s_hz_texs;
        uint64_t t0 = hz_now();
        for (i = 0; i < 4; i++)
            bind_stage(r, (int)i, tscale[i]);
        if (s_hz_texs != texs)
            s_hz_tex_ns += hz_now() - t0;
    }
    for (i = 0; i < 8; i++) {
        argb_vec4(r[0xA60 / 4 + i], c0[i]);
        argb_vec4(r[0xA80 / 4 + i], c1[i]);
    }
    {
        uint32_t fc = r[0x2A8 / 4];            /* R in the low byte */
        fog[0] = byte_f(fc, 0); fog[1] = byte_f(fc, 8);
        fog[2] = byte_f(fc, 16); fog[3] = byte_f(fc, 24);
    }
    surf[0] = 2.0f / (float)s->w;
    surf[1] = 2.0f / (float)s->h;
    surf[2] = 1.0f / (float)zmax_of(r);
    surf[3] = 0.5f - 0.5f * (float)s->w / (float)s->pw;   /* see nv2a_snap */
    aa[0] = b->aa_sx > 0 ? b->aa_sx : 1.0f;
    aa[1] = b->aa_sy > 0 ? b->aa_sy : 1.0f;
    memcpy(m, b->composite, sizeof m);

    /* 192 constants are 3 KB, and most draws in a row share them: upload
     * only when they changed since this program last got them. */
    if (p->u_c >= 0 && !(p->vpc_valid && p->vpc_gen == b->vp_const_gen)) {
        /* Same executor version: same contents, no compare needed. */
        if (!p->vpc_valid || !p->vpc_rows) {
            memcpy(p->vpc, b->vp_consts, sizeof p->vpc);
            glUniform4fv(p->u_c, 192, &b->vp_consts[0][0]);
        } else {
            /* Only the rows that changed, as runs: a draw usually changes a
             * matrix or two of the 192, and sending all 3 KB each time was
             * copied again by Mesa and by nouveau's constbuf upload. */
            int r = 0;
            while (r < 192) {
                int e;
                if (!memcmp(p->vpc[r], b->vp_consts[r], 16)) { r++; continue; }
                for (e = r + 1; e < 192 && memcmp(p->vpc[e], b->vp_consts[e], 16); e++) { }
                memcpy(p->vpc[r], b->vp_consts[r], (size_t)(e - r) * 16);
                glUniform4fv(p->u_c + r, e - r, &b->vp_consts[r][0]);
                r = e;
            }
        }
        p->vpc_valid = 1;
        p->vpc_gen = b->vp_const_gen;
    }
    {
        /* The rest, likewise: set only when something differs from what this
         * program already holds. */
        struct GlUni u;
        memset(&u, 0, sizeof u);
        memcpy(u.surf, surf, sizeof u.surf);
        memcpy(u.aa, aa, sizeof u.aa);
        memcpy(u.m, m, sizeof u.m);
        memcpy(u.vpoff, b->vp_offset, sizeof u.vpoff);
        memcpy(u.tscale, tscale, sizeof u.tscale);
        memcpy(u.c0, c0, sizeof u.c0);
        memcpy(u.c1, c1, sizeof u.c1);
        memcpy(u.fog, fog, sizeof u.fog);
        argb_vec4(r[0x1E20 / 4], u.fc[0]);      /* SPECULAR_FOG_FACTOR0/1 */
        argb_vec4(r[0x1E24 / 4], u.fc[1]);
        u.aref = (float)(r[0x340 / 4] & 0xFF);
        nv2a_fog_params(r, b->xform, u.fogp);
        u.xform = b->xform == 1 ? 1 : 0;
        /* Fixed function with an orthographic composite (no perspective:
         * row 3 is 0, 0, 0, w) draws an overlay, not the world -- the radar
         * came this way (run 33). The world's cameras are perspective. */
        if (b->xform == 0 || (b->xform == 2 && wide_prog_2d(p->vkey)) ||
            (b->xform == 1 && b->composite[12] == 0.0f && b->composite[13] == 0.0f &&
             b->composite[14] == 0.0f && b->composite[15] != 0.0f))
            wide_2d(b, s, p->vkey, u.wide);
        if (xbox_video_widescreen()) {
            /* Which transform path draws reach the 640x480 target by, once
             * per (path, movie or not), to place 2D and movies (WIDE-003). */
            static uint8_t seen[3][2][2];
            int mv = p->vkey == 0xBA9E387C24FCECE1ull;
            uint32_t xf = b->xform < 3 ? b->xform : 2;
            int sq = u.wide[0] > 0.0f;
            if (!seen[xf][mv][sq] && s->w / (b->aa_sx > 0 ? b->aa_sx : 1.0f) == 640.0f) {
                seen[xf][mv][sq] = 1;
                fprintf(stderr, "[MH-WIDE] first draw xform %u%s: prim %u verts %u tex 0x%08X squeeze %.2f\n",
                        b->xform, mv ? " (movie)" : "", b->prim, b->vertex_count, b->tex_va[0], u.wide[0]);
            }
            /* Screen-space draws of a few in-level frames, to find what draws
             * the HUD (runs 33 to 37 missed it). A capture starts on the frame
             * after one with 300 or more draws on the 640x480 target (the
             * level; the menu and movie draw under 30), then every 900 frames,
             * 3 at most. Fixed-function draws are only counted. */
            if (wide_target(b, s)) {
                float mn = 0.0f, mx = 0.0f;
                s_wide_frame_draws++;
                if (s_wide_dumping && b->xform == 1) {
                    s_wide_dump_ff++;
                } else if (s_wide_dumping) {
                    if (!wide_xrange(b, &mn, &mx))
                        mn = mx = -1.0f;
                    fprintf(stderr, "[MH-WIDE-DRAW] xf %u prog %016llX prim %u n %u tex 0x%08X x %.1f..%.1f sq %.2f\n",
                            b->xform, (unsigned long long)p->vkey, b->prim, b->vertex_count, b->tex_va[0],
                            mn, mx, u.wide[0]);
                }
            }
        }
        u.afunc = r[0x300 / 4] ? (GLint)r[0x33C / 4] : 0x207;
        if (!p->uni_valid || memcmp(&u, &p->uni, sizeof u) != 0) {
            p->uni = u;
            p->uni_valid = 1;
            if (p->u_surf >= 0) glUniform4fv(p->u_surf, 1, u.surf);
            if (p->u_aa >= 0) glUniform2fv(p->u_aa, 1, u.aa);
            if (p->u_m >= 0) glUniform4fv(p->u_m, 4, u.m);
            if (p->u_vpoff >= 0) glUniform4fv(p->u_vpoff, 1, u.vpoff);
            if (p->u_xform >= 0) glUniform1i(p->u_xform, u.xform);
            if (p->u_wide >= 0) glUniform2fv(p->u_wide, 1, u.wide);
            if (p->u_tscale >= 0) glUniform2fv(p->u_tscale, 4, &u.tscale[0][0]);
            if (p->u_c0 >= 0) glUniform4fv(p->u_c0, 8, &u.c0[0][0]);
            if (p->u_c1 >= 0) glUniform4fv(p->u_c1, 8, &u.c1[0][0]);
            if (p->u_fogcolor >= 0) glUniform4fv(p->u_fogcolor, 1, u.fog);
            if (p->u_fc0 >= 0) glUniform4fv(p->u_fc0, 1, u.fc[0]);
            if (p->u_fc1 >= 0) glUniform4fv(p->u_fc1, 1, u.fc[1]);
            if (p->u_afunc >= 0) glUniform1i(p->u_afunc, u.afunc);
            if (p->u_aref >= 0) glUniform1f(p->u_aref, u.aref);
            if (p->u_fogp >= 0) glUniform4fv(p->u_fogp, 1, u.fogp);
        }
    }

    {
        /* RECOMP_GL_WATCH=<hex va>: the state of draws that render into or
         * sample that surface/texture (first 24). */
        static int watch_init, watch_n;
        static uint32_t watch_va;
        if (!watch_init) {
            const char *w = getenv("RECOMP_GL_WATCH");
            watch_init = 1;
            watch_va = w ? (uint32_t)strtoul(w, NULL, 16) : 0;
        }
        if (watch_va && watch_n < 24 &&
            (s->va == watch_va || b->tex_va[0] == watch_va || b->tex_va[1] == watch_va ||
             b->tex_va[2] == watch_va || b->tex_va[3] == watch_va)) {
            watch_n++;
            fprintf(stderr, "  [WATCH] surf %08X %ux%u prim %u n %u xform %u attrs %04X direct %04X"
                    " modes %05X other %08X ctl %08X\n"
                    "          c0 icw %08X ocw %08X a icw %08X ocw %08X final %08X %08X\n",
                    s->va, s->w, s->h, b->prim, b->index_count, b->xform, b->attr_present,
                    b->attr_direct, pk.shader_program, pk.other_input, pk.control,
                    pk.color_icw[0], pk.color_ocw[0], pk.alpha_icw[0], pk.alpha_ocw[0],
                    pk.final0, pk.final1);
            for (i = 0; i < 4; i++) {
                uint32_t base = (0x1B00u + i * 0x40u) / 4;
                if (!(r[base + 3] & 0x40000000u))
                    continue;
                fprintf(stderr, "          t%u va %08X%s fmt %08X ctl0 %08X ctl1 %08X filt %08X rect %08X"
                        " tscale %g %g\n", i, b->tex_va[i], surf_find(b->tex_va[i]) ? " (surface)" : "",
                        r[base + 1], r[base + 3], r[base + 4], r[base + 5], r[base + 7],
                        tscale[i][0], tscale[i][1]);
            }
            fprintf(stderr, "          4UB spec %08X diff %08X 4F spec %08X %08X %08X %08X fog en %u fc0 %08X fc1 %08X\n",
                    r[0x1940 / 4 + 4], r[0x1940 / 4 + 3], r[0x1A40 / 4], r[0x1A44 / 4],
                    r[0x1A48 / 4], r[0x1A4C / 4], r[0x29C / 4], r[0x1E20 / 4], r[0x1E24 / 4]);
            fprintf(stderr, "          blend %u %04X %04X alphatest %u func %X ref %u zmask %u ztest %u cmask %08X\n",
                    r[0x304 / 4], r[0x344 / 4], r[0x348 / 4], r[0x300 / 4], r[0x33C / 4], r[0x340 / 4],
                    r[0x35C / 4], r[0x30C / 4], r[0x358 / 4]);
            s_watch_hit = 1;
            {
                /* Stage 0's image where v0's texcoords point, if it is a surface. */
                GlSurf *src = surf_find(b->tex_va[0]);
                float u0 = 0, v0 = 0;
                if (src && (b->attr_direct & 0x200) && b->direct[9].type == 2 && b->direct[9].ptr) {
                    uint8_t px[4];
                    int q;
                    memcpy(&u0, b->direct[9].ptr, 4);
                    memcpy(&v0, b->direct[9].ptr + 4, 4);
                    glBindFramebuffer(0x8CA8, src->fbo);         /* GL_READ_FRAMEBUFFER */
                    fprintf(stderr, "          src %ux%u at (%g,%g)+4/12/20/28:", src->w, src->h, u0, v0);
                    for (q = 0; q < 4; q++) {
                        GLint xx = (GLint)u0 + 4 + q * 8, yy = (GLint)v0 + 4 + q * 8;
                        if (xx < 0 || yy < 0 || xx >= (GLint)src->w || yy >= (GLint)src->h) continue;
                        glReadPixels(to_stored((uint32_t)xx, src->pw, src->w), to_stored((uint32_t)yy, src->ph, src->h), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                        fprintf(stderr, " %02X%02X%02X%02X", px[0], px[1], px[2], px[3]);
                    }
                    fprintf(stderr, "\n");
                    glBindFramebuffer(0x8CA8, s->fbo);
                }
            }
            {
                static const int wa[4] = { 0, 3, 9, 10 };
                uint32_t v, j;
                for (v = 0; v < b->vertex_count && v < 4; v++) {
                    fprintf(stderr, "          v%u", v);
                    for (j = 0; j < 4; j++) {
                        int a2 = wa[j];
                        if (!(b->attr_direct & (1u << a2)) || !b->direct[a2].ptr)
                            continue;
                        const uint8_t *q = b->direct[a2].ptr + (size_t)v * b->direct[a2].stride;
                        if (b->direct[a2].type == 2) {
                            float f4[4] = { 0 };
                            memcpy(f4, q, 4 * (b->direct[a2].size < 4 ? b->direct[a2].size : 4));
                            fprintf(stderr, " a%d(%g %g %g %g)", a2, f4[0], f4[1], f4[2], f4[3]);
                        } else {
                            uint32_t w4;
                            memcpy(&w4, q, 4);
                            fprintf(stderr, " a%d(t%u %08X)", a2, b->direct[a2].type, w4);
                        }
                    }
                    fprintf(stderr, "\n");
                }
            }
            if (b->attrs && !(b->attr_direct & 0x0209)) {
                uint32_t v;
                for (v = 0; v < b->vertex_count && v < 4; v++) {
                    const float *at = b->attrs + (size_t)v * 64;
                    fprintf(stderr, "          v%u pos %g %g %g %g d %g %g %g %g t0 %g %g %g %g t1 %g %g\n", v,
                            at[0], at[1], at[2], at[3], at[12], at[13], at[14], at[15],
                            at[36], at[37], at[38], at[39], at[40], at[41]);
                }
            }
        }
    }

    apply_state(r, b->zeta_va != 0, s);

    glBindVertexArray(s_vao);
    if (!upload_vertices(b)) {
        if (gl_gpu()) return;
        gl_fail("vertex upload failed");
    }
    {
        /* The absent attributes read the title's SET_VERTEX_DATA* values
         * (Nv2aRawBatch.attr_const); set them only when those or the set of
         * attributes changed. A fixed (0,0,0,1) gave NFSU2's lens drops a
         * specular alpha, so fog 1, of 1 instead of 0: their screen copy
         * came out as the fog colour -- grey squares. */
        static uint32_t last_mask = 0xFFFFFFFFu, last_gen;
        if (b->attr_present != last_mask || b->attr_const_gen != last_gen) {
            last_mask = b->attr_present;
            last_gen = b->attr_const_gen;
            for (a = 0; a < NV2A_RAW_ATTRS; a++)
                if (!(b->attr_present & (1u << a)))
                    glVertexAttrib4f(a, b->attr_const[a][0], b->attr_const[a][1],
                                     b->attr_const[a][2], b->attr_const[a][3]);
        }
    }
    mode = gl_prim(b, &idx, &n);
    if (!n)
        return;
    {
        /* 16-bit indices whenever the batch's vertices allow: half the bytes
         * through the ring, and the GPU's native index size. */
        static uint16_t *idx16;
        static size_t idx16_cap;
        void *dst;
        idx_type = b->vertex_count <= 65536 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        if (idx_type == GL_UNSIGNED_INT) {
            idx_at = ring_alloc(&s_iring, (GLsizeiptr)n * 4, &dst);
            if (dst)
                memcpy(dst, idx, (size_t)n * 4);
            ring_done(&s_iring, idx_at, (GLsizeiptr)n * 4, dst, idx);
        } else {
            uint16_t *o;
            idx_at = ring_alloc(&s_iring, (GLsizeiptr)n * 2, &dst);
            o = (uint16_t *)dst;
            if (!o) {
                if (n > idx16_cap) {
                    free(idx16);
                    idx16_cap = n + 4096;
                    idx16 = (uint16_t *)malloc(idx16_cap * 2);
                    if (!idx16) gl_fail("index staging allocation failed");
                }
                o = idx16;
            }
            for (i = 0; i < n; i++)
                o[i] = (uint16_t)idx[i];
            ring_done(&s_iring, idx_at, (GLsizeiptr)n * 2, dst, idx16);
        }
    }
    {
        /* RECOMP_GL_FINISH=1: name each draw before it is submitted and wait
         * for the GPU to finish it. With a synchronous log, the last line
         * of a hard GPU hang is the draw that caused it. Slow. */
        static uint32_t draws;
        if (s_finish < 0)
            s_finish = getenv("RECOMP_GL_FINISH") != NULL;
        ++draws;
        if (s_finish && s_new_obj)
            fprintf(stderr, "  [GL] draw %u: surf %08X %ux%u prim %u idx %u verts %u attrs %04X"
                    " xform %u vp %u shaders %08X combiners %08X tex0 %08X/%02X\n",
                    draws, s->va, s->w, s->h, b->prim, n, b->vertex_count,
                    b->attr_present, b->xform, b->vp_start, pk.shader_program, pk.control,
                    r[0x1B00 / 4], (r[0x1B04 / 4] >> 8) & 0xFF);
        {
            int vq = (r[0x17CC / 4] & 1);
            if (vq && !vis_room()) {
                if (!gl_gpu()) gl_fail("visibility query capacity exceeded");
                vq = 0;
            }
            if (vq) {
                s_vis_scale[s_vis_head % GL_VIS_QUERIES] =
                    (float)((double)s->w * s->h / ((double)s->pw * s->ph));
                glBeginQuery(GL_SAMPLES_PASSED, s_vis_q[s_vis_head % GL_VIS_QUERIES]);
            }
            glDrawElements(mode, (GLsizei)n, idx_type, (const void *)(uintptr_t)idx_at);
            if (vq) {
                glEndQuery(GL_SAMPLES_PASSED);
                s_vis_head++;
            }
        }
        gl_check("draw submission");
        s->dirty = 1;
        if (gl_mode() == GL_SYNC_STRICT) gl_sync(0);
        gl_step_done("draw");
        if (s_watch_hit) {
            /* What the watched draw left: the target's centre and corner. */
            uint8_t px[4];
            float x0 = 0, y0 = 0;
            int q;
            s_watch_hit = 0;
            if ((b->attr_direct & 1) && b->direct[0].type == 2 && b->direct[0].ptr) {
                memcpy(&x0, b->direct[0].ptr, 4);
                memcpy(&y0, b->direct[0].ptr + 4, 4);
            }
            fprintf(stderr, "          after (x,y from v0 + 4/12/20/28):");
            for (q = 0; q < 4; q++) {
                GLint xx = (GLint)(x0 + 0.5f) + 4 + q * 8, yy = (GLint)(y0 + 0.5f) + 4 + q * 8;
                if (xx < 0 || yy < 0 || xx >= (GLint)s->w || yy >= (GLint)s->h) continue;
                glReadPixels(to_stored((uint32_t)xx, s->pw, s->w), to_stored((uint32_t)yy, s->ph, s->h), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                fprintf(stderr, " %02X%02X%02X%02X", px[0], px[1], px[2], px[3]);
            }
            fprintf(stderr, "\n");
        }
    }
}

static void gl_clear(const Nv2aSurface *sf, const Nv2aRenderState *rs,
                     uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    GlSurf *s;
    GLbitfield bits = 0;
    float c[4];

    state_dirty();

    if (!s_q_on) s_regs = nv2a_pb_registers();

    if (!ready()) {
        if (gl_gpu()) return;
        gl_fail("GL context or entry-point initialization failed");
    }
    s = surf_bind(sf, rs ? rs->zeta_va : 0);
    if (!s) {
        if (gl_gpu()) return;
        gl_fail("surface binding failed");
    }
    glDisable(GL_SCISSOR_TEST);
    if (gl_gpu()) {
        if (s_regs) {
            uint32_t hz = s_regs[0x1D98 / 4], vt = s_regs[0x1D9C / 4];
            /* The clear rectangle is in logical pixels, like SURFACE_CLIP; the
             * surface is larger by the anti-aliasing factor. Unscaled, a 2x1
             * surface cleared only its left half, and stale depth on the right
             * kept old frames of a moving car alive behind the background. */
            uint32_t x0 = (hz & 0xFFFF) * s->aa_sx, x1 = ((hz >> 16) + 1) * s->aa_sx;
            uint32_t y0 = (vt & 0xFFFF) * s->aa_sy, y1 = ((vt >> 16) + 1) * s->aa_sy;
            if (x1 > s->w) x1 = s->w;
            if (y1 > s->h) y1 = s->h;
            if (x1 > x0 && y1 > y0 && (x1 - x0 < s->w || y1 - y0 < s->h)) {
                GLint px0 = to_stored(x0, s->pw, s->w), py0 = to_stored(y0, s->ph, s->h);
                glEnable(GL_SCISSOR_TEST);
                glScissor(px0, py0, to_stored(x1, s->pw, s->w) - px0,
                          to_stored(y1, s->ph, s->h) - py0);
            }
        }
    } else {
        /* Storage includes pitch padding and may retain a larger earlier
         * clip. Never clear those bytes: intersect the actual clip with
         * explicitly programmed clear bounds, independently on each axis. */
        uint32_t x0 = sf->clip_x, x1 = x0 + sf->width;
        uint32_t y0 = sf->clip_y, y1 = y0 + sf->height;
        if (sf->clear_rect_set & 1u) {
            uint32_t a = (sf->clear_rect_h & 0xFFFFu) * s->aa_sx;
            uint32_t b = ((sf->clear_rect_h >> 16) + 1u) * s->aa_sx;
            if (x0 < a) x0 = a;
            if (x1 > b) x1 = b;
        }
        if (sf->clear_rect_set & 2u) {
            uint32_t a = (sf->clear_rect_v & 0xFFFFu) * s->aa_sy;
            uint32_t b = ((sf->clear_rect_v >> 16) + 1u) * s->aa_sy;
            if (y0 < a) y0 = a;
            if (y1 > b) y1 = b;
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor((GLint)x0, (GLint)y0, x1 > x0 ? (GLsizei)(x1-x0) : 0,
                   y1 > y0 ? (GLsizei)(y1-y0) : 0);
    }
    if (flags & 0xF0) {
        argb_vec4(argb, c);
        glColorMask((flags & 0x10) != 0, (flags & 0x20) != 0,
                    (flags & 0x40) != 0, (flags & 0x80) != 0);
        glClearColor(c[0], c[1], c[2], c[3]);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & 0x1) {
        uint32_t zmax = s_regs ? zmax_of(s_regs) : 0xFFFFFFu;
        uint32_t z = zmax == 0xFFFFu ? (zstencil & 0xFFFF) : (zstencil >> 8);
        glDepthMask(1);
        glClearDepth((double)z / (double)zmax);
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & 0x2) {
        glStencilMask(0xFF);
        glClearStencil((GLint)(zstencil & 0xFF));
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (bits)
        glClear(bits);
    gl_check("clear submission");
    s->dirty = 1;
    if (gl_mode() == GL_SYNC_STRICT) gl_sync(0);
    gl_step_done("clear");
    glDisable(GL_SCISSOR_TEST);
}

static void dump_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb");
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t row = w * 3, pad = (4 - (row & 3)) & 3, size = 54 + (row + pad) * h, y, x;

    if (!f)
        return;
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    /* BMP rows are bottom-first; the surface is top-first. */
    for (y = h; y-- > 0; ) {
        const uint8_t *p = bgra + (size_t)y * w * 4;
        for (x = 0; x < w; x++)
            fwrite(p + x * 4, 1, 3, f);
        fwrite("\0\0\0", 1, pad, f);
    }
    fclose(f);
}

/* The host's per-frame hook (switch_saltynx.c: the frame rate for SaltyNX
 * overlays), with the rendered picture's size. Weak and undefined
 * elsewhere: the call is skipped. */
extern void nv2a_host_frame_presented(uint32_t width, uint32_t height) __attribute__((weak));

static void gl_flip(void)
{
    state_dirty();
    static int dump_every = -1;
    static char dump_prefix[256];
    static uint8_t *pix;
    static size_t pix_cap;
    int ww, wh;
    GlSurf *s = s_last;

    if (!ready()) {
        if (gl_gpu()) return;
        gl_fail("GL context or entry-point initialization failed");
    }
    vis_poll();
    page_flip();
    if (s_wide_dumping)
        fprintf(stderr, "[MH-WIDE-DRAW] frame %u end: %u draws on the 640x480 target, %u fixed-function\n",
                s_frame, s_wide_frame_draws, s_wide_dump_ff);
    s_wide_dumping = 0;
    if (s_wide_dumps < 3 && s_wide_frame_draws >= 300 &&
        (!s_wide_dumps || s_frame - s_wide_dump_at >= 900)) {
        s_wide_dumping = 1;
        s_wide_dumps++;
        s_wide_dump_at = s_frame;
        s_wide_dump_ff = 0;
        fprintf(stderr, "[MH-WIDE-DRAW] capture %u, frame %u (previous frame had %u draws)\n",
                s_wide_dumps, s_frame + 1, s_wide_frame_draws);
    }
    s_wide_frame_draws = 0;
    s_frame++;
    {
        /* A frame over 50 ms (a hitch): say what it did -- compiling programs
         * (Mesa, at first use), uploading textures (decode + glTexImage), or
         * neither (the game or the executor). At most 30 lines per 10 s. */
        static uint64_t last_ns, window_ns;
        static int lines;
        uint64_t now = hz_now();
        if (last_ns) {
            uint64_t dt = now - last_ns;
            if (now - window_ns > 10000000000ull) { window_ns = now; lines = 0; }
            if (dt > 50000000ull && lines++ < 30)
                fprintf(stderr, "[hitch] frame %u: %.0f ms -- %u programs compiled (%.0f ms),"
                        " %u textures %.1f MB (%.0f ms), %u draws\n", s_frame, dt / 1e6,
                        s_hz_progs, s_hz_prog_ns / 1e6, s_hz_texs,
                        s_hz_tex_bytes / 1048576.0, s_hz_tex_ns / 1e6, s_hz_draws);
        }
        last_ns = now;
        s_hz_progs = s_hz_texs = s_hz_draws = 0;
        s_hz_prog_ns = s_hz_tex_ns = s_hz_tex_bytes = 0;
    }
    {
        /* RECOMP_FPS_LOG=1: presented frames per 10 s on stderr (the Switch
         * build has its own [perf] report). */
        static int on = -1;
        static uint32_t last_frame;
        static struct timespec last;
        struct timespec now;
        if (on < 0) {
            const char *e = getenv("RECOMP_FPS_LOG");
            on = e && *e == '1';
            clock_gettime(CLOCK_MONOTONIC, &last);
        }
        if (on) {
            double dt;
            clock_gettime(CLOCK_MONOTONIC, &now);
            dt = (double)(now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9;
            if (dt >= 10.0) {
                extern void xbox_vblank_report(double, char *, size_t);
                char vb[128];
                xbox_vblank_report(dt, vb, sizeof vb);
                fprintf(stderr, "[fps] %.1f fps (frame %u), %s\n",
                        (s_frame - last_frame) / dt, s_frame, vb);
                last_frame = s_frame;
                last = now;
            }
        }
    }
    if (dump_every < 0) {
        const char *d = getenv("RECOMP_GL_DUMP");
        dump_every = 0;
        if (d && *d) {
            const char *comma = strchr(d, ',');
            size_t n = comma ? (size_t)(comma - d) : strlen(d);
            if (n >= sizeof dump_prefix) n = sizeof dump_prefix - 1;
            memcpy(dump_prefix, d, n);
            dump_prefix[n] = 0;
            dump_every = comma ? atoi(comma + 1) : 60;
            if (dump_every <= 0) dump_every = 60;
        }
    }
    if (!s || (gl_gpu() && !s_drew_any)) {
        /* No guest surface has been bound (strict and fenced: a clear-only
         * surface is valid guest output and takes the RAM import/present
         * path; gpu: nothing has been drawn yet, so show the placeholder). */
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(1, 1, 1, 1);
        if (!nv2a_gl_draw_placeholder()) {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        goto swap;
    }

    /* Completion (including sync(1) before CPU fallback) relinquishes GPU
     * ownership. A CPU-only framebuffer update need not be followed by a
     * draw, so presentation must import the current bytes independently. */
    gl_sync(0);
    if (!gl_gpu() && !ram_span(s->guest.color_va, (size_t)s->guest.pitch * s->h))
        gl_fail("presentation storage outside RAM");
    if (!gl_gpu() && (!gl_fenced() || !s->resident || !rows_match(s->guest.color_va,s->guest.pitch,s->color_baseline,s->w,s->h))) {
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(s->guest.pitch / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h,
                    GL_BGRA, GL_UNSIGNED_BYTE,
                    (const uint8_t *)xbox_GetMemoryOffset() + s->guest.color_va);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    tex_bind_forget();
    gl_check("presentation RAM import");
    if (gl_fenced()) rows_snapshot(s->guest.color_va,s->guest.pitch,s->color_baseline,s->w,s->h);
    }

    if (dump_every && (s_frame % (uint32_t)dump_every) == 0) {
        size_t need = (size_t)s->pw * s->ph * 4;
        if (need > pix_cap) {
            free(pix);
            pix_cap = need;
            pix = (uint8_t *)malloc(pix_cap);
        }
        if (pix) {
            char path[320];
            glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, (GLsizei)s->pw, (GLsizei)s->ph, GL_BGRA,
                         GL_UNSIGNED_BYTE, pix);
            snprintf(path, sizeof path, "%s%05u.bmp", dump_prefix, s_frame);
            dump_bmp(path, pix, s->pw, s->ph);
        }
    }

    /* Present: the logical aspect (surface / AA factor; 16:9 for a
     * widescreen title), letterboxed, with
     * the top-first rows turned the right way up. */
    /* The host may know the real on-screen size better than SDL (the
     * Switch's EGL surface follows the display, SDL keeps the window's
     * creation size). */
    if (!nv2a_gl_screen_size(&ww, &wh) || ww <= 0 || wh <= 0)
        SDL_GL_GetDrawableSize(s_win, &ww, &wh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        float lw = (float)s->w / (float)s->aa_sx, lh = (float)s->h / (float)s->aa_sy;
        /* RECOMP_PRESENT_CROP_Y: rows left out top and bottom (cy in the
         * stored pixels). */
        int crop = nv2a_present_crop_y(), cy = 0;
        if (crop > 0 && lh > 4.0f * (float)crop) {
            cy = (int)((float)crop * (float)s->ph / lh + 0.5f);
            lh -= 2.0f * (float)crop;
        }
        /* A widescreen title renders 640x480 anamorphic: show it at 16:9. */
        if (xbox_video_widescreen() && lw < lh * 1.5f)
            lw = lh * 16.0f / 9.0f;
        float scale = (float)ww / lw < (float)wh / lh ? (float)ww / lw : (float)wh / lh;
        int dw = (int)(lw * scale), dh = (int)(lh * scale);
        int dx = (ww - dw) / 2, dy = (wh - dh) / 2;
        /* Scissored to the picture: Switch Mesa (nouveau) runs a scaled,
         * flipped blit over more than the destination rectangle, and the
         * pillarbox bars filled with the source's edge columns stretched
         * sideways. Blits obey the scissor test, so the bars stay cleared. */
        glEnable(GL_SCISSOR_TEST);
        glScissor(dx, dy, dw, dh);
        if (!gamma_pass(s->tex, (int)s->pw, (int)s->ph, cy, dx, dy, dw, dh))
            glBlitFramebuffer(0, cy, (GLint)s->pw, (GLint)s->ph - cy,
                              dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glDisable(GL_SCISSOR_TEST);
    }
swap:
    gl_check("presentation");
    gl_step_done("present");
    SDL_GL_SwapWindow(s_win);
    if (nv2a_host_frame_presented)
        nv2a_host_frame_presented(s && s_drew_any ? s->pw : 0, s && s_drew_any ? s->ph : 0);
    gl_step_done("swap");
    tex_bind_forget();
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { }
    }
}

/* ── Threaded submission (RECOMP_GL_THREAD=1) ─────────────────────────
 *
 * On the Switch the executor thread spent ~35 us per draw in Mesa and ~18 us
 * decoding the pushbuffer, one after the other, and that thread was the frame
 * rate limit. With this on, the executor only decodes and queues; a GL
 * thread of its own replays the queue, so the two halves run on two cores.
 *
 * A queued draw must not point at anything the executor or the title may
 * change once it moves on, so the executor thread copies: the method shadow,
 * transform program and constants go as 64/16-byte blocks that differ from
 * what was last queued (the GL thread keeps its own copies), vertex data as
 * the byte ranges the draw reads (vertex_runs), indices, and converted
 * attributes. Texture images are still read from guest memory when the GL
 * thread gets to them (at most two frames later); texture and palette
 * addresses come pre-resolved in the batch, so nothing here reads the
 * executor's DMA state. At most two flips are queued: the executor waits
 * for the GL thread beyond that.
 *
 * Records are 16-byte aligned in one ring. Every record first carries the
 * shadow's changes, because clears and presents read registers too (the
 * clip, the depth range) and saw the executor's live shadow before. */
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

enum { Q_WRAP = 0, Q_DRAW = 1, Q_CLEAR = 2, Q_FLIP = 3, Q_REPORT = 4, Q_SYNC = 5, Q_BARRIER = 6 };
typedef struct { uint32_t type, size, nreg, nprog, nconst, pad[3]; } QHdr;
typedef struct { uint32_t idx, pad[3]; uint32_t w[16]; } QRegChunk;   /* 64 B of shadow */
typedef struct { uint32_t idx, pad[3]; uint32_t w[4]; } QRow;         /* 16 B row */
typedef struct {
    Nv2aSurface s;
    Nv2aRenderState rs;
    uint32_t flags, argb, zs, pad;
} QClear;

#define Q_BYTES   (48u << 20)
#define Q_ALIGN(x) (((x) + 15u) & ~(size_t)15u)
#define REG_WORDS (0x2000 / 4)
#define PROG_ROWS 136
#define CONST_ROWS 192

static uint8_t *s_q;
static size_t s_q_head, s_q_tail;               /* bytes written / read, ever */
static int s_q_consumer_waits, s_q_producer_waits, s_q_flips;
static pthread_mutex_t s_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_q_cv = PTHREAD_COND_INITIALIZER;
static uint64_t s_q_dropped;
/* For the perf report: time the GL thread sat with an empty queue, and the
 * executor was held back (queue full, or two flips queued). ns. */
static uint64_t s_q_gl_idle_ns, s_q_exec_wait_ns;
static uint64_t q_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Executor thread: what was last queued, and where the live shadow is. */
static uint32_t s_q_regs[REG_WORDS], s_q_prog[PROG_ROWS][4];
static float s_q_consts[CONST_ROWS][4];
static const uint32_t *s_q_live_regs;
static uint32_t s_q_prog_gen, s_q_const_gen;
/* GL thread: its own copies, rebuilt from the records. */
static uint32_t g_regs[REG_WORDS], g_prog[PROG_ROWS][4];
static float g_consts[CONST_ROWS][4];

static void q_timed_wait(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 2000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&s_q_cv, &s_q_lock, &ts);
}

/* Room for `size` contiguous bytes; returns where to write them. */
static uint8_t *q_reserve(size_t size)
{
    size_t off = s_q_head % Q_BYTES, need = size;
    if (off + size > Q_BYTES)
        need += Q_BYTES - off;                  /* the wrap filler */
    if (Q_BYTES - (s_q_head - __atomic_load_n(&s_q_tail, __ATOMIC_ACQUIRE)) < need) {
        uint64_t t0 = q_now_ns();
        pthread_mutex_lock(&s_q_lock);
        s_q_producer_waits = 1;
        while (Q_BYTES - (s_q_head - __atomic_load_n(&s_q_tail, __ATOMIC_ACQUIRE)) < need)
            q_timed_wait();
        s_q_producer_waits = 0;
        pthread_mutex_unlock(&s_q_lock);
        __atomic_add_fetch(&s_q_exec_wait_ns, q_now_ns() - t0, __ATOMIC_RELAXED);
    }
    if (off + size > Q_BYTES) {
        QHdr *w = (QHdr *)(s_q + off);
        w->type = Q_WRAP;                        /* ring has slack past the end */
        w->size = (uint32_t)(Q_BYTES - off);
        __atomic_store_n(&s_q_head, s_q_head + (Q_BYTES - off), __ATOMIC_RELEASE);
        off = 0;
    }
    return s_q + off;
}

static void q_publish(size_t size)
{
    __atomic_store_n(&s_q_head, s_q_head + size, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&s_q_consumer_waits, __ATOMIC_SEQ_CST)) {
        pthread_mutex_lock(&s_q_lock);
        pthread_cond_broadcast(&s_q_cv);
        pthread_mutex_unlock(&s_q_lock);
    }
}

/* The shadow blocks that changed since the last record. The executor marks
 * the 64-byte blocks it writes (nv2a_pb_reg_dirty; comparing all 8 KB on
 * every draw was 14% of its thread on the console); those are compared and
 * the ones that really differ kept in s_q_pend until q_reg_emit. Both run on
 * the executor thread. */
extern uint8_t *nv2a_pb_reg_dirty(void);
static uint16_t s_q_pend[REG_WORDS / 16];
static uint32_t s_q_npend;

static uint32_t q_reg_collect(void)
{
    uint8_t *dirty = nv2a_pb_reg_dirty();
    uint32_t c;
    if (!s_q_live_regs)
        return 0;
    for (c = 0; c < REG_WORDS / 16; c++) {
        if (!dirty[c])
            continue;
        dirty[c] = 0;
        if (memcmp(s_q_live_regs + c * 16, s_q_regs + c * 16, 64) != 0) {
            uint32_t k;
            for (k = 0; k < s_q_npend && s_q_pend[k] != c; k++) { }
            if (k == s_q_npend)
                s_q_pend[s_q_npend++] = (uint16_t)c;
        }
    }
    return s_q_npend;
}

static uint32_t q_reg_emit(QRegChunk *out)
{
    uint32_t k, n = s_q_npend;
    for (k = 0; k < n; k++) {
        uint32_t c = s_q_pend[k];
        out[k].idx = c;
        memcpy(out[k].w, s_q_live_regs + c * 16, 64);
        memcpy(s_q_regs + c * 16, s_q_live_regs + c * 16, 64);
    }
    s_q_npend = 0;
    return n;
}

static uint32_t q_rows_diff(const uint32_t (*live)[4], uint32_t (*sent)[4], uint32_t rows,
                            QRow *out)
{
    uint32_t r, n = 0;
    for (r = 0; r < rows; r++)
        if (memcmp(live[r], sent[r], 16) != 0) {
            if (out) {
                out[n].idx = r;
                memcpy(out[n].w, live[r], 16);
                memcpy(sent[r], live[r], 16);
            }
            n++;
        }
    return n;
}

/* Header plus the shadow changes; returns the offset after them. */
static size_t q_write_state(uint8_t *rec, uint32_t type, uint32_t nreg,
                            uint32_t nprog, uint32_t nconst, const Nv2aRawBatch *b)
{
    QHdr *h = (QHdr *)rec;
    size_t at = sizeof(QHdr);
    memset(h, 0, sizeof *h);
    h->type = type;
    h->nreg = q_reg_emit((QRegChunk *)(rec + at));
    (void)nreg;
    at += (size_t)h->nreg * sizeof(QRegChunk);
    if (b && nprog) {
        h->nprog = q_rows_diff(b->vp_program, s_q_prog, PROG_ROWS, (QRow *)(rec + at));
        at += (size_t)h->nprog * sizeof(QRow);
    }
    if (b && nconst) {
        h->nconst = q_rows_diff((const uint32_t (*)[4])b->vp_consts,
                                (uint32_t (*)[4])s_q_consts, CONST_ROWS, (QRow *)(rec + at));
        at += (size_t)h->nconst * sizeof(QRow);
    }
    return at;
}

static void q_drain(int invalidate, int visibility)
{
    size_t size = sizeof(QHdr) + 16;
    uint8_t *rec = q_reserve(size);
    memset(rec, 0, size);
    ((QHdr *)rec)->type = visibility ? Q_SYNC : Q_BARRIER;
    ((QHdr *)rec)->size = (uint32_t)size;
    ((uint32_t *)(rec + sizeof(QHdr)))[0] = (uint32_t)invalidate;
    size_t target = s_q_head + size;
    q_publish(size);
    pthread_mutex_lock(&s_q_lock);
    s_q_producer_waits = 1;
    while (__atomic_load_n(&s_q_tail, __ATOMIC_ACQUIRE) < target)
        q_timed_wait();
    s_q_producer_waits = 0;
    pthread_mutex_unlock(&s_q_lock);
}

static void q_sync(int invalidate) { if (!gl_gpu()) q_drain(invalidate,1); }

static void q_draw(const Nv2aRawBatch *b)
{
    VRun run[NV2A_RAW_ATTRS];
    int of_run[NV2A_RAW_ATTRS];
    uint32_t nrun = vertex_runs(b, run, of_run), i, a, nreg, nprog = 0, nconst = 0;
    uint16_t conv = b->attr_present & (uint16_t)~b->attr_direct;
    size_t size, at, runs_at[NV2A_RAW_ATTRS], attrs_at = 0, idx_at, batch_at;
    Nv2aRawBatch *qb;
    uint8_t *rec;

    s_q_live_regs = b->regs;
    nreg = q_reg_collect();
    if (b->vp_program && b->vp_prog_gen != s_q_prog_gen && b->vp_slots <= PROG_ROWS)
        nprog = q_rows_diff(b->vp_program, s_q_prog, PROG_ROWS, NULL);
    if (b->vp_consts && b->vp_const_gen != s_q_const_gen && b->vp_const_count <= CONST_ROWS)
        nconst = q_rows_diff((const uint32_t (*)[4])b->vp_consts,
                             (uint32_t (*)[4])s_q_consts, CONST_ROWS, NULL);
    size = sizeof(QHdr) + (size_t)nreg * sizeof(QRegChunk)
         + (size_t)(nprog + nconst) * sizeof(QRow);
    batch_at = size;
    size += Q_ALIGN(sizeof(Nv2aRawBatch));
    for (i = 0; i < nrun; i++) {
        runs_at[i] = size;
        size += Q_ALIGN((size_t)(run[i].hi - run[i].lo));
    }
    if (conv) {
        attrs_at = size;
        size += Q_ALIGN((size_t)b->vertex_count * NV2A_RAW_ATTRS * 16);
    }
    idx_at = size;
    size += Q_ALIGN((size_t)b->index_count * 4);
    if (size > Q_BYTES / 4)
        gl_fail("queued draw exceeds bounded record capacity");

    rec = q_reserve(size);
    at = q_write_state(rec, Q_DRAW, nreg, nprog, nconst, b);
    s_q_prog_gen = b->vp_prog_gen;
    s_q_const_gen = b->vp_const_gen;
    if (at != batch_at) {                       /* cannot happen: same diffs */
        fprintf(stderr, "  [GL] queue record mismatch %zu/%zu\n", at, batch_at);
        abort();
    }
    qb = (Nv2aRawBatch *)(rec + batch_at);
    *qb = *b;
    /* Pointers become offsets into the record; the GL thread fixes them. */
    for (i = 0; i < nrun; i++)
        memcpy(rec + runs_at[i], run[i].lo, (size_t)(run[i].hi - run[i].lo));
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if (b->attr_direct & (1u << a))
            qb->direct[a].ptr = (const uint8_t *)(uintptr_t)
                (runs_at[of_run[a]] + (size_t)(b->direct[a].ptr - run[of_run[a]].lo));
    if (conv)
        memcpy(rec + attrs_at, b->attrs, (size_t)b->vertex_count * NV2A_RAW_ATTRS * 16);
    qb->attrs = (const float *)(uintptr_t)attrs_at;
    memcpy(rec + idx_at, b->indices, (size_t)b->index_count * 4);
    qb->indices = (const uint32_t *)(uintptr_t)idx_at;
    ((QHdr *)rec)->size = (uint32_t)size;
    q_publish(size);
    q_drain(0,gl_mode() == GL_SYNC_STRICT);
}

static void q_clear(const Nv2aSurface *sf, const Nv2aRenderState *rs,
                    uint32_t flags, uint32_t argb, uint32_t zs)
{
    s_q_live_regs = nv2a_pb_registers();
    uint32_t nreg = q_reg_collect();
    size_t size = sizeof(QHdr) + (size_t)nreg * sizeof(QRegChunk) + Q_ALIGN(sizeof(QClear));
    uint8_t *rec = q_reserve(size);
    size_t at = q_write_state(rec, Q_CLEAR, nreg, 0, 0, NULL);
    QClear *c = (QClear *)(rec + at);
    c->s = *sf;
    c->rs = *rs;
    c->flags = flags;
    c->argb = argb;
    c->zs = zs;
    ((QHdr *)rec)->size = (uint32_t)size;
    q_publish(size);
    q_drain(0,gl_mode() == GL_SYNC_STRICT);
}

static void q_flip(void)
{
    uint32_t nreg;
    size_t size;
    uint8_t *rec;

    /* No more than two frames behind the executor. */
    if (__atomic_load_n(&s_q_flips, __ATOMIC_ACQUIRE) >= 2) {
        uint64_t t0 = q_now_ns();
        pthread_mutex_lock(&s_q_lock);
        s_q_producer_waits = 1;
        while (__atomic_load_n(&s_q_flips, __ATOMIC_ACQUIRE) >= 2)
            q_timed_wait();
        s_q_producer_waits = 0;
        pthread_mutex_unlock(&s_q_lock);
        __atomic_add_fetch(&s_q_exec_wait_ns, q_now_ns() - t0, __ATOMIC_RELAXED);
    }
    nreg = q_reg_collect();
    size = sizeof(QHdr) + (size_t)nreg * sizeof(QRegChunk);
    rec = q_reserve(size);
    q_write_state(rec, Q_FLIP, nreg, 0, 0, NULL);
    ((QHdr *)rec)->size = (uint32_t)size;
    __atomic_add_fetch(&s_q_flips, 1, __ATOMIC_ACQ_REL);
    q_publish(size);
    q_sync(0); /* Consume presentation's borrowed RAM before the caller resumes. */
}

static void q_report(uint32_t op, uint32_t va)
{
    uint32_t nreg = q_reg_collect();
    size_t size = sizeof(QHdr) + (size_t)nreg * sizeof(QRegChunk) + 16;
    uint8_t *rec = q_reserve(size);
    size_t at = q_write_state(rec, Q_REPORT, nreg, 0, 0, NULL);
    ((uint32_t *)(rec + at))[0] = op;
    ((uint32_t *)(rec + at))[1] = va;
    ((QHdr *)rec)->size = (uint32_t)size;
    q_publish(size);
}

static void *q_gl_thread(void *arg)
{
    extern void xbox_nx_spread_thread(void);
    extern void xbox_nx_track_thread(void *entry);
    (void)arg;
    xbox_nx_spread_thread();
    xbox_nx_track_thread((void *)q_gl_thread);
    for (;;) {
        size_t avail = __atomic_load_n(&s_q_head, __ATOMIC_SEQ_CST) - s_q_tail;
        uint8_t *rec;
        const QHdr *h;
        size_t at;
        uint32_t i;

        if (!avail) {
            uint64_t t0 = q_now_ns();
            pthread_mutex_lock(&s_q_lock);
            __atomic_store_n(&s_q_consumer_waits, 1, __ATOMIC_SEQ_CST);
            while (__atomic_load_n(&s_q_head, __ATOMIC_SEQ_CST) == s_q_tail)
                q_timed_wait();
            __atomic_store_n(&s_q_consumer_waits, 0, __ATOMIC_SEQ_CST);
            pthread_mutex_unlock(&s_q_lock);
            __atomic_add_fetch(&s_q_gl_idle_ns, q_now_ns() - t0, __ATOMIC_RELAXED);
            continue;
        }
        rec = s_q + s_q_tail % Q_BYTES;
        h = (const QHdr *)rec;
        if (h->type != Q_WRAP) {
            at = sizeof(QHdr);
            for (i = 0; i < h->nreg; i++, at += sizeof(QRegChunk)) {
                const QRegChunk *c = (const QRegChunk *)(rec + at);
                memcpy(g_regs + c->idx * 16, c->w, 64);
            }
            for (i = 0; i < h->nprog; i++, at += sizeof(QRow)) {
                const QRow *r = (const QRow *)(rec + at);
                memcpy(g_prog[r->idx], r->w, 16);
            }
            for (i = 0; i < h->nconst; i++, at += sizeof(QRow)) {
                const QRow *r = (const QRow *)(rec + at);
                memcpy(g_consts[r->idx], r->w, 16);
            }
            s_regs = g_regs;                    /* what clears/presents read */
            if (h->type == Q_DRAW) {
                Nv2aRawBatch bb = *(const Nv2aRawBatch *)(rec + at);
                uint32_t a;
                bb.regs = g_regs;
                bb.vp_program = (const uint32_t (*)[4])g_prog;
                bb.vp_consts = (const float (*)[4])g_consts;
                for (a = 0; a < NV2A_RAW_ATTRS; a++)
                    if (bb.attr_direct & (1u << a))
                        bb.direct[a].ptr = rec + (uintptr_t)bb.direct[a].ptr;
                bb.attrs = (const float *)(rec + (uintptr_t)bb.attrs);
                bb.indices = (const uint32_t *)(rec + (uintptr_t)bb.indices);
                gl_draw_raw(&bb);
            } else if (h->type == Q_CLEAR) {
                const QClear *c = (const QClear *)(rec + at);
                gl_clear(&c->s, &c->rs, c->flags, c->argb, c->zs);
            } else if (h->type == Q_SYNC) {
                gl_sync((int)((const uint32_t *)(rec + at))[0]);
            } else if (h->type == Q_REPORT) {
                gl_report(((const uint32_t *)(rec + at))[0], ((const uint32_t *)(rec + at))[1]);
            } else if (h->type == Q_FLIP) {
                gl_flip();
                __atomic_sub_fetch(&s_q_flips, 1, __ATOMIC_ACQ_REL);
            }
        }
        __atomic_store_n(&s_q_tail, s_q_tail + h->size, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&s_q_producer_waits, __ATOMIC_SEQ_CST)) {
            pthread_mutex_lock(&s_q_lock);
            pthread_cond_broadcast(&s_q_cv);
            pthread_mutex_unlock(&s_q_lock);
        }
    }
    return NULL;
}

/* Share of the last `secs` the GL thread sat idle and the executor was held
 * back by the queue; -1 when the GL thread is off. */
void nv2a_gl_queue_stats(double secs, int *gl_idle_pct, int *exec_wait_pct)
{
    if (!s_q_on) {
        *gl_idle_pct = *exec_wait_pct = -1;
        return;
    }
    *gl_idle_pct = (int)(__atomic_exchange_n(&s_q_gl_idle_ns, 0, __ATOMIC_RELAXED) / (secs * 1e7));
    *exec_wait_pct = (int)(__atomic_exchange_n(&s_q_exec_wait_ns, 0, __ATOMIC_RELAXED) / (secs * 1e7));
}

static int q_start(void)
{
    pthread_t t;
    pthread_attr_t at;
    s_q = (uint8_t *)malloc(Q_BYTES + 256);
    if (!s_q)
        return 0;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 1u << 20);
    if (pthread_create(&t, &at, q_gl_thread, NULL) != 0) {
        free(s_q);
        s_q = NULL;
        return 0;
    }
    pthread_detach(t);
    return 1;
}

static Nv2aBackend s_backend_q = {
    .clear = q_clear,
    .flip = q_flip,
    .draw_raw = q_draw,
    .flags = NV2A_BACKEND_RAW_DIRECT,
    .report = q_report,
    .sync = q_sync,
};

static Nv2aBackend s_backend_gl = {
    .clear = gl_clear,
    /* draw: everything arrives through draw_raw */
    .flip = gl_flip,
    .draw_raw = gl_draw_raw,
    .flags = NV2A_BACKEND_RAW_DIRECT,
    .report = gl_report,
    .sync = gl_sync,
};

void nv2a_gl_install(void)
{
    uint32_t flags=NV2A_BACKEND_RAW_DIRECT;
    if (gl_fenced()) flags |= NV2A_BACKEND_DEFERRED_RAM;
    s_backend_gl.flags=s_backend_q.flags=flags;
    {
        const char *e = getenv("RECOMP_GL_THREAD");
        s_q_on = e && !strcmp(e, "1");
        if (s_q_on && !q_start()) gl_fail("GL queue thread startup failed");
    }
    if (s_q_on) {
        nv2a_backend_register(&s_backend_q);
        fprintf(stderr, "  [GL] NV2A OpenGL renderer registered, on a thread of its own"
                        " (RECOMP_GL_THREAD=1)\n");
        return;
    }
    nv2a_backend_register(&s_backend_gl);
    fprintf(stderr, "  [GL] NV2A OpenGL renderer registered\n");
}
