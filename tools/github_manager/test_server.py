#!/usr/bin/env python3
"""Unit tests for server.py (stdlib unittest, no network): python3 tools/github_manager/test_server.py

Fixture data for the parsers and the snapshot -> history diff, plus one offline end-to-end poll: a local git
repository stands in for GitHub's (the mirror's originUrl), canned `gh api` answers stand in for the REST API.
"""
import sys

sys.dont_write_bytecode = True

import gzip
import json
import logging
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

os.environ["GHM_DOCS_SYNC"] = "off"  # no Manager made here ever pushes to the real Claude Documents
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "test"))
import server as S  # noqa: E402
from fake_github import MemoryGitHub, testing_guard, sha_of, PRODUCTION_RULE, TAGGING_RULE  # noqa: E402

AT0 = "2026-10-24T10:00:00Z"
AT1 = "2026-10-24T10:01:00Z"


def snap(at=AT1, **kw):
    s = {"at": at, "repo": "o/r", "branches": {}, "tags": {}, "releases": {}, "pulls": {}, "runs": {}, "local": {},
         "pollError": None}
    s.update(kw)
    return s


def types(events):
    return [e["type"] for e in events]


class Parsers(unittest.TestCase):
    def test_status_v2(self):
        out = "\0".join([
            "# branch.oid 1111111111111111111111111111111111111111",
            "# branch.head working",
            "# branch.upstream origin/testing",
            "# branch.ab +2 -1",
            "1 .M N... 100644 100644 100644 aaaa bbbb src/x.cpp",
            "1 A. N... 000000 100644 100644 0000 cccc docs/new file.md",
            "2 R. N... 100644 100644 100644 dddd dddd R100 tools/new name.py",
            "tools/old name.py",
            "u UU N... 100644 100644 100644 100644 e1 e2 e3 conflict.txt",
            "? untracked dir/file.txt",
        ]) + "\0"
        st = S.parse_status_v2(out)
        self.assertEqual(st["oid"], "1" * 40)
        self.assertEqual(st["branch"], "working")
        self.assertEqual(st["upstream"], "origin/testing")
        self.assertEqual(st["dirty"], 5)
        self.assertEqual(st["dirtyFiles"], ["M src/x.cpp", "A docs/new file.md",
                                            "R tools/old name.py -> tools/new name.py", "UU conflict.txt",
                                            "?? untracked dir/file.txt"])

    def test_status_v2_detached_initial_and_cap(self):
        st = S.parse_status_v2("# branch.oid (initial)\0# branch.head (detached)\0")
        self.assertIsNone(st["oid"])
        self.assertIsNone(st["branch"])
        self.assertEqual(st["dirty"], 0)
        many = "\0".join("? f%d" % i for i in range(80)) + "\0"
        st = S.parse_status_v2(many)
        self.assertEqual(st["dirty"], 80)
        self.assertEqual(len(st["dirtyFiles"]), S.MAX_DIRTY_FILES)

    def test_worktree_list(self):
        out = ("worktree C:/Users/q/Desktop/Quark/working-repo\nHEAD " + "a" * 40 + "\nbranch refs/heads/testing\n\n"
               "worktree C:/Users/q/Desktop/Quark/working-repo-server\nHEAD " + "b" * 40 + "\ndetached\n\n"
               "worktree /gone\nHEAD " + "c" * 40 + "\nbranch refs/heads/x\nprunable gitdir file points to "
               "non-existent location\n\n")
        w = S.parse_worktree_list(out)
        self.assertEqual(len(w), 3)
        self.assertEqual(w[0]["branch"], "testing")
        self.assertEqual(w[0]["path"], os.path.normpath("C:/Users/q/Desktop/Quark/working-repo"))
        self.assertTrue(w[1]["detached"])
        self.assertIsNone(w[1]["branch"])
        self.assertTrue(w[2]["prunable"])

    def test_origin_matches(self):
        for u in ("https://github.com/floration/quark", "https://github.com/floration/quark.git\n",
                  "git@github.com:floration/quark.git", "ssh://git@github.com/Floration/Quark/",
                  "https://x-access-token@github.com/floration/quark"):
            self.assertTrue(S.origin_matches(u, "floration/quark"), u)
        for u in ("https://github.com/floration/quark-docs", "https://github.com/other/quark", "", None):
            self.assertFalse(S.origin_matches(u, "floration/quark"), u)

    def test_branch_kind(self):
        d = "claude/compassionate-ritchie-07ah6i"
        self.assertEqual(S.branch_kind(d, d), "production")
        self.assertEqual(S.branch_kind("testing", d), "testing")
        self.assertEqual(S.branch_kind("claude/project-thread-x", d), "claude")
        for n in ("launcher", "site", "discord-bot"):
            self.assertEqual(S.branch_kind(n, d), "infra")
        self.assertEqual(S.branch_kind("dependabot/x", d), "other")

    def test_gh_include(self):
        out = ("HTTP/1.1 304 Not Modified\r\nEtag: \"abc\"\r\nX-Ratelimit-Remaining: 42\r\nLink: <https://x?page=2>; "
               "rel=\"next\"\r\n\r\n")
        st, h, body = S.parse_gh_include(out)
        self.assertEqual(st, 304)
        self.assertEqual(h["etag"], '"abc"')
        self.assertTrue(S.link_has_next(h["link"]))
        st, h, body = S.parse_gh_include('HTTP/2.0 200 OK\nContent-Type: x\n\n{"a": 1}')
        self.assertEqual((st, json.loads(body)), (200, {"a": 1}))

    def test_with_query(self):
        self.assertEqual(S.with_query("repos/o/r/pulls?state=open&per_page=100", page=2),
                         "repos/o/r/pulls?state=open&per_page=100&page=2")
        self.assertEqual(S.with_query("a?page=1", page=3), "a?page=3")

    def test_checks_state(self):
        ok = {"status": "completed", "conclusion": "success"}
        self.assertEqual(S.checks_state([]), "none")
        self.assertEqual(S.checks_state([ok]), "success")
        self.assertEqual(S.checks_state([ok, {"status": "in_progress", "conclusion": None}]), "pending")
        self.assertEqual(S.checks_state([ok, {"status": "completed", "conclusion": "failure"}]), "failure")
        self.assertEqual(S.checks_state([{"status": "completed", "conclusion": "cancelled"}]), "failure")
        self.assertEqual(S.checks_state([ok, {"status": "completed", "conclusion": "cancelled"}]), "success")

    def test_run_summary(self):
        r = {"id": 5, "name": "build-and-release", "head_branch": "testing", "head_sha": "f" * 40, "event": "push",
             "display_title": "t", "status": "completed", "conclusion": "failure",
             "created_at": "2026-10-24T10:00:00Z", "run_started_at": "2026-10-24T10:00:10Z",
             "updated_at": "2026-10-24T10:10:10Z", "run_attempt": 2, "html_url": "u",
             "pull_requests": [{"number": 8}], "path": ".github/workflows/release.yml"}
        s = S.run_summary(r)
        self.assertEqual(s["workflowFile"], "release.yml")
        self.assertEqual(S.run_summary(dict(r, path="dynamic/x/y.yml@refs/heads/t"))["workflowFile"], "y.yml")
        self.assertIsNone(S.run_summary(dict(r, path=None))["workflowFile"])
        self.assertEqual(s["durationSec"], 600)
        self.assertEqual(s["attempt"], 2)
        self.assertEqual(s["prs"], [8])
        for k in ("id", "workflow", "branch", "sha", "event", "title", "status", "conclusion", "createdAt",
                  "updatedAt", "durationSec", "attempt", "url"):
            self.assertIn(k, s)

    def test_last_testing_run(self):
        r = lambda i, c, **kw: dict({"id": i, "branch": "testing", "workflow": "build-and-release",
                                     "status": "completed", "conclusion": c, "createdAt": "2026-10-24T0%d:00:00Z" % i,
                                     "jobs": []}, **kw)
        runs = [r(5, None, status="in_progress"), r(4, "success", workflow="main-guard"), r(3, "success", branch="x"),
                r(2, "success"), r(1, "failure")]  # a re-run of 2 that passed: same id, newest completed
        self.assertEqual(S.last_testing_run(runs)["id"], 2)
        self.assertNotIn("jobs", S.last_testing_run(runs))
        self.assertIsNone(S.last_testing_run(runs[:1]))

    def test_job_info_failed_steps(self):
        j = S.job_info({"id": 1, "name": "windows-tests", "status": "completed", "conclusion": "failure",
                        "steps": [{"name": "build", "conclusion": "success"}, {"name": "quark_tests",
                                                                                "conclusion": "failure"}]})
        self.assertEqual(j["failedSteps"], ["quark_tests"])

    def test_cors(self):
        for o in ("http://127.0.0.1:8768", "http://localhost:8765", "http://127.0.0.1:8766", "http://localhost:8767",
                  "http://127.0.0.1:8775", "http://localhost:8775"):
            self.assertEqual(S.cors_origin(o), o)
        for o in ("http://127.0.0.1:8769", "http://127.0.0.1:8774", "http://127.0.0.1:87750", "http://evil.example",
                  "https://127.0.0.1:8768", None, "null"):
            self.assertIsNone(S.cors_origin(o))

    def test_ref_key(self):
        self.assertEqual(S.ref_key("Claude Documents"), "Claude-Documents")
        self.assertEqual(S.ref_key("working-repo (2)"), "working-repo-2")
        self.assertEqual(S.ref_key("x.lock"), "x-lock")

    def test_commits(self):
        out = "a" * 40 + "\x1fsubject\x1fq\x1f2026-10-24T05:00:00-05:00\x1e\n" + "b" * 40 + "\x1fs2\x1fq\x1f" \
              "2026-10-24T10:00:00+00:00\x1e\n"
        c = S.parse_commits(out, "o/r", on_github=lambda s: s.startswith("a"))
        self.assertEqual(c[0]["date"], "2026-10-24T10:00:00Z")
        self.assertEqual(c[0]["short"], "aaaaaaa")
        self.assertTrue(c[0]["url"].endswith("/commit/" + "a" * 40))
        self.assertIsNone(c[1]["url"])


class Diff(unittest.TestCase):
    def test_baseline_is_one_event(self):
        cur = snap(branches={"testing": "a"}, runs={str(i): {"status": "completed"} for i in range(500)},
                   pulls={"1": {"state": "open"}, "2": {"state": "closed"}}, local={"w": {"sha": "x"}})
        ev = S.diff_events(None, cur)
        self.assertEqual(types(ev), ["baseline"])
        self.assertIn("500 runs", ev[0]["text"])
        self.assertIn("1 branch,", ev[0]["text"])
        self.assertIn("1 local copy", ev[0]["text"])
        self.assertEqual(ev[0]["data"]["openPulls"], 1)

    def run_(self, **kw):
        r = {"status": "completed", "conclusion": "success", "attempt": 1, "workflow": "build-and-release",
             "branch": "testing", "event": "push", "updatedAt": AT1, "createdAt": AT1, "failedJobs": []}
        r.update(kw)
        return r

    def test_runs(self):
        prev = snap(at=AT0, runs={"1": self.run_(status="in_progress", conclusion=None),
                                  "2": self.run_(status="in_progress", conclusion=None),
                                  "3": self.run_(), "4": self.run_()})
        cur = snap(runs={"1": self.run_(),
                         "2": self.run_(conclusion="failure", failedJobs=["windows-tests"]),
                         "3": self.run_(),                                       # unchanged
                         "4": self.run_(status="queued", conclusion=None, attempt=2),  # re-run
                         "5": self.run_(status="in_progress", conclusion=None),   # new, running
                         "6": self.run_(conclusion="cancelled"),                  # new, already done
                         "7": self.run_(updatedAt="2026-09-01T00:00:00Z")})       # old, first seen: no event
        ev = S.diff_events(prev, cur)
        got = {(e["ref"]["run"], e["type"]) for e in ev}
        self.assertEqual(got, {(1, "run.completed"), (2, "run.failed"), (4, "run.started"), (5, "run.started"),
                               (6, "run.completed")})
        failed = [e for e in ev if e["type"] == "run.failed"][0]
        self.assertEqual(failed["text"], "build-and-release on testing failed (windows-tests)")
        self.assertEqual(failed["data"]["failedJobs"], ["windows-tests"])
        rerun = [e for e in ev if e["ref"]["run"] == 4][0]
        self.assertIn("re-run #2", rerun["text"])
        # the same diff again (a restart before state-prev.json was written) gives the same ids
        self.assertEqual([e["id"] for e in S.diff_events(prev, cur)], [e["id"] for e in ev])

    def test_pulls(self):
        prev = snap(at=AT0, pulls={
            "1": {"state": "open", "headSha": "a", "title": "one", "head": "h1", "base": "testing"},
            "2": {"state": "open", "headSha": "a", "title": "two", "head": "h2", "base": "testing"},
            "3": {"state": "open", "headSha": "a", "title": "three", "head": "h3", "base": "testing"},
            "4": {"state": "closed", "merged": False, "closedAt": AT0, "title": "four"},
            "5": {"state": "closed", "merged": True, "title": "five"}})
        cur = snap(pulls={
            "1": {"state": "closed", "merged": True, "mergedAt": AT1, "title": "one", "base": "testing"},
            "2": {"state": "closed", "merged": False, "closedAt": AT1, "title": "two"},
            "3": {"state": "open", "headSha": "b", "title": "three", "head": "h3", "base": "testing"},
            "4": {"state": "open", "headSha": "c", "title": "four"},
            "5": {"state": "closed", "merged": True, "title": "five"},
            "6": {"state": "open", "createdAt": AT1, "title": "six", "head": "h6", "base": "testing"},
            "7": {"state": "closed", "merged": True, "createdAt": AT0, "mergedAt": AT1, "title": "seven"},
            "8": {"state": "closed", "merged": True, "createdAt": "2026-01-01T00:00:00Z",
                  "mergedAt": "2026-01-02T00:00:00Z", "title": "old"}})
        ev = S.diff_events(prev, cur)
        got = [(e["ref"]["pr"], e["type"]) for e in ev]
        self.assertEqual(got, [(1, "pr.merged"), (2, "pr.closed"), (3, "pr.updated"), (4, "pr.opened"),
                               (6, "pr.opened"), (7, "pr.opened"), (7, "pr.merged")])
        self.assertIn("reopened", ev[3]["text"])

    def test_branches_tags_releases(self):
        prev = snap(at=AT0, branches={"testing": "a", "gone": "g", "forced": "f1", "same": "s"},
                    tags={"v1.0.0": "a"}, releases={"v1.0.0": {}})
        cur = snap(branches={"testing": "b", "forced": "f2", "same": "s", "claude/new": "n"},
                   tags={"v1.0.0": "a", "test-1.0.1-test.3": "b"},
                   releases={"v1.0.0": {}, "test-1.0.1-test.3": {"prerelease": True, "publishedAt": AT1}})

        def describe(kind, old, new):
            return {"forward": old != "f1", "count": 3} if old != "f1" else {"forward": False}
        ev = S.diff_events(prev, cur, describe)
        self.assertEqual(sorted(types(ev)), sorted(["branch.created", "branch.deleted", "branch.moved",
                                                    "branch.moved", "tag.created", "release.published"]))
        texts = {e["text"] for e in ev}
        self.assertIn("testing moved a -> b (+3 commits)", texts)
        self.assertIn("forced moved f1 -> f2 (force-pushed)", texts)
        self.assertIn("Release test-1.0.1-test.3 published (prerelease)", texts)

    def test_unknown_sections_are_skipped(self):
        prev = snap(at=AT0, branches={"testing": "a"}, tags={"v1": "a"})
        cur = snap(branches=None, tags=None, runs=None, pulls=None, releases=None)
        self.assertEqual(S.diff_events(prev, cur), [])
        merged = S.merge_snapshot(prev, cur)
        self.assertEqual(merged["branches"], {"testing": "a"})
        self.assertEqual(merged["tags"], {"v1": "a"})

    def test_local(self):
        prev = snap(at=AT0, local={"w": {"sha": "a", "branch": "testing", "dirty": 0},
                                   "p": {"sha": "a", "branch": "testing", "dirty": 3},
                                   "s": {"sha": "a", "branch": "testing", "dirty": 0},
                                   "e": {"sha": "a", "branch": "testing", "dirty": 0}})
        cur = snap(local={"w": {"sha": "b", "branch": "testing", "dirty": 2},
                          "p": {"sha": "c", "branch": "testing", "dirty": 0},
                          "s": {"sha": "a", "branch": "feature", "dirty": 0},
                          "e": {"sha": None, "branch": None, "dirty": None},   # git status failed: no events
                          "new": {"sha": "z", "branch": "x", "dirty": 1}})     # first seen: no events

        def describe(kind, old, new):
            if new == "b":
                return {"forward": True, "count": 1, "notOnTesting": 1, "subject": "fix the thing"}
            return {"forward": True, "count": 4, "notOnTesting": 0}  # a pull of testing
        ev = S.diff_events(prev, cur, describe)
        got = [(e["ref"]["local"], e["type"], e["text"]) for e in ev]
        self.assertEqual(got, [
            ("p", "local.moved", "p: testing moved a -> c (+4 commits)"),
            ("p", "local.dirty", "p: clean"),
            ("s", "local.moved", "s: switched to feature (a)"),
            ("w", "local.commit", "w: 1 new commit on testing: fix the thing"),
            ("w", "local.dirty", "w: 2 uncommitted changes")])
        merged = S.merge_snapshot(prev, cur)
        self.assertEqual(merged["local"]["e"]["sha"], "a")  # kept from the last good read

    def test_poll_error_once(self):
        prev = snap(at=AT0)
        e1 = S.diff_events(prev, snap(pollError="gh is not signed in"))
        self.assertEqual(types(e1), ["poll.error"])
        prev2 = snap(at=AT1, pollError="gh is not signed in")
        self.assertEqual(S.diff_events(prev2, snap(at="2026-10-24T10:02:00Z", pollError="gh is not signed in")), [])


