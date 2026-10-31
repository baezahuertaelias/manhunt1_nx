# Configuration

| Setting | Value | Source | Notes |
|---|---|---|---|
| `GAME_INPUT_DIR` | `assets/manhunt1_xbe` | `.env` / `.env.example` | Read by `scripts/asset-manifest.sh` and `infra/compose.yaml`; mounted read-only |
| `BUILD_PROFILE` | `proof` | `.env` | Only profile today |
| `UID` / `GID` | `1000` | `.env` | Owner of build outputs from `switch-build` |
| JDK base image | `eclipse-temurin:21-jdk-noble@sha256:107215954359f1ab0be058a247d7c64ed6e69c71c1682d471d38ef751ec8e964` (JDK 21.0.12.1) | `infra/analysis/Dockerfile` `JDK_IMAGE` | Pinned by digest |
| Ghidra | official `ghidra_12.1.4_PUBLIC_20260921.zip`, SHA-256 `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` | Dockerfile `GHIDRA_*` | Hash from the release notes, verified at build |
| XBE loader | XboxDev/ghidra-xbe `build-202610161023`, SHA-256 `339b2f67563bbb8b5217740319b7f3eae9e9bbacf1f3a8aa29151f7ecb7d5152` | Dockerfile | Hash verified at build; the build fails unless both Ghidra and the loader are `12.1.4` |
| Analysis image | `manhunt-analysis:ghidra-xbe`, ID `sha256:fc761493cdf9751d24cf97a8d210b074eda4bfe8eef61838adf07ae1501b4178` | `scripts/build-analysis-image.sh` | Runs as uid 1000; entrypoint `analyzeHeadless`; no GhidraMCP server |
| Switch toolchain | `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282` (tag `20260219`) | `scripts/build-nro-proof.sh`, compose | Pinned by digest |
| Container limits | CPU: `CPUS` (default `nproc` = 4, all cores; parallel `make -j$CPUS`). Memory: analysis 6 GiB, switch-build 4 GiB, recompiler 8 GiB, Eden 6 GiB. All without network | `infra/compose.yaml`, `scripts/*.sh` | User decision 2026-10-26: use all cores; heavy jobs stay serialized |
| Recompiler image | `manhunt-recompiler:xboxrecomp-f0f8a46`, ID `sha256:a6de3090beb75d6141e717bee501d01dcf244a50c048657004a515ffb8e7d63c` | `infra/recompiler/Dockerfile` | `python:3.12-slim-bookworm@sha256:6a0e06f5…bc70`; xboxrecomp commit `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b` (checked at build); pip capstone 5.0.9 (module reports 5.0.7), unicorn 2.1.4, pytest 9.1.1; Debian gcc 12.2, aarch64 cross gcc 12.2, qemu-user 7.2. apt packages are not version-pinned. |
| Generated-code flags | `-O2 -std=gnu11 -fno-strict-aliasing -ffp-contract=off`, `#define RECOMP_GENERATED_CODE` | `tests/recomp-proof/run.sh`, `Makefile.switch` | `-ffp-contract=off` is required: AArch64 GCC otherwise fuses multiply-add and diverges from x87 |
| Eden image | `dd-eden:local`, ID `sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb` (Eden 1.0.0 stable, `yuzu-cmd` with local patches) | `scripts/emu-run.sh` (`EDEN_IMAGE`, `EDEN_IMAGE_ID`) | Host-local, from another project; not reproducible from this repo (EMU-IMAGE-001) |
| Eden runtime | Xvfb 1280×720×24, `SDL_VIDEO_DRIVER=x11`, `HOME=/tmp/home`, Vulkan on llvmpipe; `EMU_GPU=1` adds `/dev/dri` (currently fails: no present queue) | `scripts/emu-run.sh` | Result channel: `svcOutputDebugString` → `Debug.Emulated` log lines |
| devkitA64 packages | devkitA64 r29.2, gcc 15.2.0, libnx 4.12.0, deko3d 0.5.0, uam 1.1.0, switch-tools 1.13.1 | pinned image digest | From `dkp-pacman -Q` |
| Recompiler image (milestone 2) | same tag, ID `sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63` | `infra/recompiler/Dockerfile` | Adds cmake/ninja, SDL2, libepoxy, OpenSSL, ffmpeg dev, gdb, Xvfb, Mesa for the host game build. apt packages not version-pinned. |
| Game analysis | all executable sections (no `--text-only`) | `scripts/game-build.sh` | `.text`-only missed most XDK library functions |
| Function fixes | `game/function_fixes.json` | `scripts/fix-functions.py` | Reviewed boundary corrections and bounded added entries; fails on database drift or overlapping entries |
| Hand-written functions | `game/manual_functions.json` (XInput ×4, `D3D_BlockOnTime`, DirectSound ×3) | lifter `--manual-functions` | Their bodies are not generated; `game/src/*.c` defines them |
| xboxrecomp patches | `game/patches/xboxrecomp/0001`–`0003` | applied by `scripts/game-build.sh` | Case-insensitive paths; pseudo-handle close; Horizon port |
| Runtime switches (boot) | `RECOMP_PB_EXEC=1 RECOMP_PB_WALK=dma RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=paced RECOMP_DISPATCH_GATE=1 RECOMP_FLIP_PACING=1` | host: environment; Switch: `main_switch.c` defaults + `sdmc:/manhunt.env` | Each is needed (FEASIBILITY.md steps 2, 4, 5) |
| Generated-code optimisation | `MANHUNT_GEN_OPT` `-O1` (debug variant `-O0`) | `game/CMakeLists.txt` | `-O0` makes gdb probes read exact guest registers |
| Switch SD layout | `sdmc:/manhunt/` game data (copy, manifest-verified), `sdmc:/manhunt-save/` saves and partition images, `sdmc:/manhunt-frames/` BMP dumps, `sdmc:/manhunt.env` KEY=VALUE overrides | `main_switch.c`, `scripts/switch-boot-proof.sh` | Eden opens SD files read/write, so a read-only bind of the originals fails |
| Switch config keys | `MANHUNT_DUMP_SECS` (frame dump period, 0 = off), `MANHUNT_PAD_SCRIPT`, `MANHUNT_LOG_FILE` (default off: also write the runtime log to this `sdmc:/...` file, capped at 32 MiB), any `RECOMP_*` | `sdmc:/manhunt.env` | Without `MANHUNT_LOG_FILE` the log goes only to `svcOutputDebugString` (visible in an emulator, not on a console). A copy-ready file is `docs/hardware/manhunt.env.example` |
| Host config keys | `MANHUNT_PAD_SCRIPT`, `MANHUNT_SAVE_DIR`, `MANHUNT_MIRROR_TRAP`, `MANHUNT_TILED_TRAP` (debug traps) | environment, forwarded by `scripts/game-run.sh` |`MANHUNT_PAD_SCRIPT` steps accept buttons and full-deflection stick directions `lup ldown lleft lright rup rdown rleft rright` (`game/src/pad_script.c`) |
| Eden data dir | `game/build/eden-data` (`EMU_DATA`) | `scripts/emu-run.sh` | Writable Eden user dir; SD card is its `sdmc/` |
| Guest thread placement (Switch) | runtime threads round-robin over cores 0–2, mask 0x7; guest thread prefers core 0 | patch 0003, `main_switch.c` | Horizon does not time-slice equal-priority threads |
| Git remote | `origin` = `https://github.com/baezahuertaelias/manhunt1_nx` (**public**, by user decision 2026-10-28) | `gh repo create`, `gh api` | `main` protected: no force-push, no deletion, changes via pull request (0 approvals required) |

