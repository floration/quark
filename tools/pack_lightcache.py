#!/usr/bin/env python3
"""Packaging step (release.yml's lightcache job, tools/build_windows.ps1): compresses quark --bake-maps' output.
  python3 tools/pack_lightcache.py <bake dir> <out dir>
Each <sha256>.qlb in <bake dir> (a raw light cache, the bytes of the game's own cache/<sha256>.qlm) becomes
<out dir>/<sha256>.qlbz, those bytes as one zlib stream, which the game inflates when it loads the map
(src/client/lightmap.cpp readShipped). 84 MB of bakes ship as ~12 (packages and launcher updates aren't compressed).
Any other .qlbz in <out dir> is removed, so it holds exactly this build's bakes. Python standard library only."""
import os, sys, time, zlib


def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    names = sorted(n for n in os.listdir(src) if n.endswith(".qlb"))
    if not names: sys.exit("pack_lightcache: no .qlb files in %s (run quark --bake-maps first)" % src)
    os.makedirs(out, exist_ok=True)
    t0, raw, packed = time.time(), 0, 0
    keep = set()
    for n in names:
        with open(os.path.join(src, n), "rb") as f: data = f.read()
        z = zlib.compress(data, 9)
        dst = os.path.join(out, n + "z")
        keep.add(n + "z")
        with open(dst + ".tmp", "wb") as f: f.write(z)
        os.replace(dst + ".tmp", dst)
        raw, packed = raw + len(data), packed + len(z)
    for n in os.listdir(out):
        if n.endswith(".qlbz") and n not in keep: os.remove(os.path.join(out, n))
    print("light bakes: %d maps, %.1f MB -> %.1f MB (%.1f s)" % (len(names), raw / 1e6, packed / 1e6, time.time() - t0))


main()
