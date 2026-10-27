# Feasibility and bring-up: milestone 2 (Boot)

Task: FEASIBILITY-001. 2026-10-26 to 2026-10-27. Every statement is tied to a run or an inspected source; anything not executed is marked UNVERIFIED.

**Result: PASS in Eden 1.0.0 (llvmpipe).** The Switch NRO boots the recompiled game to the original main menu (`scripts/switch-boot-proof.sh`: `MH-BOOT-PROOF PASS`, 32 presents, menu assets loaded, menu frame captured). The Linux host build reaches the same menu. Real Switch hardware: UNVERIFIED.

## Acceptance for milestone 2

"Boot: initialise guest and runtime and reach the original menu" ([ROADMAP](../docs/ROADMAP.md)).

- **PASS requires** the **Switch build** (NRO) to reach the menu in the emulator, shown by:
  - the frontend assets being opened (`MAIN_EX.TXD`, `FE_M.RIB`);
  - frames presented continuously;
  - a captured frame (framebuffer dump) of the menu;
  - no crash or exit over the run.
- A Linux-host build reaching the menu is intermediate evidence, not a pass.
- Hardware stays UNVERIFIED.
- The visual check of the captured frame is manual: images derived from the game are not stored in the repo.

## Inputs (checked)

The XBE references the front-end data, and it is present in `assets/manhunt1_xbe`:
- `D:\levels\global\pictures\{menu,title,load}`
- `initscripts\frontend\*`
- `FONT_XBOX.DAT`
- `xbox_text\XBOX_GAME.GXT`
- `audio\xbox\MUSIC\FRONTEND\FE_M.RIB`

Path case differs between the code and the files (`Pak` vs `pak`). Patch `0001-posix-case-insensitive-paths` resolves missing path components case-insensitively.

## Route taken

1. **Full lift** with xboxrecomp `f0f8a46` and a build against its POSIX runtime on Linux x86-64 (`scripts/game-build.sh`, `scripts/game-run.sh`). Debug the boot there: it's the fastest loop.
2. **Port** the runtime subset to Horizon (libnx): `scripts/game-build.sh switch`, patch `0003-horizon-port`, `game/src/main_switch.c`, `game/src/horizon_compat.c`. Then run in Eden.

## Lift

| Item | Result |
|---|---|
| `.text`-only analysis (the getting-started default) | 13,449 functions; 289 unresolved call targets. Boot dies on indirect calls into DSOUND/XGRPH/D3D/XPP addresses with no function. |
| All-section analysis | 14,822 functions (DSOUND 65→439, XGRPH 19→555, XPP 14→224, D3D 128→255); 17 unresolved targets; ~1.58 M lines of C in 60 files. DOLBY/DSSTDFX: 0 functions, consistent with APU DSP images (not x86; not verified further). |
| False function entries | `scripts/check-false-entries.py` found 4 detected starts inside an instruction. 2 are in data. 2 truncated real D3D functions (`0x0028A49E`, `0x0028C2B2`) and are fixed in `game/function_fixes.json`. |
| Build | Host: generated code at `-O1` in about 2–5 min on 4 cores; `-O0` debug variant in about 1 min. Switch (devkitA64, gcc 15): about 3 min. NRO 21.6 MB. |

## Boot bring-up on the Linux host (chronological)

Final configuration: `RECOMP_PB_EXEC=1 RECOMP_PB_WALK=dma RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=paced RECOMP_DISPATCH_GATE=1 RECOMP_FLIP_PACING=1`, plus `RECOMP_FB_WINDOW=1` for frame dumps.

| Step | Symptom | Root cause (evidence) | Fix |
|---|---|---|---|
| 1 | Wild indirect calls into DSOUND/XGRPH/D3D | `--text-only` disassembly | Full-section analysis |
| 2 | Stall after loading title assets; D3D polling `0xFD000000` | Nothing executes the pushbuffer | `RECOMP_PB_EXEC=1` (CPU rasteriser): the title/legal screen renders |
| 3 | No pad (`OHCI: no trapped-MMIO decoder for this host`) | USB needs fault-trapped MMIO; absent on x86-64 POSIX and in Eden | Native XInput at API level (`game/src/xpp_input.c`; addresses from `.XTLID` + XDK 5849 export IDs); pad marked present in `XPP_GamepadType` |
| 4 | One flip, then a crash in `SetEvent` on a guest address | D3D builds its own KEVENT | `RECOMP_TITLE_KEVENTS=1`. Finding: POSIX `SetEvent` dereferences an unknown handle instead of failing as Win32 does (not patched; not hit with the opt-in). |
| 5 | Stall in `D3D_BlockOnTime` | No fence semaphore | `game/src/d3d_fence.c` per xboxrecomp's D3D8LTCG doc |
| 6 | Endless `while (time() <= start + 3)` | Guest `ebx` = 0x40, traced with gdb (`scripts/find-reg-leak.py`, `scripts/probe-calls.sh`, `-O0` build) to a 12-byte stack imbalance per call of D3D setter `sub_0028A480`, truncated by the false entry `0x0028A49E` (padding decoded as `call` at `0x0028A46D`) | `game/function_fixes.json` + `scripts/fix-functions.py`. `_aulldiv` also proven exact (45/45). |
| 7 | Intro movie opens, then a crash in `NtClose` | POSIX `w32_handle_fd` dereferences the pseudo-handle `(HANDLE)-2` | Patch `0002-posix-pseudo-handle-close` |
| 8 | Main-menu textures open, then a guest fault at `0xFFFFFFEC` | DSOUND `XAudioDownloadEffectsImage` against an APU aperture with no device zeroed the game thread's TIB TLS pointer (`0x4004`) | `game/src/dsound_hle.c`: `DirectSoundCreate`, `DirectSoundCreateStream` and `XAudioDownloadEffectsImage` report `DSERR_NODRIVER`. The game's Miles set-up and the XMV player both handle it. **Audio is not implemented** (milestone 6). |
| 9 | **Main menu**: PLAY / SELECT SCENE / LOAD GAME / SETTINGS / BONUS FEATURES, camcorder clock advancing; about 0.5 fps (CPU rasteriser) | — | — |

