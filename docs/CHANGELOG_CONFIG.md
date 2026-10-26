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
