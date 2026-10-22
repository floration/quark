#!/usr/bin/env python3
"""Builds the Asset Studio page's files (build/studio/): every default asset of the game plus manifest.json.
    build/quark_tests exportassets build/assets_export
    python3 tools/studio/build.py [export_dir]          (default: build/assets_export)
export_dir is what `quark_tests exportassets` writes: manifest.json and every asset as a file - each sound of each of
the five sound kits and of each announcer voice (WAV), every model (glTF), every map (.qmap), the UI font, the icon,
the website's pictures - as the game has them now (its shipped overrides included). This copies them to build/studio/
and marks the ones that are already the owner's own (assets/overrides/, assets/sfx/custom/), so the page shows them as
"yours, in the game". Publish tools/studio/index.html with the files in build/studio/ (paths as they are) after each
update, so the page always shows the game's current defaults (docs/ASSETS.md)."""
import json, os, shutil, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "studio")
OVERRIDES = os.path.join(ROOT, "assets", "overrides")
CUSTOM = os.path.join(ROOT, "assets", "sfx", "custom")
CATEGORIES = ["Sounds", "Voices", "Models", "Maps", "Images", "Fonts", "Website", "Data"]   # (the page's CAT_ORDER)


def owners():
    """lower-case asset paths without extension that are the owner's own files: the shipped overrides
    (assets/overrides/<path>) and the recorded custom sounds (assets/sfx/custom/<kit>/<SOUND>.*)"""
    mine = set()
    for base, sub in ((OVERRIDES, ""), (CUSTOM, "sounds")):
        if not os.path.isdir(base): continue
        for d, _, names in os.walk(base):
            for n in names:
                stem, ext = os.path.splitext(n)
                if ext.lower() in (".md", ".txt") or n.startswith("."): continue
                rel = os.path.relpath(os.path.join(d, stem), base).replace(os.sep, "/")
                mine.add(((sub + "/") if sub else "") + rel.lower())
    return mine


def main():
    export = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "assets_export")
    man = os.path.join(export, "manifest.json")
    if not os.path.exists(man):
        sys.exit("no %s: run `build/quark_tests exportassets %s` first" % (man, export))
    src = json.load(open(man, encoding="utf-8"))
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    os.makedirs(OUT)
    mine = owners()
    items, ids, missing = [], set(), []
    for it in src["items"]:
        if it["id"] in ids: continue
        f = os.path.join(export, it["file"])
        if not os.path.isfile(f):
            missing.append(it["file"]); continue
        dst = os.path.join(OUT, it["file"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(f, dst)
        if os.path.splitext(it["file"])[0].lower() in mine: it["owner"] = True
        if it.get("category") not in CATEGORIES: print("note: category %r isn't one the page orders" % it.get("category"))
        ids.add(it["id"]); items.append(it)
    if missing: sys.exit("the export lists files it didn't write: " + ", ".join(missing[:10]))
    version = src.get("version") or open(os.path.join(ROOT, "VERSION")).read().strip()
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f: json.dump({"version": version, "items": items}, f, indent=0)
    cats, kits = {}, {}
    for it in items:
        cats[it["category"]] = cats.get(it["category"], 0) + 1
        if it.get("kit"): kits.setdefault(it["category"], set()).add(it["kit"])
    print("studio: %d assets %s -> %s" % (len(items), cats, OUT))
    for c, k in sorted(kits.items()): print("  %s: %s" % (c, ", ".join(sorted(k))))
    print("  owner's own (shown as yours): %d" % sum(1 for it in items if it.get("owner")))


main()
