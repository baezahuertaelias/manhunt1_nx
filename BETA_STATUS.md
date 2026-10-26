# Beta 1 status

Status: **NOT STARTED — UNVERIFIED**

Current milestone: **2 — Boot** ([ROADMAP](docs/ROADMAP.md)); milestone 1 (toolchain proof) passed on 2026-10-26, hardware excluded. Next step: [NEXT_ACTION.md](NEXT_ACTION.md).

No gameplay or hardware acceptance criterion has passed. Available: a user-supplied retail `DEFAULT.xbe` plus level/audio assets (gitignored; hashes in [reports/ASSET_MANIFEST.sha256](reports/ASSET_MANIFEST.sha256)), the Dockerized devkitA64 toolchain, a Ghidra 12.1.4 image with the XBE loader, the xboxrecomp lifter image, and a headless Eden runner. Missing: a physical Switch.

## Milestone 1 gates

Rerun all of them with `scripts/toolchain-proof.sh`. Details: [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md).

| Gate | Status | Evidence |
|---|---|---|
| (a) original x86 functions recompiled with verified semantics | PASS: 5 functions, 360/360 gating cases exact on x86-64 Linux, AArch64 Linux and Horizon in Eden | RECOMP_EVAL §Gate (a)/(b) |
| (b) 32-bit guest pointers on 64-bit AArch64 | PASS within its scope: a 64 MiB arena above 4 GiB on all targets, and a truncated offset faults. High VAs, MMIO, bounds checks and stored pointers are not covered yet. | RECOMP_EVAL §Gate (a)/(b), §Limits |
| (c) minimal NRO | PASS: reproducible, `a340ad44…329d` | [BUILD_LOG](BUILD_LOG.md) |
| (d) deko3d rendering | PASS in Eden with **llvmpipe** (pixel readback, 50.000% coverage). Hardware GPU: UNVERIFIED | RECOMP_EVAL §Gate (d) |
| (e) automated emulator test | PASS: `scripts/emu-run.sh`, Eden 1.0.0; negative controls detected | RECOMP_EVAL §Gate (e) |

## Other gates

| Gate | Status | Evidence |
|---|---|---|
| XBE header facts | PASS; cross-checked against Ghidra | [XBE_HEADER](reports/XBE_HEADER.md) |
| Ghidra XBE import | PASS (import only, no auto-analysis) | [BUILD_LOG](BUILD_LOG.md) |
| x87 fidelity at 64-bit precision control | INFO: 45/60 exact as double, 60/60 as float; the game's actual control word is unknown | RECOMP_EVAL §Findings |
| Native Switch playability | UNVERIFIED — no hardware | — |
| 30 FPS hardware gate | UNVERIFIED — no hardware | — |
