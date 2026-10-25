"""Tests for the Claude Manager's editor (docsedit.py + the write API in serve.py):
python3 -m unittest discover tools/docs_manager/tests

Each test builds a throwaway bare repo and two clones of it: "pc" (the owner's Claude Documents folder, CRLF line
ends like a Windows checkout, no git identity) and "other" (another agent pushing at the same time). Nothing here
touches a real Claude Documents copy; with QUARK_DOCS_REPO set, one test answers a question in a temp clone of it
(its origin pointed at a temp bare repo, never GitHub). Temp dirs follow TMPDIR.
"""
import http.client, json, os, shutil, socket, subprocess, sys, tempfile, threading, unittest

TESTS = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(TESTS)
sys.path.insert(0, TOOL)
import docsedit, docsparse, serve  # noqa: E402

HAVE_GIT = shutil.which("git") is not None
Q = "07-things-to-ask-the-user.md"
B = "03-possible-bugs.md"

FILES = {
    "README.md": "# Claude Documents\n\nShared notes. Rules for entries: `- **Title** ... _date, thread, status_`.\n",
    "00-agent-rules.md": "# Agent rules\n\nRead this first.\n",
    B: ("# Possible bugs\n\nSuspected bugs that still need a repro.\n\n## Game\n\n"
        "- **Rocket jump sometimes double counts.** Seen twice on bastion. _2026-10-22, Tweak gameplay, open_\n"
        "- **Bots stuck on ladders.** On hamlet only. _2026-10-21, Bot editor, open_\n\n"
        "## Tools\n\n- **Map tool leaks a handle.** On Windows. _2026-10-20, Map editor, open_\n\n## Archive\n"),
    Q: ("# Things to ask the user\n\nDecisions waiting on the owner (q).\n\n## Gameplay\n\n"
        "- **Quick dash feel:** 2 charges + 0.4 s cooldown = 8 ground dashes in 4 s (was 4). OK? _2026-10-23, Tweak gameplay, open_\n"
        "- **A/D strafe ceiling OK?** Reaches ~53 m/s after 8 s,\n  ~20.7 m/s after 1 s. Intended? _2026-10-23, Tweak gameplay, open_\n"
        "- **Knives: swing after a throw?** _2026-10-23, Claude Documents setup, answered: no, keep as is_\n\n"
        "## Setup\n\n- **Discord bot checks.** Its role and invite permissions. _2026-10-23, open_\n"),
}


