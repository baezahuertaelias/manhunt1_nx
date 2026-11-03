#!/usr/bin/env bash
# Differential test of the real game/src/nv2a_deko3d.c on the host (see test_adapter.c), then MUTATION
# TESTING of the test itself: deliberately broken copies of the adapter must be caught.
# Needs the patched runtime headers (game/build/xboxrecomp, made by scripts/game-build-native.sh) and the
# deko3d header from devkitPro (/opt/devkitpro/libnx/include).   usage: tests/deko-adapter/run.sh [seeds]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
seeds="${1:-30}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cflags=(-std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -I "$root/tests/deko-adapter/stub" -I /opt/devkitpro/libnx/include -I "$root/game/src"
        -I "$root/game/build/xboxrecomp/src/kernel")
build() {  # extra flags..., output
  local out="${@: -1}"; local extra=("${@:1:$#-1}")
  clang "${cflags[@]}" ${extra[@]+"${extra[@]}"} "$root/tests/deko-adapter/test_adapter.c" "$root/tests/deko-adapter/stub_deko.c" -o "$out" -lm
}
echo "== real adapter vs conservative reference ($seeds seeds)"
build "$tmp/test"
"$tmp/test" "$seeds" 2>&1 | grep -v '^\[MH-DEKO'

echo; echo "== mutation test: each deliberately broken adapter MUST be caught"
python3 -I - "$root" "$tmp" <<'PY'
import sys, re
root, tmp = sys.argv[1], sys.argv[2]
src = open(root + "/game/src/nv2a_deko3d.c").read()
mutants = {
 "M1 rectangle too small (maxx margin)":        ("ceilf(maxx) + 2.f", "ceilf(maxx) - 1.f"),
 "M2 texture hash covers only the first row":   ("for (uint32_t y = 0; y < t->height; y++)\n            h = hash_bytes(h, guest + t->offset + (size_t)y * t->pitch, (size_t)t->width * 4);", "for (uint32_t y = 0; y < 1; y++)\n            h = hash_bytes(h, guest + t->offset + (size_t)y * t->pitch, (size_t)t->width * 4);"),
 "M3 CPU-rejected clear does not invalidate":   ("if (!ok) { s_stats.rejected_clears++; resident_invalidate(SYNC_REJECT); }", "if (!ok) { s_stats.rejected_clears++; }"),
 "M4 no flush before a texture overlapping the surface": ("resident_flush_dirty(SYNC_OVERLAP);", "(void)0;"),
 "M5 surface change not noticed":               ("int same = R.valid && R.va == s->color_va && R.pitch == s->pitch && R.w == w && R.h == h && R.pix0 == o->pix0;", "int same = R.valid;"),
 "M6 dirty rectangle never grows":              ("            if (x0 < R.dx0) R.dx0 = x0;\n            if (y0 < R.dy0) R.dy0 = y0;\n            if (x1 > R.dx1) R.dx1 = x1;\n            if (y1 > R.dy1) R.dy1 = y1;", "            (void)x0; (void)y0; (void)x1; (void)y1;"),
 "M7 texture cache ignores the palette/hash (always a hit)": ("hit = e->hash == hsh && e->hash != 0;", "hit = e->hash != 0;"),
 "M8 present sync does nothing":                ("if (s_device) resident_flush_dirty(SYNC_PRESENT);", "(void)0;"),
}
import json
json.dump({}, open(tmp + "/mutants.json", "w"))
for i, (name, (a, b)) in enumerate(mutants.items(), 1):
    if src.count(a) != 1:
        print("MUTANT %s: pattern not found exactly once (%d): the test script is out of date" % (name, src.count(a))); sys.exit(3)
    open("%s/m%d.c" % (tmp, i), "w").write(src.replace(a, b, 1))
open(tmp + "/names.txt", "w").write("\n".join(mutants.keys()) + "\n")
PY
i=0; survived=0
while IFS= read -r name; do
  i=$((i+1))
  build "-DADAPTER_SRC=\"$tmp/m$i.c\"" -I "$root/game/src" "$tmp/mut$i"
  if "$tmp/mut$i" "$seeds" 2>&1 | grep -v '^\[MH-DEKO' | grep -q "RESULT: all passed"; then
    echo "SURVIVED  $name   <-- the test did NOT catch this bug"; survived=$((survived+1))
  else
    echo "killed    $name"
  fi
done < "$tmp/names.txt"
echo; if [ "$survived" = 0 ]; then echo "MUTATION RESULT: every planted bug was caught"; else echo "MUTATION RESULT: $survived planted bug(s) survived"; exit 1; fi
