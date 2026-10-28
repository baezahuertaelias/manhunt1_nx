# Rendering blocker: investigation and proof plan

Date: 2026-10-28. Task: **RENDER-001 — OPEN**. Source inspection and a bounded Switch-build/Eden timing capture are recorded below. Raster elapsed time dominates the measured scanner scope. Guest CPU/waits, repeatable profiling overhead and deko3d backend behaviour remain **UNVERIFIED**.

## Evidence and limits

Inspected the current Switch target in `game/CMakeLists.txt`, startup and
frame-dump code in `game/src/main_switch.c`, `scripts/game-build.sh`,
`game/src/horizon_compat.c`, `scripts/emu-run.sh`,
`scripts/switch-boot-proof.sh`, and the locally prepared
xboxrecomp source at commit `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`
with project patches applied. The prepared runtime lives under gitignored
`game/build/xboxrecomp/`; recreate it with the build script. No source, frames
or log excerpts derived from the game are included here.

- `src/kernel/nv2a_backend.h` in that runtime supplies clear, triangle-batch
  and flip callbacks. Vertices arrive in surface coordinates; vertex programs
  and fixed-function transforms remain CPU work. Register-combiner state and
  texture stages 1–3 are omitted from the callback interface. Whether those
  omissions affect this menu or level 1 is **UNVERIFIED**.
- `src/kernel/nv2a_pb_exec.c` handles CPU rasterization, texture decoding,
  surface writes and semaphore release. Its diagnostic FPS calculation uses
  `clock()`, which is not suitable evidence of wall-frame duration. No
  pushbuffer/CPU-raster/guest-code timing breakdown was found in the inspected
  startup, scanner or executor paths.
- The Switch target does not currently link deko3d or register a render
  backend. Native presentation already exists:
  `xbox_FramebufferWindowPresent()` in `game/src/horizon_compat.c` creates a
  linear libnx framebuffer, scales CPU pixels and presents through
  `framebufferBegin()`/`framebufferEnd()`. Startup also dumps CPU pixels for
  inspection. A GPU backend must integrate with or replace that presentation
  path with GPU-compatible surface storage and swapchain ownership; native
  display itself is not missing. Include CPU scaling/copy cost in profiling.
- `tests/deko3d-proof/source/main.c` and its Makefile provide the project's
  existing device, shader, command submission and readback example. Its
  recorded triangle result does not prove this game's backend or presentation.
- Local Switch and host executables and the expected pinned Eden, devkitA64
  and recompiler images existed during inspection. Their presence is not a
  fresh build or runtime pass. No build or emulator run was executed for this
  report. Physical Switch performance remains **UNVERIFIED**.

## Next proof: profile before implementation

Timebox: one working day for instrumentation, focused checks and one bounded
menu capture. Stop and record the cause if the runner or instrumentation fails
twice; do not start backend implementation from an assumed bottleneck.

1. Add opt-in aggregate timing to the scanner/executor through a maintained
   project patch. Reuse `src/platform/host_time.h`'s monotonic
   `xbox_HostNowNs()`. Measure scanner inclusive time, nested method execution,
   clears, triangle rasterization, vertex transformation and flip intervals.
   Report counts and nanoseconds with interval boundaries and dropped samples.
   Subtract nested scopes only on the same thread; retain inclusive values
   so totals can be audited. Measure instrumentation overhead with it off/on.
2. Measure guest-thread execution and runtime waits separately. Scanner,
   guest, timer and dump threads overlap, so elapsed time minus raster time
   is not a valid guest-code estimate. Establish an executed accounting method
   before labeling any category guest-code time. Unattributed time stays
   explicitly unattributed.
3. Build the Switch target with `scripts/game-build.sh switch`, then run a
   bounded original-menu capture with `scripts/emu-run.sh` and a separate local
   data directory. Preserve the normal boot proof. The boot-proof script
   overwrites `sdmc/manhunt.env`, so it cannot currently preserve caller pad
   settings. Use the runner directly for this investigation or review a small
   explicit script change. Capture timing with frame dumps disabled, and
   collect separate visual evidence with dumps enabled.
