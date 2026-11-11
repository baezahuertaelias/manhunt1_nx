# Plan: cut the CPU cost of rendering — 2026-11-10

Branch base: `settings-font` at `00db7a5`. Owner priority: move work off the CPU and let the GPU carry it.

## Why

Final console run of 2026-11-10 (`game/build/hw-test/2026-11-10-audio-smon/run-final.log`), heavy scene, CPU 1020 MHz / GPU 307 MHz:

| Per frame (42–54 ms) | Measured |
|---|---|
| GPU wait (`swap`) | 2.4–2.6 ms. The GPU idles about 95% of the frame even at its lowest clock |
| GL draw submission (`gl draw`) | 17–21 ms for about 360 draws, about 50 µs per draw, mostly Mesa/nouveau CPU work |
| Game code and pushbuffer decode (`rest`) | 19–28 ms |
| Cores | 92–100% on all three; the APU thread alone takes one core |

Rendering is CPU-bound in the driver, not in the GPU. Each task below either moves work into shaders or cuts CPU work per draw. Reducing GL-thread CPU also gives the APU thread room, which helps the remaining audio underruns.

## Rules for every task

- **Build.** Switch NRO, incremental: `make -C build/switch` inside the devkitA64 image (see `scripts/game-build.sh`, switch branch). Any change to `game/patches/xboxrecomp/*.patch`, `game/function_fixes.json` or `game/manual_functions.json` invalidates the lift cache. In that case run `MSYS_NO_PATHCONV=1 CPUS=12 scripts/game-build.sh switch` (re-lift plus full compile, about 5–10 minutes). `CPUS` must not exceed Docker's limit of 12.
- **Generated code** compiles at `-O2` on Switch (`MANHUNT_GEN_OPT`). Do not change it in these tasks.
- **Tests.** Run inside `manhunt-recompiler:xboxrecomp-f0f8a46` with `--user 0`: `tests/nv2a-gl-review/run.sh`, `tests/shader-lighting/run.sh`, `tests/audio-apu/run.sh`. All must pass. Add a test for every new code path that has a host-testable contract (llvmpipe is available in the image).
- **Console runs.** Batch the tasks that are ready into one NRO and one run. The owner asked to keep manual console tests to a minimum. Deploy with nxlink from the devkitA64 image: `nxlink -a 192.168.1.107 -s /nro/manhunt.nro KEY=VALUE ...`, with port 28771 published for `-s`. Settings go as `KEY=VALUE` arguments, which `main_switch.c` applies over `manhunt.env`. Always pass `MANHUNT_PERF_LOG=2`.
- **Comparisons.** Compare only at the same CPU/GPU/EMC clocks, which every `[perf]` line logs. Use the same scene: the third fight, in the pool/court. Report medians of at least 10 `[perf]` samples above 20,000 vertices per frame, as `final-summary.json` does.
- **Rollback.** Every behaviour change gets an environment switch that restores the old path (pattern: `MANHUNT_SHADER_LIGHTING=0`). The default goes to the new path only after a console run shows it correct.
- **Claims.** Never claim a speed-up or visual equivalence without a console log or a test that shows it. Write UNVERIFIED otherwise. Record results in `reports/` with the run log path, NRO SHA-256 and clocks.
- **Scope.** Do not change audio, the APU, game logic or the main-menu crash in these tasks. They are listed under Out of scope.

## Tasks

Do G0 first, because it decides the order of G1–G3. G1–G3 can then proceed in parallel; they touch different functions of `game/src/nv2a_gl/nv2a_gl.c`. Merge them before one combined console run.

### G0 — Split the per-draw cost (measurement)

Goal: know where the about 50 µs of each draw goes before optimizing it.

- In `gl_draw_raw_work` (`game/src/nv2a_gl/nv2a_gl.c`), time these stages with `hz_now()`, accumulated into new `Nv2aGlPerf` fields (`nv2a_gl.h`):
  - program lookup (`prog_get`);
  - vertex/index upload (`upload_vertices`, `ring_alloc`/`ring_done`);
  - uniform uploads (vertex-program constants `u_c`, lighting, combiners);
  - texture bind and sampler state (`bind_stage`);
  - fixed-function state (blend, depth, cull, stencil);
  - the draw call itself (`glDrawElements`).
- Count per frame: `glMapBufferRange` calls, uniform upload calls and bytes, texture binds, and attributes the executor still converts (`attr_present & ~attr_direct`, which `upload_vertices` sees as `conv`).
- Print one extra `[perf-gl]` line per period from `perf_main` in `game/src/main_switch.c`, in ms per frame and calls per frame.
- Keep timer overhead small: a single `hz_now()` per stage boundary, no logging per draw.
- Acceptance: the stage times sum to `gl draw` within 10%; host test `tests/nv2a-gl-review` still passes.

### G1 — Vertex and index streaming with fewer maps

Goal: one buffer map per frame or none, instead of one per attribute run and index list per draw.

- Today `ring_alloc` maps `GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT` for each upload. `ring_done` unmaps each time, and orphans the buffer with `glBufferData` when the ring wraps.
- Preferred path: if `GL_ARB_buffer_storage` is present (check at start-up and log it), allocate the vertex and index rings with `glBufferStorage(..., GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT)` and map them once. Write with `memcpy`. Fence each ring segment with `glFenceSync` at flip, and wait on the fence only before reusing that segment (3 segments).
- Fallback when the extension is missing: pack all of a draw's runs and its indices into one `ring_alloc` (one map, one unmap per draw).
- Switch: `MANHUNT_GL_PERSISTENT=0` gives the old path.
- Watch: the comment in `ring_alloc` about `GL_MAP_INVALIDATE_RANGE_BIT` making nouveau allocate staging buffers. Do not add that flag. Under `RECOMP_GL_THREAD=1`, all GL calls stay on the GL thread.
- Acceptance: G0's upload time per frame drops; no new visual artefacts in the court fight and the main menu; the review tests pass, including the fenced and strict sync modes.

