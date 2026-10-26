# Configuration

| Setting | Value | Source | Notes |
|---|---|---|---|
| `GAME_INPUT_DIR` | `assets/manhunt1_xbe` | `.env` / `.env.example` | Read by `scripts/asset-manifest.sh` and `infra/compose.yaml`; mounted read-only |
| `BUILD_PROFILE` | `proof` | `.env` | Only profile today |
| `UID` / `GID` | `1000` | `.env` | Owner of build outputs from `switch-build` |
| Analysis base image | `dd-ghidra-mcp:local`, ID `sha256:fc4729a6bb0cb39164c6d8d7aaeee6414f1a780478db04e32ce93cc46b7af6ad` | `scripts/build-analysis-image.sh` | Host-local, no registry digest; build refuses a different ID |
| Ghidra version | `12.1.4` | `infra/analysis/Dockerfile` `GHIDRA_VERSION` | Asserted against `application.properties`; must match loader zip |
| XBE loader | XboxDev/ghidra-xbe `build-202610161023`, SHA-256 `339b2f67563bbb8b5217740319b7f3eae9e9bbacf1f3a8aa29151f7ecb7d5152` | Dockerfile | Hash verified at build |
| Analysis image | `manhunt-analysis:ghidra-xbe` | build script | Last built ID `sha256:e513e945…8d68` (previous Dockerfile) |
| Switch toolchain | `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282` (tag `20260219`) | `scripts/build-nro-proof.sh`, compose | Pinned by digest |
| Container limits | analysis 3 CPU / 6 GiB; switch-build 3 CPU / 4 GiB, no network | `infra/compose.yaml` | Leaves 1 CPU for the host (4 total) |
| GhidraMCP port | `127.0.0.1:8089` | compose | Localhost only |