4. Exercise one menu navigation press with `MANHUNT_PAD_SCRIPT`. Calibrate its
   timing against the observed menu arrival, hold long enough to be sampled,
   and compare before/after frames. An input log alone does not prove selection
   movement. Do not select a mission until this capture is understood.
5. Capture the executor's ranked unhandled methods in the same run. Map their
   meanings from authoritative NV2A/runtime definitions and distinguish
   harmless unused state from missing visual semantics. Frequency alone does
   not establish priority. Check combiners, every used texture stage, depth,
   stencil and render-to-texture explicitly.
6. Keep raw logs, frames, NRO and manifests in gitignored local storage. Record
   revision plus dirty diff hash, patch/config hashes, image digests, input
   hashes, emulator renderer/version, sample window and test status. Publish
   only authored conclusions permitted by the project's public-repo rules.

Focused instrumentation checks must prove nonnegative nested accounting,
empty-interval handling, monotonic deltas and disabled-path behaviour. An
independent agent must inspect the actual patch, checks and local results
before integration. Any configuration change also updates
`docs/CONFIGURATION.md` and `docs/CHANGELOG_CONFIG.md`.

## Backend scope after the measurements

If CPU pixel work is the measured dominant cost, first prove a synthetic
deko3d backend using the existing callback interface and synthetic textures.
This is an options proof, not permission to silently omit guest rendering
state. Extend the interface only for state shown necessary by the capture.
Unsupported required state must fail closed with a diagnostic; a passing
triangle is insufficient acceptance for the original menu.

The smallest complete menu backend must cover:

- Surface identity, pitch, pixel format, anti-aliasing factors and surface
  switches; preserve colour/depth/stencil and clear-mask semantics.
- Triangle topology and perspective-correct attributes, used texture formats,
  addressing/filtering, blend and alpha tests, depth, culling and stencil.
  Prove any combiner/stage omissions harmless or carry the required state.
- Texture upload and invalidation based on guest writes, render-to-texture
  ordering, and coherent readback when the guest or frame dumper reads pixels.
- Queue ordering and completion: asynchronous host submission must not expose
  semaphore progress before the required guest work is complete. Preserve the
  executor's existing flip/fence expectations.
- Native swapchain presentation and teardown on the owning render thread,
  separate from guest framebuffer dump evidence. Handle allocation, shader
  and submission failures without reporting success.

Use synthetic readback comparisons against the CPU path for clears, textured
triangles, blend/depth/stencil, surface changes and synchronization before
comparing original menu frames locally. Build and focused tests precede
independent review; menu navigation and existing boot checks then establish
the playable behaviour gained. Switch hardware remains a separate gate.

## Resolution, aspect and projection boundary

Keep guest logical dimensions, internal render dimensions and display output
dimensions distinct in the backend design. Carry viewport/scissor and
anti-aliasing scale explicitly; presentation must preserve pixel proportions
and support the current 4:3 output. Do not bake 640×480 or a 16:9 stretch into
shaders or swapchain copies. Document and test any dimension limits.

Projection remains guest/CPU-transform state upstream of these decoded
surface-space vertices. WIDE-001 must locate that state before WIDE-002 changes
horizontal view extent at unchanged vertical FOV. A display resize cannot
implement Hor+ projection. The later configurable resolution/aspect/projection
controls must meet at this boundary without changing simulation timing; their
names/defaults require configuration documentation and independent review.

## Exit criteria and rollback

RENDER-001 stays open until this report contains an executed Switch-build
timing breakdown with scope/overhead limits, a verified menu-method ranking,
and a backend plan revised against those results. No backend implementation
tasks should be created from the present unmeasured hypotheses.

Rollback for the next proof is removal of its optional instrumentation patch
and run configuration, restoring the recorded baseline build and boot proof.
This report changes no runtime behaviour and needs no runtime rollback.

## Executed Switch-build timing proof — 2026-10-28

