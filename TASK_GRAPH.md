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
| EMU-IMAGE-001 | ready | test | — | Eden/Ryujinx image built from pinned source in this repo passes `scripts/toolchain-proof.sh` |
| GPU-PROOF-001 | done | runner_analysis | — | Container renders via `/dev/dri`; renderer is not a software fallback |
| REPO-REMOTE-001 | done | lead | user approval (given 2026-10-28) | Remote created (public on user instruction); `main` protected (no force-push/deletion, PRs) |
| LEVEL-LOAD-001 | pending | runtime | INPUT-NAV-001, RENDER-001 | Milestone 4: first level loads in Eden, environment shown, player spawned |
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
| RENDER-MENU-BACKEND-001 | in_progress | lead | RENDER-COHERENCE-001 | Guarded native menu semantics/composition, transform-once Q/W checks, menu comparison/navigation and independent review |
| RENDER-MENU-CONTRACT-001 | in_progress | menu_contract | RENDER-COHERENCE-001 | Symbolic program semantics, transform-once rejection and shared surface guard |
| RENDER-MENU-NATIVE-001 | in_progress | menu_native | RENDER-MENU-CONTRACT-001 | Same guarded native GPU module in game and composed synthetic references |
| HZ-MEMORY-001 | in_progress | memory_audit | BOOT-SWITCH-001 | Horizon ownership/decommit/release/remap correction and focused failure tests |
| HZ-ALIGN-001 | done | lead | BOOT-SWITCH-001 | Correct posix_memalign contract on host and Horizon, build and independent review |
