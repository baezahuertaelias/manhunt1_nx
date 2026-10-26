# Roadmap

## Beta 1 definition

**The first original Manhunt level, playable from beginning to completion on the native Switch build, at a stable 30 FPS on a stock original Switch in handheld mode.** A booting menu, a rendering demo or a partial level is not Beta 1.

Preserve the original gameplay, atmosphere, mission progression, stealth, executions, combat and AI. Do not rewrite the game before the first level is playable.

Only real Switch hardware can establish playability and the 30 FPS gate. Until hardware testing passes, the best possible label is **"Beta 1 candidate — emulator validated, hardware unverified"**, with a precise gap list.

## Hardware acceptance targets

These are targets, not results.

- Sustained 30 FPS (33.3 ms frame budget), preferably 720p output with adjustable internal resolution.
- 1% low ≥ 27 FPS; no sustained dips below 25 FPS; correct frame pacing.
- At least 30 minutes without crashes.

Track CPU and GPU frame times, memory pressure, shader stalls, texture upload cost, draw calls and recompilation overhead. Profile before optimizing. Use AArch64/NEON only where a measured bottleneck warrants it. Measure available RAM rather than assuming it.

## Milestones

Schedule by the next **observable playable milestone**. Revise the order based on evidence.

1. **Toolchain proof.** Deterministic Docker build; one representative x86 function recompiled and tested; minimal NRO built.
2. **Boot.** Initialize guest and runtime and reach the original menu (or verified equivalent menu behaviour).
3. **First image.** Render the menu/UI and essential graphics correctly.
4. **First-level load.** Parse required assets, display the environment, spawn the player.
5. **Core interaction.** Camera, movement, crouch, collision, controls.
6. **Playable systems.** Enemy patrol, detection, chase and attack; stealth and executions; combat; audio; objectives and scripts; checkpoints and saves where needed.
7. **Level completion.** First level end-to-end with no progression blockers.
8. **Performance and stability.** Optimize measured hot paths and validate frame pacing, memory and 30-minute stability on real hardware when available.

### Proof-of-concept gates before scaling (milestone 1)

| Gate | Requirement |
|---|---|
| a | One original x86 function with verified semantics under the chosen recompilation approach |
| b | Correct 32-bit guest address and pointer translation into AArch64 host code |
| c | Minimal valid NRO build |
| d | Basic deko3d rendering verified in a suitable test environment |
| e | At least one automated emulator test |

If a gate fails, stop that branch and evaluate a cheaper alternative before expanding. Gates that need hardware stay `UNVERIFIED`; emulator behaviour is not hardware proof. Live status: [BETA_STATUS.md](../BETA_STATUS.md).

## Priorities

- **P0** — blockers
- **P1** — first-level requirements
- **P2** — measured 30 FPS bottlenecks
- **P3** — nonessential polish
- **P4** — future releases

P0 preempts everything below it.

## Release 2 (deferred)

Centralize time-step, projection, viewport and render resolution from the start, but defer these until after Beta 1:

- 16:9 Hor+ widescreen
- HUD, cutscene and culling fixes
- Optional 30/40/60 FPS or experimental uncapped modes

A higher FPS must never speed up simulation, AI, animations or mission scripts. Validate frame caps against what the display actually supports.
