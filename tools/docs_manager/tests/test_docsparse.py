"""Tests for the Claude Documents manager backend: python3 -m unittest discover tools/docs_manager/tests

Parser and diff on fixtures, the link finder and waiting-on rules on the GitHub fixture, then an end-to-end
snapshot and HTTP run against a throwaway git repo (skipped without git). Set QUARK_DOCS_REPO to a clone of
the real Claude Documents repo to also parse every historical version of it.
"""
import http.client, json, os, shutil, socket, subprocess, sys, tempfile, threading, unittest

TESTS = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(TESTS)
sys.path.insert(0, TOOL)
import docsparse, ghlinks, serve  # noqa: E402

FIX = os.path.join(TESTS, "fixtures")
with open(os.path.join(TOOL, "threads.json"), encoding="utf-8") as f: THREADS = json.load(f)["threads"]
NAMES = [n for t in THREADS for n in [t["title"]] + t.get("aliases", [])]
HAVE_GIT = shutil.which("git") is not None


def sample():
    with open(os.path.join(FIX, "sample-02.md"), "rb") as f: return f.read().decode("utf-8")


def entry(file, title, status="open", body="Body.", section="S", **kw):
    e = {"id": "%s:%s" % (file, docsparse.slug(title)), "file": file, "section": section, "title": title, "priority": None,
         "body": body, "date": "2026-10-23", "thread": "Tweak gameplay", "status": status, "statusNote": "", "statusText": status,
         "archived": False, "line": 1}
    e.update(kw)
    return e


class ParseTests(unittest.TestCase):
    def setUp(self):
        self.p = docsparse.parse_file("02-currently-working-on.md", sample(), NAMES)
        self.by = {e["title"]: e for e in self.p["entries"]}

    def test_structure(self):
        self.assertEqual(self.p["title"], "Currently working on")
        self.assertEqual(self.p["sections"], ["In progress", "Archive"])
        self.assertEqual(len(self.p["entries"]), 10)
        self.assertTrue(any("section text" in n["text"] for n in self.p["notes"]))
        self.assertTrue(self.by["Old thing."]["archived"])

    def test_entry_fields(self):
        e = self.by["Sound editor."]
        self.assertEqual(e["id"], "02-currently-working-on.md:sound-editor")
        self.assertEqual((e["status"], e["date"], e["thread"]), ("in-progress", "2026-10-23", "Build a sound editor for the game"))
        self.assertNotIn("_2026", e["body"])

    def test_wrapped_and_nested(self):
        w = self.by["Wrapped entry."]
        self.assertEqual((w["priority"], w["status"]), ("P2", "open"))
        self.assertIn("unindented one", w["body"])
        n = self.by["Entry with nested bullets."]
        self.assertEqual(n["status"], "in-progress")
        self.assertIn("- first step", n["body"])
        self.assertIn("- second step", n["body"])

    def test_statuses(self):
        self.assertEqual(self.by["No trailer at all."]["status"], "unknown")
        s = self.by["Status with a note."]
        self.assertEqual((s["status"], s["statusNote"]), ("answered", "yes, fix all six (Tweak gameplay)"))
        self.assertEqual(self.by["Odd status."]["status"], "answered")
        self.assertEqual(self.by["Odd status."]["statusNote"], "partly answered")
        v = [e for e in self.p["entries"] if e["title"].startswith("Visibility")][0]
        self.assertEqual((v["priority"], v["title"]), ("P3", "Visibility rays aim at a normal-size player,"))

    def test_duplicates_and_short_trailers(self):
        d = [e for e in self.p["entries"] if e["title"] == "Duplicate."]
        self.assertEqual([e["id"].split(":")[1] for e in d], ["duplicate", "duplicate#2"])
        self.assertEqual((d[0]["thread"], d[0]["status"]), (None, "unknown"))
        self.assertEqual((d[1]["thread"], d[1]["status"]), ("UI customizer", "unknown"))

    def test_normalize_status(self):
        self.assertEqual(docsparse.normalize_status("fixed in local 1.18.2 (x, y), unpushed")[0], "fixed")
        self.assertEqual(docsparse.normalize_status("not a bug: owner says so")[0], "wontfix")
        self.assertEqual(docsparse.normalize_status("waiting on owner")[0], "open")
        self.assertEqual(docsparse.normalize_status("in progress")[0], "in-progress")
        self.assertEqual(docsparse.normalize_status("whatever")[0], "unknown")

    def test_documents_not_parsed(self):
        p = docsparse.parse_file("00-agent-rules.md", "# Rules\n\n- **A rule.** x _2026-10-23, a, open_\n")
        self.assertEqual((p["kind"], p["entries"]), ("document", []))


