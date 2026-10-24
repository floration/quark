#!/usr/bin/env python3
r"""Installs or updates the local Bot Editor (and the Map Editor, which shares its helper) from this checkout.

    python3 tools/bot_editor/install.py [--bot-editor DIR] [--map-editor DIR] [--build DIR] [--jobs N]

Run it from the repository folder (the owner's `repository\`): non-interactive, no browser, safe to re-run. It
  1. builds the shared helpers in one CMake folder (default build-editors/, client off, low priority):
     quark_maptool (maps, routes, the bot simulator) and quark_tests (brain checks, the brain catalog, player models)
     plus the shipped maps; an unchanged checkout rebuilds nothing;
  2. --bot-editor DIR (default ../bot-editor): copies serve.py, run.cmd, run.sh, README.md, web/, both helpers, the
     maps and default.qbrain there, and writes bot_editor.json's paths (other keys, provenance.json and anything else
     the owner keeps there are left alone);
  3. --map-editor DIR (default ../map-editor; `--map-editor -` skips it): runs tools/map_editor/build.py with the
     same build folder (its own page, three.js download, maps, models; its config.json is kept).
A helper that's in use (a live run) is replaced on the next run instead of failing the install. A bot editor that's
running when its files change is told to quit (POST /api/quit) and started again from the new files; when the Quark
Project app is open (its port 8768 answers) its watchdog restarts it instead. Exit code 0 = done.
Python 3 standard library; needs cmake, a C++ compiler (MinGW-w64 on Windows) and python on PATH, like the game build.
"""
import argparse, filecmp, json, os, shutil, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""
TOOL_FILES = ["serve.py", "run.cmd", "run.sh", "README.md", "bot_editor.example.json"]


def low_priority():
    """Child processes start below normal so a build never slows the game (CLAUDE.md: the owner may be playing)."""
    if IS_WIN: return {"creationflags": 0x00004000}   # BELOW_NORMAL_PRIORITY_CLASS
    return {"preexec_fn": lambda: os.nice(10)}


