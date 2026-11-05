# First runs on a physical Switch (2026-11-02)

Status: **PARTIAL, one console, one session.** Text report only. The frames, logs, SD-card contents and the NRO are game-derived and stay private (gitignored `game/build/hw-test/`). Emulator results are separate and remain diagnostic only.

## Setup (as executed)

- Host: Mac, Darwin 23.6.0, devkitPro at `/opt/devkitpro`; NRO built natively with `scripts/game-build-native.sh` (no Docker).
- Console: a modded (custom-firmware) Switch running Sphaira. Firmware and Atmosphère versions were **not recorded**: UNVERIFIED which.
- Game data and the NRO copied to `sdmc:/switch/manhunt1_nx/` over Sphaira's FTP (port 5000). All 836 data files matched the local copies by name and size (not by hash).
- `nxlink` from the Mac stalled or was reset by the receiver in every attempt, so a TCP log listener (`MANHUNT_LOG_TCP`) and an FTP-uploaded NRO were used.
- Launched in title-takeover mode (applet type 0, focus state 1 logged).

## What was observed

| # | Observation | Evidence | Confidence |
|---|---|---|---|
| 1 | The first NRO booted to kernel/D3D set-up and then stopped producing output about 2 s after the title opened its first data file. In one monitored run the console stopped answering ping 2 s after launch and came back about 76 s later. | SD log, TCP stream, ping monitor | Observed; the cause was not found |
| 2 | The 4 GiB guest window overlapped the libnx heap, so the 16 MiB NV2A, 8 MiB MCPX and 1 MiB flash aperture windows failed to map (error 8). `RECOMP_SPAN_4G=1` (patch 0015) or `RECOMP_EXT_VMA=1` hold the whole window as one reservation and the apertures then map. | Boot logs of 6 runs | Observed |
| 3 | The kernel has room for only 10 code-memory objects system-wide (`SlabCountKCodeMemory = 10`, `Create()` null gives `ResultOutOfResource`). The runtime uses about 5 at boot (RAM, contiguous, NV2A, MCPX, flash). | Atmosphère source (cloned master) and `[HZ]` log lines | Source CONFIRMED; no run hit the limit |
| 4 | With patch 0016 (NV2A ack thread sleeps instead of `Sleep(0)`, file I/O through a heap bounce buffer, watchdog does not pause threads) the freeze did not recur in this one run: the game ran past the first file open, loaded dozens of data files and drew the original legal/title screen, the first real frame on the console (640x480 dump, inspected by me). | TCP stream, frame dump | Observed |
| 5 | That run ended at about 5 s because `RECOMP_WATCHDOG_SECS=5` was set in the test env: the runtime watchdog prints a report and calls `_exit(3)`. A write during the shutdown then hit a null device-table entry (`_write_r`, far 0x20). Which thread made that write is not established (`_exit` skips the exit hooks, so it was probably the watchdog or log thread during libnx teardown); it happened only after the diagnostic watchdog ended the run, and it was not seen as a game defect. | `[WATCHDOG]` report then `[MH-CRASH]` | Observed |
| 6 | Atmosphère `fatal_errors` held four reports, all from one system module (program id 0100000000000034) early in each boot, none with program id MESOSPHR. Unrelated to the game; no kernel panic record exists for the freeze. | Decoded `report_*.bin` | Observed |
| 7 | At the legal screen the guest spins on `KeQuerySystemTime` (ordinal 128), about 3.8 million calls per second; no progress beyond that was seen within the 5 s window. | Kernel call summary | Observed; meaning UNVERIFIED |

## What is NOT known

- **Which change in patch 0016 removed the freeze** (ack-thread sleep, file bounce buffer, or no thread pausing): the three were applied together. UNVERIFIED; do not claim any one of them.
- Why the freeze existed on hardware but not in Eden. Hypotheses from reading code (ack thread `svcSleepThread(-1)` busy loop; file-system IPC into code-memory buffers; code-memory slab limit) are hypotheses.
- Whether the game gets past the legal screen, frame rate, audio, input, stability beyond about 5 s, other firmware versions, Mariko vs Erista.
- A kernel panic was never confirmed or excluded.

