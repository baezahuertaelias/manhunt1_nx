#!/usr/bin/env bash
# Synthetic production-path lifetime/config proof, without SDL dependencies.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${AUDIO_TEST_OUT:-$root/game/build/audio-review-tests}"
gen="${AUDIO_GEN_DIR:-$root/game/build/gen}"
apu="${AUDIO_APU_DIR:-$root/game/build/xboxrecomp/src/apu}"
mkdir -p "$out"
flags=(-std=c11 -Wall -Wextra -Werror -pthread -I"$root/game/src" -I"$gen" -I"$apu")
ulimit -c 0
cc "${flags[@]}" "$root/tests/audio-apu/test_glue.c" "$root/game/src/audio_apu.c" "$root/game/src/dsound_hle.c" -lm -o "$out/glue"
timeout 60s "$out/glue"
cc "${flags[@]}" "$root/tests/audio-apu/test_shutdown.c" "$root/game/src/audio_apu.c" -o "$out/shutdown"
timeout 60s "$out/shutdown"
cc "${flags[@]}" "$root/tests/audio-apu/test_period.c" -o "$out/period"
timeout 60s "$out/period"
cc "${flags[@]}" "$root/tests/audio-apu/test_process_exit.c" -o "$out/process-exit"
timeout 60s "$out/process-exit"

# Negative control: remove only production access serialization. It must compile
# and fail the same concurrent test, not merely fail compilation or hang.
python3 - "$root/game/src/audio_apu.c" "$out/unsafe-audio.c" <<'PY'
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
s = s.replace('static pthread_mutex_t s_access =', 'static pthread_mutex_t s_access __attribute__((unused)) =')
s = s.replace('pthread_mutex_lock(&s_access);', '').replace('pthread_mutex_unlock(&s_access);', '')
Path(sys.argv[2]).write_text(s)
PY
cc "${flags[@]}" "$root/tests/audio-apu/test_shutdown.c" "$out/unsafe-audio.c" -o "$out/unsafe-shutdown"
set +e
timeout 60s "$out/unsafe-shutdown" >"$out/unsafe-shutdown.log" 2>&1
status=$?
set -e
if [[ $status -ne 134 ]]; then
    echo "FAIL: shutdown control expected SIGABRT(134), got $status" >&2; exit 1
fi
grep -F 'Assertion' "$out/unsafe-shutdown.log"
echo 'PASS negative control: missing lifetime serialization fails concurrent assertion'

mkdir -p "$out/unsafe-exit"
sed 's/_Exit(status);/exit(status);/' "$root/game/src/process_exit.h" >"$out/unsafe-exit/process_exit.h"
cc -std=c11 -Wall -Wextra -Werror -pthread -I"$out/unsafe-exit" "$root/tests/audio-apu/test_process_exit.c" -o "$out/unsafe-process-exit"
set +e
timeout 60s "$out/unsafe-process-exit" >"$out/unsafe-process-exit.log" 2>&1
status=$?
set -e
if [[ $status -ne 134 ]]; then
    echo "FAIL: process-exit control expected SIGABRT(134), got $status" >&2; exit 1
fi
echo 'PASS negative control: ordinary exit runs unsafe atexit cleanup and fails expected status'
echo 'PASS audio review synthetic suite (not SDL/audio parity or hardware validation)'
