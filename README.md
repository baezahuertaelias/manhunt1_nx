# Manhunt for Nintendo Switch

A native Nintendo Switch port of **Manhunt** (original Xbox version), made by hybrid static recompilation. The Xbox executable's x86 code is translated to C, compiled for the Switch's AArch64 CPU, and linked against a runtime that stands in for the Xbox kernel, GPU and audio hardware.

This repository contains **no game data**. You need your own lawfully obtained copy of the game. Generated code, game files, frames, logs and builds stay in gitignored folders and must never be committed.

## Status

Experimental. It runs on a physical Switch, tested on one console. Current release: **v0.3.0** (see [Releases](https://github.com/marmota94/manhunt1_nx/releases)).

| Area | State |
|---|---|
| Boot to the original menu, controller on the menu | Works on the console |
| Intro movies with subtitles | Works on the console. The subtitles that sometimes turned into pink boxes are fixed (patch 0041): the font failed in 2 of 4 intro runs before the fix and in 0 of 6 after |
| First mission loads, player control in its opening | Works on the console, by user report and scripted runs |
| Frame rate | Intro and menu: 60 FPS on the console (2026-11-20). First mission, scripted console run on 2026-11-18 (CPU 1020, GPU 307, EMC 1331 MHz): median 64.5 FPS over samples above 20,000 vertices per frame, no audio underruns. Details: `reports/VISUAL_AND_PERF_FINDINGS_2026-11-17.md` |
| Lighting | The world's baked (prelit) lighting is drawn, on the OpenGL renderer since 2026-11-19 and on the CPU path since 2026-11-20, so the player and the world are lit consistently with the game's own ambient. Console screenshots only; no comparison with the original Xbox |
| Audio | Intro movie and in-game voices heard on the console; steady 48 kHz. Reverb and other DSP effects are not emulated |
| 16:9 widescreen | Wider 3D view, HUD and movies at 4:3 in the centre, confirmed by console screenshots |
| Returning from a level to the main menu | **Known crash** (reported 2026-11-10, after refused contiguous-memory requests). Not reproduced or fixed yet; `MANHUNT_CONTIG_TRACE=1` logs what is needed to find it |
| Seeing through walls with the camera close to them | Fix in place (unclamped clip depth), not yet proven on the console. `MANHUNT_GL_CLIP_CLAMP=alt` compares both forms in one run |
| Stealth, executions, AI, combat, later levels, saves | UNVERIFIED |
| 30-minute stability, full level completion | UNVERIFIED |

This code merged without the independent reviews the project had planned, by the owner's decision. Emulator results are diagnostic only and say nothing about console speed.

## What has been done

### Recompilation and runtime

- Lifted the Xbox executable to portable C with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp), pinned to one commit by `infra/recompiler/Dockerfile`.
- Maintained 40 patches to that runtime in `game/patches/xboxrecomp/`: the Horizon (Switch) port, memory layout, a raw-batch renderer interface, audio register routing, and GPU completion handling.
- Implemented the Horizon layer in `game/src/`: memory, threads, time, files, the SD layout, a crash handler with a backtrace, a watchdog, and log output to the card, TCP or nxlink.
- Replaced the fence wait the title needs (`game/src/d3d_fence.c`).

### Graphics

- Built a deko3d back end for the menu, with stages for depth, swizzled surfaces, DXT1, X8 and wrap addressing behind `MANHUNT_DEKO_*` switches. It reached about 1 flip per second in the level, because most draws still fell back to the CPU rasteriser.
- Added an OpenGL NV2A renderer (`game/src/nv2a_gl/`), taken from the Burnout 3 Switch port and extended with fixed-function lighting, fog matching and a shader program cache. This took the first mission from about 1 flip per second to the mid 30s and above.
- Applied the title's own gamma ramp at present (`MANHUNT_GAMMA`, on by default).
- Found the cause of the dark scene and the dark player: the fixed-function shader dropped the vertex colour the title routes in as emission, which carries the world's baked lighting. The base colour now follows `SET_COLOR_MATERIAL` (`MANHUNT_GL_FF_COLMAT=0` restores the old form). The earlier `MANHUNT_AMBIENT_LIGHT` lift is off by default.
- Fixed the intro's subtitles turning into pink boxes. The texture decoder borrowed the executor's first texture slot while the pushbuffer executor rewrote it on another thread, so the font sometimes decoded with the movie's settings and was replaced by the magenta "undecodable" stand-in (patch 0041). The first 32 textures that still fall back to magenta are logged.
- Three renderer synchronisation modes (`MANHUNT_GL_SYNC`): `gpu`, the default and the fast one; `strict` (about 3 FPS) and `fenced` (about 7.4 FPS), which keep guest memory up to date and exist for diagnosis.

### Audio

- Reused the MCPX audio chip emulator already in the runtime, so the original game's own DirectSound code runs. Enable it with `MANHUNT_AUDIO_APU=1`.
- Patch 0021 routes the audio code's register accesses to the emulator explicitly, since Horizon has no way to catch them by faults.
- An SDL callback sink plays the result. The default period on Switch is 2048 frames: at 512 the console played only about 85% of real time and sounded choppy.
- Enabling audio let the intro movie play for the first time, which exposed a crash: the movie decoder writes through the Xbox's tiled memory aperture, which Horizon cannot map. `game/src/d3d_tiled_lock.c` returns the equivalent contiguous address instead.

### Widescreen

`MANHUNT_WIDESCREEN=1` (off by default, OpenGL renderer only) gives true 16:9 Hor+ with the same vertical field of view:

- The game function that sets the RenderWare camera's view window (`0x001E80A0`, identified by its code shape) is wrapped, and the title's cameras are widened by 4/3.
- The 640x480 image is shown at 16:9, which restores square pixels.
- HUD, menus, text and movies are squeezed back to 4:3 in the centre. They are recognised by four shader program hashes found by logging every 2D draw in the level.
- Fades and film grain stay full width.

### Input, tools and tests

- Pad latching so short taps reach the title, Switch-to-Xbox button mapping, and Rockstar's own developer menu on L3+R3 (`MANHUNT_DEVMENU=1`).
- An on-screen frame counter (`MANHUNT_FPS_OVERLAY=1`) and a frame-rate log (`RECOMP_FPS_LOG=1`).
- Focused tests under `tests/` for the Horizon layer, renderer, audio, widescreen, SD layout and input. Several include negative controls that prove the test fails when the code is broken.

## Build

You need Docker. On Windows use Git Bash, and keep files in LF line endings:

```sh
git config core.autocrlf false
git config core.eol lf
```

Put your game files in `assets/manhunt1_xbe/`, with `DEFAULT.xbe`, `audio/` and `levels/` at the top of that folder. Then:

```sh
docker build -t manhunt-recompiler:xboxrecomp-f0f8a46 infra/recompiler
MSYS_NO_PATHCONV=1 scripts/game-build.sh switch
```

The output is `game/build/switch/manhunt.nro`. Leave out `MSYS_NO_PATHCONV=1` on Linux or macOS. A clean run of this script against this exact commit is UNVERIFIED: the last console builds were incremental.

The toolchain image is pinned: `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`.

## Install and run

You need a homebrew-capable Switch and your own copy of Manhunt for the original Xbox (a disc image or the extracted files).

**The easy way: the installer.** Open the [Manhunt Switch SD installer](https://marmota94.github.io/manhunt1_nx_installer/) in a current Chromium browser (Chrome or Edge). Choose your disc image or extracted folder and let it build `manhunt1-nx-v<version>-sd.zip`. It checks every game file against the port's manifest, and the game files never leave your computer. Delete any old `switch/manhunt1_nx/game` on the SD card, then extract the zip at the card root.

**By hand.** Download `manhunt1-nx-v<version>.zip` from [Releases](https://github.com/marmota94/manhunt1_nx/releases) and extract it at the SD card root. Copy your game files into `switch/manhunt1_nx/game/` (`DEFAULT.xbe`, `audio/`, `levels/`). `asset-manifest.sha256` in the zip lists the SHA-256, size and path of every file the port expects.

**Starting it.** The game needs **title-takeover mode** to get enough memory: hold **R** while opening any installed game. The homebrew menu (or Sphaira) opens instead; pick `manhunt.nro`. Started from the album applet it does not get enough memory. Hold **ZL + ZR + Plus + Minus** for 1.5 seconds to quit.

The SD card layout:

```text
/switch/manhunt1_nx/manhunt.nro
/switch/manhunt1_nx/manhunt.env
/switch/manhunt1_nx/game/DEFAULT.xbe
/switch/manhunt1_nx/game/audio/
/switch/manhunt1_nx/game/levels/
```

`save/` and `frames/` are kept in `/switch/manhunt1_nx/` as well.

The release's `manhunt.env`, good for playing:

```text
RECOMP_SPAN_4G=1
MANHUNT_GPU_BACKEND=2
MANHUNT_AUDIO_APU=1
MANHUNT_WIDESCREEN=1
MANHUNT_LANGUAGE=spanish
MANHUNT_AMBIENT_LIGHT=-1
```

Leave out `RECOMP_GL_DUMP` when playing: it saves a frame to the card and holds the game each time (two dumps per 60 frames hold it at about 15 FPS).

To test without copying the NRO, send it with nxlink from the toolchain image. nxlink runs the copy it sends and does **not** replace the NRO on the card:

```sh
MSYS_NO_PATHCONV=1 docker run --rm --name manhunt-nxlink -p 28771:28771 \
  --mount "type=bind,src=$(pwd -W)/game/build/switch,dst=/nro,readonly" \
  devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282 \
  stdbuf -oL -eL /opt/devkitpro/tools/bin/nxlink -a <switch-ip> -p switch/manhunt1_nx/manhunt.nro -s /nro/manhunt.nro
```

## Main settings

Set these in `manhunt.env`, one per line.

| Key | Default | Effect |
|---|---|---|
| `RECOMP_SPAN_4G` | off | Needed on the console; keeps the guest's register apertures out of the libnx heap |
| `MANHUNT_GPU_BACKEND` | CPU renderer | `2` is the OpenGL renderer, `1` the deko3d adapter |
| `MANHUNT_GL_SYNC` | `gpu` | `gpu`, `strict` or `fenced` |
| `MANHUNT_AUDIO_APU` | off | `1` starts the audio emulator |
| `MANHUNT_AUDIO_FRAMES` | 2048 on Switch | SDL period, 256 to 2048, powers of two |
| `MANHUNT_WIDESCREEN` | off | `1` for 16:9; needs `MANHUNT_GPU_BACKEND=2` |
| `MANHUNT_LANGUAGE` | `english` | Console language the title reads: `english`, `japanese`, `german`, `french`, `spanish`, `italian`, or 1 to 6. Whether Manhunt switches its text and voices by this setting is UNVERIFIED |
| `MANHUNT_GAMMA` | on | Applies the title's gamma ramp at present |
| `MANHUNT_AMBIENT_LIGHT` | `-1` | Scene ambient for lit fixed-function geometry: `-1` keeps the title's, `0` to `1` replaces it with that grey (characters are not affected) |
| `MANHUNT_DEVMENU` | off | `1` opens Rockstar's developer menu with L3+R3 |
| `MANHUNT_FPS_OVERLAY` | off | `1` draws the frame rate on screen |
| `MANHUNT_QUIT_COMBO` | on | Hold ZL + ZR + Plus + Minus for 1.5 s to leave the game; `0` turns it off. See below |
| `MANHUNT_QUIT_HOLD_MS` | 1500 | How long to hold the quit combo (500 or more) |
| `MANHUNT_QUIT_MODE` | `restart` | How the quit combo ends the game: `restart`, `return` or `process` |
| `MANHUNT_LOG_FILE` | off | Path of a log file on the card |
| `MANHUNT_SD_BASE` | `/switch/manhunt1_nx` | Application folder; `game/` is appended for game data |

Diagnostic settings, for test runs only:

| Key | Default | Effect |
|---|---|---|
| `RECOMP_GL_DUMP` | off | `<path prefix>,<N>` saves every Nth frame as a BMP |
| `MANHUNT_GL_CLIP_CLAMP` | `0` | `1` clamps clip depth per vertex (the old form); `alt` alternates linear and clamped every frame, and with `RECOMP_GL_DUMP` saves each pair |
| `MANHUNT_GL_FF_COLMAT` | on | `0` restores the old fixed-function base colour (scene ambient alone) |
| `MANHUNT_CONTIG_TRACE` | off | `1` logs contiguous-memory blocks of 64 KB or more with the guest caller, and an arena summary every 10 s and at each refused request |
| `MANHUNT_PAD_SCRIPT` | off | Scripted pad input, `<ms>:<button>[+<button>][:<hold_ms>],...` (`game/src/pad_script.c`) |
| `MANHUNT_AUTO_EXIT_SECS` | off | Ends the run after N seconds |

**Quitting.** Hold ZL + ZR + Plus + Minus for 1.5 seconds. The aim is to leave the game and land back in Sphaira instead of ending the whole takeover session, as HOME then Close Software does, which also takes Sphaira's FTP and netloader away. `MANHUNT_QUIT_MODE` picks the method: `restart` (default) restarts the program, which under the homebrew loader's title takeover reloads the menu; `return` exits the way a normal homebrew app does; `process` ends the process and returns to the Home menu. Which of them lands in Sphaira is UNVERIFIED on the console. Each step is logged as `[MH-EXIT]`.

**In `gpu` mode** rendered pixels are not copied back to guest memory, and CPU writes into a render target are not picked up. No problem from this has been seen in Manhunt, but whether the title ever reads a rendered surface back is UNVERIFIED. If you suspect this, compare with `MANHUNT_GL_SYNC=strict`.

## Texture tools

Two offline helpers for working with the game's art. They run on your own game files and write only where you point them; the output is game art and must never be committed (write it under `game/build/`, which is ignored).

```sh
python3 tools/txd_extract.py assets/manhunt1_xbe game/build/textures
python3 tools/upscale_textures.py game/build/textures game/build/textures-4x --scale 4 --only menu/
```

- `tools/txd_extract.py` reads the Xbox `.TXD` files (5,564 of the game's 5,612 textures: DXT1, DXT3, DXT5, 8-bit paletted and 32-bit, unswizzled) and writes PNGs plus a `manifest.json` with each texture's size, format and the SHA-256 of its stored bytes. Standard library only; about 0.1 s per texture, so a full run takes several minutes. The 48 older-layout textures are reported and skipped.
- `tools/upscale_textures.py` runs a local upscaler you install (default `realesrgan-ncnn-vulkan`) over those PNGs, keeping the layout and manifest, and can be stopped and restarted. Nothing is uploaded. The upscaler step itself is UNVERIFIED here: it was tested against a stub, not a real model.

The renderer does not load upscaled textures yet. Doing that on the console is limited by memory: a 4x texture costs 16x as much.

## Repository layout

| Path | Contents |
|---|---|
| `game/src/` | The port: Horizon layer, renderers, audio glue, widescreen, input |
| `game/src/nv2a_gl/` | OpenGL NV2A renderer |
| `game/patches/xboxrecomp/` | Patches applied to the pinned runtime |
| `infra/` | Container definitions |
| `scripts/` | Build, emulator and console helpers |
| `tools/` | Offline texture extraction and upscaling helpers |
| `tests/` | Focused tests and proofs |
| `analysis/` | Executable analysis helpers |
| `reports/ASSET_MANIFEST.sha256` | Hashes of the expected input files, used by `scripts/switch-boot-proof.sh` |

The project's earlier documentation (run reports, reviews, task tracking) was removed on 2026-11-09 at the owner's request. It remains in the Git history before the merge of pull request #11.

## Credits

This port stands on work done for **Burnout 3: Takedown**, the original Xbox game.

| Part | Project | Author | Licence |
|---|---|---|---|
| OpenGL NV2A renderer (`game/src/nv2a_gl/`) and the raw-batch path in patch 0019 | [antoxa2584x/burnout3-sw](https://github.com/antoxa2584x/burnout3-sw), commit `92b2791945b04d6909b655f88d5bf351bf9127f9`, the Nintendo Switch port of Burnout 3 | antoxa2584x and contributors | MIT |
| Recompiler, runtime and kernel layer | [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp), commit `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`, the toolkit extracted from [sp00nznet/burnout3](https://github.com/sp00nznet/burnout3) | sp00nz and the [xboxrecomp contributors](https://github.com/sp00nznet/xboxrecomp/blob/f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b/CONTRIBUTORS.md) | MIT, except the LGPL files below |
| MCPX audio chip emulator | [xemu](https://github.com/xemu-project/xemu), extracted into xboxrecomp `src/apu/` | espes, Jannik Vogel, Matt Borgerson | LGPL-2.1-or-later |

Local changes to the renderer: the `nv2a_backend.h` include and `Nv2aBackend` initializers, fog matching the CPU path, `RECOMP_GL_NO_CULL`, the program cache path, widescreen handling and the `MANHUNT_GL_SYNC` modes. `src/recomp_manual.c` (apart from its Manhunt section) and the boot order in `src/main.c` come from the xboxrecomp new-game template.

xboxrecomp also credits Fabian Giesen for its texture unswizzling technique, and xemu, Cxbx-Reloaded and xboxdevwiki as references for hardware behaviour. Manhunt is a trademark of its owners; this project is not affiliated with them.

## Licences

### Audio emulator: LGPL-2.1-or-later

The audio emulator compiled into the Switch build comes from xboxrecomp's `src/apu/`, extracted from xemu. It is **not** vendored here: `scripts/game-build.sh` fetches the pinned source. These files are licensed under the GNU Lesser General Public License, version 2.1 or (at your option) any later version. The full text is in [LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).

| File | Copyright |
|---|---|
| `src/apu/apu_core.c` | (c) 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_dsp.c` | (c) 2012 espes; 2019-2025 Matt Borgerson |
| `src/apu/apu_vp.c` | (c) 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_regs.h` | (c) 2012 espes; 2018-2019 Jannik Vogel; 2019-2021 Matt Borgerson |
| `src/apu/apu_state.h` | (c) 2012 espes; 2018-2019 Jannik Vogel; 2019-2025 Matt Borgerson |
| `src/apu/apu_debug.h` | (c) 2020-2021 Matt Borgerson |
| `src/apu/fpconv.h` | (c) 2020-2025 Matt Borgerson |
| `src/apu/apu_shim.h` | xboxrecomp's own file, released under the same licence |
| `src/apu/apu.h` | no copyright line in the header |

> This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
>
> This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

**If you distribute a Switch NRO:** the build links these objects statically, so the LGPL applies to that binary. You must keep these notices, make the LGPL source available, and let recipients relink against a modified version of the library, for example by supplying the object files or the complete source and build scripts.

**The project's releases** do this. Every release publishes `manhunt1-nx-v<version>-lgpl-source.zip` next to `manhunt1-nx-v<version>.zip`, with `src/apu/` exactly as compiled into that NRO. The release zip carries `LICENSES/SOURCE-OFFER.txt`, which says where it is. The rest of the program is this repository at the release tag, and `scripts/game-build.sh switch` rebuilds and relinks it from your own copy of the game. `scripts/make-release.py` writes both zips and refuses an NRO that carries the cover-art icon (`game/build/icon256.jpg`).

The host build also links LGPL-2.1 NV2A files from the same runtime: `src/nv2a/nv2a_regs.h`, `nv2a_core.c` and `nv2a_state.h`.

### MIT-licensed parts

The vendored renderer has its licence in [game/src/nv2a_gl/LICENSE](game/src/nv2a_gl/LICENSE). `game/src/recomp_manual.c`, the adapted parts of `game/src/main.c`, and `game/patches/xboxrecomp/*.patch` derive from xboxrecomp, under the same terms. The full text is in [LICENSES/MIT-xboxrecomp.txt](LICENSES/MIT-xboxrecomp.txt).

```text
MIT License

Copyright (c) 2026 sp00nz
```

## Legal

Use only game and system files you obtained lawfully. Do not commit or redistribute game assets, BIOS, firmware, keys, SDK components, lifted code, or anything derived from them. Dumped frames, audio captures and logs from your own runs count as derived data.
