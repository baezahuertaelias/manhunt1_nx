# First-level load investigation

Task: LEVEL-LOAD-001 (P0). Acceptance: first level loads from the original menu in the Switch NRO under Eden, environment displayed and player spawned, with inspected frames and logs and no guest crash. Status: **IN PROGRESS — level load and spawn UNVERIFIED**.

## Existing attempt inspected (2026-10-30)

Private evidence: `game/build/level-load/attempt1-1/`, driver `game/build/level-load/capture-attempt1.py`. Result records runner exit 0 after 303.119 seconds with NRO SHA-256 `1f93749d7d7b534da5608ac35f77f957a3f8995ee8de4a81cc926fb44fdf5f48`. Marker-minus exit 0 proves only that the runner remained alive at its bound, not milestone acceptance.

The actual final captured image, `frames/level-attempt1-1-00069.bmp`, was inspected: SAVE GAME, eight EMPTY slots, B “Continue without save”, A “Confirm and save”. This is a menu screen, not an environment or spawn. Logs record the three A presses at 60283, 91127 and 120063 milliseconds since first pad sampling. The final log continues presenting before the bounded runner shutdown. No level acceptance follows.

## Follow-up attempt

Private driver `game/build/level-load/capture-attempt2.py` retains the same NRO and first three A presses, then B at 180000 ms to select the displayed continue-without-save action and A at 240000 ms. Each hold is 5000 ms. Bound: 480 seconds. Frame-dump capture windows extend through 470 seconds. Previous SD configuration is saved and restored in the driver finally block. Existing save backup remains at `game/build/level-load/save-before-attempt1/`.

No game-derived frames, raw logs, save data or binaries are intended for publication. Hardware playability, audio and performance are UNVERIFIED.

## Follow-up result (2026-10-30)

B was sampled at 180909 ms (log time 193.249 s). Logs then show Jury_Turf animation, environment, collision, model, AI and entity files and CASH_XBOX model/texture opens. The actual captured image shows the static transition effect; no environment/player frame is established. At log time 212.549 s the guest requests HalReturnToFirmware, routine 2, with launch data type 1 and initial words 1,0,1,0. This is an observed guest exit path, not an OOM diagnosis. Cause remains UNVERIFIED. The emulator remained alive after guest exit; the project process was manually stopped after preserving `/tmp/eden.log`. Runner exit 2 and elapsed 300.175 seconds are retained; this was an early diagnostic stop, not the planned 480-second acceptance run.

Private manifest: `game/build/level-load/attempt2-1/manifest.json`. Stack candidates include 0x001FF04A, 0x00200647, 0x00201542, 0x00201F73, 0x002021FF and 0x001FFE26. Raw stack membership is not proof of an active call chain. Host GDB reproduction is the next diagnostic technique; no production fix has been made.

## Host differential diagnostic

The existing host binary (dated 2026-10-27; not rebuilt for this investigation and not identical to the current Switch integration) was run with A at 15000/30000/45000 ms and B at 60000 ms, each held 5000 ms. `game/build/logs/run-20261030-091506.log` records Jury_Turf assets followed by signal 11, guest VA 0xFFFFFFFC, eax=0xFFFFFFF0, edi=0 and esp=0x00F7EE24; exit=139. Executed addr2line resolves the fault to sub_0001A4D0, followed by sub_0001A5B0, sub_0019EA40 and sub_0019F340, with sub_000E9C80 and sub_001FE894 farther out. These are diagnostic observations, not a proven root cause or proof that the host and Switch faults are identical.

A first host GDB run with shorter holds timed out without reaching its exit breakpoint. A second run with longer holds stopped on SIGSEGV at the read in sub_0001A4D0 (`eax + 0xC`); it did not hit HalReturnToFirmware. A third bounded run adds full backtrace and guest-register capture at the stop. No failed hypothesis has been patched around.

Third GDB run captured the active host stack: sub_0001A4D0 ← sub_0001A5B0 ← sub_0019EA40 ← sub_0019F340 ← sub_000BE5B0 ← sub_000BE700 ← sub_000E6B80 ← sub_000E9C80 ← sub_001FE894. Registers match the ordinary host crash. Private full debugger evidence: `game/build/level-load/host-diagnostic/gdb.log`; wrapper output `runner3.log`. The initial cleanup interpretation was unverified and is superseded by the control-flow analysis below.

