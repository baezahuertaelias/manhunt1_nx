# Blockers

Ranked. Evidence: [reports/PREFLIGHT.md](reports/PREFLIGHT.md).

1. **P0 — No x86→AArch64 static recompiler.** No candidate (XboxXBERecomp/xboxrecomp or other) has been evaluated for source, license, target, or output. Next experiment: RECOMP-EVAL-001.
2. **P0 — Guest 32-bit pointer translation unproven.** Depends on recompiler choice.
3. **P1 — Switch emulator runner fails.** Eden (`dd-eden:local`) rejected `-n`, could not create its config path, and found no SDL3 Vulkan window backend. Ryujinx not yet tried. This is an emulator-runner failure, not a port failure.
4. **P1 — In-container GPU rendering unproven.** `/dev/dri` nodes exist on the host; no container has rendered through them.
5. **P1 — No Git remote.** `gh` is authenticated (`repo` scope); a private remote has not been created or confirmed.
6. **P2 — Ghidra auto-analysis incomplete.** Import passes on the matched 12.1.4 image. Full analysis was last tried on the old mismatched image, where it stalled on decompiler warnings; it needs a rerun. Header facts are in [reports/XBE_HEADER.md](reports/XBE_HEADER.md).
7. **P2 — No physical Switch.** All hardware gates stay `UNVERIFIED`.
