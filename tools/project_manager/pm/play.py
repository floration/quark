"""The Play tab (http://127.0.0.1:8768/play): start a game build, and when it closes ask q for a play note.

Builds: the local game folders (Quark\\production, testing, working, testing-f3; BUILD.txt parsed by builds.py) and
older GitHub releases installed into Quark\\versions\\<tag>\\ (the release's quark-windows.zip, BUILD.txt written like
tools/fetch_release.ps1, and play-install.json: how the download was checked). The release list comes from the public
GitHub API without signing in (60 requests an hour), every page of it (100 a page, until a short page, like the
launcher: production releases stay listed behind any number of test builds), cached 10 min with page 1's ETag (a 304
does not count against the limit and means nothing new was published) and refreshed in the background, so a page
request never waits on GitHub.

Install checks a release the way the launcher does (docs/SIGNING.md): from 1.17.0 on (and any release that has a
signature) the zip's Ed25519ph signature (quark-windows.zip.sig) must verify against the release key
(RELEASE_PUBLIC_KEY = kReleasePublicKey in src/common/sign.cpp), and the release's signed manifest
(quark-release.txt + .sig) must verify and name this tag, version, channel and the zip's sha256, or nothing is
installed. A release from before signing (<= 1.16.x) has neither: it installs without a prompt (owner decision B6),
checked against its SHA256SUMS when it has one, and is marked "unsigned" on its card. A download can be cancelled.

Launching: the game's exe (quark.exe; it is a GUI-subsystem program, CMakeLists `add_executable(quark WIN32 ...)`)
starts with its folder as working folder, normal priority, never elevated, stdout/stderr into
Quark\\project-manager\\logs\\play-<session>.log (the page shows its tail; a crash is flagged). On Windows it is
started through a short-lived hidden python process (HANDOFF below) that starts the game and exits at once, so the
game's parent is gone: the shell's `taskkill /T` of the Supervisor Board server (window closed, app updated) never
reaches the game, and the game is started with CREATE_BREAKAWAY_FROM_JOB (when the job allows it) and
CREATE_NO_WINDOW (no console; its own window shows). This process then opens the game by pid (OpenProcess) and waits
for it in a thread. Stop asks the game to close (Windows: WM_CLOSE through taskkill without /F; elsewhere SIGTERM to
its process group) and ends it after STOP_GRACE seconds if it is still there.

Sessions (Quark\\project-manager\\play-sessions.json): {id, build, label, version, commit, folder, exe, args,
startedAt, endedAt, exitCode, durationSec, state: running | needsNote | noted | skipped, pid, pidCreated, log,
stopped}. When the game exits the session becomes needsNote (the page opens the note form; the shell brings the
window up). When this server restarts while a game runs, the session is picked up again by pid: on Windows only when
the process at that pid was created when the game was (GetProcessTimes, within 2 s; a reused pid is not the game),
elsewhere when it is alive and its command line names the exe. A game that ended while the server was down still
gets a note (`endApprox`: the end time is when the server noticed).

Notes: each saved note is appended to Quark\\project-manager\\play-notes.jsonl (the local copy, kept forever; an
edit or a removal is appended as its own record, newest wins) and written as one entry at the top of `## Notes` in
Quark\\Claude Documents\\12-play-notes.md (the Claude Manager's entry shape: `- **Play note: <build> <version>** ...
_YYYY-MM-DD HH:MM CT, q, open_`; line breaks q typed become " / "), then committed (that file only) and pushed the way
tools/docs_manager/docsedit.py does (fetch + rebase --autostash first; git never prompts, no console window).
Offline, it stays committed on this PC and goes up on the next save or the retry every 5 min. A note can also be
written without a session (a game played through the launcher). q can edit or remove a note while its entry is still
`open` (no agent has taken it): the entry is changed in place, or removed. A note written once is never written
again, even when an agent moves or removes it.
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

RELEASES_URL = "https://api.github.com/repos/floration/quark/releases"
RELEASES_PER_PAGE = 100       # (GitHub's most; a shorter page is the last one)
RELEASES_MAX_PAGES = 50       # (a server that never sends a short page stops here)
RELEASES_TTL = 600            # s: the list is fetched again after 10 min (when the page asks)
RELEASES_MIN_FORCE = 30       # s: a "Refresh" press at most this often
ZIP_NAME = "quark-windows.zip"
SIG_NAME = ZIP_NAME + ".sig"
SUMS_NAME = "SHA256SUMS"
MANIFEST_NAME = "quark-release.txt"
# The release key's public half: kReleasePublicKey in src/common/sign.cpp (tests/test_play.py checks they match)
RELEASE_PUBLIC_KEY = "0b059c406106eed2be15b6d840166ec6bcb72817450bfa2dcae561c07b6b8b7e"
FIRST_SIGNED = "1.17.0-0"     # (releases.h kFirstSigned: below every 1.17.0 build, above every 1.16.x)
INSTALL_INFO = "play-install.json"
MAX_ZIP = 1 << 30             # bytes: a bigger download is refused
MAX_UNZIPPED = 4 << 30
LOCAL_FOLDERS = builds.FOLDERS  # production, testing, working, testing-f3
VERSIONS = "versions"
SESSIONS_FILE = "play-sessions.json"
NOTES_FILE = "play-notes.jsonl"
SYNC_FILE = "play-notes-sync.json"
RELEASES_FILE = "play-releases.json"   # (the release list, page 1's ETag and when it was read: shown at once after a restart)
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
LOG_VIEW_BYTES = 256 * 1024   # the log view shows this much of the end of a game's log
NOTE_LOG_LINES, NOTE_LOG_CHARS = 12, 1200   # "add the log's last lines" to a note: at most this much
STOP_GRACE = 5.0              # s: Stop asks the game to close, then ends it
MAX_FIELD = 4000              # characters per note box (the form's maxlength; longer is refused, never cut)
MAX_ARGS = 400
RETRY_EVERY = 300             # s: a note not yet on GitHub is tried again this often
GIT_NAME, GIT_EMAIL = "q (Supervisor Board)", "supervisor-board@localhost"
VIA = " (q, via Supervisor Board)"   # (ends every commit message here: how a play-notes commit of ours is told apart)
TAG_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$")
SESSION_RE = re.compile(r"^p\d{14}-[0-9a-f]{4}$")
NOTE_RE = re.compile(r"^[pn]\d{14}-[0-9a-f]{4}$")   # (a note's id: its session's, or n... for a note without one)
NOT_GAME = ("quark_launcher", "quark_server", "quark_tests", "quark_sign", "quark_maptool", "fuzz_")
FIELDS = (("did", "Did"), ("noticed", "Noticed"), ("change", "Wants changed"), ("other", "Also"))
# The game's modes in its own order (modeName in src/game/gamedefs.h; tests/test_play.py checks they match): the mode
# picker's order. A mode only a build's maps name (a newer game) is listed after these.
GAME_MODES = ("vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points")
GAME_MODE_TITLES = ("VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL", "1V1",
                    "TOURNEY", "HIDE AND SEEK", "TAG", "MULTI POINT")   # (modeTitle, same order)
MAP_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}\.qmap$")
MAX_MAPS = 300                # (a maps folder with more is cut there: the picker is a list, not a file browser)

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


def read_maps(folder):
    """A build's maps folder (<folder>/maps/*.qmap, as every package ships it) -> [{name, title, for, modes,
    handmade}] by name, from each map's header lines (tools/make_maps.py writes `title`, `for <mode>` and
    `modes <mode> ...` at the top; the editor writes `handmade`)."""
    out = []
    try:
        names = sorted(n for n in os.listdir(os.path.join(folder, "maps")) if MAP_RE.match(n))[:MAX_MAPS]
    except OSError:
        return out
    for n in names:
        m = {"name": n[:-5], "title": None, "for": None, "modes": [], "handmade": False}
        try:
            with open(os.path.join(folder, "maps", n), "rb") as f:
                head = f.read(4096).decode("utf-8", "replace").split("\n")[:40]
        except OSError:
            continue
        for line in head:
            key, _, val = line.strip().partition(" ")
            if key == "title" and val:
                m["title"] = one_line(val, 60)
            elif key == "for" and re.match(r"^[a-z0-9]{1,16}$", val):
                m["for"] = val
            elif key == "modes":
                m["modes"] = [x for x in val.split() if re.match(r"^[a-z0-9]{1,16}$", x)][:20]
            elif key == "handmade":
                m["handmade"] = True
        if m["for"] and m["for"] not in m["modes"]:
            m["modes"].insert(0, m["for"])
        out.append(m)
    return out


def map_modes(maps):
    """The modes for the picker: the game's own, then any other mode a map names."""
    named = {x for m in maps for x in m["modes"]}
    return list(GAME_MODES) + sorted(named - set(GAME_MODES))


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
            k.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
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

    @classmethod
    def terminate(cls, pid, created):
        """End the process at pid, only when it was created when the game was (within 2 s)."""
        h = cls.k32().OpenProcess(0x0001 | 0x1000, False, int(pid))  # PROCESS_TERMINATE | QUERY_LIMITED_INFORMATION
        if not h:
            return False
        try:
            c = cls.created(h)
            if created is not None and (c is None or abs(c - created) > 2.0):
                return False
            return bool(cls.k32().TerminateProcess(h, 1))
        finally:
            cls.close(h)


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