## Standalone container GPU proof

`scripts/gpu-proof.sh` compiles `tests/gpu-proof/main.c` with clang-19 inside the existing Eden image ID `sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb`. It uses GBM/EGL directly, independently of Eden and Xvfb. The container runs as the caller, without network or capabilities, with one CPU, 512 MiB, a read-only source mount and only `/dev/dri/renderD128` plus its owning group. HOME is temporary; execution is bounded to 15 seconds. No emulator settings change. A pass proves only container GPU rendering/readback; Eden Vulkan presentation and Switch hardware remain UNVERIFIED. The image still lacks a reproducible source build (EMU-IMAGE-001).

## Render timing investigation

`RECOMP_RENDER_PROFILE=1` enables patch `0004-render-profile.patch`; unset or any other value disables it. It is read once per thread. On Switch, set it in `sdmc:/manhunt.env`; on host, pass it through `scripts/game-run.sh`. No default changes. Executor reports require `RECOMP_PB_EXEC=1` (already a Switch default); the NV2A reporting loop is armed by the presence of PB_EXEC, PB_SCAN or NV2A_TRACE and reports while submissions progress. Existing `RECOMP_PB_REPORT_MS` controls the cumulative report cadence (default 10000 ms, minimum 100 ms); `MANHUNT_DUMP_SECS=0` disables Switch periodic BMP dumps for timing captures. `switch-boot-proof.sh` overwrites the env file, so custom profiling/input captures use `scripts/emu-run.sh` directly.

