#!/usr/bin/env python3
"""Quark Weapons Editor: the local server behind the weapons editor page.

    python3 tools/weapon_editor/serve.py [--port 8773] [--data-dir DIR] [--maptool PATH] [--quark-tests PATH] [--maps-dir DIR ...] [--no-open]
    python3 tools/weapon_editor/serve.py --install [FOLDER]     copy the tool to FOLDER (default <Quark folder>/weapons-editor)
    tools/weapon_editor/run.sh   (Linux / macOS)        tools\\weapon_editor\\run.cmd   (Windows)
    --no-open (or QUARK_SHELL=1): no browser; the Quark Project app starts it and shows http://127.0.0.1:8773/?embed=1

Python 3 standard library only. Binds 127.0.0.1 only. It:
  - serves the page (web/),
  - lists, reads, writes and deletes weapon sets: <data dir>/custom_weapons/<name>.qweapons (DESIGN.md section 2; the
    previous version of a file is kept as <name>.qweapons.bak, and a delete renames the file to that),
  - writes the "play in game" custom mode <data dir>/custom_modes/wpn-<set>.qmode (custommode=, mode=, weaponset=):
    a new file gets those three lines; an existing one keeps every other line verbatim (line endings too) and only its
    weaponset= line is replaced (added after mode= when it has none; never inside the gamemode editor's `logic`
    section),
  - lists the custom modes (which weapon set each one plays),
  - lists the maps (the build's maps/ folder, the game's user maps) for the Range and Fights tabs,
  - keeps the page's state (<data dir>/weapon_editor/state/<key>.json),
  - runs the game's helper quark_maptool once per request (one JSON request on stdin, one JSON answer on stdout:
    wpnparams / wpncheck / wpnstats / wpnrange, DESIGN.md section 4, and the bot editor's simcheck / parse),
  - runs `quark_maptool sim --stream` / `simbatch --stream` for the Fights tab (Server-Sent Events out, POST controls in),
  - asks quark_tests for the game's weapon catalog (`exportweapons`) and checks (`checkweapons`) when it has them.

Settings come from (later wins): built-in defaults, weapon_editor.json next to this file, then the command line.
weapon_editor.json keys: port, dataDir, maptool, quarkTests, mapsDirs (list), defaultMapsDir, helperArgs.

Every request must come from the page itself: Host must be 127.0.0.1 / localhost and every write carries the header
X-Quark-Editor (a page on another site can't send it without a CORS preflight, which is never allowed).
"""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, threading, time, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
WEB = os.path.join(HERE, "web")
CACHE = os.path.join(HERE, ".cache")
IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")        # map / state names
MODE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _-]{0,39}$")        # custom mode names: CustomRules safeName's characters, 40 max
SET_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _-]{0,35}$")         # weapon set names: the same characters, 36 max (wpn-<set> is a mode name)
BASE_MODES = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"}
MAX_BODY = 8 << 20
DEFAULT_PORT = 8773          # fixed (the Quark Project app opens http://127.0.0.1:8773/); weapon_editor.json / --port change it
HELPER_CMDS = {"wpnparams", "wpncheck", "wpnstats", "wpnrange", "wpnfight", "sim", "simbatch", "simcheck", "parse"}
RUN_CMDS = {"sim", "simbatch", "wpnfight", "wpnrange"}
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png",
        ".qweapons": "text/plain; charset=utf-8", ".qmode": "text/plain; charset=utf-8", ".qmap": "text/plain; charset=utf-8",
        ".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8", ".ico": "image/x-icon"}

# What the installed copy holds (--install). web/ is copied whole, without its tests.
PROGRAM_FILES = ["serve.py", "run.cmd", "run.sh", "README.md", "weapon_editor.example.json", "DESIGN.md"]


def valid_name(n):
    return isinstance(n, str) and bool(NAME_RE.match(n)) and ".." not in n


def valid_mode(n):
    return isinstance(n, str) and bool(MODE_RE.match(n)) and not n.endswith(" ")


def valid_set(n):
    return isinstance(n, str) and bool(SET_RE.match(n)) and not n.endswith(" ")


def default_data_dir():
    """The game's user data folder (map.cpp defaultUserDir), QUARK_DATA_DIR first."""
    e = os.environ.get("QUARK_DATA_DIR")
    if e: return e
    if IS_WIN:
        a = os.environ.get("APPDATA")
        if a: return os.path.join(a, "quark")
    elif sys.platform == "darwin":
        return os.path.join(os.path.expanduser("~"), "Library", "Application Support", "quark")
    x = os.environ.get("XDG_DATA_HOME")
    if x: return os.path.join(x, "quark")
    return os.path.join(os.path.expanduser("~"), ".local", "share", "quark")


