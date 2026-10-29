#!/usr/bin/env python3
"""Compress finished .qpak packages into .qpakz (src/launcher/qpak.h): the same files, each one a zlib stream (level 9),
or stored as is when that isn't smaller. Run after the launcher's signature went into the .qpak (release.yml), so both
hold the same files; the .qpakz is written next to it (quark-windows.qpak -> quark-windows.qpakz).
Launchers since this was added download the .qpakz when a release has one (about half the size); older ones keep reading the
.qpak, which every release still publishes.
  tools/qpakz.py PACKAGE.qpak...
  tools/qpakz.py --check PACKAGE.qpakz PACKAGE.qpak   (the two hold the same files: exit 1 if not)"""
import struct, sys, zlib


def entries(path):
    """Yield (name, mode, data) from a .qpak or .qpakz."""
    with open(path, "rb") as f:
        magic = f.read(6)
        if magic not in (b"QPAK1\n", b"QPAK2\n"):
            raise SystemExit("%s: not a qpak" % path)
        packed = magic == b"QPAK2\n"
        while True:
            head = f.read(4)
            if not head:
                return
            (nl,) = struct.unpack("<I", head)
            name = f.read(nl).decode()
            mode, size = struct.unpack("<IQ", f.read(12))
            if packed:
                (csize,) = struct.unpack("<Q", f.read(8))
                raw = f.read(csize)
                data = raw if csize == size else zlib.decompress(raw)
            else:
                data = f.read(size)
            if len(data) != size:
                raise SystemExit("%s: truncated at %s" % (path, name))
            yield name, mode, data


def compress(src, dst):
    with open(dst, "wb") as out:
        out.write(b"QPAK2\n")
        for name, mode, data in entries(src):
            z = zlib.compress(data, 9)
            stored = z if len(z) < len(data) else data
            n = name.encode()
            out.write(struct.pack("<I", len(n)) + n + struct.pack("<IQQ", mode, len(data), len(stored)) + stored)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--check"]:
        sys.exit(0 if list(entries(args[1])) == list(entries(args[2])) else 1)
    for p in args:
        if not p.endswith(".qpak"):
            raise SystemExit("%s: not a .qpak" % p)
        compress(p, p + "z")
        if list(entries(p + "z")) != list(entries(p)):
            raise SystemExit("%s: the .qpakz doesn't match" % p)
        print("%s -> %sz" % (p, p))
