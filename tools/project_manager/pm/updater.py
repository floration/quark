"""Automatic installs and updates of the local tools (installs.json; shell/README.md "Automatic installs").

On each app open (server start, and the shell's POST /api/updates/run) and on demand (at most once per MIN_GAP s),
in one background thread:
  1. Quark\\repository\\: `git fetch origin testing`, then `git merge --ff-only origin/testing` when it is on branch
     testing with no tracked changes. Never a reset, never a force, never another folder (working-repo* are q's).
  2. Each installs.json entry: the git object ids of its `sources` at repository\\ HEAD form its stamp. When the stamp
     differs from the one in Quark\\project-manager\\installs-state.json, or its `check` file (relative to Quark\\) is
     missing, its `run` command runs hidden in repository\\ (one entry at a time, output in logs\\install-<id>.log);
     the stamp is stored only when it exits with 0. An entry whose first source is not at HEAD yet (not merged) is
     "waiting" and skipped.
  3. A successful install of an entry with `restartsPort` queues a restart request (GET /api/updates); the shell
     restarts that server and acknowledges it (POST /api/updates/ack?id=).
  4. `"run": "builtin:self-update"` copies this program from repository\\tools\\project_manager into
     Quark\\project-manager\\ (q's tabs.json / config.json / history / state are never touched), runs the new
     server.py --init (tabs.json upgrade), and sets `selfUpdated` when any file changed: the shell then offers a restart.
  5. `"run": "builtin:electron-update"` (no sources, no stamp; at most every ELECTRON_CHECK_GAP): when the latest
     stable Electron release is newer than Quark\\project-manager\\electron\\VERSION, downloads its zip, checks it
     against that release's SHASUMS256.txt and unpacks it into electron.next\\. The running electron.exe cannot be
     replaced, so the shell swaps the folders after it exits (shell/electron-swap.ps1, on close or restart).
"""
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import traceback
import urllib.request
import zipfile

from . import tabs, util

MIN_GAP = 60.0
DEFAULT_TIMEOUT = 600
FETCH_TIMEOUT = 120
TAIL_LINES = 15
RESTART_TTL = 600.0   # an unacknowledged restart request is dropped after 10 minutes
REGISTRY = "installs.json"
STATE_FILE = "installs-state.json"
BRANCH = "testing"
SELF_UPDATE = "builtin:self-update"
ELECTRON_UPDATE = "builtin:electron-update"
# Electron (the app's browser) updates itself: GitHub's "latest" release is the newest stable one (never a beta or
# a draft). Checked at most every 12 h (a pass runs on every app open); the zip is about 110 MB.
ELECTRON_RELEASES = "https://api.github.com/repos/electron/electron/releases/latest"
ELECTRON_CHECK_GAP = 12 * 3600.0
ELECTRON_MAX_ZIP = 400 * 1024 * 1024
ELECTRON_TIMEOUT = 60
ELECTRON_DIR, ELECTRON_NEXT = "electron", "electron.next"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
# what setup.ps1 copies (quark.ico aside); q's files next to them (tabs.json, config.json, history.jsonl, state.json,
# oracle-last.json, installs-state.json, logs\, electron\) are never written here
SELF_FILES = ("server.py", "project-manager.cmd", "project-manager-hidden.vbs", REGISTRY)
SELF_DIRS = ("pm", "web", "shell")
SKIP_DIRS = {"__pycache__", "node_modules", ".git"}
ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")


def _git_env():
    # never a credential prompt or a GUI (the app runs with no console)
    return dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="Never", GIT_ASKPASS="", SSH_ASKPASS="",
                LC_ALL="C", LANG="C")


def git(repo, *args, timeout=30.0):
    """-> (code, stdout, stderr). Hidden on Windows; stdin closed."""
    try:
        p = subprocess.run(["git", "-C", repo] + list(args), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout, creationflags=util.NO_WINDOW, env=_git_env())
    except FileNotFoundError:
        return 127, "", "git not found"
    except subprocess.TimeoutExpired:
        return 124, "", "git %s timed out after %d s" % (args[0], timeout)
    dec = lambda b: b.decode("utf-8", errors="replace")
    return p.returncode, dec(p.stdout), dec(p.stderr)