# --- the Quark folder (holds repository\, working-repo*\, the installed editors) -----------------------------------

def looks_like_quark(d):
    try:
        return os.path.isdir(d) and any(n.lower() == "repository" or n.lower().startswith("working-repo") for n in os.listdir(d))
    except OSError:
        return False


def inside_git(d):
    while True:
        if os.path.exists(os.path.join(d, ".git")): return d
        up = os.path.dirname(d)
        if up == d: return None
        d = up


def default_quark():
    """where this is installed (Quark\\weapons-editor\\), the folder holding the clone this runs from
    (Quark\\working-repo\\tools\\weapon_editor\\), or ~/Desktop/Quark"""
    cands = [os.path.dirname(HERE)]
    clone = inside_git(HERE)
    if clone: cands.append(os.path.dirname(clone))
    cands.append(os.path.join(os.path.expanduser("~"), "Desktop", "Quark"))
    for c in cands:
        if looks_like_quark(c): return c
    return cands[-1]


def source_roots():
    """Repository checkouts to look in for the helper and the maps: the one this runs from (if it is one), then the
    Quark folder's clones (an installed copy has none of its own)."""
    out = []
    if os.path.isfile(os.path.join(ROOT, "CMakeLists.txt")): out.append(ROOT)
    q = default_quark()
    try:
        for n in sorted(os.listdir(q)):
            if n.lower() == "repository" or n.lower().startswith("working-repo"):
                p = os.path.join(q, n)
                if os.path.isdir(p) and p not in out: out.append(p)
    except OSError:
        pass
    return out


