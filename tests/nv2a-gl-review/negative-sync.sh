#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
source="${GL_REVIEW_SOURCE:-$root/game/src/nv2a_gl/nv2a_gl.c}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
python3 - "$source" "$out/no-sync.c" <<'PY'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
assert 'gl_sync(0);' in s
Path(sys.argv[2]).write_text(s.replace('gl_sync(0);','/* deliberate missing completion readback */'))
PY
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$(dirname "$source")" \
  "-DNV2A_GL_SOURCE=\"$out/no-sync.c\"" "$root/tests/nv2a-gl-review/test_gl.c" \
  "$(dirname "$source")/gl_psh.c" "$(dirname "$source")/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-no-sync"
ulimit -c 0
set +e
MANHUNT_GL_SYNC=strict timeout 30s "$out/test-no-sync" >"$out/no-sync.log" 2>&1
rc=$?
set -e
if [ "$rc" != 134 ] || ! grep -q 'clear_pitch_test' "$out/no-sync.log"; then
  cat "$out/no-sync.log"
  echo "FAIL missing-sync negative control: expected clear assertion/SIGABRT, got $rc"
  exit 1
fi
echo "PASS negative control: omitted production completion readback fails clear/pitch assertion"
