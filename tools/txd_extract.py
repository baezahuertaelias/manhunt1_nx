#!/usr/bin/env python3
"""Extract the textures of Manhunt's Xbox .TXD files to PNG.

Pure standard library (zlib for the PNG writer). Works on YOUR OWN game files
and writes only to the output folder you name; nothing is uploaded and the
output (game art) must never be committed.

    tools/txd_extract.py <game folder or .TXD> <out folder> [--mips] [--list]

Output: <out>/<path of the TXD below the game folder>/<texture name>.png and
<out>/manifest.json (one record per texture: source, name, size, format,
alpha, SHA-256 of the stored texture bytes, PNG path). The manifest is what a
later replacement step keys on; this tool does not upscale anything. See
tools/upscale_textures.py for that step.

Supported: RenderWare Xbox native textures (platform id 5), which are 5,564 of
the 5,612 textures in the game: DXT1, DXT3, DXT5, 8-bit paletted and 32-bit
A8R8G8B8 (swizzled, as the Xbox stores them). Older non-native layouts are
reported and skipped.
"""
import hashlib
import json
import os
import struct
import sys
import zlib

RW_TXD, RW_STRUCT, RW_TEXNATIVE = 0x16, 0x01, 0x15
PLATFORM_XBOX = 5
COMP_DXT1, COMP_DXT3, COMP_DXT5 = 12, 14, 15


# ---------------------------------------------------------------- PNG writer
def write_png(path, w, h, rgba):
    """rgba: bytes, w*h*4, row-major."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    stride = w * 4
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ------------------------------------------------------------------ decoders
def rgb565(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def dxt_color_block(b, off, has_alpha_in_color):
    c0, c1, bits = struct.unpack_from("<HHI", b, off)
    r0, g0, b0 = rgb565(c0)
    r1, g1, b1 = rgb565(c1)
    pal = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
    if c0 > c1 or not has_alpha_in_color:
        pal.append(((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255))
        pal.append(((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255))
    else:  # DXT1 with a transparent index
        pal.append(((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255))
        pal.append((0, 0, 0, 0))
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def decode_dxt(data, w, h, kind):
    out = bytearray(w * h * 4)
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    bsize = 8 if kind == COMP_DXT1 else 16
    for by in range(bh):
        for bx in range(bw):
            off = (by * bw + bx) * bsize
            if off + bsize > len(data):
                return bytes(out)
            if kind == COMP_DXT1:
                px = dxt_color_block(data, off, True)
                alpha = None
            elif kind == COMP_DXT3:
                px = dxt_color_block(data, off + 8, False)
                a64 = struct.unpack_from("<Q", data, off)[0]
                alpha = [((a64 >> (4 * i)) & 15) * 17 for i in range(16)]
            else:  # DXT5
                px = dxt_color_block(data, off + 8, False)
                a0, a1 = data[off], data[off + 1]
                bits = int.from_bytes(data[off + 2:off + 8], "little")
                if a0 > a1:
                    ap = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    ap = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [ap[(bits >> (3 * i)) & 7] for i in range(16)]
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    r, g, b, a = px[i]
                    if alpha is not None:
                        a = alpha[i]
                    o = (y * w + x) * 4
                    out[o:o + 4] = bytes((r, g, b, a))
    return bytes(out)


def swizzle_offsets(w, h):
    """Xbox swizzle: Morton order. Bits of x and y interleave (x low) while
    both have bits left; the longer side's remaining bits follow."""
    xb = w.bit_length() - 1
    yb = h.bit_length() - 1
    n = min(xb, yb)

    def off(x, y):
        o = 0
        for i in range(n):
            o |= ((x >> i) & 1) << (2 * i)
            o |= ((y >> i) & 1) << (2 * i + 1)
        o |= (x >> n) << (2 * n) if xb > n else 0
        o |= (y >> n) << (2 * n) if yb > n else 0
        return o
    return off


def decode_swizzled(data, w, h, bpp, palette):
    off = swizzle_offsets(w, h)
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            o = off(x, y) * bpp
            if o + bpp > len(data):
                return bytes(out)
            if palette is not None:
                b, g, r, a = palette[data[o]]
            else:
                b, g, r, a = data[o], data[o + 1], data[o + 2], data[o + 3]
            p = (y * w + x) * 4
            out[p:p + 4] = bytes((r, g, b, a))
    return bytes(out)


