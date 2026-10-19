#!/usr/bin/env python3
"""Pack a directory into a .qpak (the launcher's update format).
Format: b"QPAK1\\n", then per file: u32 name_len, name (utf-8, '/' separators), u32 mode, u64 size, data."""
import os, struct, sys

src, out = sys.argv[1], sys.argv[2]
with open(out, "wb") as f:
    f.write(b"QPAK1\n")
    for root, _, files in os.walk(src):
        for name in sorted(files):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src).replace(os.sep, "/").encode()
            data = open(path, "rb").read()
            mode = os.stat(path).st_mode & 0o777
            f.write(struct.pack("<I", len(rel)) + rel + struct.pack("<IQ", mode, len(data)) + data)
