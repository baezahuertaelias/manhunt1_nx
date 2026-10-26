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
