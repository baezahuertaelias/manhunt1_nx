#!/usr/bin/env bash
# Differential test of the real game/src/nv2a_deko3d.c on the host (see test_adapter.c), then MUTATION
# TESTING of the test itself: deliberately broken copies of the adapter must be caught.
# Needs the patched runtime headers (game/build/xboxrecomp, made by scripts/game-build-native.sh) and the
# deko3d header from devkitPro (/opt/devkitpro/libnx/include).   usage: tests/deko-adapter/run.sh [seeds]
set -euo pipefail
# LeakSanitizer exits without flushing stdout, which would hide the RESULT line that the mutation check reads.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"
root="$(cd "$(dirname "$0")/../.." && pwd)"
seeds="${1:-30}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cflags=(-std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
        -I "$root/tests/deko-adapter/stub" -I /opt/devkitpro/libnx/include -I "$root/game/src"
        -I "$root/game/build/xboxrecomp/src/kernel" -I "$root/game/build/xboxrecomp/src")
build() {  # extra flags..., output
  local out="${@: -1}"; local extra=("${@:1:$#-1}")
  clang "${cflags[@]}" ${extra[@]+"${extra[@]}"} "$root/tests/deko-adapter/test_adapter.c" "$root/tests/deko-adapter/stub_deko.c" -o "$out" -lm
}
# Old clang's ASan can segfault at start under high-entropy ASLR, before printing anything (measured: 9 of 40
# runs, clang 14 in Docker Desktop). That is not a test result, and it would make a mutant look caught: retry it.
run_test() {  # binary seeds
  local out rc try
  for try in 1 2 3 4 5 6 7 8 9 10; do
    # A hang (rc 124) ends with no verdict: INCONCLUSIVE. timeout's own core-dump message is not test output.
    rc=0; out="$(timeout 600 "$1" "$2" 2>&1)" || rc=$?
    out="${out#timeout: the monitored command dumped core}"
    if [ "$rc" != 139 ] || [ -n "$out" ]; then printf '%s\n' "$out"; return "$rc"; fi
    echo "run_test: $1 gave SIGSEGV and no output (try $try), retrying" >&2
  done
  echo "$1: no output and SIGSEGV in all $try tries"; return 139
}
echo "== real adapter vs conservative reference ($seeds seeds)"
build "$tmp/test"
run_test "$tmp/test" "$seeds" | grep -v '^\[MH-DEKO'

# Mutants run with DEKO_FAIL_FAST. If that mode failed by itself, every mutant would look caught: the real adapter must pass it.
echo; echo "== shader wrapCoord against the runtime's wrap_coord (both compiled from their real source)"
python3 -I - "$root" "$tmp/wrap.c" <<'WRAP'
import re, sys
root, out = sys.argv[1], sys.argv[2]
glsl = open(root + "/game/shaders/nv2a_texture_fsh.glsl").read()
glsl = glsl[glsl.index("int wrapFloorMod"):glsl.index("vec4 fetchWrapped")]
kern = open(root + "/game/build/xboxrecomp/src/kernel/nv2a_pb_exec.c").read()
kern = kern[kern.index("static uint32_t wrap_coord("):]
kern = kern[:kern.index("\n}\n") + 3]
open(out, "w").write("#include <stdint.h>\n#include <stdio.h>\n" + glsl + kern + """
int main(void) {
    static const int sizes[] = {1, 2, 3, 4, 16, 64, 512, 2048};
    long checked = 0, bad = 0;
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (int mode = 0; mode <= 5; mode++)
            for (int c = -3 * sizes[s] - 5; c <= 3 * sizes[s] + 5; c++) {
                checked++;
                if (wrapCoord(c, sizes[s], mode) != (int)wrap_coord((uint32_t)c, (uint32_t)sizes[s], (uint32_t)mode)) bad++;
            }
    printf("wrapCoord: %ld checked, %ld differ\\n", checked, bad);
    return bad != 0;
}
""")
WRAP
clang -std=gnu11 -O1 -fsanitize=undefined -w "$tmp/wrap.c" -o "$tmp/wrap" && "$tmp/wrap"

echo; echo "== control: the real adapter must pass in fail-fast mode"
control="$(DEKO_FAIL_FAST=1 run_test "$tmp/test" 2 || true)"
case "$control" in
  *"RESULT: all passed"*) echo "control passed" ;;
  *) echo "CONTROL FAILED: fail-fast mode fails on the real adapter, so mutant verdicts would mean nothing"; exit 1 ;;
