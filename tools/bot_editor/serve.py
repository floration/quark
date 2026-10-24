#!/usr/bin/env python3
"""Quark Bot Editor: the local server behind the bot editor and simulator page.

    python3 tools/bot_editor/serve.py [--port 8769] [--data-dir DIR] [--maptool PATH] [--quark-tests PATH]
                                     [--maps-dir DIR ...] [--no-browser]
    tools/bot_editor/run.sh   (Linux / macOS)        tools\\bot_editor\\run.cmd   (Windows)
    --no-open (or QUARK_SHELL=1): no browser; the project manager shell starts this and shows http://127.0.0.1:8769/

Python 3 standard library only. Binds 127.0.0.1 only. It:
  - serves the page (web/),
  - lists the default maps (the build's maps/ folder, or generates them with tools/make_maps.py into .cache/maps)
    and the user's maps (the game's user maps folder, where the map editor saves, plus any --maps-dir),
  - lists, reads and writes brains (<data dir>/bots/<name>.qbrain; the default brain is tools/bots/default.qbrain,
    or web/fixtures/default.qbrain until that exists) and scenarios (<data dir>/bot_editor/scenarios/<name>.json),
  - runs the game's helper (quark_maptool: one JSON request on stdin, one JSON answer on stdout) per request,
  - runs live matches (`quark_maptool bots --stream`): its JSON lines are relayed to the page as Server-Sent Events
    and the page's control lines (pause / resume / speed <x> / step / stop) are written to its stdin,
  - runs `quark_tests checkbrain <file>` / `quark_tests exportbrain <file>` when that binary is there.

Settings come from (later wins): built-in defaults, bot_editor.json next to this file, then the command line.
bot_editor.json keys: port, dataDir, maptool, quarkTests, mapsDirs (list), defaultMapsDir, defaultBrain, helperArgs.

Every request must come from the page itself: Host must be 127.0.0.1 / localhost, and every write carries the header
X-Quark-Editor (a page on another site can't send it without a CORS preflight, which is never allowed).
"""
import argparse, json, os, queue, re, select, shutil, socket, subprocess, sys, tempfile, threading, time, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
WEB = os.path.join(HERE, "web")
CACHE = os.path.join(HERE, ".cache")
IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")   # brain / scenario / map names: no slashes, no "..", no leading dot
MAX_BODY = 8 << 20
DEFAULT_PORT = 8769          # fixed (the project manager shell opens http://127.0.0.1:8769/); bot_editor.json / --port change it
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png",
        ".qbrain": "text/plain; charset=utf-8", ".qmap": "text/plain; charset=utf-8", ".txt": "text/plain; charset=utf-8",
        ".md": "text/plain; charset=utf-8", ".ico": "image/x-icon"}


def valid_name(n):
    return isinstance(n, str) and bool(NAME_RE.match(n)) and ".." not in n


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


