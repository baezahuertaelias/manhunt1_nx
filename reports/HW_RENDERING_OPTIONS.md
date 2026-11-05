# Rendering speed: options and a recommendation (2026-11-02)

Status: **analysis from code and the measured profile, written before any console test of the adapter.** Console results came on 2026-11-04: [HW_DEKO_ADAPTER_RUNS.md](HW_DEKO_ADAPTER_RUNS.md). Inputs: [HW_PERFORMANCE_FIRST_LOOK.md](HW_PERFORMANCE_FIRST_LOOK.md) (measured), `game/src/nv2a_deko3d.c`, `game/build/xboxrecomp/src/kernel/nv2a_pb_exec.c`.

## The problem, in numbers

The main menu runs at about 0.35 to 0.65 frames per second; the CPU software rasteriser is 98% of every frame (about 87 ms per draw, about 1.2 us per pixel, single-threaded). A button press takes about 5 s to show. Beta 1 needs 30 frames per second in the first level, a factor of 50 to 85 from here.

## Why a pixel is slow (read from the code)

`xf_rows` visits every pixel of each triangle's bounding box (about half the pixels of a large triangle lie outside it), recomputes three floating-point edge functions per pixel, then does two divisions, a `floorf`, a generic texture sample, a generic blend and a generic pixel write. It is a correctness-first design with no scanline stepping, no per-triangle specialisation and no SIMD.

## Options

| Option | Expected effect | Cost and risk | Gets to 30 FPS gameplay? |
|---|---|---|---|
| A. Turn on the existing deko3d adapter (`MANHUNT_GPU_BACKEND=1`) | Menu draws it accepts run on the GPU (7x faster in Eden even with an emulated GPU; real figure UNVERIFIED) | One config change and one hardware run. It handles only a menu subset: draws with vertex programs or register combiners are rejected and fall back to the CPU renderer | No: gameplay draws would still be CPU |
| B. Speed up the CPU rasteriser (scanline setup, per-state fast paths, a worker thread) | Plausibly 3 to 5x on large fills (a guess; not measured) | Medium work in a large third-party file (patch); at most about 2 cores are usable | No: even 5x leaves about 3 frames per second |
| C. A real GPU renderer for the NV2A (vertex programs to shaders, register combiners to fragment shaders, texture formats, render targets) | The only route to 30 FPS | Large, staged work; needs a plan and measured milestones. The adapter is the first stage | Yes (the only option that can) |
| D. Lower internal resolution | About 4x fewer pixels | The guest reads and writes the surface at a fixed size; a visual change that needs a switch and review | No |

## Offline audit of the adapter (A)

Checked against the four hardware pitfalls the sibling port documented (finding 127, "Bugs found on the hardware"):

- flush after every submit: **yes** (`dkQueueSubmitCommands`, `dkQueueFlush`, `dkQueueWaitIdle` in one line, fully synchronous);
- command buffer rewind bug: no `dkCmdBufClear` is used;
- image views re-initialised at use: **yes** (`dkImageViewDefaults` per use);
- shader dynamic vector indexing: **none found** in the three shaders.

It is a conservative, synchronous design. It logs `[MH-DEKO] flip=... draws=... reject_draw=...` so one run shows how much of the menu it accepts. Not checked: descriptor and memory-block limits under load, and anything about real GPU timing.

## Recommendation

1. **Do A first, as one hardware run**: `scripts/hw-run.sh --allow-gpu-backend` with `--profile`. It answers three things at once: how much faster the menu gets, what fraction of draws the adapter accepts (`reject_draw`), and whether the console stays up. No other test is needed before that.
2. **Then decide between B and C** with that number in hand. A mixed plan is likely: C as the main line (extend the adapter stage by stage, measuring accepted draws per stage), B only if a cheap fast path covers draws C will not reach soon.
3. Hardware runs only when the question needs the console; logic goes into host tests only when it can be run there.

## Not known

The real speed of the adapter on the console, how much of the menu it accepts, which draws the first level needs (vertex programs and combiners are the likely blockers), and the clocks and thermals in handheld mode.

Correction (2026-11-04): option A above and this section name vertex programs as a likely blocker. Code reading shows that `can_draw` accepts `NV2A_DRAW_PROGRAM`, while `state_ok` refuses any depth test, depth write, stencil or culling, so depth state is the stronger suspect for in-level refusals. That is still a hypothesis until the reject counters run (HW-PERF-002).

Update (2026-11-04): the counters ran, and depth alone unlocks nothing (`only` = 0). In the steady in-level windows about 89% of refused draws fail `surface`, about 84% `depth`, about 86% `cull` and about 84% `shading`; the combinations are not recorded. Figures: [HW_DEKO_ADAPTER_RUNS.md](HW_DEKO_ADAPTER_RUNS.md).

