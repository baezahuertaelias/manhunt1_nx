# Visual bugs and performance opportunities — 2026-11-17

Branch: `perf/investigation`.

**Status: UNVERIFIED on the console.** Everything below comes from reading the code. Nothing was run, measured or changed. Gains are estimates. Each item names the console test that would settle it.

Two problems prompted this review, both reported by the owner from play:

- The player character is sometimes too dark.
- Near a wall, the camera sometimes shows what is inside or behind it.

## Part 1: visual bugs

Ordered by how likely each one is to cause something visible in play.

### 1. Walls turn see-through near the camera (per-vertex depth clamp)

- `game/src/nv2a_gl/gl_vsh.c:415-419`, the OpenGL `nv2a_clip`, clamps depth per vertex: `clamp(z, -abs(w), abs(w))`.
- When the camera is close to a wall, some of the wall's vertices lie beside or behind the eye. The clamp changes their depth, and the interpolated depth across the triangle comes out deeper than the true surface. Objects behind the wall then pass the depth test and draw over it.
- The Vulkan path already documents and fixes this exact bug (`gl_vsh.c:319-336`): "walls in hood view turned transparent, objects showed through them" and "Any clamp per vertex bends the depth plane". It leaves z unclamped when depth clamp is available (`s_vk_clip_free`).
- The GL path already enables `GL_DEPTH_CLAMP` (`nv2a_gl.c:2133`, `:2191`). The per-vertex clamp was added for an emulator (Eden) that ignored depth clamp.
- **Fix:** return plain `z` in the GL `nv2a_clip`, as `s_vk_clip_free` does, at least when depth clamp is supported.
- **Contributing factor:** the release `manhunt.env` sets `MANHUNT_WIDESCREEN=1` (`scripts/make-release.py:33`), which widens every 4:3 camera by 4/3 (`game/src/widescreen.c`). The game's camera collision was tuned for the 4:3 frustum, so the wider frustum's edges reach further past walls.
- **Test:** stand against a wall with the fix, then again with `MANHUNT_WIDESCREEN=0`.

### 2. Fixed-function geometry is never fogged

- The fixed-function vertex path always writes `vFog = 0.0` (`gl_vsh.c:538`). The runtime has no fog-coordinate generation (no `SET_FOG_GEN_MODE` handling in `nv2a_pb_exec.c`).
- With linear fog, the factor at distance 0 is `fogp.x - 1` (`gl_psh.c:295`), which for the usual D3D parameters is above 1 and clamps to "no fog". So the world, which is drawn fixed-function, is never fogged.
- Vertex-program draws write `oFog`, so skinned characters are fogged. With a dark fog colour, characters look darker than the world around them. **This is a possible cause of the dark player.**
- **Fix:** compute the eye-space fog distance in the fixed-function vertex shader (eye z or radial distance per `SET_FOG_GEN_MODE`), as xemu does.

### 3. The ambient boost reaches the world only

