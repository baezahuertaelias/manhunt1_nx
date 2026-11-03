# Next action

**HW-FREEZE-001 / HW-SWITCH-001** (P0, owner: user + runtime): rerun the current NRO on the physical Switch **without** `RECOMP_WATCHDOG_SECS` in `sdmc:/switch/manhunt1_nx/manhunt.env` and see whether the game leaves the legal screen (first run: it drew that screen on hardware, then a diagnostic watchdog ended it at about 5 s; [reports/HW_SWITCH_FIRST_RUN.md](reports/HW_SWITCH_FIRST_RUN.md)). Then isolate which of patch 0016's three changes removed the earlier console freeze. Menu, level load, controller and FPS on hardware remain UNVERIFIED; setup is in [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md).

Without hardware, the next engineering work is, in order:
1. **ICALL-NULL-002** (P2): the second null indirect call (guest 0x00222AF1) late in host runs.
2. Graphics fidelity and performance, BLOCKERS #1; audio (AUDIO-HLE-001), which would also remove the boot-time skipped null call (ICALL-NULL-001, explained).
3. M5 interaction on the host: crouch, collision, further controls; the meaning of the player flag bits is a hypothesis.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance", with the host probe as supporting (host-only, not independently reviewed) evidence. Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