def find_binary(name, explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    cands = []
    for b in ("build", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin", "."):
        cands.append(os.path.join(ROOT, b, name + EXE))
    cands.append(os.path.join(HERE, name + EXE))
    for c in cands:
        if os.path.isfile(c): return c
    return shutil.which(name)


class Config:
    def __init__(self, args):
        cfg = {}
        path = os.path.join(HERE, "bot_editor.json")
        if os.path.isfile(path):
            try:
                with open(path, encoding="utf-8") as f: cfg = json.load(f)
            except Exception as ex:
                print("bot_editor.json ignored: %s" % ex)
        g = lambda k, a=None: a if a not in (None, [], "") else cfg.get(k)
        self.port = int(g("port", args.port) or DEFAULT_PORT)
        self.data_dir = os.path.abspath(g("dataDir", args.data_dir) or default_data_dir())
        self.maptool = find_binary("quark_maptool", g("maptool", args.maptool))
        self.quark_tests = find_binary("quark_tests", g("quarkTests", args.quark_tests))
        self.helper_args = list(cfg.get("helperArgs") or [])
        self.extra_maps = [os.path.abspath(d) for d in (g("mapsDirs", args.maps_dir) or [])]
        dm = g("defaultMapsDir")
        self.default_maps = os.path.abspath(dm) if dm else None
        db = g("defaultBrain")
        self.default_brain = os.path.abspath(db) if db else None

    def default_maps_dir(self):
        if self.default_maps and os.path.isdir(self.default_maps): return self.default_maps
        for d in (os.path.join(ROOT, "maps"), os.path.join(ROOT, "build", "maps"), os.path.join(CACHE, "maps")):
            if os.path.isdir(d) and any(f.endswith(".qmap") for f in os.listdir(d)): return d
        return None

    def user_maps_dir(self): return os.path.join(self.data_dir, "maps")
    def bots_dir(self): return os.path.join(self.data_dir, "bots")
    def scenarios_dir(self): return os.path.join(self.data_dir, "bot_editor", "scenarios")
    def state_dir(self): return os.path.join(self.data_dir, "bot_editor", "state")   # autosave, settings, drafts (JSON)

    def default_brain_path(self):
        for p in (self.default_brain, os.path.join(ROOT, "tools", "bots", "default.qbrain"), os.path.join(WEB, "fixtures", "default.qbrain")):
            if p and os.path.isfile(p): return p
        return None


CFG = None
GEN_LOCK = threading.Lock()


def generate_default_maps():
    """No build maps/ folder: make the shipped maps with tools/make_maps.py (deterministic, python only)."""
    with GEN_LOCK:
        if CFG.default_maps_dir(): return CFG.default_maps_dir(), ""
        out = os.path.join(CACHE, "maps")
        os.makedirs(out, exist_ok=True)
        gen = os.path.join(ROOT, "tools", "make_maps.py")
        if not os.path.isfile(gen): return None, "tools/make_maps.py not found"
        r = subprocess.run([sys.executable, gen, "--out", out], capture_output=True, text=True)
        if r.returncode != 0: return None, (r.stderr or r.stdout)[-2000:]
        return out, r.stdout.strip()


def map_header(path):
    """title / for / modes / handmade from the first lines of a .qmap (no full parse; the page parses the text)."""
    info = {"title": "", "for": "", "modes": [], "handmade": False}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f):
                if i > 60: break
                line = line.split("#", 1)[0].strip()
                if not line: continue
                kw, _, rest = line.partition(" ")
                if kw == "title": info["title"] = rest.strip()
                elif kw == "for": info["for"] = rest.strip()
                elif kw == "modes": info["modes"] = rest.split()
                elif kw == "handmade": info["handmade"] = True
    except OSError:
        pass
    return info


def list_maps():
    out, seen = [], set()
    dirs = []
    dm = CFG.default_maps_dir()
    if dm: dirs.append(("default", dm))
    dirs.append(("user", CFG.user_maps_dir()))
    for d in CFG.extra_maps: dirs.append(("extra", d))
    for src, d in dirs:
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".qmap"): continue
            name = f[:-5]
            if not valid_name(name): continue
            p = os.path.join(d, f)
            st = os.stat(p)
            m = {"name": name, "source": src, "dir": d, "size": st.st_size, "mtime": st.st_mtime}
            m.update(map_header(p))
            m["shadows"] = src != "default" and ("default", name) in seen
            seen.add((src, name))
            out.append(m)
    return out


def map_path(name, source):
    if not valid_name(name): return None
    for m in list_maps():
        if m["name"] == name and m["source"] == source:
            return os.path.join(m["dir"], name + ".qmap")
    return None


def list_files(d, ext):
    if not os.path.isdir(d): return []
    res = []
    for f in sorted(os.listdir(d)):
        if f.endswith(ext) and valid_name(f[:-len(ext)]):
            st = os.stat(os.path.join(d, f))
            res.append({"name": f[:-len(ext)], "size": st.st_size, "mtime": st.st_mtime})
    return res


