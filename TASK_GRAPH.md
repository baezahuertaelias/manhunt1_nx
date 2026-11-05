# Task graph

Machine-readable ledger: [tasks/tasks.jsonl](tasks/tasks.jsonl).

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| PREFLIGHT-001 | done | lead | — | PREFLIGHT.md records PASS/FAIL/UNVERIFIED for every checklist item |
| NRO-PROOF-002 | done | runtime | PREFLIGHT-001 | Revised proof rebuilds twice in pinned image with identical SHA-256 |
| GHIDRA-VER-001 | done | re | — | Analysis image's Ghidra version matches the loader; XBE import repeats |
| RECOMP-EVAL-001 | done | re | PREFLIGHT-001 | Candidate report; one function recompiled and executed with matching output |
| EMU-RUNNER-001 | done | test | NRO-PROOF-002 | Eden loads an NRO headlessly; PASS/FAIL/runner-failure separated |
| DEKO3D-PROOF-001 | done | runtime | EMU-RUNNER-001 | deko3d triangle verified by pixel readback |
| FEASIBILITY-001 | done | lead | RECOMP-EVAL-001 | `reports/FEASIBILITY.md`: boot path, lift, Horizon capabilities, bring-up evidence |
| BOOT-HOST-001 | done | runtime | FEASIBILITY-001 | Recompiled game reaches the original main menu on the Linux host build |
| BOOT-SWITCH-001 | done | runtime | BOOT-HOST-001 | Switch NRO reaches the main menu in Eden (`scripts/switch-boot-proof.sh`) |
| RENDER-001 | in_progress | render_analysis | BOOT-SWITCH-001 | Frame-time breakdown + deko3d back-end plan (`reports/RENDER_PLAN.md`) |
| AUDIO-HLE-001 | ready | runtime | BOOT-SWITCH-001 | Native DirectSound plays the front-end music on host and Switch |
| INPUT-NAV-001 | done | test | BOOT-SWITCH-001 | Scripted pad moves the main-menu selection on the Switch build |
| HZ-PORT-002 | ready | runtime | BOOT-SWITCH-001 | Horizon memory backend and libc shims match POSIX semantics (review findings) |
| CODEX-AGENTS-001 | ready | lead | — | Codex 0.164.0 config syntax and available models verified; `.codex/agents/*.toml` for the WORKFLOW roles, documented in CONFIGURATION.md |
| NEG-CONTROLS-001 | ready | test | RECOMP-EVAL-001 | Negative controls scripted and expected to fail inside `scripts/toolchain-proof.sh` |
| EMU-IMAGE-001 | in_progress | provenance_audit | — | Eden/Ryujinx image built from pinned source in this repo passes `scripts/toolchain-proof.sh` |
| GPU-PROOF-001 | done | runner_analysis | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | done | lead | user approval (given 2026-10-28) | Remote created (public on user instruction); `main` protected (no force-push/deletion, PRs) |
| LEVEL-INPUT-TRACE-001 | in_progress | lead | LEVEL-MOVE-001 | Trace guest XInput axes, downstream object/control gate; original-address evidence and independent review |
| LEVEL-MOVE-001 | in_progress | lead | LEVEL-LOAD-001 | Sample long stick holds, inspect before/after frames, preserve runner/port distinction and independent evidence review |
| LEVEL-LOAD-001 | in_progress | lead | INPUT-NAV-001, RENDER-MENU-BACKEND-001, HZ-TIME-001 | Milestone 4: first level loads in Eden, environment shown, player spawned |
| X87-CW-001 | in_progress | re | BOOT-HOST-001 | Game's x87 control word(s) known: sites + runtime value; double-based x87 decision revisited |
| MEM-MIRROR-002 | pending | runtime | LEVEL-LOAD-001 | Mirror/tiled-aperture traps silent through level load, or an address-wrapping design |
| LGPL-LINK-001 | ready | lead | — | Link maps show whether LGPL objects are linked; THIRD_PARTY.md updated (before any distribution) |

### Requested features (user, 2026-10-28; P3, after Beta 1 per ROADMAP priorities)

