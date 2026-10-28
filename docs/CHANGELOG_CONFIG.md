# Configuration changelog

## 2026-10-26 — Analysis image

Added `infra/analysis/Dockerfile` installing the hash-verified ghidra-xbe loader on `dd-ghidra-mcp:local`. Validation: built image `sha256:e513e945…8d68`; `analyzeHeadless` selected the XBE loader. Rollback: remove the image.

## 2026-10-26 — Pinning and compose

- Dockerfile: `BASE_IMAGE` arg, Ghidra 12.1.4 version assertion; removed reliance on undeclared tools (curl/unzip confirmed present in base via `docker image history`).
- `scripts/build-analysis-image.sh` checks the base image ID before building.
- devkitA64 pinned by digest in `scripts/build-nro-proof.sh` and `infra/compose.yaml`.
- `infra/compose.yaml` added (analysis, switch-build); `docker compose config -q` passes.
- `.env.example`: `GAME_INPUT_DIR` corrected to `assets/manhunt1_xbe`; `UID`/`GID` added.
- `.gitignore`: removed stray `/Manhunt1_xbe/`; added `*.nacp`, `*.o`, `*.d`, `*.gpr`, `*.rep/`, `*.sqlite`.

Validation (rerun 2026-10-26): NRO rebuild PASS, reproducible. Analysis image rebuild FAIL on the new version assertion: the base is Ghidra 12.1.2, the loader is 12.1.4. The guard caught a real mismatch in the previously built image. Rollback: revert these files.

## 2026-10-26 — Own Ghidra 12.1.4 base (replaces `dd-ghidra-mcp:local`)

- `infra/analysis/Dockerfile` is now multi-stage: digest-pinned Temurin JDK 21, official Ghidra 12.1.4 zip (SHA-256 checked), ghidra-xbe loader (SHA-256 checked), and an assertion that both are 12.1.4. Runs as uid 1000; entrypoint `analyzeHeadless`.
- `scripts/build-analysis-image.sh` no longer checks a local base ID.
- Compose `analysis` service: removed the GhidraMCP port (the server is not in the new image), added `network_mode: none` and `hostname: localhost`, and scripts now come from `analysis/ghidra_scripts`.
- Validation: image `sha256:fc761493…4178` built; headless import of `DEFAULT.xbe` succeeded with the XBE loader. `DumpLayout.java` output matches `reports/XBE_HEADER.md` (entry, base, 15 blocks).
- Lost: GhidraMCP. Re-add it as a separate layer only if an agent needs it.
- Rollback: rebuild from the previous Dockerfile in Git history (commit `9a38897`).

## 2026-10-26 — Recompiler image and emulator runner (milestone 1)

- Added `infra/recompiler/Dockerfile`: pinned Python base, xboxrecomp at a fixed commit, capstone/unicorn/pytest pinned, host and AArch64 gcc, qemu-user.
- Added `scripts/recomp-proof.sh`, `scripts/emu-run.sh` (Eden, checked by image ID) and `scripts/toolchain-proof.sh`.
- Validation: `scripts/toolchain-proof.sh`: gates c, a/b (Linux and Eden) and d PASS. Negative controls are detected (reports/RECOMP_EVAL.md).
- Known gaps: apt packages in the recompiler image are not version-pinned; the Eden image is not reproducible from this repo (EMU-IMAGE-001).
- Rollback: delete `infra/recompiler/` and the three scripts.

## 2026-10-26 — All host cores

`CPUS` (default `nproc`) for every container and parallel build (user decision). Heavy jobs stay serialized. Validation: milestone-1 suite, identical NRO hashes. Rollback: set `CPUS=3`.

## 2026-10-27 — Milestone 2 build and runtime configuration

- Recompiler image: game build/run dependencies (rebuilt, `sha256:fc226cc9…`).
- Game analysis without `--text-only`; `game/function_fixes.json`; `game/manual_functions.json`.
- xboxrecomp patches 0001 (case-insensitive paths), 0002 (pseudo-handle close), 0003 (Horizon port: memory, directory handles, libc gaps, thread placement, zeroed views).
- Runtime switches for boot (see CONFIGURATION.md) on host and Switch.
- Switch SD layout and `sdmc:/manhunt.env`; `scripts/emu-run.sh` gains `EMU_DATA`, `EMU_SDMC_GAME` and the `-` marker.
- Validation: `scripts/switch-boot-proof.sh` PASS (Eden); host boot to menu; milestone-1 suite re-run (see BUILD_LOG).
- Rollback: revert the milestone-2 merge; the milestone-1 tooling does not depend on any of it.


