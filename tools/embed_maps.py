#!/usr/bin/env python3
"""Build step (CMake): embeds the shipped maps in the game.
  python3 tools/embed_maps.py <out dir> <map.qmap>...
writes <out dir>/official_maps.h (name + sha256 of each map: ranked only runs on these) and
<out dir>/official_map_data.h (each map's text, so the game can put a map back to its original version).
The maps are the ones CMake names (the generator's, plus the hand-made ones .gitignore un-ignores), never whatever
happens to be in maps/: a stray or retired .qmap there must not become an official (ranked) map.
Line endings are normalized (CR dropped) so a CRLF checkout hashes the same. zz_* maps are test maps: left out.
Headers are only rewritten when their contents change, so an unchanged map set recompiles nothing; the stamp file
(always written) is what the build tracks."""
import hashlib, os, sys


def write_if_changed(path, text):
    try:
        with open(path, "r", newline="") as f:
            if f.read() == text: return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "w", newline="\n") as f: f.write(text)
    os.replace(tmp, path)


def main():
    out, files = sys.argv[1], sys.argv[2:]
    maps = []
    for p in files:
        name = os.path.splitext(os.path.basename(p))[0]
        if name.startswith("zz_"): continue
        with open(p, "rb") as f: data = f.read().replace(b"\r", b"")
        maps.append((name, data))
    maps.sort()
    names = [n for n, _ in maps]
    if len(set(names)) != len(names): sys.exit("embed_maps: a map name appears twice")
    os.makedirs(out, exist_ok=True)
    h = ["// generated at build time by tools/embed_maps.py from the shipped maps", "#pragma once",
         "struct OfficialMap { const char* name; const char* sha256; };", "static const OfficialMap kOfficialMaps[] = {"]
    h += ['    {"%s", "%s"},' % (n, hashlib.sha256(d).hexdigest()) for n, d in maps]
    h += ["    {nullptr, nullptr}};", ""]
    write_if_changed(os.path.join(out, "official_maps.h"), "\n".join(h))
    d = ["// generated at build time by tools/embed_maps.py: the original text of every shipped map", "#pragma once", "#include <cstddef>"]
    for n, data in maps:
        d.append("static const unsigned char kMapData_%s[] = {" % n)
        for i in range(0, len(data), 64): d.append(",".join(str(b) for b in data[i:i + 64]) + ",")
        d.append("};")
    d += ["struct OfficialMapData { const char* name; const unsigned char* data; size_t size; };",
          "static const OfficialMapData kOfficialMapData[] = {"]
    d += ['    {"%s", kMapData_%s, sizeof kMapData_%s},' % (n, n, n) for n, _ in maps]
    d += ["    {nullptr, nullptr, 0}};", ""]
    write_if_changed(os.path.join(out, "official_map_data.h"), "\n".join(d))
    with open(os.path.join(out, "official_maps.stamp"), "w") as f: f.write("\n".join(names) + "\n")   # (always: the build's timestamp)


if __name__ == "__main__":
    main()
