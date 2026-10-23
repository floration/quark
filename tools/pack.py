#!/usr/bin/env python3
"""Pack a directory into a .qpak (the launcher's update format).
Format: b"QPAK1\\n", then per file: u32 name_len, name (utf-8, '/' separators), u32 mode, u64 size, data.
The last entry is installed-files.txt: every packed file with its sha256 (src/launcher/gamefiles.h), so an update
deletes only the game files a later version retires, even when an older launcher installed this package."""
import hashlib, os, struct, sys

LIST = "installed-files.txt"
src, out = sys.argv[1], sys.argv[2]


def entry(rel, mode, data):
    rel = rel.encode()
    return struct.pack("<I", len(rel)) + rel + struct.pack("<IQ", mode, len(data)) + data


listed = []
with open(out, "wb") as f:
    f.write(b"QPAK1\n")
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src).replace(os.sep, "/")
            if rel == LIST:
                continue  # (written below, from what's packed)
            data = open(path, "rb").read()
            mode = os.stat(path).st_mode & 0o777
            f.write(entry(rel, mode, data))
            listed.append((rel, hashlib.sha256(data).hexdigest()))
    text = "quark installed files 1\n" + "".join("%s %s\n" % (h, r) for r, h in sorted(listed))
    f.write(entry(LIST, 0o644, text.encode()))
