# GPU offload implementation and acceptance — 2026-11-10

**ACCEPTANCE FAILED — investigation in progress.** The owner reports:
"after first punch on court the screen goes black and in general fps are 12".
Menu/settings passed initially; court rendering and sustained performance did
not pass. The frozen measurements below are diagnostic only: the time of the
first punch relative to that window is not established, so they cannot support
a speedup or visual-equivalence claim. New paths remain opt-in. An all-flags-off
rollback of the same NRO is the next causal test.

Branch: `codex/gpu-offload`, based on `settings-font` (`00db7a5`).
Plan: `PLAN_GPU_OFFLOAD.md`. Audio/APU, game logic and the known return-to-menu
allocation crash are outside this branch's changes.

## Implemented paths

- G0: opt-in stage clocks and counters. `[perf-gl]` reports lookup, vertex/index
  upload, uniform work, stage binds, fixed state, draw submission and **other**
  time per frame. Other includes surface preparation, diagnostics, queries,
  strict sync and overhead; stage sum coverage does not mean all time has been
  assigned to an optimization. Map/uniform/texture-bind and conversion counts
  include converted attribute vertices grouped by stored format. Extra clocks
  are gated by MANHUNT_PERF_LOG. Existing coarse profiling remains unchanged.
- G1: coherent persistent vertex/index storage when ARB_buffer_storage exists,
  three segments fenced at flip or mid-frame exhaustion, wait before reuse,
  safe oversized allocation replacement. If unavailable, pack vertices and
  indices into one mutable allocation/map per draw. The old renderer already
  packed all vertex attribute runs into one map; its remaining separate index
  allocation was the second map. `MANHUNT_GL_PERSISTENT=0` restores that path.
- G2: std140 constants and lighting blocks in a separate aligned 4 MiB stream,
  constant generation reuse across shader programs, lighting content reuse,
  bounded orphaning with both cached bindings invalidated. Shader-cache ABI
  changed; old/corrupt/truncated caches reset before new records append.
  `MANHUNT_GL_UBO=0` restores ordinary uniforms.
- G3: update only changed fixed-state fields, skip disabled texture stages
  already bound to zero, skip the draw's redundant VAO bind. Existing invalidation
  sites remain in force. `MANHUNT_GL_STATE_CACHE=0` restores previous caching.
  See `GPU_STATE_AUDIT.md`.
- G4: CMP 11:11:10 storage goes through an explicit raw-backend capability,
  four exact byte inputs and shader sign extension/division. Shared Vulkan and
  other backends do not advertise it. Invalid guest/inline ranges retain CPU
  conversion. `MANHUNT_GL_CMP=0` restores CPU CMP; `RECOMP_GL_DIRECT=0` restores
  conversion for all attributes. CMP was selected because it is the remaining
  recognized format excluded by raw_direct_attrs; scene-specific frequency is
  determined by the console counters, not assumed from the implementation.
- G5: `DEKO3D_FEASIBILITY.md` covers shader/toolchain, resource/format gaps,
  explicit unmeasured CPU budget and rough scope. No deko3d code changed.

The four behavior changes remain opt-in until console visual acceptance.

## Host proof

Run `tests/gpu-offload/run.sh` in `manhunt-recompiler:xboxrecomp-f0f8a46`
with `--user 0`. Logs: `game/build/hw-test/2026-11-10-gpu-offload/host/`.

Passed legacy and new review contracts (strict/fenced/GPU modes), persistent
ring/packed fallback ownership and failure cases, aligned UBO generation/orphan
contracts, state-cache contracts, actual executor backend registration, cache
rollover and I/O failure contracts, and audio/APU regression tests. Real llvmpipe
rendering passed legacy/new and extension-disabled fallback in strict, queued,
fenced and fenced-queued modes. Integration draws consume actual mapped packed
vertex/index storage, alternate CMP colors through production shaders and queue
copies, then restore ordinary floats. Profiling stages cover >=90% of draw time,
including the separately named residual.

Lighting versus the actual CPU lit_color passed 2,048 cases each in legacy/UBO
and normal/flip modes (8,192 cases, maximum error 1.19209e-7). All 192 constant
rows match exactly in legacy and UBO shaders. CMP matches fetch_attr bit-for-bit
for all encodings of each independent signed component and 8,192 mixed words
(13,312 words total), with direct eligibility/guest boundary/inline-short tests.
These are desktop contracts, not Switch performance or whole-game visual proof.

