#!/usr/bin/env bash
# Gate (a)/(b) proof: lift selected DEFAULT.xbe functions with xboxrecomp,
# run them on x86-64 and AArch64 Linux against a Unicorn x86-32 reference,
# then build the self-checking Switch NRO (tests/recomp-proof/build/recomp-proof.nro).
set -euo pipefail
IMAGE="${RECOMPILER_IMAGE:-manhunt-recompiler:xboxrecomp-f0f8a46}"
DEVKITA64_IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"
root="$(cd "$(dirname "$0")/.." && pwd)"
CPUS="${CPUS:-$(nproc)}"   # all host cores by default (user decision 2026-10-26)
xbe="${XBE:-$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}/DEFAULT.xbe}"
# A missing bind source would make Docker create a root-owned directory there.
[ -f "$xbe" ] || { echo "recomp-proof: XBE not found: $xbe" >&2; exit 3; }
mkdir -p "$root/tests/recomp-proof/build"

docker run --rm --network none --hostname localhost --cpus "$CPUS" --memory 8g \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$root,dst=/repo" \
  --mount "type=bind,src=$(realpath -e "$xbe"),dst=/xbe/default.xbe,readonly" \
  "$IMAGE" bash /repo/tests/recomp-proof/run.sh

# -B: always rebuild, so flag or header changes can never leave a stale NRO.
docker run --rm --network none --cpus "$CPUS" --memory 4g --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$root/tests/recomp-proof,dst=/work" -w /work \
  "$DEVKITA64_IMAGE" make -B -j"$CPUS" -f Makefile.switch
sha256sum "$root/tests/recomp-proof/build/recomp-proof.nro"
