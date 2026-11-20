#!/usr/bin/env bash
# Executor-narrowed 16-bit indices (patch 0038, MANHUNT_NARROW_INDICES) give GL
# the same element data. Needs the patched runtime tree (game/build/xboxrecomp).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$root/game/src/nv2a_gl" \
  "$root/tests/nv2a-gl-review/test_narrow_indices.c" "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-narrow-indices"
ulimit -c 0
timeout 60s "$out/test-narrow-indices"
