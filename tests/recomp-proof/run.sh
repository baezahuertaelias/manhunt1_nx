#!/usr/bin/env bash
# Runs inside manhunt-recompiler (see scripts/recomp-proof.sh).
# /repo = project checkout, /xbe/default.xbe = read-only input,
# /opt/xboxrecomp = pinned toolkit. Generated code stays in build/ (gitignored).
set -euo pipefail
cd /opt/xboxrecomp
B=/repo/tests/recomp-proof/build
P=$B/pipeline
FUNCS="0x0001A9C0 0x00089CF0 0x000E3640 0x00205A00 0x00205D90"
mkdir -p "$P/logs"

# Pipeline outputs are cached per XBE hash. Logs may print certificate keys:
# they stay in build/ and must never be committed.
xbe_sha=$(sha256sum /xbe/default.xbe | cut -d' ' -f1)
if [ "$(cat "$P/xbe.sha256" 2>/dev/null)" != "$xbe_sha" ]; then
  rm -rf "$P/work" "$P/out"
  mkdir -p "$P/work" && cp /xbe/default.xbe "$P/work/default.xbe"
  X=$P/work/default.xbe
  python -m tools.xbe_parser "$X" --json "$P/work/default_analysis.json" > "$P/logs/parse.log" 2>&1
  python -m tools.disasm "$X" --text-only -v > "$P/logs/disasm.log" 2>&1
  python -m tools.func_id "$X" -v > "$P/logs/func_id.log" 2>&1
  python -m tools.abi_analysis "$X" -v > "$P/logs/abi.log" 2>&1
  for d in disasm func_id abi_analysis; do mkdir -p "$P/out/$d"; cp -r "tools/$d/output/." "$P/out/$d/"; done
  echo "$xbe_sha" > "$P/xbe.sha256"
fi

# Lift each function; the C follows the loader chatter on stdout.
# Cached by XBE hash + toolkit commit + function list (each lift reloads the DB).
lift_key="$xbe_sha $(git rev-parse HEAD) $FUNCS"
if [ "$(cat "$B/lifted.key" 2>/dev/null)" != "$lift_key" ]; then
  {
    echo '#define RECOMP_GENERATED_CODE'
    echo '#include "recomp_types.h"'
    for f in $FUNCS; do
      python -m tools.recomp "$P/work/default.xbe" -f "$f" \
        --disasm-dir "$P/out/disasm" --func-id-dir "$P/out/func_id" --abi-dir "$P/out/abi_analysis" \
        > "$P/logs/recomp_$f.log" 2>&1
      sed -n '/^\/\*\*/,$p' "$P/logs/recomp_$f.log"
    done
  } > "$B/lifted.c"
  echo "$lift_key" > "$B/lifted.key"
fi

python -I /repo/tests/recomp-proof/reference.py /repo /xbe/default.xbe "$B"

# Headers for the Switch build, which runs in the devkitA64 image.
rm -rf "$B/include" && mkdir -p "$B/include/runtime" "$B/include/kernel"
cp /opt/xboxrecomp/templates/runtime/*.h "$B/include/runtime/"
cp /opt/xboxrecomp/src/kernel/*.h "$B/include/kernel/"

# -ffp-contract=off: AArch64 GCC otherwise fuses a*b+c into fmadd, which
# rounds differently from the x87 sequence the lifter models.
CFLAGS="-O2 -std=gnu11 -fno-strict-aliasing -ffp-contract=off -I$B -I/opt/xboxrecomp/templates/runtime -I/opt/xboxrecomp/src/kernel"
SRC="/repo/tests/recomp-proof/harness.c $B/lifted.c"
gcc $CFLAGS -o "$B/harness-x86_64" $SRC -lm
aarch64-linux-gnu-gcc $CFLAGS -static -o "$B/harness-aarch64" $SRC -lm

"$B/harness-x86_64" /xbe/default.xbe > "$B/out-x86_64.txt" 2> "$B/arena-x86_64.txt"
qemu-aarch64 "$B/harness-aarch64" /xbe/default.xbe > "$B/out-aarch64.txt" 2> "$B/arena-aarch64.txt"

status=0
for t in x86_64 aarch64; do
  echo "$t: $(cat "$B/arena-$t.txt")"
  python -I /repo/tests/recomp-proof/compare.py "$B/reference.txt" "$B/out-$t.txt" || status=1
done
echo "result: $([ $status -eq 0 ] && echo PASS || echo FAIL)"
exit $status