class DiffTests(unittest.TestCase):
    F = "01-known-bugs.md"

    def kinds(self, old, new):
        return [(i["kind"], i["from"], i["to"]) for i in docsparse.diff_entries(old, new)]

    def test_added_removed_status(self):
        a, b = entry(self.F, "Alpha bug"), entry(self.F, "Beta bug", body="Something else entirely here.")
        self.assertEqual(self.kinds([a], [a, b]), [("added", None, "open")])
        self.assertEqual(self.kinds([a, b], [a]), [("removed", "open", None)])
        it = docsparse.diff_entries([a], [dict(a, status="fixed")])[0]
        self.assertEqual((it["kind"], it["from"], it["to"]), ("status", "open", "fixed"))
        self.assertIn("open -> fixed", it["summary"])
        self.assertEqual(docsparse.diff_entries([a], [dict(a)]), [])

    def test_edited_title_is_not_add_remove(self):
        a = entry(self.F, "Server CPU cost.", body="1.18.3 and 1.18.4 on testing, CI green.")
        b = entry(self.F, "Server CPU cost: done.", body="1.18.3 and 1.18.4 on testing, CI green, prereleases out.")
        items = docsparse.diff_entries([a], [b])
        self.assertEqual([i["kind"] for i in items], ["edited"])
        self.assertEqual(items[0]["fromId"], a["id"])

    def test_moved_section_and_file(self):
        a = entry(self.F, "Gamma", section="Open")
        self.assertEqual(self.kinds([a], [dict(a, section="Archive")]), [("moved", "Open", "Archive")])
        b = dict(entry("03-possible-bugs.md", "Gamma"), body=a["body"])
        it = docsparse.diff_entries([a], [b])
        self.assertEqual([(i["kind"], i.get("fromFile")) for i in it], [("moved", self.F)])

    def test_document_diff(self):
        it = docsparse.diff_document("README.md", "a\nb\n", "a\nc\nd\n", "Claude Documents")
        self.assertEqual(it[0]["summary"], "Claude Documents edited (+2 / -1 lines)")
        self.assertEqual(docsparse.diff_document("README.md", "a\r\n", "a\n"), [])


class LinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(FIX, "github.json"), encoding="utf-8") as f: raw = json.load(f)
        cls.gh = ghlinks.normalize(raw)
        cls.commits = {k: {"exists": v} for k, v in raw["commits"].items()}
        cls.L = ghlinks.Linker(cls.gh, THREADS, cls.commits)

    def find(self, text, kind, thread=None):
        return [l for l in self.L.links(text, thread) if l["kind"] == kind]

    def test_pr_and_commit(self):
        prs = self.find("Merged as PR #8 / 1.18.6 (merge ce1ab6a); PRs #4 and #5.", "pr")
        self.assertEqual([(l["label"], l["state"]) for l in prs], [("PR #8", "merged"), ("PR #4", "merged"), ("PR #5", "merged")])
        c = self.find("merge ce1ab6a, local a37b342, not deadbeef nor 1234567", "commit")
        self.assertEqual([(l["label"], l["exists"]) for l in c], [("ce1ab6a", True), ("a37b342", False)])
        self.assertTrue(c[0]["url"].endswith("/commit/ce1ab6a"))

    def test_versions_and_releases(self):
        v = {l["label"]: l for l in self.find("1.18.2 and 1.17.0 and 1.18.6, not 129.151.8.15", "version")}
        self.assertEqual(set(v), {"1.18.2", "1.17.0", "1.18.6"})
        self.assertEqual(v["1.18.2"]["release"], "test-1.18.2-test.146")
        self.assertEqual(v["1.17.0"]["channel"], "production")
        self.assertIn("changelogs/1.18.6.md", v["1.18.6"]["url"])
        r = self.find("prereleases test-1.18.3-test.148 and test-1.18.4-test.149", "release")
        self.assertEqual([l["label"] for l in r], ["test-1.18.3-test.148", "test-1.18.4-test.149"])
        self.assertEqual(self.find("test-1.18.3-test.148", "version"), [])
        # testing builds from 1.19.0 on: test-<VERSION>.testing.<run>
        r = self.find("prerelease test-1.19.0.testing.341 is out", "release")
        self.assertEqual([(l["label"], l["prerelease"]) for l in r], [("test-1.19.0.testing.341", True)])
        self.assertEqual(self.find("test-1.19.0.testing.341", "version"), [])

    def test_branches_runs_files(self):
        links = self.L.links("cloud branch claude/project-thread-b4pufp and branch `ui-fonts`; see `src/common/profile.h:17`")
        b = [l for l in links if l["kind"] == "branch"]
        self.assertEqual([(l["label"], l["state"], l["exists"]) for l in b],
                         [("claude/project-thread-b4pufp", "failure", True), ("ui-fonts", None, False)])
        self.assertTrue(any(l["kind"] == "run" and l["label"] == "CI run 151" for l in links))
        f = [l for l in links if l["kind"] == "file"][0]
        self.assertTrue(f["url"].endswith("/blob/testing/src/common/profile.h#L17"))
        self.assertEqual(self.find("CI run 147 red", "run")[0]["state"], "failure")
        self.assertEqual(self.find("Sound editor merged", "pr"), [])
        self.assertEqual(self.find("branch claude/project-thread-sound1", "pr")[0]["label"], "PR #8")

    def test_urls_and_threads(self):
        a = self.find("Doc: https://claude.ai/artifact/DXbNadvHHzRHe5W2RkqQrW.", "artifact")
        self.assertEqual(a[0]["url"], "https://claude.ai/artifact/DXbNadvHHzRHe5W2RkqQrW")
        t = self.find("fixed (Tweak gameplay) and shares the map editor's helper", "thread", "Claude Documents setup")
        self.assertEqual([l["label"] for l in t], ["Shared Claude documents folder", "Tweak gameplay"])
        self.assertEqual(self.L.thread("sound editor"), ("Build a sound editor for the game", THREADS[9]["url"]))

    def test_checks_and_normalize(self):
        self.assertEqual(ghlinks.checks_state([{"status": "COMPLETED", "conclusion": "SUCCESS"},
                                               {"status": "IN_PROGRESS", "conclusion": ""}]), "in_progress")
        self.assertEqual(ghlinks.checks_state([{"__typename": "StatusContext", "state": "FAILURE"}]), "failure")
        self.assertIsNone(ghlinks.checks_state([]))
        d = ghlinks.normalize({"pulls": [{"number": 3, "state": "closed", "merged_at": "2026-10-23T00:00:00Z",
                                          "html_url": "https://github.com/floration/quark/pull/3", "head": {"ref": "x"}}],
                               "runs": [{"id": 5, "status": "completed", "conclusion": "failure", "head_branch": "x"}]})
        self.assertEqual((d["prs"][0]["state"], d["prs"][0]["headRefName"]), ("merged", "x"))
        self.assertEqual((d["runs"][0]["databaseId"], d["runs"][0]["state"]), (5, "failure"))

    def test_shared_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "github.json")
            with open(p, "w") as f: json.dump({"generatedAt": ghlinks.now_iso(), "prs": [{"number": 1, "state": "OPEN"}]}, f)
            self.assertEqual(ghlinks.read_shared(p)["prs"][0]["state"], "open")
            with open(p, "w") as f: json.dump({"generatedAt": "2020-01-01T00:00:00Z", "prs": [{"number": 1}]}, f)
            self.assertIsNone(ghlinks.read_shared(p))
            with open(p, "w") as f: f.write("{broken")
            self.assertIsNone(ghlinks.read_shared(p))
            self.assertIsNone(ghlinks.read_shared(os.path.join(tmp, "missing.json")))


