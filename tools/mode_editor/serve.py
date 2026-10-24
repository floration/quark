#!/usr/bin/env python3
"""Quark Gamemode Editor: the local server behind the mode editor page.

    python3 tools/mode_editor/serve.py [--port 8771] [--data-dir DIR] [--maptool PATH] [--quark-tests PATH] [--no-open]
    tools/mode_editor/run.sh   (Linux / macOS)        tools\\mode_editor\\run.cmd   (Windows, no console window)
    --no-open (or QUARK_SHELL=1): no browser; the Supervisor Board starts this and shows http://127.0.0.1:8771/?embed=1

Python 3 standard library only. Binds 127.0.0.1 only. Adapted from tools/bot_editor/serve.py at c7aae27. It:
  - serves the page (web/),
  - lists, reads and writes the user's modes (<data dir>/custom_modes/<name>.qmode), keeping every line it doesn't own
    exactly as written (the shared file rule, below) and refusing a save over a file that changed on disk since the
    page loaded it (409),
  - gives the catalog and the built-in logic texts: `quark_tests exportmode` when that build has it, else the
    repository's tools/modes/*.qlogic, else the page's fixtures (each answer says which),
  - runs `quark_tests checkmode <file>` on save (or `quark_maptool modecheck`), when the build has it,
  - runs `quark_maptool modesim` / `modebatch` for the Sim view, when the build has them (its JSON lines are relayed
    to the page as Server-Sent Events; tools/mode_editor/native/MODESIM.md has the formats). Without them the page
    shows Sim as unavailable; nothing is simulated here.

The shared file rule (agreed with the physics editor): a .qmode is the game's key=value lines (CustomRules::toText)
then the logic block from the first line that is exactly `logic <N>`. This editor owns the logic block and the mode
keys (custommode, mode, timelimit, scorelimit, rounds, maps); the physics editor owns the movement keys. A save keeps
every other line exactly as written and in place, never reorders, and never writes a key=value line after `logic`.

Settings come from (later wins): built-in defaults, mode_editor.json next to this file, then the command line.
mode_editor.json keys: port, dataDir, maptool, quarkTests, repo (a checkout whose build/ to search), helperArgs.

Every request must come from the page itself: Host must be 127.0.0.1 / localhost, and every write carries the header
X-Quark-Editor (a page on another site can't send it without a CORS preflight, which is never allowed).
"""
import argparse, hashlib, json, mmap, os, re, shutil, subprocess, sys, tempfile, threading, time, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "web")
CACHE = os.path.join(HERE, ".cache")
IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""
NO_WINDOW = subprocess.CREATE_NO_WINDOW if IS_WIN else 0

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
MAX_BODY = 8 << 20
MAX_MODE = 1 << 20
DEFAULT_PORT = 8771          # 8765 dashboard, 8768 Supervisor Board, 8769 bot editor, 8771 this, 8772 physics editor
MODES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"]
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png",
        ".qmode": "text/plain; charset=utf-8", ".qlogic": "text/plain; charset=utf-8", ".txt": "text/plain; charset=utf-8",
        ".ico": "image/x-icon"}

# ---------------------------------------------------------------- the shared file rule (web/js/qfile.js says the same)
MODE_KEYS = ("custommode", "mode", "timelimit", "scorelimit", "rounds", "maps")
PHYSICS_KEYS = ("gravity", "speed", "jump", "knockback", "dash", "grapple", "selfdamage")
PHYSICS_PREFIXES = ("move_", "move.")
UNDECIDED_KEYS = ("weapons", "weapon", "health", "instagib", "vampire", "regen")
GAME_KEYS = set(MODE_KEYS + PHYSICS_KEYS + UNDECIDED_KEYS + ("label",))
LOGIC_LINE = re.compile(r"^logic [0-9]+[ \t]*$")


def key_of(line):
    """The key as the game's CustomRules::fromText reads a line: everything before the first '=' (None: no '=')."""
    line = line[:-1] if line.endswith("\r") else line
    i = line.find("=")
    return None if i < 0 else line[:i]


def owner_of(key):
    if key is None: return "none"
    if key in MODE_KEYS: return "mode"
    if key in PHYSICS_KEYS or key.startswith(PHYSICS_PREFIXES): return "physics"
    if key in UNDECIDED_KEYS: return "undecided"
    if key == "label": return "game"
    return "unknown"


