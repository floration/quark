#!/usr/bin/env python3
"""Copies the GitHub manager (server, launcher, page files) into its output folder, so it runs from there and not
from a working copy whose branch may change, then starts it there with no window (or, if it is already running,
opens the page). Data already in the folder (github-manager.json, cache\\, history.jsonl, logs\\, mirror.git,
server.log) is never touched. A running server keeps its old code until it is restarted.

    python install.py [output folder] [--no-start]   (default %USERPROFILE%\\Desktop\\Quark\\github-manager)

What it did goes to <output folder>\\install.log as well as the console (if there is one).
"""
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
        "install.log"}
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


def install(out):
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
    return n


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    out = os.path.abspath(args[0] if args else
                          os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "github-manager"))
    if os.path.normcase(out) != os.path.normcase(HERE):
        n = install(out)
        say(out, "installed %d files / folders into %s" % (n, out))
    else:
        say(out, "already installed here: %s" % out)
    if "--no-start" in argv:
        say(out, "start it with: %s" % os.path.join(out, "github-manager.cmd"))
        return 0
    port = PORT
    try:
        with open(os.path.join(out, "github-manager.json"), "r", encoding="utf-8") as f:
            port = int(json.load(f).get("port") or PORT)
    except (OSError, ValueError, AttributeError, TypeError):
        pass
    if running(port):
        say(out, "already running (restart it to use the new files): opening http://127.0.0.1:%d/" % port)
        if os.environ.get("QUARK_SHELL") != "1":
            webbrowser.open("http://127.0.0.1:%d/" % port)
        return 0
    p = start_hidden(out)
    say(out, "started with no window (pid %d); its log is %s" % (p.pid, os.path.join(out, "server.log")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