Built with `scripts/game-build.sh switch`, exit 0, in the pinned devkitA64 image. NRO SHA-256 `bf122c4f763163727efb15ad223d884743ce3a904e49b398891ae1cf9082fca8`. Maintained patch 0004 adds opt-in thread-local monotonic wall timers. The post-build patch cleanup removes one trailing blank line only; the private manifest separately hashes the compiled runtime sources. No guest algorithm or numeric state was changed by the instrumentation.

The synthetic accounting/TLS self-check passes. Both instrumented runtime translation units compiled on host; the full Switch target linked and produced an NRO. Two serialized 240-second runs of that NRO used the existing Eden/Xvfb llvmpipe runner, identical menu boot defaults and no periodic BMP dumps; only the enabled capture set `RECOMP_RENDER_PROFILE=1`. Both reached guest entry `0x001FE908`, opened MAIN_EX.TXD and FE_M.RIB successfully, remained running to the end and had no logged guest faults. This is not a visual-menu or gameplay assertion.

Thirteen enabled snapshots have zero invalid-clock samples. Counts/times are monotonic; every snapshot satisfies scan ≥ method ≥ draw ≥ raster, and method ≥ draw + clear + present. Independent reviewer runtime_analysis inspected the source, config, build, private logs, manifest hashes and accounting; accepted for this diagnostic scope.

The final two report intervals contain six flip intervals totalling 48.792 seconds. Scope deltas:

| Nested scope | Elapsed seconds | Calls |
|---|---:|---:|
| Scanner inclusive | 48.434367076 | 3 |
| Methods inclusive | 48.428081108 | 42,011 |
| Draw inclusive | 48.328429712 | 169 |
| Raster inclusive | 48.320953204 | 169 |
| Clear | 0.044822960 | 6 |
| Present (CPU scaling/copy/waits) | 0.048036872 | 6 |
| Scanner excluding methods | 0.006285968 | — |

These scopes overlap. Raster is **99.766% of scanner elapsed time in this sample**, supporting the GPU raster-backend proof as the next P0 step. It does not measure guest execution, host CPU consumption or hardware FPS. Raster worker dispatch/waits and host/emulator descheduling are included. Present is small in this specific capture; no broader performance claim follows.

The disabled/enabled runs reported 31/27 completed flip holds and final draw counts 738/654 respectively. Those hold sums omit the last unreported interval. This single pair shows different throughput but cannot isolate profiling overhead from scheduler/emulator variation or startup timing. Repeatable overhead and output parity remain UNVERIFIED. No optimization or gameplay tradeoff is accepted from this data.

Private evidence: `game/build/render-profile/manifest.json`, `summary.json`, `game/build/logs/render-profile-{build,disabled,enabled}*` and `build-switch.log`. The manifest records commands/configs, exit codes, git/diff identity, binary/runtime/input/log hashes and image IDs. Raw game-derived logs/binaries stay gitignored. A separate frame/input capture is required; the timing runs deliberately omitted dumps and input.

The latest capture's top unhandled-method ranking was 0x1710 (722), 0x0394/0x0398 (75 each), 0x02B4/0x02C0/0x02E0 (54 each), 0x0110 (47), and 0x03C0/0x03C4/0x03C8 (31 each). Upstream NV097 definitions identify depth clip min/max, window clip type/horizontal/vertical, WAIT_FOR_IDLE and texture generation S/T/R respectively; 0x1710 remains unresolved. Do not confuse unrelated DAC register 0x03C8 with NV097 texture generation. The executor shadows method registers before dispatch and reads some later (including clip min/max), so its unhandled counter alone does not prove state is ignored. Required-state semantics and combiner/stage coverage still need verification.

The implementation proof below validates synthetic offscreen deko3d triangles carrying Nv2aVertex attributes at two surface sizes, asymmetric colours and varying rhw, with pixel-to-clip/Y/channel/perspective readback assertions before any menu integration. Depth format/range, textures, blending, stencil, surface coherence and omitted combiner/stages remain separate acceptance work. The existing hardcoded triangle proof does not cover them.

## RENDER-GPU-POC-001 result — 2026-10-28

