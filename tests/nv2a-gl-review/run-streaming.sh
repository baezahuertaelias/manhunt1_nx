#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$root/game/src/nv2a_gl" \
  "$root/tests/nv2a-gl-review/test_streaming.c" "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-streaming"
cc -std=c11 -Wall -Wextra -Werror "$root/tests/nv2a-gl-review/test_loader.c" "$root/game/src/nv2a_gl/gl_api.c" -o "$out/test-loader"
ulimit -c 0
"$out/test-loader"
MANHUNT_GL_SYNC=strict timeout 30s "$out/test-streaming"
MANHUNT_GL_SYNC=fenced timeout 30s "$out/test-streaming"
