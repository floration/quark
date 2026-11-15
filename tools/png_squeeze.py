#!/usr/bin/env python3
"""Compress the PNGs `quark_tests mappreviews` writes (stored deflate: the game has no compressor) for the website's
img/rpc/ (the Discord status's map pictures): each row's best PNG filter, then zlib level 9. Lossless; the pixels are
unchanged. Python standard library only.

usage: png_squeeze.py <dir>    (every *.png in it, rewritten in place; 8-bit RGB / RGBA, not interlaced)
"""
import os
import struct
import sys
import zlib


def chunks(data):
    at = 8
    while at + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        yield kind, data[at + 8:at + 8 + n]
        at += 12 + n


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def filtered(row, prev, bpp):
    """The row under each filter (0-4); the one with the smallest sum of signed bytes wins (libpng's heuristic)."""
    n = len(row)
    out = [bytes([0]) + row]
    sub = bytearray(n)
    up = bytearray(n)
    avg = bytearray(n)
    pa = bytearray(n)
    for i in range(n):
        left = row[i - bpp] if i >= bpp else 0
        above = prev[i]
        ul = prev[i - bpp] if i >= bpp else 0
        sub[i] = (row[i] - left) & 255
        up[i] = (row[i] - above) & 255
        avg[i] = (row[i] - ((left + above) >> 1)) & 255
        pa[i] = (row[i] - paeth(left, above, ul)) & 255
    out += [bytes([1]) + bytes(sub), bytes([2]) + bytes(up), bytes([3]) + bytes(avg), bytes([4]) + bytes(pa)]
    return min(out, key=lambda r: sum(b if b < 128 else 256 - b for b in r[1:]))


def squeeze(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    ihdr, idat = None, b""
    for kind, body in chunks(data):
        if kind == b"IHDR":
            ihdr = body
        elif kind == b"IDAT":
            idat += body
    w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", ihdr)
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("only 8-bit RGB / RGBA, not interlaced")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, out = [], bytearray(stride), []
    for y in range(h):   # (undo the filters the file has: the game writes 0, but any is read)
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            above = prev[i]
            ul = prev[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + (0, left, above, (left + above) >> 1, paeth(left, above, ul))[f]) & 255 if f else line[i]
        rows.append(bytes(line))
        prev = line
    prev = bytes(stride)
    for line in rows:
        out.append(filtered(line, prev, bpp))
        prev = line
    body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"".join(out), 9)) + chunk(b"IEND", b"")
    with open(path + ".tmp", "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + body)
    os.replace(path + ".tmp", path)
    return len(data), 8 + len(body)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    before = after = n = 0
    for name in sorted(os.listdir(sys.argv[1])):
        if name.endswith(".png"):
            a, b = squeeze(os.path.join(sys.argv[1], name))
            before += a
            after += b
            n += 1
    print(f"png_squeeze: {n} files, {before // 1024} KB -> {after // 1024} KB")


if __name__ == "__main__":
    main()
