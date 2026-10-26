# Decisions

## 2026-10-26 — Stop before implementation

The repository has no source/input/toolchain evidence and Docker/GPU/GitHub gates fail. Creating implementation tasks now would be speculation, so task creation is deferred until preflight passes.

## 2026-10-26 — Preflight closed; recompiler evaluation is the critical path

Superseded the entry above. Docker, `/dev/dri`, `gh` auth, the XBE input, the devkitA64 build and the Ghidra XBE import now pass. The XBE header shows a retail XDK 5849 title linking `D3D8LTCG`, `DSOUND`, `XMV` and 111 kernel imports, so graphics, audio and video replacements are required. Only proof and evaluation tasks are created (see TASK_GRAPH.md). Runtime/graphics work stays deferred until one function is recompiled and executed.

Rejected: full Ghidra auto-analysis as a prerequisite. It stalled on decompiler warnings, and the header facts needed now come from a direct header parse.

## 2026-10-26 — Own Ghidra base instead of `dd-ghidra-mcp:local`

The local base had Ghidra 12.1.2, and no ghidra-xbe release targets it. Chosen: build from the official, hash-verified Ghidra 12.1.4 zip on a digest-pinned Temurin JDK 21, which makes the image reproducible from Git alone. Rejected: building the loader from source for 12.1.2 (an extra fork to maintain), and accepting the mismatch (unsupported, so errors could go unnoticed). Cost: GhidraMCP is no longer in the image.

## 2026-10-26 — Retire the bootstrap prompt

`MANHUNT_CODEX_MASTER_PROMPT.md` was a one-time bootstrap brief. Its lasting content now lives in the project docs:

- rules → `AGENTS.md`
- Beta 1 definition, milestones, PoC gates, performance targets, Release 2 scope → `docs/ROADMAP.md`
- architecture principles and the function DB → `docs/ARCHITECTURE.md`
- emulator and differential testing policy → `docs/TESTING.md`
- roles, planning loop, debug loop, GitHub pipeline, recovery, provenance, reporting → `docs/WORKFLOW.md`
- layout → `README.md`

Its one-time startup steps were executed. What remains is tracked as CODEX-AGENTS-001, FEASIBILITY-001, RECOMP-EVAL-001, EMU-RUNNER-001, GPU-PROOF-001 and REPO-REMOTE-001. The original prompt is recoverable from Git history (`git show 1482bc7:MANHUNT_CODEX_MASTER_PROMPT.md`).