def split_qmode(text):
    """-> (key lines before `logic`, logic lines from `logic` on)."""
    lines = text.split("\n")
    if text.endswith("\n"): lines.pop()
    for i, l in enumerate(lines):
        if LOGIC_LINE.match(l.rstrip("\r")): return lines[:i], lines[i:]
    return lines, []


def foreign_lines(text):
    """The lines this editor must never change, in order: every key line it doesn't own and every non-key line."""
    keys, _ = split_qmode(text)
    return [l.rstrip("\r") for l in keys if owner_of(key_of(l)) != "mode"]


def logic_key_clash(text):
    """A line after `logic` that an older game (which reads every line as key=value) would take as a key."""
    _, logic = split_qmode(text)
    for n, l in enumerate(logic):
        k = key_of(l)
        if k is not None and (k in GAME_KEYS or k.startswith(PHYSICS_PREFIXES)): return n + 1, k
    return None


def valid_name(n):
    return isinstance(n, str) and bool(NAME_RE.match(n)) and ".." not in n


def sha256(data):
    return hashlib.sha256(data).hexdigest()


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


def repo_roots(explicit=None):
    """Checkouts to look in for build/ and tools/modes/: this file's repository, the one install.py copied from, and
    mode_editor.json's "repo"."""
    roots = []
    if explicit: roots.append(os.path.abspath(explicit))
    up = os.path.normpath(os.path.join(HERE, "..", ".."))
    if os.path.isfile(os.path.join(up, "CMakeLists.txt")): roots.append(up)
    inst = os.path.join(HERE, "installed_from.json")
    if os.path.isfile(inst):
        try:
            with open(inst, encoding="utf-8") as f: r = json.load(f).get("repo")
            if r and os.path.isdir(r): roots.append(os.path.abspath(r))
        except (OSError, ValueError):
            pass
    out = []
    for r in roots:
        if r not in out: out.append(r)
    return out


