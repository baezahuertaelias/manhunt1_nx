# Beta 1 status

Status: **NOT STARTED — UNVERIFIED**

Goal: first original Manhunt level playable from start to completion on native Switch hardware at stable 30 FPS.

No gameplay, emulator-runtime, or hardware acceptance criterion has passed. Available: a user-supplied retail `DEFAULT.xbe` plus level/audio assets (gitignored; hashes in [reports/ASSET_MANIFEST.sha256](reports/ASSET_MANIFEST.sha256)), a Dockerized devkitA64 toolchain, and a Ghidra image with the XBE loader. Missing: a recompiler, a working Switch emulator runner, and a physical Switch.

## Gates

| Gate | Status | Evidence |
|---|---|---|
| Switch toolchain build (devkitA64/libnx in Docker) | PASS (original proof source) | [PREFLIGHT](reports/PREFLIGHT.md) |
| Revised NRO proof (newline fix, main loop, NACP) | PASS build, reproducible (`a340ad44…329d`) | [BUILD_LOG](BUILD_LOG.md) |
| Analysis image (own Ghidra 12.1.4 + matching loader) | PASS | [BUILD_LOG](BUILD_LOG.md) |
| NRO runtime in emulator | FAIL/UNVERIFIED — Eden runner failure | [PREFLIGHT](reports/PREFLIGHT.md) |
| XBE header facts | PASS; cross-checked against Ghidra | [XBE_HEADER](reports/XBE_HEADER.md) |
| Ghidra XBE import | PASS (import only, no auto-analysis) | [BUILD_LOG](BUILD_LOG.md) |
| Recompiled function proof | UNVERIFIED — no recompiler | [BLOCKERS](BLOCKERS.md) #1 |
| Guest pointer translation | UNVERIFIED | [BLOCKERS](BLOCKERS.md) #2 |
| deko3d rendering | UNVERIFIED | — |
| Native Switch playability | UNVERIFIED — no hardware | — |
| 30 FPS hardware gate | UNVERIFIED — no hardware | — |
