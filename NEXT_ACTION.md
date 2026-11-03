# Next action

**HW-PERF-001** (P1, owner: runtime): the main menu runs on the console and answers the controller, but the software rasteriser takes 98% of every frame (about 0.35 to 0.65 frames per second; [reports/HW_PERFORMANCE_FIRST_LOOK.md](reports/HW_PERFORMANCE_FIRST_LOOK.md)). Next: (1) audit the deko3d backend offline against the known hardware pitfalls, then try `MANHUNT_GPU_BACKEND=1` on the console with `scripts/hw-run.sh --allow-gpu-backend` (UNVERIFIED there; earlier freezing runs had it on); (2) in the meantime profile why a pixel costs about 1.2 us in the single-threaded rasteriser. Smaller open items: a long run with `MANHUNT_PAD_HOLD_POLLS` left at 1 to see whether the hold is needed; why the menu accepts input only after about 35 to 60 s; the level load from the menu on hardware (HW-SWITCH-001); isolating patch 0016's three changes (HW-FREEZE-001, the freeze has not recurred in these runs). How to run a hardware cycle: [docs/MACOS_SWITCH_SETUP.md](docs/MACOS_SWITCH_SETUP.md) section 0 and `scripts/hw-run.sh`.

Without hardware, the next engineering work is, in order:
1. **ICALL-NULL-002** (P2): the second null indirect call (guest 0x00222AF1) late in host runs.
2. Graphics fidelity and performance, BLOCKERS #1; audio (AUDIO-HLE-001), which would also remove the boot-time skipped null call (ICALL-NULL-001, explained).
3. M5 interaction on the host: crouch, collision, further controls; the meaning of the player flag bits is a hypothesis.

Milestone 4 is recorded at a minimal bar only, UNVERIFIED on hardware: [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Milestone 4 acceptance", with the host probe as supporting (host-only, not independently reviewed) evidence. Audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