## Parallel investigation correction and dispatch lead

Source/original-assembly inspection by load_control establishes sub_000E6B80 is called in the active main-loop path while state +0x1C is zero; that stack does not establish main-loop exit or cleanup. Independent evidence review found the earlier observed loading defect: unresolved indirect call 0x0019B572 at 207.242861 s, followed by repeated unresolved 0x0005C620 calls beginning 207.614972 s in attempt2. Both targets are absent from the current generated dispatch. Original entry/boundary and ABI verification is underway before any manual-function addition.

The current host source rebuild completed successfully at `game/build/level-load/baseline-current/build.log`; this is a build result only, not new level acceptance.

## Reviewed candidate corrections

The independent reviewer inspected actual captured frames and identified “BORN AGAIN — LOADING...”, establishing the first level was selected (internal name Jury_Turf). All 88 attempt2 manifest artifacts hash-match on independent review.

The 0x0019B572 target is a local switch arm of 0x001977C0, not a standalone function. Original bounds permit 300 table entries; the generated code included only 256 entries / 232 unique arms. Independent raw-table inspection finds 300 local entries / 273 unique arms, index269 = 0x0019B572, and a non-code sentinel at slot300. Adding that arm as a function would be wrong. Maintained patch0014 extends existing bounded scans to4096 while retaining address/owner/file bounds. The fully synthetic large-switch regression executes all300 arms plus default and verifies EBX/ESP; author and independent reruns pass301 executions. Unpatched source fails at arm256, establishing a regression control.

The separate measured target0x0005C620 is a genuine 29-instruction function ending ret4 at0x0005C699 (exclusive end0x0005C69C), independently decoded. A reviewed explicit addition to function_fixes.json inserts it into an uncovered gap; existing duplicate/overlap/drift checks fail closed. Four synthetic tests pass, independently rerun.

Build integration previously imported the unpatched image translator before applying maintained patches. game-build.sh now applies patches and imports from the patched tree before lifting, with patch contents in the generation cache key. Independent source review and extracted-loop invalid-patch negative control pass. Full relift/host build is in progress; no corrected game acceptance yet.

## First corrected-host runtime result

Full relift and host build pass (`baseline-current/build-fixed-host.log`, `game/build/logs/lift.log`, `game/build/logs/build-host.log`). Independent regenerated-code inspection confirms all273 original local targets and generated/registered5C620 with original ret4. The lift still reports17 unresolved stubs and88 unimplemented instructions; title-wide completeness is not claimed.

The resulting host run (`game/build/logs/run-20261030-093047.log`, RECOMP_STUB_LOG=1) samples all four configured inputs and has no logged unresolved ICALLs before a new SIGSEGV at guest0xFFFFFFFF. Original-assembly inspection confirms it reached loading state4 via BDF60→18C890, beyond the former stage3 failures. The inspected final frame fb00006 is black with sparse white streaks, not a proven environment/player. The active rendering allocation path is1F0D10←1F0FC0←1F1040←1F79B0←13D6E0←17D0E0←18C890. Root cause of the new allocator metadata failure remains under investigation; changing the final invalid write without finding its source is not justified. No Switch rerun or acceptance is claimed yet.

## Allocator precursor and Switch build

The corrected Switch build passes, NRO SHA-256 `28c350a057f837413bcae0d71c667fcccad262130431abf117370aa2367ddcf9` (`baseline-current/build-fixed-switch.log`). A bounded420-second original-menu/first-level attempt3 is now running with that NRO and native backend; acceptance remains pending.

A conditional host GDB probe stops before the first observed invalid physical free: pointer0x01C6C000, header0x81C6BFF8, base/size both0xFFFFFFFF, in1F0720 from1F1040/1F79B0/13D730. Actual private `fault/free-header.log` records the header bytes, resource descriptor and active native stack. The targeted clear at offset0x00871080 has512x512 clip/pitch2048 and matching full rectangle; its direct write footprint does not overlap the captured header, rejecting that proposed direct-clear explanation. Exact corruption writer remains unverified; a hardware-watchpoint probe is being prepared.

