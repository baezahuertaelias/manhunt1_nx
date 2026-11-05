# Windows 11 + physical Switch: build and run

Purpose: build the Switch NRO on a Windows host with Docker Desktop and run it on a modded Switch.

**Status: EXECUTED ONCE (2026-11-04, one Windows 11 host and one console).** Results are in [reports/HW_DEKO_ADAPTER_RUNS.md](../reports/HW_DEKO_ADAPTER_RUNS.md). Lines marked UNVERIFIED were not executed. The console setup, game files and SD layout are the same as in [MACOS_SWITCH_SETUP.md](MACOS_SWITCH_SETUP.md) section 0, and the same rules apply: game files, the NRO, frames and logs never enter the repository.

## 1. Host

Windows 11, Docker Desktop with engine 29.9.2, and Git Bash for the commands below. The host's CPU and RAM and the build times are not recorded.

| Image | Reference | Contents |
|---|---|---|
| Toolchain | `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282` | gcc 15.2.0, `nxlink`, `elf2nro`, `nacptool`, `uam`, libnx, libdeko3d |
| Lifter | `manhunt-recompiler:xboxrecomp-f0f8a46` | Built from `infra/recompiler` |

A native devkitPro install was not possible from this connection: `pkg.devkitpro.org`, `downloads.devkitpro.org` and the wiki returned Cloudflare 403. MSYS2 is installed at `C:\msys64` but was not used.

## 2. Line endings

A CRLF checkout breaks the build: every file in `game/patches/xboxrecomp/*.patch` then fails with `patch does not apply`. Set these in the repository, then make sure no tracked file is left with CRLF (`git ls-files --eol` shows `w/crlf`). In this session the CRLF files were restored as LF.

```sh
git config core.autocrlf false
git config core.eol lf
```

Setting both during the clone (`git clone -c core.autocrlf=false -c core.eol=lf ...`) should avoid the problem from the start; that form is UNVERIFIED.

## 3. Build

From Git Bash, in the repository root, with your lawful game files in `assets/manhunt1_xbe/`:

```sh
docker build -t manhunt-recompiler:xboxrecomp-f0f8a46 infra/recompiler
MSYS_NO_PATHCONV=1 scripts/game-build.sh switch
```

`MSYS_NO_PATHCONV=1` stops Git Bash from rewriting the Linux paths passed to Docker. The output is `game/build/switch/manhunt.nro`; record its SHA-256 for each run.

## 4. Run

`scripts/hw-run.sh` is Mac-only (`ipconfig getifaddr`, `nc -G`, `shasum`, `sips`) and was **not run on Windows**. Its safety checks, such as the refusal of risky configs, therefore do not apply here: check the config by hand, and never set `RECOMP_WATCHDOG_SECS`.

1. Start Sphaira on the console in title-takeover mode, with its FTP server on (port 5000) and its nxlink receiver available.
2. Upload `manhunt.env` to `sdmc:/switch/manhunt1_nx/` over FTP, read it back and compare it with the local file. The FTP address has the form `ftp://<switch-ip>:5000/sdmc:/switch/manhunt1_nx/manhunt.env`.
3. Wait 3 to 5 s, and do not probe any console port in this step (section 5).
4. Send the NRO from the container, which also receives the log stream:

```sh
MSYS_NO_PATHCONV=1 docker run --rm --name manhunt-nxlink -p 28771:28771 \
  --mount "type=bind,src=$(pwd)/game/build/switch,dst=/nro,readonly" \
  devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282 \
  stdbuf -oL -eL /opt/devkitpro/tools/bin/nxlink -a <switch-ip> -p switch/manhunt1_nx/manhunt.nro -s /nro/manhunt.nro \
  > <log> 2>&1
```

Put `<log>` under `game/build/hw-test/<date>/`, which is gitignored. Do not write the console's or the PC's address into any tracked file.

For the adapter, set `MANHUNT_GPU_BACKEND=1` in `manhunt.env`. The resident target and the time-poll yield are off by default and need `MANHUNT_DEKO_RESIDENT=1` and `RECOMP_TIME_YIELD=1` ([CONFIGURATION.md](CONFIGURATION.md)). Check the `fast path active` line in the log to see what actually ran.

5. Play, then close the game. While the game runs, Sphaira's FTP is closed; when the game is closed, FTP returns and `frames/`, `log.txt` and `wd.txt` can be fetched from the SD folder.
6. Record the results as text in `reports/`, with everything not executed marked `UNVERIFIED`.

## 5. What is known about nxlink here

- **It works from the Windows container with the log stream**, with Sphaira as the receiver. It sent 21.7 MB, compressed to about 31%. It failed in every attempt from the Mac ([DECISIONS.md](DECISIONS.md), 2026-11-02).
- **The `-p` path does not replace the NRO on the card.** A read-back after a successful send still showed the older `fdcf540634ca…` file. So the copy nxlink sends runs directly and the card's `manhunt.nro` is not what ran. Identify a run by the hash of the local file that was sent.
- **Sends can stall, and the cause is not established.** One send stayed at 0 bytes for at least 58 s with FTP still open, so the game never launched. The retry reached an established TCP connection and Sphaira showed nxlink connected, but its log also stayed at 0 bytes for 240 s until the session was interrupted. One log file kept from those attempts holds only `Invalid response`; which attempt wrote it is not recorded. A later fresh send worked, as did all sends after it. The failing sends came shortly after raw TCP probes of port 28280, and the successful ones had a pause of 3 to 5 s and no probes before them. That link is **only a hypothesis**, not a tested cause.
- **Read the log from the stream, not from the card.** `log.txt` is truncated at each start, so a later run destroys the previous log. In run 1, the only `log.txt` kept from this session, it held only the 7 boot and config lines while the stream held the full log. The same `debug_write` code in `main_switch.c` both writes `log.txt` and counts lines for the watchdog, and that counter also stopped at 7 in run 1, so later lines seem not to pass through it under nxlink; `nxlinkStdio` redirecting stdout is a hypothesis for why, not tested.
- **Reading `[MH-WD]` in the stream.** `t=` counts ticks, not seconds: 0.25 s apart for the first 40 and 3 s apart after that. `STALE` means only that the line counter did not change, and for the reason above every tick says so in these runs while flips and draws advance. Take progress from `[MH-DEKO]` and `[RENDER-PROFILE]` instead.
- **The stream can end before the run does.** It is non-blocking and can drop lines, and the log of run 6 covers about 178 s although the user was asked to stay longer. Fetch `wd.txt` from the card afterwards if the length of a run matters.

## 6. Not verified on Windows

All UNVERIFIED: the `-c` clone form in section 2; a port of `scripts/hw-run.sh`; building the host (non-Switch) target; a run with an FTP-uploaded NRO launched from the Sphaira file list; a run with `MANHUNT_LOG_TCP`. Eden was not tried on this host.
