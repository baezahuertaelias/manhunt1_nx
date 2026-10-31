# Next action

**HW-SWITCH-001** (P1, owner: user): run the in-level build on a physical homebrew-capable Switch with your own game files and a real controller, following [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md), and return the SD-card log (`MANHUNT_LOG_FILE`) and frames. Eden is too slow to confirm the Switch build's cinematic-to-gameplay hand-over: a 111-minute Eden run was still in the cinematic ([reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Long Eden run"), while the host build hands control to the player after about 28 minutes of wall time and then responds to the stick ("Host probe"). No further long Eden runs are planned.

Without hardware, the next engineering work is, in order:
1. **ICALL-NULL-002** (P2): the second null indirect call (guest 0x00222AF1) late in host runs.
2. Graphics fidelity and performance, BLOCKERS #1; audio (AUDIO-HLE-001), which would also remove the boot-time skipped null call (ICALL-NULL-001, explained).
3. M5 interaction on the host: crouch, collision, further controls; the meaning of the player flag bits is a hypothesis.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance", with the host probe as supporting (host-only, not independently reviewed) evidence. Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
