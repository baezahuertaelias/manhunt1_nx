#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$root/game/build/xboxrecomp/src/kernel"
out="$root/game/build/gl-review-tests"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$root/game/src/nv2a_gl" \
  "$root/tests/nv2a-gl-review/test_ambient.c" "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-ambient"
env -u MANHUNT_AMBIENT_LIGHT "$out/test-ambient" 0.33
MANHUNT_AMBIENT_LIGHT=0.5 "$out/test-ambient" 0.5
MANHUNT_AMBIENT_LIGHT=0 "$out/test-ambient" 0
MANHUNT_AMBIENT_LIGHT=-1 "$out/test-ambient" -1
MANHUNT_AMBIENT_LIGHT=nan "$out/test-ambient" -1
MANHUNT_AMBIENT_LIGHT=bad "$out/test-ambient" -1
