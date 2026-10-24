#!/usr/bin/env python3
"""Builds the local Quark Map Editor into a folder you run it from.

    cmake --build build -j                          (quark_maptool, the maps, quark_tests)
    python3 tools/map_editor/build.py [out_dir]     (default build/map_editor)
        --build DIR     the CMake build folder (default build/)
        --no-models     skip the player models (quark_tests exportassets)
        --page-only     only the page and serve.py (keeps the helper, maps, models, three.js there)

<out_dir> gets:
    index.html, editor.css, js/        the page (this folder's), deco_presets/ (the decoration panel's presets)
    vendor/three.module.min.js         three.js 0.160.0, downloaded once (cached in <build>/map_editor_vendor/) and
                                       checked against THREE_SHA256 below; never committed
    serve.py, run.cmd, run.sh          the local server and its starters
    quark_maptool[.exe]                the native helper (from the build folder)
    maps/*.qmap                        the default maps the build generated (read-only in the editor)
    models/player/*.gltf               the player models, from `quark_tests exportassets` (when it is built)
    VERSION                            the game version these came from
Files you made there (config.json) are kept. Python 3 standard library only.
"""
import hashlib, io, os, shutil, subprocess, sys, tarfile, tempfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

THREE_VERSION = "0.160.0"
THREE_FILE = "three.module.min.js"
THREE_SHA256 = "3e690ac7d180b0aadf0891bea39eec643e29e2d3e75c99b18689518665f69ba6"   # build/three.module.min.js of three@0.160.0
THREE_URLS = [
    "https://cdn.jsdelivr.net/npm/three@%s/build/%s" % (THREE_VERSION, THREE_FILE),
    "https://unpkg.com/three@%s/build/%s" % (THREE_VERSION, THREE_FILE),
]
THREE_TGZ = "https://registry.npmjs.org/three/-/three-%s.tgz" % THREE_VERSION   # (the npm package, when the CDNs are blocked)

PAGE = ["index.html", "editor.css"]


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def three_js(cache_dir):
    """three.module.min.js, verified: the cache, else a CDN, else the npm tarball."""
    cached = os.path.join(cache_dir, THREE_FILE)
    if os.path.isfile(cached):
        data = open(cached, "rb").read()
        if hashlib.sha256(data).hexdigest() == THREE_SHA256: return data
    data, errors = None, []
    for url in THREE_URLS:
        try:
            d = fetch(url)
            if hashlib.sha256(d).hexdigest() == THREE_SHA256: data = d; break
            errors.append("%s: sha256 mismatch" % url)
        except Exception as e:
            errors.append("%s: %s" % (url, e))
    if data is None:
        try:
            tgz = fetch(THREE_TGZ)
            with tarfile.open(fileobj=io.BytesIO(tgz), mode="r:gz") as t:
                d = t.extractfile("package/build/" + THREE_FILE).read()
            if hashlib.sha256(d).hexdigest() == THREE_SHA256: data = d
            else: errors.append("%s: sha256 mismatch" % THREE_TGZ)
        except Exception as e:
            errors.append("%s: %s" % (THREE_TGZ, e))
    if data is None:
        sys.exit("could not get three.js %s:\n  %s\n(or put a verified %s in %s)" % (THREE_VERSION, "\n  ".join(errors), THREE_FILE, cache_dir))
    os.makedirs(cache_dir, exist_ok=True)
    open(cached, "wb").write(data)
    return data


def copy_tree(src, dst, pred=lambda n: True):
    os.makedirs(dst, exist_ok=True)
    for n in os.listdir(dst):     # (old files of this kind only)
        p = os.path.join(dst, n)
        if os.path.isfile(p) and pred(n): os.remove(p)
    count = 0
    for n in sorted(os.listdir(src)):
        if os.path.isfile(os.path.join(src, n)) and pred(n):
            shutil.copy2(os.path.join(src, n), os.path.join(dst, n)); count += 1
    return count


