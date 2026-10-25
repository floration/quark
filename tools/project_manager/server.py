#!/usr/bin/env python3
"""Supervisor Board server (the Quark project manager): http://127.0.0.1:8768 (tools/project_manager/SPEC.md section 2).

    py -3 server.py [--quark C:\\Users\\user\\Desktop\\Quark] [--port 8768]
    py -3 server.py [--no-update]  no automatic installs / updates of the tools (pm/updater.py, installs.json)
    py -3 server.py --init        write project-manager\\tabs.json / config.json if missing (and bring an older
                                  tabs.json's default tabs up to date), then exit (setup.ps1)

Python 3 standard library only. Binds 127.0.0.1 only. Every source refreshes in the background on its own interval
and requests are answered from memory. Endpoints: GET /  /static/...  /api/pm[?refresh=1][&raw=1] (ETag: a 304 when
nothing changed; raw=1 keeps the siblings' raw data)  /api/tabs  /api/history?limit=N  /api/health  /api/updates
/api/updates/log?id=<tool>  /api/checklist (sections with items, ticks);  POST /api/refresh?source=<key|all>
/api/updates/run[?id=<tool>]  /api/updates/ack?id=<restart request id>  /api/checklist (JSON {version, section, done}:
q's release-checklist ticks).
The Play tab (pm/play.py): GET /play  /api/play[?refresh=1]  /api/play/pending;  POST (JSON body) /api/play/launch
{build, args}  /api/play/note {session, did, noticed, change, other}  /api/play/skip {session}  /api/play/install {tag}
/api/play/remove {tag}  /api/play/sync; and the routes pm/play.py answers itself (Play.api_get / api_post, SPEC.md 3b).
"""
import argparse
import hashlib
import inspect
import json
import os
import re
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from pm import checklist, config, derive, play, scheduler, tabs, updater, util  # noqa: E402
from pm.history import History  # noqa: E402

DEFAULT_PORT = 8768
ALLOWED_ORIGIN = re.compile(r"^http://(?:127\.0\.0\.1|localhost):(?:876[5-9]|877[0-5])$")  # (the tools' ports: 8765-8775)
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon",
         ".woff2": "font/woff2", ".txt": "text/plain; charset=utf-8", ".map": "application/json; charset=utf-8"}
SLIM_SOURCES = ("docs", "github", "editors")  # (/api/pm leaves out their raw data: `derived` holds what the page shows)
LOG_ID = re.compile(r"^[\w.\-]{1,64}$")
LOG_MAX = 256 * 1024
FALLBACK_PAGE = (b"<!doctype html><meta charset=utf-8><title>Supervisor Board</title>"
                 b"<p>The Supervisor Board page (web/index.html) is not installed next to server.py. The API is up: "
                 b"<a href=/api/pm>/api/pm</a>, <a href=/api/tabs>/api/tabs</a>.</p>")