**PASS for synthetic coordinate/attribute scope; not a menu backend.** Implemented in `tests/nv2a-backend-proof/` with `scripts/nv2a-backend-proof.sh`. Author runtime_analysis built and ran it; the primary agent independently inspected actual C/shaders/Makefile/wrapper, then executed the complete wrapper against the real generic upstream Nv2aVertex declaration. Both builds produced identical NRO SHA-256 `2ea3c6111f4c1cd266cf3fd820ffaf9739537e11f4e17a21623f457d9d018c2c`.

| Surface | Interior pixels | Exterior pixels | Pixels distinguishable from affine reference | Bad channel/pixel checks | Max channel error |
|---|---:|---:|---:|---:|---:|
| 128×96 | 2,783 | 8,297 | 2,697 | 0 | 0 |
| 192×128 | 5,558 | 16,586 | 5,382 | 0 | 0 |

The analytical reference uses pixel-centre barycentric weights multiplied by each vertex's rhw and normalized. Asymmetric packed diffuse colour and UV-influenced output exercise channel/attribute layout; varying rhw separates perspective from affine interpolation. Interior/exterior population assertions reject empty/clear-only draws. A band at triangle edges is deliberately excluded; no raster edge-rule parity is claimed. Guest z is unused with depth/stencil disabled, so no depth convention is accepted here.

Independent full-wrapper run exit 0; warning-as-error C compilation and shader build passed. Header SHA-256 `588025bae07f2edcd713391c4d0f865daf31080f8b830d12f0d3f1e78e5e6bd8`; pinned toolchain matches CONFIGURATION.md. Independent local emulator log `tests/nv2a-backend-proof/build/independent-emu.log` SHA-256 `e46734217837de673c82d2f839bc8d33467a2ed6ef7942497194ffb22aa63490`; full build/runner log `independent-run.log` SHA-256 `4b7809c2697842d614ac2d07fe14eb42f9634a48ab439656e6db3b23a4f049d2`. Eden used llvmpipe. These are synthetic results; hardware remains UNVERIFIED.

Next proof before registration: decoded texture upload, normalized texel addressing, alpha/blend and channel masks against independent synthetic references. Depth/stencil, surface identity/readback and completion/semaphore ordering need later separate proofs. Combiner/texture-stage/interface omissions remain unresolved. No menu renderer or GPU speedup has been implemented.

## Palette boundary discovered during state-proof review

The local boot-to-menu capture records P8 (`fmt 0x0B`) textures. In the pinned executor, the CPU `Texture` retains a palette guest address, but `Nv2aTexture` omits it and `nv2a_backend_decode_texture()` explicitly clears it. Consequently that helper rejects P8 despite its prior broad format claim. This is an actual source-level boundary gap; it does not establish the active palette length or a guest instruction address. `0x1B20` is an NV097 method address, not a guest PC.

RENDER-PALETTE-001 will carry palette VA and entry count through the backend descriptor, reuse the existing CPU P8 sampler for verified 256-entry descriptors, and reject missing/short/invalid descriptors before reads. Palette length is encoded in method bits 2–3 (256/128/64/32); short-palette lookup semantics are not established and must remain unsupported by the helper. Primary and contiguous 64 MiB views are the initial bounded helper scope; mirror-addressed inputs remain unsupported. The CPU renderer and its sampler behavior stay unchanged. No backend registration follows from this change alone.

Required synthetic regression: asymmetric 4×2 Morton index layout including index255, palette channel order and in-place rewrite, descriptor metadata from actual method dispatch, and restoration of the original stage0 on success and failure. Reintroducing the original palette-dropping defect must fail the pixel check. Both full game behavior on a registered backend and active menu palette lengths remain UNVERIFIED.

## RENDER-GPU-STATE-001 executed synthetic subset — 2026-10-28