esac

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
 "M5 surface change not noticed":               ("int same = R.valid && R.va == s->color_va && R.pitch == s->pitch && R.w == w && R.h == h && R.pix0 == o->pix0\n            && R.mx == mx && R.my == my;", "int same = R.valid;"),
 "M6 dirty rectangle never grows":              ("            if (x0 < R.dx0) R.dx0 = x0;\n            if (y0 < R.dy0) R.dy0 = y0;\n            if (x1 > R.dx1) R.dx1 = x1;\n            if (y1 > R.dy1) R.dy1 = y1;", "            (void)x0; (void)y0; (void)x1; (void)y1;"),
 "M7 texture cache ignores the palette/hash (always a hit)": ("hit = e->hash == hsh && e->hash != 0;", "hit = e->hash != 0;"),
 "M8 present sync does nothing":                ("if (s_device) resident_flush_dirty(SYNC_PRESENT);", "(void)0;"),
 "M9 raster time always goes to unattributed":  ("*s_rn_to += now - s_rn_seen;", "s_rn_unattributed += now - s_rn_seen;"),
 "M10 accepted draw leaves the previous class in force": ("raster_settle(&s_rn_unattributed);\n    int ok = s_device && surface_ok(s) && state_ok(r)", "int ok = s_device && surface_ok(s) && state_ok(r)"),
 "M11 raster time past the table is hidden":    ("return &s_rj_combo_other_ns;", "return &s_rn_unattributed;"),
 "M12 attribution runs with the profile off":   ("if (!nv2a_profile_enabled()) return;\n    uint64_t now", "uint64_t now"),
 "M13 refused transform has no class":          ("raster_settle(&s_rn_transform); ", ""),
 "M14 clear raster test goes to the last class": ("raster_settle(&s_rn_unattributed);\n    int ok = s_device && surface_ok(s) && flags == 0xF0", "int ok = s_device && surface_ok(s) && flags == 0xF0"),
 "M15 report misses the batch before the flip": ("raster_settle(s_rn_to);", "(void)0;"),
 "M16 refused draw gets no class of its own":   ("raster_settle(count_combo(m));", "count_combo(m);"),
 "M17 report swaps unattributed and transform": ("(unsigned long long)s_rn_unattributed, (unsigned long long)s_rn_transform,", "(unsigned long long)s_rn_transform, (unsigned long long)s_rn_unattributed,"),
 "M18 report drops a class line":               ("for (int c = 0; c < classes; c++) {", "for (int c = 1; c < classes; c++) {"),
 "M19 report prints with the profile off":      ("if (nv2a_profile_enabled()) {\n            /* A header", "{\n            /* A header"),
 "M21 DXT1 hash and overlap cover half the texels": ("if (t->color == 0x0C) return (uint64_t)((t->width + 3) / 4) * ((t->height + 3) / 4) * 8;", "if (t->color == 0x0C) return (uint64_t)((t->width + 3) / 4) * ((t->height + 3) / 4) * 4;"),
 "M22 DXT1 on by default":                      ("s_use_dxt1 = e && *e == '1';", "s_use_dxt1 = !(e && *e == '0');"),
 "M23 DXT1 ignores its switch":                 ("else if (t->color == 0x0C && s_use_dxt1) {", "else if (t->color == 0x0C) {"),
 "M25 DXT1 RAM bounds check too small":         ("            bytes = (uint64_t)((t->width + 3) / 4) * ((t->height + 3) / 4) * 8;", "            bytes = 8;"),
 "M26 refusal counters ignore the switch":      ("|| (t->color == 0x0C && s_use_dxt1) || (t->color == 0x07", "|| (t->color == 0x07"),
 "M27 texture hash covers half of an untiled format": ("h = hash_bytes(h, guest + t->offset, (size_t)texture_bytes(t));", "h = hash_bytes(h, guest + t->offset, (size_t)texture_bytes(t) / 2);"),
 "M28 DXT1 size assumes a square texture":      ("if (t->color == 0x0C) return (uint64_t)((t->width + 3) / 4) * ((t->height + 3) / 4) * 8;", "if (t->color == 0x0C) return (uint64_t)((t->width + 3) / 4) * ((t->width + 3) / 4) * 8;"),
 "M29 refused texture gets no raster time":     ("if (nv2a_profile_enabled() && info->texture) s_rx_to = rx_entry(info->texture, info, r, m);", "(void)0;"),
 "M30 texture rows ignore the format":          (" && s_rx[i].color == t->color", ""),
 "M31 target check ignores the texture's address form": ("    offset &= 0x03FFFFFFu;\n", ""),
 "M32 raster after a texture leaks into the next": ("    s_rn_to = next;\n    s_rx_to = &s_rx_other_ns;\n", "    s_rn_to = next;\n"),
 "M33 draws on targets past the table not counted": ("    s_rx_target_draws_lost++;\n", ""),
 "M35 target check ignores the surface's address form": ("uint32_t va = s->color_va & 0x03FFFFFFu;", "uint32_t va = s->color_va;"),
 "M36 texture rows ignore the size":            ("\n            && s_rx[i].width == t->width && s_rx[i].height == t->height", ""),
 "M37 texture rows ignore the stage program":   ("&& s_rx[i].stage_program == info->stage_program && s_rx[i].shading", "&& s_rx[i].shading"),
 "M44 texture rows ignore the shading":         (" && s_rx[i].shading == info->shading", ""),
 "M45 texture rows ignore the refusal reasons": (" && s_rx[i].mask == mask", ""),
 "M46 row reasons print the wrong mask":        ("if (s_rx[t].mask & (1u << i))", "if (s_rx[0].mask & (1u << i))"),
 "M38 a cleared surface is not a target":       ("s_stats.rejected_clears++; resident_invalidate(SYNC_REJECT); }\n    rx_note_target(s);", "s_stats.rejected_clears++; resident_invalidate(SYNC_REJECT); }"),
 "M39 a texture inside a surface is not a target": ("if (offset >= s_rx_target[i].va && offset < s_rx_target[i].va + s_rx_target[i].bytes", "if (offset == s_rx_target[i].va"),
 "M40 bound flip never moves":                ("s_rx[i].draws++; s_rx[i].last_flip = s_flips;", "s_rx[i].draws++;"),
 "M41 rendered flip never moves":               ("if (bytes > s_rx_target[i].bytes) s_rx_target[i].bytes = bytes;\n            s_rx_target[i].last_flip = s_flips;", "if (bytes > s_rx_target[i].bytes) s_rx_target[i].bytes = bytes;"),
 "M42 report prints the first rows, not the costliest": ("if (!taken[i] && (pick < 0 || s_rx[i].raster_ns > s_rx[pick].raster_ns)) pick = i;", "if (!taken[i] && pick < 0) pick = i;"),
 "M43 draw is not noted as a target":           ("count_reject(s, r, info); resident_invalidate(SYNC_REJECT); }\n    rx_note_target(s);", "count_reject(s, r, info); resident_invalidate(SYNC_REJECT); }"),
 "M34 evicted time is hidden":                  ("        s_rx_evicted_ns += s_rx[use].raster_ns;\n", ""),
 "M47 eviction takes the first row, not the cheapest": ("if (s_rx[i].raster_ns < s_rx[use].raster_ns) use = i;", "(void)i;"),
 "M48 format totals forget evicted rows":       ("        s_rx_fmt[s_rx[use].color & 127].ns += s_rx[use].raster_ns;\n", ""),
 "M49 format alone total ignores the reasons":  ("if (s_rx[use].mask == 1u << RJ_TEX_FORMAT) s_rx_fmt", "s_rx_fmt"),
 "M50 format report ignores live rows":         ("                fmt[s_rx[i].color & 127].ns += s_rx[i].raster_ns;\n", ""),
 "M51 reused row keeps the old time":           (" s_rx[use].raster_ns = 0;\n", "\n"),
 "M52 0x07 on by default":                      ("s_use_x8 = e && *e == '1';", "s_use_x8 = !(e && *e == '0');"),
 "M53 0x07 ignores its switch":                 ("if (t->color == 0x06 || (t->color == 0x07 && s_use_x8)) bytes", "if (t->color == 0x06 || t->color == 0x07) bytes"),
 "M54 0x07 hash and overlap cover half the texels": ("if (t->color == 0x06 || t->color == 0x07) return (uint64_t)t->width * t->height * 4;", "if (t->color == 0x06) return (uint64_t)t->width * t->height * 4;\n    if (t->color == 0x07) return (uint64_t)t->width * t->height * 2;"),
 "M55 refusal counters ignore the 0x07 switch": (" || (t->color == 0x07 && s_use_x8);", ";"),
 "M56 wrap on by default":                      ("s_use_wrap = e && *e == '1';", "s_use_wrap = !(e && *e == '0');"),
 "M57 addressing accepts border":               ("(mode == 1 || mode == 2))", "(mode == 1 || mode == 2 || mode == 4))"),
 "M58 tent-mip filter ignores the switch":      ("(s_use_wrap && f == TENT_MIP_FILTER)", "f == TENT_MIP_FILTER"),
 "M59 addressing refusal ignores the switch":   ("if (!addr_ok(t->addr_u) || !addr_ok(t->addr_v)) m |=", "if (t->addr_u != 3 || t->addr_v != 3) m |="),
 "M60 v addressing is not checked":             ("|| !addr_ok(t->addr_u) || !addr_ok(t->addr_v)\n", "|| !addr_ok(t->addr_u)\n"),
 "M61 swizzled surfaces on by default":         ("s_use_swz = e && *e == '1';", "s_use_swz = !(e && *e == '0');"),
 "M62 readback writes rows, not Morton":        ("memcpy(pix0 + (size_t)(swizzle_deposit(x0 + x, mx) | swizzle_deposit(y, my)) * 4, &argb, 4);", "memcpy(pix0 + (size_t)y * pitch + (size_t)(x0 + x) * 4, &argb, 4);"),
 "M63 upload reads rows, not Morton":           ("memcpy(&argb, pix0 + (size_t)(swizzle_deposit(x0 + x, mx) | swizzle_deposit(y, my)) * 4, 4);", "memcpy(&argb, pix0 + (size_t)y * pitch + (size_t)(x0 + x) * 4, 4);"),
 "M64 resident flush forgets the layout":       ("R.pix0, R.pitch, R.mx, R.my, R.dx0", "R.pix0, R.pitch, 0, 0, R.dx0"),
 "M65 swizzled surface may be part of its format's size": ("\n        && s->width == 1u << ((f >> 16) & 0xFu) && s->height == 1u << ((f >> 24) & 0xFu) && s->pitch == s->width * 4;", ";"),
 "M66 cull ignores the switch":                 ("&& (s_use_swz || !r->cull_enable) &&", "&&"),
 "M67 extra blend factors ignore the switch":   ("    if (!s_use_swz) return 0;\n    switch (nv) {", "    switch (nv) {"),
 "M68 alpha reference ignores the switch":      ("            && (s_use_swz || r->alpha_ref == 0 || r->alpha_ref == 128)))", "            ))"),
 "M69 swizzled zeta not checked":               ("((f & 0xFFu) == 0x24u || (f & 0xFFu) == 0x08u) && ", ""),
 "M70 Morton axes swapped":                     ("xbox_swizzle_masks(s->width, s->height, mx, my);", "xbox_swizzle_masks(s->width, s->height, my, mx);"),
 "M20 report miscounts its classes":            ("while (classes < RJ_COMBOS && s_rj_combo[classes].mask) classes++;", "while (classes < RJ_COMBOS && s_rj_combo[classes].mask) classes++;\n            classes++;"),
}
import json
json.dump({}, open(tmp + "/mutants.json", "w"))
for i, (name, (a, b)) in enumerate(mutants.items(), 1):
    if src.count(a) != 1:
        print("MUTANT %s: pattern not found exactly once (%d): the test script is out of date" % (name, src.count(a))); sys.exit(3)
    open("%s/m%d.c" % (tmp, i), "w").write(src.replace(a, b, 1))
