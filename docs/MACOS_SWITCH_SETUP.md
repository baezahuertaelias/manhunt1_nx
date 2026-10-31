# macOS Sonoma + physical Switch: setup and handoff

Purpose: continue the port on a **Mac running macOS Sonoma** with a **modded (custom-firmware) Nintendo Switch**, starting from a fresh `git clone`. This is the handoff document for the next session on that host.

**Status: UNVERIFIED.** It was written on the Linux reference host (Debian 13, x86-64; [HOST_SETUP.md](HOST_SETUP.md)) with no Mac and no Switch. Everything about macOS and about physical hardware below is a plan, not an executed result, unless a line says it was run. Hardware gates in [BETA_STATUS.md](../BETA_STATUS.md) stay UNVERIFIED until a run on the console is captured and reviewed.

## 1. Why this host, and which path

Milestone 4 (first-level load) is recorded at a minimal bar in the **Eden emulator only** ([reports/LEVEL_LOAD.md](../reports/LEVEL_LOAD.md), "Milestone 4 acceptance"). In Eden, in-level execution runs at about 0.1 frames/s because the GPU is emulated in software, so these questions cannot be answered there:

- Is the missing player control a slow or unfinished opening cutscene, or a real control defect? (task HW-SWITCH-001, milestone 5)
- Is the dark, streaked picture an emulator artefact or a defect in our native renderer?
- Real frame rate, load times, memory use, heat.

| Path | What you do on the Mac | When |
|---|---|---|
| **A. Hardware-only (start here)** | No build. Copy the **prebuilt NRO**, your game files and a small config to the SD card, run on the Switch, bring back frames and logs. | First. Needs only a Mac, an SD card reader and the Switch. |
| **B. Full development host** | Clone the repo, install Docker Desktop and GNU tools, rebuild the lift, the host build and the NRO on the Mac. | Only if you want to change code on the Mac. Heavier and less certain (section 5). |

Not needed on the Mac for path A: Docker, Ghidra, Eden, Xvfb, gdb.

## 2. What Git does NOT carry (transfer these privately)

The repository is **public** (AGENTS.md). It contains no game data. Everything in this table must be copied separately, never committed, never uploaded to a release or issue.

| Item | Where it is on the Linux host | Size | Check |
|---|---|---|---|
| Your lawful game files | `assets/manhunt1_xbe/` (`DEFAULT.xbe`, `audio/`, `levels/`) | 2.4 GiB, 836 files, largest 535 MB | `DEFAULT.xbe` SHA-256 `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde`; whole set against `reports/ASSET_MANIFEST.sha256` (tracked) |
| Prebuilt NRO | `game/build/switch/manhunt.nro` | 21,674,173 bytes | SHA-256 `7b3c6f90f2be7e77f24a32eb52d6c17b7fed0cb80eaa787c62628af54ab600c6`, built from commit `615e640` |
| Docker images (path B only) | rebuilt from `infra/`, see section 5 | several GB | see section 5 |

The NRO is a build product that contains code derived from the game. Treat it like game data: do not commit it and do not share it.

Do **not** copy `game/build/` wholesale: it is 48 GB, mostly old debug dumps and caches.

Transfer from the Linux host (any of these; replace the placeholders):

```sh
# on the Mac, inside the clone
mkdir -p assets game/build/switch
rsync -a --progress <user>@<linux-host>:/Downloads/repos/manhunt1_nx/assets/manhunt1_xbe/ assets/manhunt1_xbe/
scp <user>@<linux-host>:/Downloads/repos/manhunt1_nx/game/build/switch/manhunt.nro game/build/switch/
shasum -a 256 assets/manhunt1_xbe/DEFAULT.xbe game/build/switch/manhunt.nro
```

Compare the two hashes with the table above before going further. A USB drive works equally well. macOS ships an old `rsync` (2.6.9); the flags above work with it.

Facts checked on the Linux host that matter for the Mac and the SD card: no game file is larger than 4 GiB (FAT32 is fine), there are no non-ASCII file names, no two files differ only by case (so a case-insensitive APFS volume or a FAT/exFAT card is safe for the game data), the longest path is 77 characters, and the tracked repository has no case-colliding names.

