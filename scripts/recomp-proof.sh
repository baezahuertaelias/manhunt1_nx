#!/usr/bin/env bash
# Gate (a)/(b) proof: lift selected DEFAULT.xbe functions with xboxrecomp,
# run them on x86-64 and AArch64 Linux against a Unicorn x86-32 reference,
# then build the self-checking Switch NRO (tests/recomp-proof/build/recomp-proof.nro).
set -euo pipefail
IMAGE="${RECOMPILER_IMAGE:-manhunt-recompiler:xboxrecomp-f0f8a46}"
DEVKITA64_IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"
root="$(cd "$(dirname "$0")/.." && pwd)"
xbe="${XBE:-$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}/DEFAULT.xbe}"
mkdir -p "$root/tests/recomp-proof/build"

docker run --rm --network none --hostname localhost --cpus 3 --memory 8g \
  --user "$(id -u):$(id -g)" \
  -v "$root:/repo" -v "$xbe:/xbe/default.xbe:ro" \
  "$IMAGE" bash /repo/tests/recomp-proof/run.sh

docker run --rm --network none --cpus 3 --memory 4g --user "$(id -u):$(id -g)" \
  -v "$root/tests/recomp-proof:/work" -w /work \
  "$DEVKITA64_IMAGE" make -f Makefile.switch
sha256sum "$root/tests/recomp-proof/build/recomp-proof.nro"
