#!/usr/bin/env python3
"""Package a release zip of the port. It contains no game data.

    scripts/make-release.py <version> <manhunt.nro> [out dir]

Makes <out>/manhunt1-nx-v<version>.zip (default out: dist/) that is meant to be
extracted at the root of the SD card:

    switch/manhunt1_nx/manhunt.nro
    switch/manhunt1_nx/manhunt.env              the playing settings (widescreen on)
    switch/manhunt1_nx/manifest.json            version, NRO SHA-256, what game files it needs
    switch/manhunt1_nx/asset-manifest.sha256    SHA-256 of each game file the port expects
    switch/manhunt1_nx/LICENSES/                licence texts
    switch/manhunt1_nx/INSTALL.txt

The game files themselves (DEFAULT.xbe, audio/, levels/) are never included: a
separate installer builds them from the user's own disc image and checks them
against asset-manifest.sha256. Prints the zip's SHA-256 and size.
"""
import hashlib
import json
import os
import re
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

asset-manifest.sha256 lists the SHA-256 and size of every file the port expects,
so a copy can be checked with:   cd game && sha256sum -c ../asset-manifest.sha256
(the first line of the manifest is a comment).

Install: extract the zip at the root of the SD card. Start manhunt.nro from
Sphaira in title-takeover mode (hold R while opening a game) so it gets enough
memory.

Controls: the Switch buttons map to the Xbox pad by label (A, B, X, Y).
Hold ZL + ZR + Plus + Minus for 1.5 seconds to quit the game.

Settings are in manhunt.env, one KEY=VALUE per line. MANHUNT_WIDESCREEN=1 gives
16:9 (HUD at the screen edges, movies filling the screen); remove it for 4:3.
See the project README for the full list.

Experimental: tested on one console. Not affiliated with or endorsed by the
owners of Manhunt.
"""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


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
    print("wrote %s" % zpath)
    print("  zip sha256 %s  %d bytes" % (sha256(zpath), os.path.getsize(zpath)))
    print("  nro sha256 %s" % nro_hash)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
