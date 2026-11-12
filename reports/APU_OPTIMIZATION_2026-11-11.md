# APU voice-processing optimization — 2026-11-11

Implemented on `codex/gpu-offload` as
`game/patches/xboxrecomp/0030-apu-exact-hrtf-history.patch`.

Switch speed, audible playback and gameplay acceptance for this change:
**UNVERIFIED**. `MANHUNT_APU_FAST_HRTF=1` enables the candidate;
`MANHUNT_APU_FAST_HRTF=0` restores the original convolution. The default stays
on the original path until a real-console run passes.

## Evidence before editing

The captured console VP wall-time bucket approached 985 ms/s. That bucket
includes scheduling and locking, so it does not by itself identify an algorithm
or measure isolated CPU execution time.

A deterministic host fixture runs the actual `mcpx_apu_vp_frame`, with 48
voices, initially 16 HRTF voices and 32 ordinary voices. It exercises PCM sample
fetching through guest SGE descriptors, pitch/resampling, low-pass filtering on
one quarter of the voices, target-volume changes and gain ramps. A later version
also pauses/resumes a voice and ends another, retaining notifier/IRQ state.
This is synthetic input, not a capture of Manhunt's actual voice configuration.

The initial 6,000-frame `-O2 -ffp-contract=off -pg` run took 704.564 ms on the
host. `gprof -l` attributed roughly 80% of sampled execution to HRTF convolution
and parameter smoothing in `apu_state.h`. Per-tap circular-buffer addressing,
interpolation and smoothing dominate that fixture; existing exact `powf`
caches were already present in patch 0026.

Profile evidence:

- `game/build/audio-tests/voice-workload-profile.txt`
- `game/build/audio-tests/voice-workload-line-profile.txt`
- `game/build/audio-tests/voice-workload-annotated.txt`

Reproduce profiling with `tests/audio-apu/run-voice-workload.sh` in the pinned
recompiler image. It requires `gprof`, available in that image. Profiling
instrumentation is absent from the production change.

## Change

The original convolution evaluates `% HRTF_BUFLEN` for circular history inside
every tap, and evaluates a fractional-delay branch for each tap. HRTF retains
all 31 taps per channel, every 32-sample frame, and every original voice.

The candidate mirrors the 73-sample history into a temporary contiguous window
at the beginning of each HRTF call. Each new input updates both copies. A tap
then reads a directly indexed address, with the fractional-delay choice outside
the tap loop. History is copied back at the end. This uses 1,168 bytes of
additional frame-local stack and changes no persistent state layout.

Parameter smoothing remains at exactly the same sample boundaries. Delay,
interpolation, coefficient multiplication and accumulator order are unchanged.
Invalid delay-state bounds select the original implementation. No voices,
samples, taps, filters, envelopes, spatialization, guest notifications or IRQ
steps are disabled or dropped. GP/EP implementation is unchanged.

The runtime prints the selected convolution path once when HRTF is used. No
per-voice production timing or logging was introduced.

## Exactness and regression checks

Run `tests/audio-apu/run-fast-hrtf.sh`, with `APU_TEST_RUNTIME` selecting a
runtime prepared with patch 0030. Tests passed in
`manhunt-recompiler:xboxrecomp-f0f8a46`, Docker `--user 0`.

- 18,615 differential HRTF frames: every one of the 73 ring positions, 85 signed
  starting delays, changing target delays/coefficients, in-place and separate
  input/output, signed zero and subnormals. Output floats **and the complete
  HrtfFilter state** match bit for bit after every frame.
- AddressSanitizer and UndefinedBehaviorSanitizer passed that same fixture.
- A 1,600-frame complete VP workload produces byte-identical mixbins, filter
  histories, guest RAM including voice play/envelope cursors and notifications,
  APU registers and pending IRQ state. Pause/resume and a voice ending are
  included. Source-address resolution counts match (9,204,279).
- The upstream `tests/apu_vp/test_main.c`, compiled against the fully patched
  isolated runtime, passed: every ADPCM block count the descriptor can express,
  per-voice lock behavior, and gain ramps.
- Existing `tests/audio-apu/run.sh` passed: audio glue, SDL callback sink, exact
  math-cache tests.
- All current patches applied cleanly, in order, to a fresh copy of the image's
  `/opt/xboxrecomp` in `game/build/apu-opt-work/full-series`. The resulting APU
  source matches the tested optimized staging copy. No live lift cache or
  generated code was changed during this investigation.

Differential output files each contain 8,198,657 bytes. Both have
SHA-256 `1956bcff84fd2b4b5ec0c1e567d80ad7ca4d4443276d2928a06c321e9d0036b7`.

## Host timing

Five alternating, uninstrumented repetitions of the same 6,000-frame complete
VP fixture (48 initial voices, one HRTF voice ends during the fixture):

| Path | Wall ms, samples | Median wall ms |
|---|---|---:|
| Original | 689.391, 655.182, 669.882, 670.406, 665.104 | 669.882 |
| Contiguous history | 419.500, 432.767, 427.421, 418.910, 416.111 | 419.500 |

The median ratio is 0.626, a 37.4% reduction in this synthetic
host workload. The complete mixbin hash is `47e02f692ef7dba7` on every repetition,
and source-address resolution counts are identical (34,294,075). A separate
64-voice HRTF-only microbenchmark in the recorded run measured 298.239 ms versus
144.677 ms. These measurements establish a host optimization, not a Switch
speedup; host load, architecture and the title's actual voice mix differ.

Evidence:

- `game/build/audio-tests/fast-hrtf-proof.log`
- `game/build/audio-tests/fast-hrtf-sanitized.log`
- `game/build/audio-tests/fast-hrtf-summary.json`
- `game/build/audio-tests/voice-fast-0.bin`
- `game/build/audio-tests/voice-fast-1.bin`

## Consolidated console acceptance

Build this patch together with the other accepted work before requesting the
owner's nxlink session. Explicitly enable `MANHUNT_APU_FAST_HRTF=1`, retain the
same clocks/scene, and compare VP wall time, consumed audio frames and underrun
events. Confirm that voice playback, spatialization and combat audio remain
correct. If needed, use the same candidate NRO with the flag set to `0` for the
original path. No claim of zero underruns or hardware improvement is made here.
