#!/usr/bin/env bash
# Build/run the synthetic Nv2aVertex offscreen proof; no game inputs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
header="$root/game/build/xboxrecomp/src/kernel/nv2a_backend.h"
[[ -f "$header" ]] || { echo "nv2a-backend-proof: prepared xboxrecomp header missing at $header" >&2; exit 3; }
sha256sum "$header" "$root/tests/nv2a-backend-proof/source/"* "$root/tests/nv2a-backend-proof/Makefile"
docker run --rm --network none --cpus 1 --memory 1g \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=$root/tests/nv2a-backend-proof,dst=/work" \
  --mount "type=bind,src=$header,dst=/upstream/src/kernel/nv2a_backend.h,readonly" \
  -w /work "$image" make -B -j1 XBOXRECOMP_DIR=/upstream all
nro="$root/tests/nv2a-backend-proof/nv2a-backend-proof.nro"
sha256sum "$nro"
"$root/scripts/emu-run.sh" "$nro" NV2A-BACKEND-PROOF 120
