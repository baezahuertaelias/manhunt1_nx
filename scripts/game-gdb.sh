#!/usr/bin/env bash
# Run the host build under gdb in batch mode with a command file.
# Usage: scripts/game-gdb.sh <cmd.gdb> [seconds]   (output: lines starting with ===)
# Uses the same runtime switches as the current boot configuration.
# GAME_BUILD_DIR=host-O0 runs the -O0 debug build (exact register values).
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cmd="$(realpath -e "$1")"; secs="${2:-60}"
case "$cmd" in "$root"/*) ;; *) echo "cmd file must be inside the repo" >&2; exit 3 ;; esac
docker run --rm --network none --hostname localhost --cpus "${CPUS:-$(nproc)}" --memory 8g \
  --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -e RECOMP_TITLE_KEVENTS=1 -e RECOMP_VBLANK=paced -e RECOMP_DISPATCH_GATE=1 \
  -e RECOMP_FLIP_PACING=1 -e RECOMP_PB_EXEC=1 -e RECOMP_PB_WALK=dma \
  -e MANHUNT_SAVE_DIR=/repo/game/build/save \
  --mount "type=bind,src=$root,dst=/repo" \
  --mount "type=bind,src=$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe},dst=/game,readonly" \
  manhunt-recompiler:xboxrecomp-f0f8a46 bash -c \
  "cd /repo/game/build/${GAME_BUILD_DIR:-host} && timeout $secs xvfb-run -a gdb -q -batch -x /repo/${cmd#$root/} ./manhunt 2>&1 | grep -aE '===|^#'"