## Attempt3 result (Switch NRO, inspected 2026-10-30)

Private evidence: `game/build/level-load/attempt3-1/` (driver `capture-attempt3.py`), NRO SHA-256 `28c350a057f837413bcae0d71c667fcccad262130431abf117370aa2367ddcf9`, runner exit 0 after 424.2 seconds (420 s emulator bound). Inputs logged: A at 60170, 91072, 120099 ms and B at 180356 ms. The guest and runner logs contain no HalReturnToFirmware, guest fault or unresolved-ICALL line; the emulator reports "still running at end". This is the first observed level-load run on the Switch build with no guest exit within the bound.

All 87 captured frames were inspected as contact sheets, and frames 74–86 individually:

- 00000–00046: main menu, brightness, difficulty and SAVE GAME screens, then "BORN AGAIN — LOADING...".
- 00047–00073: the same loading screen.
- 00074–00076: full-frame monochrome static (plausibly the intended CCTV-style transition; not proven).
- 00079 and 00085: a dark 3D scene with a lit humanoid figure, a large vehicle-like shape at right, and a partly legible red/white overlay at top in 00079. This is the first frame evidence of in-level 3D content on the Switch build.
- 00077, 00078, 00080, 00081, 00083: mostly black with horizontal streaks.
- 00082, 00084, 00086 (the last frame): a coloured noise band across the upper half with black below. These are corrupted presentations.

Interpretation: the level reaches 3D rendering of an environment with a character, but the presentation is intermittently corrupted and heavy white streaking appears in every in-level frame. Whether the figure is the controllable player spawn or part of an intro cutscene is not established: no input was sent after 180 s and no movement was tested. Environment display is therefore **partially observed**, player spawn is **UNVERIFIED**, and LEVEL-LOAD-001 is **not accepted**. Whether the corruption has the same cause as the host allocator-header corruption (above) or comes from a rendering-backend gap (every attempt, including attempts 1 and 2, logs that the tiled aperture at 0xF0000000 failed with error 50, "rendering writes will fault"; MEM-MIRROR-002 covers that trap) is UNVERIFIED.

## Corrected Switch attempt3 result

Attempt3 completes424.208 seconds wall time with runner exit0 (configured420s), NRO28c350a0..., no logged prior unresolved ICALL or guest exit/crash. Present counts advance through233 by the final periodic capture. Actual inspected executor frame00086 is640x480 with black/colorbands, not a recognizable environment/player. All82 640x480 and five512x512 executor captures remain private; offscreen target sizes alone do not establish a presentation pitch defect. Acceptance therefore remains UNVERIFIED.

Frozen104-artifact build/source/run manifest: `game/build/evidence/level-load-28c350a0-20261030/manifest.json`. The first fixed-address dual-alias hardware-watchpoint host run reproduces SIGSEGV without catching an allones transition; actual writer remains unproven. A targeted raw-position/source capture is running, using observed color-like float bitpatterns to distinguish misaddressed vertex buffers from transform errors. No projection, fence or clear change has been made on that hypothesis.

## Diagnostic reassessment

Two targeted host probes did not catch the original writer: fixed physical/low-alias watchpoints missed before SIGSEGV, and the exact-color-bit attr0 condition did not trigger before SIGSEGV. These are negative diagnostic results, not evidence of correct headers/vertices. Stop guessing fixed addresses/bitpatterns. Chosen next technique: bounded dynamic allocation/free association, recording requested size/alignment, actual returned pointer, initialized header and first mismatch; install watches against an actual live allocation if a stable lifecycle is established. Alternative: isolated ignored generated-code diagnostic hooks at allocation-return/free semantic sites, if optimized GDB locations cannot capture them. Timebox one lifecycle capture before reassessing again. Production rendering/fence behavior stays unchanged until a writer/incorrect pointer is established.

## Lifecycle tooling failure and technique change

