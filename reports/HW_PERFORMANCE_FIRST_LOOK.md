# First look at rendering speed on the physical Switch (2026-11-02)

Status: **measurements from one 42 s run on one console, plus hypotheses.** Hardware frame rate for Beta 1 (30 FPS) is **UNVERIFIED** and far away: this is the original main menu running on the CPU software rasteriser. Text only; the frames and logs stay private. Companion to [HW_SWITCH_FIRST_RUN.md](HW_SWITCH_FIRST_RUN.md).

## Measured (stream of the run; build with patch 0016, `MANHUNT_GPU_BACKEND` unset = CPU renderer)

| Quantity | Value | How |
|---|---|---|
| Frames presented | 2, 7, 9, 12, 15 at the 5 s dumps, so about 13 in 20 s: **about 0.5 to 0.65 frames per second** | `[MH-FRAME] dump ... (presents=N)` |
| Vblank timer | 600 ticks per 10.0 s = **60.0 Hz**, gaps 9 to 35 ms, no late acknowledgements | `[NV2A] vblank N: last 600 in ...` |
| Software rasteriser | 6,197,633 pixels / 428 triangles / 64 draws at the first report (about 10 s); 19,526,527 pixels / 1,870 triangles / 260 draws at the second (about 30 s): **about 0.67 million pixels per second**, about 70 triangles per second | `[GPU] ... pixels written` |
| Work per presented frame | about 1 million pixels, about 3 full 640x480 screens of fill (the menu is layered and blended) | pixels / presents |
| Flip pacing | holds: 7 in total, 7.5 s held in total, no hold timeouts | `[GPU] flip hold:` |
| Guest wait loop | kernel ordinal 128 (`KeQuerySystemTime`): 13.6 million calls in the run, rising by about 3.8 million per second, always the top call | `[KERNEL] summary` |
| Process memory | 3185 MB used of 3189 MB (the default libnx heap takes nearly all of it) | `[MH-WD]` ticks |
| Console | stayed up (ping never dropped inside the game window); no crash line | ping monitor, stream |

## Render profile run (measured, 66 s, `RECOMP_RENDER_PROFILE=1`, 26 frames presented)

| Scope | Calls | Wall time | Per call |
|---|---:|---:|---:|
| raster | 624 | 54.16 s | 86.8 ms |
| draw | 624 | 54.18 s (contains raster) | |
| scan (guest pushbuffer scan, methods included) | 29 | 54.46 s | |
| scan excluding methods | 29 | 0.010 s | |
| present (CPU scale and copy) | 26 | 0.205 s | 7.9 ms |
| clear | 25 | 0.050 s | 2.0 ms |
| flip intervals (frame time) | 25 | 55.23 s | 2.21 s per frame |

- **The software rasteriser is 98% of the frame time** (54.2 s of 55.2 s). Guest scanning, clears and presents are negligible.
- 44.3 million pixels and 4,548 triangles in that window: **about 0.82 million pixels per second, about 1,220 ns per pixel**.
- The rasteriser is **single-threaded in this build**: the worker pool in `kernel/nv2a_pb_exec.c` is compiled only for `_WIN32`; the `#else` branch used on Horizon draws every triangle serially on the executor thread. With the guest thread busy-waiting on another core, at most one more core is usable, so threading alone cannot give more than about 2x.
- Present, clear and everything else together are about 0.5% of the time: a faster rasteriser (or the GPU) is the whole game.

## Effect on control

The user reports that the screen changes about 5 s after a button press (observation, one person, handheld). The measurements explain it: the title polls the controller once per game frame (about every 2.5 s) and a frame takes about 2.2 s to render, so input latency is roughly one poll interval plus one frame time. Faster rendering shortens it directly.

## What follows from it (reasoning, not measurement)

- The limit is **rendering throughput, not timing**: the 60 Hz timer is healthy, the game spends most of each frame time waiting for the picture.
- 0.67 Mpixel/s is very slow for scalar code on a Cortex-A57 (tens of Mpixel/s would be normal), so there is either a lot of per-pixel work in the rasteriser or the rasteriser is not getting the CPU it should. The repository already records that the rasteriser is about 99.8% of the measured window in an emulator capture (BLOCKERS item 1); this run shows the same shape on hardware.
- Eden measured 0.13 frames/s on the CPU path and 0.94 on the native deko3d path (BLOCKERS item 1); the console gives 0.5 to 0.65 on the CPU path. That is consistent with "CPU software rendering is the limiter", but the two platforms are not comparable as speeds.

## Hypotheses to test (each UNVERIFIED), cheapest first

1. **The rasteriser is simply slow per pixel: now MEASURED as true** (98% of frame time, about 1.2 us per pixel, single-threaded). The open question is only why each pixel costs that much (register-combiner interpretation per pixel, generic blend/depth paths). The profile mode, `scripts/hw-run.sh --profile`, is how it was measured; test with `RECOMP_RENDER_PROFILE=1` (config only, no rebuild): `[RENDER-PROFILE]` reports wall time per scope (scan, method, draw, raster, clear, present) and call counts. `scripts/hw-run.sh --profile` sets it (and turns frame dumps and fsync logging off so they do not distort the timing).
2. **The rasteriser threads are starved.** Horizon does not time-slice equal-priority threads; the guest thread spins in the `KeQuerySystemTime` loop at about 3.8 million calls per second, and the runtime has several threads (guest, timer, NV2A ack/executor, raster workers, frame dumper). If more threads are runnable than the 3 application cores, some get little CPU. Test: the profile run shows raster wall time versus the pixels done; a code experiment would make the guest's time-polling loop yield briefly (Switch only) and compare presents per second.
3. **The patch 0016 `usleep(200)` in the NV2A ack thread slows the executor.** It was needed or at least coincided with the freeze going away. Test: vary the sleep (for example 0 versus 200 µs) once the freeze isolation (HW-FREEZE-001) is possible.
4. **A faster path exists: the deko3d backend** (`MANHUNT_GPU_BACKEND=1`). It is UNVERIFIED on the console after patch 0016, and the earliest freezing runs had it on, so it is gated in `scripts/hw-run.sh` behind `--allow-gpu-backend` until an offline audit of the backend against known deko3d hardware pitfalls is done.

## Not known

Frame time breakdown on hardware, memory pressure beyond the one sample, thermals, handheld versus docked clocks, the effect of the logging (`MANHUNT_LOG_SYNC=1` fsyncs every line; the profile mode turns it off), and anything about gameplay scenes (only the menu was measured).