Reference analysis: [reports/MODDING_REFERENCES.md](reports/MODDING_REFERENCES.md). All opt-in, default off, original behaviour unchanged when off.

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| WIDE-001 | ready | re | BOOT-SWITCH-001 | `reports/WIDESCREEN_PLAN.md`: back buffer, video flags, RW camera/projection, culling, 2D and FMV paths with guest addresses |
| WIDE-002 | pending | runtime | WIDE-001, RENDER-001, LEVEL-LOAD-001 | `MANHUNT_WIDESCREEN=1`: real 16:9 Hor+ (square pixels, same vertical FOV, no stretch); simulation unchanged |
| WIDE-003 | pending | runtime | WIDE-002 | HUD/menus/text not stretched, FMV at own aspect, no edge popping |
| DEVMENU-001 | ready | re | BOOT-HOST-001 | `reports/DEVMENU.md`: Rockstar dev menu located (addresses, enable path) and shown on host build, or proven stripped |
| DEVMENU-002 | pending | runtime | DEVMENU-001, INPUT-NAV-001 | `MANHUNT_DEVMENU=1` + pad combo opens it on Switch (Eden); off = identical behaviour |
| MODMENU-001 | pending | runtime | DEVMENU-001, LEVEL-LOAD-001 | Native overlay menu, pad combo, coordinates display; only features the dev menu lacks |
| MODMENU-002 | pending | runtime | MODMENU-001 | Infinite health, god mode, fall damage toggle, waypoint save/load |
| MODMENU-003 | pending | runtime | MODMENU-001 | Weapon spawner (weapons loaded in the level, max ammo) |
| MODMENU-004 | pending | runtime | MODMENU-001 | Cheats + cheats flag, weather presets, stars/moon, console |
| MHP-EVAL-001 | pending | re | LEVEL-LOAD-001 | Each MHP feature confirmed or ruled out on Xbox; tasks only for confirmed ones; no MHP code copied |

### Blocker audit (2026-10-28)

| ID | State | Owner | Depends on | Acceptance |
|---|---|---|---|---|
| BLOCKER-AUDIT-001 | in_progress | lead | — | Reconcile blockers against executed evidence and preserve outstanding gates |
| EMU-GPU-PRESENT-001 | done | runner_analysis | GPU-PROOF-001 | Hardware Vulkan Eden synthetic proof; runner/port failures separated |
| RENDER-GPU-POC-001 | done | runtime_analysis | DEKO3D-PROOF-001 | Synthetic Nv2aVertex GPU coordinate/colour/perspective readback, independent review |
| RENDER-GPU-STATE-001 | done | runtime_analysis | RENDER-GPU-POC-001 | Synthetic texture/address/alpha/blend/write-mask readback with independent reference |
| RENDER-PALETTE-001 | done | lead | RENDER-GPU-POC-001 | Carry/test bounded P8 palette metadata and helper restore; independent review |
| RENDER-MENU-STATE-001 | done | runtime_analysis | INPUT-NAV-001 | Complete opt-in menu-frame state capture and support/gap table, independent review |
| RENDER-MENU-PROOF-001 | done | lead | RENDER-MENU-STATE-001, RENDER-GPU-STATE-001, RENDER-PALETTE-001 | Verify observed menu Q/filter/alpha/blend/palette/format requirements against references |
| RENDER-Q-001 | done | q_trace | RENDER-MENU-STATE-001 | Aggregate eligible accepted PROJECT2D textureQ with focused tests and full-menu capture |
| RENDER-DECODE-001 | done | menu_decode | RENDER-MENU-STATE-001, RENDER-PALETTE-001 | Bounded short P8 and generic ARGB8/DXT23 decoder fixtures, build and independent review |
| RENDER-COHERENCE-001 | done | lead | RENDER-MENU-PROOF-001 | Synchronous guest-RAM/readback and semaphore/presentation ordering proof, explicit unsupported-state handling |
| RENDER-CONTRACT-001 | done | coherence_contract | RENDER-MENU-PROOF-001 | Preflight unsupported batches without replay; surface metadata and CPU fallback tests |
| RENDER-COHERENCE-PROOF-001 | done | coherence_proof | RENDER-CONTRACT-001 | Real executor→GPU synchronous readback, guest RAM and ordering proof |
| RENDER-MENU-BACKEND-001 | done | lead | RENDER-COHERENCE-001 | Guarded native menu semantics/composition, transform-once Q/W checks, menu comparison/navigation and independent review |
| RENDER-MENU-CONTRACT-001 | done | lead | RENDER-COHERENCE-001 | Symbolic program semantics, transform-once rejection and shared surface guard |
| RENDER-MENU-NATIVE-001 | done | lead | RENDER-MENU-CONTRACT-001 | Same guarded native GPU module in game and composed synthetic references |
| HZ-MEMORY-001 | done | lead | BOOT-SWITCH-001 | Horizon ownership/decommit/release/remap correction and focused failure tests |
| HZ-ALIGN-001 | done | lead | BOOT-SWITCH-001 | Correct posix_memalign contract on host and Horizon, build and independent review |
| RENDER-MENU-REG-001 | done | lead | RENDER-MENU-CONTRACT-001, RENDER-MENU-NATIVE-001 | Opt-in shader/adapter integration and actual menu image/navigation evidence |

| HZ-PWRITE-001 | done | lead | HZ-MEMORY-001 | Remove broken global pwrite; use private partition-initialization descriptor with focused proof and review |

| HZ-TIME-001 | done | lead | HZ-MEMORY-001 | Validate UTC calendar/range conversions, pre1970 inverse and focused proofs |

| RENDER-QUEUE-001 | done | lead | RENDER-MENU-NATIVE-001 | Bounded OOM isolation, queue lifetime reuse, repeat/RSS proof and review |

| EMU-GPU-GAME-001 | in_progress | intel_runner | EMU-GPU-PRESENT-001, RENDER-MENU-REG-001 | Optional IntelWayland game runner and bounded native-menu validation |

