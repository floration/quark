#!/usr/bin/env python3
"""Build step (CMake): embeds the languages in the game (src/client/lang.h).
  python3 tools/lang/embed.py <out header> <assets/lang dir>
writes kLangKeys (keys.txt), kEmbeddedLangs[] (each <code>.tsv) and kEmbeddedAtlases[] (each atlas/<name>.png with its
<name>.glyphs list, made by tools/lang/make_atlas.py) to <out header>, only rewriting it when its contents change."""
import os, sys


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


def arr(h, name, data, text):
    h.append("static const %s %s[] = {" % ("char" if text else "unsigned char", name))
    for k in range(0, len(data), 64): h.append(",".join(str(b if not text or b < 128 else b - 256) for b in data[k:k + 64]) + ",")
    h.append("0};")


def main():
    out, root = sys.argv[1], sys.argv[2]
    h = ["// generated at build time by tools/lang/embed.py from assets/lang/ (src/client/lang.h)", "#pragma once",
         "#include <cstddef>",
         "struct EmbeddedLang { const char* code; const char* tsv; size_t tsvSize; };",
         "struct EmbeddedAtlas { const char* name; const unsigned char* png; size_t pngSize; const char* glyphs; size_t glyphsSize; };"]
    keys = open(os.path.join(root, "keys.txt"), "rb").read() if os.path.isfile(os.path.join(root, "keys.txt")) else b""
    arr(h, "kLangKeys", keys, True)
    h.append("static const size_t kLangKeysSize = %d;" % len(keys))
    langs = sorted(f[:-4] for f in os.listdir(root) if f.endswith(".tsv"))
    for i, c in enumerate(langs): arr(h, "kLangTsv%d" % i, open(os.path.join(root, c + ".tsv"), "rb").read(), True)
    h.append("static const EmbeddedLang kEmbeddedLangs[] = {")
    for i, c in enumerate(langs): h.append('    {"%s", kLangTsv%d, %d},' % (c, i, os.path.getsize(os.path.join(root, c + ".tsv"))))
    h.append("    {nullptr, nullptr, 0}};")
    ad = os.path.join(root, "atlas")
    names = sorted(f[:-4] for f in os.listdir(ad) if f.endswith(".png") and os.path.isfile(os.path.join(ad, f[:-4] + ".glyphs"))) if os.path.isdir(ad) else []
    for i, n in enumerate(names):
        arr(h, "kAtlasPng%d" % i, open(os.path.join(ad, n + ".png"), "rb").read(), False)
        arr(h, "kAtlasGlyphs%d" % i, open(os.path.join(ad, n + ".glyphs"), "rb").read(), True)
    h.append("static const EmbeddedAtlas kEmbeddedAtlases[] = {")
    for i, n in enumerate(names):
        h.append('    {"%s", kAtlasPng%d, %d, kAtlasGlyphs%d, %d},' % (n, i, os.path.getsize(os.path.join(ad, n + ".png")), i, os.path.getsize(os.path.join(ad, n + ".glyphs"))))
    h += ["    {nullptr, nullptr, 0, nullptr, 0}};", ""]
    write_if_changed(out, "\n".join(h))


if __name__ == "__main__":
    main()
