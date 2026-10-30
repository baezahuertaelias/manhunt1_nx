#!/usr/bin/env bash
# Synthetic real-executor/GPU guest-RAM proof; no game inputs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
runtime="$root/game/build/xboxrecomp/src"
[[ -f "$runtime/kernel/nv2a_backend.h" ]] || { echo 'nv2a-coherence-proof: prepared runtime missing' >&2; exit 3; }
sha256sum "$runtime/kernel/"nv2a_{backend.h,pb_exec.c,vsh_interp.c,combiner.c,flip_hold.c} "$root/tests/nv2a-coherence-proof/source/"* "$root/tests/nv2a-coherence-proof/Makefile" "$root/tests/nv2a-backend-proof/source/"{attribute_vsh,texture_fsh}.glsl
docker run --rm --network none --cpus 1 --memory 1g \
 --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
 --mount "type=bind,src=$root/tests/nv2a-coherence-proof,dst=/work/nv2a-coherence-proof" \
 --mount "type=bind,src=$root/tests/nv2a-backend-proof,dst=/work/nv2a-backend-proof,readonly" \
 --mount "type=bind,src=$runtime,dst=/upstream/src,readonly" \
 -w /work/nv2a-coherence-proof "$image" make -B -j1 XBOXRECOMP_DIR=/upstream clean all
nro="$root/tests/nv2a-coherence-proof/nv2a-coherence-proof.nro"
sha256sum "$nro"
"$root/scripts/emu-run.sh" "$nro" NV2A-COHERENCE-PROOF 120
