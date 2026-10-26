# Testing

| Test | Command | Status |
|---|---|---|
| XBE input integrity | `scripts/asset-manifest.sh \| diff - reports/ASSET_MANIFEST.sha256` | PASS 2026-10-26 |
| XBE header parse | `python3 -I analysis/xbe_header.py assets/manhunt1_xbe/DEFAULT.xbe` | PASS 2026-10-26 |
| Compose config | `docker compose -f infra/compose.yaml config -q` | PASS 2026-10-26 |
| NRO proof build | `scripts/build-nro-proof.sh` (run twice, compare hashes) | UNVERIFIED for current source |
| Analysis image build | `scripts/build-analysis-image.sh` | UNVERIFIED for current Dockerfile |
| NRO in emulator | none yet (EMU-RUNNER-001) | FAIL/UNVERIFIED |
| Recompiled function | none yet (RECOMP-EVAL-001) | UNVERIFIED |
| Hardware playability / 30 FPS | requires a physical Switch | UNVERIFIED |

Emulator FPS is diagnostic only and never counts as hardware FPS. Emulator runner failures are reported separately from port failures.