class WaitingTests(unittest.TestCase):
    def w(self, e, links=()):
        return serve.waiting_on(e, list(links))

    def test_rules(self):
        self.assertEqual(self.w(entry("07-things-to-ask-the-user.md", "Q?"))[0], ["owner"])
        self.assertEqual(self.w(entry("01-known-bugs.md", "B", status="fixed"))[0], ["none"])
        w, why, ns = self.w(entry("02-currently-working-on.md", "Font", status="in-progress",
                                  body="Waiting on the owner's push (needs VERSION + both changelogs)."))
        self.assertEqual((w, why), (["owner", "push"], "Waiting on q to push (needs VERSION + changelogs)."))
        w, why, ns = self.w(entry("02-currently-working-on.md", "X", status="in-progress"),
                            [{"kind": "pr", "label": "PR #9", "state": "open", "checks": "failure"}])
        self.assertEqual(w, ["pr", "ci"])
        self.assertIn("PR #9 is open, checks failing", why)
        self.assertEqual(self.w(entry("02-currently-working-on.md", "X", status="in-progress"))[0], ["claude"])
        w, why, ns = self.w(entry("03-possible-bugs.md", "Y", body="Needs a repro on Windows."))
        self.assertEqual(w, ["tests"])
        w, why, ns = self.w(entry("01-known-bugs.md", "Z"))
        self.assertEqual((w, why, ns), (["claude"], "Nobody has started it yet.", True))
        self.assertEqual(self.w(entry("08-other.md", "N", status="unknown", body="Owner to decide."))[0], ["none"])


def read(path, mode="rb"):
    with open(path, mode) as f: return f.read()


def git(cwd, *args, env=None):
    subprocess.run(["git", "-C", cwd] + list(args), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   env=dict(os.environ, **(env or {})))


