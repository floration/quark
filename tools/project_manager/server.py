#!/usr/bin/env python3
"""Quark project manager server: http://127.0.0.1:8768 (tools/project_manager/SPEC.md section 2).

    py -3 server.py [--quark C:\\Users\\user\\Desktop\\Quark] [--port 8768]
    py -3 server.py --init        write project-manager\\tabs.json / config.json if missing, then exit (setup.ps1)

Python 3 standard library only. Binds 127.0.0.1 only. Every source refreshes in the background on its own interval
and requests are answered from memory. Endpoints: GET /  /static/...  /api/pm[?refresh=1]  /api/tabs
/api/history?limit=N  /api/health;  POST /api/refresh?source=<key|all>.
"""
import argparse
import json
import os
import re
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from pm import config, scheduler, tabs, util  # noqa: E402
from pm.history import History  # noqa: E402

DEFAULT_PORT = 8768
ALLOWED_ORIGIN = re.compile(r"^http://(?:127\.0\.0\.1|localhost):(?:876[5-9]|8770)$")
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon",
         ".woff2": "font/woff2", ".txt": "text/plain; charset=utf-8", ".map": "application/json; charset=utf-8"}
FALLBACK_PAGE = (b"<!doctype html><meta charset=utf-8><title>Quark Project</title>"
                 b"<p>The PM page (web/index.html) is not installed next to server.py. The API is up: "
                 b"<a href=/api/pm>/api/pm</a>, <a href=/api/tabs>/api/tabs</a>.</p>")


class App:
    def __init__(self, paths, port=DEFAULT_PORT, modules=scheduler.MODULES, prober=tabs.probe):
        self.paths = paths
        self.port = port
        self.history = History(paths)
        self.store = scheduler.Store(paths, self.history, modules)
        self.tabs = tabs.Tabs(paths, own_port=port, prober=prober)
        self._tabs_cache = None
        self._tabs_lock = threading.Lock()
        self.stop = self.store.stop

    def start(self):
        self.store.start()
        threading.Thread(target=self._tabs_loop, name="pm-tabs", daemon=True).start()

    def _tabs_loop(self):
        while not self.stop.is_set():
            try:
                self.tabs.probe_all()
                r = self.tabs.resolved()
                with self._tabs_lock:
                    self._tabs_cache = r
            except Exception as e:  # (never let the probe thread die)
                util.log("tabs: %s" % type(e).__name__)
            self.tabs.wake.wait(tabs.PROBE_INTERVAL)
            self.tabs.wake.clear()

    def tabs_view(self):
        with self._tabs_lock:
            r = self._tabs_cache
        return r if r is not None else self.tabs.resolved()  # (first moments: resolved without `up`)

    def pm(self, refresh=False):
        out = {"generatedAt": self.store.generated_at, "sources": self.store.results(), "derived": self.store.derived,
               "history": self.history.latest(30), "tabs": self.tabs_view()["tabs"]}
        if refresh:
            ok, wait = self.store.refresh_all()
            out["refresh"] = {"accepted": ok, "nextAllowedIn": wait}
        out["quark"] = self.paths.quark
        return out


