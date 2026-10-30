#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/tests/horizon-alloc-proof/build"
mkdir -p "$work"
sha256sum "$root/game/src/horizon_alloc.c" "$root/tests/horizon-alloc-proof/main.c" "$0"
cc -std=c11 -O2 -Wall -Wextra -Werror -Dposix_memalign=manhunt_test_posix_memalign \
  "$root/game/src/horizon_alloc.c" "$root/tests/horizon-alloc-proof/main.c" -o "$work/host"
"$work/host"
python3 - "$root/game/src/horizon_alloc.c" "$work/invalid.c" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text()
old = 'if (align < sizeof(void *) || align % sizeof(void *) || (align & (align - 1)))'
assert s.count(old) == 1
Path(sys.argv[2]).write_text(s.replace(old, 'if (0)'))
PY
cc -std=c11 -O2 -Wall -Wextra -Werror -Dposix_memalign=manhunt_test_posix_memalign \
  "$work/invalid.c" "$root/tests/horizon-alloc-proof/main.c" -o "$work/invalid"
if "$work/invalid" > "$work/negative.log"; then
  echo 'HORIZON-ALLOC-NEGATIVE FAIL validation removal was not detected' >&2
  exit 1
fi
grep -F 'HORIZON-ALLOC-PROOF FAIL' "$work/negative.log"
echo 'HORIZON-ALLOC-NEGATIVE PASS removed alignment validation detected'
image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
docker run --rm --network none --cpus 1 --memory 1g \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=$root/tests/horizon-alloc-proof,dst=/test" \
  --mount "type=bind,src=$root/game/src/horizon_alloc.c,dst=/horizon_alloc.c,readonly" \
  -w /test "$image" bash -ec '
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -std=c11 -O2 -Wall -Wextra -Werror \
  -Dposix_memalign=manhunt_test_posix_memalign -D__SWITCH__ -march=armv8-a+crc+crypto \
  -mtp=soft -fPIE -ffunction-sections -ffile-prefix-map=/test=. \
  -I/opt/devkitpro/libnx/include -specs=/opt/devkitpro/libnx/switch.specs \
  -L/opt/devkitpro/libnx/lib -Wl,--gc-sections /horizon_alloc.c main.c -lnx -o build/proof.elf
/opt/devkitpro/tools/bin/nacptool --create "Horizon allocation contract proof" "Manhunt Recompilation Team" "0.0.1" build/proof.nacp
/opt/devkitpro/tools/bin/elf2nro build/proof.elf build/proof.nro --nacp=build/proof.nacp
'
sha256sum "$work/proof.nro"
"$root/scripts/emu-run.sh" "$work/proof.nro" HORIZON-ALLOC-PROOF 120