def terminate(proc, force):
    """Ask a game to close (force False: Windows WM_CLOSE through `taskkill /PID` without /F, elsewhere SIGTERM) or
    end it (force True: TerminateProcess / SIGKILL). Off Windows the signal goes to its process group (spawn starts
    it in a new session, so whatever it started goes too), and a pid that is no longer the game is left alone."""
    if util.IS_WINDOWS:
        if force:
            _Win.terminate(proc.pid, proc.created)
        else:
            subprocess.run(["taskkill", "/PID", str(int(proc.pid))], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=15, creationflags=CREATE_NO_WINDOW)
        return
    import signal
    pid = int(proc.pid)
    if pid <= 1 or (proc.popen is None and not pid_alive(pid, proc.exe)):
        return
    if proc.popen is not None and proc.popen.poll() is not None:
        return  # (already ended: its pid may belong to something else now)
    sig = signal.SIGKILL if force else signal.SIGTERM
    try:
        if os.getpgid(pid) == pid and pid != os.getpgid(0):
            os.killpg(pid, sig)
            return
    except OSError:
        pass
    try:
        os.kill(pid, sig)
    except OSError:
        pass


def open_with_system(path):
    """Open a folder in Explorer (Finder, the file manager) or a file in its default app. Raises PlayError."""
    if util.IS_WINDOWS:
        try:
            os.startfile(path)  # (Explorer / the default app; no console)
        except OSError as e:
            raise PlayError(500, "Couldn't open %s (%s)." % (path, e.strerror or type(e).__name__))
        return
    # (elsewhere a helper program opens it: when that is what's missing, say so, not that the file is)
    opener = "open" if sys.platform == "darwin" else "xdg-open"
    exe = shutil.which(opener)
    if not exe:
        raise PlayError(500, "Couldn't open %s: %s is not installed on this PC." % (path, opener), "no-opener")
    try:
        subprocess.Popen([exe, path], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True, close_fds=True)
    except OSError as e:
        raise PlayError(500, "Couldn't start %s to open %s (%s)." % (opener, path, e.strerror or type(e).__name__))


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
    """GitHub's /releases JSON -> [{tag, name, channel, prerelease, publishedAt, version, url, zip, sums, sig,
    manifest, manifestSig, needsSig}], newest first. Drafts and odd tags are left out; a release without
    quark-windows.zip has zip None (shown greyed)."""
    out = []
    for r in data if isinstance(data, list) else []:
        if not isinstance(r, dict) or r.get("draft"):
            continue
        tag = r.get("tag_name")
        if not isinstance(tag, str) or not TAG_RE.match(tag):
            continue
        assets = {a.get("name"): a for a in r.get("assets") or [] if isinstance(a, dict)}

        def url(name):
            u = (assets.get(name) or {}).get("browser_download_url")
            return u if _url_ok(u) else None
        z = assets.get(ZIP_NAME)
        pre = r.get("prerelease") is True
        channel = "production" if (tag.startswith("v") and not pre) else "testing" if tag.startswith("test-") else "other"
        name = str(r.get("name") or tag)[:200]
        version = util.parse_version(tag)
        rel = {"tag": tag, "name": name, "channel": channel, "prerelease": pre,
               "publishedAt": r.get("published_at") or r.get("created_at"),
               "version": version, "url": r.get("html_url") if _url_ok(r.get("html_url")) else None,
               "zip": {"url": url(ZIP_NAME), "size": util.to_int(z.get("size"), 0)} if z and url(ZIP_NAME) else None,
               "sums": url(SUMS_NAME), "sig": url(SIG_NAME), "manifest": url(MANIFEST_NAME),
               "manifestSig": url(MANIFEST_NAME + ".sig")}
        rel["needsSig"] = needs_signature(rel)
        # (the page shows the name only when it says more than the tag: "quark 1.18.30-test.323 (testing)" doesn't)
        rel["nameSaysMore"] = bool(name != tag and not (version and util.base_version(version) in name))
        out.append(rel)
    out.sort(key=lambda x: (util.parse_time(x["publishedAt"]) or 0, util.version_key(x["tag"])), reverse=True)
    return out


def valid_release(x):
    """A release row read back from play-releases.json is one parse_releases could have made (only https / local
    links, a tag that is a plain name): anything else is dropped, never shown or installed."""
    if not isinstance(x, dict) or not isinstance(x.get("tag"), str) or not TAG_RE.match(x["tag"]):
        return False
    if x.get("channel") not in ("production", "testing", "other") or not isinstance(x.get("name"), str):
        return False
    for k in ("url", "sums", "sig", "manifest", "manifestSig"):
        if x.get(k) is not None and not _url_ok(x[k]):
            return False
    z = x.get("zip")
    if z is not None and not (isinstance(z, dict) and _url_ok(z.get("url")) and isinstance(z.get("size"), int)):
        return False
    return all(x.get(k) is None or isinstance(x[k], str) for k in ("publishedAt", "version"))


def page_url(base, n):
    return "%s%sper_page=%d&page=%d" % (base, "&" if "?" in base else "?", RELEASES_PER_PAGE, n)


def parse_sums(text, name=ZIP_NAME):
    """SHA256SUMS ('<hex>  <file>' or '<hex> *<file>') -> the hex for `name`, or None."""
    for line in (text or "").splitlines():
        m = re.match(r"^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$", line)
        if m and m.group(2).split("/")[-1] == name:
            return m.group(1).lower()
    return None


# ---- signatures (docs/SIGNING.md): Ed25519ph verification, the same check as `quark_sign verify` ---------------

# RFC 8032 section 5.1 with the SHA-512 prehash (Ed25519ph, empty context), as Monocypher's crypto_ed25519_ph_check:
# the signature (R, S) is good when [S]B - [h]A encodes to exactly R. Extended coordinates (X, Y, Z, T); a check takes
# a few ms in pure Python and runs only in the install thread.
_P = 2 ** 255 - 19
_L = 2 ** 252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)
_DOM2_PH = b"SigEd25519 no Ed25519 collisions" + bytes([1, 0])   # dom2(phflag 1, context "")


def _pt_add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    pa, pb = (y1 - x1) * (y2 - x2) % _P, (y1 + x1) * (y2 + x2) % _P
    pc, pd = 2 * t1 * t2 * _D % _P, 2 * z1 * z2 % _P
    e, f, g, h = pb - pa, pd - pc, pd + pc, pb + pa
    return (e * f % _P, g * h % _P, f * g % _P, e * h % _P)


def _pt_mul(k, a):
    q = (0, 1, 1, 0)
    while k:
        if k & 1:
            q = _pt_add(q, a)
        a = _pt_add(a, a)
        k >>= 1
    return q


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P) % _P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * _SQRT_M1 % _P
    if (x * x - x2) % _P:
        return None
    return _P - x if (x & 1) != sign else x


def _decode_pt(b):
    y = int.from_bytes(b, "little")
    x = _recover_x(y & ((1 << 255) - 1), y >> 255)
    return None if x is None else (x, y & ((1 << 255) - 1), 1, x * (y & ((1 << 255) - 1)) % _P)


def _encode_pt(a):
    x, y, z, _ = a
    zi = pow(z, _P - 2, _P)
    x, y = x * zi % _P, y * zi % _P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


_GY = 4 * pow(5, _P - 2, _P) % _P
_G = (_recover_x(_GY, 0), _GY, 1, _recover_x(_GY, 0) * _GY % _P)


def ed25519ph_verify(prehash, sig_hex, pub_hex):
    """True when sig_hex (128 hex) is a signature by pub_hex (64 hex) of the message whose SHA-512 is `prehash`."""
    try:
        sig, pub = bytes.fromhex((sig_hex or "").strip()), bytes.fromhex((pub_hex or "").strip())
    except ValueError:
        return False
    if len(sig) != 64 or len(pub) != 32 or len(prehash) != 64:
        return False
    a = _decode_pt(pub)
    s = int.from_bytes(sig[32:], "little")
    if a is None or s >= _L:
        return False
    h = int.from_bytes(hashlib.sha512(_DOM2_PH + sig[:32] + pub + prehash).digest(), "little") % _L
    ha = _pt_mul(h, a)
    return _encode_pt(_pt_add(_pt_mul(s, _G), ((-ha[0]) % _P, ha[1], ha[2], (-ha[3]) % _P))) == sig[:32]


def version_from_tag(tag):
    """The launcher's versionFromTag: the tag from its first digit on ('test-1.18.6-test.231' -> '1.18.6-test.231')."""
    m = re.search(r"\d", tag or "")
    return tag[m.start():] if m else ""


