# deko3d in-level renderer feasibility — 2026-11-10

Status: research only. No deko3d renderer changes. The G0–G4 console result in
`GPU_OFFLOAD_RESULTS.md` is the decision input; desktop tests cannot supply
Switch driver timings. The combined console capture now supplies those GL timings; the deko3d CPU
budget remains an explicit assumption.

**Console acceptance failed:** first court punch produces a black screen and
owner-observed FPS around 12. G0 timings below are diagnostic measurements of
that candidate, not an accepted optimized baseline. Resolve this regression
before deciding on further graphics architecture work.

## Recommendation

**No-go for replacing the GL backend in this branch.** The combined Switch run
at CPU/GPU/EMC 1020/307/1331 MHz measured 15.7 ms/frame GL draw time and 28.1 ms
rest time in 18 confirmed-court heavy samples. Its 15 paired G0 lines measured
6.124 ms actual draw submission, 3.048 ms vertex/index upload, 2.256 ms uniforms,
1.964 ms texture binding and 0.237 ms state (independent medians). FPS remained
around 21 with a different draw workload. A separate deko3d prototype is justified
only if repeated measurements show that reducing the remaining submission CPU
materially helps the title/APU schedule. It must accept the complete raw batch path;
extending the current CPU-transformed menu subset alone cannot answer this.

The existing adapter in `game/src/nv2a_deko3d.c` accepts a deliberately narrow
surface/combiner/filter subset, uses three precompiled shaders, and has resident
texture/surface experiments behind MANHUNT_DEKO_* options. The plan records
about one flip/second in-level because rejected draws rasterize on the CPU.
That historical result is not a measurement of deko3d command submission cost.

## Required pipeline coverage

| GL capability today | What a raw deko3d backend needs |
|---|---|
| Vertex microcode (`gl_vsh.c`), fixed transforms, shader lighting, CMP decode | Matching generated shader semantics, input layouts, viewport/depth conventions, constant/lighting buffers; shader keys include translator version and stage source |
| Register combiners and four texture stages (`gl_psh.c`) | All accepted combiner/texture-shader variants, explicit image/sampler bindings, alpha test/fog; unsupported variants fail explicitly rather than silently becoming CPU draws |
| Color/depth surfaces and render-target feedback (`GlSurf`, `GlDepth`) | Persistent images keyed by physical address/format/extent, shared depth ownership, render-to-sample barriers, exact clear masks and safe guest publication in strict/fenced modes |
| Linear, Morton-swizzled, palette and DXT textures | Reuse existing decode contracts initially; upload/retile to deko3d images, palette/content invalidation, mip/filter/wrap equivalence, DXT1/3/5 format coverage; Xbox swizzle is not automatically Switch image layout |
| Vertex/index rings and threaded queue | GPU-addressable staged memory, aligned writes and explicit fences before reuse; keep command recording on the renderer thread |
| Gamma, presentation, visibility queries and widescreen | LUT pass and display orientation, query/report ordering, known 2D program rules; exercise settings font and level/main-menu transitions |

## Shader compilation and delivery

Deko3d consumes native Maxwell code in DKSH. The normal workflow compiles GLSL
with the PC `uam` tool during the build; accepting GLSL/SPIR-V at runtime is
not part of the API. Bindings must be explicit and stages are separable.
Sources: [deko3d primer](https://github.com/devkitPro/deko3d/blob/master/Primer.md),
[UAM documentation](https://github.com/devkitPro/uam).

This repository already invokes host `uam` from `game/CMakeLists.txt` for three
RomFS shaders. Extend that build pipeline: export GL program records from a
representative playthrough, generate stage sources offline, replace ordinary
uniforms with explicit bound blocks and compile each distinct stage to DKSH.
Deduplicate stages, ship a manifest keyed by full shader source/translator ABI,
and load code from a bounded code-memory arena. A court-only cache is not
whole-game coverage: unseen keys must be logged and fail the prototype's
coverage acceptance. A generic combiner or microcode-interpreter shader can
reduce variants, at a GPU cost that must be measured. On-device UAM would need
a separate compiler port, memory budget and toolchain integration; it is not
available merely because host UAM is in the image.

## CPU estimate and project size

The old plan's GL submission estimate is approximately 50 microseconds/draw
including state, uploads and driver work, roughly 18 ms at 360 draws/frame.
For planning only, budget **5–15 microseconds/draw** for cached deko3d command
recording with resident resources (1.8–5.4 ms at 360 draws). This range is an
**UNVERIFIED engineering hypothesis**, not a benchmark or promised speedup.
Shader compilation, texture decode/retile, waits and guest execution sit outside
that budget and can dominate. No public generic draw benchmark predicts this
port's actual workload. The next decision should use G0's remaining CPU stages,
then measure a representative raw-batch prototype at identical clocks.

Rough scope: 4–8 engineer-weeks for raw pipeline/resource/shader integration,
then 2–4 weeks for representative-level coverage and visual/ownership tests;
several thousand lines of backend/asset tooling, with wide uncertainty. This is
an estimate, not a delivery commitment. Exit the prototype early if shader
coverage is incomplete, memory is excessive or same-clock CPU measurements do
not beat the optimized GL path. Audio/APU work and the return-to-menu allocation
crash remain separate regardless of backend choice.