def find_binary(name, explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    cands = [os.path.join(HERE, name + EXE)]          # an installed copy: next to serve.py
    for r in source_roots():
        for b in ("build", "build-editors", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin"):
            cands.append(os.path.join(r, b, name + EXE))
    for c in cands:
        if os.path.isfile(c): return c
    return shutil.which(name)


class Config:
    def __init__(self, args):
        cfg = {}
        path = os.path.join(HERE, "weapon_editor.json")
        if os.path.isfile(path):
            try:
                with open(path, encoding="utf-8") as f: cfg = json.load(f)
            except Exception as ex:
                print("weapon_editor.json ignored: %s" % ex)
        g = lambda k, a=None: a if a not in (None, [], "") else cfg.get(k)
        self.port = int(g("port", args.port) or DEFAULT_PORT)
        self.data_dir = os.path.abspath(g("dataDir", args.data_dir) or default_data_dir())
        self.maptool = find_binary("quark_maptool", g("maptool", args.maptool))
        self.quark_tests = find_binary("quark_tests", g("quarkTests", args.quark_tests))
        self.helper_args = list(cfg.get("helperArgs") or [])
        self.extra_maps = [os.path.abspath(d) for d in (g("mapsDirs", args.maps_dir) or [])]
        dm = g("defaultMapsDir")
        self.default_maps = os.path.abspath(dm) if dm else None

    def default_maps_dir(self):
        if self.default_maps and os.path.isdir(self.default_maps): return self.default_maps
        cands = [os.path.join(HERE, "maps")]
        for r in source_roots(): cands += [os.path.join(r, "maps"), os.path.join(r, "build", "maps")]
        cands.append(os.path.join(CACHE, "maps"))
        for d in cands:
            try:
                if os.path.isdir(d) and any(f.endswith(".qmap") for f in os.listdir(d)): return d
            except OSError:
                continue
        return None

    def user_maps_dir(self): return os.path.join(self.data_dir, "maps")
    def sets_dir(self): return os.path.join(self.data_dir, "custom_weapons")
    def modes_dir(self): return os.path.join(self.data_dir, "custom_modes")
    def state_dir(self): return os.path.join(self.data_dir, "weapon_editor", "state")


CFG = None


# ---------------------------------------------------------------- weapon sets

def set_path(name):
    return os.path.join(CFG.sets_dir(), name + ".qweapons")


def read_text(path):
    if not os.path.isfile(path): return None
    with open(path, "rb") as f: return f.read().decode("utf-8", "replace")


def set_info(path):
    """{title, params, slots, labels} from a .qweapons file, read loosely (the page parses it properly)."""
    info = {"title": "", "params": 0, "slots": 0, "labels": 0, "curves": 0}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f):
                s = line.split("#", 1)[0].strip()
                if not s: continue
                m = re.match(r'^weapons\s+"([^"]*)"', s)
                if m and i < 4: info["title"] = m.group(1)
                elif s.startswith("param "): info["params"] += 1
                elif s.startswith("slot "): info["slots"] += 1
                elif s.startswith("label "): info["labels"] += 1
                elif s.startswith("curve "): info["curves"] += 1
    except OSError:
        pass
    return info


def list_sets():
    d = CFG.sets_dir()
    out = []
    if not os.path.isdir(d): return out
    for f in sorted(os.listdir(d), key=str.lower):
        if not f.endswith(".qweapons") or not valid_set(f[:-9]): continue
        p = os.path.join(d, f)
        st = os.stat(p)
        e = {"name": f[:-9], "size": st.st_size, "mtime": st.st_mtime}
        e.update(set_info(p))
        out.append(e)
    return out


# ---------------------------------------------------------------- custom modes (only the weaponset= line is ours)

def is_logic_line(line):
    """A `logic` line starts the gamemode editor's section (to the end of the file): never touched here."""
    s = line.strip()
    return s == "logic" or s.startswith("logic=") or s.startswith("logic ") or s.startswith("logic:")


def merge_weaponset(text, set_name, mode_name, base):
    """The .qmode text with its weaponset= line set to `set_name`. text None/"" = a new file: custommode=, mode=,
    weaponset=. Otherwise every other line stays byte for byte (its own line ending), the last weaponset= line before a
    `logic` line is replaced in place (the game reads the last one; earlier duplicates are kept as they are), and a
    file without one gets it after its mode= line (else after custommode=, else after the last key=value line)."""
    if not text:
        return "custommode=%s\nmode=%s\nweaponset=%s\n" % (mode_name, base, set_name)
    lines = text.split("\n")
    trailing = lines[-1] == ""
    if trailing: lines = lines[:-1]
    cr = lambda l: "\r" if l.endswith("\r") else ""
    cut = next((i for i, l in enumerate(lines) if is_logic_line(l)), len(lines))
    keyof = lambda l: l.split("=", 1)[0] if "=" in l else None
    idx = [i for i in range(cut) if keyof(lines[i]) == "weaponset"]
    if idx:
        i = idx[-1]
        lines[i] = "weaponset=%s%s" % (set_name, cr(lines[i]))
    else:
        j = None
        for k in ("mode", "custommode"):
            hits = [i for i in range(cut) if keyof(lines[i]) == k]
            if hits: j = hits[-1]; break
        if j is None:
            kv = [i for i in range(cut) if keyof(lines[i]) is not None]
            j = kv[-1] if kv else -1
        end = cr(lines[j]) if j >= 0 else (cr(lines[0]) if lines else "")
        lines.insert(j + 1, "weaponset=%s%s" % (set_name, end))
    return "\n".join(lines) + ("\n" if trailing else "")


def mode_info(path):
    info = {"custommode": "", "mode": "", "weaponset": "", "logic": False}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if is_logic_line(line): info["logic"] = True; break
                k, _, v = line.strip().partition("=")
                if k in ("custommode", "mode", "weaponset"): info[k] = v
    except OSError:
        pass
    return info


# ---------------------------------------------------------------- maps

GEN_LOCK = threading.Lock()


def generate_default_maps():
    with GEN_LOCK:
        if CFG.default_maps_dir(): return CFG.default_maps_dir(), ""
        gen = next((os.path.join(r, "tools", "make_maps.py") for r in source_roots() if os.path.isfile(os.path.join(r, "tools", "make_maps.py"))), None)
        if not gen: return None, "tools/make_maps.py not found"
        out = os.path.join(CACHE, "maps")
        os.makedirs(out, exist_ok=True)
        r = subprocess.run([sys.executable, gen, "--out", out], capture_output=True, text=True)
        if r.returncode != 0: return None, (r.stderr or r.stdout)[-2000:]
        return out, r.stdout.strip()


def map_header(path):
    info = {"title": "", "for": "", "modes": []}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f):
                if i > 60: break
                line = line.split("#", 1)[0].strip()
                kw, _, rest = line.partition(" ")
                if kw == "title": info["title"] = rest.strip()
                elif kw == "for": info["for"] = rest.strip()
                elif kw == "modes": info["modes"] = rest.split()
    except OSError:
        pass
    return info


def list_maps():
    out, dirs = [], []
    dm = CFG.default_maps_dir()
    if dm: dirs.append(("default", dm))
    dirs.append(("user", CFG.user_maps_dir()))
    for d in CFG.extra_maps: dirs.append(("extra", d))
    for src, d in dirs:
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".qmap") or not valid_name(f[:-5]): continue
            m = {"name": f[:-5], "source": src}
            m.update(map_header(os.path.join(d, f)))
            out.append(m)
    return out


