#!/usr/bin/env bash
# Same native module as the game, real portable executor, synthetic inputs only.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
runtime="$root/game/build/xboxrecomp/src"
[[ -f "$runtime/kernel/nv2a_backend.h" ]] || { echo 'nv2a-menu-proof: prepared runtime missing' >&2; exit 3; }
sha256sum "$runtime/kernel/"nv2a_{backend.h,pb_exec.c,vsh_interp.c,combiner.c,flip_hold.c} \
 "$root/game/src/nv2a_deko3d."{c,h} "$root/game/shaders/"nv2a_*.glsl \
 "$root/tests/nv2a-menu-proof/source/main.c" "$root/tests/nv2a-menu-proof/Makefile" "$0"
cppflags=()
if [[ "${MENU_PROOF_NO_RC:-0}" == 1 ]]; then cppflags+=(CPPFLAGS=-DMENU_PROOF_NO_RC); fi
docker run --rm --network none --cpus 1 --memory 1g \
 --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
 --mount "type=bind,src=$root/tests/nv2a-menu-proof,dst=/work" \
 --mount "type=bind,src=$root/game/src,dst=/module/src,readonly" \
 --mount "type=bind,src=$root/game/shaders,dst=/module/shaders,readonly" \
 --mount "type=bind,src=$runtime,dst=/upstream/src,readonly" \
 -w /work "$image" make -B -j1 XBOXRECOMP_DIR=/upstream GAME_DIR=/module "${cppflags[@]}" clean all
nro="$root/tests/nv2a-menu-proof/nv2a-menu-proof.nro"
sha256sum "$nro"
"$root/scripts/emu-run.sh" "$nro" NV2A-MENU-PROOF 120
