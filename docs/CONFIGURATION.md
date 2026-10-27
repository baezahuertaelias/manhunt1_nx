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
| Function fixes | `game/function_fixes.json` | `scripts/fix-functions.py` | Reviewed removal of false entries; fails if the database no longer matches |
| Hand-written functions | `game/manual_functions.json` (XInput ×4, `D3D_BlockOnTime`, DirectSound ×3) | lifter `--manual-functions` | Their bodies are not generated; `game/src/*.c` defines them |
| xboxrecomp patches | `game/patches/xboxrecomp/0001`–`0003` | applied by `scripts/game-build.sh` | Case-insensitive paths; pseudo-handle close; Horizon port |
| Runtime switches (boot) | `RECOMP_PB_EXEC=1 RECOMP_PB_WALK=dma RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=paced RECOMP_DISPATCH_GATE=1 RECOMP_FLIP_PACING=1` | host: environment; Switch: `main_switch.c` defaults + `sdmc:/manhunt.env` | Each is needed (FEASIBILITY.md steps 2, 4, 5) |
| Generated-code optimisation | `MANHUNT_GEN_OPT` `-O1` (debug variant `-O0`) | `game/CMakeLists.txt` | `-O0` makes gdb probes read exact guest registers |
| Switch SD layout | `sdmc:/manhunt/` game data (copy, manifest-verified), `sdmc:/manhunt-save/` saves and partition images, `sdmc:/manhunt-frames/` BMP dumps, `sdmc:/manhunt.env` KEY=VALUE overrides | `main_switch.c`, `scripts/switch-boot-proof.sh` | Eden opens SD files read/write, so a read-only bind of the originals fails |
| Switch config keys | `MANHUNT_DUMP_SECS` (frame dump period, 0 = off), `MANHUNT_PAD_SCRIPT`, any `RECOMP_*` | `sdmc:/manhunt.env` | |
| Host config keys | `MANHUNT_PAD_SCRIPT`, `MANHUNT_SAVE_DIR`, `MANHUNT_MIRROR_TRAP`, `MANHUNT_TILED_TRAP` (debug traps) | environment, forwarded by `scripts/game-run.sh` | |
| Eden data dir | `game/build/eden-data` (`EMU_DATA`) | `scripts/emu-run.sh` | Writable Eden user dir; SD card is its `sdmc/` |
| Guest thread placement (Switch) | runtime threads round-robin over cores 0–2, mask 0x7; guest thread prefers core 0 | patch 0003, `main_switch.c` | Horizon does not time-slice equal-priority threads |

