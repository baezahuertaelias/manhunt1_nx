# Next action

**HW-SWITCH-001** (P1, owner: user): run the in-level build on a physical homebrew-capable Switch with your own game files and a real controller, following [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md), and return the SD-card logs and frames. In Eden the in-level game runs at about 0.1 fps, so a cutscene, slowness and a control defect cannot be told apart there.

Without hardware, the next engineering work is, in order:
1. **ICALL-NULL-001** (P1): explain the skipped null indirect call at about 10 s (call #109186).
2. **LEVEL-INPUT-TRACE-001 / LEVEL-MOVE-001** (M5): follow the title's input gate (word 0x002ECA30, mode word 0x004025E4, player+0x844 bit 0x100, script opcodes 0x147/0x148) on the host to learn what ends the scripted mode; do not force-skip it without evidence.
3. Graphics fidelity (dark scene, streaks) and performance, BLOCKERS #1.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance". Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
