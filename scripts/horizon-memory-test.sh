#!/usr/bin/env bash
# Synthetic ownership/failure tests: no game inputs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-host}"
case "$mode" in host|switch) ;; *) echo 'usage: horizon-memory-test.sh [host|switch]' >&2; exit 2;; esac
image=sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63
work="$root/tests/horizon-memory-proof"
mkdir -p "$work/build"
sha256sum "$root/game/patches/xboxrecomp/0011-horizon-memory-semantics.patch" "$work/host/test.c" "$work/source/"*.h "$work/source/wrappers.c" "$0"
docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
 --cap-drop ALL --security-opt no-new-privileges \
 --mount "type=bind,src=$root/game/patches/xboxrecomp,dst=/patches,readonly" \
 --mount "type=bind,src=$work,dst=/test" "$image" bash -ec '
cp -r /opt/xboxrecomp /tmp/runtime
for patch in /patches/*.patch; do git -C /tmp/runtime apply "$patch"; done
mkdir -p /test/build/runtime
cp /tmp/runtime/src/platform/{win32_compat.c,win32_compat.h,win32_compat_horizon.inc,xbox_winnt.h} /test/build/runtime/
cc -std=gnu11 -O2 -Wall -Wextra -pthread -I/test/host -I/test/build/runtime /test/host/test.c -o /test/build/host
/test/build/host
cc -std=gnu11 -O2 -Wall -Wextra -pthread -DHZ_TEST_CLOSE_BEFORE_LOCK -I/test/host -I/test/build/runtime /test/host/test.c -o /test/build/negative
ulimit -c 0
if /test/build/negative > /test/build/negative.log 2>&1; then echo "HZ-MEMORY-NEGATIVE FAIL"; exit 1; fi
grep -F "FAIL close-before-publication consumed a reference outside the registry lock" /test/build/negative.log
echo "HZ-MEMORY-NEGATIVE PASS pre-lock reference decrement detected"
'
sha256sum "$work/build/runtime/"*
if [ "$mode" = switch ]; then
 image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
 docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
  --cap-drop ALL --security-opt no-new-privileges --mount "type=bind,src=$work,dst=/test" \
  -w /test "$image" make -j1 BUILD=build/wrappers TARGET=horizon-memory-wrappers SOURCE=source/wrappers.c PLATFORM_DIR=build/runtime
 sha256sum "$work/horizon-memory-wrappers.nro"
 "${EMU_RUN_SCRIPT:-$root/scripts/emu-run.sh}" "$work/horizon-memory-wrappers.nro" HZ-MEMORY-WRAPPERS 120
fi
