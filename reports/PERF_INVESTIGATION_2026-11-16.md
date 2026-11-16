# Performance investigation — 2026-11-16

Branch: `perf/investigation`. Nothing in the code was changed; this is a report.

**Status: UNVERIFIED on the console.** No hardware logs were available, so the ranking below comes from reading the code and from compiling one lifted chunk on the host. Only code size and compile time were measured. No frame rate was measured.

Earlier reports show the guest CPU is the bottleneck: "rest" time of 26–48 ms per frame against 12–22 ms of GL draw time (`reports/GPU_OFFLOAD_*.md`, `SWITCH_RUN_2026-11-11.md`). So CPU-side items come first.

## Measurement: code size of one lifted chunk

`game/build/gen/recomp_0010.c` (one of 60 chunks), compiled with the pinned devkitA64 image and the production flags (`-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE -fno-strict-aliasing -ffp-contract=off`).

| Variant | Code size | vs. baseline |
|---|---:|---:|
| `-O2` (current) | 289,724 B | – |
| `-O3` | 291,056 B | +0.5% |
| `-O2 -fomit-frame-pointer` | 287,172 B | −0.9% |
| `-Os` | 251,532 B | −13% |
| Guest registers packed into one TLS struct | 258,460 B | −11% |
| Struct and `-Os` together | 219,268 B | −24% |

Compile time for the chunk fell from 17 s to 9 s with the struct. The whole NRO has 25.8 MB of code (5.85 M instructions, 14,827 functions, 113,810 `__aarch64_read_tp` calls) against a Cortex-A57 with a 48 KB L1 instruction cache. Whether smaller code is faster needs a console run.

## Findings, in rough order of expected gain

### 1. Ship the configuration that was measured

- Every hardware run used `RECOMP_GL_THREAD=1` and `MANHUNT_DUMP_SECS=0` (`scripts/gpu-offload-run.ps1:29,32`).
- The release `manhunt.env` (`scripts/make-release.py`, `ENV`) and the README example set neither, so players run an untested configuration.
- `config_init` in `game/src/main_switch.c` also defaults `MANHUNT_DUMP_SECS` to 15.
- The earlier A/B (`PERF_AUDIO_STATUS_2026-11-10.md`): 25.0 FPS with the GL thread on against 22.25 off, and APU load 61% against 85% of a core.
- Proposal: make GL thread on and dump off the defaults. Not checked: whether the 15 s dump does any work with the GL backend.

### 2. Pack the guest registers into one TLS struct

- `eax`, `ecx`, `edx`, `esp`, `ebx`, `esi`, `edi` and `g_seh_ebp` are separate `__thread` globals (`recomp_types.h`).
- With `-mtp=soft` every function calls `__aarch64_read_tp` and saves 10 callee-saved registers just to hold TLS addresses.
- With one struct (`g_regs.r_esp` and so on) a function saves 2 registers and uses plain offset loads.
- Struct member names must not be `eax` etc., because the generated code `#define`s those names.
- Needs a patch for the header and for the definitions in the runtime, then a re-lift and the existing test suites.

### 3. Try `-Os` for the generated code

- −13% size on its own; −24% together with item 2.
- Cheap to test: build two NROs and compare on the console.

### 4. Indirect-call path

- About 3,600 `RECOMP_ICALL_SAFE_AT` / `_CC` sites in the lifted code.
- Each call writes three shared volatile globals (`g_icall_trace`, `g_icall_trace_idx`, `g_icall_count`). All threads write them, so their cache lines bounce between cores.
- Each call may do three cross-file lookups: `recomp_lookup_manual`, `recomp_lookup`, `recomp_lookup_kernel`.
- The flat dispatch table is 8 bytes per byte of guest code (about 21 MB for a 0x295328 span), scattered over 4 KB pages.
- Proposal: compile out the trace, prefill manual and kernel entries into one compact table (about 15k entries), keep one inlineable lookup.

### 5. Load the guest memory base once per function

- The generated code reloads `g_xbox_mem_offset` (through the GOT) before nearly every guest access, because volatile stores force the reload.
- Option A: a post-lift step inserts `const uintptr_t _mb = g_xbox_mem_offset;` at the top of each function and `XBOX_PTR` uses it. No lifter change.
- Option B: a reserved register (`-ffixed-x28`). Riskier: thread entry points and callbacks would have to set it.

### 6. Audio

- Voice processing was about 760 ms per second (76% of a core) after the HRTF change (`SWITCH_RUN_2026-11-11.md`).
- Ideas: NEON for the HRTF and voice mixer; explicit core pinning (guest core 0, GL core 1, APU core 2).
- Threads are currently placed round-robin with a core mask of 7 (`nx_spread_thread`, patch 0003), so they can migrate. Pinning needs a console A/B.

### 7. GL renderer

- About 440 texture binds, 248 uniform uploads, 3 ms vertex upload and 6 ms draw submission per frame in heavy scenes (`GPU_OFFLOAD_RESULTS.md`).
- Ideas: skip redundant texture binds, merge compatible draws, ship a prebuilt shader program cache for first-use stutter.
- Rendering is already at native 640x480 scale (`RECOMP_GL_SCALE` = 1), so lowering resolution will not help.
- A deko3d rewrite was already judged no-go (`DEKO3D_FEASIBILITY.md`).

### 8. Minor

- Guest memory accesses must stay `volatile` where the code polls for another thread. Dropping `volatile` only for stack accesses would need a lifter change.
- Turn off the perf thread, watchdog and `MANHUNT_LOG_SYNC=1` in play configurations.
- `MANHUNT_FPS_CAP=30` helps battery, if pacing is acceptable.

## How to settle the ranking

Build with `-DMANHUNT_CALL_PROFILE=ON` (`game/src/call_profile.c`) and run one heavy scene. The top guest functions show whether items 2 to 5 matter and whether any single function is worth replacing by hand.

## Not covered here

Raising the CPU/GPU clock was discussed separately and is not part of this report.
