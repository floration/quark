#!/usr/bin/env python3
"""Quark Project is now Quarkology (2026-10-27): this stands in for the old Supervisor Board server.

This folder (tools/project_manager/) is only the hand-over for installs made before the rename, and goes one release
later (tools/quarkology/SPEC.md section 4, "Moving from Quark Project"). The old app reads its installs.json and runs
it: first tools/quarkology/setup.ps1 (installs Quarkology into Quark\\quarkology\\ and copies q's files and Electron
there), then its own self-update, which copies this server.py, the two launch scripts and shell\\ into
Quark\\project-manager\\. The old app then restarts into shell\\main.js, which starts Quarkology.

    server.py --init     nothing to do (the old self-update runs it): exit 0
    server.py [args]     runs Quark\\quarkology\\server.py with the same arguments (something that still starts the
                         old server gets the new one)
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main(argv):
    if "--init" in argv:
        return 0
    quark = os.path.dirname(HERE)
    for i, a in enumerate(argv):
        if a == "--quark" and i + 1 < len(argv):
            quark = argv[i + 1]
        elif a.startswith("--quark="):
            quark = a[len("--quark="):]
    new = os.path.join(quark, "quarkology", "server.py")
    if not os.path.isfile(new):
        print("Quarkology is not installed in %s (run repository\\tools\\quarkology\\setup.cmd)" % os.path.dirname(new))
        return 2
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    return subprocess.call([sys.executable, new] + argv, cwd=os.path.dirname(new), creationflags=flags)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