## Method notes

- The crash handler printed an offset equal to the address because `__start__` is an absolute symbol in this build; fixed to take the module base from `svcQueryMemory` (the fault above was located by scanning the disassembly for a matching instruction).
- Diagnostics added (all default off): `MANHUNT_LOG_SYNC`, `MANHUNT_LOG_TCP`, `MANHUNT_WD`, `MANHUNT_WD_DUMP`, `MANHUNT_FOCUS`, `RECOMP_SPAN_4G`. See docs/CONFIGURATION.md and docs/CHANGELOG_CONFIG.md.

## Next

Rerun on the console **without** `RECOMP_WATCHDOG_SECS` and watch whether the game leaves the legal screen; then isolate which part of patch 0016 mattered by reverting them one at a time (each failing run may reset the console).

## Controller and main menu (later runs the same day)

Four more runs with `scripts/hw-run.sh` (one console; tools in the repository). Facts, from the run files:

| # | Observation | Evidence |
|---|---|---|
| 8 | The original main menu is reached on the console in every run (101 data files opened, `MAIN_EX.TXD` and the front-end music stream among them) and stays up; the console never stopped answering ping inside a game window (short Wi-Fi drops of at most 2 samples also happen with the console idle in Sphaira). The earlier freeze did not recur in these runs. | ping monitor, stream |
| 9 | The Joy-Con is seen by the console in handheld mode (`connected=1 handheld=1`); d-pad, face-button and shoulder presses are recorded. Left as it was, the title never reacted. | `[MH-PAD]` lines |
| 10 | The title polls `XInputGetState` only about once per game frame: 0.35 to 0.45 polls per second. A normal tap falls between polls, so a poller thread latches presses and `MANHUNT_PAD_HOLD_POLLS=3` keeps a tap pressed for 3 consecutive polls. With that, the game was handed the press on 3 consecutive polls (`[MH-INPUT] poll` lines). **A later run with the final build and the hold left at 1 (plain taps) moved the highlight too (PLAY > SELECT SCENE > LOAD GAME > SETTINGS > BONUS FEATURES in about 106 s, console up throughout): the latch alone is enough and the hold is not needed.** | stream, frames |
| 11 | **The menu reacted to the controller on the console**: in a run of about 132 s the highlighted entry went PLAY, SELECT SCENE, LOAD GAME, BONUS FEATURES (5 presses, one entry per press). The press at poll 7 (about 15 s in) had no effect; the first effective press was at poll 18 in one run (about 40 s) and at poll 28 in another. So the menu starts accepting input some time after the start, between about 15 s and 40 s (roughly 7 to 20 frames at this speed); the exact moment was not measured. (One earlier run with presses at polls 7, 16 and 23 showed no movement, but its frames were half-drawn, so only the on-screen observation of no change counts.) | complete-frame dumps, poll log |
| 12 | Render profile (66 s): the software rasteriser is 98% of the frame time, about 87 ms per draw, about 1.2 us per pixel; the pool of raster worker threads exists only in the Windows branch of the code, so it is single-threaded here ([HW_PERFORMANCE_FIRST_LOOK.md](HW_PERFORMANCE_FIRST_LOOK.md)). | `[RENDER-PROFILE]` |
| 13 | The periodic frame dump used to read the surface while the rasteriser drew into it (half-drawn frames with a black band). Since this change to `horizon_compat.c` it copies the last presented frame; the dumps are labelled "complete frame". | stream, frames |

**Latency (the user's observation, not an instrument reading):** the screen changes about 5 s after a press. That fits the measurements: the title polls about every 2.5 s and a frame takes about 2.2 s to draw, so a press waits for the next poll and then for the next frame. It will shrink as rendering gets faster.

**Still not known:** why the menu waits some seconds before it accepts input (a transition animation counted in frames or time), anything beyond the main menu (level load, gameplay controls), audio, frame rate beyond 0.35 to 0.65 frames per second, thermals, firmware and Atmosphère versions.

