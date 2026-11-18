#!/usr/bin/env bash
# The voice processor with and without patch 0037 (apu-resolve-once) must
# produce the same bytes: mixbins every frame, guest RAM, registers and voice
# state. Builds test_vp_exact.c against both apu_vp.c copies and compares the
# dumps. Run inside manhunt-recompiler:xboxrecomp-f0f8a46 (needs
# /opt/xboxrecomp), or set APU_VP_BEFORE / APU_VP_AFTER to two runtime trees.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${AUDIO_TEST_OUT:-$root/game/build/audio-tests}/vp-exact"
patch=0037-apu-resolve-once.patch
mkdir -p "$out"

tree() {   # tree <dir> <apply 0037: 0/1>
  rm -rf "$1" && cp -r /opt/xboxrecomp "$1"
  for p in "$root"/game/patches/xboxrecomp/*.patch; do
    [ "$2" = 1 ] || [ "$(basename "$p")" != "$patch" ] || continue
    git -C "$1" apply "$p"
  done
}
before="${APU_VP_BEFORE:-}"
after="${APU_VP_AFTER:-}"
if [ -z "$before" ]; then before="$out/before"; tree "$before" 0; fi
if [ -z "$after" ]; then after="$out/after"; tree "$after" 1; fi

frames="${VP_EXACT_FRAMES:-3000}"
status=0
for flags in "-O2 -ffp-contract=off" "-O3 -ffp-contract=fast" "-O0"; do
  for side in before after; do
    rt=$before; [ "$side" = after ] && rt=$after
    # shellcheck disable=SC2086
    cc -D_DEFAULT_SOURCE -std=gnu11 $flags -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-but-set-variable \
      -I"$rt/src" -I"$rt/src/apu" "-DAPU_VP_SOURCE=\"$rt/src/apu/apu_vp.c\"" \
      "$root/tests/audio-apu/test_vp_exact.c" "$rt/src/platform/win32_compat.c" \
      -lm -pthread -o "$out/vp-exact-$side"
    echo "[$flags] $side: $("$out/vp-exact-$side" "$frames" "$out/$side.bin")"
  done
  # The rollback switch must give the same bytes too.
  echo "[$flags] after, MANHUNT_APU_RESOLVE_ONCE=0: $(MANHUNT_APU_RESOLVE_ONCE=0 "$out/vp-exact-after" "$frames" "$out/after-off.bin")"
  for b in after after-off; do
    if cmp "$out/before.bin" "$out/$b.bin"; then
      echo "[$flags] $b identical: $(stat -c %s "$out/$b.bin") bytes, sha256 $(sha256sum "$out/$b.bin" | cut -c1-16)"
    else
      echo "[$flags] $b DIFFERENT"; status=1
    fi
  done
done
exit $status
