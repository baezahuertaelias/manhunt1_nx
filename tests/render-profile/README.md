# Render-profile helper check

Run `scripts/render-profile-test.sh` (C11 compiler, pthreads and Git required).
It extracts the maintained patch's helper, compiles with warnings as errors
and runs synthetic clock/accounting tests. No game input is used. Tests cover
disabled-path clock/counter silence, zero/empty time, nested scan/method
accounting, invalid subtraction, backwards time, flip intervals and isolation
between threads. This does not execute scanner/executor integration or prove
Switch runtime behaviour.

`0004-render-profile.patch` enables timing only for `RECOMP_RENDER_PROFILE=1`
(read once per thread). Counters are thread-local, matching the runtime's ack
thread which executes the scanner and existing stats report. Reports are
cumulative; compare two reports for an interval. All times are monotonic wall
nanoseconds and include time descheduled by the host/emulator:

- `scan`: both scanner entrypoints, including decoded method work.
- `method`: executor entrypoint, including early-return methods and nested work.
- `draw`: raster batch including vertex transforms, clipping and rasterization.
- `raster`: CPU screen-space triangle routine plus transformed queued pixel
  rasterization (`xf_rows_parallel`), including any worker waits. No per-pixel
  timestamps. It is nested in draw, except for the existing optional raster-test
  triangle issued during clear. Transformed triangle setup/queueing stays in
  draw; screen-space validation and dispatch remain included in raster.
- `clear`: clear dispatch including its validation and optional raster test.
- `present`: existing libnx/window presentation call, including copying,
  scaling and waits. It excludes the separate backend flip callback.
- `flip_intervals`: elapsed intervals between FLIP_STALL entry timestamps,
  not displayed-frame or hardware FPS proof.

Scopes overlap: never add their times. `scan_excluding_methods_ns` subtracts
method time only, and is neither guest execution nor CPU utilisation. The
public executor can be called directly by synthetic tests outside a scanner;
that may make subtraction invalid and emits UNVERIFIED. Invalid-clock samples
are counted and excluded, and disqualify a clean measurement run. Thread-local
reports do not aggregate other threads. No guest-thread timing is provided.

The disabled path adds checks but performs no profile clock reads. Enabled
per-method and per-triangle timestamps have overhead; measure baseline versus
profiled runs before using the results. Guest addresses do not apply to this
host instrumentation. Original menu/boot, output parity, performance overhead
and Switch integration require the separate serialized build/runtime proof and
independent review. Restore baseline by removing patch 0004 and rebuilding.
