#!/usr/bin/env bash
# Milestone 1 (toolchain proof): rebuild and rerun every gate in one go.
#   c  minimal NRO (built twice, hashes must match)
#   a,b lifted XBE functions vs Unicorn reference on x86-64 + AArch64 Linux,
#      then the self-checking NRO in Eden
#   d  deko3d triangle with pixel readback, in Eden
#   e  the emulator runs above are the automated emulator tests
# Hardware gates are not covered here and stay UNVERIFIED.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
DEVKITA64_IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"
declare -A result

build_twice() {   # dir nro
  local h1 h2
  for i in 1 2; do
    docker run --rm --network none --user "$(id -u):$(id -g)" -v "$root/$1:/work" -w /work \
      "$DEVKITA64_IMAGE" make clean all > /dev/null || return 1
    [ $i = 1 ] && h1=$(sha256sum "$root/$1/$2" | cut -d' ' -f1) || h2=$(sha256sum "$root/$1/$2" | cut -d' ' -f1)
  done
  echo "  $2 $h1"
  [ "$h1" = "$h2" ]
}

echo "== gate c: minimal NRO"
build_twice tests/nro-proof nro-proof.nro && result[c]=PASS || result[c]=FAIL

echo "== gates a,b: recompiled functions (Linux)"
"$root/scripts/recomp-proof.sh" | grep -E 'arena=|FAIL|result:|recomp-proof.nro' && result[ab_linux]=PASS || result[ab_linux]=FAIL

echo "== gates a,b,e: recompiled functions (Eden)"
"$root/scripts/emu-run.sh" "$root/tests/recomp-proof/build/recomp-proof.nro" RECOMP-PROOF 120 && result[ab_emu]=PASS || result[ab_emu]=FAIL

echo "== gate d: deko3d (Eden)"
if build_twice tests/deko3d-proof deko3d-proof.nro; then
  "$root/scripts/emu-run.sh" "$root/tests/deko3d-proof/deko3d-proof.nro" DEKO3D-PROOF 120 && result[d]=PASS || result[d]=FAIL
else
  result[d]=FAIL
fi

echo "== summary"
status=0
for k in c ab_linux ab_emu d; do
  echo "  $k: ${result[$k]}"
  [ "${result[$k]}" = PASS ] || status=1
done
echo "  hardware (Switch, 30 FPS): UNVERIFIED"
exit $status
