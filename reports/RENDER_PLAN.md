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

## RENDER-MENU-STATE-001 result — complete CPU menu-frame capture

**PASS for diagnostic capture only.** Patch0006 adds gated records to the existing frame trace; it changes no rendering algorithm and registers no backend. Host backend/trace CTest2/2 and the palette original-defect negative control pass. Full Switch build exits0, NRO `a1fdb54d2c1b6d44131c62d6a77330773cc6178d6fc0d6c12d8c66e992a01213`. Independent reviewer render_analysis inspected source/config/build/tests and the actual capture.

A bounded180-second Eden/llvmpipe run reached the original menu and remained alive without logged guest/Eden faults. The flag was armed after startup; exactly one complete traced frame spans Eden timestamps128.682469–136.988611. Those timestamps include diagnostic overhead and are not an FPS result. There are28draws, each with state/depth/surface plus four texture records. Actual complete images before and after tracing show MAIN MENU with PLAY highlighted. Raw program words, logs, frames and binaries remain private.

The current CPU combiner model interprets23batches as texture0 multiplied by diffuse and five as diffuse alone. A private compiled reference check exercised both captured programs on128synthetic channel cases and passed; source review confirms their inputs do not read the final constants omitted by the older trace. This establishes the sampled CPU-model interpretation, not original Xbox/GPU correctness. A valid stale texture descriptor is not evidence that a batch samples it: joining the program state to each draw is required.

| Actual requirement in this frame | Existing evidence | Gap before backend registration |
|---|---|---|
| 23 texture0×diffuse batches;5 diffuse-only | Captured program/source interpretation and CPU reference checks | Carry/derive the correct fragment mode; texture pointer alone is insufficient |
| Alpha GREATER, reference0 | GPU test covers reference128 | Zero-alpha boundary after combining/filtering |
| ADD SRC_ALPHA/INV_SRC_ALPHA,27draws | Representative GPU proof passed | Apply to the actual modulated/bilinear fragment output |
| ADD ONE/ONE,1draw | ONE/ONE tested with reverse subtraction only | Additive ONE/ONE readback |
| Clamp U/V; bilinear min/mag; one mip | Nearest clamp/repeat proof passed | Bilinear half-texel/edge/interpolated-alpha proof |
| Active P8:5draws with256entries,1with32 | Bounded256-entry helper passed | Bounded32-entry decode/index validation and upload |
| Active swizzled ARGB8:15draws; DXT2/3-class:2 | Formats identified from upstream definitions | Independent decoder/channel/alpha fixtures and upload |
| Full colour mask; cull/depth/stencil disabled | Matching disabled-state/full-mask proof | This frame alone requires no active depth/stencil/cull; later states remain unsupported |
| Linear640×480, AA1, X8R8G8B8 variant; Z24S8 bound but disabled | Smaller synthetic RGBA8 targets | Surface identity/X8 semantics, readback and completion ordering |

Texture-stage0 sampling operation is projected2D; the descriptor lacks a separate textureQ. This trace records render/sampler/program state, not all consumed vertex outputs, so Q=1 must be verified or Q must be carried before using a nonprojective shader. Other stages' descriptor validity does not imply sampling. One frame does not establish all menu states, level graphics or gameplay coverage.

Frozen local evidence: `game/build/evidence/menu-state-a1fdb54d-20261028/build-manifest.json` and `capture-manifest.json`, with source/patch/NRO/ELF/config/build/test/raw capture/parsed records/reference-check/frame hashes. The independent reviewer verified all14build and12capture artifacts. Trace flag was removed and prior env restored after capture. No proprietary artifacts are committed. The public report contains authored conclusions rather than copied game programs or logs.

Before registration, choose the measured subset with explicit unsupported-state rejection, rather than an unverified universal NV2A implementation. Timebox the next proof to one implementation/build/readback/review iteration: verify projectedQ, close bilinear/alpha0/additive blending/32-entry palette and actual format decode gaps, then specify coherent guest-surface and fence handling. If reference comparisons fail repeatedly, gather new evidence before expanding. No menu GPU renderer, speedup, hardware compatibility or gameplay gate is claimed here.

## RENDER-MENU-PROOF-001 — synthetic sampling subset

The extended pinned deko3d proof passes two coordinate cases, fifteen state cases and nine unsupported-state rejection checks in Eden/llvmpipe. All checked channels have zero errors above tolerance; maximum absolute channel error is1. New cases independently compare bilinear clamp, texture×diffuse, diffuse-only output despite a bound texture, rounded eight-bit GREATER0 alpha testing, normal blending and ADD ONE/ONE. The original ten state cases remain. Bilinear coverage includes923 mixed samples,1794 edge samples and177 centre samples;1890 samples distinguish linear from nearest. Transparent diffuse-only output discards all2783 interior samples. These are synthetic readbacks, not original-title GPU rendering or performance results.

