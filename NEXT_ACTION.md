# Next action

**HW-SWITCH-001** (P1, owner: user): run the in-level build on a physical homebrew-capable Switch with your own game files and a real controller, following [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md), and return the SD-card logs and frames. In Eden the in-level game runs at about 0.1 fps, so a cutscene, slowness and a control defect cannot be told apart there.

The hardware run now returns text logs too: HW-LOG-001 (`MANHUNT_LOG_FILE`, default off) is implemented and checked in Eden; its independent review is recorded in the task, and the console behaviour is UNVERIFIED.

Without hardware, the next engineering work is, in order:
1. **Host probe: player position versus input** (runs in progress): the host build reaches gameplay after about 25 minutes (reports/LEVEL_LOAD.md, "Host gameplay observation"); log the player position at 0x4004A0 against the scripted input to tie the visible figure to the player object and to show control, to strengthen the minimal M4 bar; and **ICALL-NULL-002** (P2, second null call, late in host runs). ICALL-NULL-001 is explained: it follows from audio not being implemented.
2. **LEVEL-INPUT-TRACE-001 / LEVEL-MOVE-001** (M5): follow the title's input gate (word 0x002ECA30, mode word 0x004025E4, player+0x844 bit 0x100, script opcodes 0x147/0x148) on the host to learn what ends the scripted mode; do not force-skip it without evidence.
3. Graphics fidelity (dark scene, streaks) and performance, BLOCKERS #1.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance". Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