class HistoryLog(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-hist-")
        self.path = os.path.join(self.dir, "history.jsonl")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_dedupe_and_restart(self):
        h = S.History(self.path)
        a = S.ev("x:1", AT0, "tag.created", {"tag": "v1"}, "t1")
        b = S.ev("x:2", AT1, "tag.created", {"tag": "v2"}, "t2")
        self.assertEqual(len(h.append([a, b])), 2)
        self.assertEqual(len(h.append([a])), 0)
        with open(self.path, "a", encoding="utf-8") as f:
            f.write('{"id": "cut-by-a-cra')  # a crash mid-line
        h2 = S.History(self.path)
        self.assertEqual(len(h2.events), 2)
        self.assertEqual(len(h2.append([a, b, S.ev("x:3", AT1, "tag.created", {}, "t3")])), 1)
        h3 = S.History(self.path)
        self.assertEqual([e["id"] for e in h3.query()], ["x:3", "x:2", "x:1"])
        self.assertEqual([e["id"] for e in h3.query(since=AT1)], ["x:3", "x:2"])
        self.assertEqual(len(h3.query(limit=1)), 1)


class NotesStore(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-notes-")
        self.folder = os.path.join(self.dir, "editor")
        self.events = []
        self.clock = iter("2026-10-24T10:%02d:00Z" % i for i in range(60))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def store(self):
        return S.Notes(self.folder, self.events.extend, "o/r", now=lambda: next(self.clock))

    def test_create_edit_done_delete_and_history(self):
        st = self.store()
        n = st.create({"target": {"kind": "pr", "key": "#8"}, "text": "check the **netcode** test\n- one\n- two"})
        self.assertRegex(n["id"], r"^n[0-9a-f]{12}$")
        self.assertEqual((n["target"], n["status"], n["pinned"], n["author"], n["rev"]),
                         ({"kind": "pr", "key": "8"}, "open", False, "q", 1))
        self.assertEqual(n["createdAt"], n["updatedAt"])
        g = st.create({"target": {"kind": "general"}, "text": "general", "pinned": True, "author": "Claude"})
        self.assertEqual((g["target"], g["author"]), ({"kind": "general", "key": ""}, "Claude"))
        e = st.update(n["id"], {"text": "new text", "pinned": True, "rev": 1})
        self.assertEqual((e["text"], e["pinned"], e["rev"]), ("new text", True, 2))
        self.assertNotEqual(e["updatedAt"], e["createdAt"])
        self.assertEqual(st.update(n["id"], {"pinned": True}), e)  # no change: no write, no event
        with self.assertRaises(S.NoteError) as c:
            st.update(n["id"], {"text": "stale", "rev": 1})  # a second tab's old copy
        self.assertEqual(c.exception.code, 409)
        d = st.update(n["id"], {"status": "done"})
        st.update(n["id"], {"status": "open", "target": {"kind": "branch", "key": "origin/testing"}})
        self.assertEqual(st.all()[0]["target"], {"kind": "branch", "key": "testing"})
        self.assertEqual(st.delete(n["id"]), n["id"])
        with self.assertRaises(S.NoteError) as c:
            st.delete(n["id"])
        self.assertEqual(c.exception.code, 404)
        self.assertEqual(types(self.events), ["note.added", "note.added", "note.edited", "note.done", "note.edited",
                                              "note.deleted"])
        self.assertEqual(len({e["id"] for e in self.events}), 6)
        added, _, edited, done, moved, deleted = self.events
        self.assertEqual(added["ref"], {"note": n["id"], "pr": 8})
        self.assertEqual(added["text"], "Note on PR #8: check the **netcode** test - one - two")
        self.assertEqual(edited["text"], "Note edited, pinned on PR #8: new text")
        self.assertEqual((edited["data"]["changed"], edited["data"]["text"]), (["text", "pinned"], "new text"))
        self.assertEqual(done["id"], "note.done:%s:3" % n["id"])
        self.assertEqual(moved["ref"], {"note": n["id"], "branch": "testing"})
        self.assertEqual(moved["data"]["from"], {"kind": "pr", "key": "8"})
        self.assertIn("reopened", moved["text"])
        # the tombstone: the whole note is in the log
        self.assertEqual(deleted["data"]["note"]["text"], "new text")
        self.assertEqual(deleted["text"], "Note deleted on branch testing: new text")
        self.assertEqual(d["status"], "done")
        self.assertEqual([x["id"] for x in st.all()], [g["id"]])

    def test_files_atomic_bak_markdown_and_reload(self):
        st = self.store()
        a = st.create({"target": {"kind": "branch", "key": "claude/x"}, "text": "first\n\nsecond paragraph"})
        path = os.path.join(self.folder, "notes.json")
        self.assertFalse(os.path.exists(path + ".bak"))
        st.create({"target": {"kind": "run", "key": 123}, "text": "flaky", "pinned": True})
        before = S.read_json(path + ".bak")
        self.assertEqual([x["id"] for x in before["notes"]], [a["id"]])  # .bak is the previous version
        self.assertEqual(len(S.read_json(path)["notes"]), 2)
        self.assertEqual([f for f in os.listdir(self.folder) if ".tmp" in f], [])
        md = open_text(os.path.join(self.folder, "notes.md"))
        self.assertIn("## Branch `claude/x`", md)
        self.assertIn("<https://github.com/o/r/tree/claude/x>", md)
        self.assertIn("## CI run 123", md)
        self.assertIn("> first\n>\n> second paragraph", md)
        self.assertIn("### open, pinned · by q", md)
        self.assertLess(md.index("## Branch"), md.index("## CI run"))
        # a restart reads them back; the same body gives the same ETag
        st2 = self.store()
        self.assertEqual(st2.all(), st.all())
        self.assertEqual(st2.view()[0], st.view()[0])
        tag = st2.view()[0]
        st2.update(a["id"], {"author": "me"})
        self.assertNotEqual(st2.view()[0], tag)
        # a cut notes.json: kept aside, the .bak is loaded, and the next save does not copy the broken file over it
        with open(path, "w") as f:
            f.write('{"notes": [')
        st3 = self.store()
        self.assertIn("loaded notes.json.bak", st3.load_error)
        self.assertEqual(len(st3.all()), 2)
        self.assertTrue([f for f in os.listdir(self.folder) if f.startswith("notes.json.bad-")])
        self.assertIn("loadError", json.loads(st3.view()[1]))

    def test_save_failure_undoes_the_change(self):
        st = self.store()
        a = st.create({"target": {"kind": "general"}, "text": "keep"})
        real = S.write_atomic
        S.write_atomic = lambda path, data: (_ for _ in ()).throw(OSError("disk full"))
        try:
            for call in (lambda: st.create({"target": {"kind": "general"}, "text": "x"}),
                         lambda: st.update(a["id"], {"text": "changed"}), lambda: st.delete(a["id"])):
                with self.assertRaises(S.NoteError) as c:
                    call()
                self.assertEqual(c.exception.code, 500)
        finally:
            S.write_atomic = real
        self.assertEqual([(x["id"], x["text"]) for x in st.all()], [(a["id"], "keep")])
        self.assertEqual(types(self.events), ["note.added"])

    def test_validation(self):
        st = self.store()
        ok = {"target": {"kind": "general"}, "text": "t"}
        bad = [
            {"text": "no target"}, {"target": {"kind": "general"}}, dict(ok, id="n000000000000"),
            dict(ok, createdAt=AT0), dict(ok, colour="red"), dict(ok, text=""), dict(ok, text="   "), dict(ok, text=5),
            dict(ok, text="x" * (20 * 1024 + 1)), dict(ok, text="é" * 10241), dict(ok, pinned="yes"),
            dict(ok, status="closed"), dict(ok, author="a" * 81), dict(ok, author="a\nb"),
            dict(ok, target={"kind": "issue", "key": "1"}), dict(ok, target={"kind": "general", "key": "x"}),
            dict(ok, target={"kind": "pr", "key": "eight"}), dict(ok, target={"kind": "pr", "key": True}),
            dict(ok, target={"kind": "commit", "key": "xyz"}), dict(ok, target={"kind": "version", "key": "1.2"}),
            dict(ok, target={"kind": "branch", "key": "a b"}), dict(ok, target={"kind": "branch", "key": ""}),
            dict(ok, target={"kind": "local", "key": "x" * 201}), dict(ok, target={"kind": "run"}),
            dict(ok, target={"kind": "run", "key": "1", "extra": 1}), dict(ok, target="general"), [ok], None, "x",
        ]
        for b in bad:
            with self.assertRaises(S.NoteError, msg=repr(b)[:80]) as c:
                st.create(b)
            self.assertEqual(c.exception.code, 400)
        self.assertEqual(st.create(dict(ok, text="x" * (20 * 1024)))["text"], "x" * 20480)
        self.assertEqual(st.create(dict(ok, target={"kind": "version", "key": "v1.18.6"}))["target"]["key"], "1.18.6")
        self.assertEqual(st.create(dict(ok, target={"kind": "commit", "key": "ABC1234"}))["target"]["key"], "abc1234")
        self.assertEqual(st.create(dict(ok, text="a\r\nb"))["text"], "a\nb")
        n = st.all()[0]
        for b in ({}, {"rev": 1}, {"id": "x"}, {"text": ""}, {"createdAt": AT0}):
            with self.assertRaises(S.NoteError):
                st.update(n["id"], b)
        self.assertEqual(len(self.events), 4)

    def test_limit(self):
        st = self.store()
        old = S.NOTE_MAX
        S.NOTE_MAX = 3
        try:
            for i in range(3):
                st.create({"target": {"kind": "general"}, "text": str(i)})
            with self.assertRaises(S.NoteError) as c:
                st.create({"target": {"kind": "general"}, "text": "one too many"})
            self.assertEqual(c.exception.code, 409)
        finally:
            S.NOTE_MAX = old

    def test_write_origin(self):
        ok = lambda h: S.write_origin_ok(h, 8767)
        for o in ("http://127.0.0.1:8767", "http://localhost:8767", "http://127.0.0.1:8768", "http://localhost:8768"):
            self.assertTrue(ok({"Origin": o}), o)
        for o in ("http://127.0.0.1:8765", "http://127.0.0.1:8775", "http://evil.example", "null", "",
                  "https://127.0.0.1:8767", "http://127.0.0.1:8767.evil.example"):
            self.assertFalse(ok({"Origin": o}), o)
        self.assertTrue(ok({"Referer": "http://127.0.0.1:8767/#tab=editor"}))
        self.assertFalse(ok({"Referer": "http://127.0.0.1:8767@evil.example/"}))
        self.assertFalse(ok({"Referer": "http://evil.example/http://127.0.0.1:8767/"}))
        self.assertFalse(ok({}))
        self.assertFalse(ok({"Origin": "http://evil.example", "Referer": "http://127.0.0.1:8767/"}))
        self.assertTrue(S.write_origin_ok({"Origin": "http://127.0.0.1:9000"}, 9000))
        self.assertTrue(S.json_content_type("application/json; charset=utf-8"))
        for ct in ("text/plain", "application/x-www-form-urlencoded", "multipart/form-data", None):
            self.assertFalse(S.json_content_type(ct))


class NotesHttp(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-notes-http-")
        cfg = {"repo": "o/r", "port": 0, "quarkDir": self.dir}
        self.m = S.Manager(cfg, self.dir, os.path.dirname(os.path.abspath(S.__file__)), quiet=True)
        self.srv = S.make_server(self.m, 0)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.own = "http://127.0.0.1:%d" % self.port

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def call(self, method, path, body=None, headers=None, raw=None):
        h = {"Origin": self.own, "Content-Type": "application/json"}
        h.update(headers or {})
        h = {k: v for k, v in h.items() if v is not None}
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        req = urllib.request.Request("http://127.0.0.1:%d%s" % (self.port, path), data=data, headers=h,
                                     method=method)
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def test_crud_etag_history_and_safety(self):
        code, h, body = self.call("GET", "/api/notes", headers={"Content-Type": None})
        self.assertEqual((code, json.loads(body)["notes"]), (200, []))
        etag = h["ETag"]
        self.assertEqual(self.call("GET", "/api/notes", headers={"If-None-Match": etag})[0], 304)
        code, h, body = self.call("POST", "/api/notes", {"target": {"kind": "run", "key": "42"}, "text": "hi"})
        self.assertEqual(code, 201, body)
        nid = json.loads(body)["note"]["id"]
        self.assertEqual(self.call("GET", "/api/notes", headers={"If-None-Match": etag})[0], 200)
        code, h, body = self.call("PUT", "/api/notes/" + nid, {"status": "done", "rev": 1})
        self.assertEqual((code, json.loads(body)["note"]["status"]), (200, "done"))
        self.assertEqual(self.call("PUT", "/api/notes/" + nid, {"text": "x", "rev": 1})[0], 409)
        # refused: another origin, no origin at all, a form post, unknown fields, too big, bad JSON, bad ids
        evil = {"Origin": "http://evil.example"}
        self.assertEqual(self.call("POST", "/api/notes", {"target": {"kind": "general"}, "text": "x"}, evil)[0], 403)
        self.assertEqual(self.call("DELETE", "/api/notes/" + nid, headers=evil)[0], 403)
        self.assertEqual(self.call("POST", "/api/notes", {"target": {"kind": "general"}, "text": "x"},
                                   {"Origin": None})[0], 403)
        self.assertEqual(self.call("POST", "/api/notes", {"target": {"kind": "general"}, "text": "x"},
                                   {"Origin": None, "Referer": self.own + "/#tab=editor"})[0], 201)
        self.assertEqual(self.call("POST", "/api/notes", {"target": {"kind": "general"}, "text": "x"},
                                   {"Origin": "http://localhost:8768"})[0], 201)
        self.assertEqual(self.call("POST", "/api/notes", raw=b"text=x",
                                   headers={"Content-Type": "application/x-www-form-urlencoded"})[0], 415)
        self.assertEqual(self.call("POST", "/api/notes", {"target": {"kind": "general"}, "text": "x", "x": 1})[0], 400)
        self.assertEqual(self.call("POST", "/api/notes", raw=b"{not json")[0], 400)
        self.assertEqual(self.call("POST", "/api/notes", raw=b"x" * (64 * 1024 + 1))[0], 413)
        self.assertEqual(self.call("PUT", "/api/notes/../../etc", {"text": "x"})[0], 404)
        self.assertEqual(self.call("PUT", "/api/notes/n000000000000", {"text": "x"})[0], 404)
        self.assertEqual(self.call("PUT", "/api/notes", {"text": "x"})[0], 404)
        self.assertEqual(self.call("PUT", "/api/state", {"text": "x"})[0], 404)
        self.assertEqual(self.call("DELETE", "/api/notes/" + nid, {"x": 1})[0], 400)
        code, h, body = self.call("DELETE", "/api/notes/" + nid)
        self.assertEqual((code, json.loads(body)), (200, {"deleted": nid}))
        code, h, body = self.call("GET", "/api/notes")
        self.assertEqual(len(json.loads(body)["notes"]), 2)
        hist = json.loads(self.call("GET", "/api/history")[2])
        self.assertEqual(sorted(types(hist)), ["note.added", "note.added", "note.added", "note.deleted", "note.done"])
        self.assertEqual(S.History(os.path.join(self.dir, "history.jsonl")).query()[0]["type"], "note.deleted")
        # the data folder is never served as a static file, even when the page runs from the output folder
        self.m.static = self.dir
        self.assertEqual(self.call("GET", "/editor/notes.md")[0], 404)
        code, h, _ = self.call("OPTIONS", "/api/notes/" + nid, headers={"Origin": "http://127.0.0.1:8768",
                                                                        "Access-Control-Request-Method": "PUT"})
        self.assertEqual(code, 204)
        self.assertIn("PUT", h["Access-Control-Allow-Methods"])


ESC_ERR = "the response contains terminal escape sequences; pass --allow-escape-sequences to output it anyway\n"
OLD_ERR = "unknown flag: --allow-escape-sequences\n\nUsage:  gh api <endpoint> [flags]\n"


class RawProc:
    """`gh api` for raw (log) calls, as a given gh version answers."""

    def __init__(self, knows_flag, fail=None):
        self.knows_flag, self.fail, self.args = knows_flag, fail, []

    def run(self, args, cwd=None, timeout=90, binary=False):
        self.args.append(args)
        flag = "--allow-escape-sequences" in args
        if self.fail:
            return 1, b"", self.fail
        if flag and not self.knows_flag:
            return 1, b"", OLD_ERR
        if not flag and self.knows_flag:
            return 1, b"", ESC_ERR
        return 0, b"\x1b[36;1mlog\x1b[0m\n", ""


class RawLogs(unittest.TestCase):
    def test_new_gh_needs_the_flag(self):
        p = RawProc(knows_flag=True)
        g = S.GitHub("o/r", p)
        self.assertEqual(g.raw("repos/o/r/actions/jobs/1/logs"), b"\x1b[36;1mlog\x1b[0m\n")
        self.assertIn("--allow-escape-sequences", p.args[0])
        self.assertTrue(g.escape_flag)

    def test_old_gh_drops_the_flag_once(self):
        p = RawProc(knows_flag=False)
        g = S.GitHub("o/r", p)
        self.assertTrue(g.raw("a").endswith(b"\n"))
        self.assertEqual(len(p.args), 2)  # tried with the flag, then without
        g.raw("b")
        self.assertEqual(len(p.args), 3)
        self.assertNotIn("--allow-escape-sequences", p.args[2])
        self.assertIs(g.escape_flag, False)

    def test_error_keeps_the_first_stderr_line(self):
        g = S.GitHub("o/r", RawProc(True, fail="  " + ESC_ERR + "more\n"))
        with self.assertRaises(S.GhError) as cm:
            g.raw("repos/o/r/actions/jobs/1/logs")
        self.assertIn("the response contains terminal escape sequences", str(cm.exception))
        self.assertIsNone(cm.exception.status)
        g = S.GitHub("o/r", RawProc(True, fail='Get "https://x.blob.core.windows.net/a/b.txt?sig=SECRET&se=1": '
                                                 'Forbidden\n'))
        with self.assertRaises(S.GhError) as cm:
            g.raw("y")
        self.assertNotIn("SECRET", str(cm.exception))
        self.assertIn("https://x.blob.core.windows.net/a/b.txt?...", str(cm.exception))
        g = S.GitHub("o/r", RawProc(True, fail="gh: Not Found (HTTP 404)\n"))
        with self.assertRaises(S.GhError) as cm:
            g.raw("x")
        self.assertEqual(cm.exception.status, 404)


class FakeGit:
    """Answers `git -C <dir> config --get remote.origin.url` and `worktree list` from a table."""

    def __init__(self, origins, worktrees):
        self.origins, self.worktrees = origins, worktrees

    def run(self, args, cwd=None, timeout=90):
        d = os.path.normpath(args[1])
        if args[2:] == ["config", "--get", "remote.origin.url"]:
            return (0, self.origins[d] + "\n", "") if d in self.origins else (1, "", "")
        if args[2:4] == ["worktree", "list"]:
            for main, wts in self.worktrees.items():
                if d == main or d in wts:
                    lines = ["worktree %s\nHEAD %s\nbranch refs/heads/b\n" % (p, "a" * 40) for p in [main] + wts]
                    return 0, "\n".join(lines) + "\n", ""
            return 0, "worktree %s\nHEAD %s\nbranch refs/heads/b\n\n" % (d, "a" * 40), ""
        return 1, "", "unexpected"


class Discovery(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="ghm-disc-")
        self.q = os.path.join(self.root, "Quark")
        self.elsewhere = os.path.join(self.root, "elsewhere wt")
        for n, kind in (("working-repo", "dir"), ("working-repo-server", "file"), ("repository", "dir"),
                        ("Claude Documents", "dir"), ("other-repo", "dir"), ("not-a-repo", None)):
            p = os.path.join(self.q, n)
            os.makedirs(p)
            if kind == "dir":
                os.makedirs(os.path.join(p, ".git"))
            elif kind == "file":
                write(os.path.join(p, ".git"), "gitdir: x\n")
        os.makedirs(self.elsewhere)
        j = lambda n: os.path.normpath(os.path.join(self.q, n))
        self.git = FakeGit(
            {j("working-repo"): "https://github.com/floration/quark", j("working-repo-server"):
             "https://github.com/floration/quark", j("repository"): "git@github.com:floration/quark.git",
             j("Claude Documents"): "https://github.com/floration/quark", j("other-repo"): "https://github.com/x/y"},
            {j("working-repo"): [j("working-repo-server"), os.path.normpath(self.elsewhere)]})

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_auto(self):
        e = S.discover_locals({"quarkDir": self.q}, self.git, "floration/quark")
        names = [x["name"] for x in e]
        self.assertEqual(names, ["repository", "working-repo", "working-repo-server", "elsewhere wt"])
        by = {x["name"]: x for x in e}
        self.assertIsNone(by["working-repo"]["worktreeOf"])
        self.assertEqual(by["working-repo-server"]["worktreeOf"], "working-repo")
        self.assertEqual(by["elsewhere wt"]["worktreeOf"], "working-repo")
        self.assertEqual(by["elsewhere wt"]["key"], "elsewhere-wt")
        self.assertEqual(len({x["key"] for x in e}), len(e))

    def test_explicit(self):
        e = S.discover_locals({"quarkDir": self.q, "locals": [os.path.join(self.q, "working-repo"),
                                                               {"name": "srv", "path": os.path.join(
                                                                   self.q, "working-repo-server")}]},
                              self.git, "floration/quark")
        self.assertEqual([(x["name"], x["worktreeOf"]) for x in e], [("working-repo", None), ("srv", "working-repo")])


# ----------------------------------------------------------------------------------------------------------------
# running windowless: logging, crashes, a second copy

HERE = os.path.dirname(os.path.abspath(__file__))


def free_port():
    import socket
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Windowless(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-run-")
        self.env = dict(os.environ, PATH=os.path.join(self.dir, "no-tools"))  # no gh, no git: polls fail fast
        self.env.pop("QUARK_SHELL", None)
        self.procs = []

    def tearDown(self):
        for p in self.procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        shutil.rmtree(self.dir, ignore_errors=True)

    def log(self, data):
        try:
            with open(os.path.join(data, "server.log"), encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    def test_no_stdout_like_pythonw(self):
        data = os.path.join(self.dir, "data")
        code = ("import sys; sys.stdout = None; sys.stderr = None; sys.path.insert(0, %r); import server; "
                "sys.exit(server.main(['--data', %r, '--once']))" % (HERE, data))
        p = subprocess.run([sys.executable, "-B", "-c", code], env=self.env, capture_output=True, timeout=60)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("poll with errors", self.log(data))

    def test_second_copy_exits_2_and_signals_are_logged(self):
        data, data2 = os.path.join(self.dir, "data"), os.path.join(self.dir, "data2")
        port = free_port()
        first = subprocess.Popen([sys.executable, "-B", os.path.join(HERE, "server.py"), "--data", data, "--port",
                                  str(port), "--quiet"], env=self.env, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
        self.procs.append(first)
        for _ in range(200):
            if S.already_running(port, timeout=0.5):
                break
            threading.Event().wait(0.05)
        self.assertTrue(S.already_running(port))
        t0 = S.time.time()
        p = subprocess.run([sys.executable, "-B", os.path.join(HERE, "server.py"), "--data", data2, "--port",
                            str(port), "--quiet"], env=self.env, capture_output=True, timeout=30)
        self.assertEqual(p.returncode, S.EXIT_PORT_BUSY)
        self.assertLess(S.time.time() - t0, 10)
        self.assertIn("port %d is already in use by a running GitHub manager" % port, self.log(data2))
        if os.name != "nt":
            import signal
            first.send_signal(signal.SIGTERM)
            first.wait(timeout=20)
            self.assertIn("stopped (signal SIGTERM)", self.log(data))
        self.assertIn("starting (pid %d" % first.pid, self.log(data))

    def test_busy_port_by_another_program(self):
        import socket
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        s.listen(1)
        port = s.getsockname()[1]
        try:
            data = os.path.join(self.dir, "data")
            p = subprocess.run([sys.executable, "-B", os.path.join(HERE, "server.py"), "--data", data, "--port",
                                str(port)], env=self.env, capture_output=True, text=True, timeout=30)
        finally:
            s.close()
        self.assertEqual(p.returncode, 2)
        self.assertIn("already in use by another program", self.log(data))
        self.assertIn("already in use", p.stdout)  # and on the console when there is one

    def test_quark_shell_skips_the_browser(self):
        a = type("A", (), {"open": True})()
        old = os.environ.pop("QUARK_SHELL", None)
        try:
            self.assertTrue(S.wants_browser(a))
            os.environ["QUARK_SHELL"] = "1"
            self.assertFalse(S.wants_browser(a))
            a.open = False
            os.environ.pop("QUARK_SHELL")
            self.assertFalse(S.wants_browser(a))
        finally:
            if old is not None:
                os.environ["QUARK_SHELL"] = old

    def test_poll_thread_logs_crashes_and_keeps_going(self):
        m = S.Manager.__new__(S.Manager)
        m.stop, m.wake, m.interval, m.next_at, m.last_error = threading.Event(), threading.Event(), 0, None, None
        calls = []

        def poll():
            calls.append(1)
            if len(calls) == 1:
                raise KeyError("boom")
            m.stop.set()
        m.poll = poll
        records = []
        h = logging.Handler()
        h.emit = records.append
        S.LOG.addHandler(h)
        try:
            t = threading.Thread(target=m.loop)
            m.wake.set()
            t.start()
            t.join(10)
        finally:
            S.LOG.removeHandler(h)
        self.assertFalse(t.is_alive())
        self.assertEqual(len(calls), 2)
        self.assertTrue(any("poll crashed" in r.getMessage() and r.exc_info for r in records))
        self.assertIn("boom", m.last_error)

    def test_install_copies_without_data(self):
        out = os.path.join(self.dir, "out dir")
        os.makedirs(out)
        write(os.path.join(out, "history.jsonl"), "keep\n")
        sys.path.insert(0, HERE)
        try:
            import install
        finally:
            sys.path.remove(HERE)
        os.makedirs(os.path.join(out, "editor"))
        write(os.path.join(out, "editor", "notes.json"), "{}\n")
        # --help and bad arguments install nothing
        for args, code in ((["--help"], 0), (["--bogus"], 2), ([out, "--dest", out + "x"], 2)):
            with self.assertRaises(SystemExit) as c:
                install.main(args)
            self.assertEqual(c.exception.code, code)
        self.assertFalse(os.path.exists(os.path.join(out, "server.py")))
        self.assertEqual(install.main(["--dest", out, "--no-start"]), 0)
        for n in ("server.py", "github-manager.cmd", "README.md", "API.md", os.path.join("js", "notes.js")):
            self.assertTrue(os.path.isfile(os.path.join(out, n)), n)
        for n in ("test_server.py", "install.py", "test"):
            self.assertFalse(os.path.exists(os.path.join(out, n)), n)
        with open(os.path.join(out, "history.jsonl"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "keep\n")
        self.assertEqual(open_text(os.path.join(out, "editor", "notes.json")), "{}\n")
        self.assertIn("installed", open_text(os.path.join(out, "install.log")))
        # again: nothing to copy; a stale page file is removed; the old positional folder still works
        write(os.path.join(out, "js", "old.js"), "x")
        self.assertEqual(install.main([out, "--no-start"]), 0)
        self.assertFalse(os.path.exists(os.path.join(out, "js", "old.js")))
        self.assertEqual(install.main([out, "--no-start"]), 0)
        log = [x for x in open_text(os.path.join(out, "install.log")).splitlines() if x.startswith("installed")]
        self.assertIn("(1 changed)", log[-2])
        self.assertIn("already up to date", log[-1])


def open_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


# ----------------------------------------------------------------------------------------------------------------
# offline end-to-end: real git (a local origin), canned gh

GIT_ID = ["-c", "user.name=T", "-c", "user.email=t@example.invalid", "-c", "commit.gpgsign=false",
          "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main"]


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def git(*args, cwd=None):
    p = subprocess.run(["git"] + GIT_ID + list(args), cwd=cwd, capture_output=True, text=True)
    if p.returncode:
        raise AssertionError("git %s: %s" % (args, p.stderr))
    return p.stdout.strip()


class FakeGh(S.Proc):
    """git runs for real; `gh api` answers from self.routes (path without query -> object, or callable)."""

    def __init__(self):
        S.Proc.__init__(self)
        self.routes = {}
        self.calls = []
        self.etag_seq = 0
        self.raw_handler = None

    def run(self, args, cwd=None, timeout=90, binary=False, input=None):
        if args[0] != "gh":
            return S.Proc.run(self, args, cwd=cwd, timeout=timeout, binary=binary, input=input)
        path = args[-1]
        self.calls.append(path)
        key = path.split("?")[0]
        if "status=" in path:
            key += "?status=" + path.split("status=")[1].split("&")[0]
        elif "state=" in path:
            key += "?state=" + path.split("state=")[1].split("&")[0]
        val = self.routes.get(key)
        if callable(val):
            val = val(path)
        if binary:
            if self.raw_handler:
                return self.raw_handler(args)
            return (0, val, "") if isinstance(val, bytes) else (1, b"", "gh: Not Found (HTTP 404)")
        if val is None:
            return 1, 'HTTP/1.1 404 Not Found\r\n\r\n{"message":"Not Found"}', "gh: Not Found (HTTP 404)"
        body = json.dumps(val)
        etag = '"%d"' % (hash(body) & 0xffffffff)
        inm = [args[i + 1] for i, a in enumerate(args[:-1]) if a == "-H" and args[i + 1].startswith("If-None-Match")]
        if inm and inm[0].split(": ", 1)[1] == etag:
            return 1, "HTTP/1.1 304 Not Modified\r\nEtag: %s\r\nX-Ratelimit-Remaining: 4000\r\n\r\n" % etag, \
                "gh: HTTP 304"
        return 0, "HTTP/1.1 200 OK\r\nEtag: %s\r\nX-Ratelimit-Remaining: 4000\r\n\r\n%s" % (etag, body), ""


def gh_run(rid, status, conclusion, sha, branch="testing", attempt=1, name="build-and-release"):
    return {"id": rid, "name": name, "head_branch": branch, "head_sha": sha, "event": "push",
            "display_title": "run %d" % rid, "status": status, "conclusion": conclusion,
            "created_at": "2026-10-24T09:00:00Z", "run_started_at": "2026-10-24T09:00:05Z",
            "updated_at": S.iso(S.now_dt()), "run_attempt": attempt, "run_number": rid,
            "html_url": "https://github.com/o/r/actions/runs/%d" % rid, "pull_requests": [],
            "path": ".github/workflows/release.yml"}


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="ghm-e2e-")
        r = self.root
        self.origin = os.path.join(r, "origin")
        os.makedirs(self.origin)
        git("init", "-q", cwd=self.origin)
        write(os.path.join(self.origin, "VERSION"), "1.0.0\n")
        git("add", "VERSION", cwd=self.origin)
        git("commit", "-qm", "first", cwd=self.origin)
        git("tag", "-a", "v1.0.0", "-m", "v1.0.0", cwd=self.origin)
        git("checkout", "-qb", "testing", cwd=self.origin)
        write(os.path.join(self.origin, "VERSION"), "1.0.1\n")
        git("commit", "-qam", "1.0.1", cwd=self.origin)
        git("checkout", "-q", "main", cwd=self.origin)
        self.testing1 = git("rev-parse", "testing", cwd=self.origin)
        # the owner's folder: a clone with a space in its name and a worktree
        self.quark = os.path.join(r, "Quark dir")
        os.makedirs(self.quark)
        self.wr = os.path.join(self.quark, "working repo")
        git("clone", "-q", "-b", "testing", self.origin, self.wr)
        git("remote", "set-url", "origin", "https://github.com/o/r.git", cwd=self.wr)
        git("worktree", "add", "-q", "-b", "side", os.path.join(self.quark, "working-repo-server"), "HEAD",
            cwd=self.wr)
        os.makedirs(os.path.join(self.quark, "Claude Documents", ".git"))
        self.data = os.path.join(r, "data dir")
        os.makedirs(self.data)
        self.cfg = {"repo": "o/r", "port": 0, "quarkDir": self.quark, "originUrl": self.origin}
        self.gh = FakeGh()
        rt = self.gh.routes
        rt["repos/o/r"] = {"default_branch": "main"}
        rt["rate_limit"] = {"resources": {"core": {"remaining": 4999, "limit": 5000, "reset": 0}}}
        rt["repos/o/r/releases"] = [
            {"tag_name": "v1.0.0", "prerelease": False, "draft": False, "published_at": "2026-10-20T00:00:00Z",
             "html_url": "https://github.com/o/r/releases/tag/v1.0.0"},
            {"tag_name": "test-1.0.1-test.7", "prerelease": True, "draft": False,
             "published_at": "2026-10-23T00:00:00Z", "html_url": "https://github.com/o/r/releases/tag/t"}]
        pr = {"number": 4, "title": "feature", "state": "open", "draft": False, "merged_at": None,
              "closed_at": None, "head": {"ref": "testing", "sha": self.testing1, "repo": {"full_name": "o/r"}},
              "base": {"ref": "main"}, "user": {"login": "q"}, "created_at": "2026-10-24T08:00:00Z",
              "updated_at": "2026-10-24T08:00:00Z", "html_url": "https://github.com/o/r/pull/4"}
        rt["repos/o/r/pulls?state=open"] = [pr]
        rt["repos/o/r/pulls?state=closed"] = []
        rt["repos/o/r/pulls/4"] = {"updated_at": pr["updated_at"], "additions": 5, "deletions": 1,
                                   "changed_files": 2, "merged": False}
        self.runs = [gh_run(101, "completed", "failure", self.testing1), gh_run(102, "in_progress", None,
                                                                                self.testing1)]
        rt["repos/o/r/actions/runs"] = lambda p: {"workflow_runs": self.runs}
        rt["repos/o/r/actions/runs?status=in_progress"] = lambda p: {
            "workflow_runs": [x for x in self.runs if x["status"] == "in_progress"]}
        rt["repos/o/r/actions/runs?status=queued"] = {"workflow_runs": []}
        rt["repos/o/r/actions/runs?status=waiting"] = {"workflow_runs": []}
        rt["repos/o/r/actions/runs/102"] = lambda p: self.runs[1]
        failed_job = {"id": 9001, "name": "windows-tests", "status": "completed", "conclusion": "failure",
                      "html_url": "j", "steps": [{"name": "quark_tests", "conclusion": "failure"}]}
        rt["repos/o/r/actions/runs/101/jobs"] = {"jobs": [failed_job]}
        rt["repos/o/r/actions/runs/102/jobs"] = lambda p: {"jobs": [
            dict(failed_job, id=9002, name="linux-tests", status=self.runs[1]["status"],
                 conclusion=self.runs[1]["conclusion"], steps=[])]}
        rt["repos/o/r/actions/jobs/9001/logs"] = b"the windows log\n"

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_log_errors_are_shown_and_retried_on_restart(self):
        gh_err = "gh: some systematic refusal\nsecond line\n"
        self.gh.raw_handler = lambda args: (1, b"", gh_err)
        m = self.manager()
        st = m.poll(manual=True)
        self.assertIsNone(st["poll"]["lastError"])
        self.assertTrue(any("gh: some systematic refusal" in w and "second line" not in w
                            for w in st["poll"]["warnings"]), st["poll"]["warnings"])
        self.assertFalse(os.path.exists(os.path.join(self.data, "logs", "101", "9001.txt")))
        self.assertFalse(m.runs[101]["jobs"][0].get("logGone"))  # not a 404: not given up on
        m.poll()  # within the 30 min backoff: not asked again in this process
        self.assertEqual(sum(1 for c in self.gh.calls if c.endswith("/9001/logs")), 1)
        # the fix (a newer server / gh) takes effect on restart: no backoff is persisted
        self.gh.raw_handler = None
        m2 = self.manager()
        st = m2.poll()
        self.assertEqual(st["poll"]["warnings"], [])
        with open(os.path.join(self.data, "logs", "101", "9001.txt"), "rb") as f:
            self.assertEqual(f.read(), b"the windows log\n")

    def manager(self):
        return S.Manager(dict(self.cfg), self.data, os.path.dirname(os.path.abspath(S.__file__)), proc=self.gh,
                         quiet=True)

    def test_poll_restart_and_http(self):
        m = self.manager()
        st = m.poll(manual=True)
        self.assertIsNone(st["poll"]["lastError"], st["poll"])
        self.assertEqual(st["production"]["tag"], "v1.0.0")
        self.assertEqual(st["production"]["sha"], git("rev-parse", "v1.0.0^{commit}", cwd=self.origin))
        self.assertEqual(st["testing"]["version"], "1.0.1")
        self.assertEqual(st["testing"]["aheadOfProduction"], 1)
        self.assertEqual(st["testing"]["prerelease"]["tag"], "test-1.0.1-test.7")
        kinds = {b["name"]: (b["kind"], b["ahead"], b["behind"], b["pr"]) for b in st["branches"]}
        self.assertEqual(kinds["main"], ("production", 0, 1, None))
        self.assertEqual(kinds["testing"], ("testing", 0, 0, 4))
        self.assertEqual([r["id"] for r in st["running"]], [102])
        run101 = [r for r in st["runs"] if r["id"] == 101][0]
        self.assertEqual(run101["jobs"][0]["failedSteps"], ["quark_tests"])
        self.assertTrue(run101["jobs"][0]["hasLog"])
        with open(os.path.join(self.data, "logs", "101", "9001.txt"), "rb") as f:
            self.assertEqual(f.read(), b"the windows log\n")
        p4 = st["pulls"][0]
        self.assertEqual((p4["checks"]["state"], p4["additions"], p4["changedFiles"]), ("failure", 5, 2))
        loc = {l["name"]: l for l in st["local"]}
        self.assertEqual(sorted(loc), ["working repo", "working-repo-server"])
        self.assertEqual(loc["working-repo-server"]["worktreeOf"], "working repo")
        self.assertEqual(loc["working repo"]["version"], "1.0.1")
        self.assertEqual((loc["working repo"]["dirty"], loc["working repo"]["aheadOfTesting"]), (0, 0))
        hist = m.history.query()
        self.assertEqual(types(hist), ["baseline"])
        gh_json = S.read_json(os.path.join(self.data, "cache", "github.json"))
        self.assertNotIn("local", gh_json)
        self.assertEqual(gh_json["releases"][0], {"tagName": "test-1.0.1-test.7", "isPrerelease": True,
                                                  "publishedAt": "2026-10-23T00:00:00Z",
                                                  "url": "https://github.com/o/r/releases/tag/t"})

        # second poll with nothing changed: GitHub answers 304, the completed run's jobs are not asked again
        self.gh.calls.clear()
        m.poll()
        self.assertNotIn("repos/o/r/actions/runs/101/jobs?per_page=100", self.gh.calls)
        self.assertEqual(types(m.history.query()), ["baseline"])

        # things happen: the run fails, testing moves, a local commit and a dirty file
        self.runs[1] = gh_run(102, "completed", "failure", self.testing1)
        git("checkout", "-q", "testing", cwd=self.origin)
        write(os.path.join(self.origin, "b.txt"), "b\n")
        git("add", "b.txt", cwd=self.origin)
        git("commit", "-qm", "on testing", cwd=self.origin)
        git("checkout", "-q", "main", cwd=self.origin)
        write(os.path.join(self.wr, "local.txt"), "x\n")
        git("add", "local.txt", cwd=self.wr)
        git("commit", "-qm", "my local work", cwd=self.wr)
        write(os.path.join(self.wr, "VERSION"), "1.0.2\n")
        st = m.poll(manual=True)
        got = sorted(types(m.history.query()))
        self.assertEqual(got, sorted(["baseline", "run.failed", "branch.moved", "local.commit", "local.dirty"]))
        failed = [e for e in m.history.query() if e["type"] == "run.failed"][0]
        self.assertEqual(failed["text"], "build-and-release on testing failed (linux-tests)")
        loc = {l["name"]: l for l in st["local"]}["working repo"]
        self.assertEqual((loc["aheadOfTesting"], loc["behindTesting"], loc["dirty"]), (1, 1, 1))
        self.assertEqual(loc["dirtyFiles"], ["M VERSION"])
        self.assertEqual(loc["version"], "1.0.2")  # the edited working file
        self.assertEqual([c["subject"] for c in loc["commits"]], ["my local work"])
        self.assertIsNone(loc["commits"][0]["url"])  # not on GitHub
        # nothing was written into the working copy
        self.assertEqual(git("status", "--porcelain", cwd=self.wr), "M VERSION")
        self.assertEqual(git("for-each-ref", "--format=%(refname)", "refs/local", cwd=self.wr), "")

        # restart: state-prev.json and history.jsonl carry on, no duplicates
        n = len(m.history.events)
        os.remove(os.path.join(self.data, "state-prev.json"))  # as if it crashed before saving: ids dedupe
        m2 = self.manager()
        m2.prev = S.read_json(os.path.join(self.data, "state-prev.json"))
        self.assertIsNone(m2.prev)
        m2.prev = {"at": AT0}  # an old snapshot with unknown sections: nothing to diff, nothing duplicated
        m2.poll()
        self.assertEqual(len(m2.history.events), n)
        m3 = self.manager()
        m3.poll()
        self.assertEqual(len(m3.history.events), n)
        self.assertTrue(m3.runs[101]["jobsDone"])  # loaded from cache/runs/101.json

        # HTTP
        srv = S.make_server(m3, 0)
        port = srv.server_address[1]
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        try:
            base = "http://127.0.0.1:%d" % port

            def get(path, headers=None, method="GET"):
                req = urllib.request.Request(base + path, headers=headers or {}, method=method)
                try:
                    with urllib.request.urlopen(req, timeout=30) as r:
                        return r.status, dict(r.headers), r.read()
                except urllib.error.HTTPError as e:
                    return e.code, dict(e.headers), e.read()
            code, h, body = get("/api/state", {"Origin": "http://127.0.0.1:8768"})
            self.assertEqual(code, 200)
            self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8768")
            state = json.loads(body)
            for k in ("generatedAt", "repo", "defaultBranch", "poll", "production", "testing", "branches", "pulls",
                      "runs", "local", "running"):
                self.assertIn(k, state)
            code, h, body = get("/api/summary", {"Origin": "http://evil.example"})
            self.assertNotIn("Access-Control-Allow-Origin", h)
            summ = json.loads(body)
            self.assertEqual(sorted(summ), sorted(["generatedAt", "repo", "poll", "testing", "production",
                                                   "openPulls", "running", "lastFailed", "lastTesting", "local"]))
            self.assertEqual((summ["lastTesting"]["id"], summ["lastTesting"]["conclusion"]), (102, "failure"))
            self.assertNotIn("jobs", summ["lastTesting"])
            self.assertEqual(summ["lastFailed"]["id"], 102)
            self.assertEqual(summ["openPulls"][0]["checks"]["state"], "failure")
            code, h, body = get("/api/history?limit=2")
            self.assertEqual(len(json.loads(body)), 2)
            code, h, body = get("/api/log?run=101&job=9001")
            self.assertEqual((code, body), (200, b"the windows log\n"))
            code, h, body = get("/api/log?run=101&job=12345")
            self.assertEqual(code, 404)
            code, h, body = get("/api/refresh", {"Origin": "http://localhost:8765",
                                                 "Access-Control-Request-Method": "POST"}, "OPTIONS")
            self.assertEqual((code, h.get("Access-Control-Allow-Origin")), (204, "http://localhost:8765"))
            code, h, body = get("/api/refresh", method="POST")
            self.assertEqual(code, 200)
            code, h, body = get("/api/refresh", method="POST")
            self.assertEqual(h.get("X-Refresh"), "skipped (one per 10 s)")
            self.assertEqual(get("/api/state", {"Host": "evil.example"})[0], 403)
            for bad in ("/server.py", "/cache/github.json", "/%2e%2e/%2e%2e/etc/passwd", "/history.jsonl"):
                self.assertEqual(get(bad)[0], 404, bad)
            self.assertEqual(get("/")[0], 200)

            # ETags, 304s, gzip, runs=N, /api/run
            code, h, body = get("/api/state")
            etag = h["ETag"]
            self.assertTrue(etag.startswith('"st-') and not etag.endswith('-gz"'), etag)
            self.assertEqual(h["Cache-Control"], "no-cache")
            code, h2, body2 = get("/api/state", {"If-None-Match": etag})
            self.assertEqual((code, body2), (304, b""))
            code, hz, bz = get("/api/state", {"Accept-Encoding": "gzip, deflate"})
            self.assertEqual(hz.get("Content-Encoding"), "gzip")
            self.assertEqual(json.loads(gzip.decompress(bz)), json.loads(body))
            self.assertLess(len(bz), len(body))
            self.assertEqual(get("/api/state", {"Accept-Encoding": "gzip", "If-None-Match": hz["ETag"]})[0], 304)
            self.assertEqual(get("/api/state", {"Accept-Encoding": "gzip;q=0"})[1].get("Content-Encoding"), None)
            self.assertIs(m3.body(etag.strip('"'), True, lambda: self.fail("rebuilt")), m3.body(etag.strip('"'), True,
                                                                                               lambda: b""))
            code, h, body = get("/api/state?runs=1")
            one = json.loads(body)
            self.assertEqual(([r["id"] for r in one["runs"]], one["runsTotal"]), ([102], 2))
            self.assertNotEqual(h["ETag"], etag)
            code, h, body = get("/api/run?id=101")
            self.assertEqual((code, json.loads(body)["jobs"][0]["failedSteps"]), (200, ["quark_tests"]))
            self.assertEqual(get("/api/run?id=101", {"If-None-Match": h["ETag"]})[0], 304)
            self.assertEqual(get("/api/run?id=999")[0], 404)
            self.assertEqual(get("/api/run?id=x")[0], 404)
            code, h, body = get("/api/summary")
            self.assertEqual(get("/api/summary", {"If-None-Match": h["ETag"]})[0], 304)
            code, h, body = get("/api/history?limit=2")
            self.assertEqual(get("/api/history?limit=2", {"If-None-Match": h["ETag"]})[0], 304)
            self.assertEqual(get("/api/history?limit=3", {"If-None-Match": h["ETag"]})[0], 200)
            # a new version (a poll, or a log saved on request) changes every ETag
            m3.republish()
            self.assertEqual(get("/api/state", {"If-None-Match": etag})[0], 200)
            m3.history.append([S.ev("x:new", AT1, "tag.created", {}, "t")])
            self.assertEqual(get("/api/history?limit=2", {"If-None-Match": h["ETag"]})[0], 200)
        finally:
            srv.shutdown()
            srv.server_close()


# ---- the editor, part 2: the production lock, previews, pending changes, rules, structure, the docs sync ----------

DEFAULT = "claude/compassionate-ritchie-07ah6i"
REPO = "floration/quark"


def memory_github():
    gh = MemoryGitHub(REPO, DEFAULT)
    gh.rulesets[25800001] = testing_guard()
    for b in (DEFAULT, "testing", "launcher", "site", "discord-bot", "claude/a", "claude/b"):
        gh.branches[b] = sha_of(b)
    return gh


def guard_form(**kw):
    f = S.ruleset_form(S.ruleset_norm(testing_guard()))
    f.update(kw)
    return f


class HardLock(unittest.TestCase):
    """Every case the production lock refuses, straight and through the pending queue (nothing is queued or sent)."""

    def setUp(self):
        self.gh = memory_github()
        self.events = []
        self.p = S.Pending(None, self.gh, REPO, self.events.extend)

    def refused(self, typ, params, contains):
        with self.assertRaises(S.LockError) as cm:
            self.p.queue({"type": typ, "params": params})
        self.assertEqual(cm.exception.code, 422)
        self.assertTrue(cm.exception.locked)
        self.assertIn(contains, str(cm.exception))
        self.assertEqual(self.p.obj()["changes"], [])
        self.assertEqual(self.gh.writes, [])

    def test_branches(self):
        for name in (DEFAULT, "testing", "launcher", "site", "discord-bot", "gh-pages"):
            self.refused("branch.delete", {"name": name}, "can't be deleted")
            self.refused("branch.rename", {"name": name, "newName": "x"}, "can't be renamed")
            self.refused("branch.rename", {"name": "claude/a", "newName": name}, "no branch can be renamed to it")
            self.refused("branch.create", {"name": name, "from": "claude/a"}, "can't be created")
        self.refused("branch.delete", {"name": "refs/heads/testing"}, "testing is a protected branch")
        self.refused("branch.delete", {"name": "origin/" + DEFAULT}, "default (production) branch")

    def test_tags(self):
        for typ in ("tag.create", "tag.move", "tag.delete"):
            self.refused(typ, {"name": "v1.18.7", "sha": "ab" * 20}, "only q creates or moves v* tags")
            self.refused(typ, {"name": "refs/tags/v2.0.0"}, "only q creates or moves v* tags")
        self.refused("tag.create", {"name": "test-1.18.7-test.3"}, "tags are not changed")

    def test_rulesets_targeting_production(self):
        for inc in (["~DEFAULT_BRANCH"], ["~ALL"], ["claude/*"], ["refs/heads/" + DEFAULT], ["claude/compassionate*"],
                    ["**"], ["*"], ["claude/compassionate-ritchie-07ah6?"], ["claude/[c]ompassionate-ritchie-07ah6i"],
                    ["testing", "claude/**"]):
            self.refused("ruleset.create", {"form": guard_form(name="n", include=inc)}, "the ruleset as it would be")
            self.refused("ruleset.update", {"id": 25800001, "form": guard_form(include=inc)}, "matches the default"
                         if inc[-1] not in ("~DEFAULT_BRANCH", "~ALL") else "targets")
        # excluding the default branch does not make a wide pattern acceptable
        self.refused("ruleset.create", {"form": guard_form(name="n", include=["~ALL"], exclude=["~DEFAULT_BRANCH"])},
                     "every branch")
        # the locked rulesets themselves: production (default branch), tagging (v* tags), by name too
        self.refused("ruleset.update", {"id": 25727099, "form": guard_form()}, "the ruleset as it is on GitHub")
        self.refused("ruleset.delete", {"id": 25727099}, "targets the default branch")
        self.refused("ruleset.delete", {"id": 25727168}, "targets v* tags")
        self.refused("ruleset.update", {"id": 25727168, "form": guard_form()}, "only q creates or moves")
        self.gh.rulesets[25727099]["conditions"]["ref_name"]["include"] = ["refs/heads/elsewhere"]
        self.refused("ruleset.delete", {"id": 25727099}, "'production rule' ruleset guards production")
        self.refused("ruleset.create", {"form": guard_form(name="Tagging Rule")}, "guards production")

    def test_function_directly(self):
        lock = lambda *a, **k: self.assertRaises(S.LockError, S.hard_lock, *a, **k)
        lock("branch.delete", {"name": "claude/a"}, None)   # the default branch is unknown: refuse everything
        lock("ruleset.create", {}, DEFAULT, after={"target": "push", "conditions": {}})
        lock("ruleset.create", {}, DEFAULT, after={"target": "tag", "conditions": {"ref_name": {"include": ["v*"]}}})
        lock("repo.delete", {}, DEFAULT)
        lock("branch.update", {"name": "claude/a"}, DEFAULT)
        S.hard_lock("branch.delete", {"name": "claude/a"}, DEFAULT)
        S.hard_lock("ruleset.create", {}, DEFAULT, after=S.ruleset_from_form(guard_form(include=["claude/project-*"])))
        self.assertTrue(S.pattern_hits_branch("refs/heads/claude/*", "claude/x/y"))   # wider than GitHub: on purpose
        self.assertFalse(S.pattern_hits_branch("refs/heads/testing", DEFAULT))

    def test_apply_rechecks_the_lock_with_the_default_branch_read_then(self):
        ch = self.p.queue({"type": "branch.delete", "params": {"name": "claude/b"}})
        self.gh.default = "claude/b"   # q made claude/b the default branch after the preview
        out = self.p.apply(ch["id"], {"previewHash": ch["previewHash"]})
        self.assertEqual(out["status"], "refused")
        self.assertTrue(out["result"]["locked"])
        self.assertIn("default (production) branch", out["result"]["error"])
        self.assertEqual(self.gh.writes, [])
        # a ruleset that became locked on GitHub since the preview is refused too
        self.gh.default = DEFAULT
        ch = self.p.queue({"type": "ruleset.update", "params": {"id": 25800001, "form": guard_form(requirePr=True)}})
        self.gh.rulesets[25800001]["conditions"]["ref_name"]["include"].append("~DEFAULT_BRANCH")
        out = self.p.apply(ch["id"])
        self.assertEqual(out["status"], "refused")
        self.assertEqual(self.gh.writes, [])
        # a change file edited by hand to send another call: the call is rebuilt from the parameters and refused
        ch = self.p.queue({"type": "branch.delete", "params": {"name": "claude/a"}})
        self.p.changes[ch["id"]]["params"] = {"name": "testing"}
        out = self.p.apply(ch["id"])
        self.assertEqual(out["status"], "refused")
        self.assertEqual(self.gh.writes, [])


class Previews(unittest.TestCase):
    def setUp(self):
        self.gh = memory_github()
        self.events = []
        self.p = S.Pending(None, self.gh, REPO, self.events.extend, now=lambda: "2026-10-24T12:00:00Z")

    def test_branch_previews(self):
        ch = self.p.queue({"type": "branch.create", "params": {"name": "claude/new", "from": "testing"}})
        t = sha_of("testing")
        self.assertEqual(ch["summary"], "Create branch claude/new from testing (%s)" % t[:7])
        self.assertEqual(ch["call"], {"method": "POST", "path": "repos/%s/git/refs" % REPO,
                                      "body": {"ref": "refs/heads/claude/new", "sha": t}})
        self.assertEqual(ch["command"], 'gh api -i --method POST -H "Accept: application/vnd.github+json" '
                                        '-H "X-GitHub-Api-Version: 2022-11-28" repos/%s/git/refs --input -' % REPO)
        self.assertEqual(ch["basis"], {"defaultBranch": DEFAULT, "exists": False, "fromSha": t})
        self.assertEqual((ch["beforeText"], ch["status"]), ("No branch claude/new on GitHub.", "pending"))
        ch = self.p.queue({"type": "branch.create", "params": {"name": "claude/from-sha", "from": t[:10].upper()}})
        self.assertEqual(ch["call"]["body"]["sha"], t)
        self.gh.ahead[sha_of("claude/a")] = 3
        self.gh.pulls.append({"number": 12, "head": "claude/a"})
        ch = self.p.queue({"type": "branch.delete", "params": {"name": "claude/a"}})
        self.assertIn("3 commits not in testing; open PR #12", ch["beforeText"])
        self.assertEqual(len(ch["warnings"]), 2)
        self.assertIn("deleting the branch closes it", ch["warnings"][1])
        self.assertEqual(ch["call"], {"method": "DELETE", "path": "repos/%s/git/refs/heads/claude/a" % REPO,
                                      "body": None})
        ch = self.p.queue({"type": "branch.rename", "params": {"name": "claude/b", "newName": "claude/b2"}})
        self.assertEqual(ch["call"]["body"], {"new_name": "claude/b2"})
        for bad, code, text in (({"name": "claude/a", "from": "testing"}, 409, "already exists"),
                                ({"name": "claude/c", "from": "nope"}, 409, "no branch nope"),
                                ({"name": "claude/c", "from": "abcdef1234"}, 409, "no commit"),
                                ({"name": "bad name", "from": "testing"}, 400, "not a valid branch name"),
                                ({"name": "x/../y", "from": "testing"}, 400, "not a valid branch name"),
                                ({"name": "claude/c", "x": 1}, 400, "unknown field")):
            with self.assertRaises(S.EditError) as cm:
                self.p.queue({"type": "branch.create", "params": bad})
            self.assertEqual(cm.exception.code, code, bad)
            self.assertIn(text, str(cm.exception))
        with self.assertRaises(S.EditError) as cm:
            self.p.queue({"type": "branch.create", "params": {"name": "claude/new", "from": "testing"}})
        self.assertIn("already queued", str(cm.exception))

    def test_ruleset_previews(self):
        form = guard_form(requirePr=True, approvals=1, requiredChecks=["meta", "linux"], strictChecks=True,
                          linearHistory=True)
        ch = self.p.queue({"type": "ruleset.update", "params": {"id": 25800001, "form": form}})
        self.assertEqual(ch["call"]["method"], "PUT")
        self.assertEqual(ch["call"]["path"], "repos/%s/rulesets/25800001" % REPO)
        self.assertEqual(ch["before"], S.ruleset_norm(testing_guard()))
        self.assertEqual(ch["after"], ch["call"]["body"])
        self.assertTrue(ch["diff"][0].startswith("@@"))
        added = [l for l in ch["diff"] if l.startswith("+")]
        self.assertTrue(any('"type": "pull_request"' in l for l in added))
        self.assertTrue(any('"context": "meta"' in l for l in added))
        self.assertTrue(any('"type": "required_linear_history"' in l for l in added))
        self.assertFalse([l for l in ch["diff"] if l.startswith("-")])
        self.assertIn("requires a PR (1 approval)", ch["afterText"])
        self.assertIn("requires checks: meta, linux (up to date)", ch["afterText"])
        # nothing to change; a name that is taken; a form with an unknown field
        for body, text in (({"type": "ruleset.update", "params": {"id": 25800001, "form": guard_form()}},
                            "nothing would change"),
                           ({"type": "ruleset.create", "params": {"form": guard_form()}}, "already exists"),
                           ({"type": "ruleset.create", "params": {"form": dict(guard_form(name="n"), target="tag")}},
                            "unknown field"),
                           ({"type": "ruleset.create", "params": {"form": guard_form(name="n", blockForcePush=False,
                                                                                     blockDeletion=False)}},
                            "at least one rule"),
                           ({"type": "ruleset.create", "params": {"form": guard_form(name="n",
                                                                                     enforcement="evaluate")}},
                            "active or disabled"),
                           ({"type": "ruleset.update", "params": {"id": 99, "form": guard_form()}}, "no ruleset 99")):
            with self.assertRaises(S.EditError) as cm:
                self.p.queue(body)
            self.assertIn(text, str(cm.exception))
        ch = self.p.queue({"type": "ruleset.create", "params": {"form": guard_form(name="work branches",
                                                                                   include=["claude/project-*"])}})
        self.assertEqual(ch["before"], None)
        self.assertTrue(all(l.startswith(("+", "@@")) for l in ch["diff"]))
        self.assertEqual(ch["after"]["conditions"]["ref_name"]["include"], ["refs/heads/claude/project-*"])
        ch = self.p.queue({"type": "ruleset.delete", "params": {"id": 25800001}})
        self.assertEqual(ch["after"], None)
        self.assertTrue(all(l.startswith(("-", "@@")) for l in ch["diff"]))
        self.assertEqual(ch["summary"], "Delete ruleset 'testing guard' (testing)")

    def test_form_keeps_what_it_does_not_show(self):
        rs = testing_guard()
        rs["rules"].append({"type": "required_signatures"})
        rs["bypass_actors"] = [{"actor_id": 7, "actor_type": "Team", "bypass_mode": "pull_request"}]
        n = S.ruleset_norm(rs)
        f = S.ruleset_form(n)
        self.assertEqual(S.ruleset_from_form(f, n), n)   # a round trip changes nothing
        after = S.ruleset_from_form(dict(f, adminBypass=True, requirePr=True), n)
        self.assertIn({"type": "required_signatures"}, after["rules"])
        self.assertEqual(len(after["bypass_actors"]), 2)
        self.assertIn(S.ADMIN_BYPASS, after["bypass_actors"])
        # GitHub hides bypass_actors from non-admins: then the PUT leaves them alone
        rs.pop("bypass_actors")
        self.assertNotIn("bypass_actors", S.ruleset_from_form(f, S.ruleset_norm(rs)))
        self.assertEqual(S.json_diff(n, n), [])

    def test_gh_write_error_is_exact(self):
        body = json.dumps({"message": "Validation Failed", "errors": ["Invalid rule 'pull_request'",
                                                                      {"resource": "Ruleset", "code": "invalid"}]})
        self.assertEqual(S.gh_write_error(422, body, "gh: Validation Failed (HTTP 422)"),
                         "HTTP 422: Validation Failed; Invalid rule 'pull_request'; code=invalid, resource=Ruleset")
        self.assertEqual(S.gh_write_error(None, "", "error connecting to api.github.com\nmore"),
                         "HTTP ?: error connecting to api.github.com")


class ApplyAndStale(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-pending-")
        self.gh = memory_github()
        self.events = []
        self.applied = []
        self.p = S.Pending(self.dir, self.gh, REPO, self.events.extend, on_applied=self.applied.append)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_apply_ok_events_and_file(self):
        ch = self.p.queue({"type": "branch.create", "params": {"name": "claude/new", "from": "claude/a"}})
        out = self.p.apply(ch["id"], {"previewHash": ch["previewHash"]})
        self.assertEqual(out["status"], "applied")
        self.assertTrue(out["result"]["ok"])
        self.assertEqual(self.gh.branches["claude/new"], sha_of("claude/a"))
        self.assertEqual(self.gh.writes, [("POST", "repos/%s/git/refs" % REPO,
                                           {"ref": "refs/heads/claude/new", "sha": sha_of("claude/a")})])
        self.assertEqual(types(self.events), ["github.branch.created"])
        self.assertEqual(self.events[0]["ref"], {"branch": "claude/new"})
        self.assertTrue(self.events[0]["text"].startswith("Created branch claude/new from claude/a"))
        self.assertEqual(len(self.applied), 1)
        with self.assertRaises(S.EditError) as cm:
            self.p.apply(ch["id"])
        self.assertIn("applied already", str(cm.exception))
        # the file: reloaded with the result, applied ones kept until discarded
        p2 = S.Pending(self.dir, self.gh, REPO)
        self.assertEqual(p2.get(ch["id"])["status"], "applied")
        self.assertEqual(p2.discard(ch["id"]), ch["id"])
        self.assertEqual(S.Pending(self.dir, self.gh, REPO).obj()["changes"], [])
        # rulesets: create, update, delete
        ch = self.p.queue({"type": "ruleset.create", "params": {"form": guard_form(name="work", include=["claude/b"])}})
        out = self.p.apply(ch["id"])
        rid = out["result"]["rulesetId"]
        self.assertEqual(self.gh.rulesets[rid]["name"], "work")
        ch = self.p.queue({"type": "ruleset.update", "params": {"id": rid, "form": guard_form(
            name="work", include=["claude/b"], linearHistory=True)}})
        self.assertEqual(self.p.apply(ch["id"])["status"], "applied")
        ch = self.p.queue({"type": "ruleset.delete", "params": {"id": rid}})
        self.assertEqual(self.p.apply(ch["id"])["status"], "applied")
        self.assertNotIn(rid, self.gh.rulesets)
        self.assertEqual(types(self.events), ["github.branch.created", "github.ruleset.created",
                                              "github.ruleset.updated", "github.ruleset.deleted"])
        self.assertEqual(self.events[1]["ref"], {"ruleset": rid})

    def test_stale_before_apply(self):
        ch = self.p.queue({"type": "branch.delete", "params": {"name": "claude/a"}})
        self.gh.branches["claude/a"] = sha_of("someone pushed")
        out = self.p.apply(ch["id"], {"previewHash": ch["previewHash"]})
        self.assertEqual(out["status"], "stale")
        self.assertIn(S.STALE_MSG, out["result"]["error"])
        self.assertIn("the branch moved from", out["result"]["error"])
        self.assertEqual(self.gh.writes, [])
        self.assertIn("claude/a", self.gh.branches)
        # a PR opened since the preview is a change too
        self.gh.pulls.append({"number": 20, "head": "claude/a"})
        out = self.p.repreview(ch["id"])
        self.assertEqual(out["status"], "pending")
        self.assertNotEqual(out["previewHash"], ch["previewHash"])
        # the page still shows the first preview: its hash no longer matches
        with self.assertRaises(S.EditError) as cm:
            self.p.apply(ch["id"], {"previewHash": ch["previewHash"]})
        self.assertEqual(cm.exception.code, 409)
        self.assertIn("previewed again", str(cm.exception))
        self.gh.pulls.append({"number": 21, "head": "claude/a"})
        out = self.p.apply(ch["id"], {"previewHash": out["previewHash"]})
        self.assertEqual(out["status"], "stale")
        self.assertIn("open PRs are now #20, #21 (were #20)", out["result"]["error"])
        # a ruleset changed on GitHub since the preview
        ch = self.p.queue({"type": "ruleset.update", "params": {"id": 25800001, "form": guard_form(linearHistory=True)}})
        self.gh.rulesets[25800001]["rules"].append({"type": "required_signatures"})
        out = self.p.apply(ch["id"])
        self.assertEqual(out["status"], "stale")
        self.assertIn("the ruleset was changed on GitHub", out["result"]["error"])
        # created elsewhere in between
        ch = self.p.queue({"type": "branch.create", "params": {"name": "claude/c", "from": "testing"}})
        self.gh.branches["claude/c"] = sha_of("x")
        self.assertIn("the branch exists now", self.p.apply(ch["id"])["result"]["error"])
        # the default branch moved
        ch = self.p.queue({"type": "ruleset.create", "params": {"form": guard_form(name="w", include=["claude/b"])}})
        self.gh.default = "testing"
        self.assertIn("the default branch is now testing", self.p.apply(ch["id"])["result"]["error"])
        self.assertEqual(self.gh.writes, [])
        self.assertEqual(self.events, [])

    def test_apply_errors_and_apply_all(self):
        a = self.p.queue({"type": "branch.delete", "params": {"name": "claude/a"}})
        b = self.p.queue({"type": "branch.delete", "params": {"name": "claude/b"}})
        c = self.p.queue({"type": "branch.create", "params": {"name": "claude/c", "from": "testing"}})
        self.gh.fail_next = (422, "Validation Failed", ["Reference update failed"])
        res = self.p.apply_all()
        self.assertEqual((res["applied"], res["stoppedAt"]), (0, a["id"]))
        failed = self.p.get(a["id"])
        self.assertEqual(failed["status"], "failed")
        self.assertEqual(failed["result"]["error"], "HTTP 422: Validation Failed; Reference update failed")
        self.assertEqual(failed["result"]["httpStatus"], 422)
        self.assertEqual(types(self.events), ["github.apply.failed"])
        self.assertIn("Reference update failed", self.events[0]["text"])
        self.assertEqual(self.p.get(b["id"])["status"], "pending")
        # apply-all takes the pending ones; the failed one is applied (again) on its own
        res = self.p.apply_all({})
        self.assertEqual((res["applied"], res["stoppedAt"]), (2, None))
        self.assertEqual(self.p.apply(a["id"])["status"], "applied")
        self.assertEqual(sorted(self.gh.branches), sorted([DEFAULT, "testing", "launcher", "site", "discord-bot",
                                                           "claude/c"]))
        with self.assertRaises(S.EditError):
            self.p.apply_all({"x": 1})
        with self.assertRaises(S.EditError):
            self.p.apply(c["id"], {"confirm": True})
        # gh missing / GitHub unreachable while reading: failed, with the reason, and nothing sent
        d = self.p.queue({"type": "branch.delete", "params": {"name": "claude/c"}})
        self.gh.default = None
        out = self.p.apply(d["id"])
        self.assertEqual(out["status"], "failed")
        self.assertIn("default", out["result"]["error"])

    def test_interrupted_apply_is_failed_on_load(self):
        ch = self.p.queue({"type": "branch.delete", "params": {"name": "claude/a"}})
        self.p.changes[ch["id"]]["status"] = "applying"
        self.p._save()
        c = S.Pending(self.dir, self.gh, REPO).get(ch["id"])
        self.assertEqual(c["status"], "failed")
        self.assertIn("server stopped", c["result"]["error"])


class RulesStore(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-rules-")
        self.events = []
        self.t = iter("2026-10-24T10:%02d:00Z" % i for i in range(60))
        self.r = S.Rules(self.dir, self.events.extend, now=lambda: next(self.t))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def rules(self):
        return [dict(x) for x in self.r.obj()["rules"]]

    def test_seed_and_limits(self):
        ids = [x["id"] for x in self.rules()]
        self.assertEqual(ids[0], S.HARD_RULE_ID)
        for want in ("claude-pushes-non-production", "testing-version-bump", "sanitizers-final-only", "ci-scope",
                     "sync-pc-copies", "never-launch-quark-exe", "macos-skip-retry", "limit-open-prs-per-thread",
                     S.STALE_RULE_ID, "limit-ci-job-minutes"):
            self.assertIn(want, ids)
        self.assertTrue(self.rules()[0]["locked"])
        self.assertEqual(self.r.limit(S.STALE_RULE_ID, 99), 7)
        self.assertTrue(os.path.exists(os.path.join(self.dir, "rules.json")))
        self.assertEqual(S.Rules(self.dir).obj()["rules"], self.r.obj()["rules"])  # loaded, not seeded again

    def test_edit_add_remove_and_events(self):
        rs = self.rules()
        for x in rs:
            if x["id"] == S.STALE_RULE_ID:
                x["value"] = 14
        rs = [x for x in rs if x["id"] != "ci-scope"]
        rs.append({"title": "No force pushes to testing", "text": "Never force-push testing.", "kind": "push",
                   "scope": {"type": "branch", "match": "testing"}})
        out = self.r.replace({"rules": rs, "rev": 1})
        self.assertEqual(out["rev"], 2)
        self.assertEqual(self.r.limit(S.STALE_RULE_ID, 0), 14)
        new = [x for x in out["rules"] if x["title"] == "No force pushes to testing"][0]
        self.assertEqual(new["id"], "no-force-pushes-to-testing")
        self.assertEqual(new["value"], None)
        self.assertEqual(types(self.events), ["rules.changed"])
        e = self.events[0]
        self.assertEqual((e["data"]["added"], e["data"]["edited"], e["data"]["removed"]),
                         (["no-force-pushes-to-testing"], [S.STALE_RULE_ID], ["ci-scope"]))
        self.assertIn("removed CI runs meta, builds and sanitizers", e["text"])
        self.assertIn("ci-scope", e["data"]["removedRules"])
        # the same list again: no write, no event; an old rev: 409
        self.r.replace({"rules": out["rules"]})
        self.assertEqual(len(self.events), 1)
        with self.assertRaises(S.EditError) as cm:
            self.r.replace({"rules": out["rules"], "rev": 1})
        self.assertEqual(cm.exception.code, 409)
        self.assertTrue(os.path.exists(os.path.join(self.dir, "rules.json.bak")))
        self.assertEqual(S.Rules(self.dir).limit(S.STALE_RULE_ID, 0), 14)

    def test_the_production_rule_is_locked(self):
        rs = self.rules()
        for change in ({"text": "Anyone may push to production."}, {"locked": False}, {"kind": "other"},
                       {"scope": {"type": "branch", "match": "testing"}}, {"title": "x"}):
            changed = [dict(x, **change) if x["id"] == S.HARD_RULE_ID else x for x in rs]
            with self.assertRaises(S.LockError):
                self.r.replace({"rules": changed})
        with self.assertRaises(S.LockError):
            self.r.replace({"rules": rs[1:]})
        self.assertEqual(self.events, [])
        # moving it is fine, other rules can be locked / unlocked by q
        rs2 = rs[1:] + rs[:1]
        rs2[0]["locked"] = True
        self.r.replace({"rules": rs2})
        rules = self.r.obj()["rules"]
        self.assertEqual(rules[-1]["id"], S.HARD_RULE_ID)
        self.assertTrue(rules[0]["locked"])
        # and a stored file can't weaken it: the text always comes from the code
        d = S.read_json(os.path.join(self.dir, "rules.json"))
        for x in d["rules"]:
            if x["id"] == S.HARD_RULE_ID:
                x["text"], x["locked"] = "weakened", False
        write(os.path.join(self.dir, "rules.json"), json.dumps(d))
        r = S.Rules(self.dir).obj()["rules"]
        self.assertEqual(r[0]["text"], S.HARD_RULE["text"])
        self.assertTrue(r[0]["locked"])

    def test_validation(self):
        base = self.rules()
        for bad, text in (({"title": "t", "text": "x", "kind": "limit"}, "needs a value"),
                          ({"title": "t", "text": "x", "kind": "ci", "value": 3}, "only a limit"),
                          ({"title": "t", "text": "x", "kind": "nope"}, "kind must be"),
                          ({"title": "", "text": "x"}, "title can't be empty"),
                          ({"title": "t", "text": "x", "scope": {"type": "branch"}}, "scope.match can't be empty"),
                          ({"title": "t", "text": "x", "scope": {"type": "everywhere"}}, "scope.type"),
                          ({"title": "t", "text": "x", "id": "Bad Id"}, "a rule id"),
                          ({"title": "t", "text": "x", "x": 1}, "unknown field"),
                          ({"title": "t", "text": "x" * (S.RULE_TEXT_MAX + 1)}, "text is over"),
                          ({"title": "t\x07", "text": "x"}, "control character"),
                          ({"title": "t", "text": "x", "kind": "limit", "value": True}, "needs a value")):
            with self.assertRaises(S.EditError) as cm:
                self.r.replace({"rules": base + [bad]})
            self.assertEqual(cm.exception.code, 400, bad)
            self.assertIn(text, str(cm.exception))
        for bad in ({"rules": base, "x": 1}, {"rules": "no"}, [], {"rules": base + [base[1]]}):
            with self.assertRaises(S.EditError):
                self.r.replace(bad)
        self.assertEqual(self.events, [])

    def test_markdown_and_export(self):
        md = self.r.markdown(REPO, DEFAULT)
        self.assertTrue(md.startswith("# GitHub rules\n\n> " + S.DOCS_GENERATED))
        self.assertIn("The default branch (production) is `%s`." % DEFAULT, md)
        self.assertIn("- **Only q pushes to production** (locked; everywhere; `only-q-production`)", md)
        self.assertIn("- **Stale branch age**: **7 days** (branch `claude/*`; `limit-stale-days`)", md)
        self.assertIn("- **Never launch quark.exe on q's PC** (tool `quark.exe`; `never-launch-quark-exe`)", md)
        heads = [l for l in md.splitlines() if l.startswith("## ")]
        self.assertEqual(heads, ["## Pushing", "## Merging", "## Versions and changelogs", "## CI", "## Limits",
                                 "## Other"])
        self.assertEqual(md, self.r.markdown(REPO, DEFAULT))   # the same rules give the same file
        rs = self.rules()
        rs[1]["text"] = "line one\nline two"
        self.r.replace({"rules": rs})
        md = self.r.markdown(REPO, DEFAULT)
        self.assertIn("  line one\n  line two\n", md)
        ex = self.r.export(REPO, DEFAULT)
        self.assertEqual((ex["repo"], ex["defaultBranch"], ex["rev"], ex["note"]), (REPO, DEFAULT, 2, S.DOCS_GENERATED))
        self.assertEqual(ex["rules"], self.r.obj()["rules"])


class StructureStore(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="ghm-structure-")
        self.events = []
        self.s = S.Structure(self.dir, self.events.extend)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_update_and_roles(self):
        out = self.s.update({"branches": {"claude/macos-skip": {"role": "infra", "description": "macOS skip test"},
                                          "testing": {"role": None, "description": "Every Claude merges here."}},
                             "rev": 0}, DEFAULT)
        self.assertEqual(out["rev"], 1)
        self.assertEqual(out["branches"]["claude/macos-skip"]["role"], "infra")
        self.assertEqual(types(self.events), ["structure.changed"])
        self.assertIn("claude/macos-skip: role infra, description set", self.events[0]["text"])
        for bad, text in (({"branches": {"testing": {"role": "work"}}}, "comes from the repository"),
                          ({"branches": {DEFAULT: {"role": "other"}}}, "comes from the repository"),
                          ({"branches": {"x": {"role": "production"}}}, "role must be"),
                          ({"branches": {"x": {"role": None, "owner": "q"}}}, "unknown field"),
                          ({"branches": {"bad name": {"role": None}}}, "not a valid branch name"),
                          ({"branches": {"x": {"description": "d" * 2001}}}, "description is over"),
                          ({"branches": {}}, "send"), ({"x": 1}, "unknown field")):
            with self.assertRaises(S.EditError) as cm:
                self.s.update(bad, DEFAULT)
            self.assertIn(text, str(cm.exception))
        with self.assertRaises(S.EditError) as cm:
            self.s.update({"branches": {"x": {"role": "work"}}, "rev": 0}, DEFAULT)
        self.assertEqual(cm.exception.code, 409)
        self.s.update({"branches": {"testing": {"role": None, "description": "Every Claude merges here."}}}, DEFAULT)
        self.assertEqual(len(self.events), 1)   # nothing changed: no event
        self.s.update({"branches": {"claude/macos-skip": None}}, DEFAULT)
        self.assertNotIn("claude/macos-skip", S.Structure(self.dir).obj()["branches"])
        self.assertIn("testing", S.Structure(self.dir).obj()["branches"])

    def test_branch_roles_and_stale(self):
        now = S.parse_time("2026-10-24T12:00:00Z")
        old, new = "2026-10-10T12:00:00Z", "2026-10-23T12:00:00Z"
        b = lambda name, date, ahead, behind: {"name": name, "date": date, "ahead": ahead, "behind": behind}
        branches = [b(DEFAULT, old, 0, 40), b("testing", old, 0, 0), b("launcher", old, 0, 9),
                    b("claude/old-merged", old, 0, 0), b("claude/old-behind", old, 2, 5), b("claude/old-ahead", old, 3, 0),
                    b("claude/new", new, 0, 5), b("dependabot/x", old, 1, 2), b("claude/unknown", old, None, None)]
        self.s.update({"branches": {"claude/old-behind": {"role": "infra"}, "dependabot/x": {"description": "bot"}}})
        S.branch_roles(branches, self.s, 7, DEFAULT, now)
        got = dict((x["name"], (x["role"], x["kind"], x["roleSet"], x["stale"])) for x in branches)
        self.assertEqual(got, {DEFAULT: ("production", "production", False, False),
                               "testing": ("testing", "testing", False, False),
                               "launcher": ("infra", "infra", False, False),
                               "claude/old-merged": ("work", "claude", False, True),
                               "claude/old-behind": ("infra", "infra", True, False),
                               "claude/old-ahead": ("work", "claude", False, False),
                               "claude/new": ("work", "claude", False, False),
                               "dependabot/x": ("other", "other", False, True),
                               "claude/unknown": ("work", "claude", False, False)})
        self.assertEqual(branches[7]["description"], "bot")
        S.branch_roles(branches, self.s, 30, DEFAULT, now)
        self.assertFalse(any(x["stale"] for x in branches))


def bare_docs_repo(root):
    """A local bare repo standing in for floration/quark-claude-docs: main with a README."""
    bare = os.path.join(root, "quark-claude-docs.git")
    git("init", "-q", "--bare", "-b", "main", bare)
    seed = os.path.join(root, "seed")
    git("clone", "-q", bare, seed)
    write(os.path.join(seed, "README.md"), "# Claude Documents\n")
    git("add", "README.md", cwd=seed)
    git("commit", "-qm", "first", cwd=seed)
    git("push", "-q", "origin", "HEAD:main", cwd=seed)
    return bare, seed


class DocsProc(S.Proc):
    """git for real; `gh repo clone <repo> <dir> -- <git flags>` clones the local bare repo instead of GitHub; any other
    gh call fails the test."""

    def __init__(self, bare):
        S.Proc.__init__(self)
        self.bare = bare
        self.gh_calls = []
        self.env["GIT_CONFIG_GLOBAL"] = os.devnull   # no identity from the machine: the fallback one is used
        self.env["GIT_CONFIG_NOSYSTEM"] = "1"

    def run(self, args, cwd=None, timeout=90, binary=False, input=None):
        if args[0] == "gh":
            self.gh_calls.append(args)
            assert args[1:3] == ["repo", "clone"] and args[5] == "--", args
            return S.Proc.run(self, ["git", "clone"] + args[6:] + [self.bare, args[4]], cwd=cwd, timeout=timeout)
        return S.Proc.run(self, args, cwd=cwd, timeout=timeout, binary=binary, input=input)


class DocsSyncTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="ghm-docs-")
        self.bare, self.seed = bare_docs_repo(self.root)
        self.files = {"09-github-rules.md": "# GitHub rules\n\none\n", "github-rules.json": "{}\n",
                      "10-github-notes.md": "# GitHub notes\n"}
        self.proc = DocsProc(self.bare)
        self.state = os.path.join(self.root, "data", "editor", "docs-sync.json")
        self.d = S.DocsSync(os.path.join(self.root, "data", "claude-docs"), self.state, self.proc,
                            lambda: dict(self.files), repo=S.DOCS_REPO, debounce=0.3)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def log(self):
        return git("--git-dir", self.bare, "log", "--format=%s", "main").splitlines()

    def show(self, name):
        return git("--git-dir", self.bare, "show", "main:" + name)

    def wait(self, pred, sec=20):
        t = time.time() + sec
        while time.time() < t:
            if pred():
                return True
            time.sleep(0.05)
        return False

    def test_clone_commit_push_and_no_change(self):
        st = self.d.sync_now()
        self.assertEqual((st["state"], st["lastError"]), ("ok", None))
        self.assertEqual(self.proc.gh_calls[0][:5], ["gh", "repo", "clone", S.DOCS_REPO,
                                                     os.path.join(self.root, "data", "claude-docs")])
        self.assertIn("credential.helper=!gh auth git-credential", self.proc.gh_calls[0])
        self.assertEqual(self.log(), [S.DOCS_COMMIT_MSG, "first"])
        self.assertEqual(self.show("09-github-rules.md"), "# GitHub rules\n\none")
        self.assertEqual(self.show("README.md"), "# Claude Documents")   # other files are left alone
        self.assertEqual(st["lastCommit"], git("--git-dir", self.bare, "rev-parse", "main"))
        self.assertEqual(S.read_json(self.state)["digest"], S._digest(self.files))
        # nothing changed: no commit; at a restart, nothing to do
        self.d.sync_now()
        self.assertEqual(len(self.log()), 2)
        d2 = S.DocsSync(self.d.folder, self.state, self.proc, lambda: dict(self.files), debounce=0.3)
        self.assertFalse(d2.schedule_if_changed(0))
        self.files["10-github-notes.md"] = "# GitHub notes\n\nnew\n"
        self.assertTrue(d2.schedule_if_changed(0))
        self.assertTrue(self.wait(lambda: len(self.log()) == 3 and d2.status()["state"] == "ok"))
        self.assertEqual(len(self.proc.gh_calls), 1)   # cloned once

    def test_rejected_push_is_retried_after_pull_rebase(self):
        self.d.sync_now()
        # someone else pushes in between (after our fetch): simulate by pushing from the seed clone, then making
        # the tool's fetch see the old state: push right after our reset
        real = self.proc.run
        pushed = []

        def racing(args, **kw):
            if args[:3] == ["git", "-C", self.d.folder] and "commit" in args and not pushed:
                pushed.append(1)
                git("pull", "-q", "origin", "main", cwd=self.seed)
                write(os.path.join(self.seed, "05-other.md"), "someone else\n")
                git("add", "05-other.md", cwd=self.seed)
                git("commit", "-qm", "other change", cwd=self.seed)
                git("push", "-q", "origin", "HEAD:main", cwd=self.seed)
            return real(args, **kw)
        self.proc.run = racing
        self.files["09-github-rules.md"] = "# GitHub rules\n\ntwo\n"
        st = self.d.sync_now()
        self.assertEqual(st["state"], "ok", st["lastError"])
        self.assertEqual(self.log(), [S.DOCS_COMMIT_MSG, "other change", S.DOCS_COMMIT_MSG, "first"])
        self.assertEqual(self.show("05-other.md"), "someone else")
        self.assertEqual(self.show("09-github-rules.md"), "# GitHub rules\n\ntwo")

    def test_debounce_one_commit_for_a_burst(self):
        self.d.sync_now()
        for i in range(5):
            self.files["09-github-rules.md"] = "# GitHub rules\n\nedit %d\n" % i
            self.d.schedule()
            self.assertEqual(self.d.status()["state"], "waiting")
            time.sleep(0.05)
        self.assertTrue(self.wait(lambda: self.d.status()["state"] == "ok"))
        self.assertEqual(self.log(), [S.DOCS_COMMIT_MSG, S.DOCS_COMMIT_MSG, "first"])
        self.assertEqual(self.show("09-github-rules.md"), "# GitHub rules\n\nedit 4")

    def test_failures_are_reported_never_raised(self):
        shutil.rmtree(self.bare)
        st = self.d.sync_now()
        self.assertEqual(st["state"], "failed")
        self.assertIn("gh repo clone", st["lastError"])
        self.assertTrue(self.d.warnings()[0].startswith("Claude Documents sync failed"))
        os.makedirs(self.d.folder)
        write(os.path.join(self.d.folder, "x.txt"), "not a clone")
        self.assertIn("is not a git clone", self.d.sync_now()["lastError"])
        off = S.DocsSync(self.d.folder, None, self.proc, dict, enabled=False, off_reason="off (test)")
        off.schedule()
        self.assertEqual((off.status()["state"], off.sync_now()["state"], off.warnings()), ("off", "off", []))

    def test_wanted(self):
        env = os.environ.pop("GHM_DOCS_SYNC")
        try:
            self.assertEqual(S.docs_sync_wanted({"repo": REPO}), (True, None))
            self.assertFalse(S.docs_sync_wanted({"repo": "o/r"})[0])
            self.assertTrue(S.docs_sync_wanted({"repo": "o/r", "claudeDocsRepo": "o/docs"})[0])
            self.assertFalse(S.docs_sync_wanted({"repo": REPO, "claudeDocs": False})[0])
        finally:
            os.environ["GHM_DOCS_SYNC"] = env
        self.assertFalse(S.docs_sync_wanted({"repo": REPO})[0])

    def test_manager_files(self):
        data = os.path.join(self.root, "m")
        m = S.Manager({"repo": REPO, "port": 0, "quarkDir": data}, data, os.path.dirname(os.path.abspath(S.__file__)),
                      quiet=True)
        self.assertEqual(m.docs.status()["state"], "off")
        m.default_branch = DEFAULT
        m.notes.create({"target": {"kind": "branch", "key": "testing"}, "text": "keep testing green"})
        f = m.docs_files()
        self.assertEqual(sorted(f), sorted(S.DOCS_FILES))
        self.assertIn(S.DOCS_GENERATED, f["10-github-notes.md"].splitlines()[2])
        self.assertIn("keep testing green", f["10-github-notes.md"])
        self.assertNotIn("Written", f["10-github-notes.md"])
        self.assertEqual(json.loads(f["github-rules.json"])["defaultBranch"], DEFAULT)
        self.assertEqual(f, m.docs_files())


class EditorHttp(NotesHttp):
    """The editor's endpoints, with the same write protections as the notes and an in-memory GitHub."""

    def setUp(self):
        NotesHttp.setUp(self)
        self.gh = memory_github()
        self.gh.repo = "o/r"
        self.m.gh = self.m.pending.gh = self.gh
        self.m.default_branch = DEFAULT

    def test_crud_etag_history_and_safety(self):
        pass  # the notes' own test runs in NotesHttp

    def test_structure_rules_pending(self):
        for path in ("/api/structure", "/api/rules", "/api/pending"):
            code, h, body = self.call("GET", path, headers={"Content-Type": None})
            self.assertEqual(code, 200, path)
            self.assertEqual(self.call("GET", path, headers={"If-None-Match": h["ETag"]})[0], 304)
        rules = json.loads(self.call("GET", "/api/rules")[2])
        self.assertEqual((rules["hardRule"], rules["docsSync"]["state"]), (S.HARD_RULE_ID, "off"))
        st = json.loads(self.call("GET", "/api/structure")[2])
        self.assertEqual((st["staleDays"], st["defaultBranch"]), (7, DEFAULT))
        # writes: the same protections as notes
        rs = rules["rules"]
        evil = {"Origin": "http://evil.example"}
        for method, path, body in (("PUT", "/api/rules", {"rules": rs}),
                                   ("PUT", "/api/structure", {"branches": {"claude/a": {"role": "infra"}}}),
                                   ("POST", "/api/pending", {"type": "branch.delete", "params": {"name": "claude/a"}}),
                                   ("POST", "/api/pending/apply-all", {})):
            self.assertEqual(self.call(method, path, body, evil)[0], 403, path)
            self.assertEqual(self.call(method, path, body, {"Origin": None})[0], 403, path)
            self.assertEqual(self.call(method, path, raw=b"x=1",
                                       headers={"Content-Type": "application/x-www-form-urlencoded"})[0], 415, path)
            self.assertEqual(self.call(method, path, raw=b"{nope")[0], 400, path)
            self.assertEqual(self.call(method, path, dict(body, extra=1))[0], 400, path)
        self.assertEqual(self.call("PUT", "/api/rules", raw=b"x" * (256 * 1024 + 1))[0], 413)
        self.assertEqual(self.call("POST", "/api/pending", raw=b"x" * (64 * 1024 + 1))[0], 413)
        self.assertEqual(self.call("POST", "/api/rules", {"rules": rs})[0], 404)
        self.assertEqual(self.call("GET", "/api/pending/c000000000000/apply")[0], 404)
        self.assertEqual(self.call("POST", "/api/pending/../x/apply", {})[0], 404)
        self.assertEqual(self.call("POST", "/api/pending/c000000000000/apply", {})[0], 404)
        self.assertEqual(self.call("PUT", "/api/pending", {})[0], 405)
        code, h, body = self.call("PUT", "/api/structure", {"branches": {"claude/a": {"role": "infra"}}},
                                  {"Origin": "http://localhost:8768"})
        self.assertEqual((code, json.loads(body)["branches"]["claude/a"]["role"]), (200, "infra"))
        rs[1] = dict(rs[1], text="edited")
        self.assertEqual(self.call("PUT", "/api/rules", {"rules": rs, "rev": 1})[0], 200)
        self.assertEqual(self.call("PUT", "/api/rules", {"rules": rs, "rev": 1})[0], 409)
        code, h, body = self.call("PUT", "/api/rules", {"rules": rs[1:]})
        self.assertEqual((code, json.loads(body)["locked"]), (422, True))
        # pending: queue, lock refusal, apply, stale, discard
        code, h, body = self.call("POST", "/api/pending", {"type": "branch.delete", "params": {"name": "testing"}})
        self.assertEqual((code, json.loads(body)["locked"]), (422, True))
        code, h, body = self.call("POST", "/api/pending", {"type": "tag.create", "params": {"name": "v9.9.9"}})
        self.assertEqual(code, 422)
        code, h, body = self.call("POST", "/api/pending", {"type": "branch.delete", "params": {"name": "claude/a"}})
        self.assertEqual(code, 201, body)
        ch = json.loads(body)["change"]
        code, h, body = self.call("POST", "/api/pending/%s/apply" % ch["id"], {"previewHash": ch["previewHash"]})
        self.assertEqual((code, json.loads(body)["change"]["status"]), (200, "applied"))
        self.assertNotIn("claude/a", self.gh.branches)
        code, h, body = self.call("POST", "/api/pending", {"type": "branch.delete", "params": {"name": "claude/b"}})
        ch = json.loads(body)["change"]
        self.gh.branches["claude/b"] = sha_of("moved")
        code, h, body = self.call("POST", "/api/pending/%s/apply" % ch["id"], {})
        self.assertEqual(code, 409)
        self.assertIn(S.STALE_MSG, json.loads(body)["error"])
        code, h, body = self.call("POST", "/api/pending/%s/preview" % ch["id"])
        self.assertEqual((code, json.loads(body)["change"]["status"]), (200, "pending"))
        self.gh.fail_next = (403, "Resource not accessible by integration", [])
        code, h, body = self.call("POST", "/api/pending/%s/apply" % ch["id"])
        self.assertEqual((code, json.loads(body)["error"]), (502, "HTTP 403: Resource not accessible by integration"))
        self.assertEqual(self.call("DELETE", "/api/pending/%s" % ch["id"], {"x": 1})[0], 400)
        self.assertEqual(self.call("DELETE", "/api/pending/%s" % ch["id"])[0], 200)
        self.assertEqual(self.call("DELETE", "/api/pending/%s" % ch["id"])[0], 404)
        pend = json.loads(self.call("GET", "/api/pending")[2])
        self.assertEqual([c["status"] for c in pend["changes"]], ["applied"])
        hist = types(json.loads(self.call("GET", "/api/history")[2]))
        for t in ("structure.changed", "rules.changed", "github.branch.deleted", "github.apply.failed"):
            self.assertIn(t, hist)
        # the served branches take the structure's roles at once
        self.m.state = dict(self.m.state, branches=[{"name": "claude/zz", "date": None, "ahead": 1, "behind": 0}])
        self.call("PUT", "/api/structure", {"branches": {"claude/zz": {"role": "other", "description": "d"}}})
        b = json.loads(self.call("GET", "/api/state")[2])["branches"][0]
        self.assertEqual((b["role"], b["kind"], b["description"], b["roleSet"]), ("other", "other", "d", True))
        # rulesets, read from (the in-memory) GitHub: the production and tagging rules are locked, with why
        code, h, body = self.call("GET", "/api/github/rulesets?fresh=1")
        v = json.loads(body)
        self.assertEqual((code, v["error"], v["defaultBranch"]), (200, None, DEFAULT))
        by = dict((r["name"], r) for r in v["rulesets"])
        self.assertTrue(by["production rule"]["locked"])
        self.assertIn("targets the default branch", " ".join(by["production rule"]["lockReasons"]))
        self.assertTrue(by["tagging rule"]["locked"])
        self.assertIsNone(by["tagging rule"]["form"])
        self.assertFalse(by["testing guard"]["locked"])
        self.assertEqual(by["testing guard"]["form"]["include"], ["testing"])
        self.assertIn("from-testing", v["checks"])
        # the clone of the Claude Documents is never served, even from the output folder
        self.m.static = self.dir
        os.makedirs(os.path.join(self.dir, "claude-docs"), exist_ok=True)
        write(os.path.join(self.dir, "claude-docs", "x.html"), "secret")
        self.assertEqual(self.call("GET", "/claude-docs/x.html")[0], 404)


@unittest.skipUnless(os.environ.get("GHM_LIVE") == "1", "set GHM_LIVE=1 for the read-only check against GitHub")
class LiveRulesets(unittest.TestCase):
    """Read-only: GET /api/github/rulesets against the real floration/quark (needs gh signed in). Never writes."""

    def test_read_only(self):
        d = tempfile.mkdtemp(prefix="ghm-live-")
        try:
            m = S.Manager({"repo": REPO, "port": 0, "quarkDir": d, "claudeDocs": False}, d,
                          os.path.dirname(os.path.abspath(S.__file__)), quiet=True)
            m.gh.write = None   # any write would fail loudly
            v = m.rulesets_view(fresh=True)
            self.assertIsNone(v["error"], v["error"])
            by = dict((r["name"], r) for r in v["rulesets"])
            self.assertTrue(by["production rule"]["locked"], by)
            self.assertTrue(by["tagging rule"]["locked"])
            print("\nlive: default %s; %s" % (v["defaultBranch"], "; ".join(
                "%s (%s): %s%s" % (r["name"], r["target"], ", ".join(r["summary"]),
                                   " [locked: %s]" % "; ".join(r["lockReasons"]) if r["locked"] else "")
                for r in v["rulesets"])))
        finally:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
