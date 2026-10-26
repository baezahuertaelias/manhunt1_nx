# Blockers

Ranked. Evidence: [reports/PREFLIGHT.md](reports/PREFLIGHT.md), [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md).

1. **P0 — No whole-program boot path.** Only five leaf functions are proven. A boot needs a full lift and link, the kernel import shims (111 ordinals), indirect-call dispatch, threads and SEH on Horizon. Plan it in FEASIBILITY-001.
2. **P1 — Game's x87 control word unknown.** At 64-bit precision control the lifter's doubles differ from x87 in the last ulp (15/60 cases). Find the control word Manhunt actually runs with before relying on physics or AI numerics.
3. **P1 — Eden runner not reproducible.** It uses the host-local `dd-eden:local` image from another project, checked by image ID (EMU-IMAGE-001).
4. **P1 — Hardware GPU in containers.** Eden with `/dev/dri` under Xvfb fails with "Device lacks a present queue"; every emulator run so far used llvmpipe (GPU-PROOF-001).
5. **P1 — No Git remote.** `gh` is authenticated (`repo` scope); creating a private remote needs user approval.
6. **P2 — Ghidra auto-analysis incomplete.** Import passes on the 12.1.4 image; full analysis has not been rerun since the old mismatched image stalled.
7. **P2 — No physical Switch.** All hardware gates stay `UNVERIFIED`.