`[RENDER-PROFILE]` reports thread-local monotonic wall nanoseconds and call counts for scan, method, draw, raster, clear and present. Scopes overlap; do not add them or label wall-time remainder guest execution. Raster includes worker waits; present includes CPU scaling/copy and waits. Flip intervals are emulator diagnostics, not hardware FPS. Invalid-clock samples disqualify a clean capture. See `tests/render-profile/README.md` for exact scope and limits. `scripts/render-profile-test.sh` uses `${CC:-cc}` for a synthetic accounting self-check, independent of game input. Rollback: unset the option; remove patch 0004 and rebuild to remove disabled-path checks too.

## Optional Intel Vulkan/Wayland proof

`infra/emulator/wayland/build.sh` checks the existing Eden base image ID, then builds `manhunt-eden-wayland:weston-14.0.2` with Weston/libweston `14.0.2-1`. The inspected local output is `sha256:047af837b0c8cecb359fb7e7694cbfa01cf7c397bd5bb7555337ab01ffa7d788`; `probe.sh` and `run-proof.sh` pin that ID. Transitive apt packages are recorded inside the image; rebuilding is not byte reproducible and a changed image needs review before updating these IDs.

The optional proof uses headless Weston GL at 1280×720, private `wayland-proof` socket, `SDL_VIDEO_DRIVER=wayland`, Intel-only `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json`, and explicit Eden Vulkan backend/device 0. It assumes this host's Intel ADL-N and fails its identity checks otherwise. Probes run as the caller, without network/capabilities, with renderD128 only; WSI is limited to one CPU/1 GiB and Eden to one CPU/6 GiB. Readiness is bounded to 10 seconds and the synthetic NRO poll to 120 seconds, with an emulator lifetime cap of 125 seconds plus five seconds before SIGKILL. No settings in the default Xvfb runner change. See `reports/EMU_GPU_PRESENT.md` for executed scope and remaining provenance gaps.

## Synthetic NV2A attribute proof

`scripts/nv2a-backend-proof.sh` builds with the existing pinned devkitA64 image, one CPU/1 GiB, as the caller without network/capabilities. It requires the prepared generic upstream `game/build/xboxrecomp/src/kernel/nv2a_backend.h` and mounts only that header read-only plus the synthetic test directory; it reads no game assets. It prints header/source/Makefile/NRO hashes and uses the existing pinned Eden runner with marker `NV2A-BACKEND-PROOF` and a 120-second wait. Existing `EMU_LOG` can retain the emulator log. Fixed test cases are 128×96 and 192×128 with depth/stencil/culling/blending disabled; no game runtime defaults change. Checked vertex size and colour/UV offsets fail compile-time assertions if changed. The source/header provenance and executed scope are recorded in reports/RENDER_PLAN.md; physical hardware remains UNVERIFIED.

## Backend palette regression

`scripts/backend-palette-test.sh` uses the existing milestone-2 recompiler image ID `sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63`, one CPU/1 GiB, no network/capabilities, and the invoking uid/gid. Only the maintained patch directory is mounted read-only. The upstream synthetic fixed-function backend fixture is compiled in temporary container storage after applying patches; no game inputs, generated code, keys or host packages are used. A second build recreates the palette-dropping defect and must fail the explicit decoded-pixel regression. This is host synthetic validation, not Horizon/game/hardware proof. Patch0005 is inactive for the CPU renderer because no GPU backend is registered. Rollback: remove patch0005 and the standalone regression script, then rebuild.

The NV2A proof now includes ten fixed synthetic texture/state cases plus five active-state rejection checks; supported and unverified modes are listed in `tests/nv2a-backend-proof/README.md`. It still registers no backend and consumes no game textures.

