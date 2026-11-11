#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$root/game/build/xboxrecomp/src/kernel"
out="$root/game/build/gl-review-tests"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$root/game/src/nv2a_gl" \
  "$root/tests/nv2a-gl-review/test_validation.c" "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-validation"
"$out/test-validation"
"$out/test-validation" enabled