The first Python-GDB lifecycle capture is inconclusive: filtered inferior output and no terminal counters prevent establishing whether it reached the relevant allocations. The revised unfiltered/deferred capture proves all four inputs were sampled (15852,30541,45956,60221 ms), then GDB itself reports an internal fatal SIGSEGV immediately after arming at BE5B0. Its checkpoint contains only one arming event, zero allocations/frees/live records, and zero evictions. This is a debugger failure, not a new port fault. Exact evidence remains at `control/allocator-lifecycle2-{raw,inferior,gdb}.log` and checkpoint JSON.

Independent original-assembly audit of 291C00/1F1040/1F79B0 finds no direct lock-pointer subtraction or upload-count mismatch: lock returns resource.data|0x80000000 and upload copies exactly28*vertex_count. Independent GPU writer review does not establish a surface/semaphore/report write spanning the bad header. Next technique is an isolated generated-C diagnostic object and binary, reusing normal build objects read-only, recording bounded allocation/free/upload history and first header mismatch. Normal host/NRO production artifacts are preserved.

## Native observer result: tracked header change

The isolated single-object diagnostic build and synthetic controls pass; original host binary SHA-256 stays644bde5f...5848b. Independent observer review confirms no guest writes, bounded64-event/64-header storage, deliberate stop86 controls and eight invalid-pointer guard cases. Native run samples the menu inputs and intentionally stops86 on a tracked header mismatch: pointer0x01CEF890, header0x81CEF888, previously0x01CEF888/0x00003808 at snapshotseq6247, then0xFFFFFFFF/0xFFFFFFFF before an observed free. Counters:6615events,1316allocations,1266frees,2717uploads,1200evictions and1200untrackedfrees. First16 upload destinations are correct high aliases; direct low-address upload hypothesis is rejected for those observations.

Independent review verifies all12 diagnostic manifest artifacts and the retained slot. Last successful check is immediately before uploadevent6615; the next free-entry check detects the mismatch. Exact writer, bypassing whole-arena resets/frees and concurrent observer accuracy remainUNVERIFIED. Do not claim the tracked allocation was necessarily live at corruption. Private evidence: `game/build/level-load/control/diag/manifest.json`.

The64-event tail does not retain the allocation's full requestedsize/caller/ordinal, so selecting fixedsequence6247 in another run would be unsupported. Next bounded capture records every observed0x3800 allocation marker and watches at most two recently observed matching allocations using actual addresses, retiring watches on observed frees. This limited selection must be explicit even if a writer is caught. Synthetic marker/watch infrastructure is tested before the game run.

## Allocator corruption chain (host, 2026-10-30)

Private evidence: `game/build/level-load/fault/{watch-header,alloc-segv,pool-ptrs,node-watch,node-watch4}.{gdb,log}` and the earlier `control/diag*/manifest.json`. Host binary `game/build/host/manhunt` (corrected build, 09:30). All runs use pad script A 15/30/45 s, B 60 s.

- The fixed-address header watch (0x81C6BFF8 / 0x01C6BFF8) never fired: the run crashed first in sub_001F0D10 at the header store `MEM32(ebx) = eax` (original 0x001F0EEF) with guest eax=ebx=0xFFFFFFFF, edi=7. ebx = (edi − 8) | 0x80000000, so the write targets guest 0xFFFFFFFF.
- At that crash the region node the allocator was splitting (esi=0x00F84A18, the head stored at 0x413D78) held base=size=0xFFFFFFFF. Nodes are 16-byte {base, size, next, prev} free-list records from the pool at 0x413D2C (pool alloc sub_001E1F00, pool free sub_001E2030 via the pointers at 0x4195C4/0x4195C8). Neither pool routine writes all-ones, and the bitmap `sar edx, cl` at 0x001E1F5D is a 32-bit shift in the original, so it is lifted correctly. Both rejected.
- The bad node's address varies between runs (0xF84A18, 0xF84A68, 0xF84A88/0xF84A98 in an earlier run). A hardware watch on node 0xF84A18 recorded 641 ordinary writes from sub_001F0D10/sub_001F0720 and no all-ones value in that run, which crashed on node 0xF84A68 instead.
- A four-node all-ones watch caught the writer: sub_001F0720 (the free path, via sub_001F1040 ← sub_001F79B0 ← sub_0013D6E0 ← sub_0017D0E0 ← sub_0018C890) stored base=0xFFFFFFFF, size=0xFFFFFFFF into fresh node 0xF84A88. The original code at 0x001F0726–0x001F0739 loads those values from the freed block's header at (ptr | 0x80000000) − 8 / − 4.

