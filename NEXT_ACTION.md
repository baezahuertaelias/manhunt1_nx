# Next action

**HW-PERF-001** (P1, owner: runtime): the main menu runs on the console and answers the controller, but the software rasteriser takes 98% of every frame (about 0.35 to 0.65 frames per second; [reports/HW_PERFORMANCE_FIRST_LOOK.md](reports/HW_PERFORMANCE_FIRST_LOOK.md)). Analysis: [reports/HW_RENDERING_OPTIONS.md](reports/HW_RENDERING_OPTIONS.md) (options, offline audit of the deko3d adapter: no known pitfall found). Next: one hardware run with `scripts/hw-run.sh --profile --allow-gpu-backend` (UNVERIFIED on the console; the adapter is a menu subset and falls back to the CPU renderer for other draws) to measure the speed-up, the share of draws it accepts and stability; then choose between extending the GPU renderer and speeding up the CPU rasteriser. Smaller open items: why the menu accepts input only after some 15 to 40 s (probably a transition counted in frames); the level load from the menu on hardware (HW-SWITCH-001); isolating patch 0016's three changes (HW-FREEZE-001, the freeze has not recurred in these runs). How to run a hardware cycle: [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md) section 0 and `scripts/hw-run.sh`.

Without hardware, the next engineering work is, in order:
1. **ICALL-NULL-002** (P2): the second null indirect call (guest 0x00222AF1) late in host runs.
2. Graphics fidelity and performance, BLOCKERS #1; audio (AUDIO-HLE-001), which would also remove the boot-time skipped null call (ICALL-NULL-001, explained).
3. M5 interaction on the host: crouch, collision, further controls; the meaning of the player flag bits is a hypothesis.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance", with the host probe as supporting (host-only, not independently reviewed) evidence. Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
