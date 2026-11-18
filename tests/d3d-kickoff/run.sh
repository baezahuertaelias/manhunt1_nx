#!/bin/sh
# Focused regression test for game/src/d3d_kickoff.c (replacement for the
# D3D KickOff at guest 0x00292500). Needs a lifted tree (game/build/gen) for
# recomp_types.h. Runs in the project recompiler image:
#   docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
#     --mount type=bind,src="$PWD",dst=/repo manhunt-recompiler:xboxrecomp-f0f8a46 \
#     sh /repo/tests/d3d-kickoff/run.sh
set -eu
out="${TMPDIR:-/tmp}/d3d-kickoff-test"
cc -O2 -std=gnu11 -w -I/repo/game/build/gen -I/repo/game/src -I/repo/game/build/xboxrecomp/include \
   -o "$out" /repo/tests/d3d-kickoff/kickoff_test.c /repo/game/src/d3d_kickoff.c -lpthread
"$out" fast
MANHUNT_KICK_ACK=0 "$out" orig
MANHUNT_KICK_ACK= "$out" fast
MANHUNT_KICK_ACK=1 "$out" fast
