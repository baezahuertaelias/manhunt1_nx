#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${APU_TEST_RUNTIME:-$root/game/build/xboxrecomp}"
out="${AUDIO_TEST_OUT:-$root/game/build/audio-tests}"
mkdir -p "$out"
cc -D_DEFAULT_SOURCE -std=c11 -O2 -g -pg -ffp-contract=off -fno-strict-aliasing -ffunction-sections -fdata-sections \
  -I"$runtime/src" -I"$runtime/src/apu" \
  "-DAPU_VP_SOURCE=\"$runtime/src/apu/apu_vp.c\"" "$root/tests/audio-apu/test_voice_workload.c" \
  "$runtime/src/platform/win32_compat.c" -Wl,--gc-sections -lm -pthread -o "$out/voice-workload"
cd "$out"
./voice-workload 6000
gprof -b ./voice-workload gmon.out > voice-workload-profile.txt
gprof -b -l ./voice-workload gmon.out > voice-workload-line-profile.txt
head -40 voice-workload-line-profile.txt
