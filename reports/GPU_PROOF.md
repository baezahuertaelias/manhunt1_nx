# GPU-PROOF-001 — container hardware rendering

2026-10-28: **PASS within standalone GBM/EGL readback scope**. This is a
synthetic OpenGL test, not an Eden run or Switch hardware evidence. Eden's
Vulkan/Xvfb present-queue failure remains unresolved.

Run `scripts/gpu-proof.sh`. The freshly authored source is
`tests/gpu-proof/main.c`; it uses no game, firmware, keys or SDK assets.
It opens only `/dev/dri/renderD128`, creates a surfaceless GBM/EGL context,
rejects named software renderers, clears an 8×8 RGBA8 framebuffer and reads
its center pixel. Every channel must match `(64,128,191,255)` within one.
Any setup, compilation, renderer, GL or readback failure exits nonzero.
The self-test checks software-renderer rejection and one wrong value for
each pixel channel. No guest addresses apply.

## Executed evidence

- Source baseline: `1273eb1f3cfeff8060216487707564436f864873` plus this change.
- Image: `sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb`.
  This is the existing host-local Eden image; reproducible image builds remain
  EMU-IMAGE-001. The probe is compiled from repository source each run.
- Source SHA-256: `f5f7ce3a17354b4af37f08c920e9b13101dc11fb8415e57e4bdc7a2df62137ab`.
- Runner SHA-256: `f14d1fe29dbd321ecdb1718608b51c39a5c597b355d7942acfc59a50a8b0e8aa`.
- Compiler: Debian clang 19.1.7 (3+b1); pkg-config EGL 1.5, GBM
  25.0.7-2+deb13u1, GL 1.2.
- `bash -n scripts/gpu-proof.sh`: exit 0.
- `scripts/gpu-proof.sh`: exit 0, warning-free build with
  `-std=c11 -Wall -Wextra -Werror`.

```text
GPU-PROOF classifier/pixel self-test PASS
renderer=Mesa Intel(R) Graphics (ADL-N) vendor=Intel
GPU-PROOF PASS rgba=64,128,191,255
```

The container runs as the invoking uid/gid with no network, dropped
capabilities, no-new-privileges, one CPU and 512 MiB RAM. Source is mounted
read-only; the sole device is the render node. Build/cache files are temporary
and removed with the container. No host packages or services changed.

A separate exploratory run setting `LIBGL_ALWAYS_SOFTWARE=true` still selected
the Intel GBM renderer and passed; it is **not** a software negative control.
Software rejection is covered by the classifier self-test, not by an executed
software-context run. Arbitrary future software renderer names are not known.

## Limits and rollback

Eden Vulkan presentation, emulator GPU rendering, physical Switch graphics and
FPS are **UNVERIFIED by this test**. No runtime configuration changes. Remove
these three added files to roll back. Independent review is required before
integration; no merge is claimed here.
