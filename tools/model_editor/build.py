#!/usr/bin/env python3
"""Builds the local Quark Model Editor: one HTML page plus its model data, to open from disk (file://).

    build/quark_tests exportassets build/assets_export
    python3 tools/model_editor/build.py <export_dir> <out_dir>
    python3 tools/model_editor/build.py <export_dir> <out_dir> --page-only   (only the page, keeps the data)

<export_dir> is what `quark_tests exportassets` writes (manifest.json, models/<kind>/<NAME>.gltf, maps/<name>.qmap).
Every item of category "Models" in the manifest is taken, whatever its kind (players, weapons, projectiles, the crown,
and any kind a later game adds). <out_dir> gets:

    model-editor.html          the editor (index.html with its .css / .js inlined)
    data/manifest.js           every model (its glTF, base64), the shipped overrides of models in this repository
                               (assets/overrides/models/...), and each map's palette and sun (for the viewer)

A browser blocks fetch() on file://, so the data file is a script that calls window.qmeRegister(...).
Python 3 standard library only.
"""
import base64, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


def inline_page():
    """index.html with every local <link rel=stylesheet href=..> and <script src=..> inlined (one file to open)."""
    html = open(os.path.join(HERE, "index.html"), encoding="utf-8").read()

    def css(m):
        return "<style>\n" + open(os.path.join(HERE, m.group(1)), encoding="utf-8").read() + "\n</style>"

    def js(m):
        src = m.group(1)
        if src.startswith("data/"): return m.group(0)                      # (data stays a separate file)
        code = open(os.path.join(HERE, src), encoding="utf-8").read().replace("</script", "<\\/script")
        return "<script>/* %s */\n%s\n</script>" % (src, code)

    html = re.sub(r'<link rel="stylesheet" href="([^"]+)">', lambda m: css(m), html)
    html = re.sub(r'<script src="([^"]+)"></script>', lambda m: js(m), html)
    return html


def floats(words):
    try:
        return [float(w) for w in words]
    except ValueError:
        return None


def map_palette(path, name):
    """{id, title, base, accent, metal, glow, sky, sun} from a .qmap's title / palette / sun lines (None: no palette)."""
    title, pal, sun = name, {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        w = line.split()
        if not w: continue
        if w[0] == "title" and len(w) > 1: title = " ".join(w[1:])
        elif w[0] == "sun" and len(w) == 4: sun = floats(w[1:])
        elif w[0] == "palette":
            i = 1
            while i + 3 < len(w):   # key r g b
                v = floats(w[i + 1:i + 4])
                if v is None or len(v) < 3: break
                pal[w[i]] = v
                i += 4
    if not pal: return None
    out = {"id": name, "title": title + " (" + name + ")"}
    for k, d in (("base", [0.62, 0.62, 0.64]), ("accent", [0.40, 0.40, 0.43]), ("metal", [0.30, 0.33, 0.37]), ("glow", [1.0, 0.55, 0.15]), ("sky", [0.62, 0.66, 0.72])):
        out[k] = pal.get(k, d)
    out["sun"] = sun or [0.4, 1.0, 0.25]
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit("usage: build.py <export_dir> <out_dir> [--page-only]")
    export, out = os.path.abspath(args[0]), os.path.abspath(args[1])
    os.makedirs(out, exist_ok=True)
    page = os.path.join(out, "model-editor.html")
    if "--page-only" in sys.argv:
        with open(page, "w", encoding="utf-8") as f: f.write(inline_page())
        print("page -> " + page); return
    man_path = os.path.join(export, "manifest.json")
    if not os.path.isfile(man_path):
        sys.exit("no %s: run `build/quark_tests exportassets <dir>` first" % man_path)
    man = json.load(open(man_path, encoding="utf-8"))

    # the owner's model files already in the repository (assets/overrides/models): what the game draws now
    shipped = {}
    ov = os.path.join(ROOT, "assets", "overrides")
    for d, _, names in os.walk(os.path.join(ov, "models")):
        for n in sorted(names):
            stem, ext = os.path.splitext(n)
            if ext.lower() not in (".glb", ".gltf"): continue
            rel = os.path.relpath(os.path.join(d, stem), ov).replace(os.sep, "/").lower()
            if rel in shipped and shipped[rel]["name"].lower().endswith(".glb"): continue   # (the game takes the .glb first)
            shipped[rel] = {"name": n, "data": base64.b64encode(open(os.path.join(d, n), "rb").read()).decode("ascii")}

    models, kinds = [], {}
    for it in man.get("items", []):
        if it.get("category") != "Models": continue
        f = os.path.join(export, it["file"])
        if not os.path.isfile(f): continue
        key = os.path.splitext(it["file"])[0].replace("\\", "/")                    # models/<kind>/<NAME>
        parts = key.split("/")
        kind = it.get("kit") or (parts[1] if len(parts) > 2 else "model")
        m = {"key": key, "kind": kind, "name": it.get("name") or parts[-1], "desc": it.get("desc", ""), "file": it["file"],
             "data": base64.b64encode(open(f, "rb").read()).decode("ascii")}
        if key.lower() in shipped: m["shipped"] = shipped[key.lower()]
        models.append(m)
        kinds[kind] = kinds.get(kind, 0) + 1

    palettes = []
    mdir = os.path.join(export, "maps")
    if os.path.isdir(mdir):
        for n in sorted(os.listdir(mdir)):
            if n.endswith(".qmap"):
                p = map_palette(os.path.join(mdir, n), n[:-5])
                if p: palettes.append(p)

    data_dir = os.path.join(out, "data")
    os.makedirs(data_dir, exist_ok=True)
    payload = {"type": "manifest", "version": man.get("version") or "", "models": models, "palettes": palettes}
    with open(os.path.join(data_dir, "manifest.js"), "w", encoding="utf-8") as f:
        f.write("window.qmeRegister(")
        json.dump(payload, f, separators=(",", ":"))
        f.write(");\n")
    with open(page, "w", encoding="utf-8") as f: f.write(inline_page())
    size = os.path.getsize(os.path.join(data_dir, "manifest.js"))
    print("model editor %s: %d models (%s), %d shipped overrides, %d map palettes, %.0f KB of data -> %s" % (
        payload["version"], len(models), ", ".join("%d %s" % (v, k) for k, v in kinds.items()), sum(1 for m in models if "shipped" in m),
        len(palettes), size / 1024, page))


main()
