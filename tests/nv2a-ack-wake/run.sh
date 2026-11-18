#!/bin/sh
# Host test for the ack-loop wake on kick (patch 0038, nv2a_ack_wake.h).
# Needs the patched runtime tree (game/build/xboxrecomp). Runs in the
# project recompiler image:
#   docker run --rm --user 0 -v "$PWD:/repo" manhunt-recompiler:xboxrecomp-f0f8a46 \
#     sh /repo/tests/nv2a-ack-wake/run.sh
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${TMPDIR:-/tmp}/nv2a-ack-wake-test"
cc -O2 -std=gnu11 -Wall -Wextra -Werror -I"$root/game/build/xboxrecomp/src/kernel" \
   -o "$out" "$root/tests/nv2a-ack-wake/ack_wake_test.c" -lpthread
"$out"
