#!/usr/bin/env bash
# Run the recompiled Manhunt (POSIX host build) headlessly under Xvfb.
# Usage: scripts/game-run.sh [seconds]   (default 90)
# Any RECOMP_* / XBOX_* variables in the caller's environment are passed in.
# Game data is mounted read-only; saves, logs and frame dumps go to game/build/.
set -uo pipefail
IMAGE="${RECOMPILER_IMAGE:-manhunt-recompiler:xboxrecomp-f0f8a46}"
CPUS="${CPUS:-$(nproc)}"
root="$(cd "$(dirname "$0")/.." && pwd)"
data="${GAME_DATA:-$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}}"
secs="${1:-90}"
bin="$root/game/build/host/manhunt"
[ -x "$bin" ] || { echo "game-run: build first (scripts/game-build.sh)" >&2; exit 3; }
[ -f "$data/DEFAULT.xbe" ] || { echo "game-run: no DEFAULT.xbe in $data" >&2; exit 3; }
stamp="$(date +%Y%m%d-%H%M%S)"
log="$root/game/build/logs/run-$stamp.log"
mkdir -p "$root/game/build/save" "$root/game/build/frames/$stamp"

envs=()
while IFS='=' read -r k _; do envs+=(-e "$k"); done < <(env | grep -E '^(RECOMP|XBOX)_')

docker run --rm --network none --hostname localhost --cpus "$CPUS" --memory 8g \
  --user "$(id -u):$(id -g)" -e HOME=/tmp -e SECS="$secs" \
  -e MANHUNT_SAVE_DIR=/repo/game/build/save \
  -e RECOMP_FB_DUMP="${RECOMP_FB_DUMP:-/repo/game/build/frames/$stamp/fb}" "${envs[@]}" \
  --mount "type=bind,src=$root,dst=/repo" \
  --mount "type=bind,src=$(realpath -e "$data"),dst=/game,readonly" \
  "$IMAGE" bash -c '
cd /repo/game/build/host
timeout --signal=INT --kill-after=10 "$SECS" xvfb-run -a -s "-screen 0 1280x720x24" ./manhunt /game
echo "[MH-RUN] exit=$?"' > "$log" 2>&1
status=$(grep -ao '\[MH-RUN\] exit=[0-9]*' "$log" | tail -1)
echo "game-run: log $log ($status)"
grep -aE '^\[MH-BOOT\]|\[CRASH\]|\[UNIMPL\]|MISSING|Missing' "$log" | head -40
