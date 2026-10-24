#!/usr/bin/env python3
"""Copies the GitHub manager (server, launcher, page files) into its output folder, so it runs from there and not
from a working copy whose branch may change. Data already in the folder (github-manager.json, cache\\, history.jsonl,
logs\\, mirror.git) is never touched.

    python install.py [output folder]      (default %USERPROFILE%\\Desktop\\Quark\\github-manager)
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP = {"test", "__pycache__", "install.py", "install.cmd", "test_server.py"}
DATA = {"github-manager.json", "cache", "logs", "mirror.git", "history.jsonl", "state-prev.json"}


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else
                          os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "github-manager"))
    if os.path.normcase(out) == os.path.normcase(HERE):
        print("already installed here: %s" % out)
        return 0
    os.makedirs(out, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(HERE)):
        if name in SKIP or name in DATA or name.startswith("."):
            continue
        src, dst = os.path.join(HERE, name), os.path.join(out, name)
        if os.path.isdir(src):
            if os.path.isdir(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        else:
            shutil.copy2(src, dst)
        n += 1
    print("installed %d files / folders into %s" % (n, out))
    print("start it with: %s" % os.path.join(out, "github-manager.cmd"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
