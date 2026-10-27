#!/usr/bin/env python3
"""Check whether the function following each truncated function starts
inside an instruction (a false entry) when decoded linearly from the owner.

Usage (in manhunt-recompiler): python -I check-false-entries.py <xbe> <functions.json> <falloff.json>
"""
import json
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

xbe, fj, cands = sys.argv[1:4]
data = open(xbe, "rb").read()
base = struct.unpack_from("<I", data, 0x104)[0]
nsec = struct.unpack_from("<I", data, 0x11C)[0]
secoff = struct.unpack_from("<I", data, 0x120)[0] - base
secs = []
for i in range(nsec):
    _, va, vs, raw, rs = struct.unpack_from("<5I", data, secoff + i * 56)
    secs.append((va, rs, raw))


def read(va, n):
    for sva, rs, raw in secs:
        if sva <= va < sva + rs:
            return data[raw + va - sva: raw + min(va - sva + n, rs)]
    return b""


fs = sorted(int(f["start"], 16) for f in json.load(open(fj)))
md = Cs(CS_ARCH_X86, CS_MODE_32)
for c in json.load(open(cands)):
    s = int(c, 16)
    i = fs.index(s)
    nxt = fs[i + 1] if i + 1 < len(fs) else None
    if nxt is None:
        continue
    code = read(s, nxt - s + 16)
    pos, verdict = s, "boundary"
    for ins in md.disasm(code, s):
        if ins.address < nxt < ins.address + ins.size:
            verdict = f"MID-INSTRUCTION ({ins.mnemonic} {ins.op_str} @ {ins.address:#x})"
            break
        if ins.address >= nxt:
            break
        last = ins
    print(f"{s:#010x} next={nxt:#010x} {verdict}; last before next: {last.mnemonic} {last.op_str}")
