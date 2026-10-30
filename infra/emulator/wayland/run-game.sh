#!/usr/bin/env bash
# Optional Intel Vulkan/Wayland runner. The default Xvfb runner is unchanged.
# API/exit codes match scripts/emu-run.sh. EMU_DATA is a writable Eden directory;
# EMU_LOG retains the complete compositor/emulator log. CPUS defaults to all cores.
set -uo pipefail
setup_fail() { echo "wayland-game: setup: $*" >&2; exit 3; }
[[ $# -ge 2 && $# -le 3 ]] || setup_fail 'usage: run-game.sh <file.nro> <marker> [seconds]'
[[ -f "$1" ]] || setup_fail "no such file: $1"
nro="$(realpath -e "$1")" || setup_fail "cannot resolve $1"
marker="$2"; limit="${3:-120}"
[[ -n "$marker" ]] || setup_fail 'marker must not be empty'
[[ "$limit" =~ ^[0-9]+$ && ${#limit} -le 5 ]] || setup_fail 'timeout must be 1..43200 seconds'
limit=$((10#$limit))
((limit >= 1 && limit <= 43200)) || setup_fail 'timeout must be 1..43200 seconds'
[[ -z "${EMU_SDMC_GAME:-}" ]] || setup_fail 'use writable EMU_DATA/sdmc/manhunt; EMU_SDMC_GAME is unsupported'
image=sha256:047af837b0c8cecb359fb7e7694cbfa01cf7c397bd5bb7555337ab01ffa7d788
docker image inspect "$image" >/dev/null 2>&1 || setup_fail "pinned image missing: $image"
gid="$(stat -c %g /dev/dri/renderD128 2>/dev/null)" || setup_fail 'renderD128 is missing'
mounts=()
data_mounted=0
if [[ -n "${EMU_DATA:-}" ]]; then
    [[ -d "$EMU_DATA" ]] || setup_fail "EMU_DATA is not a directory: $EMU_DATA"
    mkdir -p "$EMU_DATA/sdmc" || setup_fail 'cannot prepare EMU_DATA/sdmc'
    data="$(realpath -e "$EMU_DATA")" || setup_fail 'cannot resolve EMU_DATA'
    mounts+=(--mount "type=bind,src=$data,dst=/eden-data")
    data_mounted=1
fi
log="${EMU_LOG:-$(mktemp)}"
[[ -n "$log" ]] || setup_fail 'cannot allocate log'
: >"$log" || setup_fail "cannot create log: $log"
cpus="${CPUS:-$(nproc)}"
printf 'image=%s\n' "$image"
sha256sum "$nro" "$0"
docker run --rm --network none --cpus "$cpus" --memory 6g \
    --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
    --device /dev/dri/renderD128 --group-add "$gid" \
    "${mounts[@]}" --mount "type=bind,src=$nro,dst=/nro/app.nro,readonly" \
    -e HOME=/tmp/home -e XDG_RUNTIME_DIR=/tmp/runtime -e WAYLAND_DISPLAY=wayland-game \
    -e SDL_VIDEO_DRIVER=wayland -e VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json \
    -e MARKER="$marker" -e LIMIT="$limit" -e EDEN_DATA_MOUNTED="$data_mounted" --entrypoint sh "$image" -c '
mkdir -p "$HOME/.config/eden" "$HOME/.local/share" "$HOME/.cache" "$XDG_RUNTIME_DIR" || exit 3
if [ "$EDEN_DATA_MOUNTED" = 1 ]; then
    ln -s /eden-data "$HOME/.local/share/eden" || exit 3
else
    mkdir -p "$HOME/.local/share/eden" || exit 3
fi
chmod 700 "$XDG_RUNTIME_DIR" || exit 3
weston --backend=headless --renderer=gl --width=1280 --height=720 \
    --socket=wayland-game --no-config --idle-time=0 >/tmp/weston.log 2>&1 &
compositor=$!
emulator=
trap '\''[ -z "$emulator" ] || kill "$emulator" 2>/dev/null || true; kill "$compositor" 2>/dev/null || true'\'' EXIT
i=0
while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
    if ! kill -0 "$compositor" 2>/dev/null || [ "$i" -ge 10 ]; then
        cat /tmp/weston.log; exit 2
    fi
    sleep 1; i=$((i + 1))
done
printf "[Renderer]\nbackend=1\nvulkan_device=0\n" >/tmp/eden.ini
# The supervisor also bounds lifetime if marker polling or shutdown fails.
timeout --signal=TERM --kill-after=5 "$((LIMIT + 5))" \
    /src/eden/build/linux-headless-debug/bin/eden-cli --config /tmp/eden.ini /nro/app.nro >/tmp/eden.log 2>&1 &
emulator=$!
i=0
while [ "$i" -lt "$LIMIT" ] && kill -0 "$emulator" 2>/dev/null; do
    if [ "$MARKER" != "-" ]; then
        grep -aqF "OutputDebugString: $MARKER PASS" /tmp/eden.log && break
        grep -aqF "OutputDebugString: $MARKER FAIL" /tmp/eden.log && break
    fi
    sleep 1; i=$((i + 1))
done
if [ "$i" -eq "$LIMIT" ] && kill -0 "$emulator" 2>/dev/null; then
    echo "WAYLAND-GAME: alive at bound" >/tmp/alive
fi
kill "$emulator" 2>/dev/null || true
# No unbounded wait: after a short flush grace the container shuts down all children.
i=0
while kill -0 "$emulator" 2>/dev/null && [ "$i" -lt 6 ]; do
    sleep 1; i=$((i + 1))
done
kill -KILL "$emulator" 2>/dev/null || true
cat /tmp/weston.log /tmp/eden.log
[ ! -f /tmp/alive ] || cat /tmp/alive
' >"$log" 2>&1
docker_status=$?
sed -i 's/\x1b\[[0-9;]*m//g' "$log"
if ((docker_status == 125 || docker_status == 126 || docker_status == 127 || docker_status == 3)); then
    echo "wayland-game: SETUP (log: $log)" >&2; exit 3
fi
if ((docker_status != 0)) \
    || ! grep -aqF 'GL renderer: Mesa Intel(R) Graphics (ADL-N)' "$log" \
    || ! grep -aqF 'Report: Device: Intel(R) Graphics (ADL-N)' "$log" \
    || ! grep -aqF 'Report: Driver: Intel open-source Mesa driver' "$log"; then
    echo "wayland-game: NO-RESULT: Intel runner/device prerequisite failed (log: $log)" >&2; exit 2
fi
grep -aoE 'Eden Version: .*|Report: Device: .*|Report: Driver: .*' "$log" | sed 's/^/wayland-game: /'
grep -aoE 'OutputDebugString: .*' "$log" | sed 's/^OutputDebugString: /  /'
if [[ "$marker" == - ]]; then
    if ! grep -aqF 'WAYLAND-GAME: alive at bound' "$log"; then
        echo "wayland-game: NO-RESULT: stopped before bound (log: $log)" >&2; exit 2
    fi
    status=0
elif grep -aqF "OutputDebugString: $marker FAIL" "$log"; then status=1
elif grep -aqF "OutputDebugString: $marker PASS" "$log"; then status=0
else
    echo "wayland-game: NO-RESULT: no marker (log: $log)" >&2; exit 2
fi
printf 'wayland-game: result=%s (log: %s)\n' "$status" "$log"
[[ -n "${EMU_LOG:-}" ]] || rm -f "$log"
exit "$status"
