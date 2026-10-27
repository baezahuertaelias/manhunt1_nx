#!/usr/bin/env python3
"""Symbolize a [WATCHDOG] guest stack dump (GS lines) from a game-run log.

Usage: python3 -I scripts/guest-stack.py <run.log> [functions.json]
Prints each stack word that falls inside a known function, innermost first.
"""
import bisect
import json
import re
import sys

log = sys.argv[1]
fj = sys.argv[2] if len(sys.argv) > 2 else "game/build/pipeline/out/disasm/functions.json"
fs = json.load(open(fj))
st = sorted((int(f["start"], 16), int(f["end"], 16), f["name"], f["section"]) for f in fs)
starts = [s[0] for s in st]
text = open(log, errors="replace").read()
i = text.find("[WATCHDOG]")
if i < 0:
    sys.exit("no [WATCHDOG] dump in log")
print(text[i:text.find("\n", i)])
for m in re.finditer(r"^\s+GS ([0-9A-F]{8}) ([0-9A-F]{8})", text[i:], re.M):
    v = int(m.group(2), 16)
    k = bisect.bisect_right(starts, v) - 1
    if k >= 0 and st[k][0] < v <= st[k][1] + 1 and v > 0x11000:
        print(f"  {m.group(1)}  {v:#010x}  {st[k][2]}+{v - st[k][0]:#x}  [{st[k][3]}]")
