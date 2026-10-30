# Next action

**LEVEL-LOAD-001** (P0): the corrected Switch build (attempt3) now reaches in-level 3D rendering with no guest exit in 420 s, but frames are intermittently corrupted and player spawn is UNVERIFIED ([reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Attempt3 result").

1. Run the prepared host hardware-watchpoint probe (`fault/watch-header.gdb`) to find the writer that corrupts allocator header 0x81C6BFF8 before the invalid free in 1F0720 (1F1040←1F79B0←13D730). Do not patch the final invalid write without that writer.
2. Then rerun the Switch build with movement input after the loading screen, to distinguish a controllable spawn from an intro cutscene and to check whether frame corruption remains.

Environment display is partially observed; spawn, gameplay graphics, audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only. Preserve configurable resolution/aspect/projection for WIDE-002.
