#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
source="${GL_REVIEW_SOURCE:-$root/game/src/nv2a_gl/nv2a_gl.c}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$runtime" -I"$(dirname "$source")" $(pkg-config --cflags sdl2) \
  "-DNV2A_GL_SOURCE=\"$source\"" "$root/tests/nv2a-gl-review/test_real_gl.c" \
  "$(dirname "$source")/gl_api.c" "$(dirname "$source")/gl_psh.c" "$(dirname "$source")/gl_vsh.c" \
  -Wl,--gc-sections $(pkg-config --libs sdl2) -lm -pthread -o "$out/test-real-gl"
ulimit -c 0
MANHUNT_GL_SYNC=strict LIBGL_ALWAYS_SOFTWARE=1 timeout 45s xvfb-run -a "$out/test-real-gl"
MANHUNT_GL_SYNC=strict LIBGL_ALWAYS_SOFTWARE=1 timeout 45s xvfb-run -a "$out/test-real-gl" queued
LIBGL_ALWAYS_SOFTWARE=1 timeout 45s xvfb-run -a "$out/test-real-gl" fenced
LIBGL_ALWAYS_SOFTWARE=1 timeout 45s xvfb-run -a "$out/test-real-gl" fenced-queued
