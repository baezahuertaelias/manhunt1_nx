#!/usr/bin/env bash
# Run an NRO headlessly in Eden and wait for a result line on the emulated
# debug output (svcOutputDebugString -> "Debug.Emulated" log lines).
#
# Usage: scripts/emu-run.sh <file.nro> <marker> [timeout-seconds]
#   The NRO must print "<marker> PASS ..." or "<marker> FAIL ...".
#   <marker> is matched literally.
# Exit: 0 PASS
#       1 FAIL       the app ran and reported failure (port failure)
#       2 NO-RESULT  no marker: emulator failure, or the app crashed/hung
#                    before reporting. Triage the log; do not count as either.
#       3 SETUP      runner setup error (bad arguments, image, files)
# Env:  EMU_GPU=1 passes /dev/dri for hardware Vulkan (default: llvmpipe).
#       EMU_LOG=<path> keeps the full emulator log (otherwise kept only on NO-RESULT).
#       EMU_DATA=<dir> mounts <dir> (writable) as Eden's user-data folder; the
#       emulated SD card (sdmc:/) is <dir>/sdmc.
#       EMU_SDMC_GAME=<dir> mounts <dir> read-only at sdmc:/manhunt (game data,
#       no copy needed; requires EMU_DATA).
# Marker "-": no result line is expected; run for the whole timeout and exit 0
#       if the emulator was still running at the end (2 if it stopped early).
set -uo pipefail
setup_fail() { echo "emu-run: setup: $*" >&2; exit 3; }

[ $# -ge 2 ] || setup_fail "usage: $0 <file.nro> <marker> [timeout]"
[ -f "$1" ] || setup_fail "no such file: $1"     # never let Docker create a missing bind source
nro="$(realpath -e "$1")" || setup_fail "cannot resolve $1"
marker="$2"; limit="${3:-120}"
[[ "$limit" =~ ^[0-9]+$ ]] || setup_fail "timeout must be a number"
CPUS="${CPUS:-$(nproc)}"   # all host cores by default (user decision 2026-10-26)
IMAGE="${EDEN_IMAGE:-dd-eden:local}"
IMAGE_ID="${EDEN_IMAGE_ID:-sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb}"
[ -n "$IMAGE_ID" ] || setup_fail "EDEN_IMAGE_ID must not be empty"

actual="$(docker image inspect --format '{{.Id}}' "$IMAGE" 2>/dev/null)"
[ "$actual" = "$IMAGE_ID" ] || setup_fail "$IMAGE is '${actual:-missing}', expected $IMAGE_ID"

mounts=()
if [ -n "${EMU_DATA:-}" ]; then
  [ -d "$EMU_DATA" ] || setup_fail "EMU_DATA is not a directory: $EMU_DATA"
  mkdir -p "$EMU_DATA/sdmc"
  mounts+=(--mount "type=bind,src=$(realpath -e "$EMU_DATA"),dst=/tmp/home/.local/share/eden")
  sd=/tmp/home/.local/share/eden/sdmc
  if [ -n "${EMU_SDMC_GAME:-}" ]; then
    [ -d "$EMU_SDMC_GAME" ] || setup_fail "EMU_SDMC_GAME is not a directory: $EMU_SDMC_GAME"
    mkdir -p "$EMU_DATA/sdmc/manhunt"
    mounts+=(--mount "type=bind,src=$(realpath -e "$EMU_SDMC_GAME"),dst=$sd/manhunt,readonly")
  fi
fi

gpu=()
if [ "${EMU_GPU:-0}" = 1 ]; then
  gid="$(stat -c %g /dev/dri/renderD128 2>/dev/null)" || setup_fail "EMU_GPU=1 but /dev/dri/renderD128 is missing"
  gpu=(--device /dev/dri --group-add "$gid")
fi

log="${EMU_LOG:-$(mktemp)}"
keep_log="${EMU_LOG:+1}"

docker run --rm --network none --cpus "$CPUS" --memory 6g --user "$(id -u):$(id -g)" "${gpu[@]}" \
  -e HOME=/tmp/home -e SDL_VIDEO_DRIVER=x11 -e MARKER="$marker" -e LIMIT="$limit" \
  "${mounts[@]}" --mount "type=bind,src=$nro,dst=/nro/app.nro,readonly" --entrypoint sh "$IMAGE" -c '
mkdir -p "$HOME/.config/eden" "$HOME/.local/share/eden" "$HOME/.cache"
xvfb-run -a -s "-screen 0 1280x720x24" /src/eden/build/linux-headless-debug/bin/eden-cli /nro/app.nro > /tmp/eden.log 2>&1 &
pid=$!
i=0
while [ $i -lt "$LIMIT" ] && kill -0 $pid 2>/dev/null; do
  grep -aqF "OutputDebugString: $MARKER PASS" /tmp/eden.log && break
  grep -aqF "OutputDebugString: $MARKER FAIL" /tmp/eden.log && break
  sleep 1; i=$((i + 1))
done
kill -0 $pid 2>/dev/null && echo "EMU-RUN: still running at end" > /tmp/alive   # not eden.log: Eden writes it without O_APPEND
pkill -f eden-cli 2>/dev/null; sleep 1
cat /tmp/eden.log; cat /tmp/alive 2>/dev/null' > "$log" 2>&1

sed -i 's/\x1b\[[0-9;]*m//g' "$log"
grep -aoE 'Eden Version: .*|Driver: .*' "$log" | sed 's/^/emu-run: /'
grep -aoE 'OutputDebugString: .*' "$log" | sed 's/^OutputDebugString: /  /'
if [ "$marker" = "-" ]; then
  if grep -aq "EMU-RUN: still running at end" "$log"; then
    echo "emu-run: ran ${limit}s (log: $log)"; exit 0
  fi
  echo "emu-run: NO-RESULT: emulator stopped before ${limit}s (log: $log)" >&2; exit 2
elif grep -aqF "OutputDebugString: $marker PASS" "$log"; then status=0
elif grep -aqF "OutputDebugString: $marker FAIL" "$log"; then status=1
else
  echo "emu-run: NO-RESULT: no '$marker' line within ${limit}s; emulator failure or app crash/hang (log: $log)" >&2
  grep -aE 'Critical|Error' "$log" | tail -5 >&2
  exit 2
fi
[ -n "$keep_log" ] || rm -f "$log"
exit $status