Author build/run and primary-agent full-wrapper rerun both PASS: two prior coordinate cases, ten RGBA texture/state cases and five unsupported-state rejections. Every state case checks 10,850 pixel locations; mismatches beyond tolerance are zero. Observed maximum channel error is one for additive/combined blending and zero for other cases. Repeat samples cover all four asymmetric texels (678/582/676/617); clamp samples cover 689/923/450/491. Alpha reference128 keeps1,293 and discards1,260 interior samples, including texels at127/128/129/255. The first author run failed the fixture's strict minimum-per-texel population; widening the UV fixture fixed coverage without relaxing pixel checks. No renderer-state mapping defect was hidden.

Tested modes: nearest row-major synthetic RGBA8 upload; normalized texel UV conversion once; clamp/repeat; alpha GREATER128 with 8-bit rounding; ADD with SRC_ALPHA/ONE_MINUS_SRC_ALPHA and REVERSE_SUBTRACT with ONE/ONE; each individual NV RGBA write-mask byte and combined RG+alpha+blend. Other modes, filters, formats, palette GPU upload, depth/stencil/culling, coherence, fences and actual menu rendering remain UNVERIFIED. The mapping rejects unsupported active state; no game backend is registered.

Independent source/log review by render_analysis found no blockers in this subset. The author's NRO is `df4765f8b7442b1ada5eec062a5845a9dd76f710ccd61b746fd248617abd2000`. Root's later complete-wrapper rerun uses the appended palette descriptor fields and produces `d35b8a154b053509e1e8a8eaba6cb7e384f7c1ac7141eeb2c064ecfd983a98a8`; these are different header inputs, so no binary reproducibility comparison is claimed. Both actual run results pass. Root evidence is frozen locally under `game/build/evidence/state-d35b8a15-20261028/manifest.json`, including source/header/Makefile/wrapper/NRO and complete build/emulator logs with hashes. Eden uses llvmpipe; physical hardware and FPS remain UNVERIFIED.

## RENDER-PALETTE-001 result — 2026-10-28

Bounded host helper/interface regression PASS. Patch0005 carries palette VA and entry count through Nv2aTexture, supplies palette VA to the existing sampler, and supports power-of-two P8 index images with256-entry aligned palettes in bounded primary/contiguous64MiB RAM views. Missing palettes, short palettes, malformed dimensions/alignment and invalid/overflowing spans return0 before reads. The descriptor length is taken from raw stage0 palette-register bits; CPU rendering/sampling is unchanged.

The synthetic upstream backend fixture checks4×2 Morton indexing, index255, exact ARGB palette words and in-place palette rewrite; both mocked guest views; invalid/null/short/misaligned/overflow cases; actual method→descriptor VA/count; and original stage0 restoration after success and failure. Fixed build/CTest passes. Restoring the original palette=0 defect makes eight exact decoded-pixel assertions fail; the runner retains those expected failures and reports the negative-control PASS only after checking them. Independent render_analysis execution passed the same patch; final source/config/test/build evidence review accepted.

The full Switch build with patches0001–0005 exits0 and produces game NRO `e3b6f632efa5e26e6aa14674252fe30ba3e5f5374634312e55376906c85119a1`. Frozen local source/build/test/binary evidence: `game/build/evidence/palette-e3b6f632-20261028/manifest.json`. Patch SHA-256 `d6048005abaacfaf01f1b87837d39674bc0d09b64a68c0531ed8d6258b39cc0d`; regression runner `f560b0493cfc60e88acd33b2e5c65e552fa4cf41bc9f473b112ee66df9b01e64`; final test log `98470d526c11c301a8c39b53fc4dea6785d190ab3be795375bdd8365e99e5c30`. No game/menu run or GPU palette upload is established by this host regression/build; active menu palette lengths, shorter-palette semantics, mirrors and hardware remain UNVERIFIED. Remove patch0005 and rebuild to roll back.

Provenance retention note: the earlier profiling manifest points at four build intermediates replaced by this later build (NRO/ELF/full compiler log/prepared executor). Its original timing/navigation logs remain retained, and those executed results are historical. Do not claim all old manifest paths still match the old hashes. The local integrity audit is `game/build/render-profile/integrity-audit-after-palette-build.json`; new proofs retain separate frozen evidence directories to avoid that overwrite. No proprietary artifacts are committed.