def atomic_write(path, data, backup=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if backup and os.path.isfile(path):
        try: shutil.copyfile(path, path + ".bak")
        except OSError: pass
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-", suffix=".part")
    with os.fdopen(fd, "wb") as f: f.write(data)
    os.replace(tmp, path)


# ---------------------------------------------------------------- provenance (who made which behaviour)

PROVENANCE = os.path.join(HERE, "provenance.json")
AREA_RE = re.compile(r"^(perception|aim|weapons|fight-movement|roam|targeting|mode/[a-z0-9]{1,16})$")
PROV_LOCK = threading.Lock()


def update_provenance(name, prov):
    """Merge this save's items into provenance.json next to the tool:
    {"format":1,"tool":"bot-editor","items":{"bots/<area>":{"origin","by","date","note"}}}. Other items are kept.
    prov = {"areas": [area ids changed vs the default brain], "human": bool, "note": str}."""
    human = bool(prov.get("human"))
    note = str(prov.get("note") or "").strip()[:300]
    if not note:
        note = "behaviour written by hand in the bot editor" if human else "numbers tuned in the bot editor"
    entry = {"origin": "human" if human else "ai", "by": "q", "date": time.strftime("%Y-%m-%d"), "note": note}
    areas = [a for a in (prov.get("areas") or []) if isinstance(a, str) and AREA_RE.match(a)]
    with PROV_LOCK:
        doc = {}
        if os.path.isfile(PROVENANCE):
            try:
                with open(PROVENANCE, encoding="utf-8") as f: doc = json.load(f)
            except (OSError, ValueError):
                doc = {}
        if not isinstance(doc, dict): doc = {}
        doc["format"] = 1
        doc["tool"] = "bot-editor"
        items = doc.get("items") if isinstance(doc.get("items"), dict) else {}
        changed = {}
        for a in areas: changed["bots/" + a] = dict(entry)
        changed["bots/brain/" + name] = dict(entry, note=note + " (saved as %s.qbrain)" % name)
        items.update(changed)
        doc["items"] = dict(sorted(items.items()))
        atomic_write(PROVENANCE, (json.dumps(doc, indent=1) + "\n").encode("utf-8"), backup=False)
        return changed


# ---------------------------------------------------------------- live runs (quark_maptool bots --stream)

class Run:
    """One helper process whose stdout JSON lines are kept (so a page that reconnects gets them from the start).
    It lives only while a page watches it: once its last event stream has been gone ORPHAN_GRACE seconds (a reload, a
    closed tab), or no stream ever came, it is stopped, so a long real-time run or a batch on every core doesn't go on
    for nobody. The grace covers the page's own reconnects (EventSource retries after 0.5 s)."""
    MAX_LINES = 400000
    ORPHAN_GRACE = 5.0

    def __init__(self, cmd, request):
        self.id = uuid.uuid4().hex[:12]
        self.lines, self.cond, self.done, self.code, self.err = [], threading.Condition(), False, None, ""
        self.started = time.time()
        self.clients, self.left, self.orphaned = 0, time.time(), False   # left: when the last stream went (or the start)
        flags = subprocess.CREATE_NO_WINDOW if IS_WIN else 0
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     bufsize=1, text=True, encoding="utf-8", errors="replace", creationflags=flags)
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()
        # without --stream the helper reads its request until stdin closes (and takes no controls)
        self.streaming = "--stream" in cmd
        if not self.streaming: self.proc.stdin.close()
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()
        self._watch()   # (the page opens its stream right after the POST: none by then, it went away first)

    def attach(self):
        with self.cond: self.clients += 1

    def detach(self):
        with self.cond:
            self.clients -= 1
            self.left = time.time()
            if self.clients <= 0: self._watch()

    def _watch(self):
        def check():   # (a later detach has its own timer: only the one a full grace after the last detach acts)
            with self.cond: gone = self.clients <= 0 and not self.done and time.time() - self.left >= self.ORPHAN_GRACE - 0.05
            if gone:
                self.orphaned = True
                self.stop()
        t = threading.Timer(self.ORPHAN_GRACE, check)
        t.daemon = True
        t.start()

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
        if not self.streaming: return False
        if self.done or self.proc.poll() is not None: return False
        try:
            self.proc.stdin.write(line.strip() + "\n")
            self.proc.stdin.flush()
            return True
        except (OSError, ValueError):
            return False

    def stop(self):
        if not self.control("stop") and self.proc.poll() is None and not self.streaming: self.proc.terminate()
        def killer():
            try: self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired: self.proc.kill()
        threading.Thread(target=killer, daemon=True).start()