@unittest.skipUnless(HAVE_GIT, "git not installed")
class EndToEndTests(unittest.TestCase):
    """A three-commit docs repo, a PC folder (CRLF copy, one edited file, one old file), snapshot + HTTP."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="docsmgr-")
        cls.repo = os.path.join(cls.tmp, "repo")
        os.makedirs(cls.repo)
        git(cls.repo, "init", "-q", "-b", "main")
        git(cls.repo, "config", "user.email", "t@example.com"); git(cls.repo, "config", "user.name", "Claude")
        git(cls.repo, "config", "core.autocrlf", "false")
        steps = [
            ("Claim: Tweak gameplay thread", {
                "README.md": "# Claude Documents\n\nIntro.\n",
                "01-known-bugs.md": "# Known bugs\n\n## Gameplay\n\n- **Tag knockouts never reset** (P2). x. _2026-10-23, Tweak gameplay, open_\n",
                "07-things-to-ask-the-user.md": "# Things to ask\n\n- **Quick dash feel?** OK? _2026-10-23, Tweak gameplay, open_\n"}),
            ("Tweak gameplay: fixed in 1.18.2 (PR #7)", {
                "01-known-bugs.md": "# Known bugs\n\n## Gameplay\n\n- **Tag knockouts never reset between matches** (P2). x. _2026-10-23, Tweak gameplay, fixed in 1.18.2 (PR #7)_\n"}),
            ("07: answered (Tweak gameplay)", {
                "07-things-to-ask-the-user.md": "# Things to ask\n\n- **Quick dash feel?** OK? _2026-10-23, Tweak gameplay, answered: yes_\n",
                "02-currently-working-on.md": "# Currently working on\n\n## In progress\n\n- **Feel review.** Branch claude/project-thread-t02bcv, waiting on the owner's push. _2026-10-24, Tweak gameplay, in-progress_\n"}),
        ]
        cls.versions = []
        for i, (msg, files) in enumerate(steps):
            for n, t in files.items():
                with open(os.path.join(cls.repo, n), "w", newline="") as f: f.write(t)
            git(cls.repo, "add", "-A")
            date = "2026-10-23T2%d:00:00-05:00" % i
            git(cls.repo, "commit", "-q", "-m", msg, env={"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date})
            cls.versions.append(dict(files))
        cls.pc = os.path.join(cls.tmp, "pc")
        os.makedirs(cls.pc)
        for n in os.listdir(cls.repo):
            if n.endswith(".md"):
                data = read(os.path.join(cls.repo, n))
                with open(os.path.join(cls.pc, n), "wb") as f: f.write(data.replace(b"\n", b"\r\n"))   # Windows checkout
        with open(os.path.join(cls.pc, "07-things-to-ask-the-user.md"), "w", newline="") as f:
            f.write(cls.versions[0]["07-things-to-ask-the-user.md"])                                  # an older version
        with open(os.path.join(cls.pc, "01-known-bugs.md"), "a", newline="") as f: f.write("- **PC edit.** new _2026-10-24, x, open_\n")
        cls.state = os.path.join(cls.tmp, "state")
        cls.snap = os.path.join(cls.tmp, "snap.json")
        cls.before = {n: read(os.path.join(cls.pc, n)) for n in os.listdir(cls.pc)}
        cls.argv = ["--repo", cls.repo, "--docs", cls.pc, "--state-dir", cls.state, "--gh-fixture", os.path.join(FIX, "github.json"),
                    "--quark-dir", cls.tmp, "--quiet"]
        assert serve.main(["--snapshot", cls.snap] + cls.argv) == 0
        with open(cls.snap, encoding="utf-8") as f: cls.d = json.load(f)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_snapshot_shape(self):
        d = self.d
        for k in ("generatedAt", "mirrorHead", "mirrorFetchedAt", "fetchOk", "ghOk", "ghFetchedAt", "files", "entries",
                  "changes", "threads", "github", "quarkDir"):
            self.assertIn(k, d)
        self.assertTrue(d["fetchOk"] and d["ghOk"])
        self.assertEqual(d["quarkDir"], self.tmp.replace("\\", "/"))
        e = d["entries"][0]
        for k in ("id", "file", "section", "title", "priority", "body", "date", "thread", "threadUrl", "status", "statusNote",
                  "waitingOn", "waitingWhy", "links", "firstSeen", "lastChanged"):
            self.assertIn(k, e)

    def test_pc_states(self):
        st = {f["name"]: f["pcState"] for f in self.d["files"]}
        self.assertEqual(st, {"01-known-bugs.md": "edited", "02-currently-working-on.md": "same",
                              "07-things-to-ask-the-user.md": "behind", "README.md": "same"})
        self.assertTrue([f for f in self.d["files"] if f["name"] == "01-known-bugs.md"][0]["pcUnsynced"])
        after = {n: read(os.path.join(self.pc, n)) for n in os.listdir(self.pc)}
        self.assertEqual(after, self.before)                                   # never writes the PC folder

    def test_changes_and_lineage(self):
        ch = self.d["changes"]
        self.assertEqual([c["subject"] for c in ch][0], "07: answered (Tweak gameplay)")
        self.assertEqual(ch[0]["time"], "2026-10-23T22:00:00-05:00")
        self.assertEqual(ch[0]["timeUtc"], "2026-10-24T03:00:00Z")
        self.assertEqual(ch[0]["thread"], "Tweak gameplay")
        self.assertEqual(sorted(i["kind"] for i in ch[0]["items"]), ["added", "status"])
        self.assertEqual([i["kind"] for i in ch[1]["items"]], ["status"])                 # title edit + status: one item
        bug = [e for e in self.d["entries"] if e["file"] == "01-known-bugs.md"][0]
        self.assertEqual(bug["firstSeen"]["sha"], ch[2]["sha"])
        self.assertEqual(bug["lastChanged"]["sha"], ch[1]["sha"])
        self.assertEqual(bug["lastChanged"]["what"], "status open -> fixed")
        self.assertEqual([l["state"] for l in bug["links"] if l["kind"] == "pr"], ["merged"])
        feel = [e for e in self.d["entries"] if e["file"].startswith("02")][0]
        self.assertEqual(feel["waitingOn"], ["owner", "push"])
        cache = json.loads(read(os.path.join(self.state, "cache", "changes.json"), "r"))
        self.assertEqual(len(cache["commits"]), 3)

    def test_http(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
        args = serve.argparse.Namespace(**{k: None for k in ("docs", "repo", "state_dir", "threads", "quark_dir", "gh_shared")})
        args.__dict__.update(docs=self.pc, repo=self.repo, state_dir=self.state, branch="main", gh_fixture=os.path.join(FIX, "github.json"),
                             no_gh=False, gh_repo=ghlinks.REPO, quark_dir=self.tmp)
        mgr = serve.Manager(args)
        mgr.gh.refresh(); mgr.build()
        ref = serve.Refresher(mgr, 1e9, 1e9)
        srv = serve.http.server.ThreadingHTTPServer(("127.0.0.1", port), serve.make_handler(mgr, ref, port, True))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            def get(path, headers=None, method="GET"):
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                c.request(method, path, headers=headers or {})
                r = c.getresponse(); body = r.read(); c.close()
                return r.status, dict(r.getheaders()), body
            st, h, body = get("/api/data")
            self.assertEqual(st, 200)
            self.assertEqual(json.loads(body)["mirrorHead"], self.d["mirrorHead"])
            self.assertEqual(get("/api/data", {"If-None-Match": h["ETag"]})[0], 304)
            st, h, body = get("/api/summary", {"Origin": "http://127.0.0.1:8768"})
            self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8768")
            s = json.loads(body)
            self.assertEqual(set(s), {"generatedAt", "mirrorHead", "fetchOk", "ghOk", "inProgress", "waiting", "questions", "recentChanges",
                                      "halting", "haltingCount"})
            self.assertEqual((s["halting"], s["haltingCount"]), ([], 0))
            self.assertEqual([e["title"] for e in s["inProgress"]], ["Feel review."])
            self.assertIn("claude/project-thread-t02bcv", s["inProgress"][0]["where"])
            self.assertNotIn("Access-Control-Allow-Origin", get("/api/summary", {"Origin": "http://evil.example"})[1])
            self.assertEqual(get("/api/data", {"Origin": "http://localhost:8765"}, "OPTIONS")[0], 204)
            self.assertEqual(get("/api/health")[0], 200)
            self.assertEqual(get("/api/data", {"Host": "evil.example"})[0], 403)
            self.assertEqual(get("/serve.py")[0], 404)
            self.assertEqual(get("/js/../serve.py")[0], 404)
            self.assertEqual(get("/api/refresh", method="POST")[0], 202)
        finally:
            srv.shutdown(); srv.server_close(); mgr.mirror.close()


@unittest.skipUnless(HAVE_GIT and os.environ.get("QUARK_DOCS_REPO"), "set QUARK_DOCS_REPO to a Claude Documents clone")
class RealHistoryTests(unittest.TestCase):
    def test_every_version_parses(self):
        repo = os.environ["QUARK_DOCS_REPO"]
        m = serve.Mirror(repo, None)
        m.ref = "HEAD"
        n = 0
        try:
            commits = m.log()
            self.assertTrue(commits)
            for c in commits:
                for name, blob in m.tree(c["sha"]).items():
                    if not name.endswith(".md"): continue
                    p = docsparse.parse_file(name, serve.norm_text(m.cat(blob)), NAMES)
                    for e in p["entries"]:
                        self.assertTrue(e["title"]); self.assertIn(e["status"], docsparse.STATUSES + ["unknown"])
                    n += 1
        finally:
            m.close()
        self.assertGreater(n, 0)


if __name__ == "__main__":
    unittest.main()
