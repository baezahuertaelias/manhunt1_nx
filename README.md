# Manhunt (Xbox) → Nintendo Switch

Research toward a native Switch port of the original Xbox Manhunt via hybrid static recompilation. **No game code or assets are included or distributed**; users supply their own lawfully obtained copy.

Status: preflight complete, Beta 1 not started. See [BETA_STATUS.md](BETA_STATUS.md), [BLOCKERS.md](BLOCKERS.md) and [NEXT_ACTION.md](NEXT_ACTION.md).

| Path | Purpose |
|---|---|
| `AGENTS.md`, `MANHUNT_CODEX_MASTER_PROMPT.md` | Agent rules and project brief |
| `docs/` | Setup, configuration, architecture, testing, decisions |
| `analysis/` | Analysis scripts (XBE header parser) |
| `scripts/` | Build and manifest scripts |
| `infra/` | Dockerfiles and compose |
| `tests/nro-proof/` | Minimal libnx NRO |
| `reports/` | Preflight and evidence |
| `tasks/`, `TASK_GRAPH.md` | Task ledger |

Start with [docs/SETUP.md](docs/SETUP.md).
