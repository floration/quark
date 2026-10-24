"""The Play tab (http://127.0.0.1:8768/play): start a game build, and when it closes ask q for a play note.

Builds: the local game folders (Quark\\production, testing, working, testing-f3; BUILD.txt parsed by builds.py) and
older GitHub releases installed into Quark\\versions\\<tag>\\ (the release's quark-windows.zip, checked against its
SHA256SUMS, BUILD.txt written like tools/fetch_release.ps1). The release list comes from the public GitHub API
without signing in (60 requests an hour), cached 10 min with an ETag (a 304 does not count against the limit) and
refreshed in the background, so a page request never waits on GitHub.

Launching: the game's exe (quark.exe; it is a GUI-subsystem program, CMakeLists `add_executable(quark WIN32 ...)`)
starts with its folder as working folder, normal priority, never elevated, stdout/stderr into
Quark\\project-manager\\logs\\play-<session>.log. On Windows it is started through a short-lived hidden python
process (HANDOFF below) that starts the game and exits at once, so the game's parent is gone: the shell's
`taskkill /T` of the Supervisor Board server (window closed, app updated) never reaches the game, and the game is
started with CREATE_BREAKAWAY_FROM_JOB (when the job allows it) and CREATE_NO_WINDOW (no console; its own window
shows). This process then opens the game by pid (OpenProcess) and waits for it in a thread.

Sessions (Quark\\project-manager\\play-sessions.json): {id, build, label, version, commit, folder, exe, args,
startedAt, endedAt, exitCode, durationSec, state: running | needsNote | noted | skipped, pid, pidCreated}. When the
game exits the session becomes needsNote (the page opens the note form; the shell brings the window up). When this
server restarts while a game runs, the session is picked up again by pid: on Windows only when the process at that
pid was created when the game was (GetProcessTimes, within 2 s; a reused pid is not the game), elsewhere when it is
alive and its command line names the exe. A game that ended while the server was down still gets a note
(`endApprox`: the end time is when the server noticed).

Notes: each saved note is appended to Quark\\project-manager\\play-notes.jsonl (the local copy, kept forever) and
written as one entry at the top of `## Notes` in Quark\\Claude Documents\\12-play-notes.md (the Claude Manager's
entry shape: `- **Play note: <build> <version>** ... _YYYY-MM-DD HH:MM CT, q, open_`), then committed (that file
only) and pushed the way tools/docs_manager/docsedit.py does (fetch + rebase --autostash first; git never prompts,
no console window). Offline, it stays committed on this PC and goes up on the next save or the retry every 5 min.
A note written once is never written again, even when an agent moves or removes it.
Python 3 standard library only.
"""
import datetime
import hashlib
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

from . import builds, util

RELEASES_URL = "https://api.github.com/repos/floration/quark/releases?per_page=100"
RELEASES_TTL = 600            # s: the list is fetched again after 10 min (when the page asks)
RELEASES_MIN_FORCE = 30       # s: a "Refresh" press at most this often
ZIP_NAME = "quark-windows.zip"
SUMS_NAME = "SHA256SUMS"
MAX_ZIP = 1 << 30             # bytes: a bigger download is refused
MAX_UNZIPPED = 4 << 30
LOCAL_FOLDERS = builds.FOLDERS  # production, testing, working, testing-f3
VERSIONS = "versions"
SESSIONS_FILE = "play-sessions.json"
NOTES_FILE = "play-notes.jsonl"
SYNC_FILE = "play-notes-sync.json"
DOCS_FOLDER = "Claude Documents"
DOCS_FILE = "12-play-notes.md"
DOCS_SECTION = "Notes"
DOCS_HEADER = """# Play notes from q

What q did, noticed and wants changed in a game session. The Supervisor Board's Play tab writes one entry when a game
it started closes and q writes the note, newest first (build, version, how long q played, the launch options).
Agents: read this with 01-08; act on the `open` notes that touch your work, set `in-progress` while you do and
`done` (or `told`, passed on to the thread it is for) when handled. Entry shape and rules: README.md.

## Notes
"""
KEEP_SESSIONS = 200
KEEP_LOGS = 20
MAX_FIELD = 4000
MAX_ARGS = 400
RETRY_EVERY = 300             # s: a note not yet on GitHub is tried again this often
GIT_NAME, GIT_EMAIL = "q (Supervisor Board)", "supervisor-board@localhost"
VIA = " (q, via Supervisor Board)"   # (ends every commit message here: how a play-notes commit of ours is told apart)
TAG_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$")
SESSION_RE = re.compile(r"^p\d{14}-[0-9a-f]{4}$")
NOT_GAME = ("quark_launcher", "quark_server", "quark_tests", "quark_sign", "quark_maptool", "fuzz_")
FIELDS = (("did", "Did"), ("noticed", "Noticed"), ("change", "Wants changed"), ("other", "Also"))

# Windows process creation flags
CREATE_NEW_PROCESS_GROUP = 0x00000200
NORMAL_PRIORITY_CLASS = 0x00000020
CREATE_NO_WINDOW = 0x08000000
CREATE_BREAKAWAY_FROM_JOB = 0x01000000
GAME_FLAGS = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS

# Run hidden as `python -I -S -c HANDOFF <json>`: starts the game and prints its pid, then exits, so the game has no
# living parent (taskkill /T of this server never reaches it). Leaves a job that allows it (Electron's libuv job does).
HANDOFF = r"""
import json, os, subprocess, sys
a = json.loads(sys.argv[1])
out = open(a["log"], "ab") if a.get("log") else subprocess.DEVNULL
def go(flags):
    return subprocess.Popen(a["argv"], cwd=a["cwd"], stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                            creationflags=flags, close_fds=True)
try:
    p = go(a["flags"] | 0x01000000)
except OSError:
    p = go(a["flags"])
os.write(1, str(p.pid).encode())
"""


class PlayError(Exception):
    """A request that could not be done: HTTP code and a sentence for q."""

    def __init__(self, code, message, reason=None):
        super().__init__(message)
        self.code, self.message, self.reason = code, message, reason


# ---- time (Dallas) ---------------------------------------------------------------------------------------------

