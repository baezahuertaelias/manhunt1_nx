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
| Container limits | analysis 3 CPU / 6 GiB; switch-build 3 CPU / 4 GiB; both without network | `infra/compose.yaml` | Leaves 1 CPU for the host (4 total) |
