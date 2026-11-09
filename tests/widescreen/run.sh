#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${WIDE_TEST_OUT:-$(mktemp -d)}"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/tests/widescreen/stubs" \
 "$root/tests/widescreen/test_camera.c" "$root/game/src/widescreen.c" -lm -o "$out/camera"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/tests/widescreen/stubs" \
 "$root/tests/widescreen/test_tiled_lock.c" "$root/game/src/d3d_tiled_lock.c" -o "$out/tiled"
for backend in UNSET 0 1 2 invalid; do
 for wide in UNSET '' 0 1 invalid 1junk; do
  valid=1; on=0
  if [ "$wide" = 1 ]; then
   if [ "$backend" = 2 ]; then on=1; else valid=0; fi
  elif [ "$wide" = invalid ] || [ "$wide" = 1junk ]; then valid=0; fi
  opts=(-u MANHUNT_GPU_BACKEND -u MANHUNT_WIDESCREEN)
  [ "$backend" = UNSET ] || opts+=("MANHUNT_GPU_BACKEND=$backend")
  [ "$wide" = UNSET ] || opts+=("MANHUNT_WIDESCREEN=$wide")
  env "${opts[@]}" "$out/camera" "$valid" "$on"
 done
done
ulimit -c 0
"$out/tiled"
if [ "${WIDE_NEGATIVE_CONTROLS:-1}" = 1 ]; then
 # Each deliberate production defect must be rejected by the same fixtures.
 sed 's/if (on \&\& (!backend/if (0 \&\& on \&\& (!backend/' "$root/game/src/widescreen.c" > "$out/camera-mutant.c"
 cc -std=c11 -Wall -Wextra -Werror -I"$root/tests/widescreen/stubs" \
  "$root/tests/widescreen/test_camera.c" "$out/camera-mutant.c" -lm -o "$out/camera-mutant"
 if MANHUNT_WIDESCREEN=1 MANHUNT_GPU_BACKEND=0 "$out/camera-mutant" 0 0; then
  echo 'FAIL camera backend guard negative control survived'; exit 1
 fi
 sed 's/if (p - TILED_BASE >= RAM_BYTES)/if (0 \&\& p - TILED_BASE >= RAM_BYTES)/' "$root/game/src/d3d_tiled_lock.c" > "$out/tiled-mutant.c"
 cc -std=c11 -Wall -Wextra -Werror -I"$root/tests/widescreen/stubs" \
  "$root/tests/widescreen/test_tiled_lock.c" "$out/tiled-mutant.c" -o "$out/tiled-mutant"
 if "$out/tiled-mutant"; then
  echo 'FAIL tiled aperture bounds negative control survived'; exit 1
 fi
 echo 'PASS two negative controls: removed backend guard and aperture bounds rejected'
fi
echo 'PASS widescreen suite: 30 config combinations and tiled-lock cases'