Source review by q_trace and root accepted the scoped mappings/reference calculations. Root inspected the terminal log and independently verified all ten frozen artifact hashes in `game/build/evidence/menu-sampling-b6d31d94-20261028/manifest.json`. Final NRO SHA-256: `b6d31d94c9b44aad74d0a6e70d3160b2098e3d06129cafeb5a7401c348f5323f`. The new diffuse cases use unit reciprocal W; nonunit-W diffuse interpolation equivalence remains UNVERIFIED. Decoder composition, projectedQ, guest-surface/fence coherence and backend registration remain separate gates.

## RENDER-DECODE-001 — bounded menu-format helper

Patch0007 supports the four hardware P8 palette sizes32/64/128/256 in the backend helper. Palette/image spans and alignment remain bounded; a single index read is checked before palette lookup. Missing palettes still fail before reading image data. The temporary palette bound is restored with the entire saved texture, preserving the existing CPU renderer. Synthetic fixtures cover each short palette's last/invalid index and palette rewrites, asymmetric4×2 swizzled ARGB8, DXT2/3-class explicit alpha/four-colour reversed endpoints and same-address block rewrites. CPU pixel checks establish restoration after successful and failed helper calls.

The combined patch0001–0008 host suite passes3/3. Both negative controls pass by detecting their intended failures: the original palette-dropping defect produces eight decoded-pixel failures; a forced leaked palette limit produces both CPU-pixel restoration failures. Independent source/config/log/hash review by q_trace accepted this scoped host gate. Shared full Switch build exits0, NRO `6d05c9d376198a93a27e0128d01ea69a252cd61c27dd04d6ace659e4eb31e811`. Frozen build evidence: `game/build/evidence/menu-q-6d05c9d3-20261028/build-manifest.json`; host procedure details: `game/build/menu-decode/host-manifest.json`. GPU upload/channel composition with the helper, arbitrary formats/memory mirrors, original Xbox equivalence and hardware remain UNVERIFIED. No backend is registered.

Capture setup note: the first Q-capture attempt used the runner's optional read-only game bind; Eden reported a read-only filesystem error opening the XBE before guest execution. That runner setup failure is retained under `game/build/menu-q/failed-readonly/` and establishes no renderer or Q result. The follow-up uses the existing SD staging from the earlier menu proof.

## Next coherence proof — analysis, not implementation

Read-only source analysis identifies a minimal safe direction: synchronous clear/draw callbacks import the guest surface, execute GPU work, wait and read back into guest RAM before returning. Current semaphore release is immediate (method0x1D70), and CPU presentation reads guest RAM before `backend->flip()`. Deferring readback until flip would therefore expose stale pixels/fence completion. Keep guest RAM authoritative for the first proof; performance and concurrent access remain separate measurements.

Before registration, the interface must also expose enough surface/program information to reject unsupported cases, and provide an explicit tested fallback or error path: current void callbacks cannot silently drop an unsupported draw. The next synthetic proof should exercise padded pitch/untouched pixels, two same-size surfaces, intervening CPU writes, rendered-surface texture reads, and draw→semaphore→flip ordering. These are pending requirements, not working backend claims.

## RENDER-Q-001 — complete-frame projected-coordinate measurement

Patch0008 changes diagnostics only. The successful bounded180-second Eden run contains exactly one complete CPU menu frame, timestamps87.706320–95.743931, with28draw records. Of206accepted triangles,194triangles across23eligible PROJECT2D draws contribute582vertices: every observed Q is exactly1 and every observed position W is finite, positive and exactly6. Zero/nonfinite/other Q and invalid W counts are zero. Five draws have no eligible observations and correctly report `unobserved=1, all_one=0`; their stage0 operation is disabled. No inference is made from their stale texture descriptors or first three vertices.

Actual images before/after the traced frame show MAIN MENU with PLAY selected. The runner remains alive at its bound, with no logged critical/guest fault or returned entry point; prior env is restored and the flag removed. Diagnostic timestamps do not establish FPS. Frozen local evidence contains19build and17capture artifacts under `game/build/evidence/menu-q-6d05c9d3-20261028/`, including the raw trace, parsed records, reference parser and actual images; failed runner setup is separate.

This establishes Q=1 only for the captured eligible vertices. Future native registration must carry Q or explicitly reject other Q before drawing; it must not assume all title frames/levels share this value. Constant W in these observed triangles does not establish the unobserved diffuse-only draws' W, nor arbitrary nonunit-W diffuse interpolation. Surface/readback/fence coherence and registered native rendering remain pending.

Independent final Q review by menu_decode accepted the source, raw→parsed record correspondence, both actual images, cleanup and all19build+17capture hashes. Config/result documentation was independently reviewed by q_trace. RENDER-Q-001 and the declared preparatory RENDER-MENU-PROOF-001 subset are complete; this does not close the native GPU backend blocker.

## RENDER-COHERENCE-001 — synchronous executor/GPU proof

**PASS within the declared synthetic scope.** Patch0009 adds optional side-effect-free draw/clear preflight callbacks and surface format/clip/clear metadata. Rejection uses the existing CPU path before transformation; it never retries a stateful vertex program. Backends without preflight retain their previous source-level behavior. Host CTest3/3 passes, including accepted/rejected routing, actual CPU pixels, and a stateful program whose constant changes exactly once. The original palette-dropping negative control still detects eight decoded-pixel failures. Independent source/host-log review accepted this gate.

