#!/usr/bin/env python3
"""Builds the Asset Studio page's files (build/studio/): every default asset of the game plus manifest.json.
    python3 tools/studio/build.py [export_dir]
export_dir is the output of `build/quark_tests exportassets <dir>` (sounds, models, font... rendered from the game's
code). Everything that's already a file (maps, icon, fonts, website images, recorded sound sources) is added here.
Republish the page with the files in build/studio/ after each update so it always shows the game's current defaults."""
import json, os, re, shutil, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "build", "studio")

def main():
    export = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "assets_export")
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    os.makedirs(OUT)
    items, ids = [], set()
    def add(item, src=None):
        if item["id"] in ids: return
        if src:
            dst = os.path.join(OUT, item["file"])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
        ids.add(item["id"]); items.append(item)
    version = open(os.path.join(ROOT, "VERSION")).read().strip()
    # what the game renders from code (sounds, models...): the exporter's own manifest
    man = os.path.join(export, "manifest.json")
    if os.path.exists(man):
        for it in json.load(open(man))["items"]:
            src = os.path.join(export, it["file"])
            if os.path.exists(src): add(it, src)
    # maps (the files themselves)
    for f in sorted(os.listdir(os.path.join(ROOT, "maps"))):
        if not f.endswith(".qmap"): continue
        name = f[:-5]
        text = open(os.path.join(ROOT, "maps", f), encoding="utf-8", errors="replace").read()
        mode = (re.search(r"^for (\w+)", text, re.M) or [None, ""])[1]
        setting = (re.search(r"^# setting (.+)$", text, re.M) or [None, ""])[1]
        title = (re.search(r"^title (.+)$", text, re.M) or [None, name])[1]
        add({"id": "map/" + name, "category": "Maps", "name": title.strip().upper(), "file": "maps/" + f, "kind": "map",
             "desc": " - ".join(x for x in (("built for " + mode) if mode else "", setting) if x)}, os.path.join(ROOT, "maps", f))
    # images, fonts
    add({"id": "image/icon", "category": "Images", "name": "GAME ICON", "file": "images/icon.png", "kind": "image",
         "desc": "app, launcher and website icon"}, os.path.join(ROOT, "assets", "quark.png"))
    img = os.path.join(ROOT, "site", "img")
    for f in sorted(os.listdir(img)) if os.path.isdir(img) else []:
        if f.lower().endswith((".jpg", ".png")):
            add({"id": "website/" + os.path.splitext(f)[0], "category": "Website", "name": os.path.splitext(f)[0].upper(),
                 "file": "website/" + f, "kind": "image", "desc": "map picture on the website"}, os.path.join(img, f))
    add({"id": "font/worksans", "category": "Fonts", "name": "WORK SANS", "file": "fonts/WorkSans.ttf", "kind": "font",
         "desc": "in-game UI text (Bold); baked by tools/make_font_atlas.py - Work Sans Project Authors, SIL OFL 1.1"},
        os.path.join(ROOT, "assets", "fonts", "WorkSans.ttf"))
    add({"id": "font/rubik", "category": "Fonts", "name": "RUBIK", "file": "fonts/Rubik.ttf", "kind": "font",
         "desc": "launcher and website text"}, os.path.join(ROOT, "assets", "fonts", "Rubik.ttf"))
    # the owner's own sounds already in the game (assets/sfx/custom/<kit>/<SOUND>.wav), when the exporter didn't render them
    cust = os.path.join(ROOT, "assets", "sfx", "custom")
    for kit in sorted(os.listdir(cust)) if os.path.isdir(cust) else []:
        d = os.path.join(cust, kit)
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            n, ext = os.path.splitext(f)
            if ext.lower() == ".wav":
                add({"id": "sound/%s/%s" % (kit, n.upper()), "category": "Sounds", "kit": kit, "name": n.upper(), "file": "sounds/%s/%s" % (kit, f),
                     "kind": "sound", "desc": "", "owner": True}, os.path.join(d, f))
    json.dump({"version": version, "items": items}, open(os.path.join(OUT, "manifest.json"), "w"), indent=0)
    cats = {}
    for it in items: cats[it["category"]] = cats.get(it["category"], 0) + 1
    print("studio:", len(items), "assets", cats, "->", OUT)

main()