## Implemented on branch `deko-adapter-perf` (built and run on the console 2026-11-04; not merged)

After the adapter's own profile (about 44 ms per draw: full-surface round trip through uncached memory, 6 to 8 memory blocks and the shader files recreated per draw, a per-texel texture decode) the following was written, each piece behind a config switch. When written, each defaulted on with `=0` off. **Since 2026-11-04 the resident render target and the `KeQuerySystemTime` yield default off, with `=1` on; the others are unchanged** ([CHANGELOG_CONFIG.md](../docs/CHANGELOG_CONFIG.md)):

| Piece | What it removes | Hazard and how it is handled |
|---|---|---|
| persistent memory blocks and shaders | the per-draw allocations and file reads | the queue is idle when a block grows (every call ends with `dkQueueWaitIdle`) |
| CPU-cached transfer buffer + `dkMemBlockFlushCpuCache` | the uncached per-pixel reads | libnx defines the flush as clean + invalidate; flushed after the CPU writes and again after the GPU wrote |
| rectangle transfers | whole-surface copies for small draws | scissor and copy limited to the vertex bounding box plus 2 px |
| texture cache | decoding the same texture for every draw | keyed by address and format; a 64-bit hash of every byte the decode reads (texels, and the palette for P8) is checked every draw |
| resident render target | the per-draw upload and readback (about one upload and one to three readbacks per frame instead) | guest RAM is synced where the title can see the GPU as done (pushbuffer GET published, semaphore release, report write: patch 0018 adds a backend `sync` hook), before the present reads it (the runtime presents before it calls the backend's flip, so that hook is in our present function), before any draw or clear the CPU renderer takes, before a texture overlapping the surface is decoded, and when the surface changes. NOT covered: the guest CPU reading or writing the surface mid-frame without any GPU completion signal, which verify cannot observe; so `MANHUNT_DEKO_RESIDENT` is opt-in, `=1` |
| faster present | a division per pixel (8 ms) | column table and row copies; same pixels |
| `KeQuerySystemTime` yield (patch 0017) | a core at 100% in the guest's wait loop | sleeps about 20 us every 64th call, before the time is read; how close a wait ends to its deadline is not measured; it applies to every Switch build and changes guest timing, so `RECOMP_TIME_YIELD` is opt-in, `=1` |

**Verification without guessing:** `MANHUNT_DEKO_VERIFY=1` runs every call also through the conservative path (whole surface, uncached buffer, no texture cache, nothing resident) on a host shadow and compares the two results exactly (after each call, or at each sync for the resident target). A frame-to-frame comparison is not usable: the menu background animates (measured: 96% of pixels differ between consecutive frames of an unchanged menu). A mismatch is then bisected by turning single pieces off by config, without a rebuild.

**Expected, not measured:** per frame about one upload, one readback and about 27 small GPU submits, which should be tens of milliseconds, against 1.2 s now. If the numbers say otherwise, the `[MH-DEKO-T]` log shows where the time goes.

### Pre-build review (separate agent, read-only)

No console-safety blocker. It found two flaws that would have produced a **false pass** or a silent difference, both fixed before any build: the verify mode's conservative pass shared GPU blocks with the resident path and would have overwritten the resident image (so the resident comparison could never fail); and the guest-visible completion signals (GET, semaphore, report) did not sync. Smaller items fixed: a stale-surface path after a failed batch preparation, an unbounded texture cache, a missing L2 invalidate, a buffer-mode recreation loop, a stale time from the clock-polling yield, and an unsynced read in the standalone menu proof test. Not fixed by design: diagnostic dumps that read RAM unsynced (documented).

### Result on the console (2026-11-04)

On the main menu with the defaults under review (resident target and yield off): about 28.85 flips per second, 28 draws per flip. With both on, on an older build: 39 flips per second over the whole log (25.6 ms per flip), about 43.6 in steady windows; the two pieces are not isolated. `MANHUNT_DEKO_VERIFY=1` found 0 differences in 545 comparisons on the menu. In the first mission, in both configurations: about 0.19 flips per second, with 68% to 69% of the draw calls reaching the CPU rasteriser and the rasteriser taking about 97% of the draw time. Verify in the level found 0 differences but covers very few draws and is not a level-wide pixel-identity pass; gameplay stays UNVERIFIED. So this stage does not reach the 30 FPS gate in the level, and extending the adapter (option C) continues as task HW-PERF-002. Figures and limits: [HW_DEKO_ADAPTER_RUNS.md](HW_DEKO_ADAPTER_RUNS.md).

