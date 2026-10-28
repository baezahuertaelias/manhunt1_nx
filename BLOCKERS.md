# Blockers

Ranked. Evidence: [reports/FEASIBILITY.md](reports/FEASIBILITY.md), [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md), [reports/PREFLIGHT.md](reports/PREFLIGHT.md).

1. **P0 — No GPU back end.** Frames come from xboxrecomp's CPU rasteriser (pushbuffer executor) at about 0.1–0.5 fps in Eden. Milestone 3+ needs a deko3d back end behind `nv2a_backend.h`.
2. **P1 — Audio not implemented.** DirectSound reports "no driver" (`game/src/dsound_hle.c`); a native DirectSound at API level is required (milestone 6).
3. **P1 — x87 precision/rounding fidelity unresolved.** A bounded host-debug watch observed 53-bit and 64-bit requests, including directed rounding. Double-based execution remains approximate; all-thread, level and source provenance coverage remain UNVERIFIED ([investigation](reports/X87_CONTROL_WORD.md)).
4. **P1 — Eden runner not reproducible** from this repo (`dd-eden:local`, checked by image ID) (EMU-IMAGE-001).
5. **P1 — Eden hardware presentation unresolved.** Eden + `/dev/dri` under Xvfb previously failed ("Device lacks a present queue"); emulator runs remain llvmpipe. Standalone Intel GBM/EGL rendering/readback now passes ([GPU-PROOF-001](reports/GPU_PROOF.md)); this does not establish Eden Vulkan presentation.
6. **P2 — RAM mirrors unsupported on Horizon.** Not used on the boot path (measured); re-check at level load.
7. **P2 — No physical Switch.** All hardware gates stay `UNVERIFIED`.

8. **P1 — Horizon memory/libc semantics (HZ-PORT-002).** Confirmed source defects include nonzeroing decommit, incomplete release/remap bookkeeping and non-atomic `pwrite`; current capability probes do not test these wrappers. Runtime correction and focused NRO tests remain pending.

P0 investigation and the required next measurement are in [RENDER_PLAN.md](reports/RENDER_PLAN.md). No measured GPU speedup or gameplay acceptance is claimed.
