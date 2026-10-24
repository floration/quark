#!/usr/bin/env python3
"""Quark Map Editor server: serves the page on http://127.0.0.1:<port>/ and runs quark_maptool for it.

    python3 serve.py [--port N] [--maps DIR] [--game EXE] [--no-browser]

Binds 127.0.0.1 only (nothing on the network can reach it). Python 3 standard library only.

    GET  /                          the page (and its files from this folder: index.html, editor.css, js/, vendor/, models/)
    GET  /api/info                  version, helper found, folders, game exe, deco scripts, player models
    GET  /api/config  PUT /api/config      {"gameExe", "userMaps"} (kept in config.json next to this file)
    GET  /api/maps                  {"defaults": [...], "mine": [...]}
    GET  /api/map/default/<name>    a default map's text (this folder's maps/, read-only)
    GET  /api/map/mine/<name>       PUT /api/map/mine/<name>     your map's text in the game's user maps folder (PUT:
                                    the text, or JSON {"text", "provenance": {"origin": "human"|"ai", "note"}}, which
                                    also records it in provenance.json next to this file)
    GET  /api/provenance            provenance.json
    GET  /api/sidecar/<name>        PUT /api/sidecar/<name>      <name>.mapedit.json next to your map ({} when none)
    GET  /api/deco-presets          PUT /api/deco-presets/<name>  the decoration panel's own presets ({"presets": [...]}),
                                    deco_presets/user/<name>.json in this folder
    POST /api/tool/<command>        quark_maptool <command> with the body JSON on stdin: its JSON answer
    POST /api/stream/bots           quark_maptool bots --stream: NDJSON lines as they come (chunked); the first line
                                    is {"type":"run","run":"<id>"}
    POST /api/botctl/<id>           body {"line": "pause|resume|speed X|step|stop"}: sent to that run's stdin
    POST /api/launch                {"what": "editor"|"play", "name", "mode"}: starts the game (detached)
"""
import json, os, re, secrets, socket, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG = os.path.join(HERE, "config.json")
COMMANDS = {"parse", "serialize", "check", "metrics", "route", "bots", "custom", "help"}
MODES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"]
TIMEOUTS = {"parse": 30, "serialize": 30, "check": 300, "metrics": 300, "route": 60, "custom": 300, "bots": 900, "help": 10}
MAX_BODY = 64 << 20
NAME_RE = re.compile(r"^[A-Za-z0-9_-]{1,40}$")
WIN_DEVICES = {"con", "prn", "aux", "nul"} | {"com%d" % i for i in range(10)} | {"lpt%d" % i for i in range(10)}
STATIC = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
          ".json": "application/json", ".gltf": "model/gltf+json", ".png": "image/png", ".svg": "image/svg+xml",
          ".ico": "image/x-icon", ".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8"}


def list_deco_presets():
    """the decoration presets saved from the panel: deco_presets/user/<name>.json (bad files are skipped)"""
    d = os.path.join(HERE, "deco_presets", "user")
    out = []
    for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not n.endswith(".json") or not valid_name(n[:-5]): continue
        try:
            with open(os.path.join(d, n), encoding="utf-8") as fh: obj = json.load(fh)
        except (OSError, ValueError): continue
        if isinstance(obj, dict) and isinstance(obj.get("rules"), list): out.append(dict(obj, file=n[:-5]))
    return out


def valid_name(n):
    """The game's validMapName: 1-40 of [A-Za-z0-9_-], not a Windows device name."""
    return bool(n and NAME_RE.match(n)) and n.lower() not in WIN_DEVICES


def defaults_dir():
    """The default maps: this folder's maps/ (build.py copies them), else the repository's (running from the source)."""
    d = os.path.join(HERE, "maps")
    if os.path.isdir(d): return d
    return os.path.normpath(os.path.join(HERE, "..", "..", "maps"))


def default_user_maps():
    d = os.environ.get("QUARK_DATA_DIR")
    if d: return os.path.join(d, "maps")
    if sys.platform == "win32":
        return os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "quark", "maps")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/quark/maps")
    xdg = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(xdg, "quark", "maps")


def find_maptool():
    for n in ("quark_maptool.exe", "quark_maptool") if sys.platform == "win32" else ("quark_maptool",):
        for d in (HERE, os.path.join(HERE, "..", "..", "build")):
            p = os.path.normpath(os.path.join(d, n))
            if os.path.isfile(p): return p
    return None


