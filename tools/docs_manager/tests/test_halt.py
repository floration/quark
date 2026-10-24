"""Tests for the HALT priority (a Claude agent stopped until q acts): parsing, the halting flag and waiting-on,
the change-log wording, and the /api/summary + /api/halting shape, CORS and ETag (for the Supervisor Board).
python3 -m unittest discover tools/docs_manager/tests
"""
import http.client, json, os, shutil, socket, subprocess, sys, tempfile, threading, unittest

TESTS = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(TESTS)
sys.path.insert(0, TOOL)
import docsparse, ghlinks, serve  # noqa: E402

FIX = os.path.join(TESTS, "fixtures")
HAVE_GIT = shutil.which("git") is not None
Q = "07-things-to-ask-the-user.md"


def one(text, name=Q):
    return docsparse.parse_file(name, "# X\n\n## Open\n\n" + text + "\n")["entries"][0]


def entry(file, title, status="open", priority="HALT", body="Run gh auth login on the PC. Then it pushes.", **kw):
    e = {"id": "%s:%s" % (file, docsparse.slug(title)), "file": file, "section": "S", "title": title, "priority": priority,
         "body": body, "date": "2026-10-24", "thread": "Tweak gameplay", "status": status, "statusNote": "", "statusText": status,
         "archived": False, "line": 1}
    e.update(kw)
    return e


class HaltParseTests(unittest.TestCase):
    def test_spellings(self):
        for text in ("- **Sign in** (HALT). Do it. _2026-10-24, T, open_", "- **Sign in** (halting). Do it. _2026-10-24, T, open_",
                     "- **Sign in** (Halt). Do it. _2026-10-24, T, open_", "- **Sign in (HALTING)**: Do it. _2026-10-24, T, open_",
                     "- **Sign in** ( halt ). Do it. _2026-10-24, T, open_"):
            e = one(text)
            self.assertEqual((e["title"], e["priority"], e["body"]), ("Sign in", "HALT", "Do it."), text)

    def test_others_unchanged(self):
        self.assertEqual(one("- **Bug** (P1). x. _2026-10-24, T, open_")["priority"], "P1")
        self.assertEqual(one("- **Bug** (P2/P3). x. _2026-10-24, T, open_")["priority"], "P2")
        self.assertIsNone(one("- **Bug.** x. _2026-10-24, T, open_")["priority"])
        self.assertIsNone(one("- **Rule.** Write it as a `(HALT)` entry. _2026-10-24, T, open_")["priority"])   # quoted, not a mark
        self.assertIsNone(one("- **Halting problem.** It halts. _2026-10-24, T, open_")["priority"])

    def test_rank(self):
        self.assertEqual(sorted(["P3", "P1", "HALT", "P2"], key=docsparse.PRIORITY_RANK.get), ["HALT", "P1", "P2", "P3"])
        self.assertEqual(docsparse.norm_priority("halting"), "HALT")
        self.assertEqual(docsparse.norm_priority("P2"), "P2")

    def test_is_halting(self):
        for st in ("open", "in-progress", "unknown"):
            self.assertTrue(docsparse.is_halting(entry(Q, "A", status=st)), st)
        for st in ("fixed", "done", "answered", "told", "wontfix"):
            self.assertFalse(docsparse.is_halting(entry(Q, "A", status=st)), st)
        self.assertFalse(docsparse.is_halting(entry(Q, "A", archived=True)))
        self.assertFalse(docsparse.is_halting(entry(Q, "A", priority="P1")))


