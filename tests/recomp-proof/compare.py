#!/usr/bin/env python3
"""Compare harness output with reference.txt and print a per-group summary.

Pass rule (exit 0):
  - every "num" case matches exactly (all registers, x87 TOP, st(0), memory);
  - every "x87" case at 53- or 24-bit precision control matches exactly.
Reported but not gating:
  - "x87" at 64-bit precision control: the lifter models it with doubles;
  - "nan": x87 NaN-selection rules are not modelled.

Usage: python3 -I compare.py reference.txt out.txt
"""
import collections
import struct
import sys


def parse(path):
    rows = []
    for line in open(path):
        f = line.split()
        d = dict(kv.split("=", 1) for kv in f[1:] if "=" in kv)
        d["label"] = f[3]
        rows.append(d)
    return rows


def as_float(bits):
    return struct.unpack("<f", struct.pack("<f", struct.unpack("<d", struct.pack("<Q", int(bits, 16)))[0]))[0]


def main(ref_path, out_path):
    ref, out = parse(ref_path), parse(out_path)
    if len(ref) != len(out):
        print(f"case count differs: {len(ref)} vs {len(out)}")
        return 1
    groups = collections.OrderedDict()
    first_bad = {}
    for r, o in zip(ref, out):
        key = (r["fn"], r["label"], r["cw"] if r["label"] == "x87" else "*")
        g = groups.setdefault(key, [0, 0, 0])   # total, exact, float-equal st0
        g[0] += 1
        if r == o:
            g[1] += 1
            g[2] += 1
        else:
            first_bad.setdefault(key, (r, o))
            same_but_st0 = {k: v for k, v in r.items() if k != "st0"} == {k: v for k, v in o.items() if k != "st0"}
            if same_but_st0 and r["st0"] != "-" and o["st0"] != "-" and as_float(r["st0"]) == as_float(o["st0"]):
                g[2] += 1
    status = 0
    for (fn, label, cw), (total, exact, fl) in groups.items():
        gating = label == "num" or (label == "x87" and cw != "037f")
        ok = exact == total
        if gating and not ok:
            status = 1
        tag = "PASS" if ok else ("FAIL" if gating else "INFO")
        extra = f", st0 equal after rounding to float {fl}/{total}" if label == "x87" else ""
        print(f"  {tag} fn={fn} {label:3} cw={cw:4} exact {exact}/{total}{extra}")
    for key, (r, o) in first_bad.items():
        diff = {k: (r[k], o.get(k)) for k in r if r[k] != o.get(k)}
        print(f"  first mismatch {key}: {diff}")
    # Guard against a vacuous pass: every proof function needs gating cases.
    gated = {fn for (fn, label, cw) in groups if label == "num" or (label == "x87" and cw != "037f")}
    missing = EXPECTED_FUNCTIONS - gated
    if missing:
        print(f"  FAIL no gating cases for {sorted(missing)}")
        status = 1
    return status


EXPECTED_FUNCTIONS = {"0001a9c0", "00089cf0", "000e3640", "00205a00", "00205d90", "00205040"}


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
