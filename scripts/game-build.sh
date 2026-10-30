#!/usr/bin/env bash
# Build the recompiled Manhunt for the POSIX host (milestone 2 bring-up).
#   1. copy the pinned xboxrecomp tree and apply game/patches/xboxrecomp/*.patch;
#   2. lift DEFAULT.xbe with the patched tools when its cache key changes;
#   3. cmake + ninja with all cores.
# Everything generated lands in game/build/ (gitignored: game-derived).
# "scripts/game-build.sh debug" builds generated code at -O0 into game/build/host-O0.
set -euo pipefail
IMAGE="${RECOMPILER_IMAGE:-manhunt-recompiler:xboxrecomp-f0f8a46}"
CPUS="${CPUS:-$(nproc)}"
root="$(cd "$(dirname "$0")/.." && pwd)"
xbe="${XBE:-$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}/DEFAULT.xbe}"
[ -f "$xbe" ] || { echo "game-build: XBE not found: $xbe" >&2; exit 3; }
mkdir -p "$root/game/build/logs"
variant="${1:-release}"
case "$variant" in release) bdir=host; gopt=-O1 ;; debug) bdir=host-O0; gopt=-O0 ;; switch) bdir=prep; gopt=-O1 ;; *) echo "usage: $0 [release|debug|switch]" >&2; exit 3 ;; esac
DEVKITA64_IMAGE="${DEVKITA64_IMAGE:-devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282}"

docker run --rm --network none --hostname localhost --cpus "$CPUS" --memory 12g \
  --user "$(id -u):$(id -g)" -e CPUS="$CPUS" -e BDIR="$bdir" -e GOPT="$gopt" \
  --mount "type=bind,src=$root,dst=/repo" \
  --mount "type=bind,src=$(realpath -e "$xbe"),dst=/xbe/default.xbe,readonly" \
  "$IMAGE" bash -c '
set -euo pipefail
G=/repo/game/build
P=$G/pipeline
rm -rf "$G/xboxrecomp" && cp -r /opt/xboxrecomp "$G/xboxrecomp"
for p in /repo/game/patches/xboxrecomp/*.patch; do
  git -C "$G/xboxrecomp" apply "$p"
  echo "applied $(basename "$p")"
done
# Analysis and lifting must import the patched tools, not the image copy.
cd "$G/xboxrecomp"
# Full analysis of every executable section. --text-only (the getting-started
# default) leaves the statically linked XDK sections (DSOUND, D3D, XGRPH, XMV,
# DOLBY, ...) almost undiscovered, and boot dies on unresolved indirect calls.
if [ ! -f "$P/out/abi_analysis/abi_functions.json" ]; then
  rm -rf "$P" "$G/gen" && mkdir -p "$P/work" "$P/out"
  cp /xbe/default.xbe "$P/work/default.xbe"
  X=$P/work/default.xbe
  python -m tools.xbe_parser "$X" --json "$P/work/default_analysis.json" > "$G/logs/parse.log" 2>&1
  python -m tools.disasm "$X" -v > "$G/logs/disasm.log" 2>&1
  python -m tools.func_id "$X" -v > "$G/logs/func_id.log" 2>&1
  python -m tools.abi_analysis "$X" -v > "$G/logs/abi.log" 2>&1
  for d in disasm func_id abi_analysis; do mkdir -p "$P/out/$d"; cp -r "tools/$d/output/." "$P/out/$d/"; done
fi
# Re-lift when the hand-written function list or the false-entry repairs change.
# function_fixes.json: detected starts that fall inside an instruction of the
# real owner (reports/FEASIBILITY.md), removed before translation.
manual_key=$(cat /repo/game/manual_functions.json /repo/game/function_fixes.json /repo/scripts/fix-functions.py /repo/game/patches/xboxrecomp/*.patch | sha256sum | cut -d" " -f1)
if [ "$(cat "$G/gen/.manual.sha256" 2>/dev/null)" != "$manual_key" ]; then rm -rf "$G/gen"; fi
if [ ! -f "$G/gen/recomp_dispatch.c" ]; then
  mkdir -p "$G/gen"
  python -I /repo/scripts/fix-functions.py "$P/out/disasm/functions.json" /repo/game/function_fixes.json "$G/functions.fixed.json"
  python -m tools.recomp "$P/work/default.xbe" --all --split 250 --game-name Manhunt --manual-functions /repo/game/manual_functions.json --functions "$G/functions.fixed.json" --gen-dir "$G/gen" \
    --disasm-dir "$P/out/disasm" --func-id-dir "$P/out/func_id" --abi-dir "$P/out/abi_analysis" \
    > "$G/logs/lift.log" 2>&1 || { tail -5 "$G/logs/lift.log"; exit 1; }
  echo "$manual_key" > "$G/gen/.manual.sha256"
fi
[ "$BDIR" = prep ] && exit 0
cmake -S /repo/game -B "$G/$BDIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMANHUNT_GEN_OPT="$GOPT" > "$G/logs/cmake-$BDIR.log"
if ! cmake --build "$G/$BDIR" -j "$CPUS" > "$G/logs/build-$BDIR.log" 2>&1; then tail -40 "$G/logs/build-$BDIR.log"; exit 1; fi
ls -la "$G/$BDIR/manhunt"
'

# Switch: compile the same lifted code and the runtime subset with devkitA64.
if [ "$variant" = switch ]; then
  docker run --rm --network none --cpus "$CPUS" --memory 12g --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$root,dst=/repo" -w /repo/game "$DEVKITA64_IMAGE" bash -c '
set -euo pipefail
cmake -S . -B build/switch -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
  -DCMAKE_BUILD_TYPE=Release > build/logs/cmake-switch.log
if ! make -C build/switch -k -j'"$CPUS"' > build/logs/build-switch.log 2>&1; then tail -60 build/logs/build-switch.log; exit 1; fi
ls -la build/switch/manhunt.nro'
fi
