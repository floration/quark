#!/usr/bin/env python3
"""Copies the Claude Documents manager into its run folder (default %USERPROFILE%\\Desktop\\Quark\\docs-manager),
keeping that folder's mirror/ and cache/. Then double-click docs-manager.cmd there.

    python3 install.py [--target DIR]
"""
import argparse, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ["serve.py", "docsparse.py", "ghlinks.py", "threads.json", "index.html", "manager.css", "quark-tools.css",
         "docs-manager.cmd"]
DIRS = ["js"]
REQUIRED = {"serve.py", "docsparse.py", "ghlinks.py", "threads.json", "index.html", "docs-manager.cmd"}


def install(target):
    os.makedirs(target, exist_ok=True)
    missing = [f for f in FILES + DIRS if not os.path.exists(os.path.join(HERE, f))]
    if REQUIRED & set(missing):
        print("missing in %s: %s" % (HERE, ", ".join(sorted(REQUIRED & set(missing)))))
        return 1
    for f in FILES:
        if f in missing: print("  (skipped %s: not in the source folder)" % f); continue
        shutil.copy2(os.path.join(HERE, f), os.path.join(target, f))
        print("  " + f)
    for d in DIRS:
        if d in missing: print("  (skipped %s/: not in the source folder)" % d); continue
        dst = os.path.join(target, d)
        if os.path.isdir(dst): shutil.rmtree(dst)                          # (no stale page scripts)
        shutil.copytree(os.path.join(HERE, d), dst, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        print("  %s/" % d)
    for d in ("mirror", "cache"):
        if os.path.isdir(os.path.join(target, d)): print("  kept %s/" % d)
    print("Installed into %s. Start it with docs-manager.cmd there." % target)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", default=os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "docs-manager"))
    args = ap.parse_args(argv)
    if os.path.abspath(args.target) == HERE:
        print("target is the source folder; nothing to do"); return 0
    return install(os.path.abspath(args.target))


if __name__ == "__main__":
    sys.exit(main())