def git(cwd, *args, check=True):
    p = subprocess.run(["git", "-C", cwd] + list(args), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and p.returncode != 0: raise AssertionError("git %s: %s" % (" ".join(args), p.stderr.decode()))
    return p.stdout.decode("utf-8", "replace").strip()


def rd(path):
    with open(path, "rb") as f: return f.read()


def wr(path, data):
    with open(path, "wb") as f: f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


class Repo:
    """bare remote + pc clone (CRLF, no identity) + other clone (LF)."""
    def __init__(self, files=FILES, source=None):
        self.tmp = tempfile.mkdtemp(prefix="cm-edit-")
        self.bare = os.path.join(self.tmp, "remote.git")
        if source:
            subprocess.run(["git", "clone", "-q", "--bare", source, self.bare], check=True)
        else:
            subprocess.run(["git", "init", "-q", "--bare", "-b", "main", self.bare], check=True)
            seed = os.path.join(self.tmp, "seed")
            subprocess.run(["git", "clone", "-q", self.bare, seed], check=True, stderr=subprocess.DEVNULL)
            for n, t in files.items(): wr(os.path.join(seed, n), t)
            git(seed, "add", "-A"); git(seed, "-c", "user.name=seed", "-c", "user.email=s@x", "commit", "-q", "-m", "seed")
            git(seed, "push", "-q", "origin", "HEAD:main")
        self.pc = os.path.join(self.tmp, "pc")
        subprocess.run(["git", "clone", "-q", "-c", "core.autocrlf=true", self.bare, self.pc], check=True)
        if not source:
            assert b"\r\n" in rd(os.path.join(self.pc, Q)), "pc clone should have CRLF line ends"
        self.other = os.path.join(self.tmp, "other")
        subprocess.run(["git", "clone", "-q", self.bare, self.other], check=True)
        git(self.other, "config", "user.name", "other agent"); git(self.other, "config", "user.email", "o@x")

    def other_edit(self, name, old, new, msg="other: edit"):
        git(self.other, "pull", "-q", "--rebase")
        p = os.path.join(self.other, name)
        t = rd(p).decode()
        assert old in t, old
        wr(p, t.replace(old, new, 1))
        git(self.other, "commit", "-q", "-am", msg); git(self.other, "push", "-q")

    def remote_text(self, name):
        return git(self.bare, "show", "main:" + name)

    def offline(self):
        os.rename(self.bare, self.bare + ".away")

    def online(self):
        os.rename(self.bare + ".away", self.bare)

    def close(self):
        shutil.rmtree(self.tmp, ignore_errors=True)


def isolate_git():
    """No global / system git config (so the pc clone has no identity, like a fresh PC), restored after."""
    saved = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM", "GIT_TERMINAL_PROMPT")}
    empty = tempfile.NamedTemporaryFile(prefix="cm-gitconfig-", delete=False); empty.close()
    os.environ.update(GIT_CONFIG_GLOBAL=empty.name, GIT_CONFIG_NOSYSTEM="1", GIT_TERMINAL_PROMPT="0")

    def restore():
        for k, v in saved.items():
            if v is None: os.environ.pop(k, None)
            else: os.environ[k] = v
        os.remove(empty.name)
    return restore


@unittest.skipUnless(HAVE_GIT, "needs git")
class WriterTests(unittest.TestCase):
    def setUp(self):
        self.restore = isolate_git()
        self.r = Repo()
        self.changes = []
        self.w = docsedit.Writer(self.r.pc, on_change=lambda: self.changes.append(1))

    def tearDown(self):
        self.r.close(); self.restore()

    def head_subject(self, cwd=None):
        return git(cwd or self.r.pc, "log", "-1", "--format=%s")

    def assert_crlf(self, name):
        raw = rd(os.path.join(self.r.pc, name))
        self.assertIn(b"\r\n", raw)
        self.assertEqual(raw.count(b"\n"), raw.count(b"\r\n"), "%s has bare LF line ends" % name)
        self.assertFalse(raw.startswith(b"\xef\xbb\xbf"))

    def test_editable_and_read(self):
        self.assertEqual(self.w.editable(), (True, None))
        d = self.w.read(B)
        self.assertEqual(d["eol"], "crlf")
        self.assertNotIn("\r", d["text"])
        self.assertEqual(d["sections"], ["Game", "Tools", "Archive"])
        self.assertEqual(d["sha256"], docsedit.sha256(d["text"]))

    def test_save_clean(self):
        base = self.w.read(B)["text"]
        text = base.replace("Seen twice on bastion.", "Seen twice on bastion, once on temple.")
        res = self.w.save(B, base, text)
        self.assertTrue(res["ok"] and res["pushed"] and res["committed"])
        self.assertFalse(res["merged"])
        self.assertEqual(res["message"], "Saved and pushed.")
        self.assertIn("once on temple", self.r.remote_text(B))
        self.assertEqual(self.head_subject(), "q: edited 03-possible-bugs.md (Claude Manager)")
        self.assertEqual(git(self.r.pc, "log", "-1", "--format=%an"), docsedit.FALLBACK_NAME)
        self.assertEqual(git(self.r.pc, "status", "--porcelain"), "")
        self.assert_crlf(B)
        self.assertEqual(self.w.unpushed(), 0)
        self.assertTrue(self.changes)
        again = self.w.save(B, text, text)                                  # nothing new
        self.assertFalse(again["committed"])
        self.assertIn("Nothing to save", again["message"])

    def test_save_merges_concurrent_edit(self):
        base = self.w.read(B)["text"]
        self.r.other_edit(B, "On Windows.", "On Windows, in the bake step.")
        mine = base.replace("On hamlet only.", "On hamlet and hightown.")
        res = self.w.save(B, base, mine)
        self.assertTrue(res["ok"] and res["merged"] and res["pushed"])
        remote = self.r.remote_text(B)
        self.assertIn("On hamlet and hightown.", remote)
        self.assertIn("in the bake step", remote)
        self.assertEqual(res["text"].replace("\r", ""), remote + "\n")
        self.assert_crlf(B)

    def test_overlapping_edit_conflicts(self):
        base = self.w.read(B)["text"]
        self.r.other_edit(B, "Seen twice on bastion.", "Seen three times on bastion.")
        head = git(self.r.pc, "rev-parse", "HEAD")
        with self.assertRaises(docsedit.ConflictError) as cm:
            self.w.save(B, base, base.replace("Seen twice on bastion.", "Seen twice on bastion (q)."))
        e = cm.exception
        self.assertEqual(e.code, 409)
        p = e.payload()
        self.assertIn("three times", p["current"])
        self.assertIn("<<<<<<< your edit", p["conflicted"])
        self.assertIn(">>>>>>> the newest version", p["conflicted"])
        self.assertIn("(q)", p["mine"])
        self.assertNotIn("(q)", rd(os.path.join(self.r.pc, B)).decode())
        self.assertEqual(git(self.r.pc, "rev-parse", "HEAD"), git(self.r.bare, "rev-parse", "main"))   # pulled, no commit of ours
        self.assertNotEqual(head, git(self.r.pc, "rev-parse", "HEAD"))
        # q resolves: saves against the current text
        res = self.w.save(B, p["current"], p["current"].replace("three times", "three times (q)"))
        self.assertTrue(res["pushed"])
        self.assertIn("three times (q)", self.r.remote_text(B))

    def test_push_failure_then_retry(self):
        base = self.w.read(B)["text"]
        self.r.offline()
        try:
            res = self.w.save(B, base, base.replace("On Windows.", "On Windows (offline 1)."))
            self.assertTrue(res["ok"])
            self.assertFalse(res["pushed"])
            self.assertTrue(res["message"].startswith(docsedit.OFFLINE_MSG))
            self.assertEqual(res["unpushed"], 1)
            self.assertEqual(self.head_subject(), "q: edited 03-possible-bugs.md (Claude Manager)")
            self.assertFalse(self.w.retry_push())                           # still offline
            st = self.w.status()
            self.assertEqual(st["unpushed"], 1)
            self.assertTrue(st["pushError"])
        finally:
            self.r.online()
        self.assertNotIn("offline 1", self.r.remote_text(B))
        self.assertTrue(self.w.retry_push())
        self.assertIn("offline 1", self.r.remote_text(B))
        self.assertEqual(self.w.unpushed(), 0)
        self.assertIsNone(self.w.status()["pushError"])

    def test_next_save_pushes_pending(self):
        t0 = self.w.read(B)["text"]
        self.r.offline()
        try:
            t1 = t0.replace("On Windows.", "On Windows (1).")
            self.assertFalse(self.w.save(B, t0, t1)["pushed"])
        finally:
            self.r.online()
        self.r.other_edit(Q, "Its role and invite permissions.", "Its role, invite permissions.")    # remote moved on too
        res = self.w.save(B, t1, t1.replace("On hamlet only.", "On hamlet only (2)."))
        self.assertTrue(res["pushed"])
        self.assertEqual(res["unpushed"], 0)
        remote = self.r.remote_text(B)
        self.assertIn("(1)", remote); self.assertIn("(2)", remote)

    def test_pull_conflict_refuses_and_aborts(self):
        t0 = self.w.read(B)["text"]
        self.r.offline()
        try:
            self.w.save(B, t0, t0.replace("On Windows.", "On Windows (pc)."))
        finally:
            self.r.online()
        self.r.other_edit(B, "On Windows.", "On Windows (other).")
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.save(Q, self.w.read(Q)["text"], self.w.read(Q)["text"] + "\nmore\n")
        self.assertEqual((cm.exception.code, cm.exception.error), (423, "pull-failed"))
        self.assertFalse(self.w._in_rebase())                               # rebase aborted, folder usable
        self.assertIsNone(self.w._busy_reason())
        self.assertIn("(pc)", rd(os.path.join(self.r.pc, B)).decode())
        self.assertNotIn("more", rd(os.path.join(self.r.pc, Q)).decode())

    def test_busy_folder_refuses(self):
        gp = git(self.r.pc, "rev-parse", "--git-path", "MERGE_HEAD")
        wr(os.path.join(self.r.pc, gp), git(self.r.pc, "rev-parse", "HEAD") + "\n")
        t = self.w.read(B)["text"]
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.save(B, t, t + "x\n")
        self.assertEqual((cm.exception.code, cm.exception.error), (423, "busy"))
        self.assertIn("middle of a merge", cm.exception.message)

    def test_answer_flow(self):
        before = self.w.read(Q)["text"]
        es = {e["title"]: e for e in docsparse.parse_file(Q, before)["entries"]}
        q1 = es["A/D strafe ceiling OK?"]
        res = self.w.answer([{"entryId": q1["id"], "text": "Yes, that's intended,\nkeep it."}])
        self.assertTrue(res["ok"] and res["pushed"])
        self.assertEqual(res["files"], [docsedit.ANSWERS, Q])
        self.assertEqual(self.head_subject(), '09/07: q answered "A/D strafe ceiling OK?" (Claude Manager)')
        after = self.w.read(Q)["text"]
        a, b = before.split("\n"), after.split("\n")
        self.assertEqual(len(a), len(b))
        diff = [i for i in range(len(a)) if a[i] != b[i]]
        self.assertEqual(diff, [q1["endLine"] - 1])                         # only the trailer line of that entry
        i = diff[0]
        self.assertEqual(a[i][:a[i].rindex("_2026")], b[i][:b[i].rindex("_2026")])   # the text before the trailer is unchanged
        self.assertTrue(b[i].endswith("_2026-10-23, Tweak gameplay, answered: Yes, that's intended, keep it. (q, via Claude Manager)_"))
        new = {e["id"]: e for e in docsparse.parse_file(Q, after)["entries"]}
        old = {e["id"]: e for e in docsparse.parse_file(Q, before)["entries"]}
        self.assertEqual(set(new), set(old))
        for k in old:
            if k == q1["id"]: continue
            self.assertEqual(new[k], old[k])
        self.assertEqual(new[q1["id"]]["status"], "answered")
        self.assertEqual(new[q1["id"]]["thread"], "Tweak gameplay")
        self.assertEqual(new[q1["id"]]["body"], old[q1["id"]]["body"])
        t09 = self.w.read(docsedit.ANSWERS)
        self.assertTrue(t09["exists"])
        self.assertTrue(t09["text"].startswith("# Answers from q\n"))
        self.assertIn("newest first", t09["text"])
        p09 = docsparse.parse_file(docsedit.ANSWERS, t09["text"])
        self.assertEqual(len(p09["entries"]), 1)
        e9 = p09["entries"][0]
        self.assertEqual((e9["title"], e9["status"], e9["thread"], e9["section"]), ("A/D strafe ceiling OK?", "open", "q", "Answers"))
        self.assertIn("Yes, that's intended, keep it. Question from Tweak gameplay, asked 2026-10-23.", e9["body"])
        self.assertRegex(rd(os.path.join(self.r.pc, docsedit.ANSWERS)).decode(), r"_\d{4}-\d\d-\d\d \d\d:\d\d CT, q, open_\r\n")
        self.assert_crlf(Q); self.assert_crlf(docsedit.ANSWERS)
        self.assertIn("answered: Yes", self.r.remote_text(Q))
        self.assertIn("A/D strafe", self.r.remote_text(docsedit.ANSWERS))

        # several at once: one commit, each its own entry, in the order given, above the older answer
        q2, q3 = es["Quick dash feel:"], es["Discord bot checks."]
        n0 = int(git(self.r.pc, "rev-list", "--count", "HEAD"))
        res = self.w.answer([{"entryId": q2["id"], "text": "Fine"}, {"entryId": q3["id"], "text": "Role is OK; invite needs Manage Roles"}])
        self.assertEqual(int(git(self.r.pc, "rev-list", "--count", "HEAD")), n0 + 1)
        self.assertEqual(self.head_subject(), "09/07: q answered 2 questions (Claude Manager)")
        titles = [e["title"] for e in docsparse.parse_file(docsedit.ANSWERS, self.w.read(docsedit.ANSWERS)["text"])["entries"]]
        self.assertEqual(titles, ["Quick dash feel:", "Discord bot checks.", "A/D strafe ceiling OK?"])
        q3now = [e for e in docsparse.parse_file(Q, self.w.read(Q)["text"])["entries"] if e["id"] == q3["id"]][0]
        self.assertEqual((q3now["status"], q3now["thread"]), ("answered", None))   # had no thread: still none

        # already answered / gone: nothing written
        head = git(self.r.pc, "rev-parse", "HEAD")
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.answer([{"entryId": es["Knives: swing after a throw?"]["id"], "text": "x"}])
        self.assertEqual(cm.exception.code, 409)
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.answer([{"entryId": Q + ":no-such-question", "text": "x"}])
        self.assertEqual(cm.exception.code, 404)
        with self.assertRaises(docsedit.EditError):
            self.w.answer([{"entryId": q2["id"], "text": "   "}])
        self.assertEqual(git(self.r.pc, "rev-parse", "HEAD"), head)

    def test_answer_non_07_goes_to_09_only(self):
        before = self.w.read(B)["text"]
        e = docsparse.parse_file(B, before)["entries"][0]
        res = self.w.answer([{"entryId": e["id"], "text": "Not a bug, it's the splash"}])
        self.assertEqual(res["files"], [docsedit.ANSWERS])
        self.assertEqual(self.w.read(B)["text"], before)
        self.assertEqual(self.head_subject(), '09: q answered "Rocket jump sometimes double counts" (Claude Manager)')
        self.assertIn("Question from Tweak gameplay in 03-possible-bugs.md, asked 2026-10-22.", self.w.read(docsedit.ANSWERS)["text"])

    def test_add_entry(self):
        res = self.w.add_entry({"file": B, "section": "Tools", "title": "Sound tool crashes on empty file", "priority": "P2",
                                "body": "Open a 0-byte .wav.\nIt crashes.", "status": "open"})
        self.assertTrue(res["pushed"])
        self.assertEqual(self.head_subject(), '03: q added "Sound tool crashes on empty file" (Claude Manager)')
        p = docsparse.parse_file(B, self.w.read(B)["text"])
        tools = [e for e in p["entries"] if e["section"] == "Tools"]
        self.assertEqual([e["title"] for e in tools], ["Sound tool crashes on empty file", "Map tool leaks a handle."])
        e = tools[0]
        self.assertEqual((e["priority"], e["status"], e["thread"], e["body"]), ("P2", "open", "q", "Open a 0-byte .wav. It crashes."))
        self.assertEqual(e["line"], res["line"])
        self.assertRegex(e["date"], r"^\d{4}-\d\d-\d\d$")
        # empty section (Archive, the last one) and a bad section
        self.w.add_entry({"file": B, "section": "Archive", "title": "Old", "status": "done"})
        self.assertEqual([x["title"] for x in docsparse.parse_file(B, self.w.read(B)["text"])["entries"] if x["archived"]], ["Old."])
        res = self.w.add_entry({"file": B, "section": "Tools", "title": "Sign in to gh", "priority": "halting",
                                "body": "Run gh auth login on the PC. The release thread can't push until then.", "status": "open"})
        self.assertIn("- **Sign in to gh** (HALT). Run gh auth login on the PC.", res["entry"])
        h = [x for x in docsparse.parse_file(B, self.w.read(B)["text"])["entries"] if x["title"] == "Sign in to gh"][0]
        self.assertEqual(h["priority"], "HALT")
        self.assertTrue(docsparse.is_halting(h))
        self.assert_crlf(B)
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.add_entry({"file": B, "section": "Nope", "title": "x"})
        self.assertEqual(cm.exception.code, 409)
        for bad in ({"file": "README.md", "section": "", "title": "x"}, {"file": B, "section": "Tools", "title": ""},
                    {"file": B, "section": "Tools", "title": "x", "status": "maybe"}, {"file": B, "section": "Tools", "title": "x", "priority": "P9"}):
            with self.assertRaises(docsedit.EditError):
                self.w.add_entry(bad)

    def test_bom_and_lf_files_kept(self):
        git(self.r.pc, "config", "core.autocrlf", "false")                 # an LF checkout, and a BOM, committed
        p = os.path.join(self.r.pc, "00-agent-rules.md")
        wr(p, b"\xef\xbb\xbf# Agent rules\n\nRead this first.\n")
        git(self.r.pc, "-c", "user.name=t", "-c", "user.email=t@x", "commit", "-q", "-am", "bom"); git(self.r.pc, "push", "-q")
        d = self.w.read("00-agent-rules.md")
        self.assertEqual((d["eol"], d["bom"]), ("lf", True))
        self.w.save("00-agent-rules.md", d["text"], d["text"] + "\nNew rule.\n")
        raw = rd(p)
        self.assertTrue(raw.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r\n", raw)

    def test_whitelist(self):
        for bad in ("1-x.md", "../x.md", "..\\x.md", "07-x.md/../../a.md", "/etc/passwd", "100-x.md", "07-X.md", "07-x.txt", "readme.md",
                    "", None, "07-x.md\n", "C:\\x\\07-a.md", "sync.cmd", ".git/config"):
            self.assertFalse(docsedit.valid_name(bad), bad)
            with self.assertRaises(docsedit.EditError):
                self.w.read(bad)
            with self.assertRaises(docsedit.EditError):
                self.w.save(bad, "", "x")
        for good in ("README.md", "00-agent-rules.md", B, Q, docsedit.ANSWERS):
            self.assertTrue(docsedit.valid_name(good), good)
        self.assertFalse(os.path.exists(os.path.join(self.r.tmp, "x.md")))

    def test_generated_and_report_files_refused(self):
        gen = "# GitHub notes\n\n> Generated by the GitHub Manager; edit it there.\n\n- note\n"
        rep = "# 14: pass two\n\n> Kind: report\n\n## Tab\n\n- **[low] Item.** Text. _2026-10-24, T, fixed in PR #70: x_\n"
        git(self.r.other, "pull", "-q")
        wr(os.path.join(self.r.other, "11-github-notes.md"), gen); wr(os.path.join(self.r.other, "14-pass-two.md"), rep)
        git(self.r.other, "add", "-A"); git(self.r.other, "commit", "-q", "-m", "other: add"); git(self.r.other, "push", "-q")
        head = git(self.r.bare, "rev-parse", "main")
        d = self.w.read("11-github-notes.md", sync=True)
        self.assertTrue(d["generated"])
        with self.assertRaises(docsedit.EditError) as cm:                  # (the page hides it; a script or old draft must not get through)
            self.w.save("11-github-notes.md", d["text"], d["text"] + "\nmore\n")
        self.assertEqual((cm.exception.code, cm.exception.error), (403, "generated"))
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.add_entry({"file": "14-pass-two.md", "section": "Tab", "title": "New"})
        self.assertEqual(cm.exception.error, "not-entries")
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.answer([{"entryId": "14-pass-two.md:low-item", "text": "ok"}])
        self.assertEqual(cm.exception.error, "not-entries")
        with self.assertRaises(docsedit.EditError):
            self.w.add_entry({"file": "13-app-audit.md", "section": "", "title": "New"})       # a report by its name
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.add_entry({"file": B, "section": "Archive", "title": "New", "status": "open"})
        self.assertEqual(cm.exception.error, "archive")
        self.assertEqual(git(self.r.bare, "rev-parse", "main"), head)
        ok = self.w.save("14-pass-two.md", rep, rep + "\nOne more line.\n")                    # a report itself can still be edited
        self.assertTrue(ok["ok"] and ok["pushed"])

    def test_set_status(self):
        n = "06-things-to-tell-the-user.md"
        git(self.r.other, "pull", "-q")
        wr(os.path.join(self.r.other, n), "# Tell\n\n## News\n\n- **Shipped 1.18.32.** On testing. _2026-10-24, Tweak gameplay, open_\n"
                                           "- **Old news.** Done. _2026-10-23, Tweak gameplay, told_\n")
        git(self.r.other, "add", "-A"); git(self.r.other, "commit", "-q", "-m", "06"); git(self.r.other, "push", "-q")
        res = self.w.set_status([{"entryId": n + ":shipped-1-18-32", "status": "told"},
                                 {"entryId": B + ":map-tool-leaks-a-handle", "status": "fixed"},
                                 {"entryId": n + ":old-news", "status": "told"}])                # (already told: skipped)
        self.assertTrue(res["ok"] and res["pushed"])
        self.assertEqual([m["entryId"] for m in res["marked"]], [n + ":shipped-1-18-32", B + ":map-tool-leaks-a-handle"])
        self.assertEqual(self.head_subject(), "03/06: q set 2 items fixed/told (Claude Manager)")
        t = self.r.remote_text(n)
        self.assertIn("_2026-10-24, Tweak gameplay, told (q, via Claude Manager)_", t)
        self.assertIn("Old news.** Done. _2026-10-23, Tweak gameplay, told_", t)
        e = {x["title"]: x for x in docsparse.parse_file(B, self.r.remote_text(B), ["Map editor"])["entries"]}["Map tool leaks a handle."]
        self.assertEqual((e["status"], e["thread"]), ("fixed", "Map editor"))
        self.assert_crlf(B)
        again = self.w.set_status([{"entryId": n + ":shipped-1-18-32", "status": "told"}])
        self.assertFalse(again["committed"])
        for bad in ([], [{"entryId": B + ":x", "status": "open"}], [{"entryId": "13-app-audit.md:x", "status": "done"}]):
            with self.assertRaises(docsedit.EditError):
                self.w.set_status(bad)
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.set_status([{"entryId": B + ":no-such-entry", "status": "done"}])
        self.assertEqual(cm.exception.error, "gone")

    def test_fix_problems(self):
        text = ("# Possible bugs\n\n## Game\n\n- **Split.** Text that wrapped\n## 42 here. _2026-10-22, Tweak gameplay, open_\n"
                "- **Odd.** Body. _2026-10-22, Tweak gameplay, added in PR #70: Open, Reload_\n"
                "- **Tail.** Body. _2026-10-22, Tweak gameplay, done_ More words.\n\n## Archive\n\n"
                "- **Old.** Body. _2026-10-01, Tweak gameplay, fixed_\n\n## Late\n\n- **Below.** Body. _2026-10-22, Tweak gameplay, open_\n")
        base = self.w.read(B)["text"]
        self.w.save(B, base, text)
        codes = lambda: {x["code"]: x for x in docsparse.lint(B, self.w.read(B)["text"], thread_names=["Tweak gameplay"])}
        for code in ("heading-in-entry", "odd-status", "text-after-status", "after-archive"):
            prob = codes()[code]
            res = self.w.fix({"file": B, "code": code, "line": prob["line"], "entryId": prob["entryId"]})
            self.assertTrue(res["ok"] and res["pushed"], code)
            self.assertNotIn(code, codes(), code)
            self.assertEqual(self.head_subject(), "03: q %s at line %d (Claude Manager)" % (docsedit.FIXES[code], prob["line"]))
        t = self.r.remote_text(B)
        self.assertIn("- **Split.** Text that wrapped ## 42 here. _2026-10-22, Tweak gameplay, open_", t)
        self.assertIn("_2026-10-22, Tweak gameplay, done: added in PR #70: Open, Reload_", t)
        self.assertIn("- **Tail.** Body. More words. _2026-10-22, Tweak gameplay, done_", t)
        self.assertTrue(t.rstrip().endswith("## Archive\n\n- **Old.** Body. _2026-10-01, Tweak gameplay, fixed_"), t)
        self.assertLess(t.index("## Late"), t.index("## Archive"))
        with self.assertRaises(docsedit.EditError) as cm:                              # fixed already: nothing to do
            self.w.fix({"file": B, "code": "after-archive", "line": 3})
        self.assertEqual(cm.exception.code, 409)
        with self.assertRaises(docsedit.EditError) as cm:
            self.w.fix({"file": B, "code": "no-status", "line": 3})
        self.assertEqual(cm.exception.error, "not-fixable")

    def test_read_only(self):
        w = docsedit.Writer(self.r.pc, read_only=True)
        self.assertFalse(w.editable()[0])
        t = w.read(B)["text"]
        with self.assertRaises(docsedit.EditError) as cm:
            w.save(B, t, t + "x")
        self.assertEqual((cm.exception.code, cm.exception.error), (403, "read-only"))
        with self.assertRaises(docsedit.EditError):
            w.answer([{"entryId": Q + ":quick-dash-feel", "text": "x"}])
        self.assertEqual(w.read(B, sync=True)["synced"], None)              # read-only never pulls either

    def test_not_a_clone(self):
        plain = os.path.join(self.r.tmp, "plain")
        os.makedirs(plain)
        wr(os.path.join(plain, B), FILES[B])
        ok, why = docsedit.Writer(plain).editable()
        self.assertFalse(ok)
        self.assertIn("isn't a git copy", why)


@unittest.skipUnless(HAVE_GIT, "needs git")
class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.restore = isolate_git()
        cls.r = Repo()
        cls.state = os.path.join(cls.r.tmp, "state")
        cls.servers = []

    @classmethod
    def tearDownClass(cls):
        for srv, mgr in cls.servers:
            srv.shutdown(); srv.server_close(); mgr.mirror.close()
        cls.r.close(); cls.restore()

    def start(self, read_only=False):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
        args = serve.argparse.Namespace(**{k: None for k in ("docs", "repo", "state_dir", "threads", "quark_dir", "gh_shared", "gh_fixture")})
        args.__dict__.update(docs=self.r.pc, repo=self.r.bare, state_dir=self.state + str(port), branch="main", no_gh=True,
                             gh_repo="floration/quark", quark_dir=self.r.tmp, read_only=read_only)
        mgr = serve.Manager(args)
        mgr.mirror.fetch(); mgr.build()
        ref = serve.Refresher(mgr, 1e9, 1e9)
        srv = serve.Server(("127.0.0.1", port), serve.make_handler(mgr, ref, port, True))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.servers.append((srv, mgr))
        return port, mgr

    def req(self, port, method, path, body=None, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
        data = json.dumps(body).encode() if body is not None else None
        h = {"Content-Type": "application/json"} if data is not None else {}
        h.update(headers or {})
        c.request(method, path, body=data, headers=h)
        r = c.getresponse(); raw = r.read(); c.close()
        try: js = json.loads(raw)
        except ValueError: js = None
        return r.status, dict(r.getheaders()), js

    def test_api(self):
        port, mgr = self.start()
        own = {"Origin": "http://127.0.0.1:%d" % port, "X-Claude-Manager": "1"}
        st, _, d = self.req(port, "GET", "/api/data")
        self.assertEqual(st, 200)
        self.assertTrue(d["canEdit"])
        self.assertFalse(d["readOnly"])
        st, _, doc = self.req(port, "GET", "/api/doc?file=" + B + "&sync=1", headers={"X-Claude-Manager": "1"})
        self.assertEqual(st, 200)
        self.assertTrue(doc["synced"])
        for h in ({}, {"X-Claude-Manager": "1", "Origin": "http://127.0.0.1:8768"}):        # a pull needs the page's own request
            self.assertIsNone(self.req(port, "GET", "/api/doc?file=" + B + "&sync=1", headers=h)[2]["synced"])
        self.assertEqual(self.req(port, "GET", "/api/doc?file=../x.md")[0], 400)
        self.assertEqual(self.req(port, "GET", "/api/doc?file=%2e%2e%2fx.md")[0], 400)
        body = {"file": B, "base": doc["text"], "text": doc["text"].replace("On Windows.", "On Windows (http).")}

        # refused: other origins (a sibling tool too), cross-site fetches, no header, wrong type, bad name
        for h in ({"Origin": "http://127.0.0.1:8768", "X-Claude-Manager": "1"}, {"Origin": "http://evil.example", "X-Claude-Manager": "1"},
                  {"Origin": "null", "X-Claude-Manager": "1"}, {"X-Claude-Manager": "1", "Sec-Fetch-Site": "cross-site"},
                  {"X-Claude-Manager": "1", "Sec-Fetch-Site": "same-site"}, {"Origin": "http://127.0.0.1:%d" % port}, {}):
            st, hd, js = self.req(port, "POST", "/api/doc/save", body, h)
            self.assertEqual(st, 403, h)
            self.assertNotIn("Access-Control-Allow-Origin", hd)
        self.assertEqual(self.req(port, "POST", "/api/doc/save", body, dict(own, **{"Content-Type": "text/plain"}))[0], 415)
        st, _, js = self.req(port, "POST", "/api/doc/save", dict(body, file="../x.md"), own)
        self.assertEqual((st, js["error"]), (400, "bad-name"))
        for path in sorted(serve.WRITE_PATHS):                               # preflights for writes never allow other origins
            st, hd, _ = self.req(port, "OPTIONS", path, headers={"Origin": "http://127.0.0.1:8768", "Access-Control-Request-Method": "POST",
                                                               "Access-Control-Request-Headers": "x-claude-manager, content-type"})
            self.assertEqual(st, 403)
            self.assertNotIn("Access-Control-Allow-Origin", hd)
        st, hd, _ = self.req(port, "GET", "/api/data", headers={"Origin": "http://127.0.0.1:8768"})
        self.assertEqual(hd.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8768")     # reads still shared
        self.assertNotIn("(http)", self.r.remote_text(B))

        # accepted: own origin, or no origin with the header (curl)
        st, _, js = self.req(port, "POST", "/api/doc/save", body, own)
        self.assertEqual(st, 200, js)
        self.assertTrue(js["pushed"])
        self.assertIn("(http)", self.r.remote_text(B))
        st, _, js = self.req(port, "POST", "/api/doc/save", dict(body, base=body["text"], text=body["text"] + "\nEnd.\n"),
                             {"X-Claude-Manager": "1"})
        self.assertEqual(st, 200, js)
        st, _, js = self.req(port, "POST", "/api/doc/save", dict(body, text=doc["text"].replace("On Windows.", "On Linux.")), own)  # stale base
        self.assertEqual(st, 409)
        self.assertEqual(js["error"], "conflict")
        self.assertIn("<<<<<<<", js["conflicted"])
        q = [e for e in d["entries"] if e["file"] == Q and e["status"] == "open"][0]
        st, _, js = self.req(port, "POST", "/api/answer", {"answers": [{"entryId": q["id"], "text": "OK by me"}]}, own)
        self.assertEqual(st, 200, js)
        self.assertEqual(js["answered"][0]["entryId"], q["id"])
        st, _, js = self.req(port, "POST", "/api/entry/add", {"file": B, "section": "Game", "title": "From http", "status": "open"},
                             dict(own, Origin="http://localhost:%d" % port))
        self.assertEqual(st, 200, js)
        st, _, es = self.req(port, "GET", "/api/edit/status")
        self.assertEqual((es["canEdit"], es["unpushed"]), (True, 0))
        with socket.create_connection(("127.0.0.1", port), timeout=10) as s:     # too big: refused before reading it
            s.sendall(("POST /api/doc/save HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\n"
                       "X-Claude-Manager: 1\r\nContent-Length: %d\r\n\r\n" % (port, serve.MAX_BODY + 10)).encode())
            self.assertIn(b" 413 ", s.recv(200))

    def test_read_only_server(self):
        port, mgr = self.start(read_only=True)
        own = {"Origin": "http://127.0.0.1:%d" % port, "X-Claude-Manager": "1"}
        st, _, d = self.req(port, "GET", "/api/data")
        self.assertFalse(d["canEdit"])
        self.assertTrue(d["readOnly"])
        head = git(self.r.pc, "rev-parse", "HEAD")
        for path, body in (("/api/doc/save", {"file": B, "base": "", "text": "x"}), ("/api/answer", {"answers": [{"entryId": Q + ":x", "text": "y"}]}),
                           ("/api/entry/add", {"file": B, "section": "Game", "title": "x"})):
            st, _, js = self.req(port, "POST", path, body, own)
            self.assertEqual((st, js["error"]), (403, "read-only"))
        self.assertEqual(git(self.r.pc, "rev-parse", "HEAD"), head)
        self.assertEqual(self.req(port, "GET", "/api/edit/status")[2]["canEdit"], False)


@unittest.skipUnless(HAVE_GIT and os.environ.get("QUARK_DOCS_REPO"), "set QUARK_DOCS_REPO to a Claude Documents clone")
class RealDocsTests(unittest.TestCase):
    """Answers the first open question of a temp copy of the real docs: only that entry's trailer line changes."""
    def test_answer_real_07(self):
        restore = isolate_git()
        r = Repo(source=os.environ["QUARK_DOCS_REPO"])
        try:
            w = docsedit.Writer(r.pc)
            self.assertEqual(w.editable(), (True, None))
            before = w.read(Q)["text"]
            old = docsparse.parse_file(Q, before)["entries"]
            q = [e for e in old if e["status"] == "open" and not e["archived"]][0]
            res = w.answer([{"entryId": q["id"], "text": "Test answer, from the unit test"}])
            self.assertTrue(res["pushed"])
            after = w.read(Q)["text"]
            a, b = before.split("\n"), after.split("\n")
            self.assertEqual([i for i in range(len(a)) if a[i] != b[i]], [q["endLine"] - 1])
            new = {e["id"]: e for e in docsparse.parse_file(Q, after)["entries"]}
            for e in old:
                if e["id"] != q["id"]: self.assertEqual(new[e["id"]], e)
            self.assertEqual(new[q["id"]]["status"], "answered")
            self.assertEqual(new[q["id"]]["thread"], q["thread"])
            self.assertEqual(git(r.pc, "remote", "get-url", "origin"), r.bare)       # never GitHub
        finally:
            r.close(); restore()


if __name__ == "__main__":
    unittest.main()
