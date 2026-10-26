# Decisions

## 2026-10-26 — Stop before implementation

The repository has no source/input/toolchain evidence and Docker/GPU/GitHub gates fail. Creating implementation tasks now would be speculation, so task creation is deferred until preflight passes.

## 2026-10-26 — Preflight closed; recompiler evaluation is the critical path

Superseded the entry above. Docker, `/dev/dri`, `gh` auth, the XBE input, the devkitA64 build and the Ghidra XBE import now pass. The XBE header shows a retail XDK 5849 title linking `D3D8LTCG`, `DSOUND`, `XMV` and 111 kernel imports, so graphics, audio and video replacements are required. Only proof and evaluation tasks are created (see TASK_GRAPH.md). Runtime/graphics work stays deferred until one function is recompiled and executed.

Rejected: full Ghidra auto-analysis as a prerequisite. It stalled on decompiler warnings, and the header facts needed now come from a direct header parse.