def _first_line(s):
    for line in (s or "").splitlines():
        if line.strip():
            return line.strip()[:200]
    return ""


# ---- the registry ----------------------------------------------------------------------------------------------

def normalize_entry(raw):
    """One installs.json entry -> a clean dict, or None when it is unusable (no id / run / sources)."""
    if not isinstance(raw, dict):
        return None
    eid = raw.get("id")
    if not isinstance(eid, str) or not ID_RE.match(eid):
        return None
    srcs = raw.get("sources", [])
    if isinstance(srcs, str):
        srcs = [srcs]
    if not isinstance(srcs, list):
        return None
    srcs = [s.replace("\\", "/").strip("/") for s in srcs if isinstance(s, str) and s.strip("/\\ ")]
    run = raw.get("run")
    if not isinstance(run, str) or not run.strip() or (not srcs and run.strip() != ELECTRON_UPDATE):
        return None
    check = raw.get("check") if isinstance(raw.get("check"), str) and raw.get("check").strip() else None
    rp = raw.get("restartsPort")
    ports = []
    for p in (rp if isinstance(rp, list) else [rp]):
        p = util.to_int(p)
        if p and 0 < p < 65536 and p not in ports:
            ports.append(p)
    timeout = util.to_int(raw.get("timeoutSec"))
    return {"id": eid, "name": str(raw.get("name") or eid), "sources": srcs, "run": run.strip(), "check": check,
            "restartsPort": ports, "timeoutSec": timeout if timeout and timeout > 0 else DEFAULT_TIMEOUT,
            "lowPriority": raw.get("lowPriority") is True, "enabled": raw.get("enabled") is not False}


def load_registry(paths):
    """(entries, where from): repository\\tools\\project_manager\\installs.json (just fast-forwarded, so new entries
    arrive with the pull), else the copy next to server.py."""
    for path, where in ((paths.q("repository", "tools", "project_manager", REGISTRY), "repository"),
                        (os.path.join(paths.script_dir, REGISTRY), "installed")):
        try:
            data = util.read_json_file(path)
        except FileNotFoundError:
            continue
        except (OSError, ValueError) as e:
            return [], "%s (%s unreadable: %s)" % (where, REGISTRY, type(e).__name__)
        lst = data.get("entries") if isinstance(data, dict) else data
        out, seen = [], set()
        for raw in lst if isinstance(lst, list) else []:
            e = normalize_entry(raw)
            if e and e["id"] not in seen:
                seen.add(e["id"])
                out.append(e)
        return out, where
    return [], "none"


# ---- commands ---------------------------------------------------------------------------------------------------

_py_missing = None


def python_command():
    """What `py -3` becomes when the py launcher is missing: python.exe next to this interpreter (not pythonw)."""
    exe = sys.executable or "python"
    d, base = os.path.split(exe)
    if base.lower().startswith("pythonw"):
        alt = os.path.join(d, base[:7] + base[8:])  # pythonw.exe -> python.exe
        if os.path.isfile(alt):
            exe = alt
    return '"%s"' % exe if " " in exe else exe


def fix_python(cmd):
    """'py -3 x.py' -> the same with python when the py launcher is missing (and always off Windows)."""
    global _py_missing
    m = re.match(r"^\s*py(?:\.exe)?\s+(?:-3(?:\.\d+)?\s+)?", cmd, re.I)
    if not m:
        return cmd
    if util.IS_WINDOWS:
        if _py_missing is None:
            _py_missing = shutil.which("py") is None
        if not _py_missing:
            return cmd
    return python_command() + " " + cmd[m.end():]


def expand(cmd, paths):
    """%QUARK%, %PM%, %REPO% (case-insensitive); everything else is left for cmd.exe."""
    vals = {"QUARK": paths.quark.rstrip("\\/"), "PM": paths.pm_dir.rstrip("\\/"),
            "REPO": paths.q("repository").rstrip("\\/")}
    return re.sub(r"%(QUARK|PM|REPO)%", lambda m: vals[m.group(1).upper()], cmd, flags=re.I)


def shell_argv(cmd):
    """Windows: cmd.exe /d /s /c "<cmd>" (so .cmd files, quoting and builtins work; hidden by CREATE_NO_WINDOW);
    elsewhere /bin/sh -c."""
    if util.IS_WINDOWS:
        comspec = os.environ.get("COMSPEC") or "cmd.exe"
        return '"%s" /d /s /c "%s"' % (comspec, cmd)
    return ["/bin/sh", "-c", cmd]