def version_cmp(a, b):
    """releases.cpp versionCmp: semver order, '1.17.0' > '1.17.0-test.3' > '1.17.0-test.2' > '1.16.9'; a missing part
    counts as 0; prerelease identifiers compare dot by dot (numbers numerically, numbers before words). <0, 0, >0."""
    def split(v):
        core, _, pre = (v or "").partition("-")
        nums = [int(x) if x.isdigit() else 0 for x in (core.split(".") + ["0", "0", "0"])[:3]]
        return nums, pre
    (na, pa), (nb, pb) = split(a), split(b)
    if na != nb:
        return -1 if na < nb else 1
    if not pa or not pb:
        return (1 if not pa else -1) if pa != pb else 0
    ia, ib = pa.split("."), pb.split(".")
    for x, y in zip(ia, ib):
        if x == y:
            continue
        if x.isdigit() and y.isdigit():
            return -1 if int(x) < int(y) else 1
        if x.isdigit() != y.isdigit():
            return -1 if x.isdigit() else 1
        return -1 if x < y else 1
    return (len(ia) > len(ib)) - (len(ia) < len(ib))


def parse_manifest(text):
    """quark-release.txt -> {channel, tag, version, commit, sha256: {name: hex}}, or None when it is not one
    (releases.cpp parseManifest)."""
    lines = (text or "").split("\n")
    if not lines or lines[0].rstrip("\r ") != "quark release":
        return None
    m = {"channel": "", "tag": "", "version": "", "commit": "", "sha256": {}}
    for line in lines[1:]:
        key, _, val = line.rstrip("\r ").partition(" ")
        if key in ("channel", "tag", "version", "commit"):
            m[key] = val
        elif key == "sha256" and " " in val:
            name, _, hx = val.partition(" ")
            m["sha256"].setdefault(name, hx.lower())
    return m


def check_manifest(m, rel, zip_sha256):
    """releases.cpp checkManifest for the Windows zip: None when the signed manifest vouches for it, else why not."""
    channel = "testing" if rel.get("prerelease") else "production"
    version = version_from_tag(rel["tag"])
    if m["tag"] != rel["tag"]:
        return "its signed manifest is for %s, not %s" % (m["tag"] or "no tag", rel["tag"])
    if m["version"] != version:
        return "its signed manifest is for version %s, not %s" % (m["version"], version)
    if m["channel"] != channel:
        return "its signed manifest is for the %s channel, not %s" % (m["channel"], channel)
    want = m["sha256"].get(ZIP_NAME)
    if want is None:
        return "its signed manifest doesn't name %s" % ZIP_NAME
    return None if want == zip_sha256.lower() else "the zip is not the one its signed manifest names"


def needs_signature(rel):
    """releases.cpp needsSignature: a signed release, or any version from 1.17.0 on (one without a .sig can't be
    genuine)."""
    return bool(rel.get("sig")) or version_cmp(version_from_tag(rel.get("tag") or ""), FIRST_SIGNED) >= 0


def _opener(url):
    host = urllib.parse.urlsplit(url).hostname or ""
    return util._LOCAL_OPENER if host in ("127.0.0.1", "localhost") else util._DEFAULT_OPENER


def extract_zip(zip_path, dest, check=None):
    """Unzip into dest (made here), dropping one top folder that holds everything (quark-windows/). Refuses names
    that leave dest and more than MAX_UNZIPPED bytes. `check()` runs before each file (it raises to stop: Cancel)."""
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
            if check:
                check()
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


def clean_field(v, label="A"):
    """A note box from the page -> its text with q's line breaks kept (trailing spaces and runs of blank lines
    dropped). Raises PlayError for something that is not text or is longer than MAX_FIELD: never cut silently."""
    if v is None:
        return ""
    if not isinstance(v, str):
        raise PlayError(400, "The %s box must be text." % label.lower())
    v = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", " ", v.replace("\r\n", "\n").replace("\r", "\n").replace("\t", " "))
    v = re.sub(r"\n{3,}", "\n\n", "\n".join(l.rstrip() for l in v.split("\n"))).strip()
    if len(v) > MAX_FIELD:
        raise PlayError(400, "The %s box is longer than %d characters (%d); shorten it." % (label.lower(), MAX_FIELD, len(v)))
    return v


def flat(s, limit=MAX_FIELD):
    """Text with line breaks -> one line for 12-play-notes.md (an entry is one line): the lines joined by " / "."""
    return one_line(" / ".join(l.strip() for l in str(s or "").split("\n") if l.strip()), limit)


def _sentence(s):
    return s if re.search(r"[.!?)`\"']$", s) else s + "."


def note_title(session):
    label = one_line(session.get("label") or session.get("build") or "game", 80).replace("**", "")
    v = one_line(session.get("version") or "", 60).replace("**", "")
    return "Play note: " + label + (" " + v if v and v not in label else "")


_SIGNALS = {4: "SIGILL", 6: "SIGABRT", 7: "SIGBUS", 8: "SIGFPE", 9: "SIGKILL", 11: "SIGSEGV", 15: "SIGTERM"}
_NTSTATUS = {0xC0000005: "access violation", 0xC0000409: "stack buffer overrun", 0xC00000FD: "stack overflow",
             0xC0000374: "heap corruption", 0xC000001D: "illegal instruction", 0xC0000094: "integer divide by zero",
             0xC0000135: "a DLL is missing", 0xC0000142: "a DLL failed to start", 0x40010004: "ended by Windows",
             0xC000013A: "closed with Ctrl+C"}


def exit_info(code, stopped=False):
    """How a game ended -> {kind: ok | stopped | crash | error | unknown, text}. A crash is a signal (POSIX:
    negative code) or a Windows exception status (0xC...); another non-zero code is an error exit."""
    if stopped:
        return {"kind": "stopped", "text": "stopped from the Play tab"}
    if code is None:
        return {"kind": "unknown", "text": "exit code unknown"}
    code = util.to_int(code, 0)
    if code == 0:
        return {"kind": "ok", "text": "closed normally"}
    if code < 0:
        sig = _SIGNALS.get(-code, "signal %d" % -code)
        return {"kind": "stopped" if -code in (9, 15) else "crash", "text": "exit code %d (%s)" % (code, sig)}
    if code >= 0xC0000000 or code in _NTSTATUS:
        name = _NTSTATUS.get(code)
        return {"kind": "crash", "text": "exit code 0x%08X%s" % (code, " (%s)" % name if name else "")}
    return {"kind": "error", "text": "exit code %d" % code}


def format_note(session, fields, when):
    """One 12-play-notes.md entry (a single line). `when` is the trailer stamp ('2026-10-24 14:05 CT')."""
    parts = []
    for key, label in FIELDS:
        v = flat(fields.get(key))
        if v:
            parts.append("%s: %s" % (label, _sentence(v)))
    if session.get("freeform"):
        meta = "Played outside the Supervisor Board"
    else:
        meta = "Played %s" % fmt_duration(session.get("durationSec"))
        if session.get("endApprox"):
            meta += " or less (it closed while the Supervisor Board was not running)"
    if session.get("commit"):
        meta += ", commit %s" % one_line(session["commit"], 12)
    if session.get("args"):
        meta += ", options `%s`" % one_line(session["args"], MAX_ARGS).replace("`", "'")
    code = session.get("exitCode")
    if session.get("stopped"):
        meta += ", stopped from the Play tab"
    elif code not in (None, 0):
        meta += ", " + exit_info(code)["text"]
    if session.get("logTail"):
        meta += ". Log's last lines: `%s`" % flat(session["logTail"], NOTE_LOG_CHARS).replace("`", "'")
    return "- **%s** %s %s. _%s, q, open_" % (session.get("noteTitle") or note_title(session), " ".join(parts), meta, when)


def log_tail(path, max_bytes=LOG_VIEW_BYTES):
    """The end of a log file -> (text, truncated). Raises OSError when it can't be read."""
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(max(0, size - max_bytes))
        data = f.read(max_bytes)
    if size > max_bytes:
        data = data.split(b"\n", 1)[-1]  # (from the first whole line)
    return data.decode("utf-8", "replace"), size > max_bytes


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


def change_entry(text, key, line):
    """12-play-notes.md's text with the entry `key` (title, stamp) replaced by `line` (None: removed, with the blank
    line before it) while its status is still `open` -> (text, what): "changed", "removed", "same", "gone" (not in
    the file: an agent moved or archived it) or the entry's status when an agent has taken it (left as it is)."""
    rows = text.split("\n")
    for i, l in enumerate(rows):
        m = _DOC_ENTRY.match(l)
        if not m or (m.group(1).strip(), m.group(2).strip()) != key:
            continue
        status = m.group(4).strip().lower()
        if status != "open":
            return text, status
        if line is None:
            del rows[i]
            if 0 < i < len(rows) and not rows[i - 1].strip() and not rows[i].strip():
                del rows[i - 1]
            elif i == len(rows) and i > 0 and not rows[i - 1].strip():
                del rows[i - 1]
            return "\n".join(rows), "removed"
        if l == line:
            return text, "same"
        rows[i] = line
        return "\n".join(rows), "changed"
    return text, "gone"


def blocked_why(status):
    """Why q's edit or removal stayed out of 12-play-notes.md, from what sync_docs recorded."""
    if status == "archived":
        return "an agent moved or archived it"
    return "an agent set it to %s" % status


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


