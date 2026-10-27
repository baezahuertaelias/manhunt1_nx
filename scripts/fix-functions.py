#!/usr/bin/env python3
"""Apply reviewed function-boundary fixes to a tools.disasm functions.json.

Usage: python3 -I scripts/fix-functions.py <functions.json> <fixes.json> <out.json>
Each fix removes false entries (detected starts inside an instruction of the
real owner) and extends the owner to its true exclusive end. Fails if a fix no
longer matches the database, so a re-analysis cannot silently drift.
"""
import json
import sys

src, fixes_path, out = sys.argv[1:4]
funcs = json.load(open(src))
by = {int(f["start"], 16): f for f in funcs}
for fix in json.load(open(fixes_path)):
    owner, end = int(fix["owner"], 16), int(fix["end"], 16)
    if owner not in by:
        sys.exit(f"fix-functions: owner {fix['owner']} not in database")
    for r in fix["remove"]:
        va = int(r, 16)
        if va not in by or not owner < va < end:
            sys.exit(f"fix-functions: {r} is not a detected start inside {fix['owner']}..{fix['end']}")
        del by[va]
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
