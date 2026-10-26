# Architecture

The concrete design is undecided until the recompiler and platform proofs pass (gates a–e in [ROADMAP.md](ROADMAP.md)). The first decision gate is one representative XBE function plus a minimal NRO, not a whole-game implementation.

## Principles

- **Hybrid static recompilation.** Keep the original game logic through static recompilation wherever practical. Replace incompatible Xbox platform services with native Horizon/libnx implementations, plus selective native C++ overrides.
- **Explicit guest model.** Design these deliberately:
  - 32-bit guest pointers on 64-bit AArch64, a guest memory arena and pointer translation
  - endianness assumptions, alignment, code and data relocation
  - indirect calls, vtables, callbacks, and SEH where relevant
  - x87 and SSE floating-point semantics, stack and calling conventions
  - Xbox kernel API shims
- **Traceability.** Centralize overrides and function-address dispatch so original guest addresses stay traceable. Diagnostics and logs cite guest addresses. Handle unresolved indirect targets and unknown code regions explicitly.
- **Graphics.** Replace Xbox graphics (NV2A/D3D8 as observed) with a minimal, usage-driven **deko3d** backend. Implement only the features the menu and first level actually reach: clear/triangle, textured UI, geometry, characters, lighting and material states, animation, essential effects. Convert and cache shaders and textures offline when that's justified. Do **not** build a general Xbox GPU emulator.
- **Platform.** Native controller mapping, filesystem and path handling, game data loading, audio streaming and playback, and persistence, as Beta 1 requires. Confirm the actual source APIs before choosing SDL or any other middleware.
- **Robustness.** Bounded logging, error propagation, and resource limits suited to stock Switch homebrew memory.

## Known facts (from [reports/XBE_HEADER.md](../reports/XBE_HEADER.md), cross-checked in Ghidra)

- **Guest:** 32-bit x86 little-endian retail XBE; base `0x00010000`; image size `0x41cf60`; entry `0x001fe908`; uses TLS. Ghidra language `x86:LE:32:default:windows`.
- **Linked XDK 5849 libraries:** `XAPILIB`, `D3D8LTCG`, `D3DX8`, `DSOUND`, `XMV`, `XVOICE`, `XGRAPHCL`, `LIBC`, `XBOXKRNL`. The middleware is statically linked into named sections (`D3D`, `DSOUND`, `XMV`, `MSSMIXER`, `DOLBY`, …). Recompiling those as guest code is probably the wrong boundary; they are candidates for native replacement.
- **Kernel:** 111 imports via the ordinal thunk table at `0x002a6360`.
- **Host:** AArch64, Horizon via libnx; NRO homebrew.

## Proven so far (milestone 1, [reports/RECOMP_EVAL.md](../reports/RECOMP_EVAL.md))

- **Lifter:** xboxrecomp (`f0f8a46`) turns x86 into C with a global register file, a simulated guest stack and an 8-entry `double` x87 stack. The C compiles unchanged for AArch64 Linux and for Horizon with devkitA64.
- **Guest address model:** one 64 MiB arena; host = `(uint32)guest_va + g_xbox_mem_offset`. It works with the arena above 4 GiB on x86-64, AArch64 Linux and Horizon (Eden).
- **Floating point:** exact at x87 53-bit and 24-bit precision control. 64-bit precision control is approximated with doubles (last-ulp differences). x87 two-NaN selection is not modelled. Generated code must be built with `-ffp-contract=off`.
- **Graphics:** deko3d 0.5.0 renders and reads back correctly in Eden (llvmpipe).

## Open questions

- Which x87 control word Manhunt runs with (decides whether the 64-bit approximation matters).
- Indirect calls and vtables: a dispatch table keyed by guest address (xboxrecomp's `recomp_lookup`); not yet exercised.
- Which statically linked XDK functions to intercept; this needs signature matching against XDK 5849 (XboxXBERecomp ships signatures).
- Threads, SEH and kernel shims on Horizon; xboxrecomp's runtime targets Win32/POSIX.

## Function intelligence (planned)

- **`FUNCTION_MAP.sqlite`:** keyed by XBE hash + guest address. Fields:
  - function boundaries, name, assembly and decompilation references
  - inferred signature, callers and callees, indirect targets, memory accesses
  - subsystem, Beta 1 reachability, confidence
  - implementation status, native override, test evidence, review state

  Unknown and unresolved regions are tracked too. The database is gitignored, so it must be regenerable from scripts.
- **`XBOX_API_COVERAGE` table:** every kernel, memory, graphics, audio, input, filesystem and timing interface encountered, with its compatibility status, owner, test evidence and next blocker.
- **Knowledge base:** a searchable record of RE findings, successful fixes and failed hypotheses, with confidence and evidence attached.
