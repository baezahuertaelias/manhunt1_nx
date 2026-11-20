#!/usr/bin/env python3
"""Package a release zip of the port. It contains no game data.

    scripts/make-release.py <version> <manhunt.nro> [out dir]

Makes <out>/manhunt1-nx-v<version>.zip (default out: dist/) that is meant to be
extracted at the root of the SD card:

    switch/manhunt1_nx/manhunt.nro
    switch/manhunt1_nx/manhunt.env              the playing settings (widescreen on)
    switch/manhunt1_nx/manifest.json            version, NRO SHA-256, what game files it needs
    switch/manhunt1_nx/asset-manifest.sha256    SHA-256 of each game file the port expects
    switch/manhunt1_nx/LICENSES/                licence texts and the LGPL source notice
    switch/manhunt1_nx/INSTALL.txt

It also writes <out>/manhunt1-nx-v<version>-lgpl-source.zip: the LGPL-2.1
audio emulator (src/apu/) exactly as compiled, from the patched tree in
game/build/xboxrecomp. Publish both files together (LICENSES/SOURCE-OFFER.txt).

It refuses an NRO whose icon is game/build/icon256.jpg: that icon is made from
the game's cover art and must never be published.

The game files themselves (DEFAULT.xbe, audio/, levels/) are never included:
the installer (https://github.com/marmota94/manhunt1_nx_installer) builds them
from the user's own disc image and checks them against asset-manifest.sha256. Prints the zip's SHA-256 and size.
"""
import hashlib
import json
import os
import re
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREFIX = "switch/manhunt1_nx/"

ENV = """RECOMP_SPAN_4G=1
MANHUNT_GPU_BACKEND=2
MANHUNT_AUDIO_APU=1
MANHUNT_WIDESCREEN=1
MANHUNT_LANGUAGE=spanish
MANHUNT_AMBIENT_LIGHT=-1
"""

INSTALL = """Manhunt for Nintendo Switch (recompiled), version {version}

This package has NO game data. You need your own copy of Manhunt (original
Xbox version); the game files go in switch/manhunt1_nx/game/ :

    switch/manhunt1_nx/game/DEFAULT.xbe
    switch/manhunt1_nx/game/audio/...
    switch/manhunt1_nx/game/levels/...

The easiest way to get them is the installer, which builds a complete SD
package from your own disc image or extracted folder in the browser:
    https://baezahuertaelias.github.io/manhunt1_nx_installer/
asset-manifest.sha256 lists the SHA-256, size and path of every file the port
expects (one "sha256  size  path" line per file after a comment line).

Install: extract the zip at the root of the SD card. Start manhunt.nro in
title-takeover mode so it gets enough memory: hold R while opening any game to
reach the homebrew menu (or Sphaira), then pick manhunt.nro.

Controls: the Switch buttons map to the Xbox pad by label (A, B, X, Y).
Hold ZL + ZR + Plus + Minus for 1.5 seconds to quit the game.

Settings are in manhunt.env, one KEY=VALUE per line. MANHUNT_WIDESCREEN=1 gives
16:9 (HUD at the screen edges, movies filling the screen); remove it for 4:3.
See the project README for the full list.

manhunt.nro includes an LGPL-2.1 library; LICENSES/SOURCE-OFFER.txt says where
its source is.

Experimental: tested on one console. Not affiliated with or endorsed by the
owners of Manhunt.
"""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def nro_icon(data):
    """The NRO's icon bytes, or b"" without an asset section."""
    total = struct.unpack_from("<I", data, 0x18)[0]
    if data[total:total + 4] != b"ASET":
        return b""
    off, size = struct.unpack_from("<QQ", data, total + 8)
    return data[total + off:total + off + size]


