#!/usr/bin/env python3
"""Upscale the PNGs written by tools/txd_extract.py with a local AI upscaler.

    tools/upscale_textures.py <extracted folder> <out folder> [options]

It runs a command-line upscaler you install yourself, once per PNG, and keeps
the folder layout and manifest.json so the result lines up with the original
textures. Nothing is uploaded: the upscaler runs on this machine.

Default backend: realesrgan-ncnn-vulkan (https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan),
which runs on any Vulkan GPU:

    realesrgan-ncnn-vulkan -i IN.png -o OUT.png -s SCALE -n MODEL

Options:
    --scale N        2, 3 or 4 (default 4)
    --model NAME     upscaler model (default realesrgan-x4plus)
    --bin PATH       the upscaler executable (default: realesrgan-ncnn-vulkan on PATH)
    --min-size N     skip textures whose longest side is below N (default 0)
    --only SUBSTR    only PNGs whose path contains SUBSTR (e.g. "menu/" or "frontend")
    --limit N        stop after N textures (try a few before a full run)
    --dry-run        list what would run

Resumable: a PNG that already exists in the output folder is skipped, so a run
can be stopped and started again. Output is game art: keep it out of git.

Memory note for later use in the game: a 4x texture costs 16x the memory.
On the Switch, upscale the HUD and menu textures first.
"""
import json
import os
import shutil
import subprocess
import sys


def parse(argv):
    opts = {"scale": 4, "model": "realesrgan-x4plus", "bin": None, "min_size": 0,
            "only": None, "limit": None, "dry": False}
    pos = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--dry-run":
            opts["dry"] = True
        elif a in ("--scale", "--model", "--bin", "--min-size", "--only", "--limit"):
            i += 1
            if i >= len(argv):
                raise SystemExit("%s needs a value" % a)
            key = a[2:].replace("-", "_")
            opts[key] = argv[i] if key in ("model", "bin", "only") else int(argv[i])
        elif a.startswith("--"):
            raise SystemExit("unknown option " + a)
        else:
            pos.append(a)
        i += 1
    if len(pos) != 2:
        print(__doc__)
        raise SystemExit(2)
    return pos[0], pos[1], opts


def main(argv):
    src, dst, o = parse(argv)
    if o["scale"] not in (2, 3, 4):
        raise SystemExit("--scale must be 2, 3 or 4")
    binary = o["bin"] or shutil.which("realesrgan-ncnn-vulkan")
    if not binary and not o["dry"]:
        raise SystemExit("no upscaler found: install realesrgan-ncnn-vulkan or pass --bin PATH")
    mpath = os.path.join(src, "manifest.json")
    if not os.path.isfile(mpath):
        raise SystemExit("%s not found: run tools/txd_extract.py first" % mpath)
    manifest = json.load(open(mpath))
    todo, skipped_small, done_before = [], 0, 0
    for rec in manifest:
        png = rec.get("png")
        if not png:
            continue
        if o["only"] and o["only"] not in png:
            continue
        if max(rec["width"], rec["height"]) < o["min_size"]:
            skipped_small += 1
            continue
        if os.path.isfile(os.path.join(dst, png)):
            done_before += 1
            continue
        todo.append(rec)
    if o["limit"] is not None:
        todo = todo[:o["limit"]]
    print("%d to upscale, %d already done, %d below --min-size" % (len(todo), done_before, skipped_small))
    ok = failed = 0
    for n, rec in enumerate(todo, 1):
        inp, out = os.path.join(src, rec["png"]), os.path.join(dst, rec["png"])
        cmd = [binary or "realesrgan-ncnn-vulkan", "-i", inp, "-o", out,
               "-s", str(o["scale"]), "-n", o["model"]]
        if o["dry"]:
            print(" ".join(cmd))
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if r.returncode != 0 or not os.path.isfile(out):
            failed += 1
            if os.path.isfile(out):
                os.remove(out)
            print("FAIL %s: %s" % (rec["png"], r.stderr.decode(errors="replace").strip()[-200:]))
        else:
            ok += 1
        if n % 25 == 0:
            print("%d/%d" % (n, len(todo)))
    if not o["dry"]:
        os.makedirs(dst, exist_ok=True)
        for rec in manifest:
            if rec.get("png") and os.path.isfile(os.path.join(dst, rec["png"])):
                rec["upscaled"] = {"scale": o["scale"], "model": o["model"]}
        with open(os.path.join(dst, "manifest.json"), "w") as f:
            json.dump(manifest, f, indent=1)
        print("upscaled %d, failed %d" % (ok, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
