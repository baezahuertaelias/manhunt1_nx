# Switch run and contiguous-memory lead — 2026-11-11

Branch: `codex/gpu-offload`.

## Consolidated run (`-Experiment all`)

Evidence: `game/build/hw-test/gpu-offload/20261111-230844-all/` (NRO SHA-256
`91bf655ffde557011e0086ef36f7c3fd9094a595af4a699550492c0c3f6503c4`). All five
GPU switches plus `MANHUNT_APU_FAST_HRTF=1` and `MANHUNT_CS_CACHE=1`,
1020/307 MHz, no per-method profiling, original ambient.

| | This run | Rollback 20261110-234419 |
|---|---:|---:|
| Median FPS, samples above 20,000 vertices/frame | 25.5 (144) | 15.5 (117) |
| Median APU VP wall time, ms/s | 760 | 978 |
| Audio underruns, total | 0 | 430 |

The two runs did not follow identical play paths, and the rollback median
includes post-fault samples (its pre-fault median is 17.5 FPS). This is an
uncontrolled comparison, not an accepted speedup.

The owner reported a black screen when the fight started after the
presentation. The log shows the same signature as the earlier fault: about
350 draws/frame at about 20 FPS, then 930–980 draws/frame at 7 FPS. No
`MH-GL-NONFINITE` line appeared, but that check only covered fixed-function
transforms (`xform == 1`), so this is not evidence that transforms stayed
finite. The fault reproduces with every switch on as well as with all off.

The owner also reported Xbox font/symbol imagery sometimes appearing while the
first intro video loads. The startup texture uploads and draws in this log
match five earlier runs; the cause is not identified.

## Contiguous arena exhaustion

The 64 MB contiguous window is full 26.6 s after boot (`allocs=239 frees=0`,
62,856 KB in use), and 4,064 `arena exhausted` refusals follow, all from the
XDK wrapper at guest `0x001FC7BA`. Without `RECOMP_HEAP_RECLAIM`,
`MmFreeContiguousMemory` never returns a block, so every block the title frees
and reallocates (streaming buffers, textures) is lost. The rollback run shows
the same exhaustion well before its first nonfinite transform. A title whose
allocations fail may continue with missing buffers; this is the leading
candidate for the black screen, **not yet proven**.

## Prepared for the next run

- Patch `0032-contig-reclaim-switch.patch`: `MANHUNT_CONTIG_RECLAIM=1` reuses
  freed contiguous blocks without `RECOMP_HEAP_RECLAIM`'s general-heap changes.
  Default off. Frees answered without reuse are now counted (`kept=`) in
  `[CONTIG]` lines and in each exhaustion message. The upstream memory
  regression suite gains a `contig-reclaim` mode; all six modes pass on a fresh
  copy with the full patch series applied.
- `MANHUNT_GL_VALIDATE=1` also reports the first nonfinite constant row of
  vertex-program draws (`xform == 2`), once per row, at most 32 lines. Unused
  constant rows may legitimately be nonfinite; this is evidence, not proof.
- `scripts/gpu-offload-run.ps1` gains a `contig` selection, included in `all`.

## Follow-up runs

`20261111-232620-all` (NRO `2823cd24…`, contiguous reuse on): no arena
exhaustion (473 frees reused, `kept=0`), yet the 3D world went black already
in the hiding tutorial (owner screenshot at 3:23, 7.2 FPS, HUD visible) and
stayed black through the court fight. NV2A modelview methods carried
`0xFFC00000`. Contiguous exhaustion is therefore **rejected** as the cause.
No `MH-GL-NONFINITE` vertex-program line appeared.

The owner's intro screenshot shows the whole 512×512 paletted font atlas drawn
1:1 at the top-left for a frame before the first movie; cause not identified.

`20261111-233659-all` (NRO `a8b1ec1d…`, adds the `SetTransform` probe):
the owner fought the hunter and progressed without the black screen or the
FPS drop. No `0xFFC00000` modelview, no 7 FPS/960-draw episode. The probe
never ran (not even its banner), so SetTransform is not the gameplay matrix
path. The only other difference from the failing build is code layout, so a
single passing run does not establish a fix: either the fault is intermittent
or it depends on layout/timing (for example an uninitialized read). Next: the
same build again, then an A/B with `2823cd24…` (`-Nro`).

`20261111-234749-all` (same NRO `a8b1ec1d…` again): the owner reports the
hunter did not appear and FPS fell. Draws stayed at ~336/frame and GL draw time
at ~12 ms, while guest "rest" time rose from ~12 ms to 42–48 ms per frame
(38 to 16 FPS) at log line ~17703. No NaN modelview and no 960-draw episode
before the session ended. So the fault is **intermittent with an identical
binary**; layout is rejected. The first symptom is extra guest CPU work when
the hunter should spawn; the NaN/black state followed later in earlier runs.

A `MANHUNT_CALL_PROFILE=ON` build (`build/switch-prof`, `src/call_profile.c`)
prints the 24 most-called lifted functions every 2 s, to name the code that
starts running at the rise.

### Call-profile runs

`20261111-235506-all` (profile build, fault reproduced): guest "rest" rose
from ~14 to ~48 ms/frame at log line ~17070 with ~336 draws. From that
window on, functions in the RenderWare intersection region
(`tool/intsec/rtintsec.c` per the identification inventory: `0x000E2F50`,
`0x000E2430`; neighbours `0x000DC770`, `0x000D9D20`, `0x000D9620`,
`0x000DCDA0`, `0x00186280`) appear in the top 24, and two synthetic thunk
slots `0xFE000000`/`0xFE000004` at ~12k calls per 2 s.

`20261112-000138-all` (profile build with call sites, hunter appeared
normally): the same functions also run in normal play, from the same sites
(`0x000DC770` from `0x000DD72C`/`0x000DD415`, `0x000D9D20` from
`0x000DC79C`, `0x000E2F50` from `0x000DC8DB`), at roughly half the count
(`0x000DC770` ~21k vs 40–47k per 2 s). Their presence alone is therefore
not fault-specific; a call-site comparison of a failing run in the same
scene is still needed. The earlier NaN-position hypothesis is unproven.

### Host reproduction

The host build (`build/host`, current tree plus the uncommitted host GL
wiring in `main.c`/`CMakeLists.txt`) crashes during boot with both
`MANHUNT_GPU_BACKEND=2` and the default renderer: `BlockOnTime: fence 11 not
reached in 250 ms`, then SIGSEGV with guest `eax=0x0029D8CC` (logs
`game/build/logs/run-host1.log`, `run-host2.log`). Host reproduction of the
hunter fault needs that bring-up first; the uncommitted host wiring is left
out of the branch.

Expected discriminators: with reuse on, `kept=` stays 0 and `arena exhausted`
should not appear before the fight. If the black screen persists without
exhaustion, the allocation hypothesis is rejected and the new vertex-program
check localizes the first invalid constant row.
