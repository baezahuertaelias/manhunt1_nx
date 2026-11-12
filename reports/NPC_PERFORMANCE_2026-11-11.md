# NPC slowdown investigation — 2026-11-11

Status: targeted host optimization implemented; Switch FPS impact **UNVERIFIED**. No AI updates, animation evaluation, NPC counts, skinning quality or game timing are skipped or reduced.

## Evidence and limits

The owner reports about 12 FPS with NPCs near the court versus about 20 FPS without them. The rollback capture, `game/build/hw-test/gpu-offload/20261110-234419-rollback/run.log`, uses all four GPU offload flags off and `RECOMP_RENDER_PROFILE=0`. Its pre-fault heavy samples still show both GL draw submission and guest/executor time as substantial costs. This does not isolate an AI or animation routine as the hotspot. The later nonfinite-transform episode changes the rendered workload, so its roughly 890–960 draws/frame must not be compared with the earlier roughly 250–400-draw court samples as a performance improvement.

Kernel summaries in that rollback report approximately 7.93 million `KeQuerySystemTime` calls and 2.27 million each of `RtlEnterCriticalSection`/`RtlLeaveCriticalSection`. These are cumulative counts over the run; they do not measure time or prove an NPC-specific cost. Nevertheless, their owning implementation exposes a removable cost: each critical-section enter and leave takes a global SRW read lock merely to resolve the guest address to its persistent native mutex. The actual mutex is acquired separately afterward.

## Implemented change

Patch `0031-critical-section-resolver-cache.patch` adds an eight-entry thread-local cache of resolved critical-section pointers. A slot in the backing 4096-entry table is never deleted or moved, so a pointer obtained while holding the table lock remains valid for the process. A cache hit avoids the global table read-lock and hash probe. A miss uses the unchanged locked lookup/creation path and remembers the result.

The actual recursive mutex, contention handling, guest owner diagnostics and interrupt/scheduler paths remain in use for every enter/leave. Null pointers, cache collisions and the existing `RECOMP_CS_MODE=single` path retain their semantics. `MANHUNT_CS_CACHE=1` opts in; the default and explicit `0` use the existing lookup path until console acceptance.

## Host verification

Run `tests/npc-cs-cache/run.sh` inside `manhunt-recompiler:xboxrecomp-f0f8a46` as root. It creates an isolated patched copy of the owning runtime source and links the real POSIX mutex implementation. It tests 1024 distinct guest-lock identities and cache replacement, repeated lookups, null, concurrent first resolution, thread-local reuse, recursive locking and four-thread mutual exclusion over 120,000 protected increments. It also covers the existing single-lock diagnostic mode.

An x86-64 Docker microbenchmark with three million hot resolver lookups measured approximately 17.3 ns/call with the cache disabled versus 2.0 ns with it enabled. The complete uncontended enter/leave pair measured approximately 54.8 versus 24.5 ns over 500,000 iterations. Cached lookup took one table read instead of three million. Log: `game/build/npc-cs-cache/results.log`. Timing varies with host load; these numbers establish removal of resolver overhead on this host and are **not** a Switch FPS or combat-performance claim.

At roughly 2.27 million pairs over 310 seconds, the measured x86 pair saving corresponds to only about 0.22 ms of CPU time per second. This is a small overhead cut; it does not explain a 12-versus-20 FPS difference. Switch synchronization costs may differ and need measurement.

## Additional localization

The static identification inventory associates `sub_001D06B0` and `sub_001D1030` with `skinxboxrender.c`, `sub_001D2210` with `skinxboxinstance.c`, and `sub_001D8C80`, `sub_001D9210`, `sub_001DA1E0` with animation `stdkey.c`. These are source-zone inferences (confidence 0.85), not verified function names or measured hotspots. No native pose/AI replacement is justified solely by this inventory.

The renderer's separate compact-converted-attribute queue change addresses a more direct NPC/normal data-volume cost; its proof and hardware acceptance belong to the GPU offload report. Sampling the host GL-parity build during a repeatable NPC encounter is the next discriminator for additional game-side CPU work.

For the combined console candidate, keep clocks and scene fixed, set `RECOMP_RENDER_PROFILE=0`, and record the cache flag with the NRO hash. Compare pre-fault samples only; do not attribute black-screen/NaN workload changes to optimization. No FPS gain or exact visual/gameplay equivalence is claimed until that run.