def main():
    a = sys.argv[1:]
    build = os.path.join(ROOT, "build")
    if "--build" in a:
        i = a.index("--build"); build = os.path.abspath(a[i + 1]); del a[i:i + 2]
    models = "--no-models" not in a
    a = [x for x in a if not x.startswith("--")]
    out = os.path.abspath(a[0]) if a else os.path.join(build, "map_editor")
    if os.path.normpath(out) == os.path.normpath(HERE): sys.exit("the output folder can't be the source folder")
    os.makedirs(out, exist_ok=True)

    for n in PAGE: shutil.copy2(os.path.join(HERE, n), os.path.join(out, n))
    njs = copy_tree(os.path.join(HERE, "js"), os.path.join(out, "js"), lambda n: n.endswith(".js"))
    if os.path.isdir(os.path.join(HERE, "deco_presets")):   # (the decoration panel's shipped presets)
        copy_tree(os.path.join(HERE, "deco_presets"), os.path.join(out, "deco_presets"), lambda n: n.endswith(".json"))
    for n in ("serve.py", "run.cmd", "run.sh", "README.md", "INTERFACE.md"):
        if os.path.isfile(os.path.join(HERE, n)): shutil.copy2(os.path.join(HERE, n), os.path.join(out, n))
    try: os.chmod(os.path.join(out, "run.sh"), 0o755)
    except OSError: pass
    if "--page-only" in sys.argv:
        print("map editor page (%d scripts) -> %s" % (njs, out)); return

    vend = os.path.join(out, "vendor")
    os.makedirs(vend, exist_ok=True)
    open(os.path.join(vend, THREE_FILE), "wb").write(three_js(os.path.join(build, "map_editor_vendor")))

    exe = ".exe" if sys.platform == "win32" else ""
    tool = None
    for d in (build, os.path.join(build, "Release"), os.path.join(build, "RelWithDebInfo")):
        if os.path.isfile(os.path.join(d, "quark_maptool" + exe)): tool = os.path.join(d, "quark_maptool" + exe); break
    if tool: shutil.copy2(tool, os.path.join(out, "quark_maptool" + exe))
    else: print("  warning: no quark_maptool%s in %s: the editor will only view maps (build the quark_maptool target)" % (exe, build))

    maps_src = os.path.join(ROOT, "maps")
    nmaps = copy_tree(maps_src, os.path.join(out, "maps"), lambda n: n.endswith(".qmap")) if os.path.isdir(maps_src) else 0
    if not nmaps: print("  warning: no maps in %s (cmake --build build makes them)" % maps_src)

    nmodels = 0
    tests = None
    for d in (build, os.path.join(build, "Release"), os.path.join(build, "RelWithDebInfo")):
        if os.path.isfile(os.path.join(d, "quark_tests" + exe)): tests = os.path.join(d, "quark_tests" + exe); break
    if models and tests:
        with tempfile.TemporaryDirectory() as tmp:
            r = subprocess.run([tests, "exportassets", tmp], cwd=build, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            pm = os.path.join(tmp, "models", "player")
            if os.path.isdir(pm):
                nmodels = copy_tree(pm, os.path.join(out, "models", "player"), lambda n: n.endswith(".gltf"))
            else:
                print("  warning: exportassets wrote no player models (exit %d)" % r.returncode)
    elif models:
        print("  note: no quark_tests in %s: figures use the player hull box (no models)" % build)

    version = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip() if os.path.isfile(os.path.join(ROOT, "VERSION")) else ""
    open(os.path.join(out, "VERSION"), "w", encoding="utf-8").write(version + "\n")
    print("map editor %s: %d scripts, %d maps, %d player models, helper %s -> %s" % (
        version, njs, nmaps, nmodels, "yes" if tool else "NO", out))
    print("run: %s" % os.path.join(out, "run.cmd" if sys.platform == "win32" else "run.sh"))


main()
