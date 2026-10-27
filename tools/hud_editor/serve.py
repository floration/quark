#!/usr/bin/env python3
"""Quark HUD Editor: the local server behind the HUD editor page (the Quarkology app's Editor Manager > HUD tab).

    python3 tools/hud_editor/serve.py [--port 8774] [--data-dir DIR] [--no-open]
    python3 tools/hud_editor/serve.py --install [FOLDER]     copy the tool to FOLDER (default <Quark folder>/hud-editor)
    --no-open (or QUARK_SHELL=1): no browser; the Quarkology app starts it and shows http://127.0.0.1:8774/?embed=1

Python 3 standard library only. Binds 127.0.0.1 only. It reads and writes the game's own files in its user data folder
(QUARK_DATA_DIR, else %APPDATA%\\quark, ~/Library/Application Support/quark or ~/.local/share/quark):
  - hud.cfg: the scriptable widgets (src/client/hudwidgets.*). The game looks at it once a second, so a save shows in
    a running game at once. Written whole, in HudWidgets::toText's format (the page builds the text).
  - quark.cfg: only the built-in HUD element lines (hud_<N>=x,y,scale,custom,r,g,b,hidden,z[,pitch,yaw,roll]) and
    hud_widgets / hud_verbose are rewritten; every other line stays exactly as it was. The game reads quark.cfg at
    start and writes it back when its settings change, so those edits are for when the game is closed (the page says
    so when quark.exe is running).
  - huds/<name>: HUD profiles (Settings > HUD > widgets EXPORT, and the per-mode HUD rules), listed, opened and saved.
Every write keeps the file it replaced as <file>.bak (once per server start) and is atomic (temp file + rename).
Every request must come from the page: Host must be 127.0.0.1 / localhost and every write carries X-Quark-Editor.
"""
import argparse, json, os, re, shutil, subprocess, sys, threading, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "web")
IS_WIN = os.name == "nt"
DEFAULT_PORT = 8774          # fixed (the Quarkology app opens http://127.0.0.1:8774/)
MAX_BODY = 1 << 20
PROFILE_RE = re.compile(r"^[^\\/:*?\"<>|.\x00-\x1f][^\\/:*?\"<>|\x00-\x1f]{0,127}$")   # plainFileName (src/game/map.h)
HUD_KEY_RE = re.compile(r"^hud_(\d+)$")
NUM_RE = re.compile(r"^-?\d+(\.\d+)?(e-?\d+)?$")
CFG_KEYS = ("hud_widgets", "hud_verbose")   # other quark.cfg keys the page may set
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml"}
PROGRAM_FILES = ["serve.py", "run.cmd", "run.sh", "README.md"]
BACKED_UP = set()


def default_data_dir():
    e = os.environ.get("QUARK_DATA_DIR")
    if e: return e
    if IS_WIN and os.environ.get("APPDATA"): return os.path.join(os.environ["APPDATA"], "quark")
    if sys.platform == "darwin": return os.path.expanduser("~/Library/Application Support/quark")
    x = os.environ.get("XDG_DATA_HOME")
    return os.path.join(x, "quark") if x else os.path.expanduser("~/.local/share/quark")


def default_quark():
    """Quark\\ when installed as Quark\\hud-editor\\ or run from Quark\\<clone>\\tools\\hud_editor\\, else ~/Desktop/Quark"""
    for c in (os.path.dirname(HERE), os.path.normpath(os.path.join(HERE, "..", "..", ".."))):
        try:
            if any(n.lower() == "repository" or n.lower().startswith("working-repo") for n in os.listdir(c)): return c
        except OSError:
            pass
    return os.path.join(os.path.expanduser("~"), "Desktop", "Quark")


def read_text(p):
    try:
        with open(p, "r", encoding="utf-8", errors="replace", newline="") as f: return f.read()
    except OSError:
        return None


def write_text(p, text):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if p not in BACKED_UP and os.path.isfile(p):
        try: shutil.copyfile(p, p + ".bak")
        except OSError: pass
        BACKED_UP.add(p)
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="") as f: f.write(text)
    os.replace(tmp, p)


def game_running():
    """quark.exe running (Windows only; elsewhere unknown -> False). No console window."""
    if not IS_WIN: return False
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq quark.exe", "/NH", "/FO", "CSV"], capture_output=True,
                             text=True, timeout=5, creationflags=0x08000000).stdout   # (CREATE_NO_WINDOW)
        return "quark.exe" in out.lower()
    except Exception:
        return False


def parse_cfg(text):
    """quark.cfg -> {hud: {N: "x,y,..."}, keys: {k: v}} (only what the page shows)"""
    hud, keys = {}, {}
    for line in (text or "").splitlines():
        k, eq, v = line.partition("=")
        if not eq: continue
        k = k.strip(); m = HUD_KEY_RE.match(k)
        if m: hud[m.group(1)] = v.strip()
        elif k in CFG_KEYS or k == "fov": keys[k] = v.strip()
    return {"hud": hud, "keys": keys}


def merge_cfg(text, hud, keys):
    """Rewrite only hud_<N> and CFG_KEYS lines (in place, in order); add the ones the file doesn't have at the end."""
    want = {"hud_%s" % n: v for n, v in hud.items()}
    want.update({k: v for k, v in keys.items() if k in CFG_KEYS})
    nl = "\r\n" if "\r\n" in (text or "") else "\n"
    out, seen = [], set()
    for line in (text or "").splitlines():
        k = line.partition("=")[0].strip()
        if k in want and k not in seen:
            out.append("%s=%s" % (k, want[k])); seen.add(k)
        elif k in want:
            continue          # (a duplicate line: the game reads the last one; keep one)
        else:
            out.append(line)
    out += ["%s=%s" % (k, v) for k, v in want.items() if k not in seen]
    return nl.join(out) + nl