def kill_tree(p):
    try:
        if util.IS_WINDOWS:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(p.pid)], stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15, creationflags=util.NO_WINDOW)
        else:
            os.killpg(p.pid, signal.SIGKILL)
    except (OSError, subprocess.SubprocessError):
        pass
    try:
        p.kill()
    except OSError:
        pass


BELOW_NORMAL = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0x4000)


def run_command(cmd, cwd, env, timeout, log_path, low_priority=False):
    """Runs a shell command hidden with stdin closed; its output goes to log_path. -> (exit code, tail lines, note)."""
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "wb") as log:
        log.write(("[%s] run in %s: %s\n" % (util.now_iso(), cwd, cmd)).encode("utf-8"))
        log.flush()
        # (a build at below-normal priority: its children inherit it, so the game and the editors stay smooth)
        flags = util.NO_WINDOW | (BELOW_NORMAL if low_priority and util.IS_WINDOWS else 0)
        kw = dict(cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        if not util.IS_WINDOWS:
            kw["start_new_session"] = True
            if low_priority:
                kw["preexec_fn"] = lambda: os.nice(10)
        note = None
        try:
            p = subprocess.Popen(shell_argv(cmd), **kw)
        except OSError as e:
            log.write(("could not start: %s\n" % e).encode("utf-8"))
            return -1, ["could not start: %s" % e], "could not start"
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            kill_tree(p)
            try:
                p.wait(timeout=15)
            except subprocess.TimeoutExpired:
                pass
            code = -2
            note = "timed out after %d s" % timeout
            log.flush()
            log.write(("\n[%s] %s: stopped\n" % (util.now_iso(), note)).encode("utf-8"))
        log.write(("[%s] exit code %s\n" % (util.now_iso(), code)).encode("utf-8"))
    return code, tail(log_path), note


def tail(path, n=TAIL_LINES):
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            f.seek(max(0, size - 16384))
            text = f.read().decode("utf-8", errors="replace")
    except OSError:
        return []
    lines = [l.rstrip() for l in text.splitlines() if l.strip()]
    return [l[:300] for l in lines[-n:]]


# ---- self-update ------------------------------------------------------------------------------------------------

def _same(a, b):
    try:
        if os.path.getsize(a) != os.path.getsize(b):
            return False
        with open(a, "rb") as fa, open(b, "rb") as fb:
            return hashlib.sha256(fa.read()).digest() == hashlib.sha256(fb.read()).digest()
    except OSError:
        return False


def _copy_file(src, dst):
    """Copy when different (atomically: temp file + replace). True when the file changed."""
    if os.path.isfile(dst) and _same(src, dst):
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".quark-new"
    shutil.copy2(src, tmp)
    os.replace(tmp, dst)
    return True


def _sync_dir(src, dst):
    """Mirror src into dst file by file (never removes and re-creates the folder, so a failure halfway leaves
    every file whole). Files no longer in src go; __pycache__ / node_modules are left alone. -> files changed."""
    changed = 0
    want = set()
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = os.path.relpath(root, src)
        for f in files:
            if f.endswith((".pyc", ".quark-new")):
                continue
            r = os.path.normpath(os.path.join(rel, f))
            want.add(r)
            if _copy_file(os.path.join(root, f), os.path.join(dst, r)):
                changed += 1
    for root, dirs, files in os.walk(dst, topdown=True):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = os.path.relpath(root, dst)
        for f in files:
            r = os.path.normpath(os.path.join(rel, f))
            if r not in want and not f.endswith(".pyc"):
                try:
                    os.remove(os.path.join(root, f))
                    changed += 1
                except OSError:
                    pass
    return changed


def self_update(paths, log_path, run_init=True):
    """Copy repository\\tools\\project_manager -> Quark\\project-manager (code only) and upgrade tabs.json with the new
    code. -> (exit code, tail, files changed)."""
    src = paths.q("repository", "tools", "project_manager")
    dst = paths.pm_dir
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    lines = ["[%s] self-update: %s -> %s" % (util.now_iso(), src, dst)]
    changed = 0
    code = 0
    try:
        if not os.path.isfile(os.path.join(src, "server.py")):
            raise OSError("server.py is not in %s" % src)
        if os.path.realpath(src) == os.path.realpath(dst):
            lines.append("source and target are the same folder: nothing to copy")
        else:
            for f in SELF_FILES:
                s = os.path.join(src, f)
                if os.path.isfile(s):
                    if _copy_file(s, os.path.join(dst, f)):
                        changed += 1
                        lines.append("  " + f)
                else:
                    lines.append("  (not in the source: %s)" % f)
            for d in SELF_DIRS:
                s = os.path.join(src, d)
                if os.path.isdir(s):
                    n = _sync_dir(s, os.path.join(dst, d))
                    changed += n
                    lines.append("  %s\\ (%d files changed)" % (d, n))
                else:
                    lines.append("  (not in the source: %s\\)" % d)
        lines.append("%d files changed" % changed)
        # tabs.json: new default tabs / start commands, with the NEW code (this process still runs the old tabs.py)
        new_server = os.path.join(dst, "server.py")
        if run_init and os.path.isfile(new_server):
            try:
                p = subprocess.run([python_command().strip('"'), new_server, "--quark", paths.quark, "--init"],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   timeout=60, creationflags=util.NO_WINDOW, cwd=dst)
                out = p.stdout.decode("utf-8", errors="replace").strip()
                lines.append("server.py --init: exit %d%s" % (p.returncode, (" " + out[-400:]) if out else ""))
                if p.returncode != 0:
                    raise OSError("server.py --init failed")
            except (OSError, subprocess.SubprocessError) as e:
                lines.append("server.py --init did not run (%s); tabs.upgrade() with the running code" % e)
                up = tabs.upgrade(paths)
                lines.append("tabs.json: %s" % (", ".join(up) if up else "unchanged"))
    except OSError as e:
        code = 1
        lines.append("failed: %s" % e)
    lines.append("[%s] exit code %d" % (util.now_iso(), code))
    try:
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except OSError:
        pass
    return code, lines[-TAIL_LINES:], changed


# ---- Electron ---------------------------------------------------------------------------------------------------

class _Done(Exception):
    """(electron_update: nothing more to do)"""


def electron_platform():
    """(zip platform, program name) of the official Electron build for this PC, or None (macOS: an app bundle)."""
    arm = platform.machine().lower() in ("arm64", "aarch64")
    if util.IS_WINDOWS:
        return ("win32-arm64" if arm else "win32-x64"), "electron.exe"
    if sys.platform.startswith("linux"):
        return ("linux-arm64" if arm else "linux-x64"), "electron"
    return None


def version_tuple(v):
    return tuple(int(x) for x in v.split(".")) if isinstance(v, str) and VERSION_RE.match(v) else None


def installed_electron(folder, exe):
    """The version in <folder>\\VERSION when <folder> holds the program, else None."""
    if not os.path.isfile(os.path.join(folder, exe)):
        return None
    try:
        with open(os.path.join(folder, "VERSION"), encoding="utf-8") as f:
            v = f.readline().strip().lstrip("v")
    except OSError:
        return None
    return v if VERSION_RE.match(v) else None


def _open_url(url, accept=None, timeout=ELECTRON_TIMEOUT):
    h = {"User-Agent": util.USER_AGENT}
    if accept:
        h["Accept"] = accept
    return urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=timeout)