Measured chain: (1) a live block header becomes FFFFFFFF/FFFFFFFF (the earlier diagnostic build shows it valid at allocation, invalid at free); (2) freeing that block copies the invalid header into a free-list node; (3) a later allocation from that node writes to guest 0xFFFFFFFF and faults. (2) and (3) are original game behaviour given (1); they are not lifting defects. Do not patch (2) or (3).

Lead for (1), not yet proven: in the earlier diagnostic, the corrupted header 0x81CACB08 begins exactly at the end of the preceding 0x3800-byte allocation (user 0x01CA9308 + 0x3800). That fits an overrun past the end of that preceding resource, by guest code or by native NV2A emulation writing guest RAM, but a write aimed at the header directly is not excluded. The writer remains UNVERIFIED.

## Root cause: early fence return (2026-10-30)

Investigation and independent review evidence: `game/build/level-load/fault2/` (private, `SHA256SUMS` verified by the reviewer).

- **Writer:** native, not lifted guest code. The NV2A software rasteriser `put_pixel` (xboxrecomp `nv2a_pb_exec.c`) on the pushbuffer executor thread (`nv2a_ack_thread` → `pb_consume` → `draw_primitive` → … → `xf_rows` → `put_pixel`) wrote 0xFFFFFFFF texels over a live allocator header. Caught in 3 of 3 instrumented host runs; guest registers on that thread are all zero.
- **Cause:** our `D3D_BlockOnTime` replacement (`game/src/d3d_fence.c`, guest 0x00292200) gave up after 250 ms. The original's final spin (0x00292370–0x00292378) has no bound. D3DResource_BlockUntilNotBusy (0x002926B0, via 0x002907A0 from 0x001EF210) waits there before a texture free (0x001EF1E0 → sub_001F0720). After the timeout the title freed a 512x32 A8R8G8B8 render target (block 0x01CAA080, size 0x10080, freed at semaphore 707 while fence 733 was outstanding). The allocator reused that memory for 0x3800 vertex blocks, and the executor's late draws (semaphore 729) wrote into the stale surface footprint, over header 0x81CAF408. The earlier "0x3800 adjacency" lead was coincidental.
- **Fix:** with `RECOMP_PB_EXEC` (the boot default) the wait is unbounded, logging every 2 s while it waits. As in the original (0x0029221E), a fence outside (semaphore, newest] returns at once. Without the executor the logged 250 ms bound is unchanged. Focused test `tests/d3d-fence/run.sh`: 7 cases pass; the previous code fails the lagging-semaphore cases. Host: one instrumented run (300 s bound) showed no header change across 1,856 watched blocks and one plain run showed no crash; their exit codes and exact durations are not recorded. Independent review: PASS WITH NITS; the nits are addressed.

## Attempt4 (Switch, fence fix without the window check)

NRO `f2c463121ddcd0b3d7767122a2b881bd657bd0feb53d0d808df7693976c33fc2`, native backend, runner exit 0 after 565 s (560 s bound), no guest exit or fault logged. Menu inputs sampled at 60.6/90.4/120.8/180.2 s. 31 `still waiting … 2000 ms` fence logs during level load and play, all of which completed.

All 85 frames were inspected. Loading screen, then CCTV static, then from about 326 s to 546 s a consistent in-level scene: the camcorder REC overlay, an advancing TCR timecode, a standing figure and a vehicle. The coloured-noise bands of attempt3 are absent; one frame (about 344 s) is mostly black.

Frames arrive about every 18 s, and the timecode advances only a second or two between them, so the emulated game runs at a small fraction of real time. The movement steps (4–12 s holds at 360–482 s) were never sampled, because the title reads the pad about once per game frame. The figure did not move, so spawn and controllability remain UNVERIFIED. Attempt5 uses the window-checked fence wait (NRO `7b3c6f90…`) and holds the stick for minutes.
