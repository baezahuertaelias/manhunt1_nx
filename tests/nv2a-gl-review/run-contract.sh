#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="${GL_REVIEW_RUNTIME:-$root/game/build/xboxrecomp/src/kernel}"
executor="${GL_REVIEW_EXECUTOR:-$runtime/nv2a_pb_exec.c}"
xr="${GL_REVIEW_XR:-$root/game/build/xboxrecomp}"
out="${GL_REVIEW_OUT:-$root/game/build/gl-review-tests}"
mkdir -p "$out"
# Compile the complete maintained executor, never a copied registration excerpt.
cc -std=gnu11 -O0 -ffunction-sections -fdata-sections \
  -I"$runtime" -I"$xr/src/kernel" -I"$xr/include" -I"$xr/src" -I"$xr/src/platform" \
  -c "$executor" -o "$out/contract-executor.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime" \
  "$root/tests/nv2a-gl-review/test_backend_contract.c" "$out/contract-executor.o" \
  -Wl,--gc-sections -lm -pthread -o "$out/test-backend-contract"
ulimit -c 0
timeout 30s "$out/test-backend-contract"
