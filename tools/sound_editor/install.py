"""Install or update the sound editor next to the repository checkout, non-interactively (the Quark Project app runs it).

    python tools/sound_editor/install.py [out_dir] [--no-build]   (cwd: any; out_dir default: <checkout>/../sound-editor)

It never prompts, never opens a browser or a console window, and it is safe to re-run. The sounds come from the game's
asset export, which needs a built quark_tests of this checkout's VERSION. It looks in $QUARK_TESTS first, then in every
build folder the other installs and builds use, newest quark_tests first:
  <checkout>/build-win, build, build-model-editor (model editor), build-editors (bot + map editors),
  <Quark>/working-repo-sound/build-win, <Quark>/.src/*/build*
and the first whose export names the same VERSION wins (an older build would export old sounds and voice lines, so it
is skipped with a warning). When none does, it builds quark_tests itself in build-editors (the bot editor's folder and
settings, so neither install builds twice), at low priority with no window, and exports with that (--no-build skips
this). Without a usable build: only the page is rebuilt when data/ is already there (the sounds stay as last exported)
and it exits 2 when only stale builds were found; with no data at all it exits 1 and says what to build.
"""
import glob, json, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
QUARK = os.path.dirname(ROOT)
IS_WIN = os.name == "nt"
EXE = "quark_tests.exe" if IS_WIN else "quark_tests"
BUILD_DIR = os.path.join(ROOT, "build-editors")   # (tools/bot_editor/install.py builds its helpers here)


def quiet():
    """Child processes: below normal priority (the owner may be playing) and, on Windows, no console window."""
    if IS_WIN: return {"creationflags": 0x08000000 | 0x00004000}   # CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS
    return {"preexec_fn": lambda: os.nice(10)}


def find_tests():
    """Every quark_tests to try, in order: $QUARK_TESTS, then the build folders' newest first."""
    dirs = [os.path.join(ROOT, d) for d in ("build-win", "build", "build-model-editor", "build-editors")]
    dirs += [os.path.join(QUARK, "working-repo-sound", "build-win")] + sorted(glob.glob(os.path.join(QUARK, ".src", "*", "build*")))
    found = [os.path.join(d, EXE) for d in dirs]
    found = [c for c in found if os.path.isfile(c)]
    found.sort(key=lambda c: os.path.getmtime(c), reverse=True)
    cands, seen = [os.environ.get("QUARK_TESTS", "")] + found, set()
    return [c for c in cands if c and os.path.isfile(c) and not (os.path.realpath(c) in seen or seen.add(os.path.realpath(c)))]


def build_tests():
    """Builds quark_tests in build-editors (configured like the bot editor's install); returns its path or None."""
    print("sound editor: building quark_tests in %s (low priority; minutes on a fresh folder)" % BUILD_DIR, flush=True)
    try:
        if not os.path.isfile(os.path.join(BUILD_DIR, "CMakeCache.txt")):
            cmd = ["cmake", "-S", ROOT, "-B", BUILD_DIR, "-DCMAKE_BUILD_TYPE=Release", "-DQUARK_BUILD_CLIENT=OFF",
                   "-DQUARK_BUILD_TESTS=ON", "-DPython3_EXECUTABLE=" + sys.executable]
            if IS_WIN and shutil.which("mingw32-make"): cmd[1:1] = ["-G", "MinGW Makefiles"]
            if subprocess.run(cmd, stdin=subprocess.DEVNULL, **quiet()).returncode != 0: return None
        jobs = str(max(1, (os.cpu_count() or 2) - 1))
        r = subprocess.run(["cmake", "--build", BUILD_DIR, "-j", jobs, "--target", "quark_tests"], stdin=subprocess.DEVNULL, **quiet())
    except OSError as e:   # (no cmake on PATH)
        print("sound editor: WARNING: could not build quark_tests: %s" % e)
        return None
    exe = os.path.join(BUILD_DIR, EXE)
    return exe if r.returncode == 0 and os.path.isfile(exe) else None


def checkout_version():
    try:
        with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f: return f.read().strip()
    except OSError:
        return ""


def base_version(v):   # "1.18.16-test.140+abc" / "1.19.0.testing.5" (testing builds from 1.19.0 on) -> "1.18.16" / "1.19.0"
    return ".".join(v.split("-")[0].split("+")[0].strip().split(".")[:3])


def export_with(tests, export, tmp):
    """Runs one quark_tests' export; returns the version its manifest names, or None when the export failed."""
    shutil.rmtree(export, ignore_errors=True)
    try:
        r = subprocess.run([tests, "exportassets", export], cwd=os.path.dirname(tests), env=dict(os.environ, TMPDIR=tmp),
                           stdin=subprocess.DEVNULL, timeout=600, **quiet())
        with open(os.path.join(export, "manifest.json"), encoding="utf-8") as f: man = json.load(f)
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    return str(man.get("version") or "") if r.returncode == 0 else None


def commit():
    try:
        return subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                              timeout=10).stdout.strip()
    except Exception:
        return "?"


def try_export(tests, export, tmp, want, stale):
    """True when `tests` exported this checkout's VERSION into `export`."""
    got = export_with(tests, export, tmp)
    if got is None:
        print("sound editor: WARNING: the asset export failed with %s; trying the next build" % tests)
    elif want and base_version(got) != want:
        print("sound editor: WARNING: skipping %s: it is version %s, this checkout is %s (rebuild it)" % (tests, got or "?", want))
        stale.append(tests)
    else:
        return True
    return False


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = os.path.abspath(args[0]) if args else os.path.join(QUARK, "sound-editor")
    os.makedirs(out, exist_ok=True)
    build = [sys.executable, os.path.join(HERE, "build.py")]
    want = base_version(checkout_version())
    used, stale = None, []
    with tempfile.TemporaryDirectory() as tmp:
        export = os.path.join(tmp, "export")
        for tests in find_tests():
            if try_export(tests, export, tmp, want, stale): used = tests; break
        if not used and "--no-build" not in sys.argv:
            tests = build_tests()
            if tests and try_export(tests, export, tmp, want, stale): used = tests
        if used:
            r = subprocess.run(build + [export, out], stdin=subprocess.DEVNULL, **quiet())
            if r.returncode != 0: return 1
    rc = 0
    if used:
        how = "page + sounds (export from %s)" % used
    elif os.path.isfile(os.path.join(out, "data", "manifest.js")):
        r = subprocess.run(build + ["-", out, "--page-only"], stdin=subprocess.DEVNULL, **quiet())
        if r.returncode != 0: return 1
        if stale:
            how = "page only (every quark_tests found is an older version than %s; the sounds are the last export's)" % want
            rc = 2
        else:
            how = "page only (no quark_tests found; the sounds are the last export's)"
    else:
        print("sound editor: no quark_tests %s found to export the sounds from, and no earlier install in %s.\n"
              "Building one in build-editors failed or was skipped: build it (tools/build_windows.ps1 -Tests, or\n"
              "cmake --build build --target quark_tests) or set QUARK_TESTS."
              % (want or "", out))
        return 1
    shutil.copyfile(os.path.join(HERE, "README.md"), os.path.join(out, "README.md"))
    with open(os.path.join(out, "INSTALLED.txt"), "w", encoding="utf-8") as f:
        f.write("sound editor from %s at %s: %s\n" % (commit(), time.strftime("%Y-%m-%d %H:%M"), how))
    print("sound editor: %s -> %s" % (how, os.path.join(out, "sound-editor.html")))
    if rc: print("sound editor: WARNING: the sounds were NOT updated; rebuild quark_tests for %s and run this again" % want)
    return rc


if __name__ == "__main__":
    sys.exit(main())