class HaltWaitingTests(unittest.TestCase):
    def test_waiting_on_owner(self):
        w, why, ns = serve.waiting_on(entry("02-currently-working-on.md", "Push", status="in-progress"), [])
        self.assertEqual(w, ["owner"])
        self.assertEqual(why, "A Claude agent is stopped until q acts: Run gh auth login on the PC.")
        self.assertFalse(ns)
        w, why, _ = serve.waiting_on(entry("08-other.md", "Note"), [])                 # 08 is otherwise "nothing"
        self.assertEqual(w, ["owner"])
        w, why, _ = serve.waiting_on(entry("01-known-bugs.md", "B"), [{"kind": "pr", "label": "PR #9", "state": "open"}])
        self.assertEqual(w, ["owner", "pr"])
        self.assertTrue(why.startswith("A Claude agent is stopped until q acts: Run gh auth login on the PC; PR #9 is open"))
        for st in ("answered", "done", "fixed", "told", "wontfix"):
            self.assertEqual(serve.waiting_on(entry(Q, "A", status=st), [])[0], ["none"])

    def test_what(self):
        self.assertEqual(serve.halt_what(entry(Q, "A", body="Pick **0.4 s** or `0.6 s` for the owner. Then more.")),
                         "Pick 0.4 s or 0.6 s for q.")
        self.assertEqual(serve.halt_what(entry(Q, "Sign in to gh.", body="")), "Sign in to gh")
        self.assertEqual(serve.halt_what(entry(Q, "A", body="Run v1.2.3 then say push")), "Run v1.2.3 then say push")


class HaltChangeTests(unittest.TestCase):
    def test_wording(self):
        p1 = [one("- **Log in to gh** (P1). Run gh auth login. _2026-10-24, T, open_")]
        h = [one("- **Log in to gh** (HALT). Run gh auth login. _2026-10-24, T, open_")]
        done = [one("- **Log in to gh** (HALT). Run gh auth login. _2026-10-24, T, answered: done_")]
        it = docsparse.diff_entries([], h)[0]
        self.assertEqual((it["kind"], it["summary"], it["halting"]),
                         ("added", "Now halting a Claude agent: Log in to gh (new question for q)", True))
        it = docsparse.diff_entries(p1, h)[0]
        self.assertEqual((it["kind"], it["summary"], it["what"]),
                         ("edited", "Now halting a Claude agent: Log in to gh (priority P1 -> HALT)", "now halting a Claude agent (priority P1 -> HALT)"))
        it = docsparse.diff_entries(h, done)[0]
        self.assertEqual((it["kind"], it["from"], it["to"], it["halting"]), ("status", "open", "answered", False))
        self.assertTrue(it["summary"].startswith("No longer halting: Log in to gh (open -> answered"))
        it = docsparse.diff_entries(h, p1)[0]
        self.assertEqual(it["summary"], "No longer halting: Log in to gh (priority HALT -> P1)")
        it = docsparse.diff_entries(h, [])[0]
        self.assertEqual((it["kind"], it["halting"]), ("removed", False))
        self.assertTrue(it["summary"].startswith("No longer halting: Log in to gh (removed from"))
        it = docsparse.diff_entries([], p1)[0]                                          # not halting: no flag, old words
        self.assertEqual(it["summary"], "New question for q: Log in to gh")
        self.assertNotIn("halting", it)
        self.assertEqual(docsparse.diff_entries(h, [dict(h[0], body="Run gh auth login now.")])[0].get("halting"), None)


def git(cwd, *args, env=None):
    subprocess.run(["git", "-C", cwd] + list(args), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   env=dict(os.environ, **(env or {})))


