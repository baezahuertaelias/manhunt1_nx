# Fight audio, Status Monitor and CPU performance — 2026-11-10

Branch: `settings-font`. Status Monitor FPS is confirmed by the user on Switch.
The user reported the final combined gameplay test worked well. Telemetry still
shows audio starvation under load, and returning to the main menu crashed.

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
- Follow-up patch 0026 caches exact voice-mixer math on Switch: initialize all
  4,096 attenuation values once with the original `powf` expression, and cache
  repeated pitch/filter `powf(2, exponent)` results using exact float-bit keys.
  The attenuation table is 16 KiB shared storage; the pow cache is 3 KiB per
  thread. No sample, voice, filter or mixing stage is omitted. Other hosts keep
  the original math path. Host tests explicitly enable the Switch cache path.
- Patch 0027 adds cumulative APU wall-time buckets to the existing perf line:
  throttling, voice processing, DSP mixdown, output, guest IRQ/DPC delivery and
  mutex handoff. Counters are collected only with `MANHUNT_PERF_LOG` enabled.
  This prepares the next run to explain remaining stalls without another
  diagnostic-only console run. Wall time includes preemption and waiting.

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
- Exact APU math tests pass for all 65,536 uint16 volume inputs, pitch/cutoff
  exponents, fractional envelopes, cache collisions, infinities/NaNs and four
  workers racing initial table creation. Finite results match the original
  math bit-for-bit on the host. These establish arithmetic equivalence, not a
  measured Switch speedup.
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

## Combined Switch tests C and D

Both used the combined `a59a...` NRO above, GL thread on, shader lighting on,
`-O2`, and measured CPU/GPU clocks of 1020/307 MHz. Logs are `run-c.log` and
`run-d.log` beside A/B. The manual route was not an identical replay.

- C, `MANHUNT_APU_BOOST=1`: log confirms APU priority `0x3b -> 0x3a` succeeded
  and the first FPS block at `0xbc` was reused. The user confirmed FPS displayed
  and audio sounded better, but gameplay felt slower. Heavy portions reached
  roughly 14–25 FPS. Audio often sustained approximately 48,000 frames/s, yet
  underruns resumed under heavier load; the counter ended near 297. Thus this
  did not pass sustained audio acceptance or performance acceptance.
- D, same binary with `MANHUNT_APU_BOOST=0`: the user said it felt better.
  Representative heavy-scene samples showed 25–35.5 FPS, but audio fell to about
  27–32k consumed frames/s with 46–47 underrun events per two seconds. Status
  Monitor cleared the active flag again (`smon cleared 3`), consistent with the
  user's confirmed FPS display. Audio acceptance still fails.

These runs expose the priority tradeoff; the boost must not be described as a
complete fix. Some short per-thread CPU samples exceed 100%, so those snapshots
are advisory, not proof of a physical single-thread utilization above one core.
Queue consumption and underruns remain the audio acceptance measurements.
C/D feedback did not isolate lighting fidelity; the final overall user
confirmation is recorded below.

## Final combined test E and remaining acceptance

Patch 0026 targets repeated mixer exponentiation rather than changing audio
quality. The consolidated candidate includes patch 0027's wall-time breakdown
as well. Its Switch build passed. Candidate: `game/build/switch/manhunt.nro`,
also copied to `consolidated-final.nro` beside the logs, SHA-256
`47e7aee2b7ccb6b67a8fae60f8297963c37a1e2b9351b43abe3f1e67a8ffb808`.
It was sent through nxlink and executed with the same options as C, including
the priority boost. Log: `game/build/hw-test/2026-11-10-audio-smon/run-final.log`;
`final-summary.json` beside it records the sample selection and summary.
The cache-only intermediate `9c10...` NRO was not executed on the console.

The user answered **"that worked well"** to the combined fluency/FPS, audio and
lighting check. This is the final user's overall gameplay assessment; it is
not a frame-identical comparison or a separate fidelity measurement.

The log confirms shader lighting, successful APU priority change `0x3b -> 0x3a`,
one FPS block at `0x144`, and reader handshakes (`smon cleared 3`). Measured clocks
remained CPU/GPU 1020/307 MHz. There are 184 complete parsed perf samples
(368 sampled seconds; interleaved/incomplete lines are excluded). The last ten
complete samples above 20,000 vertices/frame have medians:

| Metric | Final combined run |
| --- | ---: |
| FPS | 21.0 |
| Consumed stereo frames/s | 44,847 |
| Underrun events per approximately 2 s | 16.5 |
| VP wall time, ms per second | 984.5 |

The final reported underrun count was 725. Thus the subjective improvement is
confirmed, while the strict zero-underrun/48k acceptance target remains open.
The wall-time breakdown localizes the remaining work to voice processing;
DSP mixdown, output and IRQ delivery are much smaller in representative samples.
Wall time includes preemption and locking, so it does not identify a particular
voice-processing algorithm as the CPU hotspot by itself.

### Returning to the main menu: confirmed crash trigger

The user clarified that the crash occurred **while attempting to return to the
main menu**, after gameplay. The tail loads `LOAD_XBOX.TXD` and
`levels/Jury_Turf/PICLOAD_XBOX.TXD`, reports repeated contiguous allocation
failures, then logs `[MH-CRASH] kind 0x101`. The last reported arena state was
64,248 of 65,408 KiB in use, with a reported largest free run of 1,024 KiB.
The existing allocator excludes a low-memory reserve for unrestricted requests,
so that free-run statistic alone does not prove an eligible block was available.
The allocation failures precede the crash; the exact causal chain is unproven.
This transition has not passed acceptance and must not be reported as normal exit.

The user explicitly asked to consolidate all pending changes and minimize
manual console tests. Do not request a separate run per optimization or knob.
The completed final run captured gameplay feedback and the complete APU time
breakdown together. No additional console run is requested by this report.

Future implementation should use the captured evidence before requesting
another consolidated acceptance run. Remaining work includes reducing VP cost
without changing sound and diagnosing the return-to-main-menu allocation/crash
sequence. `MANHUNT_APU_BOOST=0` and `MANHUNT_SHADER_LIGHTING=0` remain rollback
knobs; separate manual tests for each knob are not the preferred workflow.

Battery validation remains separate: handheld, `MANHUNT_GPU_MHZ=307`, record
effective clocks, FPS and charge over 20 minutes on a repeatable route.
`MANHUNT_FPS_CAP=30` remains optional until game-speed/pacing behavior is verified.
No battery saving or sustained Switch performance gain is claimed yet.
