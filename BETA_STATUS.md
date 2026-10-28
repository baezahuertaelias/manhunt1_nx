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
| No crash/exit during the run | PASS (240 s): no crash/exit line in the guest log, no fault reported by Eden, presents still increasing at the end | `scripts/switch-boot-proof.sh` |
| Audio | NOT IMPLEMENTED: DirectSound reports "no driver" by design | `game/src/dsound_hle.c` |
| Input-driven menu navigation | PASS in Eden: PLAY → BONUS FEATURES after scripted UP; independently viewed | [INPUT_NAVIGATION.md](reports/INPUT_NAVIGATION.md) |
| Real Switch hardware | UNVERIFIED — no hardware | — |

## Milestone 1 gates (toolchain proof)

Rerun with `scripts/toolchain-proof.sh`. Details: [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md).

| Gate | Status |
|---|---|
| (a) x86 functions recompiled with verified semantics | PASS: 6 functions (incl. `_aulldiv`), 405/405 gating cases exact on x86-64, AArch64 Linux and Horizon/Eden |
| (b) 32-bit guest pointers on 64-bit AArch64 | PASS within scope (RECOMP_EVAL §Limits) |
| (c) minimal NRO | PASS, reproducible |
| (d) deko3d rendering | PASS in Eden (llvmpipe); hardware UNVERIFIED |
| (e) automated emulator test | PASS (`scripts/emu-run.sh`) |

## Other

| Item | Status |
|---|---|
| x87 fidelity at 64-bit precision control | INFO: last-ulp differences; a bounded host-debug watch observed 53/64-bit precision and directed rounding; fidelity unresolved ([report](reports/X87_CONTROL_WORD.md)) |
| Performance (30 FPS gate) | UNVERIFIED — no hardware; emulator rate is far below target (CPU rasteriser) |

## 2026-10-28 blocker investigation

Standalone container Intel GBM/EGL rendering and pixel readback pass ([GPU proof](reports/GPU_PROOF.md)); Eden Vulkan presentation remains unresolved. [Render planning](reports/RENDER_PLAN.md) identifies missing timing evidence and backend-state gaps; RENDER-001 stays open. No new gameplay, audio, frame-rate or hardware gate passed.

## 2026-10-28 continued verification

The optional project Wayland runner passes Intel Vulkan synthetic deko3d readback, including an independently reviewed shutdown bound and rerun ([report](reports/EMU_GPU_PRESENT.md)). The default runner is unchanged and still uses llvmpipe; game compatibility on Wayland remains UNVERIFIED.

A fresh Switch game build and disabled/enabled 240-second diagnostic captures pass their scoped entry/menu-asset/no-logged-fault checks. Thirteen timing snapshots satisfy nested accounting; late-window raster elapsed is 99.766% of scanner time. This supports the GPU attribute/coordinate proof as the next P0 step, not a hardware performance claim. Details and limits: [RENDER_PLAN.md](reports/RENDER_PLAN.md).

The standalone Nv2aVertex coordinate/colour/UV/perspective proof passes both surface sizes with zero pixel mismatches on independent rerun. Texture/state/synchronization and menu integration remain pending; this does not close the GPU backend blocker.

The bounded P8 palette metadata/helper gap is fixed and tested on host, with the full Switch target rebuilt. Ten synthetic nearest-texture/alpha/blend/mask cases and five rejection checks pass independent emulator rerun. GPU palette upload, other states and actual backend integration remain UNVERIFIED; see RENDER_PLAN.md.

One complete original-menu CPU frame now has independently reviewed state/texture/palette/combiner diagnostics. It identifies actual bilinear, alpha0, additive blending,32-entry palette, decoder, projection and coherence gaps before GPU registration. No new hardware/gameplay milestone passes from this capture.

Menu-backend preparation: synthetic bilinear/clamp/combiner/alpha0/additive readbacks and bounded short-P8/ARGB8/DXT23 helper fixtures pass their scoped build/test/review gates ([details](reports/RENDER_PLAN.md)). These do not register a native renderer; surface/fence coherence and hardware/gameplay acceptance remain pending.
