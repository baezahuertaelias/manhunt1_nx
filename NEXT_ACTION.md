# Next action

**RENDER-MENU-BACKEND-001** (P0): implement a guarded native menu adapter with generic combiner semantics, transform-once full-batch Q/W eligibility, actual menu surface/texture/filter/alpha/blend composition and explicit CPU fallback. Keep guest RAM authoritative with synchronous per-callback wait/readback. Opt-in game registration needs original-menu image/navigation comparison and independent review. [Evidence and scope](reports/RENDER_PLAN.md).

The synchronous coherence fixture passes host3/3, a clean real-executor/deko3d NRO run, deliberate skipped-readback failure, full Switch build and independent source/log/all36artifact review. It covers two clears/three draws, padding/preserved pixels, two surfaces, CPU writes, rendered-texture reads and semaphore/presentation ordering. It supports only declared unit-W FFP/basic opaque inputs and exact synthetic surfaces, so it cannot be registered as the game backend.

No native game GPU renderer, gameplay, concurrent CPU/GPU coherence, performance or hardware acceptance follows from these proofs. Audio, Horizon memory/x87 fidelity and level loading remain open. Preserve configurable resolution/aspect/projection for WIDE-002.
