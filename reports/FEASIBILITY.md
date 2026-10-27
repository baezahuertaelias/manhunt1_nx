# Feasibility: milestone 2 (Boot)

Task: FEASIBILITY-001. Started 2026-10-26; this report is updated as evidence arrives. Every statement is tied to a run or an inspected source; anything not executed is marked UNVERIFIED.

## Acceptance for milestone 2

"Boot: initialise guest and runtime and reach the original menu" ([ROADMAP](../docs/ROADMAP.md)).

- **PASS requires** the **Switch build** (NRO) to reach the menu in the emulator, shown by:
  - the frontend assets being opened;
  - frames presented continuously;
  - a captured frame (or a framebuffer dump) of the title or menu screen;
  - no crash over the run.
- A Linux-host build reaching the menu is **intermediate evidence**, not a pass.
- Hardware stays UNVERIFIED.

## Inputs (checked)

The XBE references the front-end data, and it is present in `assets/manhunt1_xbe`:
- `D:\levels\global\pictures\{menu,title,load}`
- `initscripts\frontend\*`
- `FONT_XBOX.DAT`
- `xbox_text\XBOX_GAME.GXT`
- `audio\xbox\MUSIC\FRONTEND\FE_M.RIB`

Path case differs between the code and the files (`Pak` vs `pak`, `MUSIC` vs `music`). The game patch `game/patches/xboxrecomp/0001-posix-case-insensitive-paths.patch` resolves missing components case-insensitively.

## Route

1. **Full lift** with xboxrecomp `f0f8a46` and a build against its POSIX runtime on Linux x86-64 (`scripts/game-build.sh`, `scripts/game-run.sh`). Debug the boot there: it's the fastest loop, and it's the toolkit's supported host.
2. **Port** the runtime's POSIX platform layer to Horizon (libnx) and build the same lifted code as an NRO.

## Evidence so far

| Item | Result |
|---|---|
| Full lift, `.text`-only analysis | 13,449 functions; 289 unresolved call targets. Boot dies on indirect calls into DSOUND/XGRPH/D3D/XPP addresses with no function. |
| Full lift, all-section analysis | 14,822 functions (DSOUND 65→439, XGRPH 19→555, XPP 14→224, D3D 128→255); 17 unresolved targets. DOLBY/DSSTDFX: 0, consistent with APU DSP images (not x86; to be confirmed). |
| Linux run 1 (`.text`-only lift) | Runtime init OK: 15/15 sections, 111/111 kernel thunks, 28 RAM mirrors. ~200 kernel calls and ~20,500 indirect calls, then a crash on wild indirect calls into data. |
| devkitPro packages | `switch-sdl2` 2.28.5, `switch-mesa` 20.1, `switch-glad`, `switch-mbedtls` 2.28.10 and `switch-ffmpeg` 7.1 are installed in the pinned image. The runtime needs from OpenSSL only `SHA1()` (2 calls) and from libepoxy only one file (51 GL entry points). |

### Horizon capability probe (`tests/horizon-probe`, Eden 1.0.0)

| Probe | Result | Consequence |
|---|---|---|
| Data abort → `__libnx_exception_handler` edits context → resume | **No abort at all**: Eden returns 0 for a load from an unmapped reserved range | Fault-based MMIO trapping (the POSIX runtime's mechanism for APU/AC'97/USB) cannot work in Eden. Use explicit software dispatch. Hardware behaviour UNVERIFIED. |
| Shared memory mapped twice (RW mirrors) | `shmemCreate` fails, rc `0x4201` | No RW aliasing of guest RAM. |
| Code memory owner (RW) + slave (RX) | Works | Only one extra view, and read-only: not usable for RAM mirrors. |

So on Horizon, the 64 MiB address wrap the POSIX runtime gets from 28 mirrors must be done in software (mask in `XBOX_PTR`), if Manhunt needs it at all. To be measured on Linux with the mirrors protected.

## Boot bring-up on the Linux host (chronological)

Run with `scripts/game-run.sh`. Configuration as of the last row: `RECOMP_PB_EXEC=1 RECOMP_PB_WALK=dma RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=paced RECOMP_DISPATCH_GATE=1 RECOMP_FLIP_PACING=1`, plus `RECOMP_FB_WINDOW=1` for frame dumps.

| Step | Symptom | Root cause (evidence) | Fix |
|---|---|---|---|
| 1 | Wild indirect calls into DSOUND/XGRPH/D3D | `--text-only` disassembly | Full-section analysis (`scripts/game-build.sh`) |
| 2 | Stall after loading title assets; D3D polling `0xFD000000` | Nothing executes the pushbuffer | `RECOMP_PB_EXEC=1` (CPU rasteriser). **The title/legal screen renders** (frame dump). |
| 3 | No pad (`OHCI: no trapped-MMIO decoder for this host`) | USB needs fault-trapped MMIO; absent on x86-64 POSIX and in Eden | Native XInput at API level (`game/src/xpp_input.c`; addresses from `.XTLID` + XDK 5849 export IDs); pad marked present in `XPP_GamepadType` |
| 4 | One flip, then a crash in `SetEvent` on a guest address | D3D builds its own KEVENT | `RECOMP_TITLE_KEVENTS=1` (the runtime's documented opt-in). Separate finding: POSIX `SetEvent` dereferences an unknown "handle" instead of failing as Win32 does. |
| 5 | Stall in `D3D_BlockOnTime` | No fence semaphore | `game/src/d3d_fence.c`, per xboxrecomp's D3D8LTCG doc: semaphore handed to the executor, KickOff, bounded wait |
| 6 | Endless `while (time() <= start + 3)` | Guest `ebx` = 0x40 when it must be 0. Traced with gdb (`scripts/find-reg-leak.py`, `scripts/probe-calls.sh`, `-O0` build) to a 12-byte stack imbalance per call of D3D setter `sub_0028A480`. The disassembler had a **false function entry** `0x0028A49E` inside `mov ecx,[esp+8]`, created by padding decoded as `call` at `0x0028A46D`, so the lifted body ended early without `pop esi; ret 4`. | `game/function_fixes.json` + `scripts/fix-functions.py`. Same defect at `0x0028C2B2`. `scripts/check-false-entries.py` found 4 mid-instruction entries; 2 are in data, 2 fixed. `_aulldiv` also verified exact (45/45) in the proof suite. |
| 7 | Intro movie `mpeg\titles_p.xmv` opens, then a crash in `NtClose` | POSIX `w32_handle_fd` dereferences the pseudo-handle `(HANDLE)-2` | `game/patches/xboxrecomp/0002-posix-pseudo-handle-close.patch` |
| 8 | **Main-menu textures `pictures\menu\MAIN_EX.TXD` open**, then a guest fault at `0xFFFFFFEC` | The game thread's TIB TLS pointer (VA `0x4004`) is zeroed by DSOUND `XAudioDownloadEffectsImage` (`0x00223C31`). DirectSound runs against an APU aperture with no device behind it (`Unable to enable audio stream 0 (error 88780078)`), then writes through a near-null object. | **Open:** DirectSound at API level (HLE), as for input. Needed on Switch anyway (no fault trapping). |

## Open

- Does Manhunt touch RAM mirrors (addresses ≥ 64 MiB that wrap)? Decides whether the Horizon build needs the masked `XBOX_PTR`.
- Which devices does the boot need (USB pads via XAPI OHCI, APU/DSound, NV2A pushbuffers)?
- The game's x87 control word.
- Threads: Manhunt creates system threads early (`PsCreateSystemThreadEx`). libnx provides pthreads; check xboxrecomp's thread model on Horizon.