def _central_offset(utc):
    """US Central offset without tzdata (zoneinfo needs the tzdata package on Windows): CDT from the 2nd Sunday of
    March 2:00 CST to the 1st Sunday of November 2:00 CDT (same rule as tools/docs_manager/docsedit.py)."""
    y = utc.year
    mar = datetime.datetime(y, 3, 8) + datetime.timedelta(days=(6 - datetime.datetime(y, 3, 8).weekday()) % 7)
    nov = datetime.datetime(y, 11, 1) + datetime.timedelta(days=(6 - datetime.datetime(y, 11, 1).weekday()) % 7)
    start = mar.replace(hour=8, tzinfo=datetime.timezone.utc)
    end = nov.replace(hour=7, tzinfo=datetime.timezone.utc)
    return datetime.timedelta(hours=-5 if start <= utc < end else -6)


def dallas(ts):
    utc = datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)
    try:
        from zoneinfo import ZoneInfo
        return utc.astimezone(ZoneInfo("America/Chicago"))
    except Exception:
        return utc.astimezone(datetime.timezone(_central_offset(utc)))


def stamp(ts):
    """'2026-10-24 14:05 CT' (the trailer time the Claude Manager reads, as in 09-answers-from-q.md)."""
    return dallas(ts).strftime("%Y-%m-%d %H:%M") + " CT"


def fmt_duration(sec):
    if sec is None:
        return "unknown time"
    sec = max(0, int(round(sec)))
    if sec < 60:
        return "%d s" % sec
    m = sec // 60
    return "%d min" % m if m < 60 else "%d h %d min" % (m // 60, m % 60)


# ---- the game exe, launch options --------------------------------------------------------------------------------

def find_exe(folder):
    """quark.exe at the folder's top, else the first quark*.exe that is not the launcher / server / tests, else (off
    Windows) an executable `quark`. None when there is none."""
    try:
        names = sorted(os.listdir(folder))
    except OSError:
        return None
    files = [n for n in names if os.path.isfile(os.path.join(folder, n))]
    for n in files:
        if n.lower() == "quark.exe":
            return os.path.join(folder, n)
    for n in files:
        low = n.lower()
        if re.match(r"^quark.*\.exe$", low) and not low.startswith(NOT_GAME):
            return os.path.join(folder, n)
    if not util.IS_WINDOWS and "quark" in files and os.access(os.path.join(folder, "quark"), os.X_OK):
        return os.path.join(folder, "quark")
    return None


def parse_args(text):
    """The launch options field ('--host ffa --map bastion') -> argv list. Quotes group words; Windows paths keep
    their backslashes. Raises PlayError for a too long or broken line."""
    text = (text or "").strip()
    if not text:
        return []
    if len(text) > MAX_ARGS or re.search(r"[\x00-\x1f]", text):
        raise PlayError(400, "The launch options are too long or hold a line break.")
    try:
        if util.IS_WINDOWS:
            parts = shlex.split(text, posix=False)
            return [p[1:-1] if len(p) >= 2 and p[0] == p[-1] == '"' else p for p in parts]
        return shlex.split(text)
    except ValueError:
        raise PlayError(400, "The launch options have an unclosed quote.")


# ---- processes ---------------------------------------------------------------------------------------------------

class _Win:
    """kernel32 through ctypes (Windows only): open a process by pid, its creation time, wait for it, exit code."""
    _k = None

    @classmethod
    def k32(cls):
        if cls._k is None:
            import ctypes
            from ctypes import wintypes
            k = ctypes.WinDLL("kernel32", use_last_error=True)
            k.OpenProcess.restype = wintypes.HANDLE
            k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            k.WaitForSingleObject.restype = wintypes.DWORD
            k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            k.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
            k.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(ctypes.c_ulonglong)] * 4
            k.CloseHandle.argtypes = [wintypes.HANDLE]
            cls._k = k
        return cls._k

    @classmethod
    def open(cls, pid):
        # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION: enough to wait and read the exit code, nothing more
        h = cls.k32().OpenProcess(0x00100000 | 0x1000, False, int(pid))
        return h or None

    @classmethod
    def created(cls, h):
        import ctypes
        c, e, k, u = (ctypes.c_ulonglong() for _ in range(4))
        if not cls.k32().GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u)):
            return None
        return (c.value - 116444736000000000) / 1e7  # FILETIME (100 ns since 1601) -> unix time

    @classmethod
    def wait(cls, h, ms):
        return cls.k32().WaitForSingleObject(h, ms) == 0  # WAIT_OBJECT_0

    @classmethod
    def exit_code(cls, h):
        import ctypes
        from ctypes import wintypes
        code = wintypes.DWORD()
        return code.value if cls.k32().GetExitCodeProcess(h, ctypes.byref(code)) else None

    @classmethod
    def close(cls, h):
        cls.k32().CloseHandle(h)


class Proc:
    """A running game: pid, creation time, and wait(stop) -> (ended, exit code or None). One of: a child Popen
    (off Windows), a Windows process handle, or a pid that is only polled (picked up after a server restart)."""

    def __init__(self, pid, popen=None, handle=None, created=None, exe=None):
        self.pid, self.popen, self.handle, self.created, self.exe = pid, popen, handle, created, exe

    def wait(self, stop):
        while not stop.is_set():
            if self.popen is not None:
                try:
                    return True, self.popen.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    continue
            if self.handle is not None:
                if _Win.wait(self.handle, 1000):
                    code = _Win.exit_code(self.handle)
                    _Win.close(self.handle)
                    self.handle = None
                    return True, code
                continue
            if util.IS_WINDOWS or not pid_alive(self.pid, self.exe):
                return True, None  # (Windows without a handle: it was gone before it could be opened)
            stop.wait(2.0)
        return False, None


def pid_alive(pid, exe=None):
    """Off Windows: the pid exists, is not a zombie, and (with /proc) its command line names the exe. (Never on
    Windows: there os.kill(pid, 0) sends CTRL_C_EVENT.)"""
    if util.IS_WINDOWS:
        return False
    try:
        os.kill(int(pid), 0)
    except (OSError, ValueError, OverflowError):
        return False
    try:
        with open("/proc/%d/stat" % int(pid), "rb") as f:
            if f.read().rsplit(b")", 1)[-1].split()[0] == b"Z":
                return False
        if exe:
            with open("/proc/%d/cmdline" % int(pid), "rb") as f:
                return os.path.basename(exe).encode() in f.read()
    except (OSError, IndexError):
        pass
    return True


def _python_for_handoff():
    exe = sys.executable or "python"
    d, b = os.path.split(exe)
    if b.lower() == "pythonw.exe" and os.path.isfile(os.path.join(d, "python.exe")):
        return os.path.join(d, "python.exe")  # (pythonw has no stdout to hand the pid back on)
    return exe