Independent subagent review found a missing CMP guest-span check before direct
queue copies; fixed and covered. No other actionable ownership bug was found.
The full maintained patch sequence including 0028 applies and re-lifts. Switch
build keeps generated code at -O2 and Docker parallelism at 12.

## Console evidence

Combined NRO: `game/build/hw-test/2026-11-10-gpu-offload/combined.nro`
SHA-256: `b000f012a64eb810ac2db3a81d95cd58abcab8b987561ea0591ae57fe526e466`.
The full re-lift/build and final runtime incremental build succeeded; logs are
`build.log` and `build-incremental.log` in that directory. Deployment through
nxlink succeeded on the second connection attempt. Full live log:
`run-combined.log`. Frozen measurement input: `court-window.log`; exact sample
rows, source line numbers and medians: `final-summary.json`; reproduce with
`summarize.py`. `manifest.json` records the launch arguments and host-log hashes.

All new paths were explicitly enabled, GL threading enabled, performance period
2 s, dumps disabled. Measured CPU/GPU/EMC clocks are **1020/307/1331 MHz**, matching
the baseline. The log confirms ARB_buffer_storage availability, persistent
streaming, UBOs, incremental state caching and the packed-normal capability.

The user confirmed **"Menu/settings look correct; now in court fight"**. A marker
at live-log line 7653 records that confirmation. The frozen window contains all
18 subsequent complete samples above 20,000 vertices/frame, spanning 36 seconds.
No shader or texture events were excluded: there were two program compilations
and ten texture uploads in the window. Baseline selection is the last ten complete
heavy samples of `2026-11-10-audio-smon/run-final.log`; its FPS/audio/vertex medians
reproduce the recorded `final-summary.json` exactly.

| Median | Previous combined run | GPU-offload candidate |
|---|---:|---:|
| FPS | 21.0 | 20.75 |
| Draws/frame | 371 | 461.5 |
| Vertices/frame | 37,622 | 41,160.5 |
| GL draw CPU time/frame | 18.7 ms | 15.7 ms |
| Rest/frame | 22.4 ms | 28.1 ms |
| Consumed stereo frames/s | 44,847 | 48,030.5 |
| Underrun events per ~2 s | 16.5 | 0 |

This is a same-clock manual-route observation, not a frame-identical replay:
the candidate had about 24% more draws and 9% more vertices. Its sampled GL draw
time was about 16% lower while FPS remained around 21. Do not claim an overall
FPS speedup or isolate any optimization's causal contribution from this run.
The underrun median is zero, **not the total**: 55 underrun events occurred over
the 36-second window, so zero-underrun audio acceptance is not achieved.

Fifteen of the 18 samples have complete paired `[perf-gl]` lines (other lines were
interleaved with concurrent logs). Their stage-sum/rounded-total ratios span
0.99675–1.00314, inside the 10% acceptance bound. Stage medians in ms/frame:

| Lookup | Vertex/index upload | Uniforms | Texture binds | Fixed state | Draw call | Other |
|---:|---:|---:|---:|---:|---:|---:|
| 0.153 | 3.048 | 2.256 | 1.964 | 0.237 | 6.124 | 1.291 |

Medians are taken independently and need not sum to the median total. Counters:
0 maps/frame, 248.6 uniform uploads/frame (ordinary uniforms plus UBO writes),
167,830 uniform bytes/frame, 440.2 texture binds/frame, and 0 converted attribute
vertices/frame. There is no instrumented old-path per-stage baseline, so no
isolated upload/uniform/state speedup is claimed. Zero conversion with CMP
capability enabled does not establish that this scene uses CMP or prove a G4
CPU benefit. Host tests establish its decoder/ownership correctness.

The observed remaining cost is actual draw submission plus streaming/uniform
work, while guest/rest work is larger than GL time. This supports the separate
feasibility report's no-go for a backend rewrite in this branch. No audio,
game-logic or deko3d changes were introduced.

Final owner check failed as quoted above. The process continues rendering and
logging, without an observed MH-CRASH or GL fail marker. Late executor summaries
contain NaN model-view register values (0x0480 onward) and huge negative vertex
coordinates. This localizes a symptom, not its cause; renderer corruption,
clean-re-lift differences and an existing game-state fault need isolation.

