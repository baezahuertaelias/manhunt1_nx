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
| FEASIBILITY-001 | done | lead | RECOMP-EVAL-001 | `reports/FEASIBILITY.md`: boot path, lift, Horizon capabilities, bring-up evidence |
| BOOT-HOST-001 | done | runtime | FEASIBILITY-001 | Recompiled game reaches the original main menu on the Linux host build |
| BOOT-SWITCH-001 | done | runtime | BOOT-HOST-001 | Switch NRO reaches the main menu in Eden (`scripts/switch-boot-proof.sh`) |
| RENDER-001 | ready | runtime | BOOT-SWITCH-001 | Frame-time breakdown + deko3d back-end plan (`reports/RENDER_PLAN.md`) |
| AUDIO-HLE-001 | ready | runtime | BOOT-SWITCH-001 | Native DirectSound plays the front-end music on host and Switch |
| INPUT-NAV-001 | ready | test | BOOT-SWITCH-001 | Scripted pad moves the main-menu selection on the Switch build |
| CODEX-AGENTS-001 | ready | lead | — | Codex 0.164.0 config syntax and available models verified; `.codex/agents/*.toml` for the WORKFLOW roles, documented in CONFIGURATION.md |
| NEG-CONTROLS-001 | ready | test | RECOMP-EVAL-001 | Negative controls scripted and expected to fail inside `scripts/toolchain-proof.sh` |
| EMU-IMAGE-001 | ready | test | — | Eden/Ryujinx image built from pinned source in this repo passes `scripts/toolchain-proof.sh` |
| GPU-PROOF-001 | ready | test | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | blocked | lead | user approval | Private remote created or confirmed; `main` protected |
