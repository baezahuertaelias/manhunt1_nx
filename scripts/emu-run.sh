#!/usr/bin/env bash
# Run an NRO headlessly in Eden and wait for a result line on the emulated
# debug output (svcOutputDebugString -> "Debug.Emulated" log lines).
#
# Usage: scripts/emu-run.sh <file.nro> <marker> [timeout-seconds]
#   The NRO must print "<marker> PASS ..." or "<marker> FAIL ...".
# Exit: 0 PASS, 1 FAIL, 2 no result (timeout/crash), 3 runner setup error.
# Env:  EMU_GPU=1 passes /dev/dri for hardware Vulkan (default: llvmpipe).
#       EMU_LOG=<path> keeps the full emulator log.
#
# Emulator failures (exit 2/3) are runner failures, not port failures.
set -euo pipefail
nro="$(realpath "$1")"; marker="$2"; limit="${3:-120}"
IMAGE="${EDEN_IMAGE:-dd-eden:local}"
IMAGE_ID="${EDEN_IMAGE_ID:-sha256:68d0f0847e3f}"
log="${EMU_LOG:-$(mktemp)}"

actual="$(docker image inspect --format '{{.Id}}' "$IMAGE" 2>/dev/null || true)"
case "$actual" in
  "$IMAGE_ID"*) ;;
  *) echo "emu-run: $IMAGE is '${actual:-missing}', expected $IMAGE_ID" >&2; exit 3 ;;
esac

gpu=()
[ "${EMU_GPU:-0}" = 1 ] && gpu=(--device /dev/dri --group-add "$(stat -c %g /dev/dri/renderD128)")

set +e
docker run --rm --network none --cpus 3 --memory 6g --user "$(id -u):$(id -g)" "${gpu[@]}" \
  -e HOME=/tmp/home -e SDL_VIDEO_DRIVER=x11 -e MARKER="$marker" -e LIMIT="$limit" \
  -v "$nro:/nro/app.nro:ro" --entrypoint sh "$IMAGE" -c '
mkdir -p "$HOME/.config/eden" "$HOME/.local/share/eden" "$HOME/.cache"
xvfb-run -a -s "-screen 0 1280x720x24" /src/eden/build/linux-headless-debug/bin/eden-cli /nro/app.nro > /tmp/eden.log 2>&1 &
pid=$!
i=0
while [ $i -lt "$LIMIT" ] && kill -0 $pid 2>/dev/null; do
  grep -aqE "OutputDebugString: $MARKER (PASS|FAIL)" /tmp/eden.log && break
  sleep 1; i=$((i + 1))
done
pkill -f eden-cli 2>/dev/null; sleep 1
cat /tmp/eden.log' > "$log" 2>&1
set -e

sed -i 's/\x1b\[[0-9;]*m//g' "$log"
grep -aoE 'Eden Version: .*|Driver: .*' "$log" | sed 's/^/emu-run: /' || true
grep -aoE 'OutputDebugString: .*' "$log" | sed 's/^OutputDebugString: /  /' || true
if grep -aqE "OutputDebugString: $marker PASS" "$log"; then exit 0; fi
if grep -aqE "OutputDebugString: $marker FAIL" "$log"; then exit 1; fi
echo "emu-run: no '$marker' result within ${limit}s (runner failure; log: $log)" >&2
grep -aE 'Critical|Error' "$log" | tail -5 >&2 || true
exit 2