## 3. Path A: run on the Switch without building anything

### 3.1 Console prerequisites

- A Switch running custom firmware with the homebrew menu (hbmenu), so a `.nro` can be launched. Firmware and Atmosphère versions are **not recorded anywhere in this project**: note them when you start (Settings > System, and the Atmosphère version shown at boot) so they can be added to the evidence.
- A microSD card formatted FAT32 or exFAT with at least 4 GiB free (the game data is 2.4 GiB, frames add about 1 MB each).
- Keeping the console offline, or using an emuMMC, to avoid risk to the online account is your decision; the project takes no position and needs no network on the console.
- Launch homebrew in **title-takeover mode** (open an installed game while holding R, then start the NRO from hbmenu), not from the Album applet. Applet mode gives homebrew far less memory. The port's real memory need on hardware has never been measured.

### 3.2 Prepare the SD card on the Mac

Mount the card (called `SDCARD` below), then:

```sh
SD=/Volumes/SDCARD
mkdir -p "$SD/switch/manhunt" "$SD/manhunt"
cp game/build/switch/manhunt.nro "$SD/switch/manhunt/manhunt.nro"
cp -R assets/manhunt1_xbe/. "$SD/manhunt/"          # becomes sdmc:/manhunt/DEFAULT.xbe etc.
cp docs/hardware/manhunt.env.example "$SD/manhunt.env"   # see 3.3 (created by this branch)
dot_clean -m "$SD"                                   # remove macOS ._* sidecar files
diskutil eject "$SD"
```

The runtime reads game data from `sdmc:/manhunt/` (`DEFAULT.xbe` plus the disc folders, `game/src/main_switch.c`). Saves go to `sdmc:/manhunt-save/` and frame dumps to `sdmc:/manhunt-frames/`. The Linux `switch-boot-proof.sh` stages data the same way for Eden.

### 3.3 Config file `sdmc:/manhunt.env`

`docs/hardware/manhunt.env.example` is a copy-ready file. Its keys, all of which were used in the Eden runs:

```
MANHUNT_DUMP_SECS=5
MANHUNT_GPU_BACKEND=1
RECOMP_PB_REPORT_MS=10000
RECOMP_PEEK=0x4004A0
RECOMP_PEEK_CHAIN=0x4004A0,0x80,4,0x40
```

- **Do not set `MANHUNT_PAD_SCRIPT`.** Without it the runtime reads the real controller (`game/src/pad_switch.c`).
- `MANHUNT_GPU_BACKEND=1` is the native deko3d path used in every Eden run, so keep it for comparability. If the console shows corruption or stalls, repeat once with that line removed (the CPU software path) and report both.
- `RECOMP_PEEK*` print the original player object and position; on hardware they are visible only once text logging works (section 4).

### 3.4 Real-controller mapping

Buttons map by **position**, matching an Xbox pad (`game/src/pad_switch.c`):

| Game (Xbox) | Switch |
|---|---|
| A, the confirm button | **B** (bottom) |
| B, cancel / "Continue without save" | **A** (right) |
| X | Y (left) |
| Y | X (top) |
| Black / White | R / L |
| Left / right trigger | ZL / ZR |
| Start / Back | + / - |
| Left stick, right stick | left stick, right stick |

The scripted Eden runs pressed the game's "A" three times and then "B"; on the real pad that is **B, B, B, then A**.

### 3.5 Test procedure and what to record

Write down the wall-clock time of each step (a phone stopwatch is enough).

1. **Boot.** Launch the NRO. Record whether the original menu (MANHUNT logo, PLAY, SELECT SCENE, ...) appears and how long it took.
2. **Menu.** Navigate with the d-pad and confirm (Switch **B**). Brightness screen > difficulty > "SAVE GAME" screen: press **A** ("Continue without save").
3. **Level load.** Record how long "BORN AGAIN — LOADING..." and the static take, and whether the console stalls.
4. **In-level.** Record whether the camcorder overlay (REC and a running TCR timecode) appears, whether the picture is dark or streaked, and **whether the timecode runs in real time**.
5. **Control test.** Wait about 2 minutes in the scene first (a scripted opening may need it), then hold the left stick forward for 10 s, turn with the right stick for 10 s, press + (Start) once, press B once. Record every visible response, or none.
6. **Pacing and stability.** Rough frame rate by eye, any freeze, crash or black screen, and whether the console gets hot (handheld or docked).
7. **Fallback.** If step 3 or 4 fails, remove `MANHUNT_GPU_BACKEND=1` from the env file, repeat steps 1–4, and record the difference.

