"""Install or update the sound editor next to the repository checkout, non-interactively (the Quark Project app runs it).

    python tools/sound_editor/install.py [out_dir]          (cwd: any; out_dir default: <checkout>/../sound-editor)

It never prompts and never opens a browser, and it is safe to re-run. The sounds come from the game's asset export, which
needs a built quark_tests (any recent build of this branch). The first one found here wins:
  $QUARK_TESTS, <checkout>/build-win, <checkout>/build, <Quark>/working-repo-sound/build-win, <Quark>/.src/testing/build-win
With one: the export is redone and the page + data rebuilt. Without one: only the page is rebuilt when data/ is already
there (the sounds stay as last exported); with no data at all it exits 1 and says what to build.
"""
import os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
QUARK = os.path.dirname(ROOT)


def find_tests():
    exe = "quark_tests.exe" if os.name == "nt" else "quark_tests"
    cands = [os.environ.get("QUARK_TESTS", "")]
    for d in (os.path.join(ROOT, "build-win"), os.path.join(ROOT, "build"),
              os.path.join(QUARK, "working-repo-sound", "build-win"), os.path.join(QUARK, ".src", "testing", "build-win")):
        cands.append(os.path.join(d, exe))
    return next((c for c in cands if c and os.path.isfile(c)), None)


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
    tests = find_tests()
    if tests:
        with tempfile.TemporaryDirectory() as tmp:
            export = os.path.join(tmp, "export")
            env = dict(os.environ, TMPDIR=tmp)
            r = subprocess.run([tests, "exportassets", export], cwd=os.path.dirname(tests), env=env,
                               stdin=subprocess.DEVNULL, timeout=600)
            if r.returncode != 0 or not os.path.isfile(os.path.join(export, "manifest.json")):
                print("sound editor: the asset export failed (%s)" % tests); return 1
            r = subprocess.run(build + [export, out], stdin=subprocess.DEVNULL)
            if r.returncode != 0: return 1
        how = "page + sounds (export from %s)" % tests
    elif os.path.isfile(os.path.join(out, "data", "manifest.js")):
        r = subprocess.run(build + ["-", out, "--page-only"], stdin=subprocess.DEVNULL)
        if r.returncode != 0: return 1
        how = "page only (no quark_tests found; the sounds are the last export's)"
    else:
        print("sound editor: no quark_tests found to export the sounds from, and no earlier install in %s.\n"
              "Build one (tools/build_windows.ps1 -Tests, or cmake --build build --target quark_tests) or set QUARK_TESTS."
              % out)
        return 1
    shutil.copyfile(os.path.join(HERE, "README.md"), os.path.join(out, "README.md"))
    with open(os.path.join(out, "INSTALLED.txt"), "w", encoding="utf-8") as f:
        f.write("sound editor from %s at %s: %s\n" % (commit(), time.strftime("%Y-%m-%d %H:%M"), how))
    print("sound editor: %s -> %s" % (how, os.path.join(out, "sound-editor.html")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
