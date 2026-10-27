# Beta 1 status

Status: **IN PROGRESS — UNVERIFIED on hardware**

Current milestone: **3 — First image** ([ROADMAP](docs/ROADMAP.md)). Milestone 1 (toolchain proof) passed on 2026-10-26 and milestone 2 (boot to the original menu) on 2026-10-27, both in the emulator only. Next step: [NEXT_ACTION.md](NEXT_ACTION.md).

The recompiled game boots to its original main menu on the Switch build in Eden, and on the Linux host build. No gameplay or hardware acceptance criterion has passed. The input data (gitignored; hashes in [reports/ASSET_MANIFEST.sha256](reports/ASSET_MANIFEST.sha256)) is user-supplied. Missing: a physical Switch, audio, a GPU back end (frames come from a CPU rasteriser at well under 1 fps).

## Milestone 2 gates (boot)

Rerun with `scripts/switch-boot-proof.sh`. Details: [reports/FEASIBILITY.md](reports/FEASIBILITY.md).

| Gate | Status | Evidence |
|---|---|---|
| Switch NRO boots the title in Eden | PASS: entry reached, intro movie, menu | FEASIBILITY §Switch bring-up |
| Original main menu reached (Switch, Eden) | PASS: `MAIN_EX.TXD` + `FE_M.RIB` loaded, 32 presents in 240 s, menu frame dumped (visual check manual) | `MH-BOOT-PROOF PASS` |
| Same on the Linux host build | PASS (intermediate evidence) | FEASIBILITY §Linux host |
| No crash/exit during the run | PASS (240 s) | `scripts/switch-boot-proof.sh` |
| Audio | NOT IMPLEMENTED: DirectSound reports "no driver" by design | `game/src/dsound_hle.c` |
| Input-driven menu navigation | UNVERIFIED: native input wired, not yet exercised | — |
| Real Switch hardware | UNVERIFIED — no hardware | — |

## Milestone 1 gates (toolchain proof)

Rerun with `scripts/toolchain-proof.sh`. Details: [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md).

| Gate | Status |
|---|---|
| (a) x86 functions recompiled with verified semantics | PASS: 6 functions (incl. `_aulldiv`), all gating cases exact on x86-64, AArch64 Linux and Horizon/Eden |
| (b) 32-bit guest pointers on 64-bit AArch64 | PASS within scope (RECOMP_EVAL §Limits) |
| (c) minimal NRO | PASS, reproducible |
| (d) deko3d rendering | PASS in Eden (llvmpipe); hardware UNVERIFIED |
| (e) automated emulator test | PASS (`scripts/emu-run.sh`) |

## Other

| Item | Status |
|---|---|
| x87 fidelity at 64-bit precision control | INFO: last-ulp differences; the game's control word is unknown |
| Performance (30 FPS gate) | UNVERIFIED — no hardware; emulator rate is far below target (CPU rasteriser) |