def map_path(name, source):
    if not valid_name(name): return None
    d = {"default": CFG.default_maps_dir(), "user": CFG.user_maps_dir()}.get(source)
    dirs = [d] if d else list(CFG.extra_maps)
    for dd in dirs:
        if dd and os.path.isfile(os.path.join(dd, name + ".qmap")): return os.path.join(dd, name + ".qmap")
    return None


def atomic_write(path, data, backup=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if backup and os.path.isfile(path):
        try: shutil.copyfile(path, path + ".bak")
        except OSError: pass
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-", suffix=".part")
    with os.fdopen(fd, "wb") as f: f.write(data)
    os.replace(tmp, path)


# ---------------------------------------------------------------- the helpers

def helper_cmd(args):
    """(the command line, None) or (None, (HTTP code, why)): a missing helper and a refused request say different things"""
    if not CFG.maptool: return None, (501, "quark_maptool not found")
    if not args or args[0] not in HELPER_CMDS: return None, (400, "not a helper command: %s" % (args[0] if args else "(none)"))
    if not all(isinstance(a, str) and re.match(r"^[A-Za-z0-9_.=:,+-]{1,64}$", a) for a in args): return None, (400, "bad helper arguments")
    return [CFG.maptool] + CFG.helper_args + list(args), None


def parse_answer(out):
    """A helper's answer: one JSON object, or NDJSON whose last line is the answer."""
    try: return json.loads(out)
    except ValueError: pass
    for line in reversed(out.strip().splitlines()):
        try: return json.loads(line)
        except ValueError: continue
    return {"raw": out[-8000:]}


CATALOG_CACHE = {"mtime": None, "answer": None}


def tests_catalog():
    """`quark_tests exportweapons <out.json>` (DESIGN.md section 3), cached per quark_tests build. None when it has no
    such command."""
    qt = CFG.quark_tests
    if not qt: return None, "quark_tests not found"
    try: mt = os.path.getmtime(qt)
    except OSError: return None, "quark_tests not found"
    if CATALOG_CACHE["mtime"] == mt: return CATALOG_CACHE["answer"], CATALOG_CACHE.get("why", "")
    fd, out = tempfile.mkstemp(prefix="qwe-cat-", suffix=".json")
    os.close(fd)
    ans, why = None, ""
    try:
        r = subprocess.run([qt, "exportweapons", out], capture_output=True, text=True, timeout=60,
                           creationflags=subprocess.CREATE_NO_WINDOW if IS_WIN else 0)
        if r.returncode == 0 and os.path.getsize(out) > 0:
            with open(out, encoding="utf-8") as f: ans = json.load(f)
        else: why = (r.stderr or r.stdout or "exit %d" % r.returncode).strip()[-300:]
    except (OSError, ValueError, subprocess.TimeoutExpired) as ex:
        why = str(ex)
    finally:
        try: os.remove(out)
        except OSError: pass
    CATALOG_CACHE.update(mtime=mt, answer=ans, why=why)
    return ans, why


def tests_check(text):
    """`quark_tests checkweapons <file>`: {ok, errors:[{line,col,msg}], warnings}. None when it has no such command."""
    qt = CFG.quark_tests
    if not qt: return None
    fd, p = tempfile.mkstemp(prefix="qwe-chk-", suffix=".qweapons")
    with os.fdopen(fd, "wb") as f: f.write(text.encode("utf-8"))
    try:
        r = subprocess.run([qt, "checkweapons", p], capture_output=True, text=True, timeout=30,
                           creationflags=subprocess.CREATE_NO_WINDOW if IS_WIN else 0)
        try:
            ans = json.loads(r.stdout)
            if isinstance(ans, dict) and "ok" in ans: return ans
        except ValueError:
            pass
        return None
    except (OSError, subprocess.TimeoutExpired):
        return None
    finally:
        try: os.remove(p)
        except OSError: pass


class Run:
    """One helper process whose stdout JSON lines are kept (a page that reconnects gets them from where it was)."""
    MAX_LINES = 400000

    def __init__(self, cmd, request):
        self.id = uuid.uuid4().hex[:12]
        self.lines, self.cond, self.done, self.code, self.err = [], threading.Condition(), False, None, ""
        self.started = time.time()
        self.clients, self.gone_at = 0, None   # open event streams; when the last one closed (see reap_runs)
        flags = subprocess.CREATE_NO_WINDOW if IS_WIN else 0
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     bufsize=1, text=True, encoding="utf-8", errors="replace", creationflags=flags)
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()
        self.streaming = "--stream" in cmd
        if not self.streaming: self.proc.stdin.close()   # (without --stream the helper reads its request until stdin closes)
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            line = line.strip()
            if not line: continue
            with self.cond:
                if len(self.lines) < self.MAX_LINES: self.lines.append(line)
                self.cond.notify_all()
        self.code = self.proc.wait()
        with self.cond:
            self.done = True
            self.cond.notify_all()

    def _read_err(self):
        for line in self.proc.stderr:
            self.err = (self.err + line)[-4000:]

    def control(self, line):
        if not self.streaming or self.done or self.proc.poll() is not None: return False
        try:
            self.proc.stdin.write(line.strip() + "\n")
            self.proc.stdin.flush()
            return True
        except (OSError, ValueError):
            return False

    def stop(self):
        if not self.control("stop") and self.proc.poll() is None: self.proc.terminate()
        def killer():
            try: self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired: self.proc.kill()
        threading.Thread(target=killer, daemon=True).start()


