#!/usr/bin/env python3
"""Apply reviewed function-boundary fixes to a tools.disasm functions.json.

Usage: python3 -I scripts/fix-functions.py <functions.json> <fixes.json> <out.json>
Boundary fixes remove false entries and extend their real owner. An `add`
record inserts a reviewed, measured entry only in an uncovered gap. Both forms
fail when the analysis changes, so a re-analysis cannot silently drift.
"""
import json
import sys

src, fixes_path, out = sys.argv[1:4]
funcs = json.load(open(src))
by = {int(f["start"], 16): f for f in funcs}
for fix in json.load(open(fixes_path)):
    if "add" in fix:
        entry = fix["add"]
        start, end = int(entry["start"], 16), int(entry["end"], 16)
        if not (0 < start < end <= 0x100000000):
            sys.exit("fix-functions: invalid added function range")
        if start in by:
            sys.exit(f"fix-functions: added entry {start:#x} already in database; re-review")
        overlaps = [s for s, f in by.items()
                    if start < int(f["end"], 16) and s < end]
        if overlaps:
            sys.exit(f"fix-functions: added entry {start:#x} overlaps {[hex(s) for s in overlaps]}")
        if entry["num_instructions"] <= 0 or not entry["section"] or not fix["why"]:
            sys.exit("fix-functions: added entry needs instruction count, section and rationale")
        by[start] = dict(entry, start=f"0x{start:08X}", end=f"0x{end:08X}",
                         size=end - start, name=f"sub_{start:08X}",
                         detection_method="reviewed_runtime_target", confidence=0.9,
                         has_prologue=False, called_by=[])
        continue
    owner, end = int(fix["owner"], 16), int(fix["end"], 16)
    if owner not in by:
        sys.exit(f"fix-functions: owner {fix['owner']} not in database")
    for r in fix["remove"]:
        va = int(r, 16)
        if va not in by or not owner < va < end:
            sys.exit(f"fix-functions: {r} is not a detected start inside {fix['owner']}..{fix['end']}")
        del by[va]
    removed_starts = [int(r, 16) for r in fix["remove"]]
    old_end = int(by[owner]["end"], 16)
    # The owner must be truncated at (or just past) a removed false entry and
    # within the new end; otherwise the analysis changed: re-review the fix.
    if not (min(removed_starts) <= old_end <= end):
        sys.exit(f"fix-functions: owner {fix['owner']} ends at {old_end:#x}, expected a truncation in "
                 f"[{min(removed_starts):#x}, {end:#x}]: analysis drifted, re-review")
    inside = [s for s in by if owner < s < end]
    if inside:
        sys.exit(f"fix-functions: other starts inside {fix['owner']}: {[hex(s) for s in inside]}")
    by[owner]["end"] = f"0x{end:08X}"
    by[owner]["size"] = end - owner
    removed = {f"0x{int(r, 16):08X}" for r in fix["remove"]}
    for f in by.values():                          # drop references to removed entries
        f["calls_to"] = [c for c in f.get("calls_to", []) if c.upper().replace("0X", "0x") not in removed]
        f["called_by"] = [c for c in f.get("called_by", []) if c.upper().replace("0X", "0x") not in removed]
json.dump([by[k] for k in sorted(by)], open(out, "w"), indent=1)
print(f"fix-functions: {len(funcs)} -> {len(by)} functions")
