# Performance investigation — 2026-11-16

Branch: `perf/investigation`.

**Status of the findings below: UNVERIFIED on the console.** No hardware logs were available when they were written, so the ranking comes from reading the code and from compiling one lifted chunk on the host. Console measurements and the changes made after them are in "Console runs" further down. Only code size and compile time were measured. No frame rate was measured.

Earlier reports show the guest CPU is the bottleneck: "rest" time of 26–48 ms per frame against 12–22 ms of GL draw time (`reports/GPU_OFFLOAD_*.md`, `SWITCH_RUN_2026-11-11.md`). So CPU-side items come first.

## Measurement: code size of one lifted chunk

`game/build/gen/recomp_0010.c` (one of 60 chunks), compiled with the pinned devkitA64 image and the production flags (`-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE -fno-strict-aliasing -ffp-contract=off`).

| Variant | Code size | vs. baseline |
|---|---:|---:|
| `-O2` (current) | 289,724 B | – |
| `-O3` | 291,056 B | +0.5% |
| `-O2 -fomit-frame-pointer` | 287,172 B | −0.9% |
| `-Os` | 251,532 B | −13% |
| Guest registers packed into one TLS struct | 258,460 B | −11% |
| Struct and `-Os` together | 219,268 B | −24% |

Compile time for the chunk fell from 17 s to 9 s with the struct. The whole NRO has 25.8 MB of code (5.85 M instructions, 14,827 functions, 113,810 `__aarch64_read_tp` calls) against a Cortex-A57 with a 48 KB L1 instruction cache. Whether smaller code is faster needs a console run.

## Findings, in rough order of expected gain

### 1. Ship the configuration that was measured

- Every hardware run used `RECOMP_GL_THREAD=1` and `MANHUNT_DUMP_SECS=0` (`scripts/gpu-offload-run.ps1:29,32`).
- The release `manhunt.env` (`scripts/make-release.py`, `ENV`) and the README example set neither, so players run an untested configuration.
- `config_init` in `game/src/main_switch.c` also defaults `MANHUNT_DUMP_SECS` to 15.
- The earlier A/B (`PERF_AUDIO_STATUS_2026-11-10.md`): 25.0 FPS with the GL thread on against 22.25 off, and APU load 61% against 85% of a core.
- Proposal: make GL thread on and dump off the defaults. Not checked: whether the 15 s dump does any work with the GL backend.

### 2. Pack the guest registers into one TLS struct

- `eax`, `ecx`, `edx`, `esp`, `ebx`, `esi`, `edi` and `g_seh_ebp` are separate `__thread` globals (`recomp_types.h`).
- With `-mtp=soft` every function calls `__aarch64_read_tp` and saves 10 callee-saved registers just to hold TLS addresses.
- With one struct (`g_regs.r_esp` and so on) a function saves 2 registers and uses plain offset loads.
- Struct member names must not be `eax` etc., because the generated code `#define`s those names.
- Needs a patch for the header and for the definitions in the runtime, then a re-lift and the existing test suites.

### 3. Try `-Os` for the generated code

- −13% size on its own; −24% together with item 2.
- Cheap to test: build two NROs and compare on the console.

### 4. Indirect-call path

- About 3,600 `RECOMP_ICALL_SAFE_AT` / `_CC` sites in the lifted code.
- Each call writes three shared volatile globals (`g_icall_trace`, `g_icall_trace_idx`, `g_icall_count`). All threads write them, so their cache lines bounce between cores.
- Each call may do three cross-file lookups: `recomp_lookup_manual`, `recomp_lookup`, `recomp_lookup_kernel`.
- The flat dispatch table is 8 bytes per byte of guest code (about 21 MB for a 0x295328 span), scattered over 4 KB pages.
- Proposal: compile out the trace, prefill manual and kernel entries into one compact table (about 15k entries), keep one inlineable lookup.

### 5. Load the guest memory base once per function

- The generated code reloads `g_xbox_mem_offset` (through the GOT) before nearly every guest access, because volatile stores force the reload.
- Option A: a post-lift step inserts `const uintptr_t _mb = g_xbox_mem_offset;` at the top of each function and `XBOX_PTR` uses it. No lifter change.
- Option B: a reserved register (`-ffixed-x28`). Riskier: thread entry points and callbacks would have to set it.

### 6. Audio

