#!/usr/bin/env bash
# Synthetic NRO proof on Intel Vulkan/Wayland; existing Eden runner is untouched.
set -euo pipefail
[[ $# == 1 && -f "$1" ]] || { echo "usage: $0 deko3d-proof.nro" >&2; exit 3; }
nro="$(realpath -e "$1")"
image=sha256:047af837b0c8cecb359fb7e7694cbfa01cf7c397bd5bb7555337ab01ffa7d788
docker image inspect "$image" >/dev/null
printf 'image=%s\n' "$image"
sha256sum "$nro" "$0"
docker run --rm --network none --cpus 1 --memory 6g \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --device /dev/dri/renderD128 --group-add "$(stat -c %g /dev/dri/renderD128)" \
  --mount "type=bind,src=$nro,dst=/proof.nro,readonly" \
  -e HOME=/tmp/home -e XDG_RUNTIME_DIR=/tmp/runtime -e WAYLAND_DISPLAY=wayland-proof \
  -e SDL_VIDEO_DRIVER=wayland -e VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json \
  --entrypoint sh "$image" -ec '
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless --renderer=gl --width=1280 --height=720 \
  --socket=wayland-proof --no-config --idle-time=0 >/tmp/weston.log 2>&1 &
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
/src/eden/build/linux-headless-debug/bin/eden-cli --config /tmp/eden.ini /proof.nro >/tmp/eden.log 2>&1 &
emulator=$!
i=0
while [ "$i" -lt 120 ] && kill -0 "$emulator" 2>/dev/null; do
  grep -aqE "OutputDebugString: DEKO3D-PROOF (PASS|FAIL)" /tmp/eden.log && break
  sleep 1; i=$((i + 1))
done
kill "$emulator" 2>/dev/null || true
wait "$emulator" 2>/dev/null || true
cat /tmp/weston.log /tmp/eden.log
grep -qF "GL renderer: Mesa Intel(R) Graphics (ADL-N)" /tmp/weston.log || exit 2
grep -aqE "OutputDebugString: DEKO3D-PROOF FAIL" /tmp/eden.log && exit 1
grep -aqF "OutputDebugString: DEKO3D-PROOF PASS" /tmp/eden.log || exit 2
grep -aqF "Report: Device: Intel(R) Graphics (ADL-N)" /tmp/eden.log || exit 2
grep -aqF "Report: Driver: Intel open-source Mesa driver" /tmp/eden.log || exit 2
printf "EMU-GPU-PRESENT PASS\n"'
