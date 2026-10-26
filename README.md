# Manhunt (Xbox) → Nintendo Switch

Research toward a native Switch port of the original Xbox Manhunt via hybrid static recompilation. **No game code or assets are included or distributed**; users supply their own lawfully obtained copy.

Status: preflight complete; milestone 1 (toolchain proof) in progress. See [BETA_STATUS.md](BETA_STATUS.md), [BLOCKERS.md](BLOCKERS.md) and [NEXT_ACTION.md](NEXT_ACTION.md).

## Read first

- [AGENTS.md](AGENTS.md): project rules for humans and agents
- [docs/ROADMAP.md](docs/ROADMAP.md): Beta 1 definition, milestones, performance targets
- [docs/WORKFLOW.md](docs/WORKFLOW.md): planning, review, GitHub pipeline, provenance
- [docs/SETUP.md](docs/SETUP.md): getting started

## Layout

Directories that don't exist yet are created as needed.

| Path | Purpose |
|---|---|
| `docs/` | Setup, configuration, architecture, testing, roadmap, workflow, decisions |
| `analysis/` | Ghidra scripts and XBE tooling |
| `scripts/` | Build, manifest, smoke-test and trace-comparison scripts |
| `infra/` | Dockerfiles and Compose |
| `tests/` | Proofs, host tests, replays, expected checkpoints |
| `reports/` | Preflight, evidence and benchmark artifacts |
| `tasks/`, `TASK_GRAPH.md` | Task ledger |
| `.codex/agents/` | Verified Codex agent configs (planned) |
| `src/`, `include/` | Native runtime, platform adapters, overrides, ABI and memory interfaces (planned) |
| `recomp/` | Generated recompilation output (planned) |
| `shaders/` | Shader sources (planned) |
| `emulators/` | Emulator harnesses, patches, adapters (planned) |
| `assets/` | Local gitignored game inputs; never committed |