- `ambient_level()` defaults to `0.33f` (`nv2a_gl.c:235`) and replaces the scene ambient colour in `light_uniforms` (`nv2a_gl.c:251-252`).
- It only affects fixed-function lit draws. Characters are skinned through the game's own vertex programs (`skinxboxrender.c`, `reports/NPC_PERFORMANCE_2026-11-11.md:27`) and keep the game's lighting constants.
- In dark areas the world is lifted to 0.33 and the player is not, so the player stands out as too dark. **Second likely cause of the dark player.**
- **Test:** `MANHUNT_AMBIENT_LIGHT=-1` (keep the game's ambient), then compare player and world brightness. The real fix is either no override, or the same lift applied to the characters' light constants.

### 4. Texture border colour is never set

- `wrap_mode()` maps address mode 4 to `GL_CLAMP_TO_BORDER` (`nv2a_gl.c:1270`), but `SET_TEXTURE_BORDER_COLOR` is never passed to GL, so the border is GL's default transparent black.
- The CPU renderer handles it and warns why (`nv2a_pb_exec.c:1267-1271`): "Projected shadow maps rely on it ... the shadow's frustum is drawn as a dark wedge".
- Expected symptom: dark wedges or rectangles on surfaces around characters wherever a projected shadow is drawn.
- **Fix:** set `GL_TEXTURE_BORDER_COLOR` from the stage's border colour register in `bind_stage` (and include it in the sampler-state cache key). Honour the format's border-source bit as the CPU path does (`nv2a_pb_exec.c:4136`).

### 5. No mipmaps

- Only level 0 of each texture is uploaded (`nv2a_gl.c:1165`, `:1190`, `:1240`). The min filter is only `GL_NEAREST` or `GL_LINEAR` (`nv2a_gl.c:1351-1352`); the mipmap filter modes (3–6) are ignored.
- Symptom: distant floors and walls shimmer and show moiré patterns.
- **Fix:** upload the mip chain (count in format bits 16–19) and map the mipmap min-filter modes.

### 6. Spot lights are lit as point lights

- The fixed-function lighting (`gl_vsh.c:488-499`, mirroring `lit_color` in `nv2a_pb_exec.c:1899`) treats light kind 3 (spot) like a local light. The spot direction and falloff registers are never read (`gl_lighting.h`).
- Symptom: flashlights and lamps light in all directions instead of a cone.

### 7. No texture-coordinate generation or texture matrices

- The fixed-function path passes raw texture coordinates (`vT0 = v9 ...`, `gl_vsh.c:538`). The runtime has no texgen or texture-matrix code.
- RenderWare's environment maps use texgen plus a texture matrix on the fixed-function path, so reflections and projected textures will land in the wrong place.

### 8. No depth bias

- `apply_state` (`nv2a_gl.c:2061`) never calls `glPolygonOffset`. The NV2A polygon-offset enable and scale/bias registers are ignored.
- Symptom: decals (blood, bullet holes, shadows) flicker against the surface beneath (z-fighting).

### 9. Cube maps read black; bump and dot-product modes sample as 2D

- `gl_psh.c:268-282`: cube maps return black on GL; modes 2 and 6–14, 17 fall through to a plain 2D sample.
- In the default `gpu` sync mode the support check is skipped (`gl_psh.c:351`), so these draw wrong instead of being rejected.
- `RECOMP_GL_TRACE=1` logs the texture modes a scene uses (`nv2a_gl.c:3042-3063`).

### 10. An all-zero gamma ramp is ignored

- `xbox_gamma_ramp` returns `set && !identity` (`game/src/xbox_gamma.c:59`). An all-zero ramp, as at the end of a fade to black, counts as "never set", so the frame is shown at full brightness.
- Symptom: a bright flash at the end of a fade, if the game fades through the gamma ramp.
- **Fix:** track "ever set" separately (for example, the ramp index or a non-zero device ramp seen once) instead of inferring it from the contents.

### 11. Widescreen HUD anchoring is decided per draw

- `wide_2d` anchors each 2D draw to the left edge, right edge or centre by that draw's own centre point (`nv2a_gl.c:2840-2844`). A HUD element made of several draws can come apart.
- "In level" is decided from the previous frame's draw count (`nv2a_gl.c:2783-2789`). A menu drawn over a live level (50 draws or more) counts as in level, so a pause menu may be pushed out to the screen edges.
- **Test:** `MANHUNT_WIDE_HUD=0`.

### 12. Widescreen full-screen test ignores anti-aliasing

- `nv2a_gl.c:2818` compares the draw's x range against `s->w`, the stored width including the AA factor, while xform 0/2 positions are in title pixels. The edge thresholds below already correct for this (`:2836-2839`); the full-width test does not.
- With AA on, full-screen fades and film grain would be squeezed to 4:3.

### 13. Presentation shows the last surface drawn

- `gl_flip_work` presents `s_last`, the last surface drawn or cleared (`nv2a_gl.c:3598`), not the surface the title flipped.
- If the title renders to an offscreen target late in a frame, that target is shown for a frame. Low likelihood.

### 14. Minor

- The alpha-test "equal" comparison uses unrounded alpha (`gl_psh.c:432-436`), so it rarely passes.
- `nv2a_lit` returns 0 for `pow(0, 0)`, where the hardware returns 1 (`gl_vsh.c:315`).
- Fixed-function normals go through the model-view matrix, not its inverse transpose, and only matrix 0 is used (no fixed-function skinning). Exact for rotations and uniform scale.

### Quick console tests

| Items | Setting or change |
|---|---|
| 2, 3 | `MANHUNT_AMBIENT_LIGHT=-1`, then compare player and world brightness in fog |
| 1 | Plain `z` in the GL `nv2a_clip` (`gl_vsh.c:419`), then stand against a wall |
| 4 | `RECOMP_GL_WATCH=<shadow texture va>` to log the stage's address mode and format |
| 11, 12 | `MANHUNT_WIDESCREEN=0`, and `MANHUNT_WIDE_HUD=0` |

## Part 2: performance opportunities

Not repeated here: the items already in `reports/PERF_INVESTIGATION_2026-11-16.md` (TLS register struct, `-Os`, indirect calls, memory base, kept attribute pointers, constant reuse). The CPU clock stays at the system setting (owner decision, 2026-11-17), so all items reduce CPU work.

### P1. Ship the measured configuration (still open)

- `RECOMP_GL_THREAD=1` is not in `config_init` (`game/src/main_switch.c:1034`) or in the release `manhunt.env` (`scripts/make-release.py:30`).
- Every console run used it. The earlier A/B measured 25.0 FPS with it against 22.25 without (`reports/PERF_AUDIO_STATUS_2026-11-10.md`).
- `MANHUNT_DUMP_SECS` defaults to 15, but the dump does nothing with the GL backend: `xbox_FramebufferDumpBmp` returns at once without its software framebuffer (`fb_present.c:406`). Only the 100 ms wake-ups remain.

### P2. APU: resolve each voice's register block once per frame (exact)

- `voice_get_mask` (`apu/apu_vp.c:110`) calls `mcpx_apu_phys()` for every field. That function is out-of-line and does up to two page-map lookups through further calls (`apu/apu_core.c:116-156`).
- Each active voice reads 50 to 80 fields per audio frame, at 1,500 frames per second.
- A voice's 0x80-byte block never crosses a 4 KB page, so the host pointer can be resolved once per voice per frame and the fields read directly.
- Estimate: about 2 million translations per second saved with 30 active voices.

### P3. APU: resolve PCM sample addresses once per page (exact)

- `voice_get_samples` (`apu_vp.c:1243-1262`) resolves the SGE entry and then the data address for every sample on every channel.
- Samples within one 4 KB page are contiguous: resolve the host pointer when the page changes and step through it.
- Non-streamed ADPCM does the same per 32-bit word (`apu_vp.c:1218-1225`).

### P4. APU: keep the decoded ADPCM block between calls (exact)

- The decoded-block cache `adpcm_block_index` is a local (`apu_vp.c:1117`), lost after every call.
- One audio frame takes about 35 source samples and a block holds 64, so the block carried over from the previous frame is decoded again.
- Keeping the decoded block in the voice's filter state removes roughly half of the ADPCM decode work. Xbox sound effects are usually ADPCM.

### P5. APU: skip mix bins with zero gain (exact)

- The mix loop (`apu_vp.c:1597-1615`) runs 32 multiply-adds for each of 8 bins even when the bin's gain is 0.
- Skip a bin when both the ramp start `g0` and the target `g` are 0. The output is identical.

The APU work buckets from patch 0027 give the before/after for P2–P5 on the console.

### P6. Wake the GPU-acknowledge thread on kick instead of every 200 µs

- `nv2a_ack_thread` (`xbox_memory_layout.c:1451`) runs its ticks and `usleep(200)` forever: about 5,000 wake-ups per second, a fourth busy thread on three usable cores that are already 92–97% busy.
- Proposal: the kick replacement (`game/src/d3d_kickoff.c`) signals the thread when it writes PUT, and the thread sleeps longer (about 1 ms) when PUT equals GET.
- This also removes up to 200 µs of latency before a kicked segment starts executing.
- Estimate: a few percent of a core. It touches frame pacing (flip holds, `KeTickCount`, the counter mirrors), so it needs a console A/B.

### P7. Renderer: one vertex copy instead of two

- With the GL thread on, vertex data is copied guest memory → queue record on the executor thread (`q_draw`, `nv2a_gl.c:4175`), then queue → vertex ring on the GL thread (`upload_vertices`, `nv2a_gl.c:2593`).
- The executor could write straight into the persistently mapped ring, removing one copy. Upload costs 4–6 µs per draw today (session 4).
- Medium-sized change: ring ownership and fencing move across threads.

### P8. Renderer: narrow indices once

- `raw_batch` rebases indices but keeps them 32-bit (`nv2a_pb_exec.c:3433`). The queue copies 4 bytes each, and the GL thread narrows them to 16-bit (`nv2a_gl.c:2604-2606`).
- Narrowing in `raw_batch` when the vertex count allows halves the index bytes and removes one pass. Small gain.

### Checked and not worth it

- Program lookup is already cached by generation and by the previous draw (`prog_get`, `nv2a_gl.c:1757-1770`).
- The texture content hash runs once per frame per texture (`tex_get`, `nv2a_gl.c:1084`).
- Log output in the draw path is bounded.

## Suggested order

1. P1 (configuration only).
2. Visual fixes 1, 2, 4, 5 and 10 (small and local), then the tests above for 3 and 11/12.
3. P2–P5 (APU runtime, about 50–100 lines, checkable on the host for bit-identical output).
4. P6, P7 (need console A/B).

## Console results — 2026-11-18

Three scripted runs of the same NRO (`54fc058f…`, commit eb187e0), same pad script (intros skipped with A, then walking, camera turns and walking into walls in the first level), `MANHUNT_AMBIENT_LIGHT=-1`, a GL frame every 300 frames, auto exit at 240 s. Clocks in all runs: CPU 1020, GPU 307, EMC 1331 MHz. Logs and frames: `game/build/hw-test/2026-11-18-auto-test{,-off,-nomip}/`.

| Run | Switches | Median FPS (clean samples, >20k verts) | µs per draw | APU voice processing | APU thread | Underruns |
|---|---|---|---|---|---|---|
| all on | defaults | 40.4 | 32.6 | 600 ms/s | 69% | 0 |
| all off | every rollback switch | 43.0 | 31.1 | 740 ms/s | 81% | 0 |
| no mip | `MANHUNT_GL_MIPMAPS=0` | — | — | — | — | 0 |

- **P2–P5 (APU):** voice processing 19% lower over the whole run. Confirmed.
- **FPS:** not comparable between runs: the scripted path drifts, so the scenes differ (365 against 402 draws per frame). GL cost per draw is unchanged within noise.
- **Mipmaps (item 5): wrong.** At frame 3600, where the runs are at the same place, the player is 7.0 (all on) against 8.3 (all off and no mip), and the upper scene 13.0 against 17.0 and 16.8. With mipmaps off, every other fix matches the all-off run. Mipmaps are now off by default (`MANHUNT_GL_MIPMAPS=1` turns them on, commit 54adcb6) until the darker levels are explained.
- **Items 1, 2, 4, 8, 12, 14:** no visible harm in these runs. Wall see-through (item 1) is not judged: it needs a camera placed against a wall by hand.
- The dark player remains with every fix on and with every fix off, with the game's own ambient (`MANHUNT_AMBIENT_LIGHT=-1`). Item 3 still needs a comparison against the default ambient.