### 3.6 What to bring back

Copy these from the SD card into `game/build/hw-test/<date>/` in the Mac clone (that path is gitignored):

- `sdmc:/manhunt-frames/` (the `fb*.bmp` frame dumps, every 5 s),
- `sdmc:/manhunt-log.txt` once text logging exists (section 4),
- Atmosphère crash reports, if the application crashed (typically `sdmc:/atmosphere/crash_reports/`),
- your step-by-step notes and the firmware/Atmosphère versions.

Hardware results enter the repository only as reviewed text reports (reports/ and BETA_STATUS.md). Frames, logs, saves and the NRO stay private.

## 4. Known gap: no text log on real hardware

On the Switch the runtime sends all `printf`/`stderr` output only to `svcOutputDebugString` (`game/src/main_switch.c`, `debug_write`). Eden captures that channel; a real console does not, so a hardware run currently returns **frame dumps only**. The player-state peeks and every `[D3D]`, `[MH-INPUT]`, `[ICALL]` line would be lost.

Tracked as **HW-LOG-001**: an optional `MANHUNT_LOG_FILE` setting that also writes the log to the SD card, default off. If this branch's `NEXT_ACTION.md` and `docs/CONFIGURATION.md` mention `MANHUNT_LOG_FILE`, it has been implemented and tested in Eden. If they do not, the hardware run can still use the frames, and the first engineering task on the Mac is HW-LOG-001.

## 5. Path B: full development host on macOS (UNVERIFIED)

Do this only if you will change code on the Mac. The existing procedure is [SETUP.md](SETUP.md); this section lists what differs on macOS.

### 5.1 Hardware and software

| Need | Details |
|---|---|
| Which Mac | **Not recorded.** Apple silicon and Intel behave differently for the images below. Record the chip, RAM and free disk first. |
| Disk | 30 GB free minimum; the Linux reference build tree reached 48 GB, almost all of it old dumps. |
| RAM | `scripts/game-build.sh` gives its containers `--memory 12g` (switch build) and 8 GiB (host build). Set Docker Desktop's memory limit to at least 12 GiB; that is tight on a 16 GB Mac. |
| Docker Desktop | Any recent 4.x with the VirtioFS file sharing. Recorded versions: none. |
| Homebrew | `brew install coreutils gnu-sed bash git python@3.12` |
| Xcode command line tools | `xcode-select --install` (git, make). |

### 5.2 GNU tools and bash

The scripts assume GNU userland and bash. Counts on this branch: `sha256sum` in 15 scripts, `nproc` in 6, `realpath -e` in 5, `stat -c` in 3, `timeout` in 4, `sed -i` in 1, and `declare -A` (bash 4+) in `scripts/toolchain-proof.sh`. macOS provides none of the GNU forms and ships bash 3.2. Put the GNU tools first in `PATH` for the shell you use for this repo only:

```sh
export PATH="$(brew --prefix)/opt/coreutils/libexec/gnubin:$(brew --prefix)/opt/gnu-sed/libexec/gnubin:$(brew --prefix)/bin:$PATH"
hash -r; bash --version | head -1     # must say 5.x
command -v nproc sha256sum timeout realpath
```

Do not put this in your global profile: it changes `sed`, `stat` and others system-wide.

### 5.3 Images

- Docker image digests pinned in the repo: `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282` and base `python:3.12-slim-bookworm@sha256:6a0e06f5ba0b373d051aabd3fffb9b873f27f9a2a3a14bcbe37dee6f0e3bbc70` (`infra/recompiler/Dockerfile`).
- **Whether those digests provide an arm64 variant is unknown.** On an Apple-silicon Mac, force `--platform linux/amd64` (runs under emulation: correct but slow) or find an arm64 build of both and record it.
- Build as in SETUP.md: `docker build -t manhunt-recompiler:xboxrecomp-f0f8a46 infra/recompiler` and `scripts/game-build.sh switch`. The first lift takes about 5 minutes on the Linux host; expect much longer under emulation.
- The `--user "$(id -u):$(id -g)"` bind-mount arguments assume Linux ownership rules; on Docker Desktop for Mac file ownership and permissions may differ. If a build fails with permission errors, record it instead of loosening the flags.

