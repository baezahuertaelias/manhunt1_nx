# Decisions

## 2026-10-26 — Stop before implementation

The repository has no source/input/toolchain evidence and Docker/GPU/GitHub gates fail. Creating implementation tasks now would be speculation, so task creation is deferred until preflight passes.

## 2026-10-26 — Preflight closed; recompiler evaluation is the critical path

Superseded the entry above. Docker, `/dev/dri`, `gh` auth, the XBE input, the devkitA64 build and the Ghidra XBE import now pass. The XBE header shows a retail XDK 5849 title linking `D3D8LTCG`, `DSOUND`, `XMV` and 111 kernel imports, so graphics, audio and video replacements are required. Only proof and evaluation tasks are created (see TASK_GRAPH.md). Runtime/graphics work stays deferred until one function is recompiled and executed.

Rejected: full Ghidra auto-analysis as a prerequisite. It stalled on decompiler warnings, and the header facts needed now come from a direct header parse.

## 2026-10-26 — Own Ghidra base instead of `dd-ghidra-mcp:local`

The local base had Ghidra 12.1.2, and no ghidra-xbe release targets it. Chosen: build from the official, hash-verified Ghidra 12.1.4 zip on a digest-pinned Temurin JDK 21, which makes the image reproducible from Git alone. Rejected: building the loader from source for 12.1.2 (an extra fork to maintain), and accepting the mismatch (unsupported, so errors could go unnoticed). Cost: GhidraMCP is no longer in the image.

## 2026-10-26 — xboxrecomp lifter for the milestone-1 proofs

Chosen: sp00nznet/xboxrecomp at `f0f8a46` (MIT). It emits portable C, its runtime already builds on arm64, and its guest-address macro works unchanged on Horizon. It is pinned in `infra/recompiler/`, and only the lifter and header templates are used: not the LGPL APU/NV2A files.

Deferred, not rejected: TGP482/XboxXBERecomp. Its XDK 5849 signatures and LTCG handling match Manhunt and may serve XDK identification in milestone 2.

Accepted, with measurements in RECOMP_EVAL: double-based x87 at 64-bit precision control, and unmodelled two-NaN selection. To revisit once the game's x87 control word is known.

Accepted temporarily: the host-local `dd-eden:local` image as the emulator runner, checked by image ID, until EMU-IMAGE-001.

## 2026-10-27 — Milestone 2 design choices

**Native input and "no audio driver" at the XDK API level.** On x86-64 POSIX the runtime's fault-trapped device models (USB, APU) don't run, and on Horizon nothing faults (`tests/horizon-probe`). So the title's statically linked XAPI XInput and DirectSound entry points are replaced by native functions (`game/manual_functions.json`; addresses from the XBE's `.XTLID` table and XDK 5849 export IDs).
- **Rejected:** emulating the MCPX hardware. It's impossible on Horizon without fault trapping, and it's slower.
- **Accepted temporarily:** DirectSound reports `DSERR_NODRIVER`, a path the game handles (silence until milestone 6).

**Guest memory on Horizon = `base + VA`, no mirrors.** Measured on the host: the boot path never touches the 28 RAM mirrors or the tiled aperture. Horizon cannot alias RW memory, but it can place memory at chosen addresses in a reserved window (probe).
- **Rejected for now:** software address masking in `XBOX_PTR` (a cost on every access, and not needed for boot). Re-check at level load.

**Fix analysis errors in the database, not in the lifter's safety checks.** The two false function entries are removed by a reviewed fixes file (`scripts/fix-functions.py`), because the toolkit's `--coalesce-functions` rightly refuses entries with call evidence. The evidence here was padding decoded as a call.

**Game data is copied onto the emulated SD card**, not bind-mounted: Eden opens SD files read/write, so a read-only mount fails, and a writable mount would let the emulator modify the user's original files. The copy is checked against the asset manifest on every proof run.

**Upstreamable runtime fixes stay as patches** (`game/patches/xboxrecomp/`) on the pinned commit, so the toolkit can be re-pinned and the patches re-applied.

## 2026-10-26 — Retire the bootstrap prompt

`MANHUNT_CODEX_MASTER_PROMPT.md` was a one-time bootstrap brief. Its lasting content now lives in the project docs:

- rules → `AGENTS.md`
- Beta 1 definition, milestones, PoC gates, performance targets, Release 2 scope → `docs/ROADMAP.md`
- architecture principles and the function DB → `docs/ARCHITECTURE.md`
- emulator and differential testing policy → `docs/TESTING.md`
- roles, planning loop, debug loop, GitHub pipeline, recovery, provenance, reporting → `docs/WORKFLOW.md`
- layout → `README.md`

Its one-time startup steps were executed. What remains is tracked as CODEX-AGENTS-001, FEASIBILITY-001, RECOMP-EVAL-001, EMU-RUNNER-001, GPU-PROOF-001 and REPO-REMOTE-001. The original prompt is recoverable from Git history (`git show 1482bc7:MANHUNT_CODEX_MASTER_PROMPT.md`).
