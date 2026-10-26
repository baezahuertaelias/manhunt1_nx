# Testing

| Test | Command | Status |
|---|---|---|
| XBE input integrity | `scripts/asset-manifest.sh \| diff - reports/ASSET_MANIFEST.sha256` | PASS 2026-10-26 |
| XBE header parse | `python3 -I analysis/xbe_header.py assets/manhunt1_xbe/DEFAULT.xbe` | PASS 2026-10-26 |
| Compose config | `docker compose -f infra/compose.yaml config -q` | PASS 2026-10-26 |
| NRO proof build | `scripts/build-nro-proof.sh` (run twice, compare hashes) | PASS 2026-10-26, `a340ad44…329d` both runs |
| Analysis image build | `scripts/build-analysis-image.sh` | PASS 2026-10-26, `sha256:fc761493…4178` |
| Ghidra import + layout cross-check | `docker compose -f infra/compose.yaml run --rm analysis /tmp proof -import /input/DEFAULT.xbe -noanalysis -deleteProject -scriptPath /scripts -postScript DumpLayout.java`; compare with `reports/XBE_HEADER.md` | PASS 2026-10-26 (run with plain `docker run`, same mounts) |
| NRO in emulator | none yet (EMU-RUNNER-001) | FAIL/UNVERIFIED |
| Recompiled function | none yet (RECOMP-EVAL-001) | UNVERIFIED |
| Hardware playability / 30 FPS | requires a physical Switch | UNVERIFIED |

Emulator FPS is diagnostic only and never counts as hardware FPS. Emulator runner failures are reported separately from port failures.

## Policy

### Xbox reference (xemu)

- Run xemu automatically at every major milestone, in a headless container. Use GPU acceleration where it works: first prove actual accelerated rendering, since a virtual display alone proves nothing, and detect and report any software-rendering fallback.
- Before claiming a feature works, verify each capability actually used: compatible execution, lawful system and game inputs (xemu may need a disc image rather than a bare XBE, so verify formats), GDB guest debugging, logs, screenshots, snapshots and input automation.
- Capture reference sequences for boot, menu, first-level load, player movement, enemy detection, combat and executions, objectives, and level completion. Collect guest PC and registers, relevant memory, function traces, screenshots and timing where supported.

### Switch side (Eden or Ryujinx)

- Download, build and configure the best legally obtainable, compatible emulator **inside Docker**. Choose by measured capability: headless Linux support, NRO loading, GPU requirements, logging, debugger and automation. If one emulator doesn't work, try the other.
- Test generated NROs at every major milestone. Keep emulator tests from blocking unrelated builds.

### Automation adapters

- Prefer documented emulator APIs, GDB, CLI flags, IPC and scripts. Don't assume an MCP server exists. Patching or forking an emulator for automation is allowed in an isolated fork with minimal patches, after checking the license.
- Planned unified interface: `boot`, `load_game`, `send_input`, `capture_frame`, `read_guest_memory`, `collect_logs`, `stop`, `get_crash`. Expose a method only once it's implemented and tested; unsupported methods return an explicit unsupported status.
- Timebox emulator and MCP proofs of concept and keep a scripted-harness fallback.

### Differential testing

- Record reference input sequences with checkpoint evidence. Replay them on the native build or an emulator and compare:
  - mission state, player position and heading, collision outcomes, enemy state
  - combat and execution transitions, audio event presence, representative frames
- Document tolerances for randomness, scheduling, x87-vs-ARM numeric differences and timing. Don't require byte-identical frames or determinism where that's impossible.
- Required smoke tests, added as milestones arrive:
  - boot, menu navigation, first-level asset load, spawn
  - walk, run and crouch; camera
  - enemy patrol, detection, chase and attack; stealth execution; combat
  - objectives, checkpoint and save if needed, level completion
- On a regression: `git bisect` where the tests are deterministic enough, keep the reproducer artifacts, and open a blocker naming the responsible guest or native address.