- Voice processing was about 760 ms per second (76% of a core) after the HRTF change (`SWITCH_RUN_2026-11-11.md`).
- Ideas: NEON for the HRTF and voice mixer; explicit core pinning (guest core 0, GL core 1, APU core 2).
- Threads are currently placed round-robin with a core mask of 7 (`nx_spread_thread`, patch 0003), so they can migrate. Pinning needs a console A/B.

### 7. GL renderer

- About 440 texture binds, 248 uniform uploads, 3 ms vertex upload and 6 ms draw submission per frame in heavy scenes (`GPU_OFFLOAD_RESULTS.md`).
- Ideas: skip redundant texture binds, merge compatible draws, ship a prebuilt shader program cache for first-use stutter.
- Rendering is already at native 640x480 scale (`RECOMP_GL_SCALE` = 1), so lowering resolution will not help.
- A deko3d rewrite was already judged no-go (`DEKO3D_FEASIBILITY.md`).

### 8. Minor

- Guest memory accesses must stay `volatile` where the code polls for another thread. Dropping `volatile` only for stack accesses would need a lifter change.
- Turn off the perf thread, watchdog and `MANHUNT_LOG_SYNC=1` in play configurations.
- `MANHUNT_FPS_CAP=30` helps battery, if pacing is acceptable.

## How to settle the ranking

Build with `-DMANHUNT_CALL_PROFILE=ON` (`game/src/call_profile.c`) and run one heavy scene. The top guest functions show whether items 2 to 5 matter and whether any single function is worth replacing by hand.

## Console runs, 2026-11-16 and 2026-11-17

Two play sessions with `scripts/perf-play-run.ps1` (nxlink, `MANHUNT_PERF_LOG=2`, `RECOMP_GL_THREAD=1`, `MANHUNT_DUMP_SECS=0`, `MANHUNT_LOG_SYNC=0`). Clocks were CPU 1020, GPU 307, EMC 1331 MHz in both, on battery. The game did not set them. Evidence: `game/build/hw-test/perf-play/20261116-233847` (6 min, intro and gameplay) and `20261117-000136` (44 s, intro only).

### Session 1: where the time goes

- **Movies.** The intro and cutscene movies ran at 16–17 FPS with only 2 draws per frame. Each frame uploads one 640x480 texture in format `0x24` (4:2:2 YUV). It went through the backend sampler one texel at a time, then through a new `glTexImage2D`: about 57 ms per frame. That was 500 of the 553 hitches.
- **Gameplay is bound by draw submission.** GL draw time grows with the number of draws, at about 0.028 ms per draw:

  | Draws per frame | GL draw | FPS |
  |---:|---:|---:|
  | 100–230 | 5–8 ms | 60–75 |
  | 340–530 | 12–17 ms | 40–50 |
  | 680–700 | 19–21 ms | 36–38 |
  | 1,180 | 32 ms | 24 |

  At 690 draws the 19.6 ms splits into: submit (`glDrawElements` alone) 8.2, vertex upload 4.2, uniforms 2.6 (226 calls), texture binds 2.6 (675 binds), other 2.0. "Rest" was only 2–5 ms in gameplay.
- **All three cores are saturated** (92–97%): guest 75–97% of a core, GL thread 45–73%, APU 76–87%.
- **Indirect calls (item 4) are not worth the work.** There were 3,300–5,500 per frame in gameplay and 11–14k during loads. At an assumed few tens of ns each, that is about 0.2 ms per frame.
- **Loading** shows "rest" at 53–56 ms with the GL thread at 4%. That is level loading on the guest thread.

### Changes made after session 1

- `nv2a_gl.c`: a direct fast path for linear `0x24`/`0x25` (YUY2/UYVY). It uses the same BT.601 integer maths as the sampler and uploads into the texture's existing storage. On the host it matched the sampler exactly for all Y/U/V combinations (67,108,864 texels, 0 different).
- `[perf]` now prints `icalls`: indirect guest calls per frame, read from `g_icall_count`.
- `[perf-gl]` now prints `samestate`: draws per frame where nothing but vertex data changed since the previous draw (same program, uniforms, textures and their contents, sampling, fixed state, framebuffer, constant attributes, surface, primitive, index size and vertex layout). These are the draws that merging could combine. Counted only under `MANHUNT_PERF_LOG`.
- `scripts/perf-play-run.ps1`: sends the NRO with logging on and splits `perf.log` (`[perf]`, `[perf-gl]`, `[hitch]`) from `run.log`. `-CallProfile` uses the `switch-prof` build.

