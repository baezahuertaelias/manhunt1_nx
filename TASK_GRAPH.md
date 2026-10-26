# Task graph

Machine-readable ledger: [tasks/tasks.jsonl](tasks/tasks.jsonl).

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| PREFLIGHT-001 | done | lead | — | PREFLIGHT.md records PASS/FAIL/UNVERIFIED for every checklist item |
| NRO-PROOF-002 | done | runtime | PREFLIGHT-001 | Revised proof rebuilds twice in pinned image with identical SHA-256 |
| GHIDRA-VER-001 | done | re | — | Analysis image's Ghidra version matches the loader; XBE import repeats |
| RECOMP-EVAL-001 | done | re | PREFLIGHT-001 | Candidate report; one function recompiled and executed with matching output |
| EMU-RUNNER-001 | done | test | NRO-PROOF-002 | Eden loads an NRO headlessly; PASS/FAIL/runner-failure separated |
| DEKO3D-PROOF-001 | done | runtime | EMU-RUNNER-001 | deko3d triangle verified by pixel readback |
| FEASIBILITY-001 | ready | lead | RECOMP-EVAL-001 | `reports/FEASIBILITY.md`: boot path, kernel/XDK functions, lift-vs-replace, x87 control word, full-lift cost |
| CODEX-AGENTS-001 | ready | lead | — | Codex 0.164.0 config syntax and available models verified; `.codex/agents/*.toml` for the WORKFLOW roles, documented in CONFIGURATION.md |
| NEG-CONTROLS-001 | ready | test | RECOMP-EVAL-001 | Negative controls scripted and expected to fail inside `scripts/toolchain-proof.sh` |
| EMU-IMAGE-001 | ready | test | — | Eden/Ryujinx image built from pinned source in this repo passes `scripts/toolchain-proof.sh` |
| GPU-PROOF-001 | ready | test | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | blocked | lead | user approval | Private remote created or confirmed; `main` protected |
