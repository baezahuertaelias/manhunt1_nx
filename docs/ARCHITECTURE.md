# Architecture

Architecture is undecided until the recompiler and platform proofs pass. The first decision gate is one representative XBE function plus a minimal NRO, not a whole-game implementation.

## Known facts (from [reports/XBE_HEADER.md](../reports/XBE_HEADER.md))

- Guest: 32-bit x86 little-endian, retail XBE, base `0x00010000`, image size `0x41cf60`, entry `0x001fe908`, uses TLS.
- Linked XDK 5849 libraries: `XAPILIB`, `D3D8LTCG`, `D3DX8`, `DSOUND`, `XMV`, `XVOICE`, `XGRAPHCL`, `LIBC`, `XBOXKRNL`. The middleware is statically linked into named sections (`D3D`, `DSOUND`, `XMV`, `MSSMIXER`, `DOLBY`, …). Recompiling those as guest code is probably the wrong boundary; they are candidates for native replacement.
- 111 kernel imports via the ordinal thunk table at `0x002a6360`.
- Host: AArch64, Horizon via libnx; NRO homebrew.

## Open questions

- Guest address model: a reserved 32-bit arena at a fixed host base vs. per-access translation.
- x87 vs. AArch64 float semantics (precision, rounding) for physics/AI.
- Indirect calls and vtables: a dispatch table keyed by guest address.
- Which statically linked XDK functions to intercept (needs signature matching against XDK 5849).
