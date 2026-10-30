# Next action

**LEVEL-LOAD-001** (P0): find the writer that turns a live physical-allocator block header into FFFFFFFF/FFFFFFFF during Jury_Turf loading (step 1 of the measured chain in [reports/LEVEL_LOAD.md](reports/LEVEL_LOAD.md), "Allocator corruption chain"). Steps 2–3 (free copies the bad header into a node, a later allocation faults at guest 0xFFFFFFFF) are consequences and must not be patched.

Proposed bounded probe (one host run): in the ignored diagnostic build, track each 0x3800-byte allocation from caller 0x001F0FE8 and hardware-watch only the header that follows the most recent such block, on both the 0x8xxxxxxx and low aliases, so the corrupted candidate cannot be evicted as it was before. Also record whether native NV2A clear/surface/DMA writes touch that range. Then confirm on the Switch build with movement input after loading, to tell a controllable spawn from a cutscene.

Attempt3 on Switch reaches in-level 3D rendering with no guest exit in 420 s; frames are intermittently corrupted and spawn is UNVERIFIED. Gameplay graphics, audio, x87/protection/mirror fidelity, playable performance and physical Switch validation remain open. Emulator rates are diagnostic only.
