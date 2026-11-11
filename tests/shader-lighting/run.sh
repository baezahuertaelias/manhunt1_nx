#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$root/game/build/xboxrecomp"
out="$root/game/build/shader-lighting-tests"
mkdir -p "$out"
ulimit -c 0
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -ffp-contract=off -ffunction-sections -fdata-sections \
  -I"$runtime/include" -I"$runtime/src" -I"$runtime/src/kernel" -I"$runtime/src/platform" \
  "$root/tests/shader-lighting/reference.c" "$root/tests/shader-lighting/test_gpu.c" \
  "$root/game/src/nv2a_gl/gl_vsh.c" -Wl,--gc-sections -lEGL -lGL -lm -pthread -o "$out/test-gpu"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-gpu"
RECOMP_FFP_FLIP_NORMALS=1 EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-gpu"

EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-gpu" ubo
RECOMP_FFP_FLIP_NORMALS=1 EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-gpu" ubo
