# Task graph

Machine-readable ledger: [tasks/tasks.jsonl](tasks/tasks.jsonl).

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| PREFLIGHT-001 | done | lead | — | PREFLIGHT.md records PASS/FAIL/UNVERIFIED for every §17 item |
| NRO-PROOF-002 | done | runtime | PREFLIGHT-001 | Revised proof rebuilds twice in pinned image with identical SHA-256 |
| GHIDRA-VER-001 | blocked | re | decision | Analysis image's Ghidra version matches the loader; XBE import repeats |
| RECOMP-EVAL-001 | ready | re | PREFLIGHT-001 | Candidate report; one function recompiled and executed with matching output |
| EMU-RUNNER-001 | ready | test | NRO-PROOF-002 | Eden or Ryujinx loads the proof NRO headlessly with logs captured |
| GPU-PROOF-001 | ready | test | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | blocked | lead | user approval | Private remote created or confirmed; `main` protected |
