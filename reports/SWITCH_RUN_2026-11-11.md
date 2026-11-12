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

Expected discriminators: with reuse on, `kept=` stays 0 and `arena exhausted`
should not appear before the fight. If the black screen persists without
exhaustion, the allocation hypothesis is rejected and the new vertex-program
check localizes the first invalid constant row.
