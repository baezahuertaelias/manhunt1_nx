#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$root/game/build/xboxrecomp"
out="$root/game/build/npc-cs-cache"
mkdir -p "$out"
# Build an isolated patched owning source, without mutating the lift/build tree.
cp "$runtime/src/kernel/kernel_rtl.c" "$out/kernel_rtl.c"
if ! grep -q MANHUNT_CS_CACHE "$out/kernel_rtl.c"; then
  patch -s "$out/kernel_rtl.c" "$root/game/patches/xboxrecomp/0031-critical-section-resolver-cache.patch"
fi
cc -std=c11 -O2 -ffunction-sections -fdata-sections -Wno-unused-parameter \
  -I"$runtime/include" -I"$runtime/src" -I"$runtime/src/kernel" -I"$runtime/src/platform" \
  "$root/tests/npc-cs-cache/test_cache.c" "$runtime/src/platform/win32_compat.c" \
  -Wl,--gc-sections -pthread -lm -o "$out/test-cache"
timeout 30s "$out/test-cache" 0
timeout 30s "$out/test-cache" 1
RECOMP_CS_MODE=single timeout 30s "$out/test-cache" 1 single