class App:
    def __init__(self, paths, port=DEFAULT_PORT, modules=scheduler.MODULES, prober=tabs.probe, update=False,
                 play_opts=None):
        self.paths = paths
        self.port = port
        self.history = History(paths)
        self.store = scheduler.Store(paths, self.history, modules)
        self.tabs = tabs.Tabs(paths, own_port=port, prober=prober)
        self._tabs_cache = None
        self._tabs_lock = threading.Lock()
        self.stop = self.store.stop
        # (off unless asked for: main() turns it on, --no-update and the tests leave it off)
        self.updater = updater.Updater(paths, enabled=update)
        self.play = play.Play(paths, **(play_opts or {}))
        self.ticks = checklist.Ticks(paths)

    def start(self):
        self.store.start()
        self.updater.start()
        self.play.start()
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

    def can_retry_one(self):
        """Whether this updater can run one tool's install (Updater.request(only=<id>))."""
        try:
            return "only" in inspect.signature(self.updater.request).parameters
        except (TypeError, ValueError):
            return False

    @staticmethod
    def slim_sources(results):
        """The sources as the page needs them: no raw sibling data (docs alone is ~50 KB of entry bodies and recent
        changes), no checklist items (GET /api/checklist has them). Each envelope keeps ok / kept / times / error."""
        out = {}
        for k, r in results.items():
            if k in SLIM_SOURCES and r.get("data") is not None:
                r = dict(r, data={}, omitted=True)
            elif k == "checklist" and isinstance(r.get("data"), dict) and "sections" in r["data"]:
                d = {kk: v for kk, v in r["data"].items() if kk != "sections"}
                d["itemCounts"] = [len(x.get("items") or []) for x in r["data"]["sections"] or []]
                r = dict(r, data=d)
            out[k] = r
        return out

    def checklist_view(self):
        """GET /api/checklist: the checklist's sections with their items, and q's ticks for the testing version."""
        res = self.store.results()
        chk = derive.shown_data(res, "checklist") or {}
        version = derive.versions(res).get("testing")
        return {"version": version, "mtime": chk.get("mtime"), "url": chk.get("url"), "sections": chk.get("sections") or [],
                "ticks": self.ticks.view(version)}

    def install_log(self, eid):
        """GET /api/updates/log?id=<tool id>: the end of Quark\\project-manager\\logs\\install-<id>.log (None: no log)."""
        if not isinstance(eid, str) or not LOG_ID.match(eid):
            return None
        path = self.paths.pm("logs", "install-%s.log" % eid)
        try:
            with open(path, "rb") as f:
                f.seek(0, os.SEEK_END)
                size = f.tell()
                f.seek(max(0, size - LOG_MAX))
                text = f.read().decode("utf-8", errors="replace")
        except OSError:
            return None
        return ("[... the first %d KB left out ...]\n" % ((size - LOG_MAX) // 1024) if size > LOG_MAX else "") + text

    def pm(self, refresh=False, raw=False):
        upd = dict(self.updater.view(), canRetryOne=self.can_retry_one())
        d = derive.with_updates(self.store.derived, upd)  # (+ failed installs: halting)
        d = derive.with_checklist(d, self.ticks.view((d.get("versions") or {}).get("testing")))
        # serverNow: the page's clock (generatedAt is the last derive, up to a source interval old)
        out = {"generatedAt": self.store.generated_at, "serverNow": int(time.time() * 1000),
               "sources": self.store.results() if raw else self.slim_sources(self.store.results()), "derived": d,
               "history": self.history.latest(30), "tabs": self.tabs_view()["tabs"], "updates": upd,
               "play": self.play.summary()}
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
            if p in ("/play", "/play.html"):
                return self._static("play.html")
            if p.startswith("/static/"):
                return self._static(p[len("/static/"):])
            if p == "/api/play/pending":
                return self._json(self.app.play.pending())
            if p == "/api/play":
                if q.get("refresh", ["0"])[0] not in ("", "0", "false"):
                    self.app.play.releases(force=True)
                return self._json(self.app.play.view(rel_sig=q.get("rel", [None])[0]))
            if p.startswith("/api/play/"):  # (the newer Play routes live in pm/play.py: log)
                code, obj = self.app.play.api_get(p[len("/api/play/"):], q)
                return self._json(obj, code)
            if p == "/api/health":
                return self._send(200, "ok", "text/plain; charset=utf-8")
            if p == "/api/pm":
                return self._pm(q)
            if p == "/api/checklist":
                return self._json(self.app.checklist_view())
            if p == "/api/updates/log":
                text = self.app.install_log(q.get("id", [""])[0])
                if text is None:
                    return self._send(404, "no log for that tool", "text/plain; charset=utf-8")
                return self._send(200, text, "text/plain; charset=utf-8")
            if p == "/api/tabs":
                if q.get("refresh", ["0"])[0] not in ("", "0", "false"):
                    self.app.tabs.wake.set()
                return self._json(self.app.tabs_view())
            if p == "/api/updates":
                return self._json(self.app.updater.view())
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

    def _pm(self, q):
        """/api/pm with an ETag over everything but serverNow (sent as X-Server-Now too): a poll with nothing new is a
        304 with no body, and the page keeps what it shows."""
        flag = lambda k: q.get(k, ["0"])[0] not in ("", "0", "false")
        out = self.app.pm(refresh=flag("refresh"), raw=flag("raw"))
        now = out.pop("serverNow")
        core = json.dumps(out, ensure_ascii=False, separators=(",", ":"))
        etag = '"%s"' % hashlib.sha1(core.encode("utf-8")).hexdigest()[:20]
        extra = {"ETag": etag, "X-Server-Now": str(now)}
        if self.headers.get("If-None-Match") == etag:
            return self._send(304, b"", extra=extra)
        self._send(200, core[:-1] + ',"serverNow":%d}' % now, extra=extra)

    def do_POST(self):
        if not self._host_ok() or not self._origin_ok():
            self.close_connection = True  # (its body is never read)
            return self._send(403, "forbidden", "text/plain; charset=utf-8")
        n = util.to_int(self.headers.get("Content-Length"), 0) or 0
        if n > 65536:
            self.close_connection = True
            return self._send(413, "too big", "text/plain; charset=utf-8")
        body = self.rfile.read(n) if n > 0 else b""
        u = urllib.parse.urlsplit(self.path)
        if u.path.startswith("/api/play/"):
            return self._play_post(u.path[len("/api/play/"):], body)
        if u.path == "/api/updates/run":
            only = urllib.parse.parse_qs(u.query).get("id", [None])[0]  # (one tool: its Retry button)
            if only is not None and not self.app.can_retry_one():
                return self._json({"accepted": False, "message": "This updater runs every tool at once (Update now)."}, 501)
            ok, wait = self.app.updater.request(only=only) if only is not None else self.app.updater.request()
            return self._json({"accepted": ok, "nextAllowedIn": wait, "enabled": self.app.updater.enabled})
        if u.path == "/api/updates/ack":
            rid = urllib.parse.parse_qs(u.query).get("id", [""])[0]
            return self._json({"removed": self.app.updater.ack(rid)})
        if u.path == "/api/checklist":
            return self._checklist_post(body)
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


    def _checklist_post(self, body):
        """{version, section, done, build?}: tick / untick one section of the release checklist for one version."""
        if "json" not in (self.headers.get("Content-Type") or "").lower():  # (no cross-site form posts)
            return self._json({"ok": False, "message": "Send JSON."}, 415)
        res = self.app.store.results()
        chk = derive.shown_data(res, "checklist")
        try:
            req = json.loads(body.decode("utf-8"))
            if not isinstance(req, dict):
                raise ValueError("not an object")
            view = self.app.ticks.set(req.get("version"), req.get("section"), req.get("done"), req.get("build"),
                                      sections=chk.get("headings") if chk else None,
                                      newest=derive.versions(res).get("testing"))
        except ValueError as e:  # (broken JSON and UTF-8 included)
            return self._json({"ok": False, "message": "Needs {version, section, done}: %s." % str(e)[:120]}, 400)
        except OSError as e:
            return self._json({"ok": False, "message": "Not saved (%s)." % type(e).__name__}, 500)
        return self._json({"ok": True, "checklist": view})

    def _play_post(self, what, body):
        # (a JSON body only: a page on another origin cannot send one without a preflight, which _origin_ok refuses)
        if body and "json" not in (self.headers.get("Content-Type") or "").lower():
            return self._json({"ok": False, "message": "Send JSON."}, 415)
        try:
            req = json.loads(body.decode("utf-8")) if body else {}
        except (ValueError, UnicodeDecodeError):
            return self._json({"ok": False, "message": "The page sent broken JSON."}, 400)
        if not isinstance(req, dict):
            req = {}
        pl = self.app.play
        try:
            if what == "launch":
                return self._json({"ok": True, "session": pl.launch(req.get("build"), req.get("args") or "")})
            if what == "note":
                note = pl.save_note(req.get("session"), req)
                return self._json({"ok": True, "note": {k: note[k] for k in ("id", "title", "stamp", "line")},
                                   "message": "Saved. It goes into Claude Documents (%s) in a moment." % play.DOCS_FILE})
            if what == "skip":
                return self._json({"ok": True, "session": pl.skip(req.get("session"))})
            if what == "install":
                return self._json({"ok": True, "install": pl.install(req.get("tag"))})
            if what == "remove":
                return self._json(dict(pl.remove(req.get("tag")), ok=True))
            if what == "sync":
                pl.wake.set()
                return self._json({"ok": True})
            res = pl.api_post(what, req)  # (the newer routes: stop, open, note-edit, note-delete, install-cancel)
            if res is not None:
                return self._json(dict(res, ok=True))
        except play.PlayError as e:
            return self._json({"ok": False, "message": e.message, "reason": e.reason}, e.code)
        return self._send(404, "not found", "text/plain; charset=utf-8")


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
    ap = argparse.ArgumentParser(description="Supervisor Board server (127.0.0.1 only)")
    ap.add_argument("--quark", help="the Quark folder (default: the parent of the folder holding server.py)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--init", action="store_true", help="write tabs.json / config.json if missing, then exit")
    ap.add_argument("--no-update", action="store_true", help="no automatic installs / updates of the tools")
    args = ap.parse_args(argv)
    paths = config.Paths(HERE, args.quark)
    if args.init:
        wrote = [n for n, f in (("tabs.json", tabs.write_default), ("config.json", config.write_default_config)) if f(paths)]
        util.log("project-manager folder %s: %s" % (paths.pm_dir, ("wrote " + ", ".join(wrote)) if wrote else "kept existing files"))
        up = tabs.upgrade(paths)
        if up:
            util.log("tabs.json: new default tabs / start commands / keepAlive for %s (old file: tabs.json.old)" % ", ".join(up))
        return 0
    app = App(paths, args.port, update=not args.no_update)
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
        app.updater.shutdown()
        app.play.shutdown()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
