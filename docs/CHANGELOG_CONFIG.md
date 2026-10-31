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

## 2026-10-28 — synchronous backend contract/proof

Patch0009 and a standalone synthetic executor→deko3d coherence proof add tested synchronous guest-RAM readback. Optional preflight chooses CPU fallback before batch transformation; surface/clip/clear metadata permits strict rejection. No title backend registration, option or default change. Host3/3, clean synthetic build/Eden PASS, skipped-readback negative control and full Switch build pass; independent source/host/actual-log/all36frozenartifact review accepted under RENDER-COHERENCE-001. The wrapper cleans old RomFS outputs before building. Rollback: remove the isolated proof/script and patch0009 and rebuild.

## 2026-10-28 — aligned-allocation contract

Moves the Horizon allocation shim into a shared testable C unit and corrects invalid-alignment/rounding-overflow handling. Host69checks, Horizon69checks, deliberate validation-removal control, full Switch build and independent source/config/all23frozenartifact review pass. Actual allocator exhaustion was not induced. No runtime option/default change. Rollback: restore the previous shim and remove the isolated helper/test integration.

## 2026-10-29 — guarded native menu option (Eden menu validated)

Adds experimental `MANHUNT_GPU_BACKEND=1`; unset/0 keeps CPU rendering, invalid values or native initialization failure stop startup. Shader RomFS assets are tracked build dependencies. Native device teardown waits for process exit, avoiding destruction under bridge callbacks. Rounding/unused-UV/bounds regressions, exactmoduleclear/draw/fallback tests, full integration build and independent synthetic/build review pass; actual menu comparison/navigation pass the scoped Eden gate. The measured emulator menu presentation rate improves; gameplay and hardware performance remain UNVERIFIED. Rollback: unset the option or revert the isolated integration and rebuild.

The synthetic menu proof adds `MENU_PROOF_NO_RC=1` for a separate non-combiner filtered CPU-fallback regression; executed Eden result passes. It does not change runtime defaults.

## 2026-10-29 — Horizon memory ownership proof

Adds patch0011 and `scripts/horizon-memory-test.sh host|switch` with pinned, restricted build containers and a synthetic close/publication negative control. No game option/default changes. Host303,506 checks and Eden4,128 checks pass; full-game build and independent review pass. Single whole-object mapping view only; protection fidelity and mirrors remain open. Rollback: remove0011/proof and rebuild.

The memory proof accepts `EMU_RUN_SCRIPT` to select the existing runner from an isolated worktree; no runtime option is introduced.

Native transform eligibility now bounds common W to1e-3..1e6 and sampled coordinates to1e6texels before diversion; diffuse-only mode ignores unused UVs. Explicit float filtering and final source packing pass the scoped prior regression run; final bounds/clear/non-combiner runs and full build pass; actual menu comparison passes; gameplay remains UNVERIFIED.

## 2026-10-29 — private partition write

Removes the incorrect global Horizon `pwrite` shim; its sole caller uses a private descriptor and checked seek/write with meaningful failure reporting. Adds `scripts/horizon-partition-test.sh host|switch` using synthetic files and existing runner directory/log settings. Host28/Eden25 checks and missing-seek control pass; full Switch build passes, independent integration review passes. No game option/default change. Rollback: remove0012 and restore the previous shim, then rebuild.

## 2026-10-29 — synchronous native queue lifetime

Reuses one lifetime graphics queue without changing synchronous guest-RAM ordering; invalidates newly loaded shader code before binding. Old repeated-queue control OOMs; candidate671draw proof and independent review pass, fullSwitchbuild passes. Actual native-menu240s recapture/navigation pass independent review. No new option/default. Rollback: disable the native option or restore the previous adapter and rebuild.

## 2026-10-29 — Horizon UTC conversion proof

Adds strict Horizon calendar conversion and `scripts/horizon-time-test.sh host|switch`; no option/default changes. Host1,632,300checks, old-code defect reproduction and removed-validation control pass. Horizon1,632,300checks, fullSwitchbuild and independent25artifact review pass. Rollback: remove0013/proof and rebuild.

## 2026-10-29 — optional Intel game runner (game validation pending)

Adds opt-in `infra/emulator/wayland/run-game.sh` with writableEMU_DATA, retainedEMU_LOG, bounded marker/duration runs and Intel prerequisites. Uses the existing pinned image and least-privileged project container; no default runner/host change. Knownpixels and10second bounded run pass independent13artifact review. Actual game validation pending; adversarial shutdown/guestFAIL execution UNVERIFIED. Rollback: remove the isolated runner/report.

## 2026-10-30 — LEVEL-LOAD-001 generation provenance

- Apply maintained recompiler patches before analysis/lifting and import tools from the patched tree. Include patches in the generation cache key so translator changes cannot silently reuse old output.
- Document reviewed bounded function additions separately from hand-written replacements. Level-load build/runtime acceptance remains pending; details in `reports/LEVEL_LOAD.md`.

- Added reviewed bounded entry `0x0005C620..0x0005C69C`; patch0014 expands existing bounded switch-table scans to4096 for the measured300-entry table. Synthetic author/independent regressions pass; rebuilt gameplay acceptance is still pending.