def find_binary(name, explicit, roots):
    if explicit:
        return os.path.abspath(explicit) if os.path.isfile(explicit) else None
    for root in roots:
        for b in ("build", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin", "."):
            c = os.path.join(root, b, name + EXE)
            if os.path.isfile(c): return c
    c = os.path.join(HERE, name + EXE)
    if os.path.isfile(c): return c
    return shutil.which(name)


# ---------------------------------------------------------------- what the binaries can do (cached per binary version)
CAPS_LOCK = threading.Lock()
CAPS_MEM = {}


def _caps_key(path):
    st = os.stat(path)
    return "%s|%d|%d" % (path, st.st_size, int(st.st_mtime))


def binary_has(path, words):
    """Which of `words` appear as NUL-terminated strings in the binary. (An unknown quark_tests command runs the whole
    test suite, so a command is only ever run when its name is in the binary.)"""
    if not path: return {w: False for w in words}
    try: key = _caps_key(path) + "|" + ",".join(words)
    except OSError: return {w: False for w in words}
    with CAPS_LOCK:
        if key in CAPS_MEM: return CAPS_MEM[key]
    res = {w: False for w in words}
    try:
        with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
            for w in words: res[w] = m.find(w.encode() + b"\x00") >= 0
    except (OSError, ValueError):
        pass
    with CAPS_LOCK: CAPS_MEM[key] = res
    return res


def maptool_commands(path):
    """`quark_maptool help` -> its command names (a JSON answer: {commands: [{name, help}]})."""
    if not path: return None
    try: key = _caps_key(path) + "|help"
    except OSError: return None
    with CAPS_LOCK:
        if key in CAPS_MEM: return CAPS_MEM[key]
    names = None
    try:
        r = subprocess.run([path, "help"], input="", capture_output=True, text=True, timeout=15, creationflags=NO_WINDOW)
        doc = parse_answer(r.stdout)
        cmds = doc.get("commands") if isinstance(doc, dict) else None
        if isinstance(cmds, list):
            names = [c.get("name") if isinstance(c, dict) else str(c) for c in cmds]
            names = [n for n in names if isinstance(n, str)]
    except (OSError, subprocess.TimeoutExpired):
        names = None
    with CAPS_LOCK: CAPS_MEM[key] = names
    return names


def parse_answer(out):
    """A helper's answer: one JSON object, or NDJSON whose last line is the answer."""
    try: return json.loads(out)
    except ValueError: pass
    for line in reversed((out or "").strip().splitlines()):
        try: return json.loads(line)
        except ValueError: continue
    return {"raw": (out or "")[-8000:]}


class Config:
    def __init__(self, args):
        cfg = {}
        path = os.path.join(HERE, "mode_editor.json")
        if os.path.isfile(path):
            try:
                with open(path, encoding="utf-8") as f: cfg = json.load(f)
            except Exception as ex:
                print("mode_editor.json ignored: %s" % ex)
        g = lambda k, a=None: a if a not in (None, [], "") else cfg.get(k)
        self.port = int(g("port", args.port) or DEFAULT_PORT)
        self.data_dir = os.path.abspath(g("dataDir", args.data_dir) or default_data_dir())
        self.roots = repo_roots(g("repo"))
        self.maptool = find_binary("quark_maptool", g("maptool", args.maptool), self.roots)
        self.quark_tests = find_binary("quark_tests", g("quarkTests", args.quark_tests), self.roots)
        self.helper_args = [a for a in (cfg.get("helperArgs") or []) if isinstance(a, str)]

    def modes_dir(self): return os.path.join(self.data_dir, "custom_modes")
    def state_dir(self): return os.path.join(self.data_dir, "mode_editor", "state")
    def user_maps_dir(self): return os.path.join(self.data_dir, "maps")

    def tests_caps(self): return binary_has(self.quark_tests, ["exportmode", "checkmode"])

    def maptool_caps(self):
        cmds = maptool_commands(self.maptool) or []
        return {"commands": cmds, "modesim": "modesim" in cmds, "modebatch": "modebatch" in cmds, "modecheck": "modecheck" in cmds}

    def modes_source_dir(self):
        for r in self.roots:
            d = os.path.join(r, "tools", "modes")
            if os.path.isfile(os.path.join(d, "common.qlogic")): return d
        d = os.path.join(HERE, "modes")   # (install.py copies tools/modes/*.qlogic here)
        if os.path.isfile(os.path.join(d, "common.qlogic")): return d
        return None

    def default_maps_dir(self):
        for r in self.roots:
            for d in (os.path.join(r, "maps"), os.path.join(r, "build", "maps")):
                if os.path.isdir(d) and any(f.endswith(".qmap") for f in os.listdir(d)): return d
        return None


CFG = None

# ---------------------------------------------------------------- exportmode (catalog + built-in texts)
EXPORT_LOCK = threading.Lock()
EXPORT_MEM = {}


def exportmode():
    """quark_tests exportmode <out.json>, once per binary version. None when the build has no such command."""
    qt = CFG.quark_tests
    if not qt or not CFG.tests_caps()["exportmode"]: return None
    key = _caps_key(qt)
    with EXPORT_LOCK:
        if key in EXPORT_MEM: return EXPORT_MEM[key]
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "modes.json")
            try:
                r = subprocess.run([qt, "exportmode", out], capture_output=True, text=True, timeout=120, creationflags=NO_WINDOW)
                doc = None
                if r.returncode == 0 and os.path.isfile(out):
                    with open(out, encoding="utf-8") as f: doc = json.load(f)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                doc = None
        EXPORT_MEM[key] = doc
        return doc


def export_texts(doc):
    """The built-in texts from an exportmode answer, as {name: text}; accepts {defaults|texts: {name: text | {text}}}
    or a list of {name, text}."""
    src = doc.get("defaults") or doc.get("texts") if isinstance(doc, dict) else None
    out = {}
    if isinstance(src, dict):
        for k, v in src.items():
            t = v.get("text") if isinstance(v, dict) else v
            if isinstance(t, str): out[k] = t
    elif isinstance(src, list):
        for e in src:
            if isinstance(e, dict) and isinstance(e.get("name"), str) and isinstance(e.get("text"), str): out[e["name"]] = e["text"]
    return out


def default_texts():
    doc = exportmode()
    if doc:
        t = export_texts(doc)
        if "common" in t: return "game", t
    d = CFG.modes_source_dir()
    source = "repo"
    if not d:
        d, source = os.path.join(WEB, "fixtures"), "fixtures"
    out = {}
    for name in ["common"] + MODES:
        p = os.path.join(d, name + ".qlogic")
        if os.path.isfile(p):
            with open(p, encoding="utf-8", errors="replace") as f: out[name] = f.read()
    return source, out