def spawn(argv, cwd, log_path):
    """Start the game detached from this server -> Proc. Raises PlayError when it cannot start."""
    out = None
    try:
        if not util.IS_WINDOWS:
            out = open(log_path, "ab") if log_path else subprocess.DEVNULL
            try:
                p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                     start_new_session=True, close_fds=True)
            except OSError as e:
                raise PlayError(500, "The game did not start: %s" % (e.strerror or e))
            return Proc(p.pid, popen=p, created=time.time(), exe=argv[0])
        payload = json.dumps({"argv": argv, "cwd": cwd, "log": log_path, "flags": GAME_FLAGS})
        try:
            r = subprocess.run([_python_for_handoff(), "-I", "-S", "-c", HANDOFF, payload], cwd=cwd,
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
                               creationflags=CREATE_NO_WINDOW)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise PlayError(500, "The game did not start (the hidden starter failed: %s)." % type(e).__name__)
        pid = util.to_int(r.stdout.decode("ascii", "replace").strip())
        if r.returncode != 0 or not pid:
            err = [l for l in r.stderr.decode("utf-8", "replace").splitlines() if l.strip()]
            raise PlayError(500, "The game did not start: %s" % (err[-1] if err else "exit code %d" % r.returncode))
        h = _Win.open(pid)
        if not h:
            return Proc(pid, created=time.time(), exe=argv[0])  # (already gone: Proc.wait reports it ended)
        return Proc(pid, handle=h, created=_Win.created(h), exe=argv[0])
    finally:
        if out not in (None, subprocess.DEVNULL):
            out.close()  # (the game has its own copy of the handle)


def adopt(session):
    """The game of a session still marked running (this server restarted) -> Proc, or None when it is gone."""
    pid = util.to_int(session.get("pid"))
    if not pid:
        return None
    if util.IS_WINDOWS:
        try:
            h = _Win.open(pid)
        except OSError:
            return None
        if not h:
            return None
        created, want = _Win.created(h), util.to_float(session.get("pidCreated"))
        if created is None or want is None or abs(created - want) > 2.0 or _Win.wait(h, 0):
            _Win.close(h)  # (another process got that pid, or it has just ended)
            return None
        return Proc(pid, handle=h, created=created, exe=session.get("exe"))
    return Proc(pid, exe=session.get("exe")) if pid_alive(pid, session.get("exe")) else None


# ---- releases ----------------------------------------------------------------------------------------------------

def _url_ok(u):
    """https anywhere; plain http only on this PC (the tests' fake GitHub)."""
    if not isinstance(u, str):
        return False
    p = urllib.parse.urlsplit(u)
    return p.scheme == "https" or (p.scheme == "http" and (p.hostname or "") in ("127.0.0.1", "localhost"))


def parse_releases(data):
    """GitHub's /releases JSON -> [{tag, name, channel, prerelease, publishedAt, version, url, zip, sums}], newest
    first. Drafts and odd tags are left out; a release without quark-windows.zip has zip None (shown greyed)."""
    out = []
    for r in data if isinstance(data, list) else []:
        if not isinstance(r, dict) or r.get("draft"):
            continue
        tag = r.get("tag_name")
        if not isinstance(tag, str) or not TAG_RE.match(tag):
            continue
        assets = {a.get("name"): a for a in r.get("assets") or [] if isinstance(a, dict)}
        z, s = assets.get(ZIP_NAME), assets.get(SUMS_NAME)
        pre = r.get("prerelease") is True
        channel = "production" if (tag.startswith("v") and not pre) else "testing" if tag.startswith("test-") else "other"
        zurl = z.get("browser_download_url") if z else None
        surl = s.get("browser_download_url") if s else None
        out.append({"tag": tag, "name": str(r.get("name") or tag)[:200], "channel": channel, "prerelease": pre,
                    "publishedAt": r.get("published_at") or r.get("created_at"),
                    "version": util.parse_version(tag), "url": r.get("html_url") if _url_ok(r.get("html_url")) else None,
                    "zip": {"url": zurl, "size": util.to_int(z.get("size"), 0)} if z and _url_ok(zurl) else None,
                    "sums": surl if _url_ok(surl) else None})
    out.sort(key=lambda x: (util.parse_time(x["publishedAt"]) or 0, util.version_key(x["tag"])), reverse=True)
    return out


def parse_sums(text, name=ZIP_NAME):
    """SHA256SUMS ('<hex>  <file>' or '<hex> *<file>') -> the hex for `name`, or None."""
    for line in (text or "").splitlines():
        m = re.match(r"^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$", line)
        if m and m.group(2).split("/")[-1] == name:
            return m.group(1).lower()
    return None


def _opener(url):
    host = urllib.parse.urlsplit(url).hostname or ""
    return util._LOCAL_OPENER if host in ("127.0.0.1", "localhost") else util._DEFAULT_OPENER


def extract_zip(zip_path, dest):
    """Unzip into dest (made here), dropping one top folder that holds everything (quark-windows/). Refuses names
    that leave dest and more than MAX_UNZIPPED bytes."""
    with zipfile.ZipFile(zip_path) as zf:
        infos = zf.infolist()
        names = [i.filename.replace("\\", "/") for i in infos]
        tops = {n.split("/", 1)[0] for n in names if n.strip("/")}
        strip = ""
        if len(tops) == 1 and all("/" in n.strip("/") or n.endswith("/") for n in names if n.strip("/")):
            strip = tops.pop() + "/"
        if sum(i.file_size for i in infos) > MAX_UNZIPPED:
            raise PlayError(500, "The zip unpacks to more than 4 GB; not installed.")
        os.makedirs(dest, exist_ok=True)
        for info, name in zip(infos, names):
            rel = name[len(strip):] if strip and name.startswith(strip) else name
            parts = [p for p in rel.split("/") if p not in ("", ".")]
            if not parts:
                continue
            if any(p == ".." or ":" in p for p in parts) or rel.startswith("/"):
                raise PlayError(500, "The zip holds a file outside its folder (%s); not installed." % name[:120])
            target = os.path.join(dest, *parts)
            if name.endswith("/"):
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with zf.open(info) as src, open(target, "wb") as f:
                shutil.copyfileobj(src, f, 1 << 20)


def _rmtree(path):
    def onerr(fn, p, exc):
        try:
            os.chmod(p, stat.S_IWRITE)
            fn(p)
        except OSError:
            pass
    if os.path.isdir(path):
        if sys.version_info >= (3, 12):
            shutil.rmtree(path, onexc=onerr)
        else:
            shutil.rmtree(path, onerror=onerr)


# ---- notes -------------------------------------------------------------------------------------------------------