### G2 — Uniforms through buffers

Goal: stop re-sending up to 192 vertex-program constant rows and the 46-row lighting block with `glUniform4fv` when they change.

- Move the vertex-program constants (GLSL uniform `c[192]`, location `p->u_c` in `prog_get`) and `u_light` (`gl_lighting.h`, `nv2a_gl_vsh_lighting`) into uniform blocks. Back them with a streamed uniform buffer: G1's ring, or a separate one aligned to `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`. Bind with `glBindBufferRange` per draw only when the contents changed. Reuse the generation counters `vp_const_gen` and `vp_prog_gen` in `Nv2aRawBatch` to skip unchanged data without a `memcmp`.
- Keep std140 layout; vec4 arrays need no padding.
- Update the shader-lighting test and the program-cache key if the shader text changes. The program cache on the card (`progcache.bin`, `RECOMP_PROG_CACHE`) must not reload stale binaries: change `PROG_REC_MAGIC` (`nv2a_gl.c`) or key records by shader text.
- Switch: `MANHUNT_GL_UBO=0`.
- Acceptance: G0's uniform time per frame drops; `tests/shader-lighting` passes against `lit_color` with the uniform block; the review tests pass.

### G3 — Redundant state and binds

Goal: skip GL calls whose state is already set.

- Audit `gl_draw_raw_work` and `bind_stage` for state set on every draw: blend, depth, stencil, cull, colour mask, texture binds, sampler parameters (`TexParam` cache already exists), and `glUseProgram` (already cached).
- Add a small shadow of the GL state, reset at points where other code changes state behind it: `tex_bind_forget`, the gamma pass, clears and surface switches.
- Consider sampler objects (`glGenSamplers`), one per distinct NV2A sampler key, instead of `glTexParameteri` on textures.
- Acceptance: G0's state and bind time per frame drops; the review tests pass; no visual change in the court fight, the main menu and the settings page (the font fix depends on texture state).

### G4 — Unpack vertex formats in the vertex shader (after G0)

Goal: stop the game thread converting attributes the shader could read as stored.

- Use G0's conversion counters to find which attribute types still convert, for example packed normals (CMP), and how many vertices per frame.
- For each common type: let the executor mark it direct (`raw_direct_attrs` in xboxrecomp `src/kernel/nv2a_pb_exec.c`, through a new patch `0028-...`). Upload it as stored, and unpack it in `gl_vsh.c` with a GLSL decoder that matches `fetch_attr` bit for bit.
- Add a host test comparing the shader decode with `fetch_attr` for every value of each type, as `tests/shader-lighting` does.
- This changes a patch, so the build re-lifts.
- Switch: `RECOMP_GL_DIRECT` already exists; add a per-type switch only if needed.
- Acceptance: G0's convert count drops; `rest` per frame drops at equal clocks; tests pass.

### G5 — deko3d feasibility (research only, no code)

Goal: decide whether a deko3d back end for the in-level pipeline is worth its cost, since the driver CPU cost is the bottleneck.

- Inputs: the existing deko3d adapter (`game/src/nv2a_deko3d.c`, `MANHUNT_DEKO_*` switches), which reached about 1 flip per second in the level because most draws fell back to the CPU rasteriser. The GL back end's shader generators (`gl_vsh.c`, `gl_psh.c`) emit GLSL; deko3d needs precompiled DKSH, and `uam` would have to run at build time or on the device.
- Deliver `reports/DEKO3D_FEASIBILITY.md`: what the GL path does that deko3d would need (shader variants, render targets, swizzled textures, DXT), how shaders would be compiled, an estimate of driver CPU per draw on deko3d, and a go/no-go with a rough size. Do it after G0–G3 report their numbers.

## Combined console run (after G0–G3, optionally G4)

One NRO, one run, the same route as before. Arguments: `MANHUNT_PERF_LOG=2 RECOMP_GL_THREAD=1`. Record:

- `[perf]` and `[perf-gl]` medians in the court fight at the logged clocks, compared with the medians in `final-summary.json` (FPS 21.0, played 44,847 frames/s, 16.5 underrun events per 2 s) **only if the clocks match** (CPU 1020 / GPU 307 / EMC 1331 MHz).
- The owner's visual check of the main menu, the settings page and the court fight.
- If anything regresses, repeat with the task's switch set to the old path in the same session.

Write the result to `reports/` with the log path, NRO SHA-256 and clocks.

## Out of scope here (tracked separately)

- APU voice processing takes one full core (`vp` about 985 ms/s). It is not a GPU candidate: about 1,500 small blocks a second with guest interrupts between them make a GPU round trip too slow. It needs its own CPU optimization plan.
- The crash when returning to the main menu, after contiguous allocation failures while loading `LOAD_XBOX.TXD` and `PICLOAD_XBOX.TXD` (see `reports/PERF_AUDIO_STATUS_2026-11-10.md`).
- Battery validation with `MANHUNT_GPU_MHZ` and `MANHUNT_FPS_CAP`.