def run(cmd, **kw):
    print("  $ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=ROOT, **low_priority(), **kw)
    if r.returncode != 0: sys.exit("failed (exit %d): %s" % (r.returncode, " ".join(cmd)))


def build(bdir, jobs):
    cache = os.path.join(bdir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        cmd = ["cmake", "-S", ROOT, "-B", bdir, "-DCMAKE_BUILD_TYPE=Release", "-DQUARK_BUILD_CLIENT=OFF", "-DQUARK_BUILD_TESTS=ON",
               "-DPython3_EXECUTABLE=" + sys.executable]
        if IS_WIN and shutil.which("mingw32-make"): cmd[1:1] = ["-G", "MinGW Makefiles"]
        run(cmd)
    run(["cmake", "--build", bdir, "-j", str(jobs), "--target", "quark_maptool", "quark_tests", "copy_maps"])


def place(src, dst):
    """Copy when different; a file in use (Windows: a running helper) is skipped with a note, not an error."""
    if os.path.isfile(dst) and filecmp.cmp(src, dst, shallow=False): return False
    tmp = dst + ".new"
    shutil.copy2(src, tmp)
    for attempt in range(5):
        try:
            os.replace(tmp, dst)
            return True
        except PermissionError:
            time.sleep(1)
    os.remove(tmp)
    print("  note: %s is in use (a live run?): it's updated next time" % dst)
    return False


def copy_tree(src, dst):
    n = 0
    for base, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in ("test", "node_modules", "__pycache__", ".cache")]
        out = os.path.join(dst, os.path.relpath(base, src))
        os.makedirs(out, exist_ok=True)
        for f in files: n += place(os.path.join(base, f), os.path.join(out, f))
    return n


def install_bot_editor(dest, bdir):
    dest = os.path.abspath(dest)
    os.makedirs(dest, exist_ok=True)
    n = 0
    for f in TOOL_FILES: n += place(os.path.join(HERE, f), os.path.join(dest, f))
    n += copy_tree(os.path.join(HERE, "web"), os.path.join(dest, "web"))
    for t in ("quark_maptool", "quark_tests"):
        src = os.path.join(bdir, t + EXE)
        if not os.path.isfile(src): sys.exit("no %s in %s" % (t + EXE, bdir))
        n += place(src, os.path.join(dest, t + EXE))
    maps = os.path.join(dest, "maps")
    os.makedirs(maps, exist_ok=True)
    shipped = sorted(f for f in os.listdir(os.path.join(bdir, "maps")) if f.endswith(".qmap"))
    for f in shipped: n += place(os.path.join(bdir, "maps", f), os.path.join(maps, f))
    for f in os.listdir(maps):   # a map the game no longer ships
        if f.endswith(".qmap") and f not in shipped: os.remove(os.path.join(maps, f)); n += 1
    n += place(os.path.join(ROOT, "tools", "bots", "default.qbrain"), os.path.join(dest, "default.qbrain"))
    n += place(os.path.join(ROOT, "VERSION"), os.path.join(dest, "VERSION"))

    cfg_path = os.path.join(dest, "bot_editor.json")
    cfg = {}
    if os.path.isfile(cfg_path):
        try:
            with open(cfg_path, encoding="utf-8") as f: cfg = json.load(f)
        except Exception as ex:
            print("  note: bot_editor.json unreadable (%s): rewritten" % ex)
    want = dict(cfg)
    want.setdefault("port", 8769)
    want.update({"maptool": os.path.join(dest, "quark_maptool" + EXE), "quarkTests": os.path.join(dest, "quark_tests" + EXE),
                 "defaultMapsDir": maps, "defaultBrain": os.path.join(dest, "default.qbrain")})
    if want != cfg:
        with open(cfg_path, "w", encoding="utf-8") as f: json.dump(want, f, indent=2)
        n += 1
    print("bot editor: %s (%d file(s) updated)" % (dest, n))
    if n: restart_bot_editor(dest, want["port"])


def http(url, data=None, timeout=1.5):
    """The answer's JSON, or None when nothing answers on that port."""
    req = urllib.request.Request(url, data=data, headers={"X-Quark-Editor": "1"} if data is not None else {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r: return json.loads(r.read() or b"{}")
    except Exception:
        return None


def restart_bot_editor(dest, port):
    """A running bot editor keeps serving the old serve.py and page until it restarts: ask it to quit, then start it
    again (the Quark Project app's watchdog does that itself when the app is open)."""
    base = "http://127.0.0.1:%d/api/" % port
    ping = http(base + "ping")
    if not ping or ping.get("tool") != "bot-editor": return
    if http(base + "quit", data=b"") is None:
        print("  note: the bot editor on port %d runs older code without /api/quit: restart it by hand" % port)
        return
    for _ in range(50):   # (up to 10 s: a live run's helper is stopped first)
        if http(base + "ping", timeout=0.5) is None: break
        time.sleep(0.2)
    if http("http://127.0.0.1:8768/", timeout=1.0) is not None or os.environ.get("QUARK_SHELL") == "1":
        print("  bot editor stopped: the Quark Project app restarts it from the new files")
        return
    if IS_WIN:   # (its own console window, like run.cmd from Explorer)
        subprocess.Popen(["cmd", "/c", "start", "Quark Bot Editor", os.path.join(dest, "run.cmd"), "--no-open"], cwd=dest)
    else:
        subprocess.Popen([sys.executable, os.path.join(dest, "serve.py"), "--no-open"], cwd=dest, start_new_session=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = any(http(base + "ping", timeout=0.5) or time.sleep(0.5) for _ in range(20))
    print("  bot editor restarted from the new files on port %d" % port if ok else "  note: the bot editor didn't come back on port %d: start run.cmd" % port)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bot-editor", default=os.path.join("..", "bot-editor"), help="install folder ('-' skips it)")
    ap.add_argument("--map-editor", default=os.path.join("..", "map-editor"), help="install folder ('-' skips it)")
    ap.add_argument("--build", default=os.path.join(ROOT, "build-editors"), help="CMake build folder for the helpers")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    a = ap.parse_args()
    bdir = os.path.abspath(a.build)
    print("building the helpers in %s" % bdir, flush=True)
    build(bdir, a.jobs)
    if a.bot_editor != "-": install_bot_editor(a.bot_editor, bdir)
    if a.map_editor != "-":
        run([sys.executable, os.path.join(ROOT, "tools", "map_editor", "build.py"), "--build", bdir, os.path.abspath(a.map_editor)])
    print("done")


if __name__ == "__main__":
    main()
