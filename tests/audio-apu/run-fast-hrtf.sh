#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${APU_TEST_RUNTIME:-$root/game/build/xboxrecomp}"
out="${AUDIO_TEST_OUT:-$root/game/build/audio-tests}"
mkdir -p "$out"
cc -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-variable -Wno-cast-function-type \
  -I"$runtime/src" -I"$runtime/src/apu" "$root/tests/audio-apu/test_hrtf_fast.c" \
  -lm -pthread -o "$out/hrtf-fast"
"$out/hrtf-fast"
cc -D_DEFAULT_SOURCE -std=c11 -O2 -ffp-contract=off -fno-strict-aliasing -ffunction-sections -fdata-sections \
  -I"$runtime/src" -I"$runtime/src/apu" \
  "-DAPU_VP_SOURCE=\"$runtime/src/apu/apu_vp.c\"" "$root/tests/audio-apu/test_voice_workload.c" \
  "$runtime/src/platform/win32_compat.c" -Wl,--gc-sections -lm -pthread -o "$out/voice-workload-fast"
for fast in 0 1; do
  MANHUNT_APU_FAST_HRTF="$fast" "$out/voice-workload-fast" 1600 "$out/voice-fast-$fast.bin"
done
cmp "$out/voice-fast-0.bin" "$out/voice-fast-1.bin"
echo "PASS whole VP frame exact mixbins/filter/guest/IRQ state: pause, resume, voice end, notifier, parameter changes"
for repeat in 1 2 3 4 5; do
  for fast in 0 1; do
    echo "[benchmark] repeat=$repeat fast=$fast"
    MANHUNT_APU_FAST_HRTF="$fast" "$out/voice-workload-fast" 6000
done
done