@unittest.skipUnless(HAVE_GIT, "git not installed")
class HaltEndToEndTests(unittest.TestCase):
    """Three commits: a P1 question becomes HALT and a HALT possible bug is added; then that bug is fixed and a
    (halting) note is added. Checks the entries, the change log, /api/summary and /api/halting."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="docsmgr-halt-")
        cls.repo = os.path.join(cls.tmp, "repo")
        os.makedirs(cls.repo)
        git(cls.repo, "init", "-q", "-b", "main")
        git(cls.repo, "config", "user.email", "t@example.com"); git(cls.repo, "config", "user.name", "Claude")
        work = ("# Currently working on\n\n## In progress\n\n- **Feel review.** Branch claude/project-thread-t02bcv. "
                "_2026-10-23, Tweak gameplay, in-progress_\n")
        q = "# Things to ask\n\n## Open\n\n- **Pick the dash cooldown** (%s). Pick 0.4 s or 0.6 s; the Tweak gameplay thread waits. Both feel fine. _2026-10-23, Tweak gameplay, open_\n"
        bug = "# Possible bugs\n\n## Game\n\n- **Approve the map** (HALT). Look at temple in the editor. _2026-10-23, Tweak gameplay, %s_\n"
        note = ("# Things to tell\n\n- **Log in to the Discord bot** (halting). Sign in to the bot on the PC with `tools/bot login`. "
                "Then it can post. _2026-10-23, Shared Claude documents folder, open_\n")
        steps = [("Claim: Tweak gameplay", {"02-currently-working-on.md": work, Q: q % "P1"}),
                 ("07: HALT (Tweak gameplay)", {Q: q % "HALT", "03-possible-bugs.md": bug % "open"}),
                 ("03 fixed, 06 halting (Tweak gameplay)", {"03-possible-bugs.md": bug % "fixed", "06-things-to-tell-the-user.md": note})]
        for i, (msg, files) in enumerate(steps):
            for n, t in files.items():
                with open(os.path.join(cls.repo, n), "w", newline="") as f: f.write(t)
            git(cls.repo, "add", "-A")
            date = "2026-10-23T2%d:00:00-05:00" % i
            git(cls.repo, "commit", "-q", "-m", msg, env={"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date})
        cls.state = os.path.join(cls.tmp, "state")
        args = serve.argparse.Namespace(**{k: None for k in ("docs", "state_dir", "threads", "quark_dir", "gh_shared")})
        args.__dict__.update(repo=cls.repo, state_dir=cls.state, branch="main", gh_fixture=os.path.join(FIX, "github.json"),
                             no_gh=False, gh_repo=ghlinks.REPO, quark_dir=cls.tmp, port=8766)
        cls.mgr = serve.Manager(args)
        assert cls.mgr.mirror.fetch()
        cls.mgr.gh.refresh(); cls.d = cls.mgr.build()
        cls.by = {e["id"]: e for e in cls.d["entries"]}

    @classmethod
    def tearDownClass(cls):
        cls.mgr.mirror.close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    QID = Q + ":pick-the-dash-cooldown"
    NID = "06-things-to-tell-the-user.md:log-in-to-the-discord-bot"
    BID = "03-possible-bugs.md:approve-the-map"

    def test_entries(self):
        q, n, b = self.by[self.QID], self.by[self.NID], self.by[self.BID]
        self.assertEqual((q["priority"], q["halting"], q["waitingOn"]), ("HALT", True, ["owner"]))
        self.assertEqual(q["waitingWhy"], "A Claude agent is stopped until q acts: Pick 0.4 s or 0.6 s; the Tweak gameplay thread waits.")
        self.assertEqual(q["haltingSince"], "2026-10-24T02:00:00Z")                     # commit 2, not when first written
        self.assertEqual(q["firstSeen"]["timeUtc"], "2026-10-24T01:00:00Z")
        self.assertEqual(q["lastChanged"]["what"], "now halting a Claude agent (priority P1 -> HALT)")
        self.assertEqual((n["priority"], n["halting"], n["haltingSince"]), ("HALT", True, "2026-10-24T03:00:00Z"))
        self.assertEqual(n["haltWhat"], "Sign in to the bot on the PC with tools/bot login.")
        self.assertEqual((b["priority"], b["halting"], b["haltingSince"], b["waitingOn"]), ("HALT", False, None, ["none"]))
        self.assertEqual(b["lastChanged"]["what"], "no longer halting (open -> fixed)")
        self.assertFalse(self.by["02-currently-working-on.md:feel-review"]["halting"])

    def test_change_log(self):
        sums = [[it["summary"] for it in c["items"]] for c in self.d["changes"]]
        self.assertIn("No longer halting: Approve the map (open -> fixed)", sums[0])
        self.assertIn("Now halting a Claude agent: Log in to the Discord bot (new note for q)", sums[0])
        self.assertIn("Now halting a Claude agent: Pick the dash cooldown (priority P1 -> HALT)", sums[1])
        self.assertIn("Now halting a Claude agent: Approve the map (new possible bug)", sums[1])
        self.assertFalse(any("halting" in s.lower() for s in sums[2]))

    def test_summary_and_halting_shape(self):
        s = serve.make_summary(self.d, 8766)
        self.assertEqual(s["haltingCount"], 2)
        h = s["halting"]
        self.assertEqual([x["id"] for x in h], [self.NID, self.QID])                    # newest first
        keys = {"id", "title", "thread", "threadUrl", "file", "section", "what", "body", "date", "since", "url", "link", "blocks",
                "blocksEntryId", "status"}
        for x in h: self.assertEqual(set(x), keys)
        q = h[1]
        self.assertEqual(q["url"], "http://127.0.0.1:8766/#entry=07-things-to-ask-the-user.md%3Apick-the-dash-cooldown")
        self.assertEqual(q["link"], q["url"])
        self.assertEqual((q["what"], q["since"], q["thread"], q["file"], q["section"]),
                         ("Pick 0.4 s or 0.6 s; the Tweak gameplay thread waits.", "2026-10-24T02:00:00Z", "Tweak gameplay", Q, "Open"))
        self.assertEqual((q["blocks"], q["blocksEntryId"]), ("Tweak gameplay: Feel review", "02-currently-working-on.md:feel-review"))
        self.assertTrue(q["threadUrl"].startswith("https://"))
        self.assertEqual((h[0]["blocks"], h[0]["blocksEntryId"]), ("Shared Claude documents folder", None))
        hal = serve.make_halting(self.d, 8766)
        self.assertEqual(set(hal), {"generatedAt", "count", "items"})
        self.assertEqual((hal["count"], hal["items"]), (2, h))

    def test_http(self):
        with socket.socket() as so:
            so.bind(("127.0.0.1", 0)); port = so.getsockname()[1]
        ref = serve.Refresher(self.mgr, 1e9, 1e9)
        srv = serve.http.server.ThreadingHTTPServer(("127.0.0.1", port), serve.make_handler(self.mgr, ref, port, True))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            def get(path, headers=None, method="GET"):
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                c.request(method, path, headers=headers or {})
                r = c.getresponse(); body = r.read(); c.close()
                return r.status, dict(r.getheaders()), body
            for path in ("/api/halting", "/api/summary"):
                st, hd, body = get(path, {"Origin": "http://127.0.0.1:8768"})
                self.assertEqual(st, 200, path)
                self.assertEqual(hd.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8768")
                self.assertIn("ETag", hd.get("Access-Control-Expose-Headers", ""))
                self.assertEqual(get(path, {"If-None-Match": hd["ETag"], "Origin": "http://127.0.0.1:8768"})[0], 304)
                self.assertNotIn("Access-Control-Allow-Origin", get(path, {"Origin": "http://evil.example"})[1])
                self.assertEqual(get(path, {"Origin": "http://localhost:8768"}, "OPTIONS")[0], 204)
            st, hd, body = get("/api/halting")
            hal = json.loads(body)
            self.assertEqual(set(hal), {"generatedAt", "count", "items"})
            self.assertEqual(hal["count"], 2)
            self.assertEqual(hal["items"], json.loads(get("/api/summary")[2])["halting"])
            etag = hd["ETag"]
            self.mgr.build()                                                             # same docs: same ETag
            self.assertEqual(get("/api/halting")[1]["ETag"], etag)
            self.assertEqual(get("/api/halting", {"Host": "evil.example"})[0], 403)
        finally:
            srv.shutdown(); srv.server_close()


if __name__ == "__main__":
    unittest.main()