RUNS = {}
RUNS_LOCK = threading.Lock()
ORPHAN_SECONDS = 5      # a run with no page watching it for this long is stopped (a closed window, a killed browser)
UNWATCHED_SECONDS = 30  # ...and one no page ever opened the events of


def reap_runs():
    """Stops runs nobody watches any more: the page's pagehide stop covers a reload, not a window that was killed or a
    browser that was closed, and `sim --stream` would play on in real time until its seconds end."""
    while True:
        time.sleep(1)
        now = time.time()
        with RUNS_LOCK:
            idle = [r for r in RUNS.values() if not r.done and r.clients == 0 and not getattr(r, "reaped", False)
                    and ((r.gone_at is not None and now - r.gone_at > ORPHAN_SECONDS)
                         or (r.gone_at is None and now - r.started > UNWATCHED_SECONDS))]
            for r in idle: r.reaped = True
        for r in idle: r.stop()


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkWeaponsEditor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        if os.environ.get("QWE_VERBOSE"): sys.stderr.write("%s\n" % (fmt % a))

    def _host_ok(self):
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]").lower()
        return host in ("127.0.0.1", "localhost", "::1")

    def _send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body)
        if isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _err(self, code, msg):
        if self.command == "POST" and not getattr(self, "_body_read", False):
            # (a POST refused before its body was read: the unread body would be taken for the next request on this
            # keep-alive connection, so close it)
            self.close_connection = True
            return self._send(code, {"error": msg}, extra={"Connection": "close"})
        self._send(code, {"error": msg})

    def _body(self):
        if getattr(self, "_raw", None) is not None: return self._raw
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY: raise ValueError("body too large")
        self._body_read = True
        self._raw = self.rfile.read(n) if n else b""
        return self._raw

    def _json_body(self):
        b = self._body()
        return json.loads(b.decode("utf-8")) if b else {}

    def _write_ok(self):
        return self._host_ok() and self.headers.get("X-Quark-Editor") == "1"

    def do_GET(self):
        if not self._host_ok(): return self._err(403, "bad host")
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/" or p == "/index.html": return self._static("index.html")
            if p.startswith("/web/"): return self._static(p[5:])
            if p == "/api/config": return self._send(200, self._config())
            if p == "/api/ping": return self._send(200, {"ok": True, "tool": "weapons-editor"}, extra={"Access-Control-Allow-Origin": "*"})
            if p == "/api/maps": return self._send(200, {"maps": list_maps(), "defaultDir": CFG.default_maps_dir()})
            if p == "/api/map":
                path = map_path(q.get("name", ""), q.get("source", "default"))
                if not path: return self._err(404, "no such map")
                with open(path, encoding="utf-8", errors="replace") as f: return self._send(200, f.read(), MIME[".qmap"])
            if p == "/api/sets": return self._send(200, {"sets": list_sets(), "dir": CFG.sets_dir()})
            if p == "/api/set":
                n = q.get("name", "")
                if not valid_set(n): return self._err(400, "bad name")
                t = read_text(set_path(n))
                if t is None: return self._err(404, "no such weapon set")
                return self._send(200, t, MIME[".qweapons"])
            if p == "/api/modes":
                d = CFG.modes_dir()
                res = []
                if os.path.isdir(d):
                    for f in sorted(os.listdir(d), key=str.lower):
                        if f.endswith(".qmode") and valid_mode(f[:-6]):
                            m = {"name": f[:-6]}
                            m.update(mode_info(os.path.join(d, f)))
                            res.append(m)
                return self._send(200, {"modes": res, "dir": d})
            if p == "/api/mode":
                n = q.get("name", "")
                if not valid_mode(n): return self._err(400, "bad name")
                t = read_text(os.path.join(CFG.modes_dir(), n + ".qmode"))
                if t is None: return self._err(404, "no such custom mode")
                return self._send(200, t, MIME[".qmode"])
            if p == "/api/catalog":
                ans, why = tests_catalog()
                return self._send(200, {"catalog": ans, "why": why, "quarkTests": CFG.quark_tests})
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                path = os.path.join(CFG.state_dir(), n + ".json")
                if not os.path.isfile(path): return self._send(200, "null")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            m = re.match(r"^/api/run/([0-9a-f]{12})/events$", p)
            if m: return self._events(m.group(1), int(q.get("from", "0") or 0))
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_POST(self):
        self._body_read = False; self._raw = None
        if not self._write_ok(): return self._err(403, "writes need the X-Quark-Editor header from the page")
        try: self._body()          # (read now, whatever the endpoint: an unread body would break the keep-alive connection)
        except ValueError as ex: return self._err(413, str(ex))
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/api/set":
                # {text, expect?}: write custom_weapons/<name>.qweapons (the old one kept as .bak). `expect` (the text
                # the page loaded) refuses the write when the file changed on disk since, unless `force`; `expect: null`
                # (a new name: Save as, a renamed set) refuses it when the file is already there.
                n = q.get("name", "")
                if not valid_set(n): return self._err(400, "bad name (letters, digits, space, _ -; 36 at most)")
                body = self._json_body()
                text = body.get("text")
                if not isinstance(text, str) or not text.strip(): return self._err(400, "no text")
                path = set_path(n)
                cur = read_text(path)
                if cur is not None and "expect" in body and body["expect"] is None and not body.get("force"):
                    return self._send(409, {"error": "a set with this name already exists", "exists": True, "text": cur})
                if cur is not None and isinstance(body.get("expect"), str) and cur != body["expect"] and not body.get("force"):
                    return self._send(409, {"error": "changed on disk since it was opened", "text": cur})
                atomic_write(path, text.encode("utf-8"))
                return self._send(200, {"ok": True, "path": path, "created": cur is None})
            if p == "/api/set/delete":
                n = q.get("name", "")
                if not valid_set(n): return self._err(400, "bad name")
                path = set_path(n)
                if not os.path.isfile(path): return self._err(404, "no such weapon set")
                os.replace(path, path + ".bak")
                return self._send(200, {"ok": True, "kept": path + ".bak"})
            if p == "/api/playmode":
                # {set, base?}: custom_modes/wpn-<set>.qmode points at the set (Host > Custom modes can start it)
                body = self._json_body()
                s = body.get("set", "")
                if not valid_set(s): return self._err(400, "bad weapon set name")
                base = body.get("base") or "ffa"
                if base not in BASE_MODES: return self._err(400, "bad base mode")
                name = "wpn-" + s
                path = os.path.join(CFG.modes_dir(), name + ".qmode")
                cur = read_text(path)
                out = merge_weaponset(cur, s, name, base)
                if out != cur: atomic_write(path, out.encode("utf-8"))
                return self._send(200, {"ok": True, "path": path, "name": name, "text": out, "created": cur is None,
                                        "setExists": os.path.isfile(set_path(s))})
            if p == "/api/merge-mode":   # the same merge without writing (tests, previews)
                body = self._json_body()
                return self._send(200, {"text": merge_weaponset(body.get("text") or "", body.get("set") or "x",
                                                                body.get("name") or "wpn-x", body.get("base") or "ffa")})
            if p == "/api/check":
                body = self._json_body()
                ans = tests_check(body.get("text") or "")
                return self._send(200, {"answer": ans, "via": "quark_tests checkweapons" if ans else None})
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                body = self._body()
                json.loads(body.decode("utf-8") or "null")
                atomic_write(os.path.join(CFG.state_dir(), n + ".json"), body, backup=False)
                return self._send(200, {"ok": True})
            if p == "/api/generate-maps":
                d, msg = generate_default_maps()
                return self._send(200 if d else 500, {"dir": d, "log": msg})
            if p == "/api/helper":
                body = self._json_body()
                cmd, bad = helper_cmd(body.get("args") or [])
                if not cmd: return self._err(*bad)
                t0 = time.time()
                r = subprocess.run(cmd, input=json.dumps(body.get("request") or {}), capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=float(body.get("timeout") or 120),
                                   creationflags=subprocess.CREATE_NO_WINDOW if IS_WIN else 0)
                return self._send(200, {"code": r.returncode, "answer": parse_answer(r.stdout), "stderr": r.stderr[-4000:],
                                        "wallMs": round((time.time() - t0) * 1000, 1)})
            if p == "/api/run":
                body = self._json_body()
                args = body.get("args") or ["sim", "--stream"]
                if not args or args[0] not in RUN_CMDS: return self._err(400, "not a streaming command")
                cmd, bad = helper_cmd(args)
                if not cmd: return self._err(*bad)
                run = Run(cmd, body.get("request") or {})
                with RUNS_LOCK:
                    for k in [k for k, r in RUNS.items() if r.done and time.time() - r.started > 3600]: del RUNS[k]
                    RUNS[run.id] = run
                return self._send(200, {"id": run.id})
            m = re.match(r"^/api/run/([0-9a-f]{12})/(control|stop)$", p)
            if m:
                run = RUNS.get(m.group(1))
                if not run: return self._err(404, "no such run")
                if m.group(2) == "stop":
                    run.stop()
                    return self._send(200, {"ok": True})
                line = (self._json_body().get("line") or "").strip()
                if not re.match(r"^(pause|resume|step|stop|speed ([0-9.]{1,8}|max)|realtime (on|off))$", line):
                    return self._err(400, "bad control line")
                return self._send(200, {"ok": run.control(line)})
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except subprocess.TimeoutExpired:
            self._err(504, "helper timed out")
        except ValueError as ex:
            self._err(400, str(ex))
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_PUT(self): return self.do_POST()

    def _config(self):
        return {"dataDir": CFG.data_dir, "sets": CFG.sets_dir(), "modes": CFG.modes_dir(), "userMaps": CFG.user_maps_dir(),
                "defaultMaps": CFG.default_maps_dir(), "maptool": CFG.maptool, "quarkTests": CFG.quark_tests,
                "platform": sys.platform, "port": CFG.port, "installed": not os.path.isfile(os.path.join(ROOT, "CMakeLists.txt"))}

    def _static(self, rel):
        rel = rel.split("?", 1)[0]
        full = os.path.normpath(os.path.join(WEB, rel))
        if not full.startswith(os.path.normpath(WEB) + os.sep) or not os.path.isfile(full):
            return self._err(404, "not found")
        ext = os.path.splitext(full)[1].lower()
        with open(full, "rb") as f: data = f.read()
        self._send(200, data, MIME.get(ext, "application/octet-stream"))

    def _events(self, rid, start):
        run = RUNS.get(rid)
        if not run: return self._err(404, "no such run")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        with RUNS_LOCK: run.clients += 1
        try: self._stream(run, start)
        finally:
            with RUNS_LOCK:
                run.clients -= 1
                if run.clients == 0: run.gone_at = time.time()

    def _stream(self, run, start):
        i = max(0, start)
        while True:
            with run.cond:
                while i >= len(run.lines) and not run.done:
                    run.cond.wait(timeout=4)
                    if i >= len(run.lines) and not run.done: break   # (keepalive)
                batch = run.lines[i:]
                done = run.done and i + len(batch) >= len(run.lines)
            if batch:
                self.wfile.write(("".join("data: %s\n" % l for l in batch) + "\n").encode("utf-8"))
                i += len(batch)
            else:
                self.wfile.write(b": keepalive\n\n")
            self.wfile.flush()
            if done:
                info = json.dumps({"type": "_exit", "code": run.code, "stderr": run.err[-2000:]})
                self.wfile.write(("event: exit\ndata: %s\n\n" % info).encode("utf-8"))
                self.wfile.flush()
                return