# ---------------------------------------------------------------- files

def atomic_write(path, data, backup=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if backup and os.path.isfile(path):
        try: shutil.copyfile(path, path + ".bak")
        except OSError: pass
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-", suffix=".part")
    with os.fdopen(fd, "wb") as f: f.write(data)
    os.replace(tmp, path)


def list_modes():
    d = CFG.modes_dir()
    res = []
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.endswith(".qmode") and valid_name(f[:-6]):
                st = os.stat(os.path.join(d, f))
                res.append({"name": f[:-6], "size": st.st_size, "mtime": st.st_mtime})
    return res


def read_mode(name):
    p = os.path.join(CFG.modes_dir(), name + ".qmode")
    with open(p, "rb") as f: data = f.read()
    return p, data, os.stat(p).st_mtime


def list_maps():
    out = []
    for src, d in (("default", CFG.default_maps_dir()), ("user", CFG.user_maps_dir())):
        if not d or not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".qmap") or not valid_name(f[:-5]): continue
            info = {"name": f[:-5], "source": src, "for": "", "modes": []}
            try:
                with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fh:
                    for i, line in enumerate(fh):
                        if i > 60: break
                        kw, _, rest = line.split("#", 1)[0].strip().partition(" ")
                        if kw == "for": info["for"] = rest.strip()
                        elif kw == "modes": info["modes"] = rest.split()
            except OSError:
                pass
            out.append(info)
    return out


# ---------------------------------------------------------------- provenance (who made which mode)
PROVENANCE = os.environ.get("QME_PROVENANCE") or os.path.join(HERE, "provenance.json")   # (the env var: tests only)
PROV_LOCK = threading.Lock()


def update_provenance(name, prov):
    """Merge this save's item into provenance.json next to the tool (docs/PROVENANCE.md):
    {"format":1,"tool":"mode-editor","items":{"gamemodes/<name>":{"origin","by","date","tool","note"}}}.
    Every other item (and unknown field) is kept. prov = {"human": bool, "note": str}."""
    human = bool(prov.get("human"))
    note = str(prov.get("note") or "").strip()[:300] or ("written by hand in the gamemode editor" if human else "made in the gamemode editor (not marked as written by hand)")
    entry = {"origin": "human" if human else "ai", "by": "q", "date": time.strftime("%Y-%m-%d"), "tool": "mode-editor", "note": note}
    with PROV_LOCK:
        doc = {}
        if os.path.isfile(PROVENANCE):
            try:
                with open(PROVENANCE, encoding="utf-8") as f: doc = json.load(f)
            except (OSError, ValueError):
                doc = {}
        if not isinstance(doc, dict): doc = {}
        doc["format"] = 1
        doc["tool"] = "mode-editor"
        items = doc.get("items") if isinstance(doc.get("items"), dict) else {}
        key = "gamemodes/" + name
        old = items.get(key) if isinstance(items.get(key), dict) else {}
        items[key] = dict(old, **entry)
        doc["items"] = dict(sorted(items.items()))
        atomic_write(PROVENANCE, (json.dumps(doc, indent=1) + "\n").encode("utf-8"), backup=False)
        return {key: items[key]}


# ---------------------------------------------------------------- checkmode

def check_mode(data):
    caps = CFG.tests_caps()
    if CFG.quark_tests and caps["checkmode"]:
        with tempfile.TemporaryDirectory() as td:
            f = os.path.join(td, "check.qmode")
            with open(f, "wb") as fh: fh.write(data)
            r = subprocess.run([CFG.quark_tests, "checkmode", f], capture_output=True, text=True, timeout=60, creationflags=NO_WINDOW)
            try: res = json.loads(r.stdout)
            except ValueError: res = parse_answer(r.stdout)
            return {"available": True, "via": "checkmode", "code": r.returncode, "result": res}
    if CFG.maptool and CFG.maptool_caps()["modecheck"]:
        r = subprocess.run([CFG.maptool] + CFG.helper_args + ["modecheck"], input=json.dumps({"text": data.decode("utf-8", "replace")}),
                           capture_output=True, text=True, timeout=60, creationflags=NO_WINDOW)
        return {"available": True, "via": "modecheck", "code": r.returncode, "result": parse_answer(r.stdout)}
    return {"available": False}


