#!/usr/bin/env python3
"""Dev server for the GitHub manager page: serves the page and the fixtures on the API paths (stdlib only).

    python3 tools/github_manager/test/fixture_server.py [--port 8767] [--no-shift] [--poll-error TEXT] [--no-notes]
                                                         [--no-editor] [--docs-error TEXT] [--signed-out]
                                                         [--notes-held]

Every timestamp in the fixtures is shifted so the fixture's generatedAt is "now" (so elapsed timers and
"5 min ago" look live); --no-shift serves them as written. /api/log returns a made-up GitHub Actions log for a
job with hasLog (with ##[error] lines for failed jobs), 404 otherwise. /api/notes is the real notes store
(server.Notes) kept in memory, with a few sample notes (--no-notes: none) and the same write checks as the server;
its history events are added to the fixture history. The GitHub editor (/api/structure, /api/rules, /api/pending,
/api/github/rulesets) runs on the real stores (server.Structure / Rules / Pending) in memory, against an in-memory
GitHub (test/fake_github.py) made from the fixture's branches and PRs: Apply really "applies" there and nothing else.
It starts with sample changes: a ruleset diff and a branch delete waiting, one apply that failed, one that is out of
date, one applied (--no-editor: an empty queue). --docs-error TEXT shows the Claude Documents copy as failed.
--signed-out: gh signed out since the start (notRead: pulls, releases, runs; no PRs or releases, runs from "cache").
--notes-held: a new install whose Claude Documents notes couldn't be read back (held, not overwritten).
"""
import argparse, json, os, re, sys
from datetime import datetime, timedelta, timezone
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)  # tools/github_manager
sys.dont_write_bytecode = True
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)
import server as S  # noqa: E402
from fake_github import MemoryGitHub, testing_guard, PRODUCTION_RULE, TAGGING_RULE  # noqa: E402

WORKFLOW_FILES = {"build-and-release": "release.yml", "main-guard": "main-guard.yml", "macos-retry": "macos-retry.yml"}
ISO = re.compile(r'"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)Z"')
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon"}


def parse(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)