## Regression investigation and next build

The inherited per-method profiler is a confirmed configuration difference.
`scripts/gpu-offload-run.ps1` now explicitly supplies RECOMP_RENDER_PROFILE=0,
records every argument and SHA, and supports `-Rollback` to disable all four
new paths in the same binary. It also fixes the GL ownership mode to `gpu`
and enables MANHUNT_GL_VALIDATE=1: bounded per-frame GL error observation and
bounded nonfinite fixed-transform input logs. Validation reports bad inputs;
it does not clamp, replace or otherwise modify guest transforms. Its opt-out
path issues no extra GL error query. Default feature paths remain unchanged.

Further host investigation found no G3 divergence in 100,000 randomized state
and sampler replays. A real-driver G2 stress fixture covers generated microcode
reading constant rows 0/191, shader variants, lit/unlit batches, unchanged and
changed generations, direct/queued paths and natural UBO stream wraps. No
renderer defect has yet been established from these checks. The necessary
next console discriminator is the same-binary rollback with per-method profiling
off; the owner requested that the build be finished before activating nxlink.
The black screen remains UNFIXED until evidence identifies and verifies its cause.

Binary comparison of the accepted baseline NRO and failed candidate found no
unclassified instruction changes across 14,655 generated guest functions
(18.3 MB), after masking address-sensitive immediates. Selected camera/vector
functions match byte-for-byte beyond the initial relocated global-base load.
Evidence and comparison limitations are in
`game/build/hw-test/2026-11-10-gpu-offload/binary-comparison/guest-comparison.json`.
This argues against changed lifted arithmetic but does not prove every relocated
target, runtime behavior or game-state timeline equivalent.

Diagnostic build ready: `game/build/hw-test/2026-11-10-gpu-offload/diagnostic.nro`,
SHA-256 `b696a0d837c1b2b87ec97e2faa7d05fd2007f140baf698cc6fb8a2a0b0ee903c`. Runtime incremental build passed; all 15 groups in
`tests/gpu-offload/run.sh` pass after the diagnostic additions. The launch helper
passes profile=0 and validation=1 explicitly; it does not overwrite the card's
configuration or saves. This is a prepared diagnostic/rollback candidate, not
an assertion that the combat black screen has been fixed. Awaiting the owner's
nxlink activation before the next console comparison.

## Owner rollback feedback and ambient adjustment

The diagnostic NRO `b696a0d8...` was deployed successfully with all four new paths
off, RECOMP_RENDER_PROFILE=0 and validation enabled. Evidence:
`game/build/hw-test/gpu-offload/20261110-234419-rollback/` (manifest and live log).
The owner reports **"screen goes well"** on this rollback. Their NPC/FPS wording
was interpreted as about 12 FPS with NPCs, about 20 without; that condition split
is user-observed, not isolated by the perf log. The previous black-screen failure
has not been attributed to a single new path, and the prior test also differed
in per-method profiling. New paths remain off by default. The launch helper now
requires `-EnableExperimentalGpuPaths` to enable the experimental group.

At the owner's request, scene ambient RGB for shader-lit fixed-function geometry
now defaults to **0.33**, applied before the existing ambient/diffuse light sum.
It is an absolute RGB level, not a final-frame brightness multiplier. Material
alpha, individual lights, unlit geometry/UI and programmable vertex shaders are
preserved. `MANHUNT_AMBIENT_LIGHT=-1` restores the title's scene ambient;
0..1 selects a different level. The existing CPU-lighting fallback retains its
original title calculation. Ordinary uniforms and UBOs use the same preparation.

All 16 host groups pass, including ambient configuration/alpha/unlit contracts
and real-driver analytic pixels at 0.33 (ordinary and UBO outputs identical),
plus the previous shader, renderer and audio regressions. Incremental Switch
build passed. Candidate: `game/build/hw-test/2026-11-10-gpu-offload/ambient-033.nro`,
SHA-256 `f7cc2377c651473573603985729cb00a5c5ade15d4fd708ff5d3ec386857f57c`. New ambient appearance is UNVERIFIED on Switch;
waiting for the owner to activate nxlink before sending this build.
