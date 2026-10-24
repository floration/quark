#!/usr/bin/env python3
"""Dev server for the GitHub manager page: serves the page and the fixtures on the API paths (stdlib only).

    python3 tools/github_manager/test/fixture_server.py [--port 8767] [--no-shift] [--poll-error TEXT]

Every timestamp in the fixtures is shifted so the fixture's generatedAt is "now" (so elapsed timers and
"5 min ago" look live); --no-shift serves them as written. /api/log returns a made-up GitHub Actions log for a
job with hasLog (with ##[error] lines for failed jobs), 404 otherwise.
"""
import argparse, json, os, re, sys
from datetime import datetime, timezone
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)  # tools/github_manager
ISO = re.compile(r'"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)Z"')
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon"}


def parse(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)


class Fixtures:
    def __init__(self, shift, poll_error):
        self.shift, self.poll_error = shift, poll_error

    def _load(self, name):
        with open(os.path.join(HERE, name), encoding="utf-8") as f:
            text = f.read()
        if self.shift:
            st = json.load(open(os.path.join(HERE, "fixture-state.json"), encoding="utf-8"))
            delta = datetime.now(timezone.utc) - parse(st["generatedAt"][:19])
            text = ISO.sub(lambda m: '"' + (parse(m.group(1)) + delta).strftime("%Y-%m-%dT%H:%M:%SZ") + '"', text)
        return json.loads(text)

    def state(self):
        s = self._load("fixture-state.json")
        if self.poll_error:
            s["poll"]["lastError"] = self.poll_error
        return s

    def history(self, since=None, limit=None):
        h = self._load("fixture-history.json")
        if since:
            h = [e for e in h if e["at"] > since]
        return h[:limit] if limit else h

    def log(self, run, job):
        s = self.state()
        for r in s["runs"]:
            if str(r["id"]) != run:
                continue
            for j in r.get("jobs", []):
                if str(j["id"]) == job and j.get("hasLog"):
                    return fake_log(r, j)
        return None


def fake_log(run, job):
    t = (job.get("startedAt") or run["createdAt"]).replace("Z", "")
    out = []

    def line(s):
        out.append(f"{t}.1234567Z {s}")

    line("##[group]Runner Image")
    line("Image: ubuntu-24.04" if "windows" not in job["name"] else "Image: windows-2025")
    line("##[endgroup]")
    for step in ["Set up job", "Run actions/checkout", "Configure", "Build"]:
        line(f"##[group]Run {step}")
        for k in range(40):
            line(f"[{k + 1}/240] Building CXX object CMakeFiles/quark_core.dir/src/game/file{k:03d}.cpp.o")
        line("##[endgroup]")
    failed = job.get("conclusion") == "failure"
    line("##[group]Run " + (job["failedSteps"][0] if failed and job["failedSteps"] else "quark_tests"))
    for k in range(300):
        line(f"[test] {'net' if 'net' in job['name'] else 'core'}/case_{k:03d} ok ({(k * 37) % 900} ms)")
        if failed and k == 211:
            line("[test] net/snapshot_delta_baseline FAILED: expected ack 1842, got 1841 (src/net/snapshot.cpp:612)")
            line("##[error]quark_tests: 1 of 512 checks failed (net/snapshot_delta_baseline)")
    line("##[endgroup]")
    if failed:
        line("##[error]Process completed with exit code 1.")
    line("Cleaning up orphan processes")
    return "\n".join(out) + "\n"


class Handler(BaseHTTPRequestHandler):
    fx: Fixtures = None

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.command, self.path))

    def send(self, code, body, ctype):
        b = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(b)

    def json(self, obj, code=200):
        self.send(code, json.dumps(obj), "application/json")

    def do_POST(self):
        if urlparse(self.path).path == "/api/refresh":
            return self.json(self.fx.state())
        self.send(404, "not found", "text/plain")

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/api/state":
            return self.json(self.fx.state())
        if u.path == "/api/history":
            return self.json(self.fx.history(q.get("since"), int(q["limit"]) if q.get("limit") else None))
        if u.path == "/api/log":
            text = self.fx.log(q.get("run", ""), q.get("job", ""))
            if text is None:
                return self.send(404, "log not found", "text/plain; charset=utf-8")
            return self.send(200, text, "text/plain; charset=utf-8")
        rel = "index.html" if u.path in ("/", "") else u.path.lstrip("/")
        full = os.path.normpath(os.path.join(ROOT, rel))
        if not full.startswith(ROOT + os.sep) or not os.path.isfile(full) or "/test/" in full.replace("\\", "/"):
            return self.send(404, "not found", "text/plain")
        with open(full, "rb") as f:
            self.send(200, f.read(), TYPES.get(os.path.splitext(full)[1], "application/octet-stream"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8767)
    ap.add_argument("--no-shift", action="store_true")
    ap.add_argument("--poll-error", default=None)
    a = ap.parse_args()
    Handler.fx = Fixtures(not a.no_shift, a.poll_error)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    print(f"fixture server on http://127.0.0.1:{a.port}/", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
