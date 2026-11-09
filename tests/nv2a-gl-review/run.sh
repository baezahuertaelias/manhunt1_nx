#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
source="${GL_REVIEW_SOURCE:-$root/game/src/nv2a_gl/nv2a_gl.c}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$(dirname "$source")" \
  "-DNV2A_GL_SOURCE=\"$source\"" "$root/tests/nv2a-gl-review/test_gl.c" \
  "$(dirname "$source")/gl_psh.c" "$(dirname "$source")/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-gl"
ulimit -c 0
MANHUNT_GL_SYNC=strict timeout 30s "$out/test-gl"
timeout 30s "$out/test-gl" fenced
timeout 30s "$out/test-gl" gpu