def _download(url, dst, limit):
    """-> sha256 hex of what was written to dst (streamed: never the whole zip in memory)."""
    sha = hashlib.sha256()
    size = 0
    with _open_url(url) as r, open(dst, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            size += len(chunk)
            if size > limit:
                raise OSError("%s is larger than %d MB" % (os.path.basename(dst), limit >> 20))
            sha.update(chunk)
            f.write(chunk)
    return sha.hexdigest()


def electron_update(paths, log_path, releases_url=None, plat=None):
    """Stage the latest stable Electron in Quark\\project-manager\\electron.next\\ when it is newer than electron\\.
    -> (exit code, tail, note, {installed, latest}). Never touches electron\\ (the running app's)."""
    lines = ["[%s] electron update" % util.now_iso()]
    info = {"installed": None, "latest": None}
    note = None
    code = 0
    pm = paths.pm_dir
    dl = os.path.join(pm, "electron.download")
    tmp = os.path.join(pm, ELECTRON_NEXT + ".tmp")
    try:
        plat = plat or electron_platform()
        if not plat:
            note = "no Electron build for this system; left alone"
            raise _Done
        zplat, exe = plat
        have = installed_electron(os.path.join(pm, ELECTRON_DIR), exe)
        info["installed"] = have
        if not have:
            note = "no electron\\ with a VERSION file (setup -NoElectron?): nothing to update"
            raise _Done
        with _open_url(releases_url or ELECTRON_RELEASES, accept="application/vnd.github+json") as r:
            rel = json.loads(r.read(4 << 20).decode("utf-8"))
        ver = str(rel.get("tag_name") or "").lstrip("v")
        if not VERSION_RE.match(ver) or rel.get("prerelease") or rel.get("draft"):
            raise OSError("unexpected latest release %r" % rel.get("tag_name"))
        info["latest"] = ver
        lines.append("installed %s, latest stable %s" % (have, ver))
        staged = installed_electron(os.path.join(pm, ELECTRON_NEXT), exe)
        if version_tuple(ver) <= version_tuple(have):
            note = "Electron %s is the latest" % have
            if staged:
                shutil.rmtree(os.path.join(pm, ELECTRON_NEXT), ignore_errors=True)  # (an older download: not needed)
            raise _Done
        if staged == ver:
            note = "Electron %s is downloaded; it replaces %s when Quark Project next closes or restarts" % (ver, have)
            raise _Done
        name = "electron-v%s-%s.zip" % (ver, zplat)
        assets = {a.get("name"): a for a in rel.get("assets") or [] if isinstance(a, dict)}
        if name not in assets or "SHASUMS256.txt" not in assets:
            raise OSError("Electron %s has no %s or SHASUMS256.txt" % (ver, name))
        if (util.to_int(assets[name].get("size")) or 0) > ELECTRON_MAX_ZIP:
            raise OSError("%s is larger than %d MB" % (name, ELECTRON_MAX_ZIP >> 20))
        shutil.rmtree(dl, ignore_errors=True)
        os.makedirs(dl)
        sums_path = os.path.join(dl, "SHASUMS256.txt")
        _download(assets["SHASUMS256.txt"]["browser_download_url"], sums_path, 1 << 20)
        expected = None
        with open(sums_path, encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.strip().split(None, 1)
                if len(parts) == 2 and parts[1].lstrip("*") == name and re.match(r"^[0-9a-fA-F]{64}$", parts[0]):
                    expected = parts[0].lower()
                    break
        if not expected:
            raise OSError("SHASUMS256.txt of Electron %s does not list %s" % (ver, name))
        lines.append("downloading %s" % name)
        zpath = os.path.join(dl, name)
        actual = _download(assets[name]["browser_download_url"], zpath, ELECTRON_MAX_ZIP)
        if actual != expected:
            raise OSError("%s is corrupt: sha256 %s, expected %s" % (name, actual, expected))
        lines.append("sha256 verified (%s)" % expected)
        shutil.rmtree(tmp, ignore_errors=True)
        with zipfile.ZipFile(zpath) as z:
            z.extractall(tmp)  # (zipfile drops absolute paths and '..' parts)
        if not os.path.isfile(os.path.join(tmp, exe)):
            raise OSError("%s has no %s" % (name, exe))
        with open(os.path.join(tmp, "VERSION"), "w", encoding="ascii") as f:
            f.write(ver + "\n")
        nxt = os.path.join(pm, ELECTRON_NEXT)
        shutil.rmtree(nxt, ignore_errors=True)
        os.replace(tmp, nxt)
        note = "Electron %s is downloaded; it replaces %s when Quark Project next closes or restarts" % (ver, have)
    except _Done:
        pass
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        code = 1
        note = "failed: %s" % e
    finally:
        shutil.rmtree(dl, ignore_errors=True)
        shutil.rmtree(tmp, ignore_errors=True)
    lines.append(note or "")
    lines.append("[%s] exit code %d" % (util.now_iso(), code))
    try:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except OSError:
        pass
    return code, lines[-TAIL_LINES:], note, info


# ---- the updater ------------------------------------------------------------------------------------------------

class Updater:
    def __init__(self, paths, enabled=True, min_gap=MIN_GAP):
        self.paths = paths
        self.enabled = enabled
        self.min_gap = min_gap
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.stop = threading.Event()
        self.running = False
        self.pending = False
        self.last_request = -1e18
        self.last_run_at = None
        self.repo = {"head": None, "behind": None, "ffOk": None, "error": None}
        self.entries = {}
        self.order = []
        self.registry_from = None
        self.self_updated = False
        self.restarts = []
        self._seq = 0
        self.thread = None
        self.saved = self._load_state()

    # -- installs-state.json -------------------------------------------------------------------------------------
    def _state_path(self):
        return self.paths.pm(STATE_FILE)

    def _load_state(self):
        try:
            d = util.read_json_file(self._state_path())
        except (OSError, ValueError):
            return {"entries": {}}
        if not isinstance(d, dict) or not isinstance(d.get("entries"), dict):
            return {"entries": {}}
        return d

    def _save_state(self):
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self._state_path(), self.saved)
        except OSError as e:
            util.log("installs-state.json not saved (%s)" % type(e).__name__)

    # -- API -------------------------------------------------------------------------------------------------------
    def request(self, now=None):
        """Run soon (the page's "Update now", the shell at app open). -> (accepted, seconds until allowed)."""
        now = now or time.time()
        with self.lock:
            if not self.enabled:
                return False, None
            if self.running or self.pending:
                return False, 0.0
            wait = self.last_request + self.min_gap - now
            if wait > 0:
                return False, round(wait, 1)
            self.last_request = now
            self.pending = True
        self.wake.set()
        return True, 0.0

    def ack(self, rid):
        with self.lock:
            n = len(self.restarts)
            self.restarts = [r for r in self.restarts if r["id"] != rid]
            return len(self.restarts) != n

    def view(self, now=None):
        now = now or time.time()
        with self.lock:
            self.restarts = [r for r in self.restarts if now - r["atTs"] < RESTART_TTL]
            entries = []
            for eid in self.order:
                e = dict(self.entries[eid])
                entries.append(e)
            wait = self.last_request + self.min_gap - now
            return {"enabled": self.enabled, "running": self.running or self.pending, "lastRunAt": self.last_run_at,
                    "repo": dict(self.repo), "entries": entries, "selfUpdated": self.self_updated,
                    "registry": self.registry_from,
                    "restart": [{k: v for k, v in r.items() if k != "atTs"} for r in self.restarts],
                    "nextAllowedIn": round(max(0.0, wait), 1) if self.enabled else None}

    # -- one pass --------------------------------------------------------------------------------------------------
    def _set(self, eid, **kw):
        with self.lock:
            if eid in self.entries:
                self.entries[eid].update(kw)

    def update_repo(self):
        """Step 1. -> True when repository\\ is usable for the installs (a git clone)."""
        repo = self.paths.q("repository")
        info = {"head": None, "behind": None, "ffOk": None, "error": None, "branch": None, "fetched": False}
        if not os.path.isdir(os.path.join(repo, ".git")):
            info["error"] = "Quark\\repository\\ is not a git clone" if os.path.isdir(repo) else "Quark\\repository\\ not found"
            with self.lock:
                self.repo = info
            return False
        errors = []
        code, _, err = git(repo, "fetch", "--quiet", "--no-tags", "origin", BRANCH, timeout=FETCH_TIMEOUT)
        if code == 0:
            info["fetched"] = True
        else:
            errors.append("fetch failed: " + (_first_line(err) or "exit %d" % code))
        code, out, _ = git(repo, "rev-parse", "--abbrev-ref", "HEAD")
        branch = out.strip() if code == 0 else None
        info["branch"] = branch
        code, out, _ = git(repo, "rev-list", "--left-right", "--count", "HEAD...origin/" + BRANCH)
        ahead = behind = None
        if code == 0 and len(out.split()) == 2:
            ahead, behind = (int(x) for x in out.split())
        info["behind"], info["ahead"] = behind, ahead
        # (untracked files do not count: builds such as build-model-editor\ live there; a merge that would
        #  overwrite an untracked file refuses by itself)
        code, out, _ = git(repo, "status", "--porcelain", "--untracked-files=no")
        dirty = code != 0 or bool(out.strip())
        info["dirty"] = dirty
        if behind is None:
            info["ffOk"] = False
            errors.append("origin/%s unknown (never fetched?)" % BRANCH)
        elif behind == 0:
            info["ffOk"] = True
        elif branch != BRANCH or dirty:
            info["ffOk"] = False
            if branch != BRANCH:
                errors.append("repository\\ is on %s, not %s: not updated (%d commits behind)"
                              % (branch or "a detached HEAD", BRANCH, behind))
            else:
                errors.append("repository\\ has local changes: not updated (%d commits behind)" % behind)
        else:
            code, _, err = git(repo, "merge", "--ff-only", "--quiet", "origin/" + BRANCH, timeout=120)
            if code == 0:
                info["ffOk"] = True
                info["pulled"] = behind
                info["behind"] = 0
            else:
                info["ffOk"] = False
                errors.append("fast-forward refused: " + (_first_line(err) or "exit %d" % code))
        code, out, _ = git(repo, "rev-parse", "HEAD")
        info["head"] = out.strip() if code == 0 else None
        info["error"] = "; ".join(errors) or None
        with self.lock:
            self.repo = info
        return info["head"] is not None

    def stamp(self, entry):
        """-> (stamp, waiting note). The first source missing at HEAD = not merged yet = waiting."""
        repo = self.paths.q("repository")
        parts = []
        for i, s in enumerate(entry["sources"]):
            code, out, _ = git(repo, "rev-parse", "--verify", "--quiet", "HEAD:" + s)
            if code != 0 or not out.strip():
                if i == 0:
                    return None, "%s is not in repository\\ yet" % s
                parts.append("-")
            else:
                parts.append(out.strip()[:40])
        return "+".join(parts), None

    def _check_path(self, entry):
        c = entry.get("check")
        if not c:
            return None
        return os.path.join(self.paths.quark, *[p for p in re.split(r"[\\/]+", c) if p])

    def _env(self):
        return dict(os.environ, QUARK_SHELL="1", QUARK_DIR=self.paths.quark, PYTHONUNBUFFERED="1",
                    PYTHONIOENCODING="utf-8", GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="Never")

    def install(self, entry, cur):
        eid = entry["id"]
        log_path = self.paths.pm("logs", "install-%s.log" % eid)
        self._set(eid, state="updating", startedAt=util.now_iso())
        started = time.time()
        changed = None
        note = None
        if entry["run"] == SELF_UPDATE:
            code, lines, changed = self_update(self.paths, log_path)
        elif entry["run"].startswith("builtin:"):
            code, lines = -1, ["unknown builtin %s" % entry["run"]]
        else:
            cmd = fix_python(expand(entry["run"], self.paths))
            try:
                code, lines, note = run_command(cmd, self.paths.q("repository"), self._env(), entry["timeoutSec"], log_path,
                                               entry.get("lowPriority"))
            except OSError as e:
                code, lines, note = -1, ["log not writable: %s" % e], "log not writable"
        now_iso = util.now_iso()
        rec = self.saved["entries"].setdefault(eid, {})
        rec.update(lastRunAt=now_iso, exitCode=code, durationSec=round(time.time() - started, 1))
        if code == 0:
            rec["stamp"] = cur
            rec.pop("failedStamp", None)
        else:
            rec["failedStamp"] = cur
        self._save_state()
        ok = code == 0
        self._set(eid, state="ok" if ok else "failed", lastRunAt=now_iso, exitCode=code, logTail=lines,
                  installedStamp=rec.get("stamp"), note=note if not ok else None, log=log_path)
        if ok and entry["run"] == SELF_UPDATE and changed:
            with self.lock:
                self.self_updated = True
        if ok and entry.get("restartsPort"):
            folder = None
            if entry.get("check"):
                folder = re.split(r"[\\/]+", entry["check"].strip("\\/"))[0] or None
            with self.lock:
                for port in entry["restartsPort"]:
                    self._seq += 1
                    self.restarts = [r for r in self.restarts if r["port"] != port]
                    self.restarts.append({"id": "%s-%d-%d" % (eid, int(time.time()), self._seq), "entry": eid,
                                          "name": entry["name"], "port": port, "folder": folder,
                                          "at": now_iso, "atTs": time.time()})
        return ok

    def electron(self, entry, now=None):
        """builtin:electron-update: at most once per ELECTRON_CHECK_GAP (a failed check is tried again next pass).
        The versions go in installedVersion / latestVersion (the page shortens stamps to 7 characters)."""
        eid = entry["id"]
        now = now or time.time()
        rec = self.saved["entries"].setdefault(eid, {})
        if now - (rec.get("checkedTs") or 0) < ELECTRON_CHECK_GAP:
            self._set(eid, state="ok", note=rec.get("note"), installedVersion=rec.get("installed"),
                      latestVersion=rec.get("latest"))
            return True
        log_path = self.paths.pm("logs", "install-%s.log" % eid)
        self._set(eid, state="updating", startedAt=util.now_iso())
        started = time.time()
        code, lines, note, info = electron_update(self.paths, log_path)
        now_iso = util.now_iso()
        rec.update(lastRunAt=now_iso, exitCode=code, durationSec=round(time.time() - started, 1), note=note,
                   installed=info["installed"], latest=info["latest"])
        if code == 0:
            rec["checkedTs"] = now
        self._save_state()
        self._set(eid, state="ok" if code == 0 else "failed", lastRunAt=now_iso, exitCode=code, logTail=lines, note=note,
                  installedVersion=info["installed"], latestVersion=info["latest"], log=log_path)
        return code == 0

    def run_once(self):
        with self.lock:
            self.running = True
            self.pending = False
        try:
            self._run()
        except Exception:  # (never let the thread die)
            util.log("updater: %s" % traceback.format_exc(limit=4))
            with self.lock:
                self.repo["error"] = (self.repo.get("error") or "") + " (internal error)"
        finally:
            with self.lock:
                self.running = False
                self.last_run_at = util.now_iso()

    def _run(self):
        usable = self.update_repo()
        entries, where = load_registry(self.paths)
        saved = self.saved["entries"]
        with self.lock:
            self.registry_from = where
            self.order = [e["id"] for e in entries]
            prev = self.entries  # (a known state stays shown while this pass checks it again)
            self.entries = {e["id"]: {
                "id": e["id"], "name": e["name"],
                "state": "disabled" if not e["enabled"] else
                         ((prev.get(e["id"]) or {}).get("state") if (prev.get(e["id"]) or {}).get("state") in ("ok", "failed", "waiting") else "pending"),
                "installedStamp": (saved.get(e["id"]) or {}).get("stamp"), "currentStamp": None,
                "lastRunAt": (saved.get(e["id"]) or {}).get("lastRunAt"),
                "exitCode": (saved.get(e["id"]) or {}).get("exitCode"),
                "logTail": tail(self.paths.pm("logs", "install-%s.log" % e["id"])),
                "restartsPort": e["restartsPort"], "check": e["check"], "note": None} for e in entries}
        for e in entries:
            if self.stop.is_set():
                return
            if not e["enabled"]:
                continue
            if e["run"] == ELECTRON_UPDATE:
                self.electron(e)  # (needs no repository)
                continue
            if not usable:
                self._set(e["id"], state="pending", note=self.repo.get("error"))
                continue
            cur, waiting = self.stamp(e)
            if cur is None:
                self._set(e["id"], state="waiting", note=waiting, currentStamp=None)
                continue
            self._set(e["id"], currentStamp=cur)
            rec = saved.get(e["id"]) or {}
            check = self._check_path(e)
            missing = bool(check) and not os.path.exists(check)
            if rec.get("stamp") == cur and not missing:
                self._set(e["id"], state="ok")
                continue
            # (a version that failed before is retried on each pass: app open or "Update now", never in a loop)
            self._set(e["id"], note="check file missing" if missing else None)
            self.install(e, cur)

    # -- thread ------------------------------------------------------------------------------------------------------
    def loop(self):
        while not self.stop.is_set():
            self.wake.wait()
            self.wake.clear()
            if self.stop.is_set():
                break
            self.run_once()

    def start(self, run_now=True):
        if not self.enabled:
            return
        if run_now:
            self.request()
        self.thread = threading.Thread(target=self.loop, name="pm-updater", daemon=True)
        self.thread.start()

    def shutdown(self):
        self.stop.set()
        self.wake.set()
