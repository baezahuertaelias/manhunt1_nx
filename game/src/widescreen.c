/*
 * MANHUNT_WIDESCREEN=1 (Switch, default off): 16:9 Hor+ by anamorphic
 * rendering. WIDE-002, first stage.
 *
 * The title keeps its 640x480 surfaces. Every 4:3 camera gets a view window
 * 4/3 wider at the same height, so the 3D scene covers a 16:9 field of view
 * at the original vertical field of view, squeezed into 640 columns; the
 * present then shows the surface at 16:9 (xbox_video_widescreen, used by the
 * GL renderer's present in nv2a_gl.c), which restores square pixels.
 *
 * Hook: 0x001E80A0, identified as RenderWare's RwCameraSetViewWindow(camera,
 * viewWindow), cdecl. It copies the RwV2d to camera+0x68/+0x6C, stores 1/x
 * and 1/y (1.0 at 0x002CF87C) at +0x70/+0x74, then calls 0x001E6290 on the
 * camera's frame, which marks it for the later sync that rebuilds the
 * frustum. The identification is a hypothesis from that code shape. The
 * title's own cameras are not exactly 4:3: run 29 logged 0.7000 x 0.5469
 * (ratio 1.280). A 4:3 screen made 16:9 needs 4/3 more width whatever that
 * base ratio is, so every window with a ratio from 1.2 to 1.45 is widened
 * by 4/3; others (square render-to-texture cameras, say) keep theirs. The
 * title also passes a camera's own, already widened window back in (run 30:
 * 0.9333 x 0.5469, ratio 1.707); that is outside the band, so it is not
 * widened twice. The
 * caller's RwV2d is left unchanged. Patching the camera after the original
 * returns lands before that sync, so culling follows the wider window.
 *
 * Only the GL renderer's present knows the 16:9 shape. Configuration rejects
 * enabled widescreen with any other backend; the query also refuses to widen
 * cameras if called before the startup validation. HUD/movie placement is
 * handled separately by the GL renderer.
 *
 * Linked with -Wl,--wrap=sub_001E80A0: the function is defined in
 * recomp_0037.c and all 8 call sites are in other object files.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "recomp_types.h"

#define CAM_VIEW_WINDOW   0x68u
#define CAM_RECIP_WINDOW  0x70u

extern void __real_sub_001E80A0(void);

static atomic_int s_on = -1;

/* Called before guest memory or renderer threads are initialized. */
int manhunt_widescreen_configure(void)
{
    const char *e = getenv("MANHUNT_WIDESCREEN");
    const char *backend = getenv("MANHUNT_GPU_BACKEND");
    int on = e && e[0] == '1' && !e[1];
    if (e && *e && strcmp(e, "0") != 0 && strcmp(e, "1") != 0) {
        atomic_store(&s_on, 0);
        fprintf(stderr, "[MH-WIDE] FAIL MANHUNT_WIDESCREEN must be 0 or 1\n");
        return 0;
    }
    if (on && (!backend || strcmp(backend, "2") != 0)) {
        atomic_store(&s_on, 0);
        fprintf(stderr, "[MH-WIDE] FAIL MANHUNT_WIDESCREEN=1 requires MANHUNT_GPU_BACKEND=2\n");
        return 0;
    }
    atomic_store(&s_on, on);
    if (on)
        fprintf(stderr, "[MH-WIDE] 16:9 Hor+ (anamorphic 640x480, cameras with ratio 1.2..1.45 widened by 4/3)\n");
    return 1;
}

int xbox_video_widescreen(void)
{
    if (atomic_load(&s_on) < 0 && !manhunt_widescreen_configure())
        return 0;
    return atomic_load(&s_on);
}

static float rdf(uint32_t va)
{
    uint32_t u = MEM32(va);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static void wrf(uint32_t va, float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    MEM32(va) = u;
}

/* RwCamera *RwCameraSetViewWindow(RwCamera *camera, const RwV2d *viewWindow) */
void __wrap_sub_001E80A0(void)
{
    static atomic_uint s_logged;
    uint32_t cam = MEM32(g_esp + 4);
    float x, y, r;

    __real_sub_001E80A0();
    if (!xbox_video_widescreen() || !cam)
        return;
    x = rdf(cam + CAM_VIEW_WINDOW);
    y = rdf(cam + CAM_VIEW_WINDOW + 4);
    if (!(x > 0.0f) || !(y > 0.0f))
        return;
    r = x / y;
    if (r < 1.2f || r > 1.45f) {
        if (atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 8) {
            fprintf(stderr, "[MH-WIDE] camera 0x%08X window %.4f x %.4f (ratio %.3f) kept\n", cam, x, y, r);
        }
        return;
    }
    x *= 4.0f / 3.0f;
    wrf(cam + CAM_VIEW_WINDOW, x);
    wrf(cam + CAM_RECIP_WINDOW, 1.0f / x);
    if (atomic_fetch_add_explicit(&s_logged, 1, memory_order_relaxed) < 8) {
        fprintf(stderr, "[MH-WIDE] camera 0x%08X window %.4f x %.4f widened to %.4f\n", cam, x * 0.75f, y, x);
    }
}