### 5.4 What will not work on the Mac

- **Eden runner:** `dd-eden:local` is a host-local image that cannot be built from this repo (EMU-IMAGE-001). With a real Switch it is not needed. Keep emulator investigations on the Linux host.
- **Debugger work:** the allocator investigation used gdb hardware watchpoints on native x86-64 Linux. Do not expect that to work under emulated containers on a Mac. Keep gdb work on the Linux host.

### 5.5 Optional: case-sensitive volume

Not required for the game data (no case collisions, section 2). If you want exact parity with the Linux host anyway, create a case-sensitive APFS volume or sparse image (`hdiutil create -type SPARSE -fs "Case-sensitive APFS" ...`) and clone the repo into it. Bind mounts from it into containers are untested.

## 6. First session on the Mac

Clone and check out this branch, restore the private files (section 2), then start Claude Code in the clone with a prompt like:

> Read AGENTS.md, NEXT_ACTION.md, docs/MACOS_SWITCH_SETUP.md and reports/LEVEL_LOAD.md. This is a new host (macOS Sonoma) with a physical modded Switch. First, report: Mac chip and RAM, Docker and Homebrew state, the hashes from section 2 of the setup doc, and which of path A/B I am doing. Do not commit game data, the NRO, frames or logs. Do not claim any hardware result without captured evidence in game/build/hw-test/.

```sh
git clone https://github.com/baezahuertaelias/manhunt1_nx.git
cd manhunt1_nx
git switch macos-switch-handoff      # or main once the branch has been merged
git config user.name  "<your name>"
git config user.email "<your email>"
```

Read in this order: `AGENTS.md` (the rules), `NEXT_ACTION.md` (the single next action), this file, `reports/LEVEL_LOAD.md` (what is known and what is not), `BLOCKERS.md`.

## 7. Open items before or during the first run

| Open item | Why it matters |
|---|---|
| HW-LOG-001 (text log to SD) | Without it a hardware run returns frames only (section 4). |
| Mac chip, RAM, disk, macOS minor version | Decides path B feasibility and the image platform. |
| Switch firmware and Atmosphère versions | Needed for any hardware evidence. |
| Applet vs title-takeover memory | The port's memory use on hardware is unmeasured. |
| ICALL-NULL-001 | A null indirect call is skipped at boot; its cause is unexplained. |
| Player control (M5) | The scene is static in Eden at 0.1 fps; hardware decides between cutscene, slowness and a defect. |
| Audio | Not implemented; DirectSound reports no driver. Expect silence. |

## 8. Rules that still apply on the new host

- Use only lawfully obtained game files. Never commit or upload game files, the NRO, frames, logs, saves or keys. The repository is public.
- Never push directly to `main`; use a branch and a pull request (AGENTS.md).
- Mark anything not executed `UNVERIFIED`. Emulator frame rates are diagnostic only and are not Switch hardware frame rates.
- A configuration change updates `docs/CONFIGURATION.md` and `docs/CHANGELOG_CONFIG.md` in the same change and is reviewed independently.

## 9. Checklist

- [ ] Mac chip, RAM, free disk and macOS version recorded
- [ ] Repository cloned, branch checked out, git identity set
- [ ] `assets/manhunt1_xbe/` copied; `DEFAULT.xbe` hash matches; manifest verified
- [ ] `manhunt.nro` copied; hash matches `7b3c6f90…600c6`
- [ ] SD card prepared (section 3.2) and `dot_clean` run
- [ ] Switch firmware and Atmosphère versions noted
- [ ] Launched in title-takeover mode
- [ ] Test steps 1–7 done, times and observations written down
- [ ] Frames (and logs, if available) copied to `game/build/hw-test/<date>/`
- [ ] Results written into `reports/` as text, with every unrun item marked UNVERIFIED
