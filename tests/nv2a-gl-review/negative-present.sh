#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
source="${GL_REVIEW_SOURCE:-$root/game/src/nv2a_gl/nv2a_gl.c}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
python3 - "$source" "$out/no-present-import.c" <<'PY'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
a=s.index('    /* Completion (including sync(1) before CPU fallback)')
b=s.index('    if (dump_every &&',a)
Path(sys.argv[2]).write_text(s[:a]+'    /* deliberately omitted presentation RAM import */\n'+s[b:])
PY
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -ffunction-sections -fdata-sections \
  -I"$runtime" -I"$(dirname "$source")" $(pkg-config --cflags sdl2) \
  "-DNV2A_GL_SOURCE=\"$out/no-present-import.c\"" "$root/tests/nv2a-gl-review/test_real_gl.c" \
  "$(dirname "$source")/gl_api.c" "$(dirname "$source")/gl_psh.c" "$(dirname "$source")/gl_vsh.c" \
  -Wl,--gc-sections $(pkg-config --libs sdl2) -lm -pthread -o "$out/test-no-present-import"
ulimit -c 0
set +e
MANHUNT_GL_SYNC=strict LIBGL_ALWAYS_SOFTWARE=1 timeout 45s xvfb-run -a "$out/test-no-present-import" >"$out/no-present-import.log" 2>&1
rc=$?
set -e
if [ "$rc" != 134 ] || ! grep -q 'swap_pixel==0xff334455' "$out/no-present-import.log"; then
  cat "$out/no-present-import.log"
  echo "FAIL missing-presentation-import control: expected stale presented-pixel assertion, got $rc"
  exit 1
fi
echo "PASS negative control: omitted presentation RAM import fails actual displayed-pixel assertion"
