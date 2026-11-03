# Rendering speed: options and a recommendation (2026-11-02)

Status: **analysis from code and the measured profile; nothing here is tested on the console.** Inputs: [HW_PERFORMANCE_FIRST_LOOK.md](HW_PERFORMANCE_FIRST_LOOK.md) (measured), `game/src/nv2a_deko3d.c`, `game/build/xboxrecomp/src/kernel/nv2a_pb_exec.c`.

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

## Implemented on branch `deko-adapter-perf` (not built, not run)

After the adapter's own profile (about 44 ms per draw: full-surface round trip through uncached memory, 6 to 8 memory blocks and the shader files recreated per draw, a per-texel texture decode) the following was written, each piece behind a config switch (default on, `=0` off):

| Piece | What it removes | Hazard and how it is handled |
|---|---|---|
| persistent memory blocks and shaders | the per-draw allocations and file reads | the queue is idle when a block grows (every call ends with `dkQueueWaitIdle`) |
| CPU-cached transfer buffer + `dkMemBlockFlushCpuCache` | the uncached per-pixel reads | libnx defines the flush as clean + invalidate; flushed after the CPU writes and again after the GPU wrote |
| rectangle transfers | whole-surface copies for small draws | scissor and copy limited to the vertex bounding box plus 2 px |
| texture cache | decoding the same texture for every draw | keyed by address and format; a 64-bit hash of every byte the decode reads (texels, and the palette for P8) is checked every draw |
| resident render target | the per-draw upload and readback (one upload and one readback per frame instead) | guest RAM is synced before the present reads it (the runtime presents before it calls the backend's flip, so the hook is in our present function), before any draw or clear the CPU renderer takes, before a texture overlapping the surface is decoded, and when the surface changes. NOT covered: the guest CPU reading the surface mid-frame |
| faster present | a division per pixel (8 ms) | column table and row copies; same pixels |
| `KeQuerySystemTime` yield (patch 0017) | a core at 100% in the guest's wait loop | sleeps about 20 us every 64th call; reported time unchanged; `RECOMP_TIME_YIELD=0` |

**Verification without guessing:** `MANHUNT_DEKO_VERIFY=1` runs every call also through the conservative path (whole surface, uncached buffer, no texture cache, nothing resident) on a host shadow and compares the two results exactly (after each call, or at each sync for the resident target). A frame-to-frame comparison is not usable: the menu background animates (measured: 96% of pixels differ between consecutive frames of an unchanged menu). A mismatch is then bisected by turning single pieces off by config, without a rebuild.

**Expected, not measured:** per frame about one upload, one readback and about 27 small GPU submits, which should be tens of milliseconds, against 1.2 s now. If the numbers say otherwise, the `[MH-DEKO-T]` log shows where the time goes.

