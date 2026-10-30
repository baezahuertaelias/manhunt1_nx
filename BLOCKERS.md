# Blockers

Ranked. Evidence: [reports/FEASIBILITY.md](reports/FEASIBILITY.md), [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md), [reports/PREFLIGHT.md](reports/PREFLIGHT.md).

1. **P0 — Gameplay graphics/performance unverified.** A guarded opt-in deko3d menu adapter now passes synthetic, build,240s original-menu/navigation and independent-review gates. Same-binary emulator captures measured0.1305CPU→0.9367native presentations/s (diagnostic only, still slow). Level graphics, unsupported-state coverage, playable performance and Switch hardware remain unverified. [Evidence](reports/RENDER_MENU_NATIVE.md).
2. **P1 — Audio not implemented.** DirectSound reports "no driver" (`game/src/dsound_hle.c`); a native DirectSound at API level is required (milestone 6).
3. **P1 — x87 precision/rounding fidelity unresolved.** A bounded host-debug watch observed 53-bit and 64-bit requests, including directed rounding. Double-based execution remains approximate; all-thread, level and source provenance coverage remain UNVERIFIED ([investigation](reports/X87_CONTROL_WORD.md)).
4. **P1 — Eden runner not reproducible** from this repo (`dd-eden:local`, checked by image ID) (EMU-IMAGE-001).
5. **P1 — Default runner still uses software rendering.** Optional Wayland/Intel Vulkan presentation now passes the synthetic deko3d proof ([EMU-GPU-PRESENT-001](reports/EMU_GPU_PRESENT.md)). Default Xvfb lacks DRI3 and remains llvmpipe; game validation/integration of the optional path is pending. This is separate from container GPU access, which passes.
6. **P2 — RAM mirrors unsupported on Horizon.** Not used on the boot path (measured); re-check at level load.
7. **P2 — No physical Switch.** All hardware gates stay `UNVERIFIED`.

8. **P1 — Horizon memory/libc semantics (HZ-PORT-002).** Decommit/release/reservation/remap ownership and close/publication races are corrected within the tested single whole-object anonymous-view subset:303,506hostchecks, intendedracecontrol,4,128Edenchecks, fullSwitchbuild and independentreview pass ([report](reports/HORIZON_MEMORY_OPTIONS.md)). Protection fidelity, mirrors/multipleviews, protection/mapping coverage and general native errno fidelity and level validation remain open. A64MiB split requests about192MiB peak backing/shadow allocation in the mocked test.

P0 investigation and the required next measurement are in [RENDER_PLAN.md](reports/RENDER_PLAN.md). Measured raster elapsed time is 99.766% of the scanner window in the diagnostic capture; no GPU speedup or gameplay acceptance is claimed.

Menu-preparation gaps now pass their scoped synthetic/helper/diagnostic gates: bilinear/combiner/alpha0/additive readbacks, bounded short-P8/ARGB8/DXT23 decoding, and captured eligible Q1/W6 measurement. The former menu semantics/composition/registration gap is now resolved within the tested menu subset; blocker1 retains gameplay/performance coverage ([NEXT_ACTION.md](NEXT_ACTION.md)).

Synchronous executor→GPU→guest-RAM correctness now passes a restricted synthetic fixture and negative control. Menu semantics/composition and guarded registration now pass their scoped gates; blocker1 remains open for gameplay/performance; concurrent access, gameplay, performance and hardware remain UNVERIFIED.

HZ-ALIGN-001 is resolved within its allocation contract:69host+69Horizon checks, deliberate-defect control, full Switch build and independent review pass ([report](reports/HORIZON_ALLOCATION.md)). Decommit/release/remap/pwrite and other Horizon fidelity work remain pending.

HZ-PWRITE-001 is resolved for the current private partition-initialization caller: the global emulation is removed; host28/Eden25 checks, a deliberate missing-seek failure, fullSwitchbuild and independent review pass ([report](reports/HORIZON_PARTITION_WRITE.md)). This does not provide a generic atomic pwrite API.

HZ-TIME-001 is resolved for strict Horizon UTC conversion:1,632,300host+1,632,300Eden checks, old-code and removed-validation controls, fullSwitchbuild and independent review pass ([report](reports/HORIZON_TIME.md)). General clocks, title date usage and save compatibility remain UNVERIFIED.
