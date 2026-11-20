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

# Fixed-function fog coordinate, GL clip depth, LIT and alpha test: the new
# defaults, then each rollback switch's old form.
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -ffp-contract=off -ffunction-sections -fdata-sections \
  "$root/tests/shader-lighting/test_fog_clip.c" "$root/game/src/nv2a_gl/gl_vsh.c" "$root/game/src/nv2a_gl/gl_psh.c" \
  -Wl,--gc-sections -lEGL -lGL -lm -pthread -o "$out/test-fog-clip"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-fog-clip"
MANHUNT_GL_FF_FOG=0 MANHUNT_GL_CLIP_CLAMP=1 MANHUNT_GL_SHADER_MINOR=0 \
  EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-fog-clip"

# Fixed-function spot cones and texgen/texture matrices: defaults, then the
# rollback switches.
cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -ffp-contract=off -ffunction-sections -fdata-sections \
  "$root/tests/shader-lighting/test_spot_texgen.c" "$root/game/src/nv2a_gl/gl_vsh.c" \
  -Wl,--gc-sections -lEGL -lGL -lm -pthread -o "$out/test-spot-texgen"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-spot-texgen"
MANHUNT_GL_FF_SPOT=0 MANHUNT_GL_FF_TEXGEN=0 \
  EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$out/test-spot-texgen"