class Fixtures:
    def __init__(self, shift, poll_error, sample_notes=True, sample_editor=True, docs_error=None, signed_out=False,
                 notes_held=False):
        self.shift, self.poll_error, self.docs_error, self.signed_out = shift, poll_error, docs_error, signed_out
        self.notes_held = ("Claude Documents: 11-github-notes.md there holds 3 notes this PC could not read back, so "
                           "it is left as it is (not replaced by this PC's empty notes) until a note is written "
                           "here") if notes_held else None
        self.note_events = []
        self.notes = S.Notes(None, self.note_events.extend, "floration/quark")
        if sample_notes:
            self.seed_notes()
        self.structure = S.Structure(None, self.note_events.extend)
        self.rules = S.Rules(None, self.note_events.extend)
        st = self.state(roles=False)
        self.default = st["defaultBranch"]
        gh = self.gh = MemoryGitHub("floration/quark", self.default, [PRODUCTION_RULE, TAGGING_RULE, testing_guard()])
        for b in st["branches"]:
            gh.branches[b["name"]] = b["sha"]
            gh.ahead[b["sha"]] = b.get("ahead") or 0
        gh.pulls = [{"number": p["number"], "head": p["head"]} for p in st["pulls"] if p["state"] == "open"]
        self.pending = S.Pending(None, gh, "floration/quark", self.note_events.extend)
        self.docs_at = S.iso(datetime.now(timezone.utc) - timedelta(minutes=3))
        if sample_editor:
            self.seed_editor(st)

    def seed_editor(self, st):
        add = self.structure.update
        add({"branches": {
            "testing": {"role": None, "description": "Where every Claude's work lands. Each push publishes a TESTING "
                                                     "prerelease (`test-<VERSION>-test.<run>`)."},
            "launcher": {"role": None, "description": "The launcher's own release line."},
            "site": {"role": None, "description": "The website (floration.github.io/quark)."},
            "claude/macos-skip": {"role": "other", "description": "Experiment from the macOS runner wait; kept "
                                                                  "for reference."}}}, self.default)
        names = [b["name"] for b in st["branches"]]
        work = [b for b in st["branches"] if b["kind"] == "claude"]
        q = self.pending.queue
        form = S.ruleset_form(S.ruleset_norm(testing_guard()))
        form.update(requirePr=True, requiredChecks=["meta", "linux", "windows"], linearHistory=True)
        q({"type": "ruleset.update", "params": {"id": testing_guard()["id"], "form": form}})
        if "claude/macos-skip" in names:
            q({"type": "branch.delete", "params": {"name": "claude/macos-skip"}})
        if len(work) > 4:
            c = q({"type": "branch.delete", "params": {"name": work[4]["name"]}})
            self.gh.fail_next = (422, "Repository rule violations found", ["Cannot delete this branch"])
            self.pending.apply(c["id"])
        if len(work) > 5:
            c = q({"type": "branch.delete", "params": {"name": work[5]["name"]}})
            self.gh.branches[work[5]["name"]] = "f" * 40
            self.pending.apply(c["id"])
            self.gh.branches[work[5]["name"]] = work[5]["sha"]
        c = q({"type": "branch.create", "params": {"name": "claude/project-thread-n3wb1e", "from": "testing"}})
        self.pending.apply(c["id"])

    def docs_status(self):
        st = {"enabled": True, "state": "ok", "offReason": None, "repo": S.DOCS_REPO, "branch": S.DOCS_BRANCH,
              "files": list(S.DOCS_FILES), "dueAt": None, "lastOk": self.docs_at, "lastCommit": "4f1c2ab9" + "0" * 32,
              "lastAttempt": self.docs_at, "lastError": None, "notesHeld": self.notes_held}
        if self.docs_error:
            st.update(state="failed", lastError=self.docs_error)
        return st

    def structure_view(self):
        o = self.structure.obj()
        o.update({"roles": list(S.ROLES), "settableRoles": list(S.SETTABLE_ROLES),
                  "protectedBranches": list(S.PROTECTED_BRANCHES), "defaultBranch": self.default,
                  "staleDays": self.rules.limit(S.STALE_RULE_ID, S.STALE_DAYS_DEFAULT),
                  "staleScope": self.rules.scope(S.STALE_RULE_ID), "staleRule": S.STALE_RULE_ID})
        return o

    def rules_view(self):
        o = self.rules.obj()
        o.update({"kinds": list(S.RULE_KINDS), "scopes": list(S.RULE_SCOPES), "hardRule": S.HARD_RULE_ID,
                  "defaultBranch": self.default, "docsSync": self.docs_status(),
                  "limits": {"rules": S.RULES_MAX, "titleBytes": S.RULE_TITLE_MAX, "textBytes": S.RULE_TEXT_MAX}})
        return o

    def pending_view(self):
        o = self.pending.obj()
        o["defaultBranch"] = self.default
        return o

    def rulesets_view(self):
        items = [S.ruleset_item(self.gh.get("repos/floration/quark/rulesets/%d" % rid)[0], self.default,
                                "floration/quark") for rid in sorted(self.gh.rulesets)]
        names = set()
        for r in self.state()["runs"][:100]:
            for j in r.get("jobs") or []:
                names.add(j["name"])
        names.add("from-testing")
        return {"rulesets": items, "defaultBranch": self.default, "checks": sorted(names, key=str.lower),
                "fetchedAt": S.iso(datetime.now(timezone.utc)), "error": None,
                "lockedNames": list(S.LOCKED_RULESET_NAMES), "protectedBranches": list(S.PROTECTED_BRANCHES)}

    def seed_notes(self):
        s = self.state()
        work = [b["name"] for b in s["branches"] if b["kind"] == "claude"]
        open_pr = [p["number"] for p in s["pulls"] if p["state"] == "open"]
        failed = [r["id"] for r in s["runs"] if r.get("conclusion") == "failure"]
        add = self.notes.create
        t0 = datetime.now(timezone.utc) - timedelta(hours=30)
        times = (S.iso(t0 + timedelta(minutes=97 * i)) for i in range(100))
        self.notes.now = lambda: next(times)
        add({"target": {"kind": "general"}, "pinned": True, "text":
             "Before the next production PR:\n- run the **whole** `docs/RELEASE_CHECKLIST.md` on the testing build\n"
             "- ask about the macOS runner wait\n\nSee https://github.com/floration/quark/actions for the queue."})
        add({"target": {"kind": "version", "key": s["testing"]["version"]}, "pinned": True,
             "text": "Hold %s until ranked is re-tested on Windows." % s["testing"]["version"]})
        if work:
            add({"target": {"kind": "branch", "key": work[0]}, "text": "Rebase onto testing before review."})
        if open_pr:
            add({"target": {"kind": "pr", "key": str(open_pr[0])}, "text":
                 "Review the `netcode` change first.\n\n1. snapshot delta\n2. ack window", "author": "Claude"})
            add({"target": {"kind": "pr", "key": str(open_pr[0])}, "text": "Owner asked for screenshots.",
                 "status": "done"})
        if failed:
            n = add({"target": {"kind": "run", "key": str(failed[0])}, "pinned": True,
                     "text": "Flaky? `net/snapshot_delta_baseline` failed once; re-run before blaming the change."})
            self.notes.update(n["id"], {"text": n["text"] + " <b>not bold</b>"})
        if s["local"]:
            add({"target": {"kind": "local", "key": s["local"][0]["name"]}, "text": "Uncommitted map edits: keep."})
        self.notes.now = lambda: S.iso(datetime.now(timezone.utc))

    def _load(self, name):
        with open(os.path.join(HERE, name), encoding="utf-8") as f:
            text = f.read()
        if self.shift:
            st = json.load(open(os.path.join(HERE, "fixture-state.json"), encoding="utf-8"))
            delta = datetime.now(timezone.utc) - parse(st["generatedAt"][:19])
            text = ISO.sub(lambda m: '"' + (parse(m.group(1)) + delta).strftime("%Y-%m-%dT%H:%M:%SZ") + '"', text)
        return json.loads(text)

    def state(self, roles=True):
        s = self._load("fixture-state.json")
        for r in s["runs"] + s.get("running", []):
            r.setdefault("workflowFile", WORKFLOW_FILES.get(r["workflow"]))
        if self.poll_error:
            s["poll"]["lastError"] = self.poll_error
        s["notRead"] = []
        if self.signed_out:
            s["notRead"] = list(S.NOT_READ_SECTIONS)
            s["pulls"], s["releases"], s["poll"]["lastOk"] = [], [], None
            s["testing"]["prerelease"] = None
            s["poll"]["lastError"] = self.poll_error or "gh: To get started with GitHub CLI, please run: gh auth login"
            for b in s["branches"]:
                b["pr"] = None
        if roles and hasattr(self, "rules"):
            now = S.parse_time(s["generatedAt"]) if not self.shift else datetime.now(timezone.utc)
            S.branch_roles(s["branches"], self.structure, self.rules.limit(S.STALE_RULE_ID, 7), s["defaultBranch"], now,
                           stale_scope=self.rules.scope(S.STALE_RULE_ID))
        return s

    def history(self, since=None, limit=None):
        h = self._load("fixture-history.json") + self.note_events
        h = [e for _, e in sorted(enumerate(h), key=lambda ie: (ie[1]["at"], ie[0]), reverse=True)]
        if since:
            h = [e for e in h if e["at"] >= since]  # as the server: since is inclusive (the page dedupes)
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
        self.write()

    def do_PUT(self):
        self.write()

    def do_DELETE(self):
        self.write()

    def write(self):
        """The notes and editor writes, with the server's checks (origin, JSON, known fields, sizes)."""
        path = urlparse(self.path).path
        n = int(self.headers.get("Content-Length") or 0)
        limit = S.EDIT_BODY_MAX if path in ("/api/rules", "/api/structure") else S.NOTE_BODY_MAX
        raw = self.rfile.read(n) if 0 < n <= limit else b""
        if n > limit:
            return self.json({"error": "too big"}, 413)
        if not S.write_origin_ok(self.headers, self.server.server_address[1]):
            return self.json({"error": "notes can only be changed from the GitHub manager's own page"}, 403)
        if not S.json_content_type(self.headers.get("Content-Type")):
            return self.json({"error": "send JSON"}, 415)
        try:
            body = json.loads(raw.decode("utf-8")) if raw.strip() else None
        except ValueError:
            return self.json({"error": "the body is not valid JSON"}, 400)
        if not path.startswith("/api/notes"):
            return self.editor_write(path, body)
        try:
            if path == "/api/notes" and self.command == "POST":
                return self.json({"note": self.fx.notes.create(body)}, 201)
            nid = path[len("/api/notes/"):] if path.startswith("/api/notes/") else ""
            if not S.NOTE_ID_RE.match(nid):
                return self.json({"error": "not found"}, 404)
            if self.command == "PUT":
                return self.json({"note": self.fx.notes.update(nid, body)})
            if self.command == "DELETE":
                return self.json({"deleted": self.fx.notes.delete(nid)})
        except S.NoteError as e:
            return self.json({"error": str(e)}, e.code)
        self.json({"error": "not found"}, 404)

    def editor_write(self, path, body):
        fx, cmd = self.fx, self.command
        try:
            if path == "/api/structure" and cmd == "PUT":
                fx.structure.update(body, fx.default)
                return self.json(fx.structure_view())
            if path == "/api/rules" and cmd == "PUT":
                fx.rules.replace(body)
                return self.json(fx.rules_view())
            if path == "/api/pending" and cmd == "POST":
                return self.json({"change": fx.pending.queue(body)}, 201)
            if path == "/api/pending/apply-all" and cmd == "POST":
                return self.json(fx.pending.apply_all(body))
            m = re.match(r"^/api/pending/(c[0-9a-f]{12})(/apply|/preview)?$", path)
            if m and cmd == "DELETE" and not m.group(2):
                return self.json({"discarded": fx.pending.discard(m.group(1))})
            if m and cmd == "POST" and m.group(2) == "/preview":
                return self.json({"change": fx.pending.repreview(m.group(1))})
            if m and cmd == "POST" and m.group(2) == "/apply":
                ch = fx.pending.apply(m.group(1), body)
                code = {"applied": 200, "stale": 409, "refused": 422}.get(ch["status"], 502)
                out = {"change": ch}
                if code != 200:
                    out["error"] = (ch.get("result") or {}).get("error") or ch["status"]
                return self.json(out, code)
        except S.EditError as e:
            out = {"error": str(e)}
            if e.locked:
                out["locked"] = True
            if e.change is not None:
                out["change"] = e.change
            return self.json(out, e.code)
        except S.NoteError as e:
            return self.json({"error": str(e)}, e.code)
        self.json({"error": "not found"}, 404)

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/api/state":
            return self.json(self.fx.state())
        if u.path == "/api/history":
            return self.json(self.fx.history(q.get("since"), int(q["limit"]) if q.get("limit") else None))
        if u.path == "/api/notes":
            return self.send(200, self.fx.notes.view()[1], "application/json")
        if u.path == "/api/structure":
            return self.json(self.fx.structure_view())
        if u.path == "/api/rules":
            return self.json(self.fx.rules_view())
        if u.path == "/api/pending":
            return self.json(self.fx.pending_view())
        if u.path == "/api/github/rulesets":
            return self.json(self.fx.rulesets_view())
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
    ap.add_argument("--no-notes", action="store_true", help="start with no sample notes")
    ap.add_argument("--no-editor", action="store_true", help="start with no sample structure / pending changes")
    ap.add_argument("--docs-error", default=None, help="show the Claude Documents copy as failed with this error")
    ap.add_argument("--signed-out", action="store_true", help="gh signed out since the start: GitHub parts unknown")
    ap.add_argument("--notes-held", action="store_true", help="the Claude Documents' notes held on a new install")
    a = ap.parse_args()
    Handler.fx = Fixtures(not a.no_shift, a.poll_error, not a.no_notes, not a.no_editor, a.docs_error, a.signed_out,
                          a.notes_held)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    print(f"fixture server on http://127.0.0.1:{a.port}/", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
