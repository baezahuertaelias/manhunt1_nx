# Fight audio, Status Monitor and CPU performance — 2026-11-10

Branch: `settings-font`. Hardware acceptance of the combined candidate is pending.

## Hardware evidence before the combined changes

The new diagnostic NRO was sent through nxlink from Docker with TCP port 28771
published for its stdout server. The console was running title takeover with
Status Monitor and SaltyNX. Runs A and B used the same NRO:
SHA-256 `27c0c0a6d03f5912abfa1bd790f98fabefd9fac9034a4adb407dfedf472f790e`.

Logs: `game/build/hw-test/2026-11-10-audio-smon/run-{a,b}.log`.
`ab-summary.json` records the last ten complete `[perf]` samples in each log
with more than 20,000 vertices/frame. These are manual gameplay samples,
not a frame-identical replay or an independently observed fight-route proof.

| Median metric in those samples | A: GL thread on | B: GL thread off |
| --- | ---: | ---: |
| FPS | 25.0 | 22.25 |
| APU thread, % of one core | 61.0 | 84.5 |
| Consumed stereo frames/s (48,000 required) | 28,409 | 40,565 |
| Underrun events per approximately 2 s | 47 | 40 |
| Queued stereo frames at sample time | 512 | 896 |
| CPU/GPU MHz | 1020/307 | 1020/307 |

Disabling the GL thread improves audio production but does not eliminate
starvation. This supports CPU contention as a contributor. Underruns count
dry events, not missing samples; `played` excludes inserted silence. Startup,
pauses and scene changes must be excluded from steady-state acceptance.
The measured clocks supersede any assumption that these runs used 1785 MHz.

Status Monitor diagnostics found earlier FPS blocks at `0xbc`, `0x16c` and
`0x21c`; A allocated `0x2cc`, B allocated `0x37c`. The new block's cleared
counter stayed at its initial 1. The overlay scans for the first magic, so
later allocations are hidden. The root cause is consistent with reservations
surviving successive NROs inside Sphaira's title process; the provenance of
each earlier block was not individually established.

Owning sources inspected:

- [Status Monitor Utils.hpp](https://github.com/masagrator/Status-Monitor-Overlay/blob/0434c0bac70f4fd2a82db85179f283fb9c504d77/source/Utils.hpp): packed 174-byte layout, first-magic search and active handshake.
- [SaltyNX service.c](https://github.com/masagrator/SaltyNX/blob/6d4b27255fe350fd349483430d42da770ea3d982/saltysd_proc/source/service.c): aligned sequential reservations; size zero is rejected.

## Combined candidate

- Add an atomic SDL snapshot of queue fill, consumed frames and underruns to
  `[perf]`. Protect snapshot/device lifetime during startup and shutdown.
- Patch 0024 registers the MCPX frame thread as `apu`. Thread registration
  publishes its handle before its name. GL/APU startup logs priority and affinity.
- Give the APU worker one priority level above its inherited priority, provided
  it remains below the diagnostic threads. `MANHUNT_APU_BOOST=0` restores the old
  scheduling for comparison. No core pin or larger audio queue is introduced.
- Map SaltyNX's page before reserving. Reuse the first complete FPS block only
  if its frame number and tick history remain unchanged for 500 ms. A changing
  producer is left alone. Reuse preserves control/resolution fields; only the
  port's FPS/timing/active fields are updated. Allocate only when no block exists.
  This probe detects observed inactivity, not ownership or permanent liveness.
- Patch 0025 advertises a raw shader-lighting capability. Supported FFP batches
  retain source attributes and carry lighting flags by value through the GL
  queue. Invalid position/normal endpoints and unusual colour-material slots
  keep the original CPU path. `RECOMP_GL_NO_LIGHT` retains its existing meaning.
- GL evaluates the original `lit_color` model: scene ambient, eight directional
  or local/spot lights, range, attenuation, colour material, alpha, normal
  transform/normalization and optional normal flip. It preserves the original
  model's limits, including its lack of specular and spot cones.
  `MANHUNT_SHADER_LIGHTING=0` selects CPU lighting for comparison.
- Cache lighting uniforms per shader and send other uniforms only when their
  own fields change. Capture the longest frame with an atomic exchange.
- Switch builds explicitly use `MANHUNT_GEN_OPT=-O2`; native host bring-up stays
  at `-O1`. The native build script accepts `MANHUNT_GEN_OPT=-O1` for rollback.
- Retain the added `0x0006F9E0` function record and per-address ICALL log limit.

Runtime-only changes were compiled incrementally against the existing lifted
game code, which already contains `sub_0006F9E0`. A clean build applies the
maintained patches through the normal preparation/lift pipeline. Changing
the patch list invalidates that pipeline's lift cache as before.

## Local validation

- Combined Switch NRO built successfully with `MANHUNT_GEN_OPT=-O2`:
  `game/build/switch/manhunt.nro`, SHA-256
  `a59a3300e2794388f1112889973bbd25fff86fd618d4148001bd8ea049cc531b`.
  An identical copy is saved as `combined-o2.nro` beside the A/B logs.
- The entire patch series applies to pinned xboxrecomp and produces the same
  APU core, raw-batch executor and backend header used by this build.
- All four function-list repair tests pass; the generated dispatch contains
  `sub_0006F9E0`. Failure-log rate-limit tables are private to each guest worker.
- SDL sink and APU glue tests pass, including snapshot values, wrap, overflow,
  silence, expected drain, reopen and shutdown.
- Status FPS tests cover first-block selection, complete-block bounds and
  detecting timing changes independently of reader-owned active flags.
- Real OpenGL transform feedback on Mesa llvmpipe compares shader output with
  the runtime's actual unchanged `lit_color`: 2,048 randomized cases each with
  normal flip off/on (4,096 total), maximum absolute error `1.19209e-7`.
  The complete production fixed-function vertex shader compiles; switching
  back to an unlit batch restores the input colour. This is numerical desktop
  evidence, not Switch visual acceptance.
- Synthetic GL contracts pass in strict, fenced and GPU modes.

Repeat commands inside the host build image:

```sh
bash tests/audio-apu/run.sh
cc -std=c11 -Wall -Wextra -Werror tests/status-fps/test_shared.c -o /tmp/status-fps-test
/tmp/status-fps-test
bash tests/shader-lighting/run.sh
bash tests/nv2a-gl-review/run.sh
```

## Consolidated Switch acceptance

Per the user's later instruction, finish implementation before further tests.
Keep CPU clock and the route fixed. First run the combined candidate with GL
thread on, Status Monitor visible for at least ten seconds, and ordinary audio.
Confirm the first FPS block is reused, its reader handshake occurs, and FPS is
actually visible. Reproduce all fights and check lighting, camera, audio and
normal exit. Audio acceptance requires steady consumption near 48,000 frames/s,
no increasing underruns in the fight and the user's audible confirmation.

If needed, compare `MANHUNT_APU_BOOST=0` or `MANHUNT_SHADER_LIGHTING=0` one at a
time in this same build; all candidates are ready before asking for those runs.
Priority changes are a candidate fix until the console results establish them.

Battery validation remains separate: handheld, `MANHUNT_GPU_MHZ=307`, record
effective clocks, FPS and charge over 20 minutes on a repeatable route.
`MANHUNT_FPS_CAP=30` remains optional until game-speed/pacing behavior is verified.
No battery saving or sustained Switch performance gain is claimed yet.