RUNS = {}
RUNS_LOCK = threading.Lock()


def parse_answer(out):
    """A helper's answer: one JSON object, or NDJSON whose last line is the answer (simcheck, sim without --stream)."""
    try: return json.loads(out)
    except ValueError: pass
    for line in reversed(out.strip().splitlines()):
        try: return json.loads(line)
        except ValueError: continue
    return {"raw": out[-8000:]}


def helper_cmd(args):
    if not CFG.maptool: return None
    if not all(isinstance(a, str) and re.match(r"^[A-Za-z0-9_.=:,+-]{1,64}$", a) for a in args): return None
    return [CFG.maptool] + CFG.helper_args + list(args)


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkBotEditor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        if os.environ.get("QBE_VERBOSE"): sys.stderr.write("%s\n" % (fmt % a))

    # --- helpers
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

    def _err(self, code, msg): self._send(code, {"error": msg})

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY: raise ValueError("body too large")
        return self.rfile.read(n) if n else b""

    def _json_body(self):
        b = self._body()
        return json.loads(b.decode("utf-8")) if b else {}

    def _write_ok(self):
        return self._host_ok() and self.headers.get("X-Quark-Editor") == "1"

    # --- verbs
    def do_GET(self):
        if not self._host_ok(): return self._err(403, "bad host")
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/" or p == "/index.html": return self._static("index.html")
            if p.startswith("/web/"): return self._static(p[5:])
            if p == "/api/config": return self._send(200, self._config())
            if p == "/api/maps": return self._send(200, {"maps": list_maps(), "defaultDir": CFG.default_maps_dir()})
            if p == "/api/map":
                path = map_path(q.get("name", ""), q.get("source", "default"))
                if not path: return self._err(404, "no such map")
                with open(path, encoding="utf-8", errors="replace") as f: return self._send(200, f.read(), MIME[".qmap"])
            if p == "/api/brains":
                d = CFG.default_brain_path()
                return self._send(200, {"default": {"path": d, "exists": bool(d)}, "user": list_files(CFG.bots_dir(), ".qbrain"),
                                        "dir": CFG.bots_dir()})
            if p == "/api/brain":
                n = q.get("name", "")
                if n == "default" and q.get("source") != "user":
                    d = CFG.default_brain_path()
                    if not d: return self._err(404, "no default brain yet (tools/bots/default.qbrain)")
                    path = d
                else:
                    if not valid_name(n): return self._err(400, "bad name")
                    path = os.path.join(CFG.bots_dir(), n + ".qbrain")
                if not os.path.isfile(path): return self._err(404, "no such brain")
                with open(path, encoding="utf-8", errors="replace") as f: return self._send(200, f.read(), MIME[".qbrain"])
            if p == "/api/scenarios": return self._send(200, {"scenarios": list_files(CFG.scenarios_dir(), ".json"), "dir": CFG.scenarios_dir()})
            if p == "/api/scenario":
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                path = os.path.join(CFG.scenarios_dir(), n + ".json")
                if not os.path.isfile(path): return self._err(404, "no such scenario")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            if p == "/api/ping": return self._send(200, {"ok": True, "tool": "bot-editor"}, extra={"Access-Control-Allow-Origin": "*"})
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                path = os.path.join(CFG.state_dir(), n + ".json")
                if not os.path.isfile(path): return self._send(200, "null")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            if p == "/api/exportbrain":
                if not CFG.quark_tests: return self._err(501, "quark_tests not found")
                with tempfile.TemporaryDirectory() as td:
                    out = os.path.join(td, "brain.json")
                    r = subprocess.run([CFG.quark_tests, "exportbrain", out], capture_output=True, text=True, timeout=120)
                    if r.returncode != 0 or not os.path.isfile(out): return self._err(500, (r.stderr or r.stdout)[-2000:])
                    with open(out, encoding="utf-8") as f: return self._send(200, f.read())
            m = re.match(r"^/api/run/([0-9a-f]{12})/events$", p)
            if m: return self._events(m.group(1), int(q.get("from", "0") or 0))
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_POST(self):
        if not self._write_ok(): return self._err(403, "writes need the X-Quark-Editor header from the page")
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/api/quit":   # install.py after an update: this process still runs the old files (it starts the new ones)
                self._send(200, {"ok": True})
                threading.Thread(target=self.server.shutdown, daemon=True).start()
                return
            if p == "/api/brain":
                n = q.get("name", "")
                if not valid_name(n) or n == "default": return self._err(400, "bad name (letters, digits, _ . -; not 'default')")
                data = self._body()
                prov = None
                if (self.headers.get("Content-Type") or "").startswith("application/json"):
                    body = json.loads(data.decode("utf-8"))
                    data = (body.get("text") or "").encode("utf-8")
                    prov = body.get("provenance")
                if not data.strip(): return self._err(400, "empty brain")
                atomic_write(os.path.join(CFG.bots_dir(), n + ".qbrain"), data)
                items = update_provenance(n, prov) if prov is not None else None
                return self._send(200, {"ok": True, "path": os.path.join(CFG.bots_dir(), n + ".qbrain"), "provenance": items})
            if p == "/api/state":   # the page's work between sessions (the shell's browser storage starts empty)
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                body = self._body()
                json.loads(body.decode("utf-8") or "null")   # (must be JSON)
                atomic_write(os.path.join(CFG.state_dir(), n + ".json"), body, backup=False)
                return self._send(200, {"ok": True})
            if p == "/api/provenance":   # an export (download) without a save: record it all the same
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                return self._send(200, {"ok": True, "provenance": update_provenance(n, self._json_body())})
            if p == "/api/scenario":
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                body = self._json_body()
                atomic_write(os.path.join(CFG.scenarios_dir(), n + ".json"), json.dumps(body, indent=1).encode("utf-8"))
                return self._send(200, {"ok": True})
            if p == "/api/generate-maps":
                d, msg = generate_default_maps()
                return self._send(200 if d else 500, {"dir": d, "log": msg})
            if p == "/api/checkbrain":
                data = self._body()
                if not CFG.quark_tests:
                    # quark_maptool simcheck: compiles the text the way a sim run would (when the build has brains)
                    cmd = helper_cmd(["simcheck"])
                    if not cmd: return self._send(200, {"available": False})
                    r = subprocess.run(cmd, input=json.dumps({"text": data.decode("utf-8", "replace")}), capture_output=True,
                                       text=True, timeout=60)
                    return self._send(200, {"available": True, "via": "simcheck", "code": r.returncode, "result": parse_answer(r.stdout)})
                with tempfile.TemporaryDirectory() as td:
                    f = os.path.join(td, "check.qbrain")
                    with open(f, "wb") as fh: fh.write(data)
                    r = subprocess.run([CFG.quark_tests, "checkbrain", f], capture_output=True, text=True, timeout=60)
                    try: res = json.loads(r.stdout)
                    except ValueError: res = {"raw": r.stdout[-4000:], "stderr": r.stderr[-2000:]}
                    return self._send(200, {"available": True, "code": r.returncode, "result": res})
            if p == "/api/helper":
                body = self._json_body()
                cmd = helper_cmd(body.get("args") or [])
                if not cmd: return self._err(501, "quark_maptool not found (or bad args)")
                r = subprocess.run(cmd, input=json.dumps(body.get("request") or {}), capture_output=True, text=True,
                                   timeout=float(body.get("timeout") or 300))
                return self._send(200, {"code": r.returncode, "answer": parse_answer(r.stdout), "stderr": r.stderr[-4000:]})
            if p == "/api/run":
                body = self._json_body()
                cmd = helper_cmd(body.get("args") or ["bots", "--stream"])
                if not cmd: return self._err(501, "quark_maptool not found (or bad args)")
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
    def _config(self):
        return {"dataDir": CFG.data_dir, "userMaps": CFG.user_maps_dir(), "bots": CFG.bots_dir(),
                "scenarios": CFG.scenarios_dir(), "defaultMaps": CFG.default_maps_dir(),
                "extraMaps": CFG.extra_maps, "maptool": CFG.maptool, "quarkTests": CFG.quark_tests,
                "defaultBrain": CFG.default_brain_path(), "platform": sys.platform}

    def _static(self, rel):
        rel = rel.split("?", 1)[0]
        full = os.path.normpath(os.path.join(WEB, rel))
        if not full.startswith(os.path.normpath(WEB) + os.sep) or not os.path.isfile(full):
            return self._err(404, "not found")
        ext = os.path.splitext(full)[1].lower()
        with open(full, "rb") as f: data = f.read()
        self._send(200, data, MIME.get(ext, "application/octet-stream"))

    def _client_gone(self):
        """The page closed this stream (an EventSource sends nothing after its request: readable = closed)."""
        try:
            r, _, _ = select.select([self.connection], [], [], 0)
            return bool(r) and self.connection.recv(1, socket.MSG_PEEK) == b""
        except (OSError, ValueError):
            return True

    def _events(self, rid, start):
        run = RUNS.get(rid)
        if not run: return self._err(404, "no such run")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        run.attach()
        try:
            i, quiet = max(0, start), time.time()
            while True:
                with run.cond:
                    if i >= len(run.lines) and not run.done: run.cond.wait(timeout=1.0)   # (a paused run: look at the page every second)
                    batch = run.lines[i:]
                    done = run.done and i + len(batch) >= len(run.lines)
                if batch:
                    # one SSE message per batch: lines joined with \n data: (the page splits them)
                    payload = "".join("data: %s\n" % l for l in batch) + "\n"
                    self.wfile.write(payload.encode("utf-8"))
                    self.wfile.flush()
                    i += len(batch)
                    quiet = time.time()
                elif self._client_gone():
                    return
                elif time.time() - quiet >= 10:
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
                    quiet = time.time()
                if done:
                    info = json.dumps({"type": "_exit", "code": run.code, "stderr": run.err[-2000:]})
                    self.wfile.write(("event: exit\ndata: %s\n\n" % info).encode("utf-8"))
                    self.wfile.flush()
                    return
        finally:
            run.detach()