Host: all 13 GL suites in `tests/gpu-offload/run.sh` pass. The audio suite does not compile: `recomp_types.h` (generated) has an unused variable `flush` under `-Werror`. These changes do not touch that file.

### Session 2: movie fix verified on the console

| Intro movie | Before | After |
|---|---:|---:|
| FPS | 16–17 | 60 |
| Texture work per frame | ~57 ms | ~4 ms |
| Hitches | ~500 over the movies | 24 in 44 s |

Session 2 ended in the intro, so `samestate` is still unmeasured in gameplay.

### Session 3: heavy gameplay, docked (2026-11-17)

Launched from the SD card with `MANHUNT_PERF_LOG=2`, `RECOMP_GL_THREAD=1` and `MANHUNT_DUMP_SECS=0` added to `manhunt.env` (the original is kept as `manhunt.env.pre-perf`). The run lasted about 11 min, 322 `[perf]` periods, charging. Clocks were CPU 1020, GPU 768, EMC 1600 MHz throughout. Evidence: `game/build/hw-test/perf-play/20261117-114928-sd`.

- **`samestate` was 0.0 in every period**, menus included. No draw left GL state exactly as the previous draw did. Every change to the counter is guarded by an actual change (the state-cache early-out, the per-uniform compares, the constants UBO keyed by `vp_const_gen`). So the most likely cause is that each object draw brings its own vertex constants, such as the transform matrices. Merging consecutive draws as they are would combine nothing. Merging would need each draw's constants in one buffer, indexed per draw: a much larger change.
- **A faster GPU did not help.** At 2.5x the GPU clock and a faster memory clock, frame rate at a given draw count was no better than session 1 at 307 MHz:

  | Draws per frame | Periods | FPS | GL draw | Swap |
  |---|---:|---:|---:|---:|
  | 100–299 (avg 173) | 160 | 67.8 | 7.3 ms | 1.5 ms |
  | 300–599 (avg 427) | 125 | 40.9 | 14.6 ms | 1.7 ms |
  | 600+ (avg 705) | 32 | 31.1 | 21.8 ms | 1.6 ms |

  The cost is still about 0.03 ms of CPU per draw. Gameplay is limited by the CPU: the driver's per-draw work on the GL thread, the guest and the APU each take most of a core, at 1020 MHz.

### Changes made after session 3 (commit `d50e5fc`)

- **Kept attribute pointers.** An interleaved draw (one source run, one stride, nothing converted) with the same layout as the previous draw is placed a whole number of vertices after it in the same ring buffer. It keeps the attribute pointers and draws with `glDrawElementsBaseVertex`, so Mesa has no vertex arrays to validate again. The pointers are set again when the layout changes, the buffer is replaced, or the ring wraps to an earlier offset. `MANHUNT_GL_BASE_VERTEX=0` restores the old path. `[perf-gl]` counts these draws as `kept`.
- **Same-content constants.** `vp_const_gen` moves on every constant write, including rewrites of the same values. Constants identical to the bound range are no longer uploaded again (3 KB each). `[perf-gl]` counts these as `constreuse`.

Host: all GL suites and the llvmpipe rendering suite pass. A new streaming contract covers placement, the base vertex, re-pointing on a layout change, and ring wrap. **Not yet measured on the console.**

## Decisions

- **The CPU clock stays at the system's setting** (owner, 2026-11-17). The port does not raise it. `MANHUNT_CPU_MHZ` and `MANHUNT_GPU_MHZ` can only lower clocks, and that stays as is. Performance work has to reduce CPU work instead.

### Open

1. **Measure `d50e5fc` on the console** in the same heavy scene as session 3. Compare the `kept` and `constreuse` counts and the upload, uniform and submit times.
2. **Other per-draw CPU cost in the driver.** `glDrawElements` is still about 8–10 ms a frame in heavy scenes.
3. **Audio (item 6)** still uses most of a core (APU 84–89% in session 3). Ideas: NEON for the HRTF and the voice mixer, and core pinning.

## Not covered here

Raising the CPU/GPU clock was discussed separately and is not part of this report.
