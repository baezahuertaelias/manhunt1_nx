# GPU offload follow-up — 2026-11-11

Branch: `codex/gpu-offload`. Owner priority: black screen/NPC slowdown first;
finish all optimization code and host checks before the next Switch run.
Ambient tuning is excluded from the controlled comparison.

## Correction from the complete rollback log

`game/build/hw-test/gpu-offload/20261110-234419-rollback/run.log` confirms
nonfinite incoming fixed transforms at lines 19806–19819 with **all four original
new paths disabled and per-method profiling off**. Later summaries contain NaN
model-view methods (first at line 21822). No MH-GL-ERROR appears in that log.
The first reported allocation failures follow the first nonfinite input, around
line 19830. A later return-to-menu load again exhausts the contiguous arena and
crashes; that known, separate transition is outside the rendering optimization
work. The owner reported correct court rendering earlier in this rollback run.
The exact later action causing invalid transforms is not identified by the log.

Therefore invalid transforms are not exclusive evidence against a new GPU path.
The black screen has **not been fixed or causally localized**. Do not clamp or
replace bad transforms and mistake that for a root-cause repair. The initial
all-paths run additionally had per-method profiling enabled, unlike this rollback.

## Measured CPU work before the first invalid transform

`pre-fault-summary.json` beside the rollback log selects 101 complete samples
above 20,000 vertices/frame, CPU/GPU/EMC 1020/307/1331 MHz, before line 19806.
Medians: 17.5 FPS, 413 draws, 44,013 vertices, GL draw 22.6 ms, rest 26.0 ms.
Upload median is 8.104 ms, uniform work 1.5 ms, submission 7.91 ms; maps/frame
825.2 and converted CMP attribute vertices/frame 37,277. NPC presence is not
encoded in the log, so this is a rendering workload summary, not an NPC A/B test.

When any attribute was CPU-converted, q_draw copied vertex_count * 16 slots *
float4 into the queue, even when only the CMP normal was converted. At the above
median, that is approximately 9,542,912 bytes/frame; one float4 slot would be
596,432 bytes/frame. These byte counts exclude direct runs, indices and headers.

## Additional implemented optimization

`MANHUNT_GL_QUEUE_PACK=1` stores only converted float4 slots in a draw record.
Their queued descriptors use the existing direct GL_FLOAT format; the original
CPU conversion and shaders remain in use. Original direct guest attributes keep
their formats and copied bytes. There is no new executor patch or lift change.
With one converted attribute, converted payload storage is 16 times smaller.
The all-converted case uses memcpy. The setting defaults off until acceptance.

The consumer uses a shared relocation helper and represents absent float arrays
as NULL rather than a pointer into the record header. Conversion telemetry is
counted before packed slots are described as direct, so CMP CPU work remains
visible. Profiling initialization is atomic because both queue producer and GL
consumer now update the atomic counters. `[perf-gl]` adds qattr and qsaved byte
counts per frame. This is a host-proven byte reduction, **not a measured Switch
FPS improvement**, and it does not repair nonfinite incoming transforms.

## Host validation and prepared hardware experiment

All 17 groups of `tests/gpu-offload/run.sh` pass. New queue contracts cover sparse
and full masks, direct/converted coexistence, exact float and NaN payloads, ring
wrap, consumer relocation, byte savings and conversion counts. Production
llvmpipe draws with queue packing pass strict, queued, fenced and fenced-queued
paths. The 6,000-frame UBO integration fixtures also pass with packed queue data,
including ordinary/UBO equality and lit/unlit/ambient transitions. Existing
shader/CMP, streaming, state, audio and cache tests remain passing.

`tests/gpu-offload/test-launch.ps1` passes all seven selections and checks one
binary, explicit feature flags, profiling off, original ambient and locale-safe
numeric arguments. `scripts/gpu-offload-run.ps1 -Experiment all` enables the four
original optimizations plus compact queue storage in one consolidated build.
Individual selections remain available if a regression requires isolation;
`-Rollback` disables all five. Launching never edits SD configuration or saves.
No further Switch run was started after the owner requested implementation first.

Incremental Switch build passed with generated code still at -O2. Prepared NRO:
`game/build/hw-test/2026-11-11-gpu-offload/consolidated.nro`, SHA-256
`8cf3796583d5403650f5b301ca47e53a343006d119ad96b93b8e4dbd42343bb5`. Build log:
`game/build/hw-test/2026-11-10-gpu-offload/build-queue-pack.log`.
The NRO and final combined settings remain **UNVERIFIED on Switch**. Next is one
consolidated court-punch/NPC validation at matching clocks, with no per-method
profiling and no ambient change. Neither final acceptance nor a speedup is claimed.