ACTIVE = ("downloading", "verifying", "extracting", "copying")   # (copying: into a game folder, past cancelling)
CHANNEL_FOLDERS = ("production", "testing")   # (the local folders a release of that channel can update)


class Play:
    def __init__(self, paths, releases_url=RELEASES_URL, spawner=spawn, clock=time.time, docs_dir=None, git="git",
                 opener=open_with_system, killer=terminate, public_key=RELEASE_PUBLIC_KEY):
        self.paths = paths
        self.releases_url = releases_url
        self.spawner = spawner
        self.opener = opener
        self.killer = killer
        self.public_key = public_key
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
                    "forcedAt": -1e9, "fetchedTs": 0.0, "complete": None}
        self._load_releases()
        self.installs = {}
        self.sync = self._load_json(SYNC_FILE, {})
        if not isinstance(self.sync, dict):
            self.sync = {}
        self.sync.setdefault("written", [])
        self._doc_cache = (None, [])
        self._notes_cache = (None, {})
        self._maps_cache = {}   # (maps folder -> (its listing's key, {sig, maps, modes}))

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

    def _load_releases(self):
        """The last release list (play-releases.json): the page has it at once after a restart (offline too), and the
        next fetch asks GitHub with its ETag (a 304 costs nothing against the 60 an hour)."""
        d = self._load_json(RELEASES_FILE, None)
        if not isinstance(d, dict) or not isinstance(d.get("list"), list):
            return
        lst = [dict(x, needsSig=needs_signature(x)) for x in d["list"] if valid_release(x)]
        ts = util.to_float(d.get("fetchedTs")) or 0.0
        if not lst or ts > self.clock() + 60:
            return  # (nothing usable, or a time from the future: fetched again)
        etag = d.get("etag") if isinstance(d.get("etag"), str) and len(d["etag"]) < 200 else None
        self.rel.update(list=lst, ok=True, fetchedTs=ts, fetchedAt=util.iso(ts), complete=d.get("complete") is True,
                        etag=etag)

    def _save_releases(self):
        with self.lock:
            r = self.rel
            data = {"list": r["list"], "etag": r["etag"], "fetchedTs": r["fetchedTs"], "complete": r["complete"]}
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self.paths.pm(RELEASES_FILE), data)
        except OSError as e:
            util.log("play: release list not saved (%s)" % type(e).__name__)

    def _save_sync(self):
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self.paths.pm(SYNC_FILE), self.sync)
        except OSError:
            pass

    def _note_records(self):
        """play-notes.jsonl -> {id: note}, oldest first, each with its edits applied (newest wins); a removed note
        has deleted True; rev counts its edits and removals. Cached by the file's size + mtime (read every poll)."""
        path = self.paths.pm(NOTES_FILE)
        try:
            st = os.stat(path)
            key = (st.st_size, st.st_mtime_ns)
        except OSError:
            return {}
        if self._notes_cache[0] == key:
            return self._notes_cache[1]
        out = {}
        try:
            with open(path, "rb") as f:
                for line in f:
                    try:
                        n = json.loads(line.decode("utf-8"))
                    except (ValueError, UnicodeDecodeError):
                        continue
                    if not isinstance(n, dict) or not n.get("id"):
                        continue
                    op, cur = n.get("op"), out.get(n["id"])
                    if op in ("edit", "delete"):
                        if cur is None:
                            continue
                        cur = dict(cur, rev=cur.get("rev", 0) + 1)
                        if op == "delete":
                            cur.update(deleted=True, deletedAt=n.get("at"))
                        elif isinstance(n.get("fields"), dict) and n.get("line"):
                            cur.update(fields=n["fields"], line=n["line"], editedAt=n.get("at"))
                        out[n["id"]] = cur
                    elif n.get("line") and cur is None:
                        out[n["id"]] = n
        except OSError:
            return {}
        self._notes_cache = (key, out)
        return out

    def notes(self, deleted=False):
        """Every note saved on this PC (play-notes.jsonl, edits applied), oldest first; removed ones only with
        deleted=True."""
        return [n for n in self._note_records().values() if deleted or not n.get("deleted")]

    def _append_note(self, rec):
        try:
            self.paths.ensure_pm_dir()
            with open(self.paths.pm(NOTES_FILE), "ab") as f:
                f.write((json.dumps(rec, ensure_ascii=False) + "\n").encode("utf-8"))
        except OSError as e:
            raise PlayError(500, "Could not save the note on this PC (%s)." % (e.strerror or type(e).__name__))

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
        counts = {}   # (games played and crashed per build, over the kept sessions: shown on its card)
        with self.lock:
            for s in self.sessions:
                if s.get("state") != "running":
                    c = counts.setdefault(s.get("build"), [0, 0])
                    c[0] += 1
                    c[1] += exit_info(s.get("exitCode"), s.get("stopped"))["kind"] == "crash"
        for name in LOCAL_FOLDERS:
            out.append(self._build(builds.read_folder(ctx, name), name, name, "local", counts))
        vdir = self.paths.q(VERSIONS)
        try:
            tags = [t for t in os.listdir(vdir) if TAG_RE.match(t) and os.path.isdir(os.path.join(vdir, t))]
        except OSError:
            tags = []
        tags.sort(key=lambda t: (util.version_key(t), t), reverse=True)
        for t in tags:
            out.append(self._build(builds.read_folder(ctx, os.path.join(VERSIONS, t)), VERSIONS + "/" + t, t, "version",
                                   counts))
        return out

    def _maps(self, folder):
        """read_maps of a build folder, cached by the maps folder's listing (names, sizes, times: one scandir a poll,
        which on Windows needs no stat per file) -> {sig, maps, modes}, or None when it has no maps folder."""
        d = os.path.join(folder, "maps")
        try:
            with os.scandir(d) as it:
                key = tuple(sorted((x.name, x.stat().st_size, x.stat().st_mtime_ns) for x in it if MAP_RE.match(x.name)))
        except OSError:
            return None
        if not key:
            return None
        hit = self._maps_cache.get(d)
        if hit and hit[0] == key:
            return hit[1]
        maps = read_maps(folder)
        val = {"maps": maps, "modes": map_modes(maps),
               "sig": hashlib.sha1(json.dumps(maps, sort_keys=True).encode("utf-8")).hexdigest()[:12]}
        self._maps_cache[d] = (key, val)
        return val

    def build_maps(self, bid):
        """The maps and modes of a build for the page's pickers (GET /api/play/maps?build=<id>; the poll only
        carries their sig, so the lists are fetched once per set of maps)."""
        b = self.find_build(str(bid or ""))
        if not b:
            raise PlayError(404, "That build is not on this PC.")
        m = self._maps(b["folder"]) if b.get("present") else None
        if not m:
            m = {"sig": None, "maps": [], "modes": list(GAME_MODES)}
        return dict(m, build=b["id"], titles=dict(zip(GAME_MODES, GAME_MODE_TITLES)))

    def _build(self, e, bid, label, kind, counts):
        exe = find_exe(e["path"]) if e.get("present") else None
        with self.lock:
            running = [s["id"] for s in self.sessions if s.get("state") == "running" and s.get("build") == bid]
        b = {"id": bid, "label": label, "kind": kind, "folder": e["path"], "present": e.get("present"),
             "version": e.get("version"), "tag": e.get("tag"), "commit": e.get("commit"), "dirty": e.get("dirty"),
             "mtime": e.get("mtime"), "lines": e.get("lines") or [], "note": e.get("note"),
             "fromRelease": e.get("kind") == "release",   # (BUILD.txt written when a release was put there)
             "exe": os.path.basename(exe) if exe else None, "playable": bool(exe), "running": running,
             "missing": None if exe else ("folder missing" if not e.get("present") else "no quark.exe in it")}
        m = self._maps(e["path"]) if exe else None
        b["maps"] = m["sig"] if m else None
        b["plays"], b["crashes"] = counts.get(bid, (0, 0))
        if e.get("present") and (kind == "version" or b["fromRelease"]):
            # (how Install checked it: written next to BUILD.txt; none for a folder from before this was recorded. In
            # a game folder only when it is about the release BUILD.txt names: fetch_release.ps1 leaves an old one)
            try:
                info = util.read_json_file(os.path.join(e["path"], INSTALL_INFO))
            except (OSError, ValueError):
                info = None
            if kind == "local" and not (isinstance(info, dict) and e.get("tag") and info.get("tag") == e.get("tag")):
                info = None
            if isinstance(info, dict) and info.get("check") in ("signed", "sha256", "none"):
                b["check"], b["checkNote"] = info["check"], one_line(info.get("note") or "", 300)
            if isinstance(info, dict) and isinstance(info.get("installedAt"), str):
                b["installedAt"] = info["installedAt"]
        return b

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
        with self.lock:
            if any(i.get("into") == b["id"] and i.get("state") in ACTIVE for i in self.installs.values()):
                raise PlayError(409, "The %s folder is being updated; play it when that is done." % b["id"])
        if not isinstance(args_text, str):
            raise PlayError(400, "The launch options must be text.")
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
            s.pop("stopping", None)
            if approx:
                s["endApprox"] = True
        self._save_sessions()

    def _session(self, sid):
        for s in self.sessions:
            if s.get("id") == sid:
                return s
        return None

    # -- stopping a game, opening folders, the game's log
    def stop_game(self, sid):
        """Stop a running game: ask it to close, end it after STOP_GRACE s. Its note says it was stopped (not a
        crash)."""
        with self.lock:
            s = self._session(str(sid or ""))
            if not s:
                raise PlayError(404, "No such game session.")
            proc = self.procs.get(s["id"])
            if s.get("state") != "running" or proc is None:
                raise PlayError(409, "That game is not running.", "not-running")
            if s.get("stopping"):
                return dict(s, noop=True)
            s.update(stopping=True, stopped=True)
        self._save_sessions()
        threading.Thread(target=self._stop, args=(s["id"], proc), name="pm-play-stop", daemon=True).start()
        return dict(s)

    def _stop(self, sid, proc):
        for force in (False, True):
            try:
                self.killer(proc, force)
            except Exception as e:  # (taskkill missing, a pid that went away: the next step or the wait decides)
                util.log("play: stopping the game failed (%s)" % type(e).__name__)
            end = time.monotonic() + STOP_GRACE
            while time.monotonic() < end and not self.stop.is_set():
                with self.lock:
                    if sid not in self.procs:
                        return
                self.stop.wait(0.1)
        with self.lock:  # (still there after being ended: say so instead of claiming it stopped)
            s = self._session(sid)
            if s and s.get("state") == "running":
                s.pop("stopping", None)
                s.pop("stopped", None)
        self._save_sessions()

    def _log_path(self, s):
        """A session's log file, only when it is one of ours in project-manager\\logs."""
        p = s.get("log") if s else None
        if not isinstance(p, str) or not re.match(r"^play-p\d{14}-[0-9a-f]{4}\.log$", os.path.basename(p)):
            return None
        logs = os.path.realpath(self.paths.pm("logs"))
        real = os.path.realpath(p)
        return real if os.path.dirname(real) == logs else None

    def game_log(self, sid):
        with self.lock:
            s = self._session(str(sid or ""))
            s = dict(s) if s else None
        if not s:
            raise PlayError(404, "No such game session.")
        path = self._log_path(s)
        if not path or not os.path.isfile(path):
            raise PlayError(404, "That game's log is gone (only the last %d are kept)." % KEEP_LOGS, "no-log")
        try:
            text, cut = log_tail(path)
            size = os.path.getsize(path)
        except OSError as e:
            raise PlayError(500, "Can't read the log (%s)." % (e.strerror or type(e).__name__))
        return {"session": s["id"], "label": s.get("label"), "version": s.get("version"), "path": path, "size": size,
                "truncated": cut, "text": text, "state": s.get("state"),
                "exit": None if s.get("state") == "running" else exit_info(s.get("exitCode"), s.get("stopped"))}

    def _log_lines(self, s):
        """The last lines of a session's log for a note (NOTE_LOG_LINES, at most NOTE_LOG_CHARS), or None."""
        path = self._log_path(s)
        if not path:
            return None
        try:
            text, _ = log_tail(path, 64 * 1024)
        except OSError:
            return None
        lines = [one_line(l, 300) for l in text.split("\n") if l.strip()][-NOTE_LOG_LINES:]
        tail = "\n".join(lines)
        return tail[-NOTE_LOG_CHARS:] or None

    def open_path(self, what, ref=None):
        """Open a build's folder, the logs folder, a game's log, the Claude Documents folder or 12-play-notes.md on
        this PC (Explorer / the default app). Only those: never a path the page sends."""
        what = str(what or "")
        if what == "build":
            b = self.find_build(str(ref or ""))
            if not b:
                raise PlayError(404, "That build is not on this PC.")
            path = b["folder"]
        elif what == "logs":
            path = self.paths.pm("logs")
            try:
                os.makedirs(path, exist_ok=True)
            except OSError:
                pass
        elif what == "log":
            with self.lock:
                path = self._log_path(self._session(str(ref or "")))
        elif what == "docs":
            path = self.docs_dir
        elif what == "notes-file":
            path = os.path.join(self.docs_dir, DOCS_FILE)
        else:
            raise PlayError(400, "Nothing to open.")
        if not path or not os.path.exists(path):
            raise PlayError(404, "%s is not on this PC." % (path or "That file"))
        self.opener(path)
        return {"opened": path}

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

    def _fields(self, fields):
        if not isinstance(fields, dict):
            raise PlayError(400, "The page sent a note without its fields.")
        clean = {k: clean_field(fields.get(k), label) for k, label in FIELDS}
        if not any(clean.values()):
            raise PlayError(400, "Write something in at least one box, or press Skip.")
        return clean

    def _freeform(self, fields, now):
        """What a note without a session is about: a listed build (`build`), or `label` + `version` as q typed them
        (a game played through the launcher)."""
        for k in ("build", "label", "version"):
            if fields.get(k) is not None and not isinstance(fields.get(k), str):
                raise PlayError(400, "The note's %s must be text." % k)
        b = self.find_build(fields["build"]) if fields.get("build") else None
        if fields.get("build") and not b:
            raise PlayError(404, "That build is not on this PC.")
        nid = "n%s-%s" % (datetime.datetime.fromtimestamp(now, datetime.timezone.utc).strftime("%Y%m%d%H%M%S"),
                          os.urandom(2).hex())
        if b:
            return {"id": nid, "build": b["id"], "label": b["label"], "version": b["version"], "commit": b["commit"],
                    "freeform": True}
        label = one_line(fields.get("label") or "launcher", 80)
        return {"id": nid, "build": None, "label": label, "version": one_line(fields.get("version") or "", 60) or None,
                "commit": None, "freeform": True}

    def save_note(self, sid, fields):
        """The note for a game session (sid), or without one (sid empty: see _freeform). `attachLog` true adds the
        last lines of the game's log."""
        clean = self._fields(fields)
        now = self.clock()
        when = stamp(now)
        src = None if sid else self._freeform(fields, now)  # (find_build reads folders: outside the lock)
        with self.lock:
            s = None
            if sid:
                s = self._session(str(sid))
                if not s:
                    raise PlayError(404, "No such game session.")
                if s.get("state") == "noted":  # (saved from another page already: never a second copy)
                    raise PlayError(409, "That game's note was already saved.", "already-noted")
                if s.get("state") not in ("needsNote", "skipped"):
                    raise PlayError(409, "That game is still running.", "running")
                src = s
            base = title = note_title(src)
            taken = {(n.get("title"), n.get("stamp")) for n in self.notes(deleted=True)}
            k = 2
            while (title, when) in taken:  # (two notes for one build in the same minute)
                title = "%s #%d" % (base, k)
                k += 1
            tail = self._log_lines(src) if s is not None and fields.get("attachLog") is True else None
            line = format_note(dict(src, noteTitle=title, logTail=tail), clean, when)
            note = {"id": src["id"], "session": s["id"] if s else None, "savedAt": util.iso(now), "stamp": when,
                    "title": title, "line": line, "fields": clean, "build": src.get("build"), "label": src.get("label"),
                    "version": src.get("version"), "commit": src.get("commit"), "args": src.get("args"),
                    "durationSec": src.get("durationSec"), "startedAt": src.get("startedAt"),
                    "endedAt": src.get("endedAt"), "exitCode": src.get("exitCode")}
            for k in ("endApprox", "stopped", "freeform"):
                if src.get(k):
                    note[k] = True
            if tail:
                note["logTail"] = tail
            self._append_note(note)
            if s is not None:
                s.update(state="noted", notedAt=note["savedAt"], noteTitle=title)
        if s is not None:
            self._save_sessions()
        self.wake.set()  # (written to Claude Documents and pushed in the background)
        return note

    def _locked_why(self, n):
        """Why a saved note can't be edited or removed any more (None: it can): an agent has taken its entry, or it
        is no longer in 12-play-notes.md."""
        if n["id"] not in set(self.sync.get("written") or []) or not os.path.isdir(self.docs_dir):
            return None
        status = {(e["title"], e["stamp"]): e["status"] for e in self.doc_entries()}.get((n.get("title"), n.get("stamp")))
        if status is None:
            return "That note is no longer in %s (an agent moved or archived it); write a new note instead." % DOCS_FILE
        if status != "open":
            return "An agent already set that note to %s; write a new note instead." % status
        return None

    def _editable_note(self, nid):
        n = self._note_records().get(str(nid or ""))
        if not n or n.get("deleted"):
            raise PlayError(404, "No such play note.")
        why = self._locked_why(n)
        if why:
            raise PlayError(409, why, "locked")
        return n

    def edit_note(self, nid, fields):
        """Change a saved note's boxes. Its entry in 12-play-notes.md is changed in place while it is still `open`."""
        clean = self._fields(fields)
        with self.lock:
            n = self._editable_note(nid)
            line = format_note(dict(n, noteTitle=n["title"]), clean, n["stamp"])
            if line == n["line"]:
                return dict(n, noop=True)
            self._append_note({"id": n["id"], "op": "edit", "at": util.iso(self.clock()), "fields": clean, "line": line})
        self.wake.set()
        return dict(n, fields=clean, line=line)

    def delete_note(self, nid):
        """Remove a saved note; its entry leaves 12-play-notes.md while it is still `open`. The page keeps showing it
        ("removing") until the file has it out, or says it was not removed when an agent took the entry first."""
        with self.lock:
            n = self._editable_note(nid)
            self._append_note({"id": n["id"], "op": "delete", "at": util.iso(self.clock())})
        self.wake.set()
        return {"removed": n["id"], "title": n.get("title")}

    def dismiss_blocked(self, nid):
        """q has seen that an edit or removal of this note didn't reach 12-play-notes.md (an agent took the entry
        first): it leaves the page's list and is not tried again."""
        n = self._note_records().get(str(nid or ""))
        with self.lock:
            if not n or n["id"] not in (self.sync.get("blocked") or {}):
                raise PlayError(404, "That change is not waiting any more.", "not-blocked")
            self.sync.setdefault("dismissed", {})[n["id"]] = n.get("rev", 0)
            self.sync["blocked"] = {k: v for k, v in self.sync["blocked"].items() if k != n["id"]}
            self._save_sync()
        return {"dismissed": n["id"]}

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
        """Write the notes not yet in 12-play-notes.md (and q's edits / removals of open ones), commit that file,
        push. Returns the sync state."""
        with self.docs_lock:
            st = self._sync_once()
            st.setdefault("noUpstream", False)
            st["at"] = util.now_iso()
            self.sync.update(st)
            self._save_sync()
            return dict(self.sync)

    def _sync_once(self):
        records = self.notes(deleted=True)
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
        applied = dict(self.sync.get("applied") or {})   # (id -> the rev of q's edits already put in the file)
        blocked = dict(self.sync.get("blocked") or {})   # (id -> the status that kept an edit out of the file)
        if conflict:
            again = self._drop_own_commits()
            if again is not None:
                written -= {n["id"] for n in records if (n.get("title"), n.get("stamp")) in again}
                applied = {}  # (an edit commit may be gone too: check every edited note against the file again)
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
        todo = [n for n in records if not n.get("deleted") and n["id"] not in written
                and (n.get("title"), n.get("stamp")) not in present]
        for n in records:  # (already in the file: an earlier write whose bookkeeping did not finish; or removed)
            if n["id"] not in written and ((n.get("title"), n.get("stamp")) in present or n.get("deleted")):
                written.add(n["id"])
        # q's edits and removals of notes already in the file: only while the entry is still `open`
        # (a change an agent blocked is tried again on every sync: it goes in if the entry is set back to `open`,
        # until q dismisses it on the page)
        dismissed = self.sync.get("dismissed") or {}
        changes, done = [], {}
        for n in records:
            rev = n.get("rev", 0)
            if (not rev or n["id"] not in written or text is None or dismissed.get(n["id"]) == rev
                    or (rev <= applied.get(n["id"], 0) and n["id"] not in blocked)):
                continue
            text, what = change_entry(text, (n.get("title"), n.get("stamp")), None if n.get("deleted") else n["line"])
            if what in ("changed", "removed", "same"):
                if what != "same":
                    changes.append("%s (%s)" % (n["title"], "edited" if what == "changed" else "removed"))
                blocked.pop(n["id"], None)
            elif what == "gone" and n.get("deleted"):
                blocked.pop(n["id"], None)  # (an agent moved or archived it: out of the file, as q wanted)
            elif what == "gone":
                blocked[n["id"]] = "archived"
            else:
                blocked[n["id"]] = what
            done[n["id"]] = rev
        with self.lock:  # (dismissed on the page while this sync ran: stays dismissed)
            dismissed = self.sync.get("dismissed") or {}
            blocked = {k: v for k, v in blocked.items() if dismissed.get(k) != next(
                (n.get("rev", 0) for n in records if n["id"] == k), None)}
        if todo or changes:
            new = add_note_lines(text, [n["line"] for n in reversed(todo)]) if todo else text  # (newest first)
            data = (new.replace("\n", "\r\n") if crlf else new).encode("utf-8")
            try:
                with open(path, "wb") as f:
                    f.write((b"\xef\xbb\xbf" if bom else b"") + data)
            except OSError as e:
                return {"ok": False, "pushed": False, "error": "Can't write %s (%s)." % (DOCS_FILE, type(e).__name__)}
        result = {"ok": True, "error": None}
        code, _, err = self._git("add", "--", DOCS_FILE)
        if code == 0 and self._git("diff", "--cached", "--quiet", "--", DOCS_FILE)[0] != 0:
            items = [n["title"] for n in todo] + changes
            msg = items[0] if len(items) == 1 else "Play notes: %d" % max(len(items), 1)
            code, _, err = self._git(*(self._identity() + ["commit", "-q", "-m", msg + VIA, "--", DOCS_FILE]))
        if code != 0:
            self.sync["written"] = sorted(written)
            return {"ok": False, "pushed": False, "error": "Wrote the note into the file on this PC, but git couldn't "
                    "commit it: " + self._err(err, "commit failed")}
        written.update(n["id"] for n in todo)
        applied.update(done)
        applied.update({n["id"]: n.get("rev", 0) for n in todo if n.get("rev")})
        self.sync.update(written=sorted(written), applied=applied, blocked=blocked)
        code, out, _ = self._git("rev-list", "--count", "@{u}..HEAD")
        if code != 0:
            # no upstream branch (unset, or its remote branch is gone): nothing goes to GitHub, whatever is committed
            # here; count what no remote has, never read the failed count as "0 to push"
            unpushed = util.to_int(self._git("rev-list", "--count", "HEAD", "--not", "--remotes")[1], 0) or 0
            result.update(ok=False, pushed=False, unpushed=unpushed, noUpstream=True,
                          error="Saved on this PC, not going to GitHub: the Claude Documents folder has no upstream "
                                "branch (in that folder: git branch -u origin/main).")
            return result
        unpushed = util.to_int(out, 0) or 0
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
        """A note not yet written into Claude Documents, an edit or removal not yet put in the file, or a commit not
        yet pushed."""
        written = set(self.sync.get("written") or [])
        applied = self.sync.get("applied") or {}
        return bool(self.sync.get("unpushed") or self.sync.get("noUpstream")) or any(
            n["id"] not in written or n.get("rev", 0) > applied.get(n["id"], 0) for n in self.notes(deleted=True))

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
            if force and now - r["forcedAt"] >= RELEASES_MIN_FORCE and not r["busy"]:
                r["forcedAt"] = now
                stale = True
            if stale and not r["busy"]:
                r["busy"] = True
                threading.Thread(target=self._fetch_releases, name="pm-play-releases", daemon=True).start()
            out = {k: r[k] for k in ("ok", "fetchedAt", "error", "busy", "list", "complete")}
            # (seconds until a Refresh press is taken: the page counts down instead of ignoring a press)
            out["refreshIn"] = max(0, int(round(RELEASES_MIN_FORCE - (now - r["forcedAt"]))))
            return out

    def _get_page(self, n, etag=None):
        """One page of the release list -> (parsed JSON, ETag), or (None, None) for a 304 (nothing changed)."""
        url = page_url(self.releases_url, n)
        hdr = {"User-Agent": util.USER_AGENT, "Accept": "application/vnd.github+json"}
        if etag:
            hdr["If-None-Match"] = etag
        req = urllib.request.Request(url, headers=hdr)
        try:
            with _opener(url).open(req, timeout=15) as resp:
                body, tag = resp.read(8 << 20), resp.headers.get("ETag")
            return json.loads(body.decode("utf-8-sig")), tag
        except urllib.error.HTTPError as e:
            if e.code == 304:
                return None, None
            if e.code in (403, 429) and (e.headers.get("X-RateLimit-Remaining") == "0" or e.code == 429):
                reset = util.to_int(e.headers.get("X-RateLimit-Reset"))
                at = (" until " + dallas(reset).strftime("%H:%M") + " Dallas") if reset else ""
                raise util.SourceError("GitHub's limit for requests without signing in is used up%s" % at)
            raise util.SourceError("GitHub answered HTTP %d" % e.code)

    def _fetch_releases(self):
        """Every page of the list, like the launcher (releases.cpp fetchAll): until a short page, or one that only
        repeats releases already read. Page 1 answering 304 to its ETag means nothing new: the list stays. A later
        page that fails keeps what was read plus the older releases of the last list (`complete` False)."""
        r = self.rel
        try:
            with self.lock:
                etag = r["etag"] if r["list"] and r["complete"] else None
            raw, seen, complete, first_etag, note = [], set(), False, None, None
            for n in range(1, RELEASES_MAX_PAGES + 1):
                try:
                    data, tag = self._get_page(n, etag if n == 1 else None)
                except (util.SourceError, urllib.error.URLError, OSError, ValueError, UnicodeDecodeError) as e:
                    if n == 1:
                        raise
                    note = "only the newest %d releases could be read (%s)" % (
                        len(raw), str(getattr(e, "reason", e))[:120])
                    break
                if n == 1:
                    if data is None:  # (304: nothing published since)
                        with self.lock:
                            r.update(ok=True, error=None)
                        return
                    first_etag = tag
                if not isinstance(data, list):
                    if n == 1:
                        raise util.SourceError("GitHub sent something that is not a release list")
                    break
                tags = {x.get("tag_name") for x in data if isinstance(x, dict)}
                if data and tags <= seen:
                    complete = True  # (the same releases again: a server that ignores ?page=)
                    break
                seen |= tags
                raw += data
                if len(data) < RELEASES_PER_PAGE:
                    complete = True
                    break
            lst = parse_releases(raw)
            if not complete:
                have = {x["tag"] for x in lst}
                with self.lock:
                    old = [x for x in r["list"] if x["tag"] not in have]
                if old:
                    lst = sorted(lst + old, key=lambda x: (util.parse_time(x["publishedAt"]) or 0,
                                                           util.version_key(x["tag"])), reverse=True)
            with self.lock:
                r.update(list=lst, etag=first_etag if complete else None, ok=True, complete=complete, error=note)
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
                keep = bool(r["ok"] and r["list"])
            if keep:
                self._save_releases()  # (a 304 too: its new time keeps a restart within 10 min from asking again)

    # -- installing older versions
    def install(self, tag, into=None):
        """Install a release into versions\\<tag>, or (into: production / testing) copy it over that local game folder
        the way tools/fetch_release.ps1 does (merged in, never emptied first; BUILD.txt rewritten), only with a
        release of that folder's channel. Checked the same way either way."""
        tag = str(tag or "")
        into = str(into) if into else None
        with self.lock:
            rel = next((x for x in self.rel["list"] if x["tag"] == tag), None)
            if not rel:
                raise PlayError(404, "That release is not in the list (refresh it).")
            if into is not None:
                if into not in CHANNEL_FOLDERS:
                    raise PlayError(400, "Only the production or testing folder can be updated from a release.")
                if rel["channel"] != into:
                    raise PlayError(409, "%s is not a %s release, so it doesn't go into the %s folder." % (tag, into, into))
                if not os.path.isdir(self.paths.q(into)):
                    raise PlayError(404, "There is no Quark\\%s folder on this PC." % into)
                if self._build_running(into):
                    raise PlayError(409, "A game is running from the %s folder; close it first." % into)
            if not rel["zip"]:
                raise PlayError(409, "%s has no %s." % (tag, ZIP_NAME))
            if rel["needsSig"] and not (rel.get("sig") and rel.get("manifest") and rel.get("manifestSig")):
                raise PlayError(409, "%s claims a version from 1.17.0 on but has no signature or signed manifest; it "
                                "can't be a genuine release, so it is not installed." % tag, "unsigned")
            if any(i.get("state") in ACTIVE for i in self.installs.values()):
                raise PlayError(409, "Another version is being installed; wait for it.")
            if self._in_use(tag):
                raise PlayError(409, "%s is running; close the game first." % tag)
            self.installs[tag] = {"state": "downloading", "got": 0, "total": rel["zip"]["size"] or 0,
                                  "startedAt": util.now_iso(), "error": None, "into": into}
        threading.Thread(target=self._install, args=(rel, into), name="pm-play-install", daemon=True).start()
        return dict(self.installs[tag])

    def cancel_install(self, tag):
        tag = str(tag or "")
        with self.lock:
            i = self.installs.get(tag)
            if not i or i.get("state") not in ACTIVE:
                raise PlayError(409, "%s is not being installed." % (tag or "That version"), "not-installing")
            if i.get("state") == "copying":
                raise PlayError(409, "Too late to cancel: its files are being copied into the %s folder." % i.get("into"),
                                "copying")
            i["cancel"] = True
        return {"cancelling": tag}

    def _in_use(self, tag):
        return self._build_running(VERSIONS + "/" + tag)

    def _build_running(self, bid):
        return any(s.get("state") == "running" and s.get("build") == bid for s in self.sessions)

    def _set_install(self, tag, **kw):
        with self.lock:
            self.installs[tag].update(kw)

    def _check_cancel(self, tag):
        if self.stop.is_set():
            raise PlayError(500, "The server stopped during the install.")
        with self.lock:
            if self.installs[tag].get("cancel"):
                raise PlayError(499, "Cancelled.", "cancelled")

    def _fetch_small(self, url, limit=1 << 20):
        req = urllib.request.Request(url, headers={"User-Agent": util.USER_AGENT})
        with _opener(url).open(req, timeout=30) as resp:
            return resp.read(limit).decode("utf-8", "replace")

    def _verify(self, rel, sha256_hex, sha512):
        """The launcher's checks for this zip -> (check, note): "signed" (its signature and the signed manifest
        verify and name it; the manifest's sha256 makes SHA256SUMS redundant), "sha256" (unsigned, from before
        signing, matches SHA256SUMS) or "none" (unsigned, no SHA256SUMS line). Raises PlayError when a check fails:
        nothing is installed."""
        if rel["needsSig"]:
            sig = self._fetch_small(rel["sig"], 4096)
            if not ed25519ph_verify(sha512, sig, self.public_key):
                raise PlayError(500, "The zip's signature does not verify against the release key; not installed.")
            text = self._fetch_small(rel["manifest"], 256 * 1024)
            msig = self._fetch_small(rel["manifestSig"], 4096)
            if not ed25519ph_verify(hashlib.sha512(text.encode("utf-8")).digest(), msig, self.public_key):
                raise PlayError(500, "Its release manifest's signature does not verify; not installed.")
            m = parse_manifest(text)
            why = "its release manifest is unreadable" if m is None else check_manifest(m, rel, sha256_hex)
            if why:
                raise PlayError(500, "Not installed: %s." % why)
            return "signed", "signature and signed release manifest verified (release key)"
        # (from before signing: SHA256SUMS, fetched over the same HTTPS as the zip, is all there is)
        want = parse_sums(self._fetch_small(rel["sums"])) if rel.get("sums") else None
        if want and sha256_hex != want:
            raise PlayError(500, "The download does not match the release's SHA256SUMS; not installed.")
        # (a tag like build-1 names no version: unsigned, but not known to be from before signing)
        why = "from before signing (1.16.x or older)" if util.parse_version(rel["tag"]) else "unsigned, and its tag names no version"
        if want:
            return "sha256", why + ": matches its SHA256SUMS, no signature to check"
        return "none", why + ": no signature and no SHA256SUMS to check it against"

    def _install(self, rel, into=None):
        tag = rel["tag"]
        vdir = self.paths.q(VERSIONS)
        tmp = os.path.join(vdir, ".download-%s.zip" % tag)
        partial = os.path.join(vdir, ".partial-%s" % tag)
        try:
            os.makedirs(vdir, exist_ok=True)
            h, h512 = hashlib.sha256(), hashlib.sha512()  # (sha512: the signature is over the zip's SHA-512)
            req = urllib.request.Request(rel["zip"]["url"], headers={"User-Agent": util.USER_AGENT,
                                                                     "Accept": "application/octet-stream"})
            got = 0
            with _opener(rel["zip"]["url"]).open(req, timeout=30) as resp, open(tmp, "wb") as f:
                total = util.to_int(resp.headers.get("Content-Length"), 0) or rel["zip"]["size"] or 0
                self._set_install(tag, total=total)
                while True:
                    self._check_cancel(tag)
                    chunk = resp.read(256 * 1024)
                    if not chunk:
                        break
                    got += len(chunk)
                    if got > MAX_ZIP:
                        raise PlayError(500, "The download is bigger than 1 GB; stopped.")
                    h.update(chunk)
                    h512.update(chunk)
                    f.write(chunk)
                    self._set_install(tag, got=got)
            self._set_install(tag, state="verifying")
            self._check_cancel(tag)
            check, note = self._verify(rel, h.hexdigest(), h512.digest())
            self._set_install(tag, state="extracting")
            _rmtree(partial)
            extract_zip(tmp, partial, check=lambda: self._check_cancel(tag))
            if not find_exe(partial):
                raise PlayError(500, "The zip has no quark.exe; not installed.")
            with open(os.path.join(partial, "BUILD.txt"), "w", encoding="utf-8") as f:
                f.write("%s (GitHub release, published %s)\n" % (tag, rel.get("publishedAt") or "unknown"))
            util.write_json_atomic(os.path.join(partial, INSTALL_INFO), {
                "tag": tag, "publishedAt": rel.get("publishedAt"), "installedAt": util.now_iso(), "check": check,
                "note": note, "sha256": h.hexdigest()})
            if into:
                self._copy_into(tag, partial, into)
            else:
                dest = os.path.join(vdir, tag)
                if self._in_use(tag):
                    raise PlayError(409, "%s started while installing; close it and install again." % tag)
                self._check_cancel(tag)
                _rmtree(dest)
                os.replace(partial, dest)
            self._set_install(tag, state="done", verified=check != "none", check=check, doneAt=util.now_iso(),
                              note=note)
        except PlayError as e:
            self._set_install(tag, state="cancelled" if e.reason == "cancelled" else "failed",
                              error=None if e.reason == "cancelled" else e.message)
        except (urllib.error.URLError, OSError, ValueError, zipfile.BadZipFile) as e:
            self._set_install(tag, state="failed", error="%s (%s)" % (type(e).__name__, str(getattr(e, "reason", e))[:160]))
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass
            _rmtree(partial)

    def _copy_into(self, tag, partial, into):
        """The checked, unpacked release over Quark\\<into> (fetch_release.ps1's robocopy /E: files replaced or added,
        none deleted; settings and q's own maps live in %APPDATA%\\quark anyway)."""
        self._check_cancel(tag)
        if self._build_running(into):
            raise PlayError(409, "A game started from the %s folder while installing; close it and update again." % into)
        self._set_install(tag, state="copying")
        dest = self.paths.q(into)
        try:
            shutil.copytree(partial, dest, dirs_exist_ok=True)
        except (OSError, shutil.Error) as e:
            raise PlayError(500, "Quark\\%s was only partly updated (%s); update it again." % (
                into, str(getattr(e, "strerror", None) or e)[:160]))

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
            if (self.installs.get(tag) or {}).get("state") in ACTIVE:
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

    def _release_rows(self, lst, blist):
        """The release list with what this PC has: installed (and how it was checked), which local folder holds
        that release, and whether it is newer than the local folder of its channel."""
        inst = {b["label"]: b for b in blist if b["kind"] == "version" and b["playable"]}
        local = {b["id"]: b for b in blist if b["kind"] == "local" and b.get("present")}
        rows = []
        for x in lst:
            b = inst.get(x["tag"])
            row = dict(x, installed=b is not None)
            if b is not None and b.get("check"):
                row["check"], row["checkNote"] = b["check"], b.get("checkNote")
            row["folders"] = [f for f, lb in local.items() if (lb.get("tag") and lb["tag"] == x["tag"]) or
                              (x["version"] and lb.get("version") == x["version"])]
            mine = local.get(x["channel"])  # (production / testing: the folder that follows that channel)
            if mine and mine.get("version") and x["version"] and version_cmp(x["version"], mine["version"]) > 0:
                row["newer"] = x["channel"]
            rows.append(row)
        return rows

    def view(self, rel_sig=None):
        """Everything the page shows. The release list is left out (`list` None, `same` True) when the page already
        has it (rel_sig: the `sig` it got last), which is almost every poll."""
        rel = self.releases()
        blist = self.builds()
        lst = self._release_rows(rel["list"], blist)
        sig = hashlib.sha1(json.dumps(lst, sort_keys=True).encode("utf-8")).hexdigest()[:16]
        releases = dict(rel, list=None if rel_sig == sig else lst, same=rel_sig == sig, sig=sig, total=len(lst))
        status = {(e["title"], e["stamp"]): e["status"] for e in self.doc_entries()}
        written = set(self.sync.get("written") or [])
        applied, blocked = self.sync.get("applied") or {}, self.sync.get("blocked") or {}
        unpushed = bool(self.sync.get("unpushed"))
        no_upstream = bool(self.sync.get("noUpstream"))
        present = os.path.isdir(self.docs_dir)
        records = self.notes(deleted=True)
        all_notes = [n for n in records if not n.get("deleted")]

        def removal(n):
            """A removed note the page still shows: "pending" (its entry is not out of the file yet) or "blocked" (an
            agent took the entry first, so it stays in the file); None: gone from the page."""
            if not n.get("deleted") or not present or n["id"] not in written:
                return None
            if n["id"] in blocked:
                return "blocked"
            return "pending" if n.get("rev", 0) > applied.get(n["id"], 0) else None
        notes = []
        for n in reversed([n for n in records if not n.get("deleted") or removal(n)][-20:]):
            key = (n.get("title"), n.get("stamp"))
            st = status.get(key)
            mine = n["id"] in written
            pending_edit = n.get("rev", 0) > applied.get(n["id"], 0)
            removed = removal(n)
            if not present:
                where = "kept on this PC only (no Claude Documents folder)"
            elif not mine:
                where = "waiting to go into Claude Documents"
            elif removed == "pending":
                where = "your removal is waiting to go into Claude Documents"
            elif pending_edit:
                where = "your edit is waiting to go into Claude Documents"
            elif n["id"] in blocked:
                where = "%s: %s first" % ("not removed" if removed else "your edit is not in the file",
                                          blocked_why(blocked[n["id"]]))
            elif no_upstream:
                where = "saved on this PC, not going to GitHub (no upstream branch)"
            elif unpushed:
                where = "saved on this PC, will go up later"
            else:
                where = "in Claude Documents"
            locked = None
            if present and mine:
                locked = ("no longer in %s" % DOCS_FILE) if st is None else (None if st == "open" else "an agent set it to " + st)
            notes.append({"id": n["id"], "title": n.get("title"), "stamp": n.get("stamp"), "savedAt": n.get("savedAt"),
                          "fields": n.get("fields"), "label": n.get("label"), "version": n.get("version"),
                          "build": n.get("build"), "durationSec": n.get("durationSec"), "args": n.get("args"),
                          "status": st, "where": where, "editedAt": n.get("editedAt"), "freeform": bool(n.get("freeform")),
                          "logTail": n.get("logTail"), "locked": locked, "removed": removed,
                          "blocked": n["id"] in blocked,
                          "exit": exit_info(n.get("exitCode"), n.get("stopped")) if n.get("session") else None})
        # every change that didn't reach the file (not only the newest 20 notes'): the page lists them with a way to
        # write a follow-up note
        byid = {n["id"]: n for n in records}
        blocked_list = [{"id": k, "title": byid[k].get("title"), "stamp": byid[k].get("stamp"),
                         "change": "removal" if byid[k].get("deleted") else "edit", "why": blocked_why(v),
                         "build": byid[k].get("build"), "label": byid[k].get("label"), "version": byid[k].get("version")}
                        for k, v in blocked.items() if k in byid and present]
        with self.lock:
            sessions = [self._session_view(s) for s in reversed(self.sessions[-30:])]
            installs = {k: dict(v) for k, v in self.installs.items()}
            total = len(self.sessions)
        p = self.pending()
        return {"now": util.now_iso(), "builds": blist, "releases": releases, "installs": installs,
                "sessions": sessions, "sessionsTotal": total, "running": p["running"], "pending": p["pending"], "openNotes": p["openNotes"],
                "notes": notes, "notesTotal": len(all_notes), "blocked": blocked_list,
                "docs": {"folder": self.docs_dir, "present": present, "file": DOCS_FILE,
                         "filePresent": os.path.isfile(os.path.join(self.docs_dir, DOCS_FILE)),
                         "sync": {k: v for k, v in self.sync.items() if k not in ("written", "applied", "blocked", "dismissed")}},
                "limits": {"field": MAX_FIELD, "args": MAX_ARGS},
                "platform": "windows" if util.IS_WINDOWS else sys.platform}

    def _session_view(self, s):
        """A session as the page shows it: how it ended, whether its log is still there."""
        s = dict(s)
        s.pop("pidCreated", None)
        if s.get("state") != "running":
            s["exit"] = exit_info(s.get("exitCode"), s.get("stopped"))
        path = self._log_path(s)
        s["hasLog"] = bool(path and os.path.isfile(path))
        return s

    def all_sessions(self):
        """Every kept session (KEEP_SESSIONS), newest first: the page's "Show all" (a poll sends the newest 30)."""
        with self.lock:
            return {"sessions": [self._session_view(s) for s in reversed(self.sessions)], "total": len(self.sessions)}

    # -- the routes server.py hands over (GET /api/play/<what>, POST /api/play/<what>)
    def api_get(self, what, query):
        """-> (HTTP code, JSON object)."""
        q = lambda k: (query.get(k) or [""])[0]
        try:
            if what == "log":
                return 200, self.game_log(q("session"))
            if what == "maps":
                return 200, self.build_maps(q("build"))
            if what == "sessions":
                return 200, self.all_sessions()
        except PlayError as e:
            return e.code, {"ok": False, "message": e.message, "reason": e.reason}
        return 404, {"ok": False, "message": "not found"}

    def api_post(self, what, req):
        """-> the answer for the newer POST routes, or None (not one of them). Raises PlayError."""
        if what == "stop":
            return {"session": self.stop_game(req.get("session"))}
        if what == "open":
            return self.open_path(req.get("what"), req.get("build") or req.get("session"))
        if what == "note-edit":
            n = self.edit_note(req.get("id"), req)
            return {"note": {k: n.get(k) for k in ("id", "title", "stamp", "line")}, "noop": bool(n.get("noop")),
                    "message": "Saved. The entry in %s changes in a moment." % DOCS_FILE}
        if what == "note-delete":
            return dict(self.delete_note(req.get("id")), message="Removed. Its entry leaves %s in a moment." % DOCS_FILE)
        if what == "note-dismiss":
            return self.dismiss_blocked(req.get("id"))
        if what == "install-cancel":
            return self.cancel_install(req.get("tag"))
        if what == "install-into":
            return {"install": self.install(req.get("tag"), req.get("into") or "-")}
        return None
