# The faster deko3d adapter on the physical Switch (2026-11-04)

Status: **measured on one console in one session. With the defaults under review the main menu runs at about 28.85 flips per second and the first mission at about 0.19; the 30 FPS gate is not met, and gameplay stays UNVERIFIED.** The console is a modded Switch started in title-takeover mode, on Wi-Fi at 53 ms ping at worst. Its firmware and Atmosphère versions are still not recorded. Every log has `[MH-PAD] poller up: connected=1 handheld=1`, which describes the controller and suggests the console was not docked; the performance mode and clocks are not recorded. Text only: the frames, logs and NRO stay private under `game/build/`. Companion to [HW_RENDERING_OPTIONS.md](HW_RENDERING_OPTIONS.md) and [HW_PERFORMANCE_FIRST_LOOK.md](HW_PERFORMANCE_FIRST_LOOK.md); the host and the run procedure are in [docs/WINDOWS_SWITCH_SETUP.md](../docs/WINDOWS_SWITCH_SETUP.md).

## Summary

Two independent reviews failed the first candidate, and their conditions turned the resident render target and the time-poll yield into opt-in pieces. The figures below therefore come in two configurations, and they must not be mixed.

- **Menu, final defaults (run 7, resident and yield off):** about 28.85 flips per second, steady, with no crash line. This is just under 30, so the earlier statement that the menu reaches its speed goal holds only for the older configuration below.
- **Menu, older defaults (run 3, resident and yield on, older build):** 39 flips per second averaged over the whole log, about 43.6 in its steady windows.
- **First mission, both configurations (runs 4 and 7):** about 0.19 flips per second, about 5.4 s per flip.
- **Raster share (a time measure):** in run 7's steady window 68% of the draw calls reach the CPU rasteriser and take 97% of the draw time; in run 4 the same two shares are 69% and 97%.
- **Refusal share (a count of the adapter's decisions):** in run 5 the adapter refused 18,896 draws against 816 accepted over the whole log, and 17,884 against 77 between flips 60 and 120.
- **Verify:** 0 differences in every log, but the coverage in the level is thin in both level runs and is not a level-wide pixel-identity pass; see run 6 below.

Mission entry and in-level console stability over a few minutes were observed. Player control, the REC timecode and picture correctness are UNVERIFIED; the user has not answered on them.

## Builds

None of these builds is committed; the hashes identify private files.

| Source | NRO SHA-256 | Bytes | Use |
|---|---|---|---|
| `main` at `bd0d45e` | `9701940c29f5cca6074b1b57709255c62b72ca11a98910f33b69736b25ac9be7` | 21,694,653 | Reached the Manhunt menu, as reported by the user; no measurement taken |
| `deko-adapter-perf` at `1630025` | `ce40e999f75e0d8fd514f8979b25abf632444d3335de520edab8339491e464d7` | 21,706,941 | Runs 1 to 4 below; it has no reject-reason counters, and resident and yield default on |
| `deko-adapter-perf` at `1630025` plus uncommitted fixes | `b0cfe0554da8422d3100fb8aff4c863773ba6694801c9aed52356d171c97b922` | 21,711,037 | Run 5 below: the first reject counters; resident and yield default on |
| `deko-adapter-perf` working tree after both reviews | `45892c0f2a8da57dfaa8e6a37af746150be26940ae0471ae188cd502eb3d28da` | 21,711,037 | Runs 6 and 7 below: resident and yield default off, corrected `only` count. Built on Windows with Docker, 0 errors |

The last two have the same size and different hashes. The hash of the last build was checked against the file on disk.

## Host test on the source of the last build

`tests/deko-adapter/run.sh` with Debian clang 14.0.6 and 30 seeds: 9 of 9 configurations pass, including the new `defaults (resident off)` and the negative control (30 of 30 seeds differ); 8 of 8 mutants are killed under the strict rule below; the script prints `MUTATION RESULT: every planted bug was caught` and returns 0. The retry logged 2 start-up SIGSEGVs in that run. These figures come from the session record: no saved log of this test is in the private run folder.

The script changed for two reasons. With leak detection on, the test had failed on a 4 MiB leak in the harness itself (`test_adapter.c:181`), not in the adapter; the harness now frees those buffers and `run.sh` turns leak detection off. Second, measuring one binary 40 times per configuration showed clang 14's ASan segfaulting at start with no output in 9 of 40 runs, at random and with either form of `ASAN_OPTIONS` tried. The cause is probably high-entropy ASLR; this was not confirmed, because `setarch -R` could not run in the container. Under the old script each such crash made a mutant count as killed, so the earlier 8 of 8 results are not trustworthy on their own.

Now a run that gives SIGSEGV and no output is retried and logged to stderr. A mutant counts as killed only on `RESULT: N FAILED`, an AddressSanitizer report or an explicit test abort. Anything else is INCONCLUSIVE and makes the script exit 1.

In the configurations, `defaults (resident off)` sets nothing; the others that exercise the resident target, including the negative control, request `MANHUNT_DEKO_RESIDENT=1` explicitly.

The render-to-texture overlap flush in `nv2a_deko3d.c` now includes the P8 palette, and that addition has no dedicated test case: UNVERIFIED by test. The present in `horizon_compat.c` now returns when the height is 0 or the width from the guest's pitch is above 4096, clamps the output width and keeps its column table in `uint32_t`; no test or console run provoked those cases.

## Runs

All runs carried `MANHUNT_LOG_FILE`, `MANHUNT_WD=1` and `RECOMP_SPAN_4G=1`, and the log came back over the nxlink stream. There is no `[MH-CRASH]` line in any of the seven logs.

**Reading `[MH-WD]`.** `t=` counts watchdog ticks, not seconds: the first 40 ticks are 0.25 s apart and the rest 3 s apart (`main_switch.c`), so `t=N` above 40 is about `10 + 3 x (N - 40)` s. Run 7 checks this: t=131 gives about 283 s against a profile clock of 277 s. `STALE` means only that the watchdog's line counter did not change between ticks. In all seven logs that counter stays at its boot value (7 to 9) and every tick says `STALE`, while flips and draws advance, so it is not a no-progress signal in these runs.

| Run | Config | Log ends at | Result |
|---|---|---|---|
| 1, CPU baseline | `MANHUNT_GPU_BACKEND=0`, `MANHUNT_LOG_SYNC=1`, `MANHUNT_DUMP_SECS=5` | t=56, about 58 s, closed normally | 20 polls (about 0.34 per second); last dump at `presents=24`. The user reported that d-pad up and down moved the menu highlight |
| 2b, verify | `MANHUNT_GPU_BACKEND=1`, `MANHUNT_DEKO_VERIFY=1`, `MANHUNT_LOG_SYNC=1`, `MANHUNT_DUMP_SECS=5` | t=88, about 154 s | Last line `[MH-DEKO-V] compared 545: 0 differ, 0 pixels differ in total, largest channel difference 0`; `flip=240 draws=6584 reject_draw=4 reject_clear=238`; `fast path active: cached=1 rect=1 tcache=1 resident=1 verify=1`. **Menu only** |
| 3, menu speed | `MANHUNT_GPU_BACKEND=1`, `RECOMP_RENDER_PROFILE=1`, `RECOMP_PB_REPORT_MS=5000`, `MANHUNT_LOG_SYNC=0`, `MANHUNT_DUMP_SECS=0` | t=53, about 49 s | `flip_intervals=1724 ns=44210648100`, which is 39 flips per second over the whole log; the steady 5 s windows run at about 43.6. Resident and yield on. No button was pressed in this run |
| 4, mission | As run 3 but `MANHUNT_DUMP_SECS=10`; the user played | t=88, about 154 s, `flip_intervals=409 ns=152212246996` | Entered the mission from the menu; resident and yield on; see below |
| 5, mission verify | `MANHUNT_GPU_BACKEND=1`, `MANHUNT_DEKO_VERIFY=1`, `MANHUNT_LOG_SYNC=0`, `MANHUNT_DUMP_SECS=0`; build `b0cfe055…` | t=123, about 259 s, flip=120 | `resident=1`. Last line `[MH-DEKO-V] compared 138: 0 differ, 0 pixels differ in total, largest channel difference 0`; `draws=816 reject_draw=18896`; see below |
| 6, mission verify on the defaults | `MANHUNT_GPU_BACKEND=1`, `MANHUNT_DEKO_VERIFY=1`, `MANHUNT_LOG_SYNC=0`, `MANHUNT_DUMP_SECS=0`; build `45892c0f…`; env file `run6-level-verify-defaults.env` | t=96, about 178 s, after flip 115 | `resident=0`. Last verify line `[MH-DEKO-V] compared 1161: 0 differ, 0 pixels differ in total, largest channel difference 0`, **at flip 90**; last marker `[GPU] draw #14800`; see below |
| 7, speed on the defaults | `MANHUNT_GPU_BACKEND=1`, `RECOMP_RENDER_PROFILE=1`, `RECOMP_PB_REPORT_MS=5000`, `MANHUNT_LOG_SYNC=0`, `MANHUNT_DUMP_SECS=0`, no verify; build `45892c0f…`; env file `run7-speed-defaults.env` | t=131, about 283 s, `flip_intervals=3097 ns=277475677852` | `resident=0`, yield off. Menu about 28.85 flips per second, level about 0.19; see below |

Each log also has the boot-time lines `Mirror N: FAILED (error 50)`, the tiled-aperture warning and two missing-file lines. These come from the boot path and from the known unsupported RAM mirrors (BLOCKERS #6), not from the adapter; no `[MH-DEKO]` line reports a failure and every `[MH-DEKO-V]` line reports 0 differ.

The session 1 hang from 2026-11-03 (BUILD_LOG) did not recur in run 2b with the same two adapter settings, but its cause remains **undetermined**. Its log cannot be recovered, because `log.txt` on the card is truncated at each start.

## The main menu (runs 3 and 7)

Run 3 (resident and yield on): each 30-flip report shows 840 draws, which is 28 draws per flip; per call the upload takes 20 us, the command build 187 us and the GPU about 170 us; textures have 690 hits and 0 misses. The earlier figure from the Mac sessions was 1.8x over the CPU renderer at about 44 ms per draw.

Run 7 (resident and yield off, later build): the same 28 draws per flip and 690 texture hits, with the upload at 171 us, the build at 221 us and the GPU at about 277 us per call. The steady windows are about 146 flips in 5.06 s, which is about 28.85 flips per second; they range from 28.83 to 29.09 between flips 129 and 2896. From the start of the log to flip 2896 the average is 27.6.

| Compared like for like | Run 3 | Run 7 | Drop |
|---|---:|---:|---:|
| Steady windows | about 43.6 | about 28.85 | about 34% |
| Whole menu from the start of the log | 39.0 | 27.6 | about 29% |

The "39 against 28.85", about 26%, that was first quoted compares a whole-log average with a steady window, so it understates the difference. **Do not attribute the drop to either piece alone.** The two runs differ in the resident target, the yield and the build, and these were not isolated. The benefit of each piece has still to be measured separately (HW-PERF-002).

Compared with run 1, run 3 is about 100x, but **not like for like**: run 1 had `MANHUNT_LOG_SYNC=1` and frame dumps, and its rate is taken from title polls and the watchdog rather than from the render profile. Measured on `presents` instead (24 in about 58 s) the factor is about 90x. Read it as an order of magnitude.

## The first mission (runs 4 and 7)

The user's words in run 4: "almost everything is running well at first view, even the load of the mission. but when playing the scene is too slow".

The render profile, in each run's steady window:

| Quantity | Run 4, flips 386 to 405 (resident and yield on) | Run 7, flips 3070 to 3097 (resident and yield off) |
|---|---:|---:|
| Profile clock | 98.93 s | 145.77 s |
| Flips | 19, so **0.19 per second, 5.2 s per flip** | 27, so **0.19 per second, 5.4 s per flip** |
| Draw calls | 5748, about 303 per flip | 8768, about 325 per flip |
| Draw time | 96.46 s | 145.62 s |
| Raster calls | 3943, **69% of the draw calls**, about 23.8 ms per call | 5955, **68% of the draw calls**, about 23.7 ms per call |
| Raster time | 93.69 s, **97% of the draw time** | 141.43 s, **97% of the draw time** |

Run 4's window start, flip 386, includes one faster flip, which is why its figure is 5.2 s; from flip 388 to the end of its log the pace is about 5.3 s per flip. An earlier hand calculation for run 4 paired raster and draw snapshots that were one report apart and gave 96% and 67%; the figures above use the same snapshot for both. Final totals: run 4 `draw calls=23323`, `raster calls=9401 ns=134222162512`; run 7 `draw calls=99266 ns=246647673016`, `raster calls=10786 ns=162538305352`. Nobody noted which screen was showing at the start of either window.

In-level speed is the same in both configurations, which fits the raster dominating; neither the resident target nor the yield is shown to matter there, and neither is isolated.

**Not known for the level (all UNVERIFIED):**

- Player control. The user has not confirmed movement. In runs 4, 6 and 7 the last `[MH-PAD]` change comes at or just after the start of the mission's asset loading; in run 5 button changes appear later, at t=69, and what they did in the game is not known. That line records buttons and analog buttons only, not the sticks, so it neither shows nor excludes stick use.
- Whether the REC timecode runs in real time.
- Whether the in-level picture is correct. The user said "almost everything" in run 4 and gave no detail. Runs 5, 6 and 7 dumped no frames.
- Audio, which is not implemented.
- Pixel identity across the level as a whole: see the next section.

## In-level verify (runs 5 and 6)

**Run 6 is a stronger check per call, and it does not cover the steady level.** With the resident target off, verify compares after every call over the whole surface. With it on, it compares only at syncs, over the dirty rectangle. So 1161 comparisons in run 6 are 1161 accepted draws.

The evidence that run 6 entered the mission is in its log. 45 lines name `Jury_Turf`, the first at t=43 (about 19 s) just after a `[MH-PAD]` analog press, the last at t=45 (about 25 s). The log then runs to t=96 (about 178 s), still drawing. That is about 150 s after the last level file opened. Nothing in the log shows what was on screen; `MANHUNT_DUMP_SECS=0` means there are no frames.

The user was asked to stay in the mission for at least 5 minutes, and **the log does not show that**: all of it covers about 3 minutes, including the boot and the menu. The run's `wd.txt` and `log.txt` were not fetched, and the host log file closed about 36 minutes after the env file was written, so the run may have lasted longer than the stream shows. The stream is non-blocking and can drop lines. So "0 crash lines" holds for what the log contains, and a longer stay cannot be claimed or excluded.

Verify totals are printed only every 30 flips, and a mismatch has no line of its own. The run's three totals are:

| At flip | About | Compared (0 differ in each) | Refused so far |
|---:|---:|---:|---:|
| 30 | 19 s | 704 (menu) | few |
| 60 | 25 s | 991 | 1,012 |
| 90 | 43 s | 1161 | 5,347 |

The pace of about 5.3 s per flip starts at about flip 95 and lasts to the end of the log at about flip 115, and flip 120 was never reached. **So run 6 reports no verify result at all for the steady in-level phase, about the last 135 s of its log.** Between flips 60 and 90 it compared 170 draws against 4,335 refused. Its result is 0 differences on the draws of the menu and the transition into the level, with the resident target off.

Run 5 did reach flip 120, with the resident target on: compared 88, 108 and 138 at flips 60, 90 and 120. It shows 0 differences on 30 comparisons in steady play, and between flips 60 and 120 only 77 draws reached the GPU against 17,884 refused. That is too little to say anything about the resident target under heavier GPU use.

Neither run may be read as a level-wide pixel-identity pass. Little can be gained by simply staying longer: in run 7 only 29 draws reached the GPU between flips 3060 and 3090, against 9,328 refused. Verify covers what the GPU draws, and the level hardly uses the GPU yet. An in-level verify with the resident target on and substantial GPU use is a gate for HW-PERF-002, before that piece can ever default on.

## Why draws are refused (runs 5, 6 and 7)

Diagnostics-only counters in `game/src/nv2a_deko3d.c` print `[MH-DEKO-R] flip=N refused=N transforms=N (reason any/only): name=a/b ...` every 5 flips when the count changed. "any" counts every failing check of a refused draw. "only" counts draws refused by that check alone among the named checks, which is at most what supporting that one feature would move to the GPU. A null render state or an out-of-range P8 index is not a named check: on its own it shows as `other`, and alongside a named check it can be counted in that check's `only`, so read `only` as an upper bound. An unsupported format is also shape-checked as a 32-bit texture for `tex_other`. No config key was added.

**Run 5 used an older `only` count for `tex_format`, which could overstate it.** Its table is kept for the record; the corrected counter is in the table for run 7.

| Reason, run 5 (build `b0cfe055…`) | Total at flip 120, any/only | Steady window, flip 110 to 120, any/only |
|---|---:|---:|
| Refused draws | 18,896 | 3,104 (about 310 per flip) |
| `cull` | 12,384 / 0 | 2,650 / 0 |
| `depth` | 12,064 / 0 | 2,580 / 0 |
| `surface` | 11,504 / 286 | 2,750 / 70 |
| `shading` | 11,012 / 0 | 2,570 / 0 |
| `tex_format` | 7,272 / 6,086 | 394 / 344 |
| `tex_addr` | 1,170 / 42 | 40 / 10 |

| Reason, run 7 (build `45892c0f…`, corrected counter) | Total at flip 3095, any/only | Steady window, flip 3075 to 3095, any/only |
|---|---:|---:|
| Refused draws | 13,610 | 6,446 (about 322 per flip) |
| `cull` | 7,879 / 0 | 5,564 / 0 |
| `depth` | 7,698 / 0 | 5,423 / 0 |
| `surface` | 6,855 / 173 | 5,763 / 140 |
| `shading` | 6,663 / 0 | 5,403 / 0 |
| `tex_format` | 6,567 / 5,466 | 763 / 663 |
| `tex_addr` | 1,102 / 25 | 80 / 20 |

Run 6's steady window, flip 95 to 115, gives the same shares. Refused formats at the end of the logs: run 5 `0x0C` 6,819 and `0x07` 453; run 7 `0x0C` 6,301 and `0x07` 266. That `0x0C` is DXT1 is a hypothesis from the NV2A format numbering, not checked against the decoder.

What this shows:

- **Depth alone unlocks nothing** (`only` = 0), which disproves the expectation previously written here that depth state was the key blocker on its own.
- In the steady window about 89% of refused draws fail `surface`, in all three runs. About 84% fail `depth`, about 86% `cull` and about 84% `shading`. **The combinations are not recorded**, so how many draws fail several of them together is not measured. That the near-zero `only` counts point to one class of 3D world draws is an inference.
- The large cumulative `tex_format` `only` count comes mostly from before the steady window. In the window it is about 10% of refusals on the corrected counter, so adding that format alone would not change the level's speed much.
- The counters count draws, not time. Which class costs the most raster time is not measured.
- Refused draws (about 320 per flip) outnumber raster calls (about 220 per flip) in run 7's steady window. Why a third of the refused draws make no raster call is not established, so the refusal share and the raster share are not interchangeable.
- Which `surface_ok` check fails is not recorded: it is the first thing to find out for HW-PERF-002.

## Open questions

- The cause of the session 1 hang of 2026-11-03 is undetermined.
- Why `[MH-DEKO-T]` reports `present 0` in every line with the resident target on, while `fence` counts syncs, is not confirmed.
- In verify runs `[MH-DEKO-T]` doubles the draw counts: run 5's lines sum to 1632 against `draws=816`, and run 6's to 2322 against 1161. Each call runs through both paths and both are counted; its per-call timings are not representative there either. Which paths' figures to keep is task HW-TOOLS-002.
- Whether the yield or the resident target brings a benefit is not measured separately. Each stays default off until it is.
- The resident target's known gap remains: the guest CPU reading or writing the surface with no GPU completion signal has no sync, and verify cannot observe that gap.

## Corrections to earlier text

These earlier statements in this report and its companions were wrong and are withdrawn:

- "About 89% of refused draws fail several checks at once." About 89% fail `surface`; combinations are not recorded.
- "About 69% of the draws were refused." 69% is the share of draw calls reaching the rasteriser in run 4. The refusal share is a different and higher figure.
- "The watchdog's `t=` runs behind the profile clock under load." `t=` counts ticks, and with the tick spacing it matches the clock: run 4's t=88 is about 154 s against 152 s.
- Run 5's `tex_format` `only` count, which used the older count.
- An earlier explanation that blamed a leading colon in `ASAN_OPTIONS` for the start-up crashes. The crash is random in both forms.

Next step: [NEXT_ACTION.md](../NEXT_ACTION.md), tasks HW-PERF-002 and HW-TOOLS-002.