## Opt-in whole-frame render-state trace

Patch0006 extends the existing `RECOMP_FRAME_TRACE=<flag-file>` diagnostics without adding an option or changing rendering. On Switch the flag path is an emulated SD path, for example `sdmc:/manhunt-menu-state.flag`. Once the file appears, tracing begins at the next flip boundary and ends at the following one. Removing the flag re-arms tracing; leaving it present does not repeatedly dump frames. The original trace/combiner lines remain; new per-draw state/depth/surface lines and exactly four texture records include alpha/masks/culling/stencil/raw formats and palette/filter metadata. An absent flag leaves the added output disabled. Diagnostics add overhead when active and cannot establish FPS.

The synthetic upstream CTest fixture checks off/on stdout parity, disabled silence and complete records including a128-entry P8 descriptor. `scripts/backend-palette-test.sh` applies the patch and runs that test alongside the existing backend regression, with no game data. This is a trace-contract test, not short-palette decoding support. Rollback: remove the flag/option; remove patch0006 and rebuild to remove diagnostics completely. Logs/frames derived from the title remain gitignored.

## Projected-coordinate diagnostic

Patch0008 adds one `projected_q` record per draw to the existing opt-in frame trace. It aggregates Q and position W from every accepted clipped CPU triangle eligible for stage0 PROJECT2D sampling. This is a conservative superset: a combiner can ignore an otherwise valid texture descriptor. Counts, finite ranges and invalid values describe those vertices only; empty observations never establish Q=1. The disabled path emits no records. No option or rendering calculation changes. Rollback: remove the trace flag, or remove patch0008 and rebuild. Combined host CTest3/3 and the shared Switch build pass with independent artifact review; a complete-menu capture observes Q1/positive finite W6 for582eligible vertices, with five unobserved draws explicitly unproven. Independent raw-record/image/hash capture review accepted under RENDER-Q-001.

## Backend menu-format decoding

Patch0007 extends only the backend decode helper's P8 validation to hardware palette sizes32/64/128/256, rejects indices outside that declared palette, and restores prior CPU texture state after success or failure. No runtime option or backend registration is added. Generic ARGB8/DXT23 fixtures cover the other formats observed in the captured menu; they do not prove guest surface coherence or GPU upload. Combined host CTest3/3, both deliberate-defect controls and the shared Switch build pass; independent source/config/evidence review accepted under RENDER-DECODE-001. Rollback: remove patch0007 and rebuild.

## Synchronous backend coherence proof

`scripts/nv2a-coherence-proof.sh` is a standalone synthetic Switch build/run using the existing pinned devkitA64 image and Eden runner. It links only the portable executor and generic fixtures; no title assets or generated game code are inputs. The proof registers its own synchronous deko3d callbacks; the game does not register them. No game environment option/default changes. The wrapper serially cleans and rebuilds with one CPU/1GiB, no network/capabilities and the invoking uid/gid; only the prepared generic runtime/source is mounted. Host3/3, clean synthetic build/Eden run, skipped-readback negative control and full Switch build pass; independent source/config/actual-log/all36frozenartifact review accepted under RENDER-COHERENCE-001. Rollback: remove the isolated proof/script and patch0009, then rebuild.

Patch0009 adds optional backend preflight callbacks. A rejected batch takes the existing CPU renderer before transformation, avoiding a second execution of a stateful vertex program. Existing backends without preflight retain their previous behavior. The synthetic backend must explicitly reject states outside its tested subset; acceptance does not establish native menu rendering or performance.

The fixture accepts only declared unit-W FFP/basic opaque state and exact synthetic surfaces; textured draws require white diffuse. It rejects program/combiner paths and stops on accepted per-vertex/resource failure. It cannot be registered as the native game renderer. No hardware/concurrent-memory/performance acceptance is implied.

## Horizon aligned allocation

The Horizon-only `posix_memalign` shim lives in `game/src/horizon_alloc.c`, shared with the standalone contract proof. It rejects invalid alignments, checks size-rounding overflow, preserves the output pointer on failure and preserves errno. No option/default changes. `scripts/horizon-alloc-test.sh` runs host contract checks and a removed-validation negative control, then builds the same implementation in the pinned devkitA64 image (one CPU/1GiB, no network/capabilities, invoking uid/gid) and runs its synthetic NRO in Eden. Game assets are not inputs. Host69checks, Horizon69checks, validation-removal negative control, full Switch build and independent source/config/all23frozenartifact review pass under HZ-ALIGN-001. Actual allocator exhaustion was not induced. Rollback: restore the previous shim and remove the helper/CMake entry/proof script.

