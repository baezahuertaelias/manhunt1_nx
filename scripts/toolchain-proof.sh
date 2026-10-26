#!/usr/bin/env bash
# Milestone 1 (toolchain proof): rebuild and rerun every gate in one go.
#   c   minimal NRO (built twice, hashes must match)
#   a,b lifted XBE functions vs Unicorn reference on x86-64 + AArch64 Linux,
#       then the self-checking NRO in Eden
#   d   deko3d triangle with pixel readback, in Eden (built twice)
#   e   the emulator runs above are the automated emulator tests
# Hardware gates are not covered here and stay UNVERIFIED.
# Negative controls are manual evidence (reports/RECOMP_EVAL.md), not part of this suite.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
DEVKITA64_IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"
declare -A result

build_twice() {   # dir nro
  local hashes=()
  for _ in 1 2; do
    docker run --rm --network none --user "$(id -u):$(id -g)" \
      --mount "type=bind,src=$root/$1,dst=/work" -w /work \
      "$DEVKITA64_IMAGE" make clean all > /dev/null || return 1
    hashes+=("$(sha256sum "$root/$1/$2" | cut -d' ' -f1)") || return 1
  done
  echo "  $2 ${hashes[0]}"
  [ -n "${hashes[0]}" ] && [ "${hashes[0]}" = "${hashes[1]}" ]
}

emu() {   # nro marker -> PASS | FAIL | NO-RESULT | SETUP-ERROR
  "$root/scripts/emu-run.sh" "$1" "$2" 120
  case $? in 0) echo PASS > "$3" ;; 1) echo FAIL > "$3" ;; 2) echo NO-RESULT > "$3" ;; *) echo SETUP-ERROR > "$3" ;; esac
}

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

echo "== gate c: minimal NRO"
build_twice tests/nro-proof nro-proof.nro && result[c]=PASS || result[c]=FAIL

echo "== gates a,b: recompiled functions (Linux)"
"$root/scripts/recomp-proof.sh" > "$tmp" 2>&1
rc=$?
grep -E 'arena=|FAIL|result:|recomp-proof.nro' "$tmp"
[ $rc -eq 0 ] && result[ab_linux]=PASS || result[ab_linux]=FAIL

echo "== gates a,b,e: recompiled functions (Eden)"
if [ "${result[ab_linux]}" = PASS ]; then
  emu "$root/tests/recomp-proof/build/recomp-proof.nro" RECOMP-PROOF "$tmp"; result[ab_emu]=$(cat "$tmp")
else
  echo "  skipped: Linux stage failed, the NRO may be stale"; result[ab_emu]=SKIPPED
fi

echo "== gate d: deko3d (Eden)"
if build_twice tests/deko3d-proof deko3d-proof.nro; then
  emu "$root/tests/deko3d-proof/deko3d-proof.nro" DEKO3D-PROOF "$tmp"; result[d]=$(cat "$tmp")
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