# ---------------------------------------------------------------- install

def checkout_maptool():
    """quark_maptool from the checkout this runs from (its build folders), or the current folder's (the Quark Project
    app runs --install with cwd = a checkout); never PATH or an installed copy."""
    roots = []
    for r in (ROOT, os.getcwd()):
        r = os.path.abspath(r)
        if os.path.isfile(os.path.join(r, "CMakeLists.txt")) and r not in roots: roots.append(r)
    for r in roots:
        for b in ("build-editors", "build", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin"):
            c = os.path.join(r, b, "quark_maptool" + EXE)
            if os.path.isfile(c): return c
    return None


def install(dest):
    """Copy the program to `dest`. Non-interactive and safe to re-run: only the program files are replaced
    (weapon_editor.json, the data folder and anything else there are kept); quark_maptool is copied from the
    checkout's build folders when one is built, else the copy already installed (if any) is kept."""
    dest = os.path.abspath(dest)
    if os.path.normcase(dest) == os.path.normcase(HERE):
        print("weapons editor: already installed here (%s)" % dest); return 0
    try:
        os.makedirs(dest, exist_ok=True)
        for n in PROGRAM_FILES:
            src = os.path.join(HERE, n)
            if os.path.isfile(src): shutil.copyfile(src, os.path.join(dest, n))
        if not IS_WIN and os.path.isfile(os.path.join(dest, "run.sh")): os.chmod(os.path.join(dest, "run.sh"), 0o755)
        # web/: copied over what is there (a stale file left from an older version is harmless: nothing links it)
        shutil.copytree(WEB, os.path.join(dest, "web"), dirs_exist_ok=True, ignore=shutil.ignore_patterns("node_modules", "*.png", "test"))
        mt = checkout_maptool()
        have = os.path.join(dest, "quark_maptool" + EXE)
        if mt:
            tmp = have + ".new"
            shutil.copyfile(mt, tmp)
            if not IS_WIN: os.chmod(tmp, 0o755)
            try: os.replace(tmp, have)
            except OSError as ex:   # (Windows: the installed one is running; keep it, say so)
                try: os.remove(tmp)
                except OSError: pass
                print("weapons editor: quark_maptool in use, kept the installed one (%s)" % ex)
            if IS_WIN:   # a MinGW build's runtime DLLs, if it has any next to it
                for f in os.listdir(os.path.dirname(mt)):
                    if f.lower().endswith(".dll") and f.lower().startswith(("libgcc", "libstdc", "libwinpthread")):
                        try: shutil.copyfile(os.path.join(os.path.dirname(mt), f), os.path.join(dest, f))
                        except OSError: pass
            maptool_note = "quark_maptool from %s" % mt
        elif os.path.isfile(have):
            maptool_note = "no quark_maptool built in this checkout: kept the installed one"
        else:
            maptool_note = "no quark_maptool built yet: the page uses its MOCK until one is installed"
        dm = CFG.default_maps_dir() if CFG else None
        md = os.path.join(dest, "maps")
        if dm and os.path.normcase(os.path.abspath(dm)) != os.path.normcase(md):
            os.makedirs(md, exist_ok=True)
            for f in os.listdir(dm):
                if f.endswith(".qmap"): shutil.copyfile(os.path.join(dm, f), os.path.join(md, f))
    except OSError as ex:
        print("weapons editor: install to %s failed: %s" % (dest, ex))
        return 1
    print("weapons editor installed in %s (%s)" % (dest, maptool_note))
    return 0


def main():
    global CFG
    ap = argparse.ArgumentParser(description="Quark weapons editor (local server)")
    ap.add_argument("--port", type=int)
    ap.add_argument("--data-dir", help="the game's user data folder (default: QUARK_DATA_DIR, else %%APPDATA%%\\quark or ~/.local/share/quark)")
    ap.add_argument("--maptool", help="path to quark_maptool (default: next to serve.py, then this repository's build/, then PATH)")
    ap.add_argument("--quark-tests", help="path to quark_tests (the weapon catalog and checks; default: next to serve.py, then build/, then PATH)")
    ap.add_argument("--maps-dir", action="append", help="another folder of .qmap files to list (repeatable)")
    ap.add_argument("--no-browser", "--no-open", dest="no_browser", action="store_true",
                    help="don't open a browser (also when QUARK_SHELL=1: the Quark Project app shows the page)")
    ap.add_argument("--install", nargs="?", const="", metavar="FOLDER", help="copy the tool to FOLDER (default <Quark folder>/weapons-editor) and stop")
    args = ap.parse_args()
    CFG = Config(args)
    if args.install is not None:
        sys.exit(install(args.install or os.path.join(default_quark(), "weapons-editor")))
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", CFG.port), Handler)
    except OSError as ex:
        print("can't listen on 127.0.0.1:%d (%s): is the weapons editor already running? (another port: --port or weapon_editor.json)" % (CFG.port, ex))
        sys.exit(1)
    srv.daemon_threads = True
    threading.Thread(target=reap_runs, daemon=True).start()
    url = "http://127.0.0.1:%d/" % CFG.port
    print("Quark Weapons Editor on %s" % url)
    print("  data dir     %s" % CFG.data_dir)
    print("  weapon sets  %s" % CFG.sets_dir())
    print("  maps         %s" % CFG.default_maps_dir())
    print("  maptool      %s" % (CFG.maptool or "(not found: the page uses its MOCK)"))
    print("  quark_tests  %s" % (CFG.quark_tests or "(not found)"))
    sys.stdout.flush()
    if not args.no_browser and os.environ.get("QUARK_SHELL") != "1":
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        for r in list(RUNS.values()):
            try: r.proc.kill()
            except Exception: pass


if __name__ == "__main__":
    main()
