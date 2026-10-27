# Setup

Headless, SSH-only host with Docker. No host packages are installed by this project.

## Inputs

Place the lawfully obtained game files under `assets/manhunt1_xbe/` (or set `GAME_INPUT_DIR` in `.env`, copied from `.env.example`). `assets/` is gitignored. Never place proprietary inputs in Git. Record their hashes:

```sh
scripts/asset-manifest.sh > reports/ASSET_MANIFEST.sha256
python3 -I analysis/xbe_header.py assets/manhunt1_xbe/DEFAULT.xbe > reports/XBE_HEADER.md
```

Expected `DEFAULT.xbe` SHA-256: `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde`.

## Images

```sh
scripts/build-analysis-image.sh   # official Ghidra 12.1.4 + ghidra-xbe, hash-verified
scripts/build-nro-proof.sh        # builds tests/nro-proof in the pinned devkitA64 image
docker build -t manhunt-recompiler:xboxrecomp-f0f8a46 infra/recompiler   # lifter + reference tools
```

The Eden runner uses the host-local image `dd-eden:local` (ID checked by `scripts/emu-run.sh`; see docs/CONFIGURATION.md).

## Verify the toolchain (milestone 1)

```sh
scripts/toolchain-proof.sh        # gates a-e; prints PASS/FAIL per gate, hardware stays UNVERIFIED
```

The first run disassembles the XBE (about 4 minutes) and lifts the proof functions (about 76 s each). Both are cached under `tests/recomp-proof/build/`, which is gitignored and holds game-derived code and logs containing certificate keys.

Services are also defined in `infra/compose.yaml` (`docker compose -f infra/compose.yaml config` validates it).

## Recovery

All state that matters is in Git or under `reports/`. The Ghidra project lives in the `ghidra-projects` named volume (or `reports/ghidra*/`, gitignored) and can be regenerated from the XBE. Re-run the checks in [../reports/PREFLIGHT.md](../reports/PREFLIGHT.md) after any host change.

## Build and run the game (milestone 2)

```sh
scripts/recomp-proof.sh            # once: also warms the analysis tooling
scripts/game-build.sh              # host build: analysis, lift (about 5 min first time), compile
scripts/game-build.sh switch       # Switch NRO: game/build/switch/manhunt.nro
scripts/switch-boot-proof.sh       # copies data to the emulated SD, runs in Eden, PASS/FAIL
```

Host run: `RECOMP_PB_EXEC=1 RECOMP_PB_WALK=dma RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=paced RECOMP_DISPATCH_GATE=1 RECOMP_FLIP_PACING=1 RECOMP_FB_WINDOW=1 RECOMP_FB_WINDOW_DUMP_EVERY=60 scripts/game-run.sh 100`. Frames land in `game/build/frames/`; convert them with `scripts/bmp2png.py`.

**On a real Switch (UNVERIFIED):**
1. Copy `manhunt.nro` to the SD card's `switch/` folder.
2. Copy the game data folder to `sdmc:/manhunt/`.
3. Launch it from the homebrew menu.

`game/build/` holds game-derived code, binaries, frames and logs. It is gitignored: never commit or distribute it.

