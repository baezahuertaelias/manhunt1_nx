#!/bin/sh
# LEVEL-LOAD-001 regression: D3D_BlockOnTime (game/src/d3d_fence.c) must wait
# for the GPU semaphore while the executor is enabled. Needs a lifted tree
# (game/build/gen) for recomp_types.h. Runs in the project recompiler image:
#   docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
#     --mount type=bind,src="$PWD",dst=/repo manhunt-recompiler:xboxrecomp-f0f8a46 \
#     sh /repo/tests/d3d-fence/run.sh
set -eu
out="${TMPDIR:-/tmp}/d3d-fence-test"
cc -O2 -std=gnu11 -w -I/repo/game/build/gen -I/repo/game/build/xboxrecomp/include \
   -o "$out" /repo/tests/d3d-fence/fence_test.c /repo/game/src/d3d_fence.c -lpthread
"$out"
"$out" noexec