# ------------------------------------------------------------------- parsing
def cstr(b):
    return b.split(b"\0")[0].decode("latin1")


def parse_txd(d):
    """Yield (record, rgba_or_None) per texture."""
    if len(d) < 12 or struct.unpack_from("<I", d, 0)[0] != RW_TXD:
        raise ValueError("not a TXD")
    end = 12 + struct.unpack_from("<I", d, 4)[0]
    o = 12
    while o + 12 <= end:
        ct, cs, _ = struct.unpack_from("<III", d, o)
        if ct == RW_TEXNATIVE:
            yield parse_native(d, o + 12, o + 12 + cs)
        o += 12 + cs


def parse_native(d, o, end):
    st, ss, _ = struct.unpack_from("<III", d, o)
    if st != RW_STRUCT or ss < 92 or struct.unpack_from("<I", d, o + 12)[0] != PLATFORM_XBOX:
        return {"supported": False, "reason": "not an Xbox native texture"}, None
    b = o + 12
    name = cstr(d[b + 8:b + 40])
    mask = cstr(d[b + 40:b + 72])
    raster, alpha, w, h, depth, mips, rtype, comp, size = struct.unpack_from("<IIHHBBBBI", d, b + 72)
    data = d[b + 92:b + 92 + ss - 92]
    rec = {"supported": True, "name": name, "mask": mask, "width": w, "height": h,
           "depth": depth, "mips": mips, "compression": comp, "raster": hex(raster),
           "alpha": bool(alpha), "stored_bytes": len(data),
           "sha256": hashlib.sha256(data).hexdigest()}
    if rtype != 4:
        rec.update(supported=False, reason="raster type %d (not a 2D texture)" % rtype)
        return rec, None
    if comp in (COMP_DXT1, COMP_DXT3, COMP_DXT5):
        rec["format"] = {COMP_DXT1: "DXT1", COMP_DXT3: "DXT3", COMP_DXT5: "DXT5"}[comp]
        return rec, decode_dxt(data, w, h, comp)
    if comp == 0 and depth == 8:
        pal = [tuple(data[i * 4:i * 4 + 4]) for i in range(256)]
        rec["format"] = "P8"
        return rec, decode_swizzled(data[1024:], w, h, 1, pal)
    if comp == 0 and depth == 32:
        rec["format"] = "A8R8G8B8"
        return rec, decode_swizzled(data, w, h, 4, None)
    rec.update(supported=False, reason="compression %d depth %d" % (comp, depth))
    return rec, None


def find_txds(path):
    if os.path.isfile(path):
        return [path], os.path.dirname(path)
    found = []
    for root, _, files in os.walk(path):
        found += [os.path.join(root, f) for f in files if f.lower().endswith(".txd")]
    return sorted(found), path


def safe(name):
    return "".join(c if c.isalnum() or c in "-_." else "_" for c in name) or "unnamed"


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    listing = "--list" in argv
    src, out = args
    files, base = find_txds(src)
    manifest, skipped, count = [], 0, 0
    for f in files:
        rel = os.path.relpath(f, base)
        try:
            recs = list(parse_txd(open(f, "rb").read()))
        except (ValueError, struct.error) as e:
            print("skip %s: %s" % (rel, e))
            skipped += 1
            continue
        used = {}
        for rec, rgba in recs:
            rec["source"] = rel.replace(os.sep, "/")
            if rgba is None:
                skipped += 1
                manifest.append(rec)
                continue
            n = used.get(rec["name"], 0)
            used[rec["name"]] = n + 1
            fn = safe(rec["name"]) + ("" if n == 0 else "_%d" % n) + ".png"
            png = os.path.join(os.path.splitext(rel)[0], fn)
            rec["png"] = png.replace(os.sep, "/")
            manifest.append(rec)
            count += 1
            if not listing:
                dest = os.path.join(out, png)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                write_png(dest, rec["width"], rec["height"], rgba)
    if not listing:
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "manifest.json"), "w") as fh:
            json.dump(manifest, fh, indent=1)
    print("%s %d textures from %d TXD files; %d skipped (unsupported)" %
          ("listed" if listing else "wrote", count, len(files), skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