def lgpl_source_zip(path, version, fixed):
    """src/apu/ as compiled (the patched xboxrecomp tree) with the licence."""
    tree = os.path.join(ROOT, "game", "build", "xboxrecomp")
    apu = os.path.join(tree, "src", "apu")
    if not os.path.isdir(apu):
        raise SystemExit("patched tree not found: " + apu + " (run scripts/game-build.sh switch first)")
    pin = open(os.path.join(ROOT, "infra", "recompiler", "Dockerfile")).read()
    commit = re.search(r"XBOXRECOMP_COMMIT=([0-9a-f]{40})", pin)
    readme = ("LGPL-2.1 source in manhunt1-nx v%s\n\n"
              "src/apu/ is xboxrecomp's audio emulator (extracted from xemu) as compiled\n"
              "into manhunt.nro: https://github.com/sp00nznet/xboxrecomp at commit %s\n"
              "with the patches in game/patches/xboxrecomp/ of\n"
              "https://github.com/baezahuertaelias/manhunt1_nx (tag v%s) applied.\n"
              "Licence: LGPL-2.1-or-later, text in LGPL-2.1.txt. Copyright lines are in\n"
              "each file's header.\n" % (version, commit.group(1) if commit else "(see infra/recompiler/Dockerfile)", version))

    def add(z, name, data):
        zi = zipfile.ZipInfo(name, fixed)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        z.writestr(zi, data)

    with zipfile.ZipFile(path, "w") as z:
        add(z, "README.txt", readme)
        add(z, "LGPL-2.1.txt", open(os.path.join(ROOT, "LICENSES", "LGPL-2.1.txt"), "rb").read())
        for n in sorted(os.listdir(apu)):
            f = os.path.join(apu, n)
            if os.path.isfile(f):
                add(z, "src/apu/" + n, open(f, "rb").read())


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 2
    version, nro = argv[1], argv[2]
    out = argv[3] if len(argv) == 4 else os.path.join(ROOT, "dist")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("version must look like 0.2.0")
    if not os.path.isfile(nro):
        raise SystemExit("NRO not found: " + nro)
    am = os.path.join(ROOT, "reports", "ASSET_MANIFEST.sha256")
    xbe_hash = None
    for line in open(am):
        if line.strip().endswith("DEFAULT.xbe") and not line.startswith("#"):
            xbe_hash = line.split()[0]
            break
    if not xbe_hash:
        raise SystemExit("DEFAULT.xbe not found in " + am)
    nro_hash = sha256(nro)
    cover = os.path.join(ROOT, "game", "build", "icon256.jpg")
    icon = nro_icon(open(nro, "rb").read())
    if icon and os.path.isfile(cover) and icon == open(cover, "rb").read():
        raise SystemExit("the NRO's icon is game/build/icon256.jpg (cover art): "
                         "move that file aside and rebuild before releasing")
    manifest = {
        "name": "manhunt1_nx",
        "version": version,
        "nro": {"path": "manhunt.nro", "sha256": nro_hash, "bytes": os.path.getsize(nro)},
        "game": {
            "install_dir": "game",
            "default_xbe_sha256": xbe_hash,
            "asset_manifest": "asset-manifest.sha256",
            "note": "Game files are not included; build them from the user's own disc image.",
        },
        "env_default": "manhunt.env",
    }
    os.makedirs(out, exist_ok=True)
    zpath = os.path.join(out, "manhunt1-nx-v%s.zip" % version)
    fixed = (2026, 1, 1, 0, 0, 0)

    def add(z, name, data):
        zi = zipfile.ZipInfo(PREFIX + name, fixed)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        z.writestr(zi, data)

    with zipfile.ZipFile(zpath, "w") as z:
        add(z, "manhunt.nro", open(nro, "rb").read())
        add(z, "manhunt.env", ENV)
        add(z, "manifest.json", json.dumps(manifest, indent=2) + "\n")
        add(z, "asset-manifest.sha256", open(am, "rb").read())
        add(z, "INSTALL.txt", INSTALL.format(version=version))
        lic = os.path.join(ROOT, "LICENSES")
        for n in sorted(os.listdir(lic)):
            add(z, "LICENSES/" + n, open(os.path.join(lic, n), "rb").read())
    spath = os.path.join(out, "manhunt1-nx-v%s-lgpl-source.zip" % version)
    lgpl_source_zip(spath, version, fixed)
    print("wrote %s" % zpath)
    print("  zip sha256 %s  %d bytes" % (sha256(zpath), os.path.getsize(zpath)))
    print("  nro sha256 %s" % nro_hash)
    print("wrote %s" % spath)
    print("  zip sha256 %s  %d bytes" % (sha256(spath), os.path.getsize(spath)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