def guess_game():
    names = ["quark.exe"] if sys.platform == "win32" else ["quark"]
    dirs = [os.path.join(HERE, ".."), os.path.join(HERE, "..", "..", "build")]
    if sys.platform == "win32":
        desk = os.path.join(os.path.expanduser("~"), "Desktop", "Quark")
        dirs += [os.path.join(desk, "testing"), os.path.join(desk, "production"), desk]
    for d in dirs:
        for n in names:
            p = os.path.normpath(os.path.join(d, n))
            if os.path.isfile(p): return p
    return ""


class State:
    def __init__(self, args):
        self.cfg = {}
        try:
            with open(CONFIG, encoding="utf-8") as f: self.cfg = json.load(f)
        except Exception:
            pass
        if args.get("maps"): self.cfg["userMaps"] = args["maps"]
        if args.get("game"): self.cfg["gameExe"] = args["game"]
        self.maptool = find_maptool()
        self.runs = {}            # bot stream id -> Popen
        self.lock = threading.Lock()

    def user_maps(self):
        return os.path.abspath(self.cfg.get("userMaps") or default_user_maps())

    def game(self):
        return self.cfg.get("gameExe") or guess_game()

    def save_cfg(self):
        tmp = CONFIG + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f: json.dump(self.cfg, f, indent=1)
        os.replace(tmp, CONFIG)


def write_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp%d" % os.getpid()
    with open(tmp, "wb") as f: f.write(data)
    os.replace(tmp, path)


PROVENANCE = os.path.join(HERE, "provenance.json")
prov_lock = threading.Lock()


def read_provenance():
    try:
        with open(PROVENANCE, encoding="utf-8") as f: p = json.load(f)
        if isinstance(p, dict) and isinstance(p.get("items"), dict): return p
    except Exception:
        pass
    return {"format": 1, "tool": "map-editor", "items": {}}


def record_provenance(name, prov):
    """One entry per map in provenance.json next to this file (latest save wins): who laid the map out.
    origin "human" (started new here, or the owner says they laid it out) or "ai" (a generated default edited)."""
    origin = prov.get("origin")
    if origin not in ("human", "ai"): return None
    entry = {"origin": origin, "by": str(prov.get("by") or "q")[:40], "date": time.strftime("%Y-%m-%d"),
             "note": str(prov.get("note") or "")[:300]}
    with prov_lock:
        p = read_provenance()
        p["format"], p["tool"] = 1, "map-editor"
        p["items"]["maps/%s.qmap" % name] = entry
        write_atomic(PROVENANCE, (json.dumps(p, indent=1, sort_keys=True) + "\n").encode("utf-8"))
    return entry


def map_info(path):
    """name, title, for, handmade, size from the first lines of a .qmap."""
    title = mode = ""
    hand = False
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f):
                if i > 40: break
                s = line.split("#", 1)[0].strip()
                if s.startswith("title "): title = s[6:].strip()
                elif s.startswith("for "): mode = s[4:].strip()
                elif s == "handmade": hand = True
    except OSError:
        pass
    name = os.path.basename(path)[:-5]
    return {"name": name, "title": title, "for": mode, "handmade": hand, "size": os.path.getsize(path),
            "mtime": os.path.getmtime(path), "sidecar": os.path.isfile(os.path.join(os.path.dirname(path), name + ".mapedit.json"))}


def list_dir_maps(d):
    out = []
    if os.path.isdir(d):
        for n in sorted(os.listdir(d)):
            if n.endswith(".qmap") and valid_name(n[:-5]) and os.path.isfile(os.path.join(d, n)):
                out.append(map_info(os.path.join(d, n)))
    return out


