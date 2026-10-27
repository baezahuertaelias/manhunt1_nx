#!/usr/bin/env python3
"""Convert 24/32-bit BMP frame dumps to PNG (stdlib only).
Usage: python3 -I scripts/bmp2png.py <dir-or-files...>"""
import os
import struct
import sys
import zlib


def convert(src):
    b = open(src, "rb").read()
    off = struct.unpack_from("<I", b, 10)[0]
    w, h = struct.unpack_from("<ii", b, 18)
    bpp = struct.unpack_from("<H", b, 28)[0] // 8
    stride = (w * bpp + 3) & ~3
    rows = []
    for y in range(abs(h)):
        sy = abs(h) - 1 - y if h > 0 else y
        r = b[off + sy * stride: off + sy * stride + w * bpp]
        px = bytearray()
        for x in range(w):
            px += bytes((r[x * bpp + 2], r[x * bpp + 1], r[x * bpp]))
        rows.append(b"\0" + bytes(px))

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, abs(h), 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))
    dst = src[:-4] + ".png"
    open(dst, "wb").write(png)
    return dst


for arg in sys.argv[1:]:
    files = [os.path.join(arg, f) for f in sorted(os.listdir(arg))] if os.path.isdir(arg) else [arg]
    for f in files:
        if f.lower().endswith(".bmp"):
            print(convert(f))