The standalone NRO links the real portable executor, vertex interpreter, combiner and flip-hold units to generic fake guest RAM and synchronous deko3d callbacks. On each accepted operation it imports current guest pixels, submits GPU work, waits/checks the queue, then reads results back before returning. Two clears and three draws pass independent rectangle/full-buffer checks at32×24 with144-byte pitch (16padding bytes per row): untouched pixels/guards, two same-size surfaces, intervening CPU writes, and a rendered surface decoded from guest RAM as the next texture. The test checks completed pixels before method0x1D70 releases a semaphore, then verifies presentation reads the last drawn surface even after the current target changes, before the backend flip callback. Unsupported state also exercises actual CPU draw/clear pixels.

The clean pinned build/Eden run exits0 with five callback commits and the final PASS marker. Proof NRO SHA-256 `0f056c1870af5f381b1fd11a649e5c955e96bdab6b1bde8ff9f8459de29b7239`. A deliberate `COHERENCE_SKIP_READBACK` build still completes all five GPU callbacks but produces the expected guest-RAM ordering/readback FAIL and runner exit1. This negative control establishes stale-RAM sensitivity. Full Switch game build exits0, NRO `10298000ed14dda1b6439db7cffb0739e8e7cb3845aded50b88b2e3eeb710bde`. Final36-artifact manifest: `game/build/evidence/coherence-0f056c18-20261028/manifest.json` (includes both reused shaders, all maintained patches, source, binaries, host/build/run logs and negative control). No proprietary inputs are used by the standalone proof; all generated artifacts remain private.

Early experimental runs returned NO-RESULT; progress markers localized the draw boundary, and the final implementation uses a dedicated sampler-free diffuse shader. Buffered logging temporarily obscured successful progress; no ongoing hang or general Eden bug fix is claimed. Earlier artifacts are retained separately under `game/build/evidence/coherence-1eaff413-20261028/`. Review also caught a stale unused shader in the old RomFS; the final wrapper cleans before rebuilding, and the clean run passes.

This is a fixture, not a game backend: it accepts only declared unit-W FFP/basic opaque-state inputs, exact test surfaces, and white diffuse for textured draws. It rejects program/combiner paths; accepted per-vertex/resource failures stop the test rather than silently dropping work or replaying programs. Other formats/states, concurrent CPU access, asynchronous/cached surfaces, game GPU rendering, hardware and performance remain UNVERIFIED. Current game rendering stays on the CPU; no fresh game-menu/playability acceptance follows from this build alone.

The smallest next native-menu slice needs generic authored combiner semantics (texture×diffuse/diffuse-only), post-transform full-batch Q/W eligibility without replay, actual menu surface/decoded-texture/bilinear/alpha/blend composition, explicit CPU fallback, then original-menu image/navigation comparison and independent review. Do not register the restricted coherence fixture as the title renderer.

Independent final review by coherence_contract (GPU proof authored by another agent) verified all36frozen hashes, clean packaging, actual positive/negative logs and full Switch build. Contract source/host gate was separately reviewed by coherence_review and root. No blocking findings remain for the declared fixture. Main fixture code uses warnings-as-errors; the unchanged upstream translation units retain warnings without failing their compilation.


## Native menu regression follow-up — 2026-10-29 (in progress)

Expanded synthetic executor/native comparisons found reproducible failures in the previous shader: ten blend-factor combinations exceeded the one-channel-value tolerance (maximum error3), and two near-zero-alpha filtered cases disagreed on discard. Evidence is private in `game/build/menu-native/old-quant-eden.log`; these failures block native integration. The CPU path packs final source colour before blending, after its rounded alpha comparator. Its register-combiner texture path performs floating-point bilinear interpolation; pre-quantizing filtered texture samples would change that reference and is not the proposed fix. Explicit float filtering and final-source packing are under test.

A separate source defect affects diffuse-only draws with huge unused texture coordinates: scaling stale descriptors can produce nonfinite UVs which the adapter must ignore and sanitize, because that mode does not sample a texture. A regression is being added. No successful fix, registered game rendering or hardware acceptance is claimed here. Registration/configuration source review found no blocker; full build, shader dependency execution and actual game image/navigation evidence remain pending.


Final scoped native proof resolves the preceding regressions: exactmoduleclear,31GPUcases and5CPUfallbacks pass (NRO9cf37602), alongside a separate non-combiner CPUfallback binary127fa721. Full Switch NRO9666205d builds, and executed shader-touch dependency checks pass. Independent reviewers inspected exact source, raw results and all frozen manifests. Detailed scope and retained negative evidence: [RENDER_MENU_NATIVE.md](RENDER_MENU_NATIVE.md). Actual registered game menu/navigation remains pending; edge-band equality, gameplay, concurrency, performance and hardware remain UNVERIFIED.