class Handler(BaseHTTPRequestHandler):
    server_version = "quark-pm"
    protocol_version = "HTTP/1.1"
    app = None  # set by make_server

    def log_message(self, fmt, *args):  # (quiet; and under pythonw there is no stderr)
        pass

    # -- helpers ------------------------------------------------------------------------------------------------
    def _host_ok(self):
        host = (self.headers.get("Host") or "").lower()
        port = str(self.server.server_address[1])
        return host in ("127.0.0.1", "localhost", "127.0.0.1:" + port, "localhost:" + port)

    def _origin_ok(self):
        o = self.headers.get("Origin")
        return o is None or bool(ALLOWED_ORIGIN.match(o)) or o == "http://127.0.0.1:%d" % self.server.server_address[1]

    def _send(self, code, body, ctype="application/json; charset=utf-8", cache="no-store", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        self.send_header("X-Content-Type-Options", "nosniff")
        o = self.headers.get("Origin")
        if o and (ALLOWED_ORIGIN.match(o) or o == "http://127.0.0.1:%d" % self.server.server_address[1]):
            self.send_header("Access-Control-Allow-Origin", o)
            self.send_header("Vary", "Origin")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False, separators=(",", ":")))

    def _static(self, rel):
        web = os.path.realpath(self.app.paths.web_dir)
        rel = urllib.parse.unquote(rel).replace("\\", "/").lstrip("/")
        if not rel or any(p in ("..", "") for p in rel.split("/")) or ":" in rel:
            return self._send(404, "not found", "text/plain; charset=utf-8")
        path = os.path.realpath(os.path.join(web, *rel.split("/")))
        if os.path.commonpath([web, path]) != web or not os.path.isfile(path):
            return self._send(404, "not found", "text/plain; charset=utf-8")
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            return self._send(404, "not found", "text/plain; charset=utf-8")
        self._send(200, body, TYPES.get(os.path.splitext(path)[1].lower(), "application/octet-stream"), "no-cache")

    def _icon(self):
        for p in (os.path.join(HERE, "quark.ico"), os.path.join(HERE, "..", "..", "assets", "quark.ico")):
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    return self._send(200, f.read(), "image/x-icon", "max-age=3600")
        self._send(404, "not found", "text/plain; charset=utf-8")

    # -- methods ------------------------------------------------------------------------------------------------
    def do_OPTIONS(self):
        if not self._host_ok() or not self._origin_ok():
            return self._send(403, "forbidden", "text/plain; charset=utf-8")
        self._send(204, b"", "text/plain", extra={"Access-Control-Allow-Methods": "GET, POST, OPTIONS",
                                                  "Access-Control-Allow-Headers": "Content-Type",
                                                  "Access-Control-Max-Age": "600"})

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain; charset=utf-8")
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        p = u.path
        try:
            if p in ("/", "/index.html"):
                path = os.path.join(self.app.paths.web_dir, "index.html")
                if os.path.isfile(path):
                    return self._static("index.html")
                return self._send(200, FALLBACK_PAGE, "text/html; charset=utf-8")
            if p.startswith("/static/"):
                return self._static(p[len("/static/"):])
            if p == "/api/health":
                return self._send(200, "ok", "text/plain; charset=utf-8")
            if p == "/api/pm":
                return self._json(self.app.pm(refresh=q.get("refresh", ["0"])[0] not in ("", "0", "false")))
            if p == "/api/tabs":
                if q.get("refresh", ["0"])[0] not in ("", "0", "false"):
                    self.app.tabs.wake.set()
                return self._json(self.app.tabs_view())
            if p == "/api/history":
                limit = util.to_int(q.get("limit", ["100"])[0], 100)
                return self._json({"events": self.app.history.latest(max(1, min(limit, 10000)))})
            if p in ("/favicon.ico", "/quark.ico"):
                return self._icon()
            # (the page's own files, also at the top level: /pm.css, /pm.js)
            if p.count("/") == 1 and re.match(r"^/[\w.\-]+\.(?:css|js|json|svg|png|ico|html)$", p):
                return self._static(p[1:])
            return self._send(404, "not found", "text/plain; charset=utf-8")
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        if not self._host_ok() or not self._origin_ok():
            return self._send(403, "forbidden", "text/plain; charset=utf-8")
        n = util.to_int(self.headers.get("Content-Length"), 0) or 0
        if n:
            self.rfile.read(min(n, 65536))
        u = urllib.parse.urlsplit(self.path)
        if u.path != "/api/refresh":
            return self._send(404, "not found", "text/plain; charset=utf-8")
        src = urllib.parse.parse_qs(u.query).get("source", ["all"])[0]
        if src == "all":
            ok, wait = self.app.store.refresh_all()
        else:
            res = self.app.store.refresh(src)
            if res is None:
                return self._json({"accepted": False, "error": "unknown source"}, 400)
            ok, wait = res
        self._json({"accepted": ok, "source": src, "nextAllowedIn": wait})


def make_server(app, port):
    class Server(ThreadingHTTPServer):
        daemon_threads = True
        # (on Windows SO_REUSEADDR would let a second copy bind the same port; one server per port)
        allow_reuse_address = os.name != "nt"

        def handle_error(self, request, client_address):  # (no traceback printing: pythonw has no stderr)
            util.log("request from %s failed: %s" % (client_address[0], sys.exc_info()[0].__name__))

    handler = type("BoundHandler", (Handler,), {"app": app})
    return Server(("127.0.0.1", port), handler)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Quark project manager server (127.0.0.1 only)")
    ap.add_argument("--quark", help="the Quark folder (default: the parent of the folder holding server.py)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--init", action="store_true", help="write tabs.json / config.json if missing, then exit")
    args = ap.parse_args(argv)
    paths = config.Paths(HERE, args.quark)
    if args.init:
        wrote = [n for n, f in (("tabs.json", tabs.write_default), ("config.json", config.write_default_config)) if f(paths)]
        util.log("project-manager folder %s: %s" % (paths.pm_dir, ("wrote " + ", ".join(wrote)) if wrote else "kept existing files"))
        return 0
    app = App(paths, args.port)
    try:
        httpd = make_server(app, args.port)
    except OSError as e:
        util.log("cannot listen on 127.0.0.1:%d (%s); is it already running?" % (args.port, e.strerror or e))
        return 2
    app.start()
    util.log("serving http://127.0.0.1:%d/ for %s" % (args.port, paths.quark))
    try:
        httpd.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        app.store.shutdown()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