## 2026-10-28 — Standalone GPU proof

Added `scripts/gpu-proof.sh` using the existing pinned Eden image, GBM/EGL readback and a restricted render-node container (one CPU, 512 MiB, no network/capabilities). This does not change the emulator runner. Validation and independent review: see `reports/GPU_PROOF.md`. Rollback: remove the standalone proof and its documentation. Eden Vulkan presentation remains UNVERIFIED.

## 2026-10-28 — Opt-in render timing

Added `RECOMP_RENDER_PROFILE=1` in maintained patch 0004, disabled by default. Existing report cadence and Switch env-file mechanism are reused; timing captures disable periodic frame dumps explicitly. Thread-local cumulative nested wall times do not estimate guest CPU time. Focused helper self-check passes; Switch build/runtime and independent review results are recorded in BUILD_LOG.md and reports/RENDER_PLAN.md as they execute. Rollback: unset option or remove patch and rebuild.

## 2026-10-28 — Optional Wayland hardware presentation proof

Added a separate project container layer and synthetic proof scripts using version-pinned Weston, the Intel Vulkan ICD and private headless Wayland display. Default Xvfb automation is unchanged. The source-provenance blocker EMU-IMAGE-001 remains open. Build, Vulkan WSI and Eden deko3d pixel assertions passed in the authored proof; independent rerun/review is recorded in reports/EMU_GPU_PRESENT.md. Rollback: remove the optional layer/proof files.

## 2026-10-28 — Synthetic NV2A attribute proof

Added a standalone pinned-toolchain build/run script using the existing Eden runner and generic upstream vertex header, with no game inputs or runtime settings changed. Two fixed surface sizes verify coordinate/channel/UV/perspective conventions. Author run and primary-agent full-wrapper rerun passed with zero pixel mismatches. Scope and rollback: remove this synthetic proof; see reports/RENDER_PLAN.md.

## 2026-10-28 — Backend P8 palette boundary

Prepared patch0005 to carry palette metadata into the decode helper, with a bounded256-entry P8 path and explicit rejection of unresolved cases. Added a standalone pinned-container host regression and original-defect negative control. CPU sampling is unchanged; no backend is registered. Host regression, original-defect negative control, independent review and full Switch build passed; exact scope/hashes are in reports/RENDER_PLAN.md. Game/GPU palette runtime remains UNVERIFIED.

## 2026-10-28 — Complete existing frame-trace diagnostics

Patch0006 adds exact per-draw render/surface/four-texture state to the existing flag-gated whole-frame trace. No runtime default or rendering algorithm changes. Host backend/trace CTest2/2 and original-defect palette negative control pass; full Switch build exits0. A complete28-draw CPU menu-frame capture and independent final review passed within diagnostic scope; support gaps and exact evidence are in reports/RENDER_PLAN.md. Rollback: remove flag/option or patch and rebuild.

## 2026-10-28 — projected-coordinate trace

Patch0008 extends the existing frame-trace gate with per-draw eligible PROJECT2D Q/W aggregates at accepted clipped triangle submission. No default or rendering algorithm changes. It includes a synthetic helper test and record-contract assertions; combined host CTest3/3 and the shared Switch build pass with independent artifact review; complete-menu capture observes582eligible Q1/positive finite W6 vertices, while five empty draws remain unobserved. Independent raw-record/image/hash capture review accepted. Empty observations and unsampled stale descriptors are explicit. Rollback: remove the flag or patch and rebuild.

## 2026-10-28 — short-palette helper

Patch0007 adds bounded short-P8 helper decoding and generic format/restoration fixtures. CPU sampling retains its previous unrestricted palette behavior outside the temporary helper state. No option/default changes or GPU backend registration. Combined host CTest3/3, original-palette and leaked-limit negative controls, the shared Switch build and independent review pass under RENDER-DECODE-001. Rollback: remove patch0007 and rebuild.