Measured with the menu reached:
- **RAM mirrors** (guest `0x04000000`–`0x74000000`) made inaccessible: no fault. The boot path doesn't use them.
- **Tiled aperture** (`0xF0000000`) made inaccessible: no fault.

So the Switch build needs no RW aliasing for milestone 2. Re-check at level load (milestone 4).

## Horizon capability probe (`tests/horizon-probe`, Eden 1.0.0)

| Probe | Result | Consequence |
|---|---|---|
| Data abort → `__libnx_exception_handler` edits context → resume | No abort at all: Eden returns 0 for a load from an unmapped reserved range | Fault-based MMIO trapping can't work in Eden. Input and audio are therefore native at API level. Hardware behaviour UNVERIFIED. |
| Shared memory mapped twice | `shmemCreate` fails, rc `0x4201` | No RW aliasing |
| Code memory owner (RW) + slave (RX) | Works | One extra read-only view only |
| RW memory at chosen addresses in a reserved 4 GiB window (`0x0`, `0x80000000`, `0xFD000000`, `0xFE800000`, `0xFF000000`) | **Works**; process memory 3285 MiB | `XBOX_PTR = base + VA` is kept unchanged on Horizon |

## Switch port (patch `0003-horizon-port`, `game/src/*_switch.c`, `horizon_compat.c`)

| Area | Implementation |
|---|---|
| Runtime subset | Kernel, platform and pushbuffer executor sources only; no SDL2/OpenGL/OpenSSL/APU. D3D is statically linked in the title; input and audio are at API level. |
| Memory | Win32 `VirtualAlloc`/`VirtualFree`/`VirtualProtect`/`CreateFileMapping`/`MapViewOfFile(Ex)` implemented with virtmem reservations plus code-memory owner mappings at fixed addresses (`win32_compat_horizon.inc`). A second view of a mapping (the 28 mirrors) fails with error 50; protection changes are no-ops. |
| Fault decoder, watchpoints, AC'97 trap | Compiled out (`__aarch64__ && !__SWITCH__`) |
| libc gaps | `tm_gmtoff` (0), `fstatvfs` (fixed geometry), `timegm`, `sysconf`, `pwrite`, `posix_memalign` |
| Directory handles | Horizon `open()` can't open directories: a placeholder fd stands for the directory, and `fstat` answers for it |
| Threads | Every runtime thread gets a round-robin preferred core and mask 0–2; the guest thread prefers core 0 |
| Presentation | `xbox_FramebufferWindowPresent` scales the guest surface into a libnx 1280×720 framebuffer; `xbox_FramebufferDumpBmp` writes BMP evidence to `sdmc:/manhunt-frames` |
| Logs | stdout/stderr → `svcOutputDebugString`, visible in Eden's log |
| Game data | `sdmc:/manhunt/` (copy of the input, verified against the asset manifest); saves `sdmc:/manhunt-save`; config `sdmc:/manhunt.env` |

### Switch bring-up (chronological, Eden)

| Step | Symptom | Root cause | Fix |
|---|---|---|---|
| S1 | `DEFAULT.xbe` listed by `opendir` but not openable (ENOENT) | Assets were bind-mounted read-only; Eden opens SD files read/write | Copy the data onto the emulated SD card once, verified against `reports/ASSET_MANIFEST.sha256` |
| S2 | XAPI's first disk open fails and the title calls `HalReturnToFirmware(2)` | `open(O_DIRECTORY)` is unsupported on Horizon | Placeholder directory fds (patch 0003, `kernel_file.c`) |
| S3 | Stall in XPP; even the watchdog never ran | Horizon doesn't time-slice equal-priority threads on one core | Spread runtime threads over cores 0–2 (patch 0003); the watchdog was also not started (added to `main_switch.c`) |
| S4 | XAPI spins on USB `HcControl` bit `0x100` | **Eden presents code-memory owner views 0xFF-filled**, not with the zeroed source; device apertures and guest BSS weren't zero | Zero each mapped view after mapping (patch 0003) |
| S5 | **Main menu** reached, 32 presents in 240 s, menu frame dumped and confirmed visually | — | — |

## Open (next milestones)

- **Performance:** about 0.1–0.5 fps (CPU rasteriser in the pushbuffer executor; under emulation on the Switch side). Needs a deko3d back end (`nv2a_backend.h` lets a project register one). Milestone 3+.
- **Audio:** not implemented; native DirectSound at API level. Milestone 6.
- **Input on Switch:** the native path is wired (script or libnx pad) but no input-driven menu navigation has been tested yet.
- Whether level loading uses RAM mirrors.
- The game's x87 control word.
- POSIX `SetEvent` on unknown handles crashes (upstream robustness finding).
