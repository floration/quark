#!/usr/bin/env python3
"""Unit tests for server.py (stdlib unittest, no network): python3 tools/github_manager/test_server.py

Fixture data for the parsers and the snapshot -> history diff, plus one offline end-to-end poll: a local git
repository stands in for GitHub's (the mirror's originUrl), canned `gh api` answers stand in for the REST API.
"""
import sys

sys.dont_write_bytecode = True

import json
import os
import shutil
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server as S  # noqa: E402

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
             "pull_requests": [{"number": 8}]}
        s = S.run_summary(r)
        self.assertEqual(s["durationSec"], 600)
        self.assertEqual(s["attempt"], 2)
        self.assertEqual(s["prs"], [8])
        for k in ("id", "workflow", "branch", "sha", "event", "title", "status", "conclusion", "createdAt",
                  "updatedAt", "durationSec", "attempt", "url"):
            self.assertIn(k, s)

    def test_job_info_failed_steps(self):
        j = S.job_info({"id": 1, "name": "windows-tests", "status": "completed", "conclusion": "failure",
                        "steps": [{"name": "build", "conclusion": "success"}, {"name": "quark_tests",
                                                                                "conclusion": "failure"}]})
        self.assertEqual(j["failedSteps"], ["quark_tests"])

    def test_cors(self):
        for o in ("http://127.0.0.1:8768", "http://localhost:8765", "http://127.0.0.1:8766", "http://localhost:8767"):
            self.assertEqual(S.cors_origin(o), o)
        for o in ("http://127.0.0.1:8769", "http://evil.example", "https://127.0.0.1:8768", None, "null"):
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

    def run(self, args, cwd=None, timeout=90, binary=False):
        if args[0] != "gh":
            return S.Proc.run(self, args, cwd=cwd, timeout=timeout, binary=binary)
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
            "html_url": "https://github.com/o/r/actions/runs/%d" % rid, "pull_requests": []}


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
                                                   "openPulls", "running", "lastFailed", "local"]))
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
        finally:
            srv.shutdown()
            srv.server_close()


if __name__ == "__main__":
    unittest.main()
