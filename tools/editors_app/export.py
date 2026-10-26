#!/usr/bin/env python3
"""Stages Quarkology Editors: the editors-only app (no Supervisor Board, Claude, GitHub or Tests) that anyone can run.

    python3 tools/editors_app/export.py --maptool build-win/quark_maptool.exe --assets build/assets_export \
        --python python.3.12.10.nupkg --out editors-stage

Writes <out>/ ready for electron-builder (tools/editors_app/package.json, run in <out>):
  app files (main.js, preload.js, index.html, tabs.json, package.json)
  bundle/   what the editors run from, laid out like a checkout so each serve.py finds its files as it does at home:
            tools/<editor folders>, maps/*.qmap, VERSION, build/quark_maptool.exe, python/ (Windows Python, from the
            NuGet "python" package), pages/<sound|model|ui>-editor/ (the static editors, built here)
Left out on purpose (and checked): the Quarkology tools that need q's PC, git or accounts, the signing and ranking
server tools, anything secret-looking. The editors save into QUARK_DATA_DIR (main.js: Documents\\Quarkology Editors).
"""
import argparse, os, re, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
APP_FILES = ("main.js", "preload.js", "index.html", "tabs.json", "package.json")
# tools/ folders the editors need; everything else in tools/ stays out
TOOLS = ("bot_editor", "bots", "map_editor", "maps", "mode_editor", "modes", "physics_editor", "weapon_editor", "hud_editor")
TOOL_FILES = ("provenance.py",)
NEVER = ("docs_manager", "github_manager", "project_manager", "tests_editor", "editor_dashboard", "master", "sign",
         "githooks", "local", "server", "Claude Documents")
SECRET_RE = re.compile(rb"(BEGIN [A-Z ]*PRIVATE KEY|ghp_[A-Za-z0-9]{30}|github_pat_|discord(app)?\.com/api/webhooks/|xox[bp]-)")
SKIP = shutil.ignore_patterns("__pycache__", "*.pyc", "test", "tests", "*.example.json", "node_modules")


def run(cmd, **kw):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--maptool", required=True, help="the Windows quark_maptool.exe (CI: the windows job's build)")
    ap.add_argument("--assets", required=True, help="a quark_tests exportassets folder of this checkout")
    ap.add_argument("--python", required=True, help="the NuGet python package (.nupkg) for Windows x64")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    out = os.path.abspath(a.out)
    if os.path.exists(out): shutil.rmtree(out)
    bundle = os.path.join(out, "bundle")
    os.makedirs(os.path.join(bundle, "tools"))
    for f in APP_FILES: shutil.copy2(os.path.join(HERE, f), out)
    for t in TOOLS: shutil.copytree(os.path.join(ROOT, "tools", t), os.path.join(bundle, "tools", t), ignore=SKIP)
    for f in TOOL_FILES: shutil.copy2(os.path.join(ROOT, "tools", f), os.path.join(bundle, "tools", f))
    shutil.copy2(os.path.join(ROOT, "VERSION"), bundle)
    maps = os.path.join(bundle, "maps"); os.makedirs(maps)
    for n in sorted(os.listdir(os.path.join(ROOT, "maps"))):
        if n.endswith(".qmap"): shutil.copy2(os.path.join(ROOT, "maps", n), maps)
    os.makedirs(os.path.join(bundle, "build"))
    shutil.copy2(a.maptool, os.path.join(bundle, "build", "quark_maptool.exe"))

    # Windows Python: the package's tools/ folder is a full install (python.exe, Lib, DLLs); no pip, no tests
    py = os.path.join(bundle, "python")
    with zipfile.ZipFile(a.python) as z:
        for m in z.infolist():
            n = m.filename
            if not n.startswith("tools/") or n.endswith("/"): continue
            rel = n[len("tools/"):]
            if re.match(r"Lib/(test|idlelib|tkinter|turtledemo|ensurepip|site-packages)/", rel): continue
            dst = os.path.join(py, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(m) as s, open(dst, "wb") as d: shutil.copyfileobj(s, d)
    if not os.path.isfile(os.path.join(py, "python.exe")): sys.exit("no tools/python.exe in " + a.python)

    # the map editor's three.js (its build.py downloads it, sha256-checked)
    src = open(os.path.join(ROOT, "tools", "map_editor", "build.py"), encoding="utf-8").read()
    map_build = {"__file__": os.path.join(ROOT, "tools", "map_editor", "build.py"), "__name__": "map_build"}
    exec(compile(src[:src.rindex("\nmain()")], "map_editor/build.py", "exec"), map_build)
    vend = os.path.join(bundle, "tools", "map_editor", "vendor"); os.makedirs(vend)
    open(os.path.join(vend, map_build["THREE_FILE"]), "wb").write(map_build["three_js"](os.path.join(ROOT, "build", "map_editor_vendor")))

    # the static editors (one HTML page each, with their data next to it)
    pages = os.path.join(bundle, "pages")
    run([sys.executable, os.path.join(ROOT, "tools", "sound_editor", "build.py"), a.assets, os.path.join(pages, "sound-editor")])
    run([sys.executable, os.path.join(ROOT, "tools", "model_editor", "build.py"), a.assets, os.path.join(pages, "model-editor")])
    run([sys.executable, os.path.join(ROOT, "tools", "ui_customizer", "install.py"), "--dest", os.path.join(pages, "ui-customizer")],
        env=dict(os.environ, QUARK_SHELL="1"))

    # checks: nothing left-out made it in, nothing secret-looking, every page and server there
    bad = []
    for d, dirs, files in os.walk(out):
        for n in dirs + files:
            if n in NEVER: bad.append(os.path.join(d, n))
        if os.sep + "python" in d[len(out):]: continue
        for n in files:
            p = os.path.join(d, n)
            if os.path.getsize(p) < 4 << 20 and SECRET_RE.search(open(p, "rb").read()): bad.append(p + " (looks secret)")
    for f in ("pages/sound-editor/sound-editor.html", "pages/model-editor/model-editor.html", "pages/ui-customizer/ui-customizer.html",
              "tools/map_editor/serve.py", "tools/bot_editor/serve.py", "tools/mode_editor/serve.py",
              "tools/physics_editor/serve.py", "tools/weapon_editor/serve.py", "tools/hud_editor/serve.py"):
        if not os.path.isfile(os.path.join(bundle, *f.split("/"))): bad.append("missing " + f)
    if bad: sys.exit("export_editors: refused:\n  " + "\n  ".join(bad))
    print("staged Quarkology Editors in %s" % out)


if __name__ == "__main__":
    main()
