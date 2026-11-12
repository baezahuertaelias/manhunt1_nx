#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${AUDIO_TEST_OUT:-$root/game/build/audio-tests}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -pthread -I"$root/game/src" -I"$root/game/build/gen" -I"$root/game/build/xboxrecomp/src/apu" \
 "$root/tests/audio-apu/test_glue.c" "$root/game/src/audio_apu.c" "$root/game/src/dsound_hle.c" -lm -o "$out/glue"
ulimit -c 0
"$out/glue"
cc -std=c11 -Wall -Wextra -Werror -pthread -I"$root/game/build/xboxrecomp/src/apu" \
 "$root/tests/audio-apu/test_sdl.c" $(pkg-config --cflags --libs sdl2) -o "$out/sdl"
"$out/sdl"
cc -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -pthread \
 -I"$root/game/build/xboxrecomp/src/apu" \
 "$root/tests/audio-apu/test_math_cache.c" -lm -o "$out/math-cache"
"$out/math-cache"