## Experimental native menu rendering (Eden menu validated)

`MANHUNT_GPU_BACKEND=1` in `sdmc:/manhunt.env` requests the guarded synchronous deko3d adapter. Unset or `0` retains the CPU renderer; other values stop startup with a configuration error. Initialization failure also stops rather than silently falling back to a different requested backend. The option is experimental: sameNROCPU/native240second Eden menu/navigation and startup checks pass independent review. Gameplay and physical Switch performance remain UNVERIFIED.

The Switch target links the adapter and packages three tracked shaders in RomFS using explicit asset/file dependencies. Shader changes must rebuild the NRO. Only the opt-in path mounts RomFS and initializes/registers the adapter, before the kernel bridge submits guest work. Device lifetime extends to process exit because the bridge threads are not joined on guest-entry return; the standalone single-threaded proof can unregister and destroy it safely.

Unknown draw state takes the existing CPU path; post-transform rejection uses already-computed outputs without replaying vertex programs. Accepted GPU operations import current guest pixels and wait/read back before returning. The adapter uses explicit float bilinear sampling for the RC path and packs final source colour before blending. Post-transform eligibility conservatively requires common W within1e-3..1e6 and sampled absolute UV within1e6 texels; diffuse-only mode ignores unused UVs. Other values use the CPU renderer. Final synthetic bounds/clear/draw and separate non-combiner fallback tests, full Switch build and independent review pass. Actual menu comparison/navigation passes its scoped Eden gate. No default is changed. Rollback: unset the option, or revert the isolated registration/module/shader integration and rebuild.

The synthetic `scripts/nv2a-menu-proof.sh` accepts `MENU_PROOF_NO_RC=1` to build a separate fixture that enables the executor’s existing `RECOMP_NO_COMBINERS` path and checks filtered-draw CPU fallback. This is a proof-script option, not a game option. Its separate Eden execution passes with exact CPU fallback and zero native draws.

## Horizon memory ownership proof

`scripts/horizon-memory-test.sh host` reconstructs the runtime from the pinned recompiler image and maintained patches, then runs the synthetic ownership/failure/race harness and its deliberate close-before-lock negative control. `switch` also builds the actual-service wrapper NRO in the pinned devkitA64 image and runs it in Eden. Containers use one CPU/1GiB, no network/capabilities and the invoking uid/gid. No game assets or runtime options are added. The harness shares the actual memory include but simplifies object-manager glue; full-game build/review is required separately.

Patch0011 tracks reservations, commits and anonymous mapping ownership, implements transactional split/rebuild decommit, and serializes view publication with handle release. Registry exhaustion fails before mutation; unrecoverable rollback stops execution. Only a single whole-object anonymous view is supported. Protection fidelity and RAM mirrors remain open. Host303,506 checks, deliberate race control and Eden4,128 checks pass; integration build and independent review pass. Rollback: remove0011 and the isolated proof, then rebuild.

For an isolated worktree, `EMU_RUN_SCRIPT` selects the emulator-runner path (default: that worktree’s `scripts/emu-run.sh`). Existing `EMU_LOG` retains its complete log. These proof-runner settings do not affect game behavior.

## Private partition write proof

`scripts/horizon-partition-test.sh [host|switch]` defaults to host. Switch mode creates a private ignored proof SD directory unless existing `EMU_DATA` is supplied; `EMU_LOG` retains the raw runner log (default: proof `build/eden.log`). It uses synthetic files without game inputs. No game runtime option changes. Patch0012 replaces the sole Horizon `pwrite` caller using its private descriptor; the incorrect global shim is removed. Missing error codes and short writes receive EIO locally. Host28checks, Eden25checks and missing-seek control pass; full Switch build passes, independent integration review passes. General native errno fidelity remains open. Rollback: remove0012 and restore the prior shim, then rebuild.

