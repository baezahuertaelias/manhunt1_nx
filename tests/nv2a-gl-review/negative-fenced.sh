#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
source="${GL_REVIEW_SOURCE:-$root/game/src/nv2a_gl/nv2a_gl.c}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
python3 - "$source" "$out/no-fenced-publish.c" <<'PY'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
old='static void gl_sync(int invalidate)\n{'
assert s.count(old)==1
Path(sys.argv[2]).write_text(s.replace(old,old+'\n    if (gl_fenced()) return; /* deliberately missing completion publication */'))
PY
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$root/tests/nv2a-gl-review/stubs" -I"$runtime" -I"$(dirname "$source")" \
  "-DNV2A_GL_SOURCE=\"$out/no-fenced-publish.c\"" "$root/tests/nv2a-gl-review/test_gl.c" \
  "$(dirname "$source")/gl_psh.c" "$(dirname "$source")/gl_vsh.c" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-no-fenced-publish"
ulimit -c 0
set +e
timeout 30s "$out/test-no-fenced-publish" fenced >"$out/no-fenced-publish.log" 2>&1
rc=$?
set -e
if [ "$rc" != 134 ] || ! grep -q 'fenced_ownership_test' "$out/no-fenced-publish.log"; then
  cat "$out/no-fenced-publish.log"
  echo "FAIL missing fenced completion: expected stale-RAM assertion, got $rc"; exit 1
fi
echo "PASS negative control: omitted fenced completion publication fails exact guest-RAM assertion"
