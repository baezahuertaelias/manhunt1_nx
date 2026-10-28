# Rendering blocker: investigation and proof plan

Date: 2026-10-28. Task: **RENDER-001 — OPEN**. This is source inspection,
not a measured performance result. Required Switch-build frame-time breakdown
has not been collected; deko3d backend behaviour is **UNVERIFIED**.

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