## 2026-10-30 — Pad script stick directions

`MANHUNT_PAD_SCRIPT` steps now accept full-deflection stick directions `lup ldown lleft lright` (left stick) and `rup rdown rleft rright` (right stick), combinable with buttons via `+`; opposite directions in one step cancel. Needed to test player movement for LEVEL-LOAD-001. Existing scripts are unchanged: button parsing and timing are untouched, and steps without stick tokens leave the sticks at 0. Validation: `tests/pad-script/test_pad_script.c` passes; the same test against the previous parser fails its two stick checks (negative control). Switch/Eden behaviour UNVERIFIED until the next level-load run. Rollback: revert `game/src/pad_script.c`.

## 2026-10-30 — Unbounded D3D fence wait with the executor

`D3D_BlockOnTime` (`game/src/d3d_fence.c`, guest 0x00292200) no longer gives up after 250 ms when `RECOMP_PB_EXEC` is set (the host and Switch boot default); it waits for the GPU semaphore like the original's final spin, logging `still waiting` every 2 s while it waits. As in the original (0x0029221E), a fence outside the window (semaphore, newest] returns at once. Without `RECOMP_PB_EXEC` the logged 250 ms bound is unchanged. No new switch. Reason: the early return let D3DResource_BlockUntilNotBusy (0x002926B0) report a render target idle while the executor still had queued draws into it; the title freed and reused that memory, and the software rasteriser then overwrote a live allocator header (LEVEL-LOAD-001). Validation: `tests/d3d-fence/run.sh` passes both modes, including out-of-window fences, and fails against the previous code. One instrumented host run (300 s bound) recorded no header change across 1,856 watched blocks, and one plain host run recorded no crash; neither log records its exit code or exact duration (last log timestamps 206 s and 272 s). Private evidence: `game/build/level-load/fault2/`. Risk: an in-window fence the executor never reaches is now a logged hang instead of silent corruption; the cost of the longer waits is not measured. Switch timing UNVERIFIED until the next Eden run. Rollback: revert `game/src/d3d_fence.c`.

## 2026-10-31 — read-only spawn proof parameters

Documented existing periodic pointer-chain and watchdog peek syntax, including the separate parsers and intentional watchdog exit3. No runtime defaults or game source change. The temporary existing-NRO test configuration and restoration are recorded privately; Switch result remains pending in `reports/LEVEL_LOAD.md`.

## 2026-10-31 — Optional SD-card log on Switch (HW-LOG-001)

New Switch key `MANHUNT_LOG_FILE=sdmc:/<file>` (default unset = unchanged behaviour). When set, `game/src/main_switch.c` also writes every runtime log line to that file on the SD card, flushed per line, preceded by the boot and config lines logged before it was opened (8 KiB buffer), and stops at 32 MiB with a final `[MH-LOG] size limit reached` line. The file is truncated at every start (copy the previous log off the card first). A failed write (SD full or removed) closes the file and logs `[MH-LOG] file write failed, file log stopped` on the debug channel; if more than 8 KiB of lines precede the open, the file records `[MH-LOG] early lines beyond 8 KiB were not kept`. There is no fsync, so a hard power loss or crash may lose the last lines. The log lock serialises all threads, so heavy logging can slow the game on a console (UNVERIFIED). A path that does not start with `sdmc:/` or cannot be opened logs `[MH-BOOT] log file ...: FAILED` to the debug channel and nothing else changes. Reason: on a physical console `svcOutputDebugString` is not visible, so a hardware run returned frame dumps only (docs/MACOS_SWITCH_SETUP.md, section 4). Cost: one flushed SD write per log line while enabled (diagnostic use). Validation: three short Eden runs (see reports/LEVEL_LOAD.md, "HW-LOG-001 check"); physical-console behaviour UNVERIFIED. Rollback: revert `game/src/main_switch.c`.

## 2026-10-31 — Switch config lines: 4,095-byte limit, over-long lines rejected

`config_init` (`game/src/main_switch.c`) read `sdmc:/manhunt.env` with a 512-byte buffer, so a longer line was cut at 511 bytes and the remainder was silently dropped: a 1,086-character `MANHUNT_PAD_SCRIPT` lost every step after the 26th, which invalidated a 111-minute Eden run (found when the logged input steps stopped at the cut). The buffer is now 4,096 bytes; a line that still does not fit is ignored whole (never applied cut-off), the rest of that line is discarded, and `[MH-BOOT] config line longer than 4095 bytes ignored (starts "...")` is logged. Lines that fit (at most 4,094 bytes of content plus LF, 4,093 plus CRLF; a final line without a newline at most 4,094) behave as before; the logged message says "longer than 4095 bytes" although exactly 4,095 bytes already no longer fit. A line containing an embedded NUL byte is still not detected as over-long (pre-existing). Validation: two 60 s Eden runs: a 1,045-byte `MANHUNT_PAD_SCRIPT` line parses completely (`pad script: 53 step(s)`); a 5,000-byte line is ignored with the message and the following line is still applied. Physical-console behaviour UNVERIFIED. Rollback: revert `config_init`.
