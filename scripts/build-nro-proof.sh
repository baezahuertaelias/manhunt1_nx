#!/usr/bin/env bash
# Build tests/nro-proof inside the pinned devkitA64 image and print the NRO hash.
set -euo pipefail
IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"
root="$(cd "$(dirname "$0")/.." && pwd)"
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$root/tests/nro-proof:/work" -w /work \
  "$IMAGE" make clean all
sha256sum "$root/tests/nro-proof/nro-proof.nro"