# ---------------------------------------------------------------- runs (quark_maptool modesim)

class Run:
    """One helper process whose stdout JSON lines are kept (a page that reconnects gets them from the start)."""
    MAX_LINES = 400000

    def __init__(self, cmd, request):
        self.id = uuid.uuid4().hex[:12]
        self.lines, self.cond, self.done, self.code, self.err = [], threading.Condition(), False, None, ""
        self.started = time.time()
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     bufsize=1, text=True, encoding="utf-8", errors="replace", creationflags=NO_WINDOW)
        self.streaming = "--stream" in cmd
        try:
            self.proc.stdin.write(json.dumps(request) + "\n")
            self.proc.stdin.flush()
            if not self.streaming: self.proc.stdin.close()
        except (OSError, ValueError):
            pass
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
RUN_COMMANDS = ("modesim", "modebatch")
HELPER_COMMANDS = ("modesim", "modebatch", "modecheck")


def resolve_map(request):
    """A run's map named by the page: one of the user's maps (this editor's data folder, which may not be the one the
    helper would look in) goes to the helper as its path; a shipped map's name is left for the helper to find."""
    if isinstance(request, dict) and isinstance(request.get("map"), str) and valid_name(request["map"]):
        p = os.path.join(CFG.user_maps_dir(), request["map"] + ".qmap")
        if os.path.isfile(p): request = dict(request, map=p)
    return request


