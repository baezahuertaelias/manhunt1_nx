# Task graph

Machine-readable ledger: [tasks/tasks.jsonl](tasks/tasks.jsonl).

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| PREFLIGHT-001 | done | lead | — | PREFLIGHT.md records PASS/FAIL/UNVERIFIED for every checklist item |
| CODEX-AGENTS-001 | ready | lead | — | Codex 0.164.0 config syntax and available models verified; `.codex/agents/*.toml` for the WORKFLOW roles, documented in CONFIGURATION.md |
| FEASIBILITY-001 | ready | lead | RECOMP-EVAL-001 | `reports/FEASIBILITY.md`: top blockers, evidence, options, recommended path to first frame and first level; libnx/deko3d/NRO APIs verified from headers |
| NRO-PROOF-002 | done | runtime | PREFLIGHT-001 | Revised proof rebuilds twice in pinned image with identical SHA-256 |
| GHIDRA-VER-001 | done | re | — | Analysis image's Ghidra version matches the loader; XBE import repeats |
| RECOMP-EVAL-001 | ready | re | PREFLIGHT-001 | Candidate report; one function recompiled and executed with matching output |
| EMU-RUNNER-001 | ready | test | NRO-PROOF-002 | Eden or Ryujinx loads the proof NRO headlessly with logs captured |
| GPU-PROOF-001 | ready | test | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | blocked | lead | user approval | Private remote created or confirmed; `main` protected |
