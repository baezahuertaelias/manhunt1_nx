#!/usr/bin/env python3
"""Compare console frame dumps pixel by pixel (stdlib only; needs scripts/hw-summarize.py beside it).

  python3 -I scripts/hw-compare-frames.py A.bmp B.bmp [--ignore-top N]
  python3 -I scripts/hw-compare-frames.py DIR_A DIR_B [--ignore-top N]

Two files: prints how many pixels differ, the largest per-channel difference and the bounding box
of the differences. Two directories: compares every pair of frames that show the same highlighted
main-menu entry (so an unchanged menu must give 0 differing pixels) and prints one line per pair.
--ignore-top N skips the first N rows (the camcorder overlay with a running timecode changes every frame).
LIMIT (measured): the main-menu background animates, so two frames of an unchanged menu taken at
different times differ in most pixels. Use it for frames of the same state only; to prove a renderer
change is pixel-identical use MANHUNT_DEKO_VERIFY=1 (same run, same inputs). Frames are game-derived:
look at them, never commit them. This tool states differences; it does not decide what is acceptable."""
import importlib.util
import os
import sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("hs", os.path.join(_here, "hw-summarize.py"))
hs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hs)


def compare(a, b, ignore_top=0):
    ra, rb = hs.bmp_rows(a), hs.bmp_rows(b)
    if not ra or not rb:
        return None
    if ra[0] != rb[0] or ra[1] != rb[1]:
        return {"error": "size differs %dx%d vs %dx%d" % (ra[0], ra[1], rb[0], rb[1])}
    w, h = ra[0], ra[1]
    pa, pb = ra[2], rb[2]
    diff = 0
    worst = 0
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for y in range(ignore_top, h):
        for x in range(w):
            ca, cb = pa(x, y), pb(x, y)
            if ca != cb:
                diff += 1
                d = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
                worst = max(worst, d)
                x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
    return {"pixels": (h - ignore_top) * w, "different": diff, "max_channel_diff": worst,
            "bbox": None if diff == 0 else (x0, y0, x1, y1)}


def line(name, r):
    if r is None:
        return "%s: unreadable or truncated frame" % name
    if "error" in r:
        return "%s: %s" % (name, r["error"])
    return "%s: %d of %d pixels differ (%.3f%%), max channel difference %d%s" % (
        name, r["different"], r["pixels"], 100.0 * r["different"] / r["pixels"], r["max_channel_diff"],
        "" if r["bbox"] is None else ", differences within x%d-%d y%d-%d" % (r["bbox"][0], r["bbox"][2], r["bbox"][1], r["bbox"][3]))


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    top = 0
    if "--ignore-top" in argv:
        top = int(argv[argv.index("--ignore-top") + 1])
        args = [a for a in args if a != str(top)]
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    a, b = args
    if os.path.isfile(a) and os.path.isfile(b):
        print(line("%s vs %s" % (os.path.basename(a), os.path.basename(b)), compare(a, b, top)))
        return 0
    if not (os.path.isdir(a) and os.path.isdir(b)):
        sys.stderr.write(__doc__)
        return 2
    def menu_frames(d):
        out = {}
        for f in sorted(os.listdir(d)):
            if f.lower().endswith(".bmp"):
                try:
                    r = hs.highlighted_row(os.path.join(d, f))
                except (IndexError, ValueError):
                    r = None
                if r is not None:
                    out.setdefault(r, []).append(f)
        return out
    ma, mb = menu_frames(a), menu_frames(b)
    common = sorted(set(ma) & set(mb))
    if not common:
        print("no main-menu frames with the same highlighted entry in both directories")
        return 0
    for row in common:
        fa, fb = ma[row][len(ma[row]) // 2], mb[row][len(mb[row]) // 2]   # a middle frame: the menu has settled
        print(line("%s (%s) vs %s (%s)" % (fa, hs.MENU_NAMES[row], fb, hs.MENU_NAMES[row]),
                   compare(os.path.join(a, fa), os.path.join(b, fb), top)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