def helper_cmd(args, allowed):
    if not CFG.maptool or not args or args[0] not in allowed: return None
    if not all(isinstance(a, str) and re.match(r"^[A-Za-z0-9_.=:,+-]{1,64}$", a) for a in args): return None
    if args[0] not in (CFG.maptool_caps()["commands"] or []): return None
    return [CFG.maptool] + CFG.helper_args + list(args)


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkModeEditor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        if os.environ.get("QME_VERBOSE"): sys.stderr.write("%s\n" % (fmt % a))

    def _host_ok(self):
        host = (self.headers.get("Host") or "").strip().lower()
        if host.startswith("["): host = host[1:].split("]", 1)[0]
        else: host = host.rsplit(":", 1)[0]
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

    def _err(self, code, msg, **more):
        d = {"error": msg}
        d.update(more)
        self._send(code, d)

    def _body(self):
        """The request body, read once. (Every POST reads it before answering, even a refusal: with keep-alive an
        unread body would be taken for the next request.)"""
        if getattr(self, "_raw", None) is None:
            n = int(self.headers.get("Content-Length") or 0)
            if n > MAX_BODY:
                self.close_connection = True
                raise ValueError("body too large")
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
            if p == "/api/ping": return self._send(200, {"ok": True, "tool": "gamemode-editor"}, extra={"Access-Control-Allow-Origin": "*"})
            if p == "/api/config": return self._send(200, self._config())
            if p == "/api/catalog":
                doc = exportmode()
                if doc:
                    cat = doc.get("catalog") if isinstance(doc.get("catalog"), dict) else doc
                    return self._send(200, {"source": "game", "catalog": cat})
                return self._send(200, {"source": "builtin", "why": "quark_tests not found" if not CFG.quark_tests else "this quark_tests has no exportmode"})
            if p == "/api/defaults":
                src, texts = default_texts()
                return self._send(200, {"source": src, "texts": texts, "missing": [m for m in ["common"] + MODES if m not in texts]})
            if p == "/api/modes":
                return self._send(200, {"builtin": MODES, "user": list_modes(), "dir": CFG.modes_dir()})
            if p == "/api/mode":
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                try: path, data, mt = read_mode(n)
                except FileNotFoundError: return self._err(404, "no such mode")
                return self._send(200, {"name": n, "path": path, "text": data.decode("utf-8", "replace"), "mtime": mt, "sha256": sha256(data)})
            if p == "/api/maps": return self._send(200, {"maps": list_maps()})
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                path = os.path.join(CFG.state_dir(), n + ".json")
                if not os.path.isfile(path): return self._send(200, "null")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            if p == "/api/modesim/status": return self._send(200, self._modesim_status())
            m = re.match(r"^/api/run/([0-9a-f]{12})/events$", p)
            if m: return self._events(m.group(1), int(q.get("from", "0") or 0))
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_POST(self):
        self._raw = None
        try: self._body()
        except ValueError: return self._err(413, "body too large")
        if not self._write_ok(): return self._err(403, "writes need the X-Quark-Editor header from the page")
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/api/mode": return self._save_mode(q.get("name", ""))
            if p == "/api/checkmode": return self._send(200, check_mode(self._body()))
            if p == "/api/provenance":   # a download without a save: record it all the same
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                return self._send(200, {"ok": True, "provenance": update_provenance(n, self._json_body())})
            if p == "/api/state":   # the page's work between sessions (the shell's browser storage starts empty)
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                body = self._body()
                json.loads(body.decode("utf-8") or "null")
                atomic_write(os.path.join(CFG.state_dir(), n + ".json"), body, backup=False)
                return self._send(200, {"ok": True})
            if p == "/api/helper":
                body = self._json_body()
                cmd = helper_cmd(body.get("args") or [], HELPER_COMMANDS)
                if not cmd: return self._err(501, "quark_maptool with that command not found")
                r = subprocess.run(cmd, input=json.dumps(resolve_map(body.get("request") or {})), capture_output=True, text=True,
                                   timeout=min(7200.0, float(body.get("timeout") or 300)), creationflags=NO_WINDOW)
                return self._send(200, {"code": r.returncode, "answer": parse_answer(r.stdout), "stderr": r.stderr[-4000:]})
            if p == "/api/run":
                body = self._json_body()
                cmd = helper_cmd(body.get("args") or [], RUN_COMMANDS)
                if not cmd: return self._err(501, "quark_maptool modesim not found")
                run = Run(cmd, resolve_map(body.get("request") or {}))
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
                if not re.match(r"^(pause|resume|step|stop|speed ([0-9.]{1,8}|max)|realtime (on|off)|seek [0-9.]{1,12})$", line):
                    return self._err(400, "bad control line")
                return self._send(200, {"ok": run.control(line)})
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except subprocess.TimeoutExpired:
            self._err(504, "helper timed out")
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_PUT(self): return self.do_POST()

    # --- parts
    def _save_mode(self, name):
        """POST /api/mode?name=N {text, baseSha256, baseMtime, provenance, overwrite}.
        409 when the file changed (or appeared, or went) on disk since the page loaded it; 400 when the text would
        change a line this editor doesn't own, or put a key line after `logic`."""
        if not valid_name(name): return self._err(400, "bad name (letters, digits, _ . -)")
        if name in MODES: return self._err(400, "'%s' is a built-in mode's name: pick another" % name)
        body = self._json_body()
        text = body.get("text")
        if not isinstance(text, str) or not text.strip(): return self._err(400, "empty mode")
        data = text.encode("utf-8")
        if len(data) > MAX_MODE: return self._err(400, "too big (1 MB at most)")
        path = os.path.join(CFG.modes_dir(), name + ".qmode")
        base_sha = body.get("baseSha256")
        overwrite = bool(body.get("overwrite"))
        disk = None
        if os.path.isfile(path):
            with open(path, "rb") as f: disk = f.read()
        if base_sha:
            if disk is None:
                return self._err(409, "%s.qmode is gone from disk since you opened it (deleted or renamed): nothing written" % name, changed=True)
            if sha256(disk) != base_sha:
                st = os.stat(path)
                return self._err(409, "%s.qmode changed on disk since you opened it (modified %s; another editor or program saved it): nothing written"
                                 % (name, time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime))), changed=True, mtime=st.st_mtime)
        elif disk is not None and not overwrite:
            return self._err(409, "%s.qmode already exists" % name, exists=True)
        # the shared file rule, checked again here: lines this editor doesn't own stay exactly as they were
        if disk is not None and not overwrite:
            before, after = foreign_lines(disk.decode("utf-8", "replace")), foreign_lines(text)
            if before != after:
                diff = next((i for i in range(max(len(before), len(after))) if i >= len(before) or i >= len(after) or before[i] != after[i]), 0)
                return self._err(400, "refused: the save would change a line this editor doesn't own (line %d of the others': %r -> %r)"
                                 % (diff + 1, before[diff] if diff < len(before) else None, after[diff] if diff < len(after) else None))
        clash = logic_key_clash(text)
        if clash: return self._err(400, "refused: logic line %d would read as the key '%s' in older games" % clash)
        atomic_write(path, data)
        st = os.stat(path)
        prov = body.get("provenance")
        items = update_provenance(name, prov) if isinstance(prov, dict) else None
        try: check = check_mode(data)
        except (OSError, subprocess.TimeoutExpired) as ex: check = {"available": True, "error": str(ex)}
        return self._send(200, {"ok": True, "path": path, "mtime": st.st_mtime, "sha256": sha256(data), "provenance": items, "check": check})

    def _modesim_status(self):
        if not CFG.maptool:
            return {"available": False, "maptool": None, "reason": "quark_maptool wasn't found, so there is no simulation."}
        caps = CFG.maptool_caps()
        if not caps["modesim"]:
            return {"available": False, "maptool": CFG.maptool, "commands": caps["commands"],
                    "reason": "This quark_maptool has no modesim command yet (it comes with the sim helper, tools/mode_editor/native/)."}
        return {"available": True, "maptool": CFG.maptool, "batch": caps["modebatch"], "commands": caps["commands"]}

    def _config(self):
        tc = CFG.tests_caps()
        mc = CFG.maptool_caps() if CFG.maptool else {"modesim": False, "modebatch": False, "modecheck": False}
        return {"dataDir": CFG.data_dir, "modesDir": CFG.modes_dir(), "maptool": CFG.maptool, "quarkTests": CFG.quark_tests,
                "exportmode": tc["exportmode"], "checkmode": tc["checkmode"] or mc["modecheck"], "modesim": mc["modesim"],
                "modebatch": mc["modebatch"], "repos": CFG.roots, "modesSource": CFG.modes_source_dir(), "platform": sys.platform}

    def _static(self, rel):
        rel = rel.split("?", 1)[0]
        full = os.path.normpath(os.path.join(WEB, rel))
        if not full.startswith(os.path.normpath(WEB) + os.sep) or not os.path.isfile(full):
            return self._err(404, "not found")
        with open(full, "rb") as f: data = f.read()
        self._send(200, data, MIME.get(os.path.splitext(full)[1].lower(), "application/octet-stream"))

    def _events(self, rid, start):
        run = RUNS.get(rid)
        if not run: return self._err(404, "no such run")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        i = max(0, start)
        while True:
            with run.cond:
                while i >= len(run.lines) and not run.done:
                    run.cond.wait(timeout=10)
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


