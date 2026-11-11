# GPU offload G3: state and binding audit

Branch: `codex/gpu-offload`. Console results belong in the combined GPU offload report; hardware performance and visual acceptance are **UNVERIFIED** here.

## Existing behavior

The renderer already tracks the current program, per-unit texture bindings, active texture unit, texture-object sampler parameters, and enabled vertex arrays. Fixed-function state was cached as one `GlState`: an identical draw made no state calls, but changing one field re-applied every field. Surface creation/switching, clear callbacks and gamma presentation invalidate the shadow through `state_dirty()`. Texture uploads invalidate binding knowledge through `tex_bind_forget()`. The gamma pass modifies a source texture's filtering and calls `tex_param_forget(src)`, which is necessary for the settings font and render-to-texture reuse.

## Changes

`MANHUNT_GL_STATE_CACHE=1` enables incremental fixed-function updates. Blend, depth, stencil, cull, color mask and scissor only submit the fields that changed. Re-enabling a disabled group restores its parameters, and `state_dirty()` forces a complete restoration. This path honors the computed `RECOMP_GL_NO_CULL` state; the legacy submission path ignored that setting despite recording it in the shadow.

Disabled texture stages already known to bind zero no longer cycle the active texture unit. The draw path also omits a redundant VAO bind: renderer initialization selects `s_vao`, and the only other production VAO user, the gamma pass, restores it before returning.

The switch defaults off pending the combined console run. `MANHUNT_GL_STATE_CACHE=0` retains the original submission path and original caching. No sampler objects are introduced. Existing sampler parameter lookup and invalidation remain in use.

## Host evidence

- `tests/nv2a-gl-review/run-state-cache.sh`: passes for legacy and incremental modes. The production implementation is included with synthetic GL counters. Repeated state emits zero calls in either mode; an isolated color-mask, depth-function or stencil-op change emits one call in incremental mode. It also tests dirty restoration, blend disable/change/re-enable, disabled-stage binding reuse, binding invalidation and texture identity reuse.
- `MANHUNT_GL_STATE_CACHE=1 tests/nv2a-gl-review/run.sh`: passes strict, fenced and GPU contract modes in `manhunt-recompiler:xboxrecomp-f0f8a46` as root. Log: `game/build/gl-review-tests/g3-review.log`.

These synthetic tests establish call-elision and invalidation contracts, not visual equivalence, FPS or Switch driver cost. Main menu, settings font and court-fight checks, and G0 state/bind timings, remain part of the combined hardware acceptance run.
