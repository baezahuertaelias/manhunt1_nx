# EMU-GPU-PRESENT-001 — Intel Vulkan with container Wayland

2026-10-28: **PASS for the synthetic deko3d proof**. A headless Weston
compositor fixes the presentation prerequisite without host changes. The
existing Xvfb runner remains unchanged. Game execution on this runner and
physical Switch graphics are **UNVERIFIED**.

## Root cause and proof

With renderD128 passed to the existing Eden image, `vulkaninfo --summary`
under Xvfb emitted `No DRI3 support detected - required for presentation`.
Intel ADL-N enumerated correctly. Separately, `glxinfo -B` under Xvfb reported
llvmpipe and `Accelerated: no`, so changing Eden to OpenGL on Xvfb is not a
hardware solution.

One presentation hypothesis was tested: Weston headless with its GL renderer,
a private Wayland socket and the Intel-only Vulkan ICD. `probe.sh` verified:

```text
GL renderer: Mesa Intel(R) Graphics (ADL-N)
Surface type = VK_KHR_wayland_surface
deviceName        = Intel(R) Graphics (ADL-N)
present support             = true
EMU-GPU-WSI PASS (Eden execution UNVERIFIED)
```

A subsequently authorized Eden run used the existing synthetic deko3d NRO.
Its source checks exact red/clear pixels, coverage and every row's triangle
geometry. Executed result, exit 0:

```text
Driver: Intel open-source Mesa driver 25.0.7
Device: Intel(R) Graphics (ADL-N)
DEKO3D-PROOF PASS center=ff0000ff corner=ffff0000 red=460800 clear=460800 other=0 coverage=50.000% bad_rows=0
EMU-GPU-PRESENT PASS
```

The guest marker appeared at emulator elapsed time 8.123 seconds; that is not
an FPS measurement. No guest game addresses apply to this synthetic proof.
The run also logged empty-path setup errors and uninitialized networking;
these did not prevent the stated guest readback assertions. No general
compatibility claim follows.

## Reproduction and provenance

From the repository root:

```sh
infra/emulator/wayland/build.sh
infra/emulator/wayland/probe.sh
infra/emulator/wayland/run-proof.sh tests/deko3d-proof/deko3d-proof.nro
```

- Source baseline `30b5e9e7892934cc599737cb080f57325986f5aa` plus this change.
- Base image, checked before build:
  `sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb`.
- Built image:
  `sha256:047af837b0c8cecb359fb7e7694cbfa01cf7c397bd5bb7555337ab01ffa7d788`.
- Weston and libweston-14-0: `14.0.2-1`, from Debian's signed trixie package
  indices using the inherited Debian archive keyring. Apt signature/hash checks
  were not bypassed. No host packages or services changed.
- Complete installed package manifest is retained inside the image at
  `/usr/local/share/manhunt-wayland-packages.txt`, SHA-256
  `47cb330245658482eb0c5329505ac2758f10f04ae1598c7adf61dc3dd864f554`.
- NRO SHA-256:
  `8d8483073531bdca807576b5595f07b183c14bd402c234b0ad9c83d4991a7114`.
- Local synthetic log `/tmp/manhunt-wayland-deko3d.log`, SHA-256
  `540add97534251b5a54c92cf426fa082a6c4c2aab5eeaeaeb6158b7db714ae3c`.
  This log is local only; the report contains synthetic result excerpts.
- Shell syntax checks passed for all three scripts. Package install/build,
  WSI probe and synthetic emulator run all exited 0. Independent review pending.

The probes run nonroot, without network, with dropped capabilities and
no-new-privileges. Only renderD128 is exposed. WSI gets one CPU/1 GiB;
Eden gets one CPU/6 GiB and a read-only NRO mount. Compositor socket,
configuration and caches are temporary container files. Emulator wait is
bounded to 120 seconds, compositor readiness to 10 seconds. No public ports,
firmware, keys or game files are used.

## Limits and rollback

The base remains host-local with unverified source provenance (EMU-IMAGE-001).
Weston packages are version-pinned; transitive packages are recorded in the
manifest but use live signed Debian repositories. This is not a byte-reproducible
emulator image. A rebuild with a different image ID must be inspected and
reviewed before changing the probe scripts' pinned IDs.

Do not label hardware presentation as fixed for the existing Xvfb runner.
The standalone optional Wayland path is the passing configuration. No runtime
or gameplay code changed. Roll back by removing `infra/emulator/wayland/` and
this report; the default runner is unaffected.