def main():
    global CFG
    ap = argparse.ArgumentParser(description="Quark gamemode editor (local server)")
    ap.add_argument("--port", type=int, help="default 8771")
    ap.add_argument("--data-dir", help="the game's user data folder (default: QUARK_DATA_DIR, else %%APPDATA%%\\quark or ~/.local/share/quark)")
    ap.add_argument("--maptool", help="path to quark_maptool (default: build/ of this repository, then PATH)")
    ap.add_argument("--quark-tests", help="path to quark_tests (exportmode / checkmode)")
    ap.add_argument("--no-open", "--no-browser", dest="no_open", action="store_true",
                    help="don't open a browser (also when QUARK_SHELL=1: the Supervisor Board shows the page)")
    args = ap.parse_args()
    CFG = Config(args)
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", CFG.port), Handler)
    except OSError as ex:
        print("can't listen on 127.0.0.1:%d (%s): is the gamemode editor already running? (another port: --port or mode_editor.json)" % (CFG.port, ex))
        sys.exit(1)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % CFG.port
    tc = CFG.tests_caps()
    print("Quark Gamemode Editor on %s" % url)
    print("  modes        %s" % CFG.modes_dir())
    print("  quark_tests  %s" % (("%s (exportmode %s, checkmode %s)" % (CFG.quark_tests, "yes" if tc["exportmode"] else "no", "yes" if tc["checkmode"] else "no")) if CFG.quark_tests else "(not found: the built-in catalog and the page's own checks)"))
    print("  maptool      %s" % (CFG.maptool or "(not found: Sim is unavailable)"))
    print("  built-in texts: %s" % (CFG.modes_source_dir() or "(no tools/modes yet: the page's fixtures)"))
    sys.stdout.flush()
    if not args.no_open and os.environ.get("QUARK_SHELL") != "1":
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
