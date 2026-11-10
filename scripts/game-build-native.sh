#!/usr/bin/env bash
# Native (no Docker) build of the Switch NRO on a host with devkitPro (macOS/Linux).
# Same steps as "scripts/game-build.sh switch": pinned xboxrecomp + patches,
# analysis, lift, then cmake with the devkitA64 toolchain.
#   scripts/game-build-native.sh          # analysis (cached) + lift (cached) + NRO
# Needs: devkitPro at $DEVKITPRO (default /opt/devkitpro), cmake, make, git, patch,
# python3 (a venv with capstone==5.0.9 unicorn==2.1.4 is created in game/build/venv).
# Everything generated lands in game/build/ (gitignored: game-derived).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$PATH"
CPUS="${CPUS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc)}"
COMMIT=f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b
xbe="${XBE:-$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}/DEFAULT.xbe}"
[ -f "$xbe" ] || { echo "game-build-native: XBE not found: $xbe" >&2; exit 3; }
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || { echo "game-build-native: devkitPro Switch toolchain not found in $DEVKITPRO" >&2; exit 3; }
sha() { shasum -a 256 "$@" 2>/dev/null || sha256sum "$@"; }
G="$root/game/build"; P="$G/pipeline"
mkdir -p "$G/logs"

# 1. pinned lifter source + venv (cached)
if [ "$(git -C "$G/xboxrecomp-src" rev-parse HEAD 2>/dev/null)" != "$COMMIT" ]; then
  rm -rf "$G/xboxrecomp-src"; git init -q "$G/xboxrecomp-src"
  git -C "$G/xboxrecomp-src" fetch -q --depth 1 https://github.com/sp00nznet/xboxrecomp.git "$COMMIT"
  git -C "$G/xboxrecomp-src" checkout -q FETCH_HEAD
  [ "$(git -C "$G/xboxrecomp-src" rev-parse HEAD)" = "$COMMIT" ]
fi
if [ ! -x "$G/venv/bin/python" ]; then
  python3 -m venv "$G/venv"; "$G/venv/bin/pip" install -q capstone==5.0.9 unicorn==2.1.4
fi
export PATH="$G/venv/bin:$PATH"

# 2. patched copy (always refreshed: patches are small and cheap)
rm -rf "$G/xboxrecomp" && cp -r "$G/xboxrecomp-src" "$G/xboxrecomp"
for p in "$root"/game/patches/xboxrecomp/*.patch; do git -C "$G/xboxrecomp" apply "$p"; done

# 3. analysis (cached by existence of abi_functions.json)
cd "$G/xboxrecomp"
if [ ! -f "$P/out/abi_analysis/abi_functions.json" ]; then
  rm -rf "$P" "$G/gen" && mkdir -p "$P/work" "$P/out"
  cp "$xbe" "$P/work/default.xbe"; X="$P/work/default.xbe"
  python -m tools.xbe_parser "$X" --json "$P/work/default_analysis.json" > "$G/logs/parse.log" 2>&1
  python -m tools.disasm "$X" -v > "$G/logs/disasm.log" 2>&1
  python -m tools.func_id "$X" -v > "$G/logs/func_id.log" 2>&1
  python -m tools.abi_analysis "$X" -v > "$G/logs/abi.log" 2>&1
  for d in disasm func_id abi_analysis; do mkdir -p "$P/out/$d"; cp -r "tools/$d/output/." "$P/out/$d/"; done
fi

# 4. lift (cached by the same key as game-build.sh)
manual_key=$(cat "$root"/game/manual_functions.json "$root"/game/function_fixes.json "$root"/scripts/fix-functions.py "$root"/game/patches/xboxrecomp/*.patch | sha -  | cut -d" " -f1)
if [ "$(cat "$G/gen/.manual.sha256" 2>/dev/null)" != "$manual_key" ]; then rm -rf "$G/gen"; fi
if [ ! -f "$G/gen/recomp_dispatch.c" ]; then
  mkdir -p "$G/gen"
  python -I "$root/scripts/fix-functions.py" "$P/out/disasm/functions.json" "$root/game/function_fixes.json" "$G/functions.fixed.json"
  python -m tools.recomp "$P/work/default.xbe" --all --split 250 --game-name Manhunt \
    --manual-functions "$root/game/manual_functions.json" --functions "$G/functions.fixed.json" --gen-dir "$G/gen" \
    --disasm-dir "$P/out/disasm" --func-id-dir "$P/out/func_id" --abi-dir "$P/out/abi_analysis" \
    > "$G/logs/lift.log" 2>&1 || { tail -5 "$G/logs/lift.log"; exit 1; }
  echo "$manual_key" > "$G/gen/.manual.sha256"
fi

# 5. Switch NRO (optional icon: game/icon.jpg -> 256x256 JPEG, macOS sips)
rm -f "$G/icon256.jpg"
if [ -f "$root/game/icon.jpg" ] && command -v sips >/dev/null 2>&1; then
  sips -z 256 256 "$root/game/icon.jpg" --out "$G/icon256.jpg" >/dev/null
fi
cd "$root/game"
cmake -S . -B build/switch -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DMANHUNT_GEN_OPT="${MANHUNT_GEN_OPT:--O2}" > build/logs/cmake-switch.log
if ! make -C build/switch -k -j"$CPUS" > build/logs/build-switch.log 2>&1; then tail -60 build/logs/build-switch.log; exit 1; fi
ls -la build/switch/manhunt.nro
sha build/switch/manhunt.nro
