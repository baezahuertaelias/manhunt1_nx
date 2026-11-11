#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$runtime" -I"$root/game/src/nv2a_gl" $(pkg-config --cflags sdl2) \
  "$root/tests/nv2a-gl-review/test_ubo_real.c" "$root/game/src/nv2a_gl/gl_api.c" \
  "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections $(pkg-config --libs sdl2) -lm -pthread -o "$out/test-ubo-real"
ulimit -c 0
for mode in direct queued; do
  for ubo in 0 1; do
    MANHUNT_AMBIENT_LIGHT=-1 MANHUNT_GL_UBO="$ubo" LIBGL_ALWAYS_SOFTWARE=1 timeout 60s xvfb-run -a "$out/test-ubo-real" "$mode" "$out/ubo-$mode-$ubo.rgba"
  done
  cmp "$out/ubo-$mode-0.rgba" "$out/ubo-$mode-1.rgba"
  echo "PASS exact 6000 framebuffer comparisons: ordinary uniforms versus UBO ($mode)"
done
cmp "$out/ubo-direct-0.rgba" "$out/ubo-queued-1.rgba"
echo "PASS exact direct/queued cross-comparison"

for ubo in 0 1; do
  MANHUNT_AMBIENT_LIGHT=0.33 MANHUNT_GL_UBO="$ubo" LIBGL_ALWAYS_SOFTWARE=1 timeout 60s xvfb-run -a "$out/test-ubo-real" direct "$out/ambient-$ubo.rgba"
done
cmp "$out/ambient-0.rgba" "$out/ambient-1.rgba"
echo "PASS ambient RGB 0.33: analytic pixels, lit/unlit transitions and ordinary/UBO equality"