def valid_hud_value(v):
    parts = v.split(",")
    return 7 <= len(parts) <= 16 and all(NUM_RE.match(p.strip()) for p in parts)


class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkHudEditor"
    data_dir = ""

    def log_message(self, *a): pass

    def _host_ok(self):
        h = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]").lower()
        return h in ("127.0.0.1", "localhost")

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)): body = json.dumps(body)
        if isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _err(self, code, msg): self._send(code, {"error": msg})

    def _path(self, *rel): return os.path.join(self.data_dir, *rel)

    def _profiles(self):
        d = self._path("huds")
        try: return sorted(n for n in os.listdir(d) if os.path.isfile(os.path.join(d, n)) and PROFILE_RE.match(n) and not n.endswith((".bak", ".tmp")))
        except OSError: return []

    def do_GET(self):
        if not self._host_ok(): return self._err(403, "local only")
        u = urlparse(self.path); q = parse_qs(u.query)
        if u.path == "/api/state":
            return self._send(200, {"dataDir": self.data_dir, "hudCfg": read_text(self._path("hud.cfg")),
                                    "quarkCfg": parse_cfg(read_text(self._path("quark.cfg"))),
                                    "hasQuarkCfg": os.path.isfile(self._path("quark.cfg")),
                                    "profiles": self._profiles(), "gameRunning": game_running()})
        if u.path == "/api/profile":
            n = (q.get("name") or [""])[0]
            if not PROFILE_RE.match(n): return self._err(400, "bad profile name")
            t = read_text(self._path("huds", n))
            return self._send(200, {"name": n, "text": t}) if t is not None else self._err(404, "no such profile")
        rel = "index.html" if u.path in ("/", "/index.html") else u.path.lstrip("/")
        p = os.path.normpath(os.path.join(WEB, rel))
        if not p.startswith(WEB + os.sep) or not os.path.isfile(p): return self._err(404, "not found")
        with open(p, "rb") as f: self._send(200, f.read(), MIME.get(os.path.splitext(p)[1], "application/octet-stream"))

    def do_POST(self):
        if not self._host_ok() or self.headers.get("X-Quark-Editor") != "1": return self._err(403, "writes need the X-Quark-Editor header from the page")
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY: return self._err(413, "too big")
        try: body = json.loads(self.rfile.read(n) or b"{}")
        except ValueError: return self._err(400, "bad json")
        u = urlparse(self.path)
        try:
            if u.path == "/api/hudcfg":
                text = body.get("text")
                if not isinstance(text, str): return self._err(400, "text missing")
                write_text(self._path("hud.cfg"), text)
                return self._send(200, {"ok": True})
            if u.path == "/api/profile":
                name, text = body.get("name"), body.get("text")
                if not isinstance(name, str) or not PROFILE_RE.match(name) or not isinstance(text, str): return self._err(400, "bad profile")
                write_text(self._path("huds", name), text)
                return self._send(200, {"ok": True, "profiles": self._profiles()})
            if u.path == "/api/elements":
                hud, keys = body.get("hud") or {}, body.get("keys") or {}
                if not all(isinstance(k, str) and k.isdigit() and int(k) < 32 and isinstance(v, str) and valid_hud_value(v) for k, v in hud.items()):
                    return self._err(400, "bad hud_N value")
                if not all(k in CFG_KEYS and v in ("0", "1") for k, v in keys.items()): return self._err(400, "bad key")
                p = self._path("quark.cfg")
                write_text(p, merge_cfg(read_text(p) or "", hud, keys))
                return self._send(200, {"ok": True, "gameRunning": game_running()})
        except OSError as ex:
            return self._err(500, "write failed: %s" % ex)
        self._err(404, "not found")


def install(dest):
    """Copy the program to `dest` (safe to re-run; anything else there is kept)."""
    dest = os.path.abspath(dest)
    if os.path.normcase(dest) == os.path.normcase(HERE):
        print("hud editor: already installed here (%s)" % dest); return 0
    try:
        os.makedirs(dest, exist_ok=True)
        for n in PROGRAM_FILES:
            src = os.path.join(HERE, n)
            if os.path.isfile(src): shutil.copyfile(src, os.path.join(dest, n))
        shutil.copytree(WEB, os.path.join(dest, "web"), dirs_exist_ok=True)
    except OSError as ex:
        print("hud editor: install to %s failed: %s" % (dest, ex)); return 1
    print("hud editor installed in %s" % dest)
    return 0


def main():
    ap = argparse.ArgumentParser(description="Quark HUD editor (local server)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--data-dir", help="the game's user data folder (default: QUARK_DATA_DIR, else %%APPDATA%%\\quark or ~/.local/share/quark)")
    ap.add_argument("--no-browser", "--no-open", dest="no_browser", action="store_true")
    ap.add_argument("--install", nargs="?", const="", metavar="FOLDER")
    args = ap.parse_args()
    if args.install is not None:
        sys.exit(install(args.install or os.path.join(default_quark(), "hud-editor")))
    Handler.data_dir = os.path.abspath(args.data_dir or default_data_dir())
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    except OSError as ex:
        print("can't listen on 127.0.0.1:%d (%s): is the HUD editor already running?" % (args.port, ex)); sys.exit(1)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % args.port
    print("Quark HUD Editor on %s (data %s)" % (url, Handler.data_dir)); sys.stdout.flush()
    if not args.no_browser and os.environ.get("QUARK_SHELL") != "1":
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == "__main__":
    main()