def main():
    global CFG
    ap = argparse.ArgumentParser(description="Quark bot editor and simulator (local server)")
    ap.add_argument("--port", type=int)
    ap.add_argument("--data-dir", help="the game's user data folder (default: QUARK_DATA_DIR, else %%APPDATA%%\\quark or ~/.local/share/quark)")
    ap.add_argument("--maptool", help="path to quark_maptool (default: build/ of this repository, then PATH)")
    ap.add_argument("--quark-tests", help="path to quark_tests (checkbrain / exportbrain)")
    ap.add_argument("--maps-dir", action="append", help="another folder of .qmap files to list (repeatable)")
    ap.add_argument("--no-browser", "--no-open", dest="no_browser", action="store_true",
                    help="don't open a browser (also when QUARK_SHELL=1: the project manager shows the page)")
    args = ap.parse_args()
    CFG = Config(args)
    if not CFG.default_maps_dir():   # (in the background: GET / answers at once; the page asks again when it's done)
        print("no built maps/ folder: generating the default maps with tools/make_maps.py in the background ...")
        threading.Thread(target=lambda: print("  maps: %s" % (generate_default_maps()[1] or "done")), daemon=True).start()
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", CFG.port), Handler)
    except OSError as ex:
        print("can't listen on 127.0.0.1:%d (%s): is the bot editor already running? (set another port with --port or bot_editor.json)" % (CFG.port, ex))
        sys.exit(1)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % CFG.port
    print("Quark Bot Editor on %s" % url)
    print("  data dir     %s" % CFG.data_dir)
    print("  default maps %s" % CFG.default_maps_dir())
    print("  maptool      %s" % (CFG.maptool or "(not found: live runs use the built-in mock)"))
    print("  quark_tests  %s" % (CFG.quark_tests or "(not found: brain checks are the page's own)"))
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
