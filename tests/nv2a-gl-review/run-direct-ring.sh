#!/usr/bin/env bash
# P7: the direct vertex ring against the old queue-copy path, on llvmpipe.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$runtime" -I"$root/game/src/nv2a_gl" $(pkg-config --cflags sdl2) \
  "$root/tests/nv2a-gl-review/test_direct_ring.c" "$root/game/src/nv2a_gl/gl_api.c" \
  "$root/game/src/nv2a_gl/gl_psh.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections $(pkg-config --libs sdl2) -lm -pthread -o "$out/test-direct-ring"
ulimit -c 0
run() { LIBGL_ALWAYS_SOFTWARE=1 timeout 120s xvfb-run -a "$out/test-direct-ring" "$@"; }
for mode in strict gpu; do
  for pack in 0 1; do
    MANHUNT_GL_QUEUE_PACK=$pack MANHUNT_GL_DIRECT_RING=0 run "$mode" "$out/ring-$mode-$pack-off.bin"
    MANHUNT_GL_QUEUE_PACK=$pack run "$mode" "$out/ring-$mode-$pack-on.bin" 64
    MANHUNT_GL_QUEUE_PACK=$pack run "$mode" "$out/ring-$mode-$pack-tiny.bin" 24
    cmp "$out/ring-$mode-$pack-off.bin" "$out/ring-$mode-$pack-on.bin"
    cmp "$out/ring-$mode-$pack-off.bin" "$out/ring-$mode-$pack-tiny.bin"
    echo "PASS direct ring ($mode, MANHUNT_GL_QUEUE_PACK=$pack): output identical to the old path, with wraps and full-ring fallbacks"
  done
done
cmp "$out/ring-strict-0-off.bin" "$out/ring-strict-1-on.bin"
echo "PASS direct ring: queue pack on and off render the same"
