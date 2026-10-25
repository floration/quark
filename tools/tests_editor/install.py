#!/usr/bin/env python3
"""Copies the tests editor into its run folder (default: the folder `tests-editor` beside the repository checkout this
file is in, i.e. Quark\\tests-editor), keeping that folder's tests_editor.json and cache/. Then start it with
tests-editor.cmd there (or `py -3 serve.py`).

    py -3 tools\\tests_editor\\install.py [--target DIR] [--quiet]

Non-interactive and safe to re-run (the Quark app runs it on open with --quiet): tool files are overwritten, a
running serve.py may have its files replaced (the app restarts it). Exit 0 on success; on failure one line on stderr
and exit 1.
"""
import argparse, json, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ["serve.py", "API.md", "README.md", "tests-editor.cmd", "tests_editor.example.json"]
DIRS = ["model", "web"]
REQUIRED = {"serve.py", "model", "tests-editor.cmd"}
CONFIG = "tests_editor.json"


def default_target():
    """The `tests-editor` folder beside the checkout (tools/tests_editor -> <checkout>/../tests-editor)."""
    checkout = os.path.abspath(os.path.join(HERE, "..", ".."))
    return os.path.join(os.path.dirname(checkout), "tests-editor")


def install(target, say):
    missing = [f for f in FILES + DIRS if not os.path.exists(os.path.join(HERE, f))]
    if REQUIRED & set(missing):
        raise RuntimeError("missing in %s: %s" % (HERE, ", ".join(sorted(REQUIRED & set(missing)))))
    os.makedirs(target, exist_ok=True)
    for f in FILES:
        if f in missing: say("  (skipped %s: not in the source folder)" % f); continue
        dst = os.path.join(target, f)
        tmp = dst + ".installing"
        shutil.copy2(os.path.join(HERE, f), tmp)
        os.replace(tmp, dst)
        say("  " + f)
    for d in DIRS:
        if d in missing: say("  (skipped %s/: not in the source folder)" % d); continue
        dst = os.path.join(target, d)
        new = dst + ".installing"
        if os.path.isdir(new): shutil.rmtree(new)
        shutil.copytree(os.path.join(HERE, d), new, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        old = dst + ".old"
        if os.path.isdir(old): shutil.rmtree(old, ignore_errors=True)
        if os.path.isdir(dst): os.replace(dst, old)                     # (no stale files from an older version)
        os.replace(new, dst)
        shutil.rmtree(old, ignore_errors=True)
        say("  %s/" % d)
    cfg = os.path.join(target, CONFIG)
    if os.path.isfile(cfg):
        say("  kept %s" % CONFIG)
    else:
        with open(cfg, "w", encoding="utf-8") as f:
            json.dump({"repo": "../working-repo", "port": 8775, "timingDir": None}, f, indent=2)
            f.write("\n")
        say("  wrote %s (repo: ..\\working-repo)" % CONFIG)
    if os.path.isdir(os.path.join(target, "cache")): say("  kept cache/")
    say("Installed into %s. Start it with tests-editor.cmd there." % target)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", default=None, help="default: %s" % default_target())
    ap.add_argument("--quiet", action="store_true", help="print nothing on success")
    args = ap.parse_args(argv)
    target = os.path.abspath(args.target or default_target())
    say = (lambda s: None) if args.quiet else print
    if os.path.normcase(target) == os.path.normcase(HERE):
        say("target is the source folder; nothing to do"); return 0
    try:
        install(target, say)
    except Exception as e:
        sys.stderr.write("tests-editor install failed: %s: %s\n" % (type(e).__name__, e))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
