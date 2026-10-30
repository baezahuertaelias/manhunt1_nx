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