def one_line(s, limit=MAX_FIELD):
    s = re.sub(r"\s+", " ", str(s or "")).strip()
    return s[:limit].rstrip()


def _sentence(s):
    return s if re.search(r"[.!?)`\"']$", s) else s + "."


def note_title(session):
    label = one_line(session.get("label") or session.get("build") or "game", 80).replace("**", "")
    v = one_line(session.get("version") or "", 60).replace("**", "")
    return "Play note: " + label + (" " + v if v and v not in label else "")


def format_note(session, fields, when):
    """One 12-play-notes.md entry (a single line). `when` is the trailer stamp ('2026-10-24 14:05 CT')."""
    parts = []
    for key, label in FIELDS:
        v = one_line(fields.get(key))
        if v:
            parts.append("%s: %s" % (label, _sentence(v)))
    meta = "Played %s" % fmt_duration(session.get("durationSec"))
    if session.get("endApprox"):
        meta += " or less (it closed while the Supervisor Board was not running)"
    if session.get("commit"):
        meta += ", commit %s" % one_line(session["commit"], 12)
    if session.get("args"):
        meta += ", options `%s`" % one_line(session["args"], MAX_ARGS).replace("`", "'")
    code = session.get("exitCode")
    if code not in (None, 0):
        meta += ", exit code %s" % code
    return "- **%s** %s %s. _%s, q, open_" % (session.get("noteTitle") or note_title(session), " ".join(parts), meta, when)


_DOC_ENTRY = re.compile(r"^\s{0,1}[-*]\s+\*\*(.+?)\*\*.*?\s_(\d{4}-\d{2}-\d{2}(?:\s+\d{1,2}:\d{2}(?:\s*[A-Z]{2,4})?)?)"
                        r",\s*([^,_]*?),\s*([A-Za-z][\w-]*)[^_]*_\s*$")


def parse_doc(text):
    """12-play-notes.md -> [{title, stamp, thread, status}] for every one-line entry (agents set the status)."""
    out = []
    for line in (text or "").replace("\r\n", "\n").split("\n"):
        m = _DOC_ENTRY.match(line)
        if m:
            out.append({"title": m.group(1).strip(), "stamp": m.group(2).strip(), "thread": m.group(3).strip(),
                        "status": m.group(4).strip().lower()})
    return out


def add_note_lines(text, lines):
    """12-play-notes.md's text (None / empty: create it) with `lines` (newest first) at the top of ## Notes."""
    if text is None or not text.strip():
        text = DOCS_HEADER
    rows = text.split("\n")
    head = next((i for i, l in enumerate(rows) if re.match(r"^##\s+%s\s*$" % DOCS_SECTION, l)), None)
    if head is None:
        rows = text.rstrip("\n").split("\n") + ["", "## " + DOCS_SECTION, ""]
        head = len(rows) - 2
    # Right under the heading, each after a blank line, and the blank line that was under the heading stays between
    # them and the older entries: an agent changing the newest older entry's status and q adding a note on another
    # PC copy then touch lines that are not next to each other, so git puts the two together without a conflict.
    block = []
    for l in lines:
        block += ["", l]
    if head + 1 < len(rows) and rows[head + 1].strip():
        block.append("")
    elif head + 1 >= len(rows):
        block.append("")
    rows[head + 1:head + 1] = block
    out = "\n".join(rows)
    return out if out.endswith("\n") else out + "\n"


# ---- the Play service --------------------------------------------------------------------------------------------

class _Ctx:
    def __init__(self, paths):
        self.paths = paths