The synchronous native adapter now reuses one graphics queue for its device lifetime and invalidates reloaded shader code before binding. Per-callback import/wait/error-check/readback is preserved. A671draw synthetic stress test passes with stable sampled runner memory, while per-draw queue creation reproduces DockerOOM in the pinned Eden image. FullSwitchbuild passes; the240s original-menu recapture and navigation pass independent review. No option/default changes. See `reports/RENDER_QUEUE_LIFETIME.md`.

## Horizon UTC conversion proof

`scripts/horizon-time-test.sh host|switch` defaults to host and uses pinned restricted containers, synthetic calendar inputs and the exact production Horizon conversion include. It reproduces the old invalid-date/pre-1970 defects and checks a removed-validation control. No game option/default changes. Host1,632,300checks and174expected control failures pass; Horizon1,632,300checks, fullSwitchbuild and independent25artifact review pass. Patch0013 validates dates/ranges and preserves output on failure while retaining POSIX branches. Rollback: remove0013 and the isolated proof, then rebuild.

## Optional Intel Wayland game runner (game validation pending)

`infra/emulator/wayland/run-game.sh <NRO> <marker> [seconds]` uses the existing pinned047af837image, headless Weston and Intel Vulkan. `EMU_DATA` mounts a writable project Eden directory (`sdmc/manhunt` beneath it); `EMU_LOG` retains complete logs and `CPUS` defaults to all cores. `EMU_SDMC_GAME` is rejected. The container stays nonroot, network-disabled,6GiB, capabilities dropped with only renderD128 exposed. It checks actual Intel compositor/device/driver logs and keeps0PASS/1guestFAIL/2NO-RESULT/3setup classes. Marker `-` requires the emulator alive at the bound. Readiness and supervisor/shutdown waits are bounded; no host changes/public ports.

Known-pixel Intel proof and10second bounded run pass independent source/all13artifact review. Game/SD validation remains pending; adversarial shutdown and guestFAIL runtime classification are UNVERIFIED. Existing Xvfb and synthetic runners remain unchanged. Rollback: remove the isolated runner/report.

## Level-load analysis corrections

`scripts/game-build.sh` copies and applies the maintained xboxrecomp patches before importing analysis/lifting tools, then runs Python from that patched tree. The generated-code cache key includes `game/manual_functions.json`, `game/function_fixes.json`, `scripts/fix-functions.py` and every maintained xboxrecomp patch. A change to any of these invalidates the generated-code cache and requires relifting and rebuilding both targets.

`game/function_fixes.json` may declare an independently reviewed explicit function start and exclusive end for a measured unresolved indirect call. These entries are generated normally; they are distinct from hand-written replacements in `game/manual_functions.json`. All decoded game code and runtime evidence stay beneath gitignored `game/build/`.

For LEVEL-LOAD-001, the reviewed added function is `0x0005C620..0x0005C69C` (exclusive end). Patch `0014-large-local-switch-tables.patch` raises the existing forward/backward/local table scan limits from 256 to 4096; code-address, local-owner, adjacent-table and file bounds remain enforced. This recovers the observed 300-entry local switch without treating its interior arm `0x0019B572` as a function. Synthetic regression: `tests/recomp-large-switch/test_large_switch.py <patched-xboxrecomp-dir>` inside the pinned recompiler container. Function-correction tests: `python3 -I tests/test_fix_functions.py`.

## Read-only player-spawn diagnostics

A temporary `sdmc:/manhunt.env` test configuration can select existing runtime diagnostics without rebuilding. `RECOMP_PEEK_CHAIN=0x4004A0,0x80,4,0x40` reports the original player global → render object → frame → position-X chain at each pushbuffer report. It prints only the final DWORD; interpret its bits as float only after verifying the original getter. `RECOMP_PEEK=0x4004A0` reports the global literally. The periodic peek parser does not support bracket expressions.

The watchdog's separate peek parser supports expressions such as `[0x4004A0]` (player vtable), `[[0x4004A0]]` (first virtual method), and `[0x4004A0]+0x80` (render object). `RECOMP_WATCHDOG_SECS=450` requests a snapshot and deliberately exits with code3 afterward. This is a diagnostic termination, not a survival-test PASS. The spawn proof preserves the prior SD configuration and restores it only if it still matches the test's configuration; all runtime logs/frames remain private under `game/build/`. Switch execution of the selected proof is pending in `reports/LEVEL_LOAD.md`.
