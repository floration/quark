#!/usr/bin/env python3
"""Fills the website's assets/ and sounds/ pages with data: every model, map, sound and announcer line of a build.
    python3 tools/site_assets.py                       local: export from build/quark_tests, into site/assets/data/production
    python3 tools/site_assets.py --channel testing     the same, as the TESTING channel's data
    python3 tools/site_assets.py --studio build/studio --out DIR     CI: from tools/studio/build.py's output into DIR
The data is the asset export's (docs/ASSETS.md): `build/quark_tests exportassets <dir>` writes every asset and
manifest.json, `tools/studio/build.py <dir>` turns that into build/studio/. This keeps the categories the two pages
show (Models, Maps, Sounds, Voices: the site already has its pictures, and the font / icon / data aren't shown) and
writes them with a manifest.json of the same format. CI publishes it under assets/data/production/ (pages job) and
assets/data/testing/ (release-testing job). Generated: never committed (.gitignore)."""
import argparse, json, os, shutil, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KEEP = ("Models", "Maps", "Sounds", "Voices")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--channel", choices=("production", "testing"), default="production")
    ap.add_argument("--studio", help="tools/studio/build.py's output (default: export + build it now)")
    ap.add_argument("--out", help="where to write (default: site/assets/data/<channel>)")
    a = ap.parse_args()
    out = a.out or os.path.join(ROOT, "site", "assets", "data", a.channel)
    studio = a.studio
    if not studio:
        exe = os.path.join(ROOT, "build", "quark_tests" + (".exe" if os.name == "nt" else ""))
        if not os.path.exists(exe):
            sys.exit("no %s: build first (cmake -S . -B build && cmake --build build -j)" % exe)
        export = os.path.join(ROOT, "build", "assets_export")
        subprocess.run([exe, "exportassets", export], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "studio", "build.py"), export], check=True)
        studio = os.path.join(ROOT, "build", "studio")
    man = json.load(open(os.path.join(studio, "manifest.json"), encoding="utf-8"))
    items = [it for it in man["items"] if it.get("category") in KEEP]
    if not items: sys.exit("%s lists no models, maps or sounds" % studio)
    if os.path.isdir(out): shutil.rmtree(out)
    for it in items:
        dst = os.path.join(out, it["file"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(studio, it["file"]), dst)
        it.pop("owner", None)   # (the studio's "yours" mark: not the site's business)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"version": man.get("version", ""), "items": items}, f, separators=(",", ":"))
    size = sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(out) for n in ns)
    print("site assets: %d files (%.1f MB), version %s -> %s" % (len(items), size / 1e6, man.get("version", "?"), out))


main()