class Play:
    def __init__(self, paths, releases_url=RELEASES_URL, spawner=spawn, clock=time.time, docs_dir=None, git="git"):
        self.paths = paths
        self.releases_url = releases_url
        self.spawner = spawner
        self.clock = clock
        self.docs_dir = docs_dir or paths.q(DOCS_FOLDER)
        self.git_cmd = git
        self.lock = threading.RLock()
        self.docs_lock = threading.Lock()
        self.stop = threading.Event()
        self.wake = threading.Event()
        self.started = False
        self.procs = {}
        self.sessions = self._load_sessions()
        self.rel = {"ok": None, "fetchedAt": None, "error": None, "list": [], "etag": None, "busy": False,
                    "forcedAt": 0.0, "fetchedTs": 0.0}
        self.installs = {}
        self.sync = self._load_json(SYNC_FILE, {})
        if not isinstance(self.sync, dict):
            self.sync = {}
        self.sync.setdefault("written", [])
        self._doc_cache = (None, [])

    # -- files
    def _load_json(self, name, default):
        try:
            return util.read_json_file(self.paths.pm(name))
        except (OSError, ValueError):
            return default

    def _load_sessions(self):
        d = self._load_json(SESSIONS_FILE, {})
        lst = d.get("sessions") if isinstance(d, dict) else None
        return [s for s in lst if isinstance(s, dict) and SESSION_RE.match(str(s.get("id", "")))] if isinstance(lst, list) else []

    def _save_sessions(self):
        with self.lock:
            self.sessions = self.sessions[-KEEP_SESSIONS:]
            data = {"sessions": self.sessions}
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self.paths.pm(SESSIONS_FILE), data)
        except OSError as e:
            util.log("play: sessions not saved (%s)" % type(e).__name__)

    def _save_sync(self):
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self.paths.pm(SYNC_FILE), self.sync)
        except OSError:
            pass

    def notes(self):
        """Every note saved on this PC (play-notes.jsonl), oldest first."""
        out = []
        try:
            with open(self.paths.pm(NOTES_FILE), "rb") as f:
                for line in f:
                    try:
                        n = json.loads(line.decode("utf-8"))
                    except (ValueError, UnicodeDecodeError):
                        continue
                    if isinstance(n, dict) and n.get("id") and n.get("line"):
                        out.append(n)
        except OSError:
            pass
        return out

    # -- lifecycle
    def start(self):
        if self.started:
            return
        self.started = True
        with self.lock:
            running = [s for s in self.sessions if s.get("state") == "running"]
        for s in running:
            proc = None
            try:
                proc = adopt(s)
            except Exception as e:  # (never let a strange pid stop the server)
                util.log("play: could not check pid %s (%s)" % (s.get("pid"), type(e).__name__))
            if proc:
                self._watch(s["id"], proc)
            else:
                self._ended(s["id"], None, approx=True)
        threading.Thread(target=self._sync_loop, name="pm-play-notes", daemon=True).start()
        if self.needs_sync():
            self.wake.set()

    def shutdown(self):
        self.stop.set()  # (the games keep running; the next server start picks them up by pid)
        self.wake.set()

    # -- builds
    def builds(self):
        out = []
        ctx = _Ctx(self.paths)
        for name in LOCAL_FOLDERS:
            out.append(self._build(builds.read_folder(ctx, name), name, name, "local"))
        vdir = self.paths.q(VERSIONS)
        try:
            tags = [t for t in os.listdir(vdir) if TAG_RE.match(t) and os.path.isdir(os.path.join(vdir, t))]
        except OSError:
            tags = []
        tags.sort(key=lambda t: (util.version_key(t), t), reverse=True)
        for t in tags:
            out.append(self._build(builds.read_folder(ctx, os.path.join(VERSIONS, t)), VERSIONS + "/" + t, t, "version"))
        return out

    def _build(self, e, bid, label, kind):
        exe = find_exe(e["path"]) if e.get("present") else None
        with self.lock:
            running = [s["id"] for s in self.sessions if s.get("state") == "running" and s.get("build") == bid]
        return {"id": bid, "label": label, "kind": kind, "folder": e["path"], "present": e.get("present"),
                "version": e.get("version"), "tag": e.get("tag"), "commit": e.get("commit"), "dirty": e.get("dirty"),
                "mtime": e.get("mtime"), "lines": e.get("lines") or [], "note": e.get("note"),
                "exe": os.path.basename(exe) if exe else None, "playable": bool(exe), "running": running,
                "missing": None if exe else ("folder missing" if not e.get("present") else "no quark.exe in it")}

    def find_build(self, bid):
        for b in self.builds():
            if b["id"] == bid:
                return b
        return None

    # -- launching
    def launch(self, bid, args_text=""):
        b = self.find_build(str(bid or ""))
        if not b:
            raise PlayError(404, "That build is not on this PC.")
        if not b["playable"]:
            raise PlayError(409, "%s has no game in it (%s)." % (b["label"], b["missing"]))
        args = parse_args(args_text)
        now = self.clock()
        sid = "p%s-%s" % (datetime.datetime.fromtimestamp(now, datetime.timezone.utc).strftime("%Y%m%d%H%M%S"),
                          os.urandom(2).hex())
        exe = os.path.join(b["folder"], b["exe"])
        logs = self.paths.pm("logs")
        log_path = None
        try:
            os.makedirs(logs, exist_ok=True)
            log_path = os.path.join(logs, "play-%s.log" % sid)
            self._trim_logs(logs)
        except OSError:
            pass
        proc = self.spawner([exe] + args, b["folder"], log_path)
        s = {"id": sid, "build": b["id"], "label": b["label"], "kind": b["kind"], "version": b["version"],
             "commit": b["commit"], "folder": b["folder"], "exe": exe, "args": (args_text or "").strip() if args else "",
             "startedAt": util.iso(now), "startedTs": now, "endedAt": None,
             "exitCode": None, "durationSec": None, "state": "running", "pid": proc.pid, "pidCreated": proc.created,
             "log": log_path}
        with self.lock:
            self.sessions.append(s)
        self._save_sessions()
        self._watch(sid, proc)
        return dict(s)

    def _trim_logs(self, logs):
        old = sorted((n for n in os.listdir(logs) if re.match(r"^play-p\d{14}-[0-9a-f]{4}\.log$", n)), reverse=True)
        for n in old[KEEP_LOGS - 1:]:
            try:
                os.remove(os.path.join(logs, n))
            except OSError:
                pass

    def _watch(self, sid, proc):
        with self.lock:
            self.procs[sid] = proc

        def run():
            try:
                ended, code = proc.wait(self.stop)
            except Exception as e:  # (a handle that went bad: treat as ended, unknown code)
                util.log("play: waiting for the game failed (%s)" % type(e).__name__)
                ended, code = True, None
            with self.lock:
                self.procs.pop(sid, None)
            if ended and not self.stop.is_set():
                self._ended(sid, code)
        threading.Thread(target=run, name="pm-play-" + sid, daemon=True).start()

    def _ended(self, sid, code, approx=False):
        now = self.clock()
        with self.lock:
            s = self._session(sid)
            if not s or s.get("state") != "running":
                return
            start = util.to_float(s.get("startedTs")) or util.parse_time(s.get("startedAt")) or now
            s.update(endedAt=util.iso(now), exitCode=code, durationSec=round(max(0.0, now - start), 1),
                     state="needsNote")
            if approx:
                s["endApprox"] = True
        self._save_sessions()

    def _session(self, sid):
        for s in self.sessions:
            if s.get("id") == sid:
                return s
        return None

    # -- notes
    def skip(self, sid):
        with self.lock:
            s = self._session(str(sid or ""))
            if not s:
                raise PlayError(404, "No such game session.")
            if s.get("state") == "skipped":
                return dict(s, noop=True)  # (skipped already, e.g. from another page: nothing to do)
            if s.get("state") == "noted":
                raise PlayError(409, "That game's note was already saved.", "already-noted")
            if s.get("state") != "needsNote":
                raise PlayError(409, "That game is still running.", "running")
            s["state"] = "skipped"
            s["skippedAt"] = util.now_iso()
        self._save_sessions()
        return dict(s)

    def save_note(self, sid, fields):
        if not isinstance(fields, dict):
            raise PlayError(400, "The page sent a note without its fields.")
        clean = {k: one_line(fields.get(k)) for k, _ in FIELDS}
        if not any(clean.values()):
            raise PlayError(400, "Write something in at least one box, or press Skip.")
        now = self.clock()
        when = stamp(now)
        with self.lock:
            s = self._session(str(sid or ""))
            if not s:
                raise PlayError(404, "No such game session.")
            if s.get("state") == "noted":  # (saved from another page already: never a second copy)
                raise PlayError(409, "That game's note was already saved.", "already-noted")
            if s.get("state") not in ("needsNote", "skipped"):
                raise PlayError(409, "That game is still running.", "running")
            base = title = note_title(s)
            taken = {(n.get("title"), n.get("stamp")) for n in self.notes()}
            k = 2
            while (title, when) in taken:  # (two notes for one build in the same minute)
                title = "%s #%d" % (base, k)
                k += 1
            s2 = dict(s, noteTitle=title)
            line = format_note(s2, clean, when)
            note = {"id": s["id"], "session": s["id"], "savedAt": util.iso(now), "stamp": when, "title": title,
                    "line": line, "fields": clean, "build": s.get("build"), "label": s.get("label"),
                    "version": s.get("version"), "commit": s.get("commit"), "args": s.get("args"),
                    "durationSec": s.get("durationSec"), "startedAt": s.get("startedAt"), "endedAt": s.get("endedAt"),
                    "exitCode": s.get("exitCode")}
            try:
                self.paths.ensure_pm_dir()
                with open(self.paths.pm(NOTES_FILE), "ab") as f:
                    f.write((json.dumps(note, ensure_ascii=False) + "\n").encode("utf-8"))
            except OSError as e:
                raise PlayError(500, "Could not save the note on this PC (%s)." % (e.strerror or type(e).__name__))
            s.update(state="noted", notedAt=note["savedAt"], noteTitle=title)
        self._save_sessions()
        self.wake.set()  # (written to Claude Documents and pushed in the background)
        return note

    # -- Claude Documents (git)
    def _git(self, *args, timeout=60):
        env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
        try:
            p = subprocess.run([self.git_cmd, "-C", self.docs_dir] + list(args), stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env,
                               creationflags=util.NO_WINDOW)
        except FileNotFoundError:
            return 127, "", "git not found"
        except subprocess.TimeoutExpired:
            return 124, "", "git %s timed out" % args[0]
        dec = lambda b: b.decode("utf-8", errors="replace")
        return p.returncode, dec(p.stdout).strip(), dec(p.stderr).strip()

    @staticmethod
    def _err(err, default):
        lines = [l for l in (err or "").splitlines() if l.strip()]
        for l in lines:
            if l.startswith(("fatal:", "error:")):
                return l.split(":", 1)[1].strip()[:200]
        return (lines[-1] if lines else default)[:200]

    def _identity(self):
        if self._git("config", "user.name")[1] and self._git("config", "user.email")[1]:
            return []
        return ["-c", "user.name=" + GIT_NAME, "-c", "user.email=" + GIT_EMAIL]

    def _pull(self):
        """fetch + rebase --autostash onto the upstream -> (ok, why, conflict). Offline / no upstream: not ok; a
        rebase that stops on a conflict is aborted (conflict True), leaving the folder as it was."""
        if self._git("rev-parse", "--abbrev-ref", "@{u}")[0] != 0:
            return False, "the Claude Documents folder has no upstream branch", False
        code, _, err = self._git("fetch", "-q", timeout=90)
        if code != 0:
            return False, "couldn't reach GitHub: " + self._err(err, "fetch failed"), False
        code, _, err = self._git(*(self._identity() + ["rebase", "--autostash", "-q"]), timeout=120)
        if code != 0:
            stopped = False
            for p in ("rebase-merge", "rebase-apply"):
                gp = self._git("rev-parse", "--git-path", p)[1]
                if gp and os.path.isdir(gp if os.path.isabs(gp) else os.path.join(self.docs_dir, gp)):
                    self._git(*(self._identity() + ["rebase", "--abort"]), timeout=60)
                    stopped = True
                    break
            return False, "couldn't bring the PC copy up to date: " + self._err(err, "rebase failed"), stopped
        return True, None, False

    def _doc_keys(self):
        try:
            return {(e["title"], e["stamp"]) for e in parse_doc(util.read_text(os.path.join(self.docs_dir, DOCS_FILE)))}
        except OSError:
            return set()

    def _drop_own_commits(self):
        """After a rebase refused on a conflict: when every commit not yet on GitHub is a play-notes commit of ours
        (only 12-play-notes.md, our message), drop them (`reset --keep @{u}`) so their notes are written again on top
        of GitHub's newest file. -> the (title, stamp) keys to write again, or None (nothing done; q's other commits
        are never dropped)."""
        code, out, _ = self._git("log", "--format=%x00%s", "--name-only", "@{u}..HEAD")
        commits = [[l for l in c.split("\n") if l.strip()] for c in out.split("\x00") if c.strip()]
        if code != 0 or not commits or any(not c[0].endswith(VIA) or c[1:] != [DOCS_FILE] for c in commits):
            return None
        before = self._doc_keys()
        if self._git("reset", "-q", "--keep", "@{u}")[0] != 0:
            return None
        return before - self._doc_keys()

    def _busy(self):
        for p in ("rebase-merge", "rebase-apply", "MERGE_HEAD"):
            gp = self._git("rev-parse", "--git-path", p)[1]
            if gp and os.path.exists(gp if os.path.isabs(gp) else os.path.join(self.docs_dir, gp)):
                return "git is in the middle of a rebase or merge in the Claude Documents folder"
        return None

    def sync_docs(self):
        """Write the notes not yet in 12-play-notes.md, commit that file, push. Returns the sync state."""
        with self.docs_lock:
            st = self._sync_once()
            st["at"] = util.now_iso()
            self.sync.update(st)
            self._save_sync()
            return dict(self.sync)

    def _sync_once(self):
        notes = self.notes()
        if not os.path.isdir(self.docs_dir):
            return {"ok": False, "pushed": False, "error": "No Claude Documents folder at %s; the notes are kept in "
                    "project-manager\\%s." % (self.docs_dir, NOTES_FILE)}
        if self._git("rev-parse", "--is-inside-work-tree")[1] != "true":
            return {"ok": False, "pushed": False, "error": "The Claude Documents folder is not a git copy."}
        busy = self._busy()
        if busy:
            return {"ok": False, "pushed": False, "error": busy + "; trying again later."}
        online, why, conflict = self._pull()
        written = set(self.sync.get("written") or [])
        if conflict:
            again = self._drop_own_commits()
            if again is not None:
                written -= {n["id"] for n in notes if (n.get("title"), n.get("stamp")) in again}
                online, why, conflict = self._pull()
        path = os.path.join(self.docs_dir, DOCS_FILE)
        try:
            with open(path, "rb") as f:
                raw = f.read()
        except FileNotFoundError:
            raw = None
        except OSError as e:
            return {"ok": False, "pushed": False, "error": "Can't read %s (%s)." % (DOCS_FILE, type(e).__name__)}
        bom = raw.startswith(b"\xef\xbb\xbf") if raw else False
        text = raw.decode("utf-8-sig", errors="replace") if raw is not None else None
        crlf = bool(text) and "\r\n" in text
        text = text.replace("\r\n", "\n") if text is not None else None
        present = {(e["title"], e["stamp"]) for e in parse_doc(text)}
        todo = [n for n in notes if n["id"] not in written and (n.get("title"), n.get("stamp")) not in present]
        for n in notes:  # (already in the file: an earlier write whose bookkeeping did not finish)
            if n["id"] not in written and (n.get("title"), n.get("stamp")) in present:
                written.add(n["id"])
        if todo:
            new = add_note_lines(text, [n["line"] for n in reversed(todo)])  # (newest first)
            data = (new.replace("\n", "\r\n") if crlf else new).encode("utf-8")
            try:
                with open(path, "wb") as f:
                    f.write((b"\xef\xbb\xbf" if bom else b"") + data)
            except OSError as e:
                return {"ok": False, "pushed": False, "error": "Can't write %s (%s)." % (DOCS_FILE, type(e).__name__)}
        result = {"ok": True, "error": None}
        code, _, err = self._git("add", "--", DOCS_FILE)
        if code == 0 and self._git("diff", "--cached", "--quiet", "--", DOCS_FILE)[0] != 0:
            msg = todo[0]["title"] if len(todo) == 1 else "Play notes: %d" % max(len(todo), 1)
            code, _, err = self._git(*(self._identity() + ["commit", "-q", "-m", msg + VIA, "--", DOCS_FILE]))
        if code != 0:
            self.sync["written"] = sorted(written)
            return {"ok": False, "pushed": False, "error": "Wrote the note into the file on this PC, but git couldn't "
                    "commit it: " + self._err(err, "commit failed")}
        written.update(n["id"] for n in todo)
        self.sync["written"] = sorted(written)
        unpushed = util.to_int(self._git("rev-list", "--count", "@{u}..HEAD")[1], 0) or 0
        if unpushed and online:
            for attempt in (0, 1):
                code, _, err = self._git("push", "-q", timeout=120)
                if code == 0:
                    break
                low = err.lower()
                if attempt == 0 and ("rejected" in low or "fetch first" in low or "non-fast-forward" in low):
                    ok = self._pull()[0]
                    if ok:
                        continue
                break
            if code == 0:
                unpushed = 0
                result["pushedAt"] = util.now_iso()
            else:
                why = "couldn't push: " + self._err(err, "push failed")
        result.update(pushed=unpushed == 0, unpushed=unpushed)
        if unpushed:
            result["error"] = "Saved on this PC, will go up later (%s)." % (why or "not pushed yet")
        return result

    def needs_sync(self):
        """A note not yet written into Claude Documents, or a commit not yet pushed."""
        written = set(self.sync.get("written") or [])
        return bool(self.sync.get("unpushed")) or any(n["id"] not in written for n in self.notes())

    def _sync_loop(self):
        # (git runs only when there is something to send: a new note, or the retry every 5 min while one waits)
        while not self.stop.is_set():
            woke = self.wake.wait(RETRY_EVERY if self.needs_sync() else None)
            self.wake.clear()
            if self.stop.is_set():
                break
            if not (woke or self.needs_sync()):
                continue
            try:
                self.sync_docs()
            except Exception as e:  # (never let the thread die)
                util.log("play notes: sync failed (%s)" % type(e).__name__)

    def doc_entries(self):
        """parse_doc of 12-play-notes.md, cached by size + mtime (the shell asks every 2 s while a game runs)."""
        path = os.path.join(self.docs_dir, DOCS_FILE)
        try:
            st = os.stat(path)
            key = (st.st_size, st.st_mtime_ns)
        except OSError:
            return []
        if self._doc_cache[0] != key:
            try:
                self._doc_cache = (key, parse_doc(util.read_text(path)))
            except OSError:
                return []
        return self._doc_cache[1]

    def open_count(self):
        return sum(1 for e in self.doc_entries() if e["status"] == "open")

    # -- releases
    def releases(self, force=False):
        now = self.clock()
        with self.lock:
            r = self.rel
            stale = now - r["fetchedTs"] > RELEASES_TTL
            if force and now - r["forcedAt"] >= RELEASES_MIN_FORCE:
                r["forcedAt"] = now
                stale = True
            if stale and not r["busy"]:
                r["busy"] = True
                threading.Thread(target=self._fetch_releases, name="pm-play-releases", daemon=True).start()
            return {k: r[k] for k in ("ok", "fetchedAt", "error", "busy", "list")}

    def _fetch_releases(self):
        r = self.rel
        try:
            hdr = {"User-Agent": util.USER_AGENT, "Accept": "application/vnd.github+json"}
            if r["etag"] and r["list"]:
                hdr["If-None-Match"] = r["etag"]
            req = urllib.request.Request(self.releases_url, headers=hdr)
            try:
                with _opener(self.releases_url).open(req, timeout=15) as resp:
                    body, etag = resp.read(8 << 20), resp.headers.get("ETag")
                lst = parse_releases(json.loads(body.decode("utf-8-sig")))
                with self.lock:
                    r.update(list=lst, etag=etag, ok=True, error=None)
            except urllib.error.HTTPError as e:
                if e.code == 304:
                    with self.lock:
                        r.update(ok=True, error=None)
                elif e.code in (403, 429) and (e.headers.get("X-RateLimit-Remaining") == "0" or e.code == 429):
                    reset = util.to_int(e.headers.get("X-RateLimit-Reset"))
                    at = (" until " + dallas(reset).strftime("%H:%M") + " Dallas") if reset else ""
                    raise util.SourceError("GitHub's limit for requests without signing in is used up%s" % at)
                else:
                    raise util.SourceError("GitHub answered HTTP %d" % e.code)
        except util.SourceError as e:
            with self.lock:
                r.update(ok=False, error=str(e))
        except (urllib.error.URLError, OSError, ValueError, UnicodeDecodeError) as e:
            with self.lock:
                r.update(ok=False, error="GitHub unreachable (%s)" % str(getattr(e, "reason", e))[:120])
        finally:
            with self.lock:
                r["busy"] = False
                r["fetchedTs"] = self.clock()
                r["fetchedAt"] = util.iso(r["fetchedTs"])

    # -- installing older versions
    def install(self, tag):
        tag = str(tag or "")
        with self.lock:
            rel = next((x for x in self.rel["list"] if x["tag"] == tag), None)
            if not rel:
                raise PlayError(404, "That release is not in the list (refresh it).")
            if not rel["zip"]:
                raise PlayError(409, "%s has no %s." % (tag, ZIP_NAME))
            if any(i.get("state") in ("downloading", "verifying", "extracting") for i in self.installs.values()):
                raise PlayError(409, "Another version is being installed; wait for it.")
            if self._in_use(tag):
                raise PlayError(409, "%s is running; close the game first." % tag)
            self.installs[tag] = {"state": "downloading", "got": 0, "total": rel["zip"]["size"] or 0,
                                  "startedAt": util.now_iso(), "error": None}
        threading.Thread(target=self._install, args=(rel,), name="pm-play-install", daemon=True).start()
        return dict(self.installs[tag])

    def _in_use(self, tag):
        bid = VERSIONS + "/" + tag
        return any(s.get("state") == "running" and s.get("build") == bid for s in self.sessions)

    def _set_install(self, tag, **kw):
        with self.lock:
            self.installs[tag].update(kw)

    def _install(self, rel):
        tag = rel["tag"]
        vdir = self.paths.q(VERSIONS)
        tmp = os.path.join(vdir, ".download-%s.zip" % tag)
        partial = os.path.join(vdir, ".partial-%s" % tag)
        try:
            os.makedirs(vdir, exist_ok=True)
            h = hashlib.sha256()
            req = urllib.request.Request(rel["zip"]["url"], headers={"User-Agent": util.USER_AGENT,
                                                                     "Accept": "application/octet-stream"})
            got = 0
            with _opener(rel["zip"]["url"]).open(req, timeout=30) as resp, open(tmp, "wb") as f:
                total = util.to_int(resp.headers.get("Content-Length"), 0) or rel["zip"]["size"] or 0
                self._set_install(tag, total=total)
                while True:
                    if self.stop.is_set():
                        raise PlayError(500, "The server stopped during the download.")
                    chunk = resp.read(256 * 1024)
                    if not chunk:
                        break
                    got += len(chunk)
                    if got > MAX_ZIP:
                        raise PlayError(500, "The download is bigger than 1 GB; stopped.")
                    h.update(chunk)
                    f.write(chunk)
                    self._set_install(tag, got=got)
            self._set_install(tag, state="verifying")
            want, verified = None, False
            if rel.get("sums"):
                sreq = urllib.request.Request(rel["sums"], headers={"User-Agent": util.USER_AGENT})
                with _opener(rel["sums"]).open(sreq, timeout=30) as resp:
                    want = parse_sums(resp.read(1 << 20).decode("utf-8", "replace"))
            if want:
                if h.hexdigest() != want:
                    raise PlayError(500, "The download does not match the release's SHA256SUMS; not installed.")
                verified = True
            self._set_install(tag, state="extracting")
            _rmtree(partial)
            extract_zip(tmp, partial)
            if not find_exe(partial):
                raise PlayError(500, "The zip has no quark.exe; not installed.")
            with open(os.path.join(partial, "BUILD.txt"), "w", encoding="utf-8") as f:
                f.write("%s (GitHub release, published %s)\n" % (tag, rel.get("publishedAt") or "unknown"))
            dest = os.path.join(vdir, tag)
            if self._in_use(tag):
                raise PlayError(409, "%s started while installing; close it and install again." % tag)
            _rmtree(dest)
            os.replace(partial, dest)
            self._set_install(tag, state="done", verified=verified, doneAt=util.now_iso(),
                              note=None if verified else "not checked: the release has no SHA256SUMS line for the zip")
        except PlayError as e:
            self._set_install(tag, state="failed", error=e.message)
        except (urllib.error.URLError, OSError, ValueError, zipfile.BadZipFile) as e:
            self._set_install(tag, state="failed", error="%s (%s)" % (type(e).__name__, str(getattr(e, "reason", e))[:160]))
        finally:
            for p in (tmp,):
                try:
                    os.remove(p)
                except OSError:
                    pass
            _rmtree(partial)

    def remove(self, tag):
        tag = str(tag or "")
        if not TAG_RE.match(tag):
            raise PlayError(400, "Not a version name.")
        vdir = os.path.realpath(self.paths.q(VERSIONS))
        dest = os.path.realpath(os.path.join(vdir, tag))
        if os.path.dirname(dest) != vdir or not os.path.isdir(dest):
            raise PlayError(404, "%s is not installed." % tag)
        with self.lock:
            if self._in_use(tag):
                raise PlayError(409, "%s is running; close the game first." % tag)
            if (self.installs.get(tag) or {}).get("state") in ("downloading", "verifying", "extracting"):
                raise PlayError(409, "%s is being installed." % tag)
            self.installs.pop(tag, None)
        _rmtree(dest)
        if os.path.isdir(dest):
            raise PlayError(500, "Could not remove all of %s (a file is in use?)." % dest)
        return {"removed": tag}

    # -- views
    def pending(self):
        with self.lock:
            run = [s for s in self.sessions if s.get("state") == "running"]
            pend = [{"id": s["id"], "label": s.get("label"), "version": s.get("version"), "endedAt": s.get("endedAt"),
                     "durationSec": s.get("durationSec")} for s in self.sessions if s.get("state") == "needsNote"]
        return {"running": len(run), "pending": pend, "openNotes": self.open_count()}

    def summary(self):
        p = self.pending()
        return {"running": p["running"], "pending": len(p["pending"]), "openNotes": p["openNotes"]}

    def view(self):
        rel = self.releases()
        installed = {b["id"][len(VERSIONS) + 1:] for b in self.builds() if b["kind"] == "version" and b["playable"]}
        lst = [dict(x, installed=x["tag"] in installed) for x in rel["list"]]
        status = {(e["title"], e["stamp"]): e["status"] for e in self.doc_entries()}
        written = set(self.sync.get("written") or [])
        unpushed = bool(self.sync.get("unpushed"))
        notes = []
        for n in reversed(self.notes()[-20:]):
            st = status.get((n.get("title"), n.get("stamp")))
            where = ("waiting to go into Claude Documents" if n["id"] not in written else
                     "saved on this PC, will go up later" if unpushed else "in Claude Documents")
            notes.append({"id": n["id"], "title": n.get("title"), "stamp": n.get("stamp"), "savedAt": n.get("savedAt"),
                          "fields": n.get("fields"), "label": n.get("label"), "version": n.get("version"),
                          "durationSec": n.get("durationSec"), "args": n.get("args"), "status": st, "where": where})
        with self.lock:
            sessions = [dict(s) for s in reversed(self.sessions[-30:])]
            installs = {k: dict(v) for k, v in self.installs.items()}
        for s in sessions:
            s.pop("pidCreated", None)
        p = self.pending()
        return {"now": util.now_iso(), "builds": self.builds(), "releases": dict(rel, list=lst), "installs": installs,
                "sessions": sessions, "running": p["running"], "pending": p["pending"], "openNotes": p["openNotes"],
                "notes": notes, "docs": {"folder": self.docs_dir, "present": os.path.isdir(self.docs_dir),
                                         "file": DOCS_FILE, "sync": {k: v for k, v in self.sync.items() if k != "written"}},
                "platform": "windows" if util.IS_WINDOWS else sys.platform}
