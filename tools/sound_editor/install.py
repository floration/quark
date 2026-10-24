"""Install or update the sound editor next to the repository checkout, non-interactively (the Quark Project app runs it).

    python tools/sound_editor/install.py [out_dir]          (cwd: any; out_dir default: <checkout>/../sound-editor)

It never prompts and never opens a browser, and it is safe to re-run. The sounds come from the game's asset export, which
needs a built quark_tests of this checkout's VERSION. They are tried in this order, and the first whose export names the
same VERSION wins (an older build would export old sounds and voice lines, so it is skipped with a warning):
  $QUARK_TESTS, <checkout>/build-win, <checkout>/build, <Quark>/working-repo-sound/build-win, <Quark>/.src/testing/build-win
With one: the export is redone and the page + data rebuilt. Without one: only the page is rebuilt when data/ is already
there (the sounds stay as last exported) and it exits 2 when only stale builds were found; with no data at all it exits 1
and says what to build.
"""
import json, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
QUARK = os.path.dirname(ROOT)


def find_tests():
    exe = "quark_tests.exe" if os.name == "nt" else "quark_tests"
    cands = [os.environ.get("QUARK_TESTS", "")]
    for d in (os.path.join(ROOT, "build-win"), os.path.join(ROOT, "build"),
              os.path.join(QUARK, "working-repo-sound", "build-win"), os.path.join(QUARK, ".src", "testing", "build-win")):
        cands.append(os.path.join(d, exe))
    seen = set()
    return [c for c in cands if c and os.path.isfile(c) and not (os.path.realpath(c) in seen or seen.add(os.path.realpath(c)))]


def checkout_version():
    try:
        with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f: return f.read().strip()
    except OSError:
        return ""


def base_version(v):   # "1.18.16-test.140+abc" -> "1.18.16"
    return v.split("-")[0].split("+")[0].strip()


def export_with(tests, export, tmp):
    """Runs one quark_tests' export; returns the version its manifest names, or None when the export failed."""
    shutil.rmtree(export, ignore_errors=True)
    try:
        r = subprocess.run([tests, "exportassets", export], cwd=os.path.dirname(tests), env=dict(os.environ, TMPDIR=tmp),
                           stdin=subprocess.DEVNULL, timeout=600)
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


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(QUARK, "sound-editor")
    os.makedirs(out, exist_ok=True)
    build = [sys.executable, os.path.join(HERE, "build.py")]
    want = base_version(checkout_version())
    used, stale = None, []
    with tempfile.TemporaryDirectory() as tmp:
        export = os.path.join(tmp, "export")
        for tests in find_tests():
            got = export_with(tests, export, tmp)
            if got is None:
                print("sound editor: WARNING: the asset export failed with %s; trying the next build" % tests)
            elif want and base_version(got) != want:
                print("sound editor: WARNING: skipping %s: it is version %s, this checkout is %s (rebuild it)"
                      % (tests, got or "?", want))
                stale.append(tests)
            else:
                used = tests; break
        if used:
            r = subprocess.run(build + [export, out], stdin=subprocess.DEVNULL)
            if r.returncode != 0: return 1
    rc = 0
    if used:
        how = "page + sounds (export from %s)" % used
    elif os.path.isfile(os.path.join(out, "data", "manifest.js")):
        r = subprocess.run(build + ["-", out, "--page-only"], stdin=subprocess.DEVNULL)
        if r.returncode != 0: return 1
        if stale:
            how = "page only (every quark_tests found is an older version than %s; the sounds are the last export's)" % want
            rc = 2
        else:
            how = "page only (no quark_tests found; the sounds are the last export's)"
    else:
        print("sound editor: no quark_tests %s found to export the sounds from, and no earlier install in %s.\n"
              "Build one (tools/build_windows.ps1 -Tests, or cmake --build build --target quark_tests) or set QUARK_TESTS."
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
