#!/usr/bin/env python3
"""Build step (CMake): embeds the owner's shipped asset overrides (assets/overrides/, docs/ASSETS.md) in the game.
  python3 tools/embed_overrides.py <out header> <overrides dir>
writes kEmbeddedOverrides[] (path relative to the overrides dir, bytes) to <out header>. Everything under the folder
is embedded except maps/ (tools/make_maps.py and embed_maps.py take those: a map is official map data, not a file
the game looks up), website/ (the site's pictures: the release workflow copies them over site/img/) and notes
(*.md, *.txt; except sound-levels.txt at the root, the list of sounds that keep their own level, which the game
reads). No overrides: an empty table, and the game never looks further.
The header is only rewritten when its contents change, so an unchanged set recompiles nothing."""
import os, sys

SKIP_DIRS = {"maps", "website"}
SKIP_EXT = {".md", ".txt"}
KEEP_ROOT = {"sound-levels.txt"}   # (root notes the game reads: embedded despite the extension)


def write_if_changed(path, text):
    try:
        with open(path, "r", newline="") as f:
            if f.read() == text: return
    except OSError:
        pass
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", newline="\n") as f: f.write(text)
    os.replace(tmp, path)


def files(root):
    out = []
    if not os.path.isdir(root): return out
    for d, dirs, names in os.walk(root):
        rel_d = os.path.relpath(d, root).replace(os.sep, "/")
        if rel_d == ".":
            dirs[:] = [x for x in dirs if x.lower() not in SKIP_DIRS]
        dirs.sort()
        for n in sorted(names):
            if n.startswith("."): continue
            if os.path.splitext(n)[1].lower() in SKIP_EXT and not (rel_d == "." and n.lower() in KEEP_ROOT): continue
            rel = n if rel_d == "." else rel_d + "/" + n
            out.append((rel, os.path.join(d, n)))
    return out


def main():
    out, root = sys.argv[1], sys.argv[2]
    h = ["// generated at build time by tools/embed_overrides.py from assets/overrides/ (docs/ASSETS.md)", "#pragma once",
         "#include <cstddef>", "struct EmbeddedOverride { const char* path; const unsigned char* data; size_t size; };"]
    entries = []
    for i, (rel, path) in enumerate(files(root)):
        if any(c in rel for c in '"\\\n'): sys.exit("embed_overrides: bad file name " + rel)
        data = open(path, "rb").read()
        h.append("static const unsigned char kOverride%d[] = {" % i)
        for k in range(0, len(data), 48): h.append(",".join(str(b) for b in data[k:k + 48]) + ",")
        if not data: h.append("0")
        h.append("};")
        entries.append('    {"%s", kOverride%d, %d},' % (rel, i, len(data)))
    h.append("static const EmbeddedOverride kEmbeddedOverrides[] = {")
    h += entries
    h += ["    {nullptr, nullptr, 0}};", ""]
    write_if_changed(out, "\n".join(h))


if __name__ == "__main__":
    main()