class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkMapEditor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        if os.environ.get("QME_LOG"): sys.stderr.write("%s\n" % (fmt % a))

    # ---- helpers
    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)): body = json.dumps(body).encode("utf-8")
        elif isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def err(self, code, msg):
        self.send(code, {"error": msg})

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY: raise ValueError("request too large")
        return self.rfile.read(n) if n else b""

    def same_origin(self):
        """Only the page itself may call the API: no other site's script (CSRF / DNS rebinding)."""
        host = self.headers.get("Host", "")
        if host.split(":")[0] not in ("127.0.0.1", "localhost"): return False
        origin = self.headers.get("Origin")
        if origin and origin not in ("http://" + host,): return False
        return True

    # ---- routes
    def do_GET(self):
        path = unquote(self.path.split("?", 1)[0])
        if path.startswith("/api/"):
            if not self.same_origin(): return self.err(403, "forbidden")
            return self.api_get(path[5:])
        self.static(path)

    def do_PUT(self):
        path = unquote(self.path.split("?", 1)[0])
        if not path.startswith("/api/") or not self.same_origin(): return self.err(403, "forbidden")
        try: data = self.body()
        except ValueError as e: return self.err(413, str(e))
        self.api_put(path[5:], data)

    def do_POST(self):
        path = unquote(self.path.split("?", 1)[0])
        if not path.startswith("/api/") or not self.same_origin(): return self.err(403, "forbidden")
        try: data = self.body()
        except ValueError as e: return self.err(413, str(e))
        self.api_post(path[5:], data)

    def static(self, path):
        if path in ("", "/"): path = "/index.html"
        rel = os.path.normpath(path.lstrip("/"))
        if rel.startswith("..") or os.path.isabs(rel) or rel.split(os.sep)[0] not in (
                "index.html", "editor.css", "js", "vendor", "models", "deco_presets", "favicon.ico"):
            return self.err(404, "not found")
        full = os.path.join(HERE, rel)
        ext = os.path.splitext(full)[1].lower()
        if not os.path.isfile(full) or ext not in STATIC: return self.err(404, "not found")
        with open(full, "rb") as f: self.send(200, f.read(), STATIC[ext])

    def api_get(self, p):
        st = self.server.st
        if p == "info":
            js = os.path.join(HERE, "js")
            deco = sorted(n for n in os.listdir(js) if n.startswith("deco") and n.endswith(".js")) if os.path.isdir(js) else []
            md = os.path.join(HERE, "models", "player")
            models = sorted(n[:-5] for n in os.listdir(md) if n.endswith(".gltf")) if os.path.isdir(md) else []
            version = ""
            vf = os.path.join(HERE, "VERSION")
            if os.path.isfile(vf): version = open(vf, encoding="utf-8").read().strip()
            return self.send(200, {"version": version, "maptool": st.maptool or "", "userMaps": st.user_maps(),
                                   "defaultMaps": defaults_dir(), "gameExe": st.game(), "modes": MODES,
                                   "deco": deco, "playerModels": models, "platform": sys.platform})
        if p == "config":
            return self.send(200, {"gameExe": st.game(), "userMaps": st.user_maps()})
        if p == "provenance":
            return self.send(200, read_provenance())
        if p == "maps":
            prov = read_provenance()["items"]
            mine = list_dir_maps(st.user_maps())
            for m in mine: m["provenance"] = prov.get("maps/%s.qmap" % m["name"])
            return self.send(200, {"defaults": list_dir_maps(defaults_dir()), "mine": mine})
        m = re.match(r"^map/(default|mine)/([^/]+)$", p)
        if m:
            src, name = m.groups()
            if not valid_name(name): return self.err(400, "bad map name")
            d = defaults_dir() if src == "default" else st.user_maps()
            f = os.path.join(d, name + ".qmap")
            if not os.path.isfile(f): return self.err(404, "no map " + name)
            with open(f, "rb") as fh: return self.send(200, fh.read(), "text/plain; charset=utf-8")
        m = re.match(r"^sidecar/([^/]+)$", p)
        if m:
            name = m.group(1)
            if not valid_name(name): return self.err(400, "bad map name")
            f = os.path.join(st.user_maps(), name + ".mapedit.json")
            if not os.path.isfile(f): return self.send(200, {})
            with open(f, "rb") as fh: return self.send(200, fh.read())
        if p == "deco-presets":   # the decoration panel's own presets (deco_presets/user/<name>.json; DECO.md)
            return self.send(200, {"presets": list_deco_presets()})
        self.err(404, "not found")

    def api_put(self, p, data):
        st = self.server.st
        m = re.match(r"^deco-presets/([^/]+)$", p)
        if m:
            name = m.group(1)
            if not valid_name(name): return self.err(400, "a preset name is 1-40 letters, digits, - and _")
            try: obj = json.loads(data or b"null")
            except ValueError: return self.err(400, "bad JSON")
            if not isinstance(obj, dict) or not isinstance(obj.get("rules"), list): return self.err(400, "a preset is {name, description, rules: [...]}")
            d = os.path.join(HERE, "deco_presets", "user")
            os.makedirs(d, exist_ok=True)
            f = os.path.join(d, name + ".json")
            with open(f + ".tmp", "w", encoding="utf-8", newline="\n") as fh: json.dump(obj, fh, indent=2)
            os.replace(f + ".tmp", f)
            return self.send(200, {"ok": True, "file": name + ".json"})
        if p == "config":
            try: cfg = json.loads(data or b"{}")
            except ValueError: return self.err(400, "bad JSON")
            for k in ("gameExe", "userMaps"):
                if k in cfg:
                    v = str(cfg[k] or "").strip()
                    if v: st.cfg[k] = v
                    else: st.cfg.pop(k, None)
            st.save_cfg()
            return self.send(200, {"gameExe": st.game(), "userMaps": st.user_maps()})
        m = re.match(r"^map/mine/([^/]+)$", p)
        if m:
            name = m.group(1)
            if not valid_name(name): return self.err(400, "a map name is 1-40 letters, digits, - and _")
            if os.path.isfile(os.path.join(defaults_dir(), name + ".qmap")):
                return self.err(409, name + " is a default map's name: save your map under a name of its own")
            prov = None
            if (self.headers.get("Content-Type") or "").startswith("application/json"):
                try: obj = json.loads(data)
                except ValueError: return self.err(400, "bad JSON")
                text, prov = str(obj.get("text", "")), obj.get("provenance")
            else:
                text = data.decode("utf-8", "replace")
            text = text.replace("\r\n", "\n")
            if len(text) > 32 << 20: return self.err(413, "map too large")
            if not text.strip(): return self.err(400, "empty map")
            write_atomic(os.path.join(st.user_maps(), name + ".qmap"), text.encode("utf-8"))
            entry = record_provenance(name, prov) if isinstance(prov, dict) else None
            return self.send(200, {"ok": True, "path": os.path.join(st.user_maps(), name + ".qmap"), "provenance": entry})
        m = re.match(r"^sidecar/([^/]+)$", p)
        if m:
            name = m.group(1)
            if not valid_name(name): return self.err(400, "bad map name")
            try: obj = json.loads(data or b"{}")
            except ValueError: return self.err(400, "bad JSON")
            if not isinstance(obj, dict): return self.err(400, "the sidecar is a JSON object")
            write_atomic(os.path.join(st.user_maps(), name + ".mapedit.json"), json.dumps(obj, indent=1).encode("utf-8"))
            return self.send(200, {"ok": True})
        self.err(404, "not found")

    def api_post(self, p, data):
        st = self.server.st
        m = re.match(r"^tool/([a-z]+)$", p)
        if m:
            cmd = m.group(1)
            if cmd not in COMMANDS: return self.err(400, "unknown command")
            if not st.maptool: return self.err(503, "quark_maptool not found (build it: cmake --build build --target quark_maptool)")
            try:
                r = subprocess.run([st.maptool, cmd], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=TIMEOUTS.get(cmd, 120), cwd=HERE, creationflags=no_window())
            except subprocess.TimeoutExpired:
                return self.err(504, "quark_maptool %s took longer than %d s" % (cmd, TIMEOUTS.get(cmd, 120)))
            out = r.stdout.strip()
            if not out:
                return self.err(500, "quark_maptool %s: no answer (exit %d) %s" % (cmd, r.returncode, r.stderr.decode("utf-8", "replace")[-2000:]))
            return self.send(200 if r.returncode == 0 else 422, out)
        if p == "stream/bots":
            return self.stream_bots(data)
        m = re.match(r"^botctl/([a-f0-9]+)$", p)
        if m:
            try: line = str(json.loads(data or b"{}").get("line", ""))
            except ValueError: return self.err(400, "bad JSON")
            if not re.match(r"^(pause|resume|step|stop|speed [0-9]+(\.[0-9]+)?)$", line): return self.err(400, "bad control")
            with st.lock: proc = st.runs.get(m.group(1))
            if not proc or proc.poll() is not None: return self.err(404, "run finished")
            try:
                proc.stdin.write((line + "\n").encode()); proc.stdin.flush()
            except OSError:
                return self.err(410, "run finished")
            return self.send(200, {"ok": True})
        if p == "launch":
            return self.launch(data)
        self.err(404, "not found")

    def stream_bots(self, data):
        st = self.server.st
        if not st.maptool: return self.err(503, "quark_maptool not found")
        try: req = json.loads(data or b"{}")
        except ValueError: return self.err(400, "bad JSON")
        if not isinstance(req, dict): return self.err(400, "the request is a JSON object")
        if not data.strip(): data = b"{}"
        run = secrets.token_hex(8)
        try:
            proc = subprocess.Popen([st.maptool, "bots", "--stream"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL, cwd=HERE, creationflags=no_window())
        except OSError as e:
            return self.err(500, "could not start quark_maptool: %s" % e)
        with st.lock: st.runs[run] = proc
        deadline = time.time() + TIMEOUTS["bots"] * 4
        killer = threading.Timer(TIMEOUTS["bots"] * 4, proc.kill); killer.daemon = True; killer.start()
        try:
            # the body as the page sent it (re-encoding it would turn a -0 into 0), on one line: a raw CR or LF in
            # JSON can only be whitespace between tokens, so blanking them keeps the same JSON
            proc.stdin.write(data.replace(b"\r", b" ").replace(b"\n", b" ") + b"\n"); proc.stdin.flush()
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("X-Accel-Buffering", "no")
            self.end_headers()

            def chunk(b):
                self.wfile.write(b"%x\r\n" % len(b) + b + b"\r\n"); self.wfile.flush()

            chunk(json.dumps({"type": "run", "run": run}).encode() + b"\n")
            for line in proc.stdout:
                if time.time() > deadline: break
                chunk(line if line.endswith(b"\n") else line + b"\n")
            proc.wait(timeout=5)
            self.wfile.write(b"0\r\n\r\n"); self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError, subprocess.TimeoutExpired):
            pass  # (the page went away or stopped reading: the run stops)
        finally:
            killer.cancel()
            if proc.poll() is None:
                try:
                    proc.stdin.write(b"stop\n"); proc.stdin.flush(); proc.wait(timeout=2)
                except Exception:
                    proc.kill()
            with st.lock: st.runs.pop(run, None)
            self.close_connection = True

    def launch(self, data):
        st = self.server.st
        try: req = json.loads(data or b"{}")
        except ValueError: return self.err(400, "bad JSON")
        name, what, mode = str(req.get("name", "")), req.get("what"), str(req.get("mode") or "ffa")
        if not valid_name(name): return self.err(400, "bad map name")
        exe = st.game()
        if not exe or not os.path.isfile(exe): return self.err(400, "set the game's exe in Settings (quark.exe in your game folder)")
        if what == "editor": args = [exe, "--editor", name]
        elif what == "play":
            if mode not in MODES: return self.err(400, "unknown mode")
            args = [exe, "--host", mode, "--map", name]
        else:
            return self.err(400, "what: editor or play")
        kw = {"cwd": os.path.dirname(exe), "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
        if sys.platform == "win32": kw["creationflags"] = 0x00000008 | 0x00000200   # DETACHED_PROCESS | NEW_PROCESS_GROUP
        else: kw["start_new_session"] = True
        try: subprocess.Popen(args, **kw)
        except OSError as e: return self.err(500, "could not start the game: %s" % e)
        self.send(200, {"ok": True, "args": args})


def no_window():
    return 0x08000000 if sys.platform == "win32" else 0   # CREATE_NO_WINDOW: no console flashing up per command


def main():
    for f in (sys.stdout, sys.stderr):   # (a console or log that can't show a folder's name, e.g. C:\Users\Дмитрий: no crash)
        try: f.reconfigure(errors="replace")
        except (AttributeError, ValueError): pass
    args, a = {}, sys.argv[1:]
    i = 0
    while i < len(a):
        if a[i] in ("--port", "--maps", "--game") and i + 1 < len(a): args[a[i][2:]] = a[i + 1]; i += 2
        elif a[i] == "--no-browser": args["nobrowser"] = True; i += 1
        else: sys.exit(__doc__)
    port = int(args.get("port") or 0)
    if not port:   # a random free port
        s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    httpd.daemon_threads = True
    httpd.st = State(args)
    url = "http://127.0.0.1:%d/" % port
    print("Quark map editor: %s" % url)
    print("  helper:    %s" % (httpd.st.maptool or "quark_maptool NOT FOUND (viewing only, checks and bots off)"))
    print("  your maps: %s" % httpd.st.user_maps())
    print("  game:      %s" % (httpd.st.game() or "(not set: Settings on the page)"))
    print("Ctrl+C stops it.")
    sys.stdout.flush()
    if not args.get("nobrowser"):
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