open(tmp + "/names.txt", "w").write("\n".join(mutants.keys()) + "\n")
PY
run_mutant() {  # index name: the verdict is the last line printed; a failed build leaves none
  local result
  build "-DADAPTER_SRC=\"$tmp/m$1.c\"" -I "$root/game/src" "$tmp/mut$1"
  result="$(DEKO_FAIL_FAST=1 run_test "$tmp/mut$1" "$seeds" || true)"
  rm -f "$tmp/mut$1"
  case "$result" in
    *"RESULT: all passed"*) echo "SURVIVED  $2   <-- the test did NOT catch this bug" ;;
    *"RESULT: "*" FAILED"*|*"ERROR: AddressSanitizer"*|*"unexpectedly rejected"*|*"expected a rejection"*) echo "killed    $2" ;;
    *) echo "INCONCLUSIVE $2   <-- the run gave no verdict" ;;
  esac
}
# Mutants share nothing, so they run side by side (JOBS, default all cores); verdicts print in mutant order.
jobs_max="${JOBS:-$(nproc)}"
i=0; running=0
while IFS= read -r name; do
  i=$((i+1))
  if [ "$running" -ge "$jobs_max" ]; then wait -n || true; running=$((running-1)); fi
  run_mutant "$i" "$name" > "$tmp/verdict$i" 2>&1 &
  running=$((running+1))
done < "$tmp/names.txt"
wait || true
n=0; survived=0; inconclusive=0
while IFS= read -r name; do
  n=$((n+1))
  verdict="$(tail -n 1 "$tmp/verdict$n")"
  case "$verdict" in
    "killed    $name") cat "$tmp/verdict$n" ;;
    "SURVIVED  $name"*) cat "$tmp/verdict$n"; survived=$((survived+1)) ;;
    "INCONCLUSIVE $name"*) cat "$tmp/verdict$n"; inconclusive=$((inconclusive+1)) ;;
    *) cat "$tmp/verdict$n"; echo "INCONCLUSIVE $name   <-- no verdict, probably a failed build"; inconclusive=$((inconclusive+1)) ;;
  esac
done < "$tmp/names.txt"
echo; if [ "$survived" = 0 ] && [ "$inconclusive" = 0 ]; then echo "MUTATION RESULT: every planted bug was caught"; else echo "MUTATION RESULT: $survived planted bug(s) survived, $inconclusive inconclusive"; exit 1; fi
