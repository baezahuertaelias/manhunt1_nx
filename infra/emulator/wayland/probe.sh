#!/usr/bin/env bash
# Inspect Intel Vulkan WSI using an optional local compositor, without Eden.
set -euo pipefail
image=sha256:047af837b0c8cecb359fb7e7694cbfa01cf7c397bd5bb7555337ab01ffa7d788
docker image inspect "$image" >/dev/null
printf 'image=%s commit=%s\n' "$image" "$(git -C "$(dirname "$0")" rev-parse HEAD)"
sha256sum "$(dirname "$0")/Dockerfile" "$(dirname "$0")/build.sh" "$0"
docker run --rm --network none --cpus 1 --memory 1g \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --device /dev/dri/renderD128 --group-add "$(stat -c %g /dev/dri/renderD128)" \
  -e HOME=/tmp/home -e XDG_RUNTIME_DIR=/tmp/runtime -e WAYLAND_DISPLAY=wayland-proof \
  -e VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json \
  --entrypoint sh "$image" -ec '
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless --renderer=gl --width=1280 --height=720 \
  --socket=wayland-proof --no-config --idle-time=0 >/tmp/weston.log 2>&1 &
pid=$!
trap "kill $pid 2>/dev/null || true" EXIT
i=0
while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
  if ! kill -0 "$pid" 2>/dev/null || [ "$i" -ge 10 ]; then
    cat /tmp/weston.log; exit 1
  fi
  sleep 1; i=$((i + 1))
done
timeout 15 vulkaninfo >/tmp/vulkan.txt 2>&1
grep -F "GL renderer: Mesa Intel(R) Graphics (ADL-N)" /tmp/weston.log
grep -F "Surface type = VK_KHR_wayland_surface" /tmp/vulkan.txt
grep -F "deviceName        = Intel(R) Graphics (ADL-N)" /tmp/vulkan.txt
grep -E "present support[[:space:]]*=[[:space:]]*true" /tmp/vulkan.txt
sha256sum /usr/local/share/manhunt-wayland-packages.txt
printf "EMU-GPU-WSI PASS (Eden execution UNVERIFIED)\n"'