### Level-load investigation (2026-10-30)

| ID | State | Owner | Scope / acceptance | Rollback |
|---|---|---|---|---|
| LEVEL-LOAD-CONTROL-001 | done | load_control | Private game/build/level-load/control/; Trace actual original loading control flow and exit trigger; identify state transition and precise evidence-backed probe | Remove isolated diagnostics |
| LEVEL-LOAD-FAULT-001 | done | host_fault | Private game/build/level-load/fault/; Determine original caller/target semantics causing loading failure and host invalid pointer, with assembly and runtime evidence | Remove isolated diagnostics |
| LEVEL-LOAD-REVIEW-001 | done | evidence_review | Private game/build/level-load/review/; Independently inspect actual frames/logs/source/provenance and review eventual critical fix and acceptance evidence | Remove isolated diagnostics |

| LEVEL-SPAWN-PROOF-001 | done | lead | INPUT-NAV-001, RENDER-MENU-BACKEND-001, HZ-TIME-001 | Switch scene plus original player-class/render-frame spawn proof; read-only diagnostics, expected watchdog stop separate from crash; independent review |
| ICALL-NULL-001 | done | re | LEVEL-LOAD-001 | Explain the skipped null indirect call (call #109186 at about 10 s); faithful fix or documented harmless behaviour | Revert game/ change |
| HW-SWITCH-001 | in_progress | user | LEVEL-LOAD-001 | Physical-Switch run: menu, first level, real-controller control test, real FPS; evidence returned from the SD card. 2026-11-02: boots and draws the legal screen (reports/HW_SWITCH_FIRST_RUN.md). 2026-11-04: first mission entered from the menu in four logged runs, no crash line; control test, REC timecode and picture UNVERIFIED (the user has not answered), in-level rate about 0.19 flips/s (reports/HW_DEKO_ADAPTER_RUNS.md) | n/a (measurement) |
| HW-NATIVE-001 | done | runtime | HW-SWITCH-001 | Native (no Docker) macOS NRO build script | Delete the script |
| HW-FREEZE-001 | in_progress | runtime | HW-SWITCH-001 | Console freeze ~2 s after the first file open: isolate which of patch 0016's changes mattered | Revert branch; delete patches 0015/0016 |
| HW-INPUT-001 | done | runtime | HW-SWITCH-001 | Controller drives the main menu on the console (pad latch, hold polls, poll logging) | `MANHUNT_PAD_LATCH=0` / revert |
| HW-TOOLS-001 | done | runtime | HW-NATIVE-001 | `scripts/hw-run.sh` and `scripts/hw-summarize.py`: one command per hardware cycle | Delete the scripts |
| HW-PERF-001 | in_progress | runtime | HW-INPUT-001 | Rendering speed on the console: audit then try the deko3d backend; profile the CPU rasteriser. 2026-11-04, one console, defaults under review (resident and yield off): menu about 28.85 flips/s, level about 0.19 flips/s; older defaults: 39 flips/s over the log on the menu. Two independent reviews failed the earlier candidate; final independent review and merge pending, done at merge (reports/HW_DEKO_ADAPTER_RUNS.md) | Keep the CPU renderer |
| HW-PERF-002 | ready | runtime | HW-PERF-001 | In-level GPU rendering, measured on the console. First log which `surface_ok` check fails and how raster time splits by refusal class; then stage a GPU path, each stage host-tested, behind its own default-off switch, 0 differ in a level and a measured flip rate. Measure the yield and the resident target's benefit separately; bound or justify `dkQueueWaitIdle` and the sync on every kick, semaphore and report. In-level verify with `MANHUNT_DEKO_RESIDENT=1` and substantial GPU use before the resident target can ever default on. Closes at 30 flips/s sustained in the first level, or with an options analysis if the reasons cannot get there | `MANHUNT_GPU_BACKEND=0` or the stage's own config switch; revert the stage |
| HW-TOOLS-002 | ready | runtime | HW-PERF-001 | Diagnostics and tooling found by the 2026-11-04 reviews: verify must report a failed shadow allocation instead of skipping silently; `[MH-DEKO-T]` counts must not double in verify runs; `scripts/hw-run.sh` must not delete the card's NRO before the rename can succeed; large guest pitch bounded in the snapshot path; a `[MH-WD]` progress signal that means something under nxlink. Each with an executed test | Revert the change |
| HW-LOG-001 | done | runtime | LEVEL-LOAD-001 | Optional MANHUNT_LOG_FILE so hardware runs return text logs (stdout/stderr are debug-channel only today); default off | Revert main_switch.c |
| ICALL-NULL-002 | ready | re | ICALL-NULL-001 | Explain the second null indirect call at guest 0x00222AF1 (late in host runs); faithful fix or documented harmless behaviour | Revert game/ change |
| CFG-LINE-001 | done | runtime | HW-LOG-001 | Switch config reader: 4095-byte lines, over-long lines rejected visibly instead of silently truncated | Revert config_init |
