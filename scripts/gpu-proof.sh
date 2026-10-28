#!/usr/bin/env bash
# Standalone container hardware-GPU readback; does not test Eden presentation.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb
docker image inspect "$image" >/dev/null
[[ -c /dev/dri/renderD128 ]] || { echo 'GPU-PROOF SETUP: missing renderD128' >&2; exit 3; }
printf 'image=%s commit=%s\n' "$image" "$(git -C "$root" rev-parse HEAD)"
sha256sum "$root/tests/gpu-proof/main.c" "$root/scripts/gpu-proof.sh"
docker run --rm --network none --cpus 1 --memory 512m \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --device /dev/dri/renderD128 --group-add "$(stat -c %g /dev/dri/renderD128)" \
  --mount "type=bind,src=$root/tests/gpu-proof,dst=/proof,readonly" \
  -e HOME=/tmp/home --entrypoint sh "$image" -ec '
mkdir -p "$HOME"
clang-19 --version | head -1
pkg-config --modversion egl gbm gl
clang-19 -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror /proof/main.c -o /tmp/gpu-proof $(pkg-config --cflags --libs egl gbm gl)
/tmp/gpu-proof --self-test
timeout 15 /tmp/gpu-proof'
