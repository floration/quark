#!/usr/bin/env python3
"""Copies the GitHub manager (server, launcher, page files) into its output folder, so it runs from there and not
from a working copy whose branch may change, then starts it there with no window (or, if it is already running,
opens the page). Data already in the folder (github-manager.json, cache\\, history.jsonl, logs\\, mirror.git,
editor\\, server.log) is never touched. Non-interactive and safe to re-run: only files that differ are copied. A running
server is never stopped: when files changed it keeps its old code, and this prints "restart needed".

    python install.py [--dest <folder>] [--no-start]    (default %USERPROFILE%\\Desktop\\Quark\\github-manager)
    python install.py <folder> [--no-start]              (the same; install.cmd passes the folder like this)

--no-start only copies (the Quark Project app does this, then starts and restarts the server itself). No browser
opens when QUARK_SHELL=1. What it did goes to <output folder>\\install.log as well as the console (if there is one).
Exit codes: 0 done, 1 the copy failed, 2 bad arguments.
"""
import argparse
import filecmp
import json
import os
import shutil
import subprocess
import sys
import urllib.request
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP = {"test", "__pycache__", "install.py", "install.cmd", "test_server.py"}
DATA = {"github-manager.json", "cache", "logs", "mirror.git", "history.jsonl", "state-prev.json", "server.log",
        "install.log", "editor"}
WIN = os.name == "nt"
PORT = 8767


def say(out, msg):
    if sys.stdout is not None:
        try:
            print(msg)
        except (OSError, ValueError):
            pass
    try:
        with open(os.path.join(out, "install.log"), "a", encoding="utf-8") as f:
            f.write(msg + "\n")
    except OSError:
        pass


def running(port):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/summary" % port, timeout=2) as r:
            return r.status == 200
    except Exception:
        return False


def windowless_python():
    """pythonw.exe next to this Python on Windows (no console window), else this Python."""
    exe = sys.executable or "python"
    if WIN:
        w = os.path.join(os.path.dirname(exe), "pythonw.exe")
        if os.path.isfile(w):
            return w
    return exe


def start_hidden(out):
    """Starts <out>/server.py detached, with no window, at low priority. -> the Popen object."""
    args = [windowless_python(), "-B", os.path.join(out, "server.py"), "--data", out, "--open"]
    kw = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL,
          "cwd": out, "close_fds": True}
    if WIN:
        # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS (= start /low)
        kw["creationflags"] = 0x00000008 | 0x00000200 | 0x08000000 | 0x00000040
    else:
        kw["start_new_session"] = True
        kw["preexec_fn"] = lambda: os.nice(10)
    return subprocess.Popen(args, **kw)


def _same(a, b):
    try:
        return os.path.isfile(b) and filecmp.cmp(a, b, shallow=False)
    except OSError:
        return False


def _sync_dir(src, dst):
    """Makes dst a copy of src (no __pycache__ / .pyc), touching only what differs. -> files changed."""
    n = 0
    os.makedirs(dst, exist_ok=True)
    want = set()
    for name in os.listdir(src):
        if name == "__pycache__" or name.endswith(".pyc"):
            continue
        want.add(name)
        a, b = os.path.join(src, name), os.path.join(dst, name)
        if os.path.isdir(a):
            if os.path.isfile(b):
                os.remove(b)
            n += _sync_dir(a, b)
        elif not _same(a, b):
            if os.path.isdir(b):
                shutil.rmtree(b)
            shutil.copy2(a, b)
            n += 1
    for name in os.listdir(dst):
        if name not in want:
            p = os.path.join(dst, name)
            shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
            n += 1
    return n


def install(out):
    """Copies the tool's files into out (never the data). -> (files / folders copied, files that changed)."""
    os.makedirs(out, exist_ok=True)
    n = changed = 0
    for name in sorted(os.listdir(HERE)):
        if name in SKIP or name in DATA or name.startswith("."):
            continue
        src, dst = os.path.join(HERE, name), os.path.join(out, name)
        if os.path.isdir(src):
            changed += _sync_dir(src, dst)
        elif not _same(src, dst):
            shutil.copy2(src, dst)
            changed += 1
        n += 1
    return n, changed


def default_dest():
    return os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "github-manager")


def parse_args(argv):
    ap = argparse.ArgumentParser(
        prog="install.py", description="Copy the Quark GitHub manager into its output folder and start it there "
        "(no window). Data in the folder is never touched; a running server is never stopped.")
    ap.add_argument("folder", nargs="?", help="output folder (same as --dest)")
    ap.add_argument("--dest", help="output folder (default %%USERPROFILE%%\\Desktop\\Quark\\github-manager)")
    ap.add_argument("--no-start", action="store_true", help="only copy the files; don't start the server or open "
                    "the page (the Quark Project app starts it itself)")
    a = ap.parse_args(argv)  # unknown arguments: a message and exit code 2
    if a.folder and a.dest and os.path.abspath(a.folder) != os.path.abspath(a.dest):
        ap.error("give the output folder once (a folder argument or --dest)")
    return a


def main(argv):
    a = parse_args(argv)
    out = os.path.abspath(os.path.expanduser(a.dest or a.folder or default_dest()))
    changed = 0
    if os.path.normcase(out) != os.path.normcase(HERE):
        try:
            n, changed = install(out)
        except OSError as e:
            say(out, "could not install into %s: %s" % (out, e))
            return 1
        say(out, "installed %d files / folders into %s (%s)" % (
            n, out, "%d changed" % changed if changed else "already up to date"))
    else:
        say(out, "already installed here: %s" % out)
    port = PORT
    try:
        with open(os.path.join(out, "github-manager.json"), "r", encoding="utf-8") as f:
            port = int(json.load(f).get("port") or PORT)
    except (OSError, ValueError, AttributeError, TypeError):
        pass
    up = running(port)
    if up and changed:
        say(out, "restart needed: the GitHub manager running on port %d still uses its old files" % port)
    if a.no_start:
        if not up:
            say(out, "not started (--no-start); start it with: %s" % os.path.join(out, "github-manager.cmd"))
        return 0
    if up:
        if not changed:
            say(out, "already running on port %d" % port)
        if os.environ.get("QUARK_SHELL") != "1":
            say(out, "opening http://127.0.0.1:%d/" % port)
            webbrowser.open("http://127.0.0.1:%d/" % port)
        return 0
    p = start_hidden(out)
    say(out, "started with no window (pid %d); its log is %s" % (p.pid, os.path.join(out, "server.log")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
