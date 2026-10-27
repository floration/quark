"""Tests for Quarkology server (python3 -m unittest discover tools/quarkology/tests).

No sibling servers, no network, no SSH: small fake HTTP servers, a fixture Quark folder in a temp dir (with real
git clones made here) and fixture text for the parsers.
"""
import http.client
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PM_ROOT = os.path.dirname(HERE)
sys.path.insert(0, PM_ROOT)

import server  # noqa: E402
from qo import builds, checklist, config, derive, docs, editors, github, history, oracle, repo, scheduler  # noqa: E402
from qo import servers, tabs, util  # noqa: E402

SAMPLE = os.path.join(PM_ROOT, "web", "fixtures", "pm-sample.json")
HAVE_GIT = shutil.which("git") is not None

ORACLE_OUTPUT = """@@uptime
up 3 weeks, 2 days, 4 hours, 11 minutes
@@load
0.08 0.12 0.10 1/180 23456
@@mem
Mem:             960         612          71           2         276         201
@@unit quark
Id=quark.service
ActiveState=active
SubState=running
ActiveEnterTimestamp=Fri 2026-10-02 21:52:10 UTC
@@unit quark-testing
Id=quark-testing.service
ActiveState=failed
SubState=failed
ActiveEnterTimestamp=Sat 2026-10-24 05:46:02 UTC
@@unit quark-discord
Id=quark-discord.service
ActiveState=active
SubState=running
ActiveEnterTimestamp=Wed 2026-10-21 13:20:33 CDT
@@unit quark-update.timer
Id=quark-update.timer
ActiveState=active
SubState=waiting
ActiveEnterTimestamp=
@@unit quark-testing-update.timer
Id=quark-testing-update.timer
ActiveState=active
SubState=waiting
ActiveEnterTimestamp=Thu 2026-10-01 09:00:00 UTC
@@unit quark-discord-update.timer
Id=quark-discord-update.timer
ActiveState=active
SubState=waiting
ActiveEnterTimestamp=Thu 2026-10-01 09:00:00 UTC
@@botupdate
Already up to date: quark-discord 1.18.3
@@serverupdate
Production is 1.17.0, installed 1.17.0: nothing to do
Next check in 10 min
@@end
"""

GITHUB_SUMMARY = {
    "testing": {"sha": "4fe65a7c3b1e9d2a0f6e8c7b5a4d3c2b1a0f9e8d", "version": "1.18.6",
                "prerelease": {"tag": "test-1.18.6-test.231", "publishedAt": "2026-10-24T05:31:09Z"}},
    "production": {"tag": "v1.17.0", "version": "1.17.0", "sha": "b81c0de9a7f6e5d4c3b2a1f0e9d8c7b6a5f4e3d2"},
    "prs": [{"number": 9, "title": "Release 1.18.6", "head": "testing", "base": "main", "checks": "pending"}],
    "runs": {"running": [{"id": 1, "name": "build-and-release", "branch": "testing"}],
             "lastTesting": {"id": 0, "conclusion": "failure", "url": "https://example.invalid/run/0"}},
    "locals": [{"name": "working-repo", "ahead": 3, "behind": 0, "dirty": True, "dirtyFiles": 4,
                "dirtySince": "2020-01-01T00:00:00Z"}],
}
DOCS_SUMMARY = {
    "questions": [{"id": "q-14", "text": "Gantry 5 or 7 min?", "thread": "Tweak gameplay"}],
    "inProgress": [
        {"id": "a", "title": "Map editor", "waitingOn": "Claude", "waitingWhy": "snapping next", "threadUrl": "https://claude.ai/x"},
        {"id": "b", "title": "Tweak gameplay", "thread": "Tweak gameplay", "waitingOn": "push"},
        {"id": "c", "title": "Rank roles", "waitingOn": "owner", "waitingWhy": "role names?"},
        {"id": "d", "title": "Release", "waitingOn": "PR"},
        {"id": "e", "title": "Old thing", "waitingOn": "owner", "status": "done"},
    ],
}
EDITORS_SUMMARY = {"versions": {"testing": "1.18.6", "production": "1.17.0"},
                   "provenance": {"human": 92, "thirdParty": 131, "ai": 319},
                   "programs": [{"id": "map-editor", "name": "Map editor", "ahead_testing": 5, "uncommitted": 2}]}


# ---- fakes ------------------------------------------------------------------------------------------------------

class FakeHTTP:
    """A tiny HTTP server answering path -> (status, body) from a dict (callable values allowed)."""

    def __init__(self, routes):
        self.routes = routes
        self.hits = []
        outer = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.hits.append(self.path)
                r = outer.routes.get(self.path.split("?")[0])
                if callable(r):
                    r = r(self.path)
                code, body = r if r else (404, "nope")
                body = body if isinstance(body, bytes) else (json.dumps(body) if not isinstance(body, str) else body).encode()
                self.send_response(code)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.port = self.httpd.server_address[1]
        self.url = "http://127.0.0.1:%d" % self.port
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def git(cwd, *args):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.invalid", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    subprocess.run(["git"] + list(args), cwd=cwd, check=True, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write(path, text, mtime=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    if mtime is not None:
        os.utime(path, (mtime, mtime))


def make_tree(root, with_git=True):
    """A Quark\\ folder: repository\\ + working-repo\\ clones, game folders with BUILD.txt, tool folders."""
    q = os.path.join(root, "Quark")
    r = os.path.join(q, "repository")
    write(os.path.join(r, "VERSION"), "1.18.6\n")
    for v in ("1.18.4", "1.18.5", "1.18.6", "1.9.0", "1.17.0", "1.18.10"):
        write(os.path.join(r, "changelogs", v + ".md"), "# %s\n\nline\n" % v + "".join("x%d\n" % i for i in range(20)))
    write(os.path.join(r, "changelogs", "full", "1.18.6.md"), "# full\n")
    write(os.path.join(r, "changelogs", "notes.md"), "not a version\n")
    write(os.path.join(r, "docs", "RELEASE_CHECKLIST.md"),
          "# Production release checklist\n\n## 0. Before you start\n\ntext\n\n```\n## not a heading\n```\n"
          "## 1. Install\n\n### Sign-off record\n\n## 16. Sign-off\n")
    if with_git:
        git(r, "init", "-q", "-b", "testing")
        git(r, "add", "-A")
        git(r, "commit", "-q", "-m", "first commit")
        w = os.path.join(q, "working-repo")
        git(q, "clone", "-q", r, w)
        write(os.path.join(w, "new.txt"), "x\n")
        git(w, "add", "-A")
        git(w, "commit", "-q", "-m", "local work")
        write(os.path.join(w, "VERSION"), "1.18.6\nchanged\n", mtime=time.time() - 3 * 86400)
    write(os.path.join(q, "production", "BUILD.txt"), "v1.17.0 (GitHub release, published 10/02/2026 21:10:44)\n")
    write(os.path.join(q, "testing", "BUILD.txt"), "test-1.18.6-test.231 (GitHub release, published 10/24/2026)\n")
    write(os.path.join(q, "working", "BUILD.txt"), "quark-1.18.6-9c2e1f0-dirty (local build, 2026-10-24 18:58)\n")
    write(os.path.join(q, "docs-manager", "docs-manager.cmd"), "@echo off\n")
    write(os.path.join(q, "docs-manager", "zzz.cmd"), "@echo off\n")
    write(os.path.join(q, "github-manager", "start.cmd"), "@echo off\n")
    write(os.path.join(q, "ui-customizer", "ui-customizer.html"), "<p>ui</p>")
    write(os.path.join(q, "sound-editor", "other.html"), "<p>sound</p>")
    write(os.path.join(q, "map-editor", "serve.py"), "")
    write(os.path.join(q, "bot-editor", "run.cmd"), "@echo off\n")
    write(os.path.join(q, "model-editor", "index.html"), "<p>models</p>")
    write(os.path.join(q, "quarkology", "config.json"),
          json.dumps({"oracle": {"host": "203.0.113.1", "user": "ubuntu", "key": os.path.join(root, "no-such.key")}}))
    return q


class Ctx:
    def __init__(self, paths, cfg=None, vers=None):
        self.paths = paths
        self._cfg = cfg or {}
        self._v = vers or {}

    def config(self):
        return self._cfg

    def versions(self):
        return self._v


def env(data):
    return {"ok": True, "fetchedAt": "x", "error": None, "data": data}


# ---- parsers ----------------------------------------------------------------------------------------------------

class TestUtil(unittest.TestCase):
    def test_versions(self):
        self.assertEqual(util.parse_version("test-1.18.6-test.231"), "1.18.6-test.231")
        self.assertEqual(util.parse_version("quark-1.18.6-9c2e1f0-dirty"), "1.18.6")
        self.assertEqual(util.parse_version("v1.17.0"), "1.17.0")
        self.assertEqual(util.parse_version("testing is 1.18.6."), "1.18.6")
        self.assertIsNone(util.parse_version("no version"))
        self.assertEqual(util.base_version("1.18.6-test.231"), "1.18.6")
        self.assertGreater(util.version_key("1.18.10"), util.version_key("1.18.9"))
        self.assertGreater(util.version_key("1.18.6"), util.version_key("1.18.6-test.231"))
        self.assertEqual(util.parse_version("test-1.19.0.testing.341"), "1.19.0.testing.341")
        self.assertEqual(util.parse_version("quark 1.19.0.testing.5 (testing)"), "1.19.0.testing.5")
        self.assertEqual(util.base_version("1.19.0.testing.341"), "1.19.0")
        self.assertGreater(util.version_key("1.19.0.testing.5"), util.version_key("1.19.0"))
        self.assertGreater(util.version_key("1.19.0.testing.10"), util.version_key("1.19.0.testing.9"))
        self.assertGreater(util.version_key("1.19.1"), util.version_key("1.19.0.testing.900"))
        self.assertGreater(util.version_key("1.19.0.testing.1"), util.version_key("1.19.0-test.400"))

    def test_get(self):
        d = {"a": {"b": [1, {"c": 2}]}, "x": None}
        self.assertEqual(util.get(d, "nope", "a.b.1.c"), 2)
        self.assertEqual(util.get(d, "x", default=5), 5)
        self.assertEqual(util.as_list({"n": {"v": 1}}), [{"v": 1, "name": "n"}])

    def test_parse_time(self):
        self.assertEqual(util.parse_time("2026-10-24T00:00:00Z"), util.parse_time("2026-10-23T19:00:00-05:00"))
        self.assertIsNone(util.parse_time("yesterday"))


class TestBuilds(unittest.TestCase):
    def test_formats(self):
        p = builds.parse_build_txt("v1.18.5 (GitHub release, published 10/02/2026 21:10:44)")
        self.assertEqual((p["version"], p["tag"], p["commit"], p["kind"]), ("1.18.5", "v1.18.5", None, "release"))
        p = builds.parse_build_txt("quark-1.18.6-test.231-4fe65a7 (local build, 2026-10-24 18:58)")
        self.assertEqual((p["version"], p["commit"], p["dirty"]), ("1.18.6-test.231", "4fe65a7", False))
        p = builds.parse_build_txt("quark-1.18.6-9c2e1f0-dirty (local build, 2026-10-24 18:58)")
        self.assertEqual((p["version"], p["commit"], p["dirty"]), ("1.18.6", "9c2e1f0", True))
        p = builds.parse_build_txt("testing origin/testing 4fe65a7c3b1e9d2a0f6e8c7b5a4d3c2b1a0f9e8d-test.231 "
                                   "(built on this PC 2026-10-24 05:00; unsigned)")
        self.assertEqual((p["version"], p["commit"], p["suffix"], p["kind"]),
                         (None, "4fe65a7", "-test.231", "local release build"))
        self.assertIsNone(builds.parse_build_txt("v1.18.5 (GitHub release, published 10/02/2026 21:10:44)")["publishedAt"])
        p = builds.parse_build_txt("v1.18.25 (GitHub release, published 2026-10-20T18:00:05Z)")
        self.assertEqual((p["tag"], p["publishedAt"]), ("v1.18.25", "2026-10-20T18:00:05Z"))
        p = builds.parse_build_txt("version 1.18.6\ncommit 9c2e1f0a8b7c (dirty)\n")
        self.assertEqual((p["version"], p["commit"], p["dirty"], len(p["lines"])), ("1.18.6", "9c2e1f0", True, 2))

    def test_fetch(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            write(os.path.join(q, "testing-f3", "BUILD.txt"),
                  "testing-f3 origin/testing 4fe65a7c3b1e9d2a0f6e8c7b5a4d3c2b1a0f9e8d-test.230 (built on this PC)\n")
            write(os.path.join(q, ".src", "testing-f3", "VERSION"), "1.18.5\n")
            d = builds.fetch(Ctx(config.Paths(PM_ROOT, q)))
            f = {x["name"]: x for x in d["folders"]}
            self.assertEqual(f["production"]["version"], "1.17.0")
            self.assertEqual(f["testing"]["tag"], "test-1.18.6-test.231")
            self.assertEqual(f["working"]["commit"], "9c2e1f0")
            self.assertEqual(f["testing-f3"]["version"], "1.18.5-test.230")
            self.assertTrue(f["production"]["mtime"].endswith("Z"))
            shutil.rmtree(os.path.join(q, "working"))
            d = builds.fetch(Ctx(config.Paths(PM_ROOT, q)))
            self.assertEqual([x["note"] for x in d["folders"] if x["name"] == "working"], ["folder missing"])


class TestOracle(unittest.TestCase):
    def test_parse(self):
        d = oracle.parse(ORACLE_OUTPUT)
        self.assertEqual(d["uptime"], "up 3 weeks, 2 days, 4 hours, 11 minutes")
        self.assertEqual(d["load"], [0.08, 0.12, 0.10])
        self.assertEqual((d["memUsedMb"], d["memTotalMb"]), (612, 960))
        self.assertEqual(list(d["units"]), list(oracle.UNITS))
        self.assertEqual(d["units"]["quark-testing"]["active"], "failed")
        self.assertEqual(d["units"]["quark"]["since"], "2026-10-02T21:52:10Z")
        self.assertEqual(d["units"]["quark-discord"]["since"], "Wed 2026-10-21 13:20:33 CDT")  # (kept raw)
        self.assertIsNone(d["units"]["quark-update.timer"]["since"])
        self.assertEqual(d["botUpdate"], ["Already up to date: quark-discord 1.18.3"])
        self.assertEqual(len(d["serverUpdate"]), 2)
        self.assertTrue(d["complete"])

    def test_parse_no_journal_access(self):
        d = oracle.parse(ORACLE_OUTPUT.replace("Already up to date: quark-discord 1.18.3\n", ""))
        self.assertEqual(d["botUpdate"], [])

    def test_command_is_fixed_and_read_only(self):
        args = oracle.build_args("ssh", "203.0.113.1", "ubuntu", "C:\\keys\\k.key")
        cmd = args[-1]
        self.assertEqual(cmd, oracle.REMOTE_COMMAND)
        self.assertTrue(cmd.startswith("nice -n 19 ionice -c3 "))
        self.assertNotIn('"', cmd)
        for word in ("restart", "start ", "stop", "rm ", ">", "tee", "sudo", "kill"):
            self.assertNotIn(word, cmd.replace("2>/dev/null", ""), word)
        for opt in ("BatchMode=yes", "ConnectTimeout=8", "StrictHostKeyChecking=accept-new"):
            self.assertIn(opt, args)
        self.assertEqual(args[-2], "ubuntu@203.0.113.1")

    def test_key_missing_never_runs_ssh(self):
        calls = []
        ctx = Ctx(None, {"oracle": {"host": "203.0.113.1", "user": "ubuntu", "key": "/no/such/key"}})
        with self.assertRaises(util.SourceError) as e:
            oracle.fetch(ctx, runner=lambda *a, **k: calls.append(a))
        self.assertEqual(str(e.exception), "SSH key not found")
        self.assertEqual(calls, [])

    def test_bad_host_rejected(self):
        with tempfile.NamedTemporaryFile(delete=False) as k:
            pass
        try:
            for host, user in (("-oProxyCommand=x", "ubuntu"), ("a b", "ubuntu"), ("h", "-l"), ("", "ubuntu")):
                ctx = Ctx(None, {"oracle": {"host": host, "user": user, "key": k.name}})
                with self.assertRaises(util.SourceError):
                    oracle.fetch(ctx, runner=lambda *a, **kw: self.fail("ran ssh"))
        finally:
            os.unlink(k.name)

    def test_fetch_with_fake_ssh(self):
        with tempfile.NamedTemporaryFile(delete=False) as k:
            pass
        try:
            ctx = Ctx(None, {"oracle": {"host": "203.0.113.1", "user": "ubuntu", "key": k.name}})
            orig = oracle.ssh_exe
            oracle.ssh_exe = lambda: "ssh"
            try:
                seen = {}

                def runner(args, timeout):
                    seen["args"], seen["timeout"] = args, timeout
                    return 0, ORACLE_OUTPUT, ""
                d = oracle.fetch(ctx, runner=runner)
                self.assertEqual(d["host"], "ubuntu@203.0.113.1")
                self.assertEqual(seen["timeout"], 45.0)
                with self.assertRaises(util.SourceError) as e:
                    oracle.fetch(ctx, runner=lambda a, timeout: (255, "", "ubuntu@203.0.113.1: Permission denied (publickey)."))
                self.assertIn("Permission denied", str(e.exception))
            finally:
                oracle.ssh_exe = orig
        finally:
            os.unlink(k.name)


class TestChecklistServers(unittest.TestCase):
    def test_checklist(self):
        hs = checklist.parse("# T\n## 0. A\n```\n## no\n```\n### sub\n## 1. B ##\n")
        self.assertEqual(hs, ["0. A", "1. B"])

    def test_sections(self):
        text = ("# T\n## 0. A\n| ✓ | Check |\n|---|---|\n| ☐ | First **check** |\n| ☐ | Tag | Foundry | runs |\n"
                "```\n- not an item\n```\n### Sub\n- a bullet\n## 1. B\ntext only\n### Sign-off record\n| Build | Date |\n")
        secs = checklist.parse_sections(text)
        self.assertEqual([s["heading"] for s in secs], checklist.parse(text))
        self.assertEqual(secs[0]["items"], [{"text": "First **check**", "group": None},
                                            {"text": "Tag · Foundry · runs", "group": None},
                                            {"text": "a bullet", "group": "Sub"}])
        self.assertEqual(secs[1]["items"], [])

    def test_ticks_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = checklist.Ticks(config.Paths(PM_ROOT, tmp))
            heads = ["0. Before", "4. Settings"]
            for args in (("1.18.6", {"a": 1}, True), ("1.18.6", "4. Settings", "yes"), ("1.18.6", "4. Settings", None),
                         ("1.18.6", "9. Nope", True), ("9.9.9", "4. Settings", True)):
                with self.assertRaises(ValueError, msg=repr(args)):
                    t.set(*args, sections=heads, newest="1.18.6")
            self.assertEqual(t.data, {})
            self.assertIn("Settings", t.set("1.18.5", "Settings", True, sections=heads, newest="1.18.6")["done"])

    def test_ticks(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            t = checklist.Ticks(paths)
            self.assertIsNone(t.view(None))
            self.assertEqual(t.set("1.18.6", "4. Settings", True, "1.18.6-test.231")["done"]["Settings"]["build"], "1.18.6-test.231")
            t.set("1.18.6", "5. Modes", True)
            t.set("1.18.6", "5. Modes", False)
            self.assertEqual(sorted(checklist.Ticks(paths).view("1.18.6")["done"]), ["Settings"])  # (saved)
            for bad in (("1.18", "x"), ("../x", "x"), ("1.18.6", ""), (None, "x")):
                with self.assertRaises(ValueError):
                    t.set(bad[0], bad[1], True)
            for i in range(checklist.KEEP_VERSIONS + 3):
                t.set("1.19.%d" % i, "Settings", True)
            self.assertEqual(len(t.data), checklist.KEEP_VERSIONS)
            self.assertNotIn("1.18.6", t.data)  # (the oldest versions go first)

    def test_servers(self):
        worker = FakeHTTP({"/list": lambda p: (200, "1.2.3.4:27960\n5.6.7.8:27960\n" if "v=1.17.0" in p else
                                               ("9.9.9.9:27970\n" if "v=1.18.6-test.231" in p else "")),
                           "/queue": (200, "duel 1 2\nffa 3 8\n")})
        orig_base, orig_opener = servers.BASE, util._DEFAULT_OPENER
        servers.BASE, util._DEFAULT_OPENER = worker.url, util._LOCAL_OPENER
        try:
            d = servers.fetch(Ctx(None, vers={"production": "1.17.0", "testing": "1.18.6", "testingServer": "1.18.6-test.231"}))
            self.assertEqual(d["production"]["count"], 2)
            self.assertEqual(d["testing"], {"version": "1.18.6-test.231", "count": 1, "addresses": ["9.9.9.9:27970"]})
            self.assertEqual(d["queued"], 4)
            self.assertEqual(d["queue"], "duel 1 2\nffa 3 8")
            with self.assertRaises(util.Retry):
                servers.fetch(Ctx(None, vers={}))
        finally:
            servers.BASE, util._DEFAULT_OPENER = orig_base, orig_opener
            worker.close()


@unittest.skipUnless(HAVE_GIT, "git not installed")
class TestRepo(unittest.TestCase):
    def test_fetch(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp)
            d = repo.fetch(Ctx(config.Paths(PM_ROOT, q)))
            self.assertEqual(d["version"], "1.18.6")
            self.assertTrue(d["fullChangelog"])
            vs = [c["version"] for c in d["changelogs"]]
            self.assertEqual(vs, ["1.18.10", "1.18.6", "1.18.5", "1.18.4", "1.17.0", "1.9.0"])
            self.assertEqual(len(d["changelogs"][0]["lines"]), 12)
            self.assertEqual(d["log"][0]["subject"], "first commit")
            self.assertEqual(len(d["log"][0]["sha"]), 40)
            loc = {x["name"]: x for x in d["locals"]}
            w = loc["working-repo"]
            self.assertEqual((w["ahead"], w["behind"], w["dirty"], w["dirtyFiles"]), (1, 0, True, 1))
            self.assertLess(util.parse_time(w["dirtySince"]), time.time() - 2 * 86400)
            self.assertFalse(loc["repository"]["dirty"])
            # no-optional-locks: reading never rewrote the index
            st = os.stat(os.path.join(q, "working-repo", ".git", "index")).st_mtime
            repo.fetch(Ctx(config.Paths(PM_ROOT, q)))
            self.assertEqual(st, os.stat(os.path.join(q, "working-repo", ".git", "index")).st_mtime)

    @unittest.skipUnless(HAVE_GIT, "git missing")
    def test_changelog_times(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp)
            r = os.path.join(q, "repository")
            repo._ADDED.update(head=None, times={})
            write(os.path.join(r, "changelogs", "1.18.11.md"), "# 1.18.11\n")
            env_ = dict(GIT_COMMITTER_DATE="2026-10-20T12:00:00Z")
            old = {k: os.environ.get(k) for k in env_}
            os.environ.update(env_)
            try:
                git(r, "add", "-A")
                git(r, "commit", "-q", "-m", "1.18.11")
                write(os.path.join(r, "changelogs", "1.18.11.md"), "# 1.18.11\nedited\n")
                os.environ["GIT_COMMITTER_DATE"] = "2026-10-22T12:00:00Z"
                git(r, "commit", "-q", "-am", "edit notes")  # (an edit later does not move the release time)
            finally:
                for k, v in old.items():
                    os.environ.pop(k) if v is None else os.environ.__setitem__(k, v)
            d = repo.fetch(Ctx(config.Paths(PM_ROOT, q)))
            c = {x["version"]: x for x in d["changelogs"]}
            self.assertEqual(c["1.18.11"]["committedAt"], "2026-10-20T12:00:00Z")
            self.assertTrue(c["1.18.6"]["committedAt"].endswith("Z"))
            self.assertEqual(repo._ADDED["head"], d["log"][0]["sha"])  # (asked again only when HEAD moves)

    def test_status_parse(self):
        s = repo.parse_status("## testing...origin/testing [ahead 3, behind 1]\n M a.txt\nR  b -> c\n?? \"d e\"\n")
        self.assertEqual((s["branch"], s["upstream"], s["ahead"], s["behind"]), ("testing", "origin/testing", 3, 1))
        self.assertEqual(s["files"], ["a.txt", "c", "d e"])

    def test_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(util.SourceError):
                repo.fetch(Ctx(config.Paths(PM_ROOT, tmp)))


class TestSiblings(unittest.TestCase):
    def test_github_live_cache_down(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            ctx = Ctx(config.Paths(PM_ROOT, q))
            fake = FakeHTTP({"/api/summary": (200, GITHUB_SUMMARY)})
            orig = github.URL
            try:
                github.URL = fake.url + "/api/summary"
                data, meta = github.fetch(ctx)
                self.assertEqual((data["testing"]["version"], meta["via"]), ("1.18.6", "live"))
                fake.close()
                with self.assertRaises(util.SourceError) as e:
                    github.fetch(ctx)
                self.assertEqual(str(e.exception), "GitHub Manager not running")
                write(os.path.join(q, "github-manager", "cache", "github.json"), json.dumps({"testing": {"version": "1.0.0"}}))
                data, meta = github.fetch(ctx)
                self.assertEqual((data["testing"]["version"], meta["via"], meta["stale"]), ("1.0.0", "cache", True))
            finally:
                github.URL = orig

    def test_docs_editors_down(self):
        orig = docs.URL, editors.URL
        docs.URL = editors.URL = "http://127.0.0.1:9/api/summary"
        try:
            for m, msg in ((docs, "Claude Manager not running"), (editors, "Editor Manager not running")):
                with self.assertRaises(util.SourceError) as e:
                    m.fetch(None)
                self.assertEqual(str(e.exception), msg)
        finally:
            docs.URL, editors.URL = orig


class TestTabs(unittest.TestCase):
    def test_resolve(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            paths = config.Paths(PM_ROOT, q)
            raw = [dict(t) for t in tabs.DEFAULT_TABS["tabs"]] + [
                {"id": "nofolder", "name": "X", "folder": "missing-tool"},
                {"id": "sound2", "name": "S", "folder": "sound-editor"},
                {"id": "docs2", "name": "D", "url": "http://localhost:8799/x", "folder": "docs-manager"},
                {"id": "gh2", "name": "G", "url": "http://127.0.0.1:8798/", "folder": "github-manager", "keepAlive": "yes"},
                {"id": "far away", "name": "F", "url": "https://example.invalid/", "start": "run.cmd", "folder": "bot-editor"},
                {"id": "fileurl", "name": "U", "url": "file:///no/such/page.html"},
                {"id": "pmvar", "name": "P", "folder": "%quark%/ui-customizer", "file": "ui-customizer.html"}]
            t = {r["id"]: r for r in (tabs.resolve_tab(x, paths, i) for i, x in enumerate(raw))}
            self.assertEqual(t["project"]["startCommand"], 'py -3 "%s\\server.py"' % paths.pm_dir)
            self.assertEqual(t["dashboard"]["installed"], False)
            self.assertEqual(t["dashboard"]["note"], "not installed")
            self.assertEqual(t["gh2"]["startCommand"], '"%s"' % os.path.join(q, "github-manager", "start.cmd"))
            self.assertFalse(t["gh2"]["keepAlive"])  # (true only)
            # the managers run their servers directly (no .cmd launcher with `start /min`: no console window)
            self.assertEqual(t["github"]["startCommand"], 'py -3 -B server.py --data "%s\\github-manager"' % q)
            self.assertEqual(t["github"]["startCwd"], os.path.join(q, "github-manager"))
            self.assertEqual(t["docs"]["startCommand"],
                             'py -3 serve.py --docs "%s\\Claude Documents" --port 8766 --quiet' % q)
            self.assertEqual([i for i in t if t[i]["keepAlive"]], ["project", "docs", "github", "tests", "dashboard", "map", "bot", "gamemode", "physics", "weapons", "hud"])
            self.assertEqual([i for i in t if t[i]["lowPriority"]], ["github"])
            self.assertTrue(all(".cmd" not in t[i]["startCommand"] and "start " not in t[i]["startCommand"]
                                for i in ("project", "github", "docs", "dashboard")))
            self.assertEqual(t["docs2"]["startCommand"], '"%s"' % os.path.join(q, "docs-manager", "docs-manager.cmd"))
            self.assertEqual((t["docs2"]["port"], t["docs2"]["kind"]), (8799, "server"))
            self.assertEqual(t["far-away"]["port"], 443)
            self.assertIsNone(t["far-away"]["startCommand"])  # (only local servers are started)
            self.assertFalse(t["far-away"]["local"])
            self.assertEqual((t["fileurl"]["kind"], t["fileurl"]["installed"]), ("file", False))
            self.assertTrue(t["pmvar"]["installed"])
            self.assertTrue(t["ui"]["installed"])
            self.assertTrue(t["ui"]["url"].startswith("file:///") and t["ui"]["url"].endswith("ui-customizer.html"))
            self.assertFalse(t["sound"]["installed"])          # sound-editor.html missing
            self.assertTrue(t["sound2"]["filePath"].endswith("other.html"))  # first *.html
            self.assertTrue(t["model"]["filePath"].endswith("index.html"))
            self.assertEqual(t["bot"]["startCommand"], "py -3 serve.py --port 8769 --no-open")
            self.assertEqual(t["bot"]["startCwd"], os.path.join(q, "bot-editor"))
            self.assertEqual(t["map"]["startCommand"], "py -3 serve.py --port 8770 --no-browser")
            self.assertEqual(t["map"]["startCwd"], os.path.join(q, "map-editor"))
            self.assertFalse(t["nofolder"]["installed"])

    def test_file_and_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            paths = config.Paths(PM_ROOT, q)
            write(paths.pm("tabs.json"), json.dumps({"tabs": [
                {"id": "project", "name": "Project", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768},
                {"id": "docs", "name": "Docs", "url": "http://127.0.0.1:8766/", "port": 8766, "folder": "docs-manager"},
                {"id": "gh", "name": "GH", "url": "http://127.0.0.1:8767/", "port": 8767}]}))
            tb = tabs.Tabs(paths, own_port=8768, prober=lambda p: p == 8766)
            self.assertIsNone(tb.resolved()["tabs"][1]["up"])
            tb.probe_all()
            r = tb.resolved()
            self.assertEqual(r["from"], "tabs.json")
            self.assertEqual([x["up"] for x in r["tabs"]], [True, True, False])
            write(paths.pm("tabs.json"), "{broken")
            self.assertTrue(tb.resolved()["from"].startswith("built-in"))

    def test_tree_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            raw = [{"id": "a", "url": "http://127.0.0.1:1/"}, {"id": "c", "parent": "b"}, {"id": "b", "parent": "a"},
                   {"id": "x", "parent": "nope"}, {"id": "y", "parent": "z"}, {"id": "z", "parent": "y"},
                   {"id": "d", "parent": "a"}]
            out = tabs.tree_order([tabs.resolve_tab(r, paths, i) for i, r in enumerate(raw)])
            self.assertEqual([(t["id"], t["depth"]) for t in out],
                             [("a", 0), ("b", 1), ("c", 2), ("d", 1), ("x", 0), ("y", 0), ("z", 1)])
            self.assertIsNone(out[4]["parent"])

    def test_builtin_mirrors_shell_default(self):
        # (the built-in list is the last fallback; it must stay the same as the shell's tabs.default.json)
        path = os.path.join(PM_ROOT, "shell", "tabs.default.json")
        if not os.path.isfile(path):
            self.skipTest("shell/tabs.default.json missing")
        self.assertEqual(util.read_json_file(path), tabs.DEFAULT_TABS)

    def test_copied_default_keeps_dragged_order(self):
        # the shell's tab drag (and setup.ps1) copy shell\tabs.default.json as it is: its bookkeeping keys must stop
        # MOVE_ONCE from undoing an order q dragged (GitHub Manager before Claude Manager is the older default order)
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            paths = config.Paths(PM_ROOT, q)
            data = util.read_json_file(os.path.join(PM_ROOT, "shell", "tabs.default.json"))
            self.assertEqual({k: v for k, v in data.items() if k != "tabs"}, tabs.DEFAULT_MARKS)
            ids = [t["id"] for t in data["tabs"]]
            gh = data["tabs"].pop(ids.index("github"))
            data["tabs"].insert(ids.index("docs"), gh)
            write(paths.pm("tabs.json"), json.dumps(data))
            self.assertEqual(tabs.upgrade(paths), [])
            got = [t["id"] for t in util.read_json_file(paths.pm("tabs.json"))["tabs"]]
            self.assertLess(got.index("github"), got.index("docs"))

    def test_upgrade(self):
        # q's tabs.json from an older setup: default tabs get the new start commands / keepAlive; q's own edits stay
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            paths = config.Paths(PM_ROOT, q)
            self.assertEqual(tabs.upgrade(paths), [])  # (no tabs.json)
            old = [{"id": "project", "name": "Mine", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
                    "start": "py -3 \"%PM%\\server.py\""},
                   {"id": "github", "name": "GitHub Manager", "url": "http://127.0.0.1:8767/?embed=1", "port": 8767,
                    "folder": "github-manager"},
                   {"id": "docs", "name": "Claude Manager", "url": "http://127.0.0.1:8766/?embed=1", "port": 8766,
                    "folder": "docs-manager", "start": "my.cmd", "keepAlive": False},
                   {"id": "dashboard", "name": "Editor Manager", "url": "http://127.0.0.1:8765/?embed=1", "port": 8765,
                    "folder": "editor-dashboard", "start": "dashboard.cmd"},
                   {"id": "bot", "name": "Bots", "url": "http://127.0.0.1:9999/", "folder": "bot-editor"},
                   {"id": "ui-redesign", "name": "UI Redesign", "parent": "dashboard", "folder": "ui-customizer",
                    "file": "ui-customizer-redesign.html"}]
            write(paths.pm("tabs.json"), json.dumps({"tabs": old}))
            self.assertEqual(tabs.upgrade(paths), ["ui-redesign", "project", "github", "dashboard", "docs", "play", "tests", "gamemode", "physics", "weapons", "hud"])
            # (the old default order, top level here: Claude Manager moved before GitHub Manager, Tests after it)
            self.assertEqual([t["id"] for t in util.read_json_file(paths.pm("tabs.json"))["tabs"]][:6],
                             ["project", "play", "docs", "github", "tests", "dashboard"])
            got = {t["id"]: t for t in util.read_json_file(paths.pm("tabs.json"))["tabs"]}
            d = {t["id"]: t for t in tabs.DEFAULT_TABS["tabs"]}
            self.assertEqual(got["project"]["name"], "Mine")
            self.assertTrue(got["project"]["keepAlive"])
            for k in ("start", "keepAlive", "lowPriority"):
                self.assertEqual(got["github"][k], d["github"][k])
            self.assertEqual((got["docs"]["start"], got["docs"]["keepAlive"]), ("my.cmd", False))  # q's choices
            self.assertEqual(got["dashboard"]["start"], d["dashboard"]["start"])
            self.assertEqual(got["bot"], old[4])
            self.assertNotIn("sound", got)  # (a default tab q removed stays removed)
            self.assertNotIn("ui-redesign", got)  # (retired)
            self.assertEqual(got["tests"], d["tests"])  # (new since: added)
            self.assertEqual(util.read_json_file(paths.pm("tabs.json.old"))["tabs"], old)
            saved = util.read_json_file(paths.pm("tabs.json"))
            self.assertEqual(saved["defaultsSeen"], sorted(d))
            self.assertEqual(saved["defaultsMoved"], ["docs-before-github-2"])
            self.assertEqual(tabs.upgrade(paths), [])  # (idempotent)
            # q removes a tab that was added: it stays removed
            saved["tabs"] = [t for t in saved["tabs"] if t["id"] != "physics"]
            write(paths.pm("tabs.json"), json.dumps(saved))
            self.assertEqual(tabs.upgrade(paths), [])
            # a later default (not in defaultsSeen) is added
            saved["defaultsSeen"].remove("gamemode")
            saved["tabs"] = [t for t in saved["tabs"] if t["id"] != "gamemode"]
            write(paths.pm("tabs.json"), json.dumps(saved))
            self.assertEqual(tabs.upgrade(paths), ["gamemode"])
            # a fresh tabs.json (from the defaults) records every default id
            os.remove(paths.pm("tabs.json"))
            self.assertTrue(tabs.write_default(paths))
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["defaultsSeen"], sorted(d))
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["defaultsMoved"], ["docs-before-github-2"])
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["defaultsUpgraded"], ["map-embed"])
            self.assertEqual(tabs.upgrade(paths), [])

    def test_upgrade_map_embed(self):
        # URL_ONCE: q's saved Map Editor tab (the old default url) gets ?embed=1 once; then it is a default tab again
        with tempfile.TemporaryDirectory() as tmp:
            q = make_tree(tmp, with_git=False)
            paths = config.Paths(PM_ROOT, q)
            d = {t["id"]: t for t in tabs.DEFAULT_TABS["tabs"]}
            self.assertEqual(d["map"]["url"], "http://127.0.0.1:8770/?embed=1")
            old_map = dict(d["map"], url="http://127.0.0.1:8770/")
            old_map.pop("keepAlive")
            mine = {"id": "bot", "name": "Bots", "url": "http://127.0.0.1:8770/", "port": 8771, "folder": "bot-editor"}
            saved = {"tabs": [dict(d["project"]), old_map, mine], "defaultsSeen": sorted(d)}
            write(paths.pm("tabs.json"), json.dumps(saved))
            self.assertEqual(tabs.upgrade(paths), ["map"])
            got = util.read_json_file(paths.pm("tabs.json"))
            m = {t["id"]: t for t in got["tabs"]}
            self.assertEqual(m["map"]["url"], d["map"]["url"])
            self.assertTrue(m["map"]["keepAlive"])  # (a default tab again: missing defaults filled in)
            self.assertEqual(m["bot"], mine)  # (not the map tab: left alone)
            self.assertEqual(got["defaultsUpgraded"], ["map-embed"])
            self.assertEqual(util.read_json_file(paths.pm("tabs.json.old"))["tabs"][1]["url"], "http://127.0.0.1:8770/")
            self.assertEqual(tabs.upgrade(paths), [])
            # q sets the old url back on purpose: it stays (the change ran once)
            got["tabs"][1]["url"] = "http://127.0.0.1:8770/"
            write(paths.pm("tabs.json"), json.dumps(got))
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["tabs"][1]["url"], "http://127.0.0.1:8770/")
            # a map tab on another port or folder (q's own) is not touched
            saved["tabs"][1] = dict(old_map, port=9000)
            write(paths.pm("tabs.json"), json.dumps(saved))
            tabs.upgrade(paths)
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["tabs"][1]["url"], "http://127.0.0.1:8770/")

    def test_shell_supervisor(self):
        # main.js: a keepAlive watchdog, servers started at app start, never `start` / detached on Windows
        with open(os.path.join(PM_ROOT, "shell", "main.js"), encoding="utf-8") as f:
            js = f.read()
        for needle in ("const WATCHDOG_MS = 10000;", "const RESTART_DELAYS_MS = [10000, 30000, 60000, 120000];",
                       "const FAIL_WINDOW_MS = 5 * 60000;", "startKeepAlive();", "windowsHide: true, detached: !IS_WIN",
                       "PRIORITY_LOW"):
            self.assertIn(needle, js)
        self.assertNotRegex(js, r"detached:\s*true")
        if shutil.which("node"):
            r = subprocess.run(["node", "--check", os.path.join(PM_ROOT, "shell", "main.js")], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_write_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            self.assertTrue(tabs.write_default(paths))
            self.assertFalse(tabs.write_default(paths))
            self.assertTrue(config.write_default_config(paths))
            self.assertFalse(config.write_default_config(paths))
            self.assertIn("oracle", util.read_json_file(paths.pm("config.json")))
            self.assertIn("tabs", util.read_json_file(paths.pm("tabs.json")))


# ---- derive + history -------------------------------------------------------------------------------------------

def sample_results():
    with open(SAMPLE, encoding="utf-8") as f:
        s = json.load(f)
    res = s["sources"]
    res["docs"]["data"]["inProgress"] = s["derived"]["inProgress"]
    return res


class TestDerive(unittest.TestCase):
    @unittest.skipUnless(os.path.isfile(SAMPLE), "web fixture missing")
    def test_sample(self):
        d = derive.derive(sample_results(), now=util.parse_time("2026-10-24T19:42:10Z"))
        v = d["versions"]
        self.assertEqual((v["production"], v["testing"], v["testingPrerelease"], v["working"], v["nextFree"]),
                         ("1.17.0", "1.18.6", "test-1.18.6-test.231", "1.18.6", "1.18.7"))
        self.assertEqual(v["testingServer"], "1.18.6-test.231")
        st = {p["stage"]: p for p in d["pipeline"]}
        self.assertEqual((st["working"]["state"], st["testing"]["state"], st["production"]["state"]), ("ahead", "ahead", "behind"))
        self.assertIn("3 commits not on testing", st["working"]["note"])
        self.assertIn("PR #9 open", st["production"]["note"])
        self.assertEqual(d["arrows"][1]["label"], "PR #9 open")
        self.assertEqual(d["humanPct"], 16.9)
        texts = [s["text"] for s in d["nextSteps"]]
        self.assertIn("Hand-test 1.18.6 with RELEASE_CHECKLIST", texts)
        self.assertIn("working-repo has 3 commits not on testing", texts)
        self.assertIn("map-editor-repo has 5 commits not on testing", texts)
        owners = {s["text"]: s["owner"] for s in d["nextSteps"]}
        self.assertEqual(owners["Say push for \"Tweak gameplay\""], "q")
        self.assertTrue(any(o == "claude" for o in owners.values()))
        self.assertTrue(all(s.get("source") for s in d["nextSteps"] + d["attention"]))
        att = [(a["level"], a["text"]) for a in d["attention"]]
        self.assertEqual(att[0], ("red", "quark-testing is failed on the Oracle box"))
        self.assertIn(("amber", "working-repo has uncommitted changes older than a day (4 files)"), att)
        self.assertEqual(len(d["inProgress"]), 8)
        self.assertEqual({e["bucket"] for e in d["inProgress"]}, {"claude", "push", "owner", "pr", "ci", "tests"})
        hand = next(s for s in d["nextSteps"] if s["text"].startswith("Hand-test"))
        self.assertEqual(len(hand["steps"]), 17)
        # halting: the summary's item (newest) then the in-progress entry marked (HALT); its step is not repeated
        self.assertEqual([h["id"] for h in d["halting"]], ["02-in-progress.md:worker-secret", "discord-roles"])
        self.assertEqual(d["haltingCount"], 2)
        self.assertNotIn("http://127.0.0.1:8766/#entry=discord-roles", [s["link"] for s in d["nextSteps"]])

    def test_halting(self):
        docs = {"halting": [
            {"id": "a", "title": "Old", "what": "Do the old thing", "since": "2026-10-24T01:00:00Z"},
            {"id": "b", "title": "New", "blocks": "Thread B", "threadUrl": "https://claude.ai/b", "date": "2026-10-24T09:00:00Z"},
            {"title": "No id", "threadUrl": "https://claude.ai/c"}, "just text", 7],
            "inProgress": [
                {"id": "a", "title": "Dup of a", "status": "halting", "waitingOn": ["owner"]},
                {"id": "p", "title": "Prio", "priority": "(HALT)", "waitingOn": ["owner"], "waitingWhy": "Paste the token",
                 "date": "2026-10-24T05:00:00Z"},
                {"id": "w", "title": "Waiting", "waitingOn": ["owner", "halting"]},
                {"id": "n", "title": "Normal", "waitingOn": ["owner"], "waitingWhy": "role names"}]}
        res = {"docs": env(docs), "oracle": {"ok": False, "error": "SSH key not found", "fetchedAt": "2026-10-24T10:00:00Z",
                                             "lastOkAt": "2026-10-20T10:00:00Z"}}
        d = derive.derive(res)
        h = d["halting"]
        self.assertEqual([x["id"] for x in h if x["source"] == "docs"], ["b", "p", "a", None, None, "w"])
        self.assertEqual(h[0]["link"], derive.entry_link("b"))
        self.assertEqual(h[0]["what"], "New")
        self.assertEqual(h[1]["what"], "Paste the token")
        nid = next(x for x in h if x["title"] == "No id")
        self.assertEqual(nid["link"], "https://claude.ai/c")
        self.assertEqual(h[-1]["source"], "oracle")  # (board items after the docs ones)
        self.assertIn("SSH key", h[-1]["what"])
        self.assertEqual(h[-1]["since"], "2026-10-20T10:00:00Z")
        self.assertEqual(d["haltingCount"], len(h))
        # not shown twice: no Needs-you step for a halting entry, no amber card for the Oracle source
        links = [s["link"] for s in d["nextSteps"]]
        for i in ("a", "p", "w"):
            self.assertNotIn(derive.entry_link(i), links)
        self.assertIn(derive.entry_link("n"), links)
        self.assertFalse([a for a in d["attention"] if a["source"] == "oracle"])
        self.assertTrue(next(e for e in d["inProgress"] if e["id"] == "p")["halting"])
        self.assertFalse(next(e for e in d["inProgress"] if e["id"] == "n")["halting"])
        # an Oracle error q can't fix by hand is attention, not halting
        d2 = derive.derive({"oracle": {"ok": False, "error": "internal error (KeyError)"}})
        self.assertEqual(d2["halting"], [])
        self.assertTrue([a for a in d2["attention"] if a["source"] == "oracle"])
        # the updater: repository\ not fast-forwardable, a failed install; the store's dict stays as it was
        upd = {"enabled": True, "lastRunAt": "2026-10-24T11:00:00Z",
               "repo": {"ffOk": False, "error": "repository\\ has local changes: not updated (2 commits behind)"},
               "entries": [{"id": "model", "name": "Model Editor", "state": "failed", "exitCode": 1,
                            "lastRunAt": "2026-10-24T11:01:00Z"}, {"id": "docs", "state": "ok"}]}
        d3 = derive.with_updates(d, upd)
        self.assertEqual(len(d["halting"]), len(h))
        self.assertEqual([x["source"] for x in d3["halting"][-2:]], ["updates", "updates"])
        self.assertIn("local changes", d3["halting"][-2]["what"])
        self.assertIn("exit 1", d3["halting"][-1]["what"])
        self.assertEqual(d3["haltingCount"], len(h) + 2)
        self.assertIs(derive.with_updates(d, {"enabled": False, "repo": upd["repo"]}), d)
        self.assertIs(derive.with_updates(d, None), d)
        json.dumps(d3)

    def test_minimal_and_down(self):
        res = {k: {"ok": False, "error": "x", "data": None} for k in derive.SOURCE_LABELS}
        res["checklist"]["pending"] = True
        d = derive.derive(res)
        self.assertEqual(d["versions"]["testing"], None)
        self.assertEqual(len(d["attention"]), 7)  # (every source down, the pending one not yet)
        self.assertEqual([p["state"] for p in d["pipeline"]], ["unknown"] * 3)
        json.dumps(d)

    def test_siblings(self):
        res = {"github": env(GITHUB_SUMMARY), "docs": env(DOCS_SUMMARY), "editors": env(EDITORS_SUMMARY),
               "servers": env({"production": {"version": "1.17.0", "count": 0, "addresses": []}}),
               "oracle": env(oracle.parse(ORACLE_OUTPUT))}
        d = derive.derive(res)
        att = [a["text"] for a in d["attention"]]
        self.assertIn("Last CI run on testing failed (failure)", att)
        self.assertIn("No production servers in the public list (1.17.0)", att)
        self.assertEqual(d["humanPct"], 17.0)  # (computed: 92 / 542)
        self.assertEqual(len(d["inProgress"]), 4)  # (done entry left out)
        steps = [(s["owner"], s["text"]) for s in d["nextSteps"]]
        self.assertEqual(steps[0], ("q", "Answer: Gantry 5 or 7 min?"))
        self.assertIn(("q", "role names?"), steps)
        self.assertIn(("claude", "Release"), steps)
        self.assertIn(("claude", "Map editor: snapping next"), steps)
        self.assertEqual(d["programs"][0]["aheadTesting"], 5)
        self.assertEqual(d["runs"]["running"][0]["link"], "http://127.0.0.1:8767/#run=1")

    def test_fallbacks_without_github(self):
        b = {"folders": [
            {"name": "production", "present": True, "version": "1.18.25", "tag": "v1.18.25", "commit": "b81c0de",
             "mtime": "2026-10-20T18:05:00Z", "publishedAt": None},
            {"name": "testing", "present": True, "version": "1.18.30-test.312", "commit": "4fe65a7", "mtime": "x"},
            {"name": "testing-f3", "present": False, "note": "folder missing"}]}
        res = {"builds": env(b), "repo": env({"version": "1.18.30", "log": []}),
               "editors": env({"production": {"version": "1.18.25", "tag": "v1.18.25", "sha": "c0ffee123"}})}
        d = derive.derive(res)
        v = d["versions"]
        self.assertEqual((v["testingPrerelease"], v["testingServer"], v["productionTag"]),
                         ("test-1.18.30-test.312", "1.18.30-test.312", "v1.18.25"))
        prod = next(p for p in d["pipeline"] if p["stage"] == "production")
        self.assertEqual((prod["sha"], prod["time"]), ("c0ffee1", "2026-10-20T18:05:00Z"))
        # a local testing build of another version is not the prerelease
        res["repo"] = env({"version": "1.18.31", "log": []})
        self.assertIsNone(derive.derive(res)["versions"]["testingPrerelease"])
        # production tag without any source of it: v<VERSION>
        self.assertEqual(derive.derive({"builds": env({"folders": [{"name": "production", "version": "1.18.2"}]})})
                         ["versions"]["productionTag"], "v1.18.2")

    def test_disagreements(self):
        gh = {"testing": {"version": "1.18.31", "sha": "aaaaaaa1111"}, "production": {"tag": "v1.18.25", "version": "1.18.25"}}
        ed = {"testing": {"version": "1.18.31", "sha": "aaaaaaa11"}, "production": {"version": "1.18.25"}}
        res = {"github": env(gh), "editors": env(ed),
               "repo": env({"version": "1.18.30", "log": [{"sha": "bbbbbbb2222", "time": "x"}]})}
        dis = derive.disagreements(res, "bbbbbbb2222")
        self.assertEqual([x["key"] for x in dis], ["repo"])
        self.assertIn("testing 1.18.30 (GitHub: 1.18.31)", dis[0]["text"])
        self.assertIn("Update now", dis[0]["fix"])
        # same version, other commit; the Editor Manager ahead of GitHub = the GitHub Manager is stale
        res["repo"] = env({"version": "1.18.31", "log": [{"sha": "bbbbbbb2222", "time": "x"}]})
        res["editors"] = env(dict(ed, testing={"version": "1.18.32", "sha": "ccc"}))
        dis = {x["key"]: x for x in derive.disagreements(res, "bbbbbbb2222")}
        self.assertIn("testing at bbbbbbb (GitHub: aaaaaaa)", dis["repo"]["text"])
        self.assertIn("GitHub Manager", dis["editors"]["fix"])
        # its cache does not count; without GitHub the local ones are compared with each other
        res["github"] = dict(env(gh), stale=True)
        self.assertEqual([x["key"] for x in derive.disagreements(res, "b")], ["local"])
        self.assertEqual(derive.disagreements({"github": env(gh), "editors": env(ed)}, None), [])
        # shown in Needs you only after DISAGREE_GRACE with a memo (the store keeps one)
        res["github"] = env(gh)
        memo, t0 = {}, 1_000_000.0
        pick = lambda d: [a for a in d["attention"] if a.get("kind") == "disagree"]
        self.assertEqual(pick(derive.derive(res, now=t0, memo=memo)), [])
        self.assertEqual(len(pick(derive.derive(res, now=t0 + derive.DISAGREE_GRACE, memo=memo))), 2)
        self.assertEqual(len(pick(derive.derive(res))), 2)  # (no memo: at once)
        res["editors"], res["repo"] = env(ed), env({"version": "1.18.31", "log": [{"sha": "aaaaaaa1111", "time": "x"}]})
        self.assertEqual(pick(derive.derive(res, now=t0 + 2 * derive.DISAGREE_GRACE, memo=memo)), [])
        self.assertEqual(memo["disagreeSince"], {})

    def test_kept_data(self):
        """A source that fails keeps its last good data (scheduler): HALT items, in-progress entries and questions
        stay, marked stale; the "X is down" item itself is not stale; disagreements never use kept data."""
        halts = {"halting": [{"id": "07-q.md:a", "what": "Run `gh auth login`", "file": "07-q.md"}],
                 "questions": [{"id": "07-q.md:b", "text": "5 or 7?"}], "inProgress": DOCS_SUMMARY["inProgress"]}
        down = {"ok": False, "kept": True, "data": halts, "error": "Claude Manager is running but /api/summary failed (HTTP 503)",
                "lastOkAt": "2026-10-25T01:00:00Z"}
        d = derive.derive({"docs": down})
        self.assertEqual(d["kept"], ["docs"])
        self.assertEqual([(h["id"], h.get("stale")) for h in d["halting"]], [("07-q.md:a", True)])
        self.assertEqual(d["halting"][0]["answerLink"], "http://127.0.0.1:8766/#answer=07-q.md:a")
        self.assertEqual(len(d["inProgress"]), 4)
        q = next(s for s in d["nextSteps"] if s["text"] == "Answer: 5 or 7?")
        self.assertEqual((q.get("stale"), q["answerLink"]), (True, "http://127.0.0.1:8766/#answer=07-q.md:b"))
        a = next(a for a in d["attention"] if a["source"] == "docs")
        self.assertNotIn("stale", a)
        self.assertIn("restart the Claude Manager", a["fix"])  # (it answers: running but failing, not "start it")
        self.assertEqual(derive.source_fix("docs", "Claude Manager not running"), "start the Claude Manager")
        # without kept data (or with ok data) nothing is stale
        self.assertEqual(derive.derive({"docs": dict(down, kept=False)})["halting"], [])
        self.assertFalse(any(h.get("stale") for h in derive.derive({"docs": env(halts)})["halting"]))
        # kept GitHub data: the versions stay, no disagreement from it, no links into the GitHub Manager's tab
        res = {"github": {"ok": False, "kept": True, "data": GITHUB_SUMMARY, "error": "GitHub Manager not running"},
               "repo": env({"version": "1.18.5", "log": [], "locals": [{"name": "working-repo", "present": True, "ahead": 2}]})}
        d = derive.derive(res)
        self.assertEqual(d["versions"]["testing"], "1.18.6")
        self.assertEqual(derive.disagreements(res, None), [])
        self.assertTrue(d["localCopies"] and all(x["link"] is None for x in d["localCopies"]))
        step = next(s for s in d["nextSteps"] if s["text"].startswith("working-repo has"))
        self.assertEqual((step["link"], step["goto"]), (None, "local"))
        self.assertEqual(derive.derive(dict(res, github=env(GITHUB_SUMMARY)))["localCopies"][0]["link"],
                         "http://127.0.0.1:8767/#local=working-repo")

    def test_failed_installs_grouped(self):
        ents = [{"id": t, "name": t.title(), "state": "failed", "exitCode": 2, "note": "py -3: not found",
                 "lastRunAt": "2026-10-25T01:0%dZ" % i} for i, t in enumerate(("docs", "map", "model"))]
        h = derive.updates_halting({"enabled": True, "entries": ents + [{"id": "ok", "state": "ok"}]})
        self.assertEqual(len(h), 1)
        self.assertEqual(h[0]["title"], "3 installs failed")
        self.assertIn("all exit 2", h[0]["what"])
        self.assertIn("Docs, Map, Model", h[0]["blocks"])
        self.assertEqual((h[0]["detail"], h[0]["since"]), ("all: py -3: not found", "2026-10-25T01:02Z"))
        ents[1] = dict(ents[1], exitCode=1, note="other")
        h = derive.updates_halting({"enabled": True, "entries": ents})
        self.assertNotIn("all exit", h[0]["what"])
        self.assertIsNone(h[0]["detail"])

    def test_with_checklist(self):
        d = {"nextSteps": [{"text": "Hand-test 1.18.6 with RELEASE_CHECKLIST", "steps": ["0. Before", "1. Install", "2. Menus"]},
                           {"text": "Other"}]}
        ticks = {"version": "1.18.6", "done": {"Install": {"at": "x"}, "Gone section": {"at": "y"}}}
        out = derive.with_checklist(d, ticks)
        self.assertEqual(out["nextSteps"][0]["progress"], {"done": 1, "total": 3})
        self.assertNotIn("progress", d["nextSteps"][0])  # (a copy: the store's dict is shared)
        self.assertEqual(out["checklist"]["count"], 2)
        self.assertIs(derive.with_checklist(d, None), d)

    def test_halting_body_kept(self):
        d = derive.derive({"docs": env({"halting": [{"id": "07:x", "title": "T", "what": "Run gh auth login",
                                                     "body": "Run `gh auth login` on the PC.", "file": "07-questions.md"}]})})
        self.assertEqual((d["halting"][0]["body"], d["halting"][0]["file"]), ("Run `gh auth login` on the PC.", "07-questions.md"))

    def test_buckets(self):
        for w, b in (("owner", "owner"), ("q", "owner"), ("Push", "push"), ("PR #9", "pr"), ("CI", "ci"),
                     ("tests", "tests"), ("Claude", "claude"), ("", "other"), ("lunch", "other")):
            self.assertEqual(derive.bucket(w), b, w)


# the documented /api/summary shapes: tools/docs_manager/README.md and tools/editor_dashboard/README.md
DOCS_REAL = {
    "generatedAt": "2026-10-24T05:20:00Z", "mirrorHead": "abc", "fetchOk": True, "ghOk": True,
    "inProgress": [
        {"id": "02-in-progress.md:map-editor", "title": "Map editor", "thread": "Maps", "threadUrl": "https://claude.ai/m",
         "status": "in-progress", "where": "claude/x", "waitingOn": ["pr", "ci"], "waitingWhy": "PR #8 is open, checks running.",
         "date": "2026-10-23", "lastChanged": {"sha": "s", "time": "t", "what": "edited"}},
        {"id": "02-in-progress.md:feel", "title": "Feel", "thread": "Tweak", "threadUrl": None, "status": "in-progress",
         "where": "", "waitingOn": ["owner", "push"], "waitingWhy": "Waiting on q to push.", "date": "2026-10-23"},
        {"id": "02-in-progress.md:roles", "title": "Rank roles", "status": "open", "waitingOn": ["owner"],
         "waitingWhy": "Waiting on q: role names."},
        {"id": "02-in-progress.md:bare", "title": "Bare", "status": "in-progress"},
    ],
    "waiting": {"owner": ["02-in-progress.md:feel", "02-in-progress.md:roles", "07-questions.md:gantry"],
                "push": ["02-in-progress.md:feel"], "pr": ["02-in-progress.md:map-editor"], "ci": ["02-in-progress.md:map-editor"],
                "tests": [], "claude": ["02-in-progress.md:bare"]},
    "questions": [{"id": "07-questions.md:gantry", "title": "Gantry 5 or 7 min?", "body": "...", "thread": "Tweak",
                   "threadUrl": "https://claude.ai/t", "date": "2026-10-24", "recommendation": "7 min"}],
    "recentChanges": [{"time": "t", "what": "added"}],
}
EDITORS_REAL = {
    "testing": {"version": "1.18.6", "ref": "origin/testing"}, "production": {"version": "1.17.0", "tag": "v1.17.0"},
    "provenance": {"total": 10, "human": 2, "third_party": 3, "ai": 5, "pct_human": 20.0,
                   "by_category": {"sounds": {"total": 6, "human": 1, "third_party": 2, "ai": 3},
                                   "models": {"total": 4, "human": 1, "third_party": 1, "ai": 2}}},
    "pending_marks": 6, "remaining": 5, "generated_at": "2026-10-24T05:00:00Z",
    "programs": [{"id": "map-editor", "name": "Map editor", "ahead_testing": 1, "ahead_production": 2, "uncommitted": 0,
                  "files_changed": 3}],
}


RUN = {"id": 123, "workflow": "build-and-release", "branch": "testing", "sha": "4fe65a7c3b1e", "event": "push",
       "title": "Merge PR #9", "status": "completed", "conclusion": "failure", "createdAt": "2026-10-24T05:00:00Z",
       "updatedAt": "2026-10-24T05:10:00Z", "durationSec": 600, "attempt": 1, "url": "https://github.com/x/actions/runs/123"}
# tools/github_manager/API.md, GET /api/summary
GH_SUMMARY_REAL = {
    "generatedAt": "2026-10-24T05:20:00Z", "repo": "floration/quark",
    "poll": {"lastOk": "2026-10-24T05:20:00Z", "lastError": None, "nextAt": "2026-10-24T05:21:00Z", "intervalSec": 60,
             "rateRemaining": 4800},
    "testing": {"sha": "4fe65a7c3b1e9d2a0f6e8c7b5a4d3c2b1a0f9e8d", "version": "1.18.6",
                "prerelease": {"tag": "test-1.18.6-test.150", "publishedAt": "2026-10-24T05:31:09Z", "url": "u"}},
    "production": {"tag": "v1.17.0", "version": "1.17.0"},
    "openPulls": [{"number": 9, "title": "Release", "head": "testing", "checks": {"state": "pending"}, "url": "u9"},
                  {"number": 8, "title": "Map editor", "head": "claude/x", "checks": {"state": "success"}, "url": "u8"}],
    "running": [dict(RUN, id=124, status="in_progress", conclusion=None)], "lastFailed": RUN,
    "local": [{"name": "working-repo", "branch": "testing", "aheadOfTesting": 2, "behindTesting": 1, "dirty": 3}],
}
# GET /api/state minus `local` (= cache/github.json)
GH_STATE_REAL = {
    "generatedAt": "2026-10-24T05:20:00Z", "repo": "floration/quark", "defaultBranch": "main",
    "poll": {"lastOk": "x", "lastError": "HTTP 502 from api.github.com", "nextAt": "y", "intervalSec": 60},
    "production": {"tag": "v1.17.0", "sha": "b81c0de9a7f6", "version": "1.17.0", "publishedAt": "2026-10-02T21:10:44Z", "url": "u"},
    "testing": {"sha": "4fe65a7c3b1e", "version": "1.18.6", "date": "2026-10-24T04:57:48Z", "subject": "s", "prerelease": None,
                "aheadOfProduction": 42, "commitsSinceProduction": []},
    "branches": [{"name": "testing", "sha": "4fe65a7c3b1e", "kind": "testing", "ahead": 0, "behind": 0, "pr": None,
                  "lastRun": dict(RUN, conclusion="success")}],
    "pulls": [{"number": 9, "title": "Release", "state": "open", "head": "testing", "base": "main",
               "checks": {"state": "failure", "runs": []}},
              {"number": 7, "title": "Old", "state": "closed", "merged": True, "head": "claude/y", "base": "testing",
               "checks": {"state": "success", "runs": []}}],
    "runs": [dict(RUN, id=125, conclusion="success"), RUN],
    "running": [], "releases": [{"tagName": "v1.17.0", "isPrerelease": False}],
}


class TestGitHubManagerShapes(unittest.TestCase):
    def test_summary(self):
        d = derive.derive({"github": env(GH_SUMMARY_REAL)})
        v = d["versions"]
        self.assertEqual((v["testing"], v["production"], v["testingPrerelease"], v["testingServer"]),
                         ("1.18.6", "1.17.0", "test-1.18.6-test.150", "1.18.6-test.150"))
        self.assertEqual([(p["number"], p["checks"], p["link"]) for p in d["prs"]],
                         [(9, "pending", "http://127.0.0.1:8767/#pr=9"), (8, "success", "http://127.0.0.1:8767/#pr=8")])
        self.assertEqual(d["arrows"][1]["label"], "PR #9 open")  # (openPulls has no base: head testing is enough)
        self.assertEqual(d["runs"]["running"][0]["name"], "build-and-release")
        self.assertEqual(d["runs"]["lastFailed"]["id"], 123)
        self.assertIn("Last CI run on testing failed (build-and-release)", [a["text"] for a in d["attention"]])
        w = d["localCopies"][0]
        self.assertEqual((w["ahead"], w["behind"], w["dirty"], w["dirtyFiles"]), (2, 1, True, 3))
        self.assertIn("working-repo has 2 commits not on testing", [s["text"] for s in d["nextSteps"]])
        self.assertEqual(d["github"]["repo"], "floration/quark")
        self.assertEqual(d["github"]["poll"]["rateRemaining"], 4800)

    def test_cache_state(self):
        d = derive.derive({"github": dict(env(GH_STATE_REAL), via="cache", stale=True)})
        self.assertEqual([p["number"] for p in d["prs"]], [9])  # (closed PRs left out)
        self.assertEqual(d["prs"][0]["checks"], "failure")
        self.assertEqual(d["runs"]["lastTesting"]["conclusion"], "success")  # (branches[testing].lastRun)
        att = [a["text"] for a in d["attention"]]
        self.assertNotIn("Last CI run on testing failed (build-and-release)", att)
        self.assertIn("GitHub Manager: last poll failed (HTTP 502 from api.github.com)", att)
        self.assertEqual(d["pipeline"][2]["time"], "2026-10-02T21:10:44Z")

    def test_gzip_and_etag(self):
        import gzip as gz
        body = json.dumps(GH_SUMMARY_REAL).encode()
        seen = []

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                seen.append((self.headers.get("Accept-Encoding"), self.headers.get("If-None-Match")))
                if self.headers.get("If-None-Match") == '"v1-gz"':
                    self.send_response(304)
                    self.send_header("ETag", '"v1-gz"')
                    self.end_headers()
                    return
                z = gz.compress(body)
                self.send_response(200)
                self.send_header("Content-Encoding", "gzip")
                self.send_header("ETag", '"v1-gz"')
                self.send_header("Content-Length", str(len(z)))
                self.end_headers()
                self.wfile.write(z)
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        try:
            c = util.CachedJSON()
            url = "http://127.0.0.1:%d/api/summary" % httpd.server_address[1]
            self.assertEqual(c.get(url)["repo"], "floration/quark")
            self.assertEqual(c.get(url)["repo"], "floration/quark")  # (304: the last body)
            self.assertEqual([n for _, n in seen], [None, '"v1-gz"'])
            self.assertTrue(all("gzip" not in (a or "") for a, _ in seen))  # (gzip is never asked for, only decoded)
        finally:
            httpd.shutdown()
            httpd.server_close()


class TestRealSiblingShapes(unittest.TestCase):
    def test_docs_and_editors(self):
        d = derive.derive({"docs": env(DOCS_REAL), "editors": env(EDITORS_REAL)})
        ip = {e["id"]: e for e in d["inProgress"]}
        self.assertEqual(ip["02-in-progress.md:map-editor"]["bucket"], "pr")
        self.assertEqual(ip["02-in-progress.md:map-editor"]["waitingOn"], "pr, ci")
        self.assertEqual(ip["02-in-progress.md:map-editor"]["waitingOnList"], ["pr", "ci"])
        self.assertEqual(ip["02-in-progress.md:feel"]["bucket"], "push")
        self.assertEqual(ip["02-in-progress.md:roles"]["bucket"], "owner")
        self.assertEqual(ip["02-in-progress.md:bare"]["bucket"], "claude")  # (from waiting{})
        self.assertEqual(ip["02-in-progress.md:feel"]["link"], "http://127.0.0.1:8766/#entry=02-in-progress.md:feel")
        self.assertEqual(ip["02-in-progress.md:map-editor"]["threadUrl"], "https://claude.ai/m")
        steps = [(s["owner"], s["text"], s["link"]) for s in d["nextSteps"]]
        self.assertEqual(steps[0], ("q", "Answer: Gantry 5 or 7 min?", "http://127.0.0.1:8766/#entry=07-questions.md:gantry"))
        self.assertIn(("q", 'Say push for "Tweak"', "http://127.0.0.1:8766/#entry=02-in-progress.md:feel"), steps)
        self.assertIn(("q", "Waiting on q: role names.", "http://127.0.0.1:8766/#entry=02-in-progress.md:roles"), steps)
        self.assertIn("claude", [o for o, _, _ in steps])
        self.assertEqual(d["questions"][0]["recommendation"], "7 min")
        self.assertEqual(d["humanPct"], 20.0)
        self.assertEqual((d["versions"]["testing"], d["versions"]["production"]), ("1.18.6", "1.17.0"))
        a = d["assets"]
        self.assertEqual((a["thirdParty"], a["pendingMarks"], a["remaining"]), (3, 6, 5))
        self.assertEqual(sorted(c["category"] for c in a["byCategory"]), ["models", "sounds"])
        self.assertEqual(next(c for c in a["byCategory"] if c["category"] == "sounds")["thirdParty"], 2)
        self.assertEqual(d["programs"][0]["filesChanged"], 3)
        # pct missing: computed from the counts with third_party
        e2 = json.loads(json.dumps(EDITORS_REAL))
        del e2["provenance"]["pct_human"]
        self.assertEqual(derive.human_pct(e2), 20.0)


class TestHistory(unittest.TestCase):
    def results(self, **gh):
        g = json.loads(json.dumps(GITHUB_SUMMARY))
        g.update(gh)
        return {"github": env(g), "docs": env(DOCS_SUMMARY), "editors": env(EDITORS_SUMMARY),
                "oracle": env(oracle.parse(ORACLE_OUTPUT))}

    def test_events_and_persistence(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            h = history.History(paths)
            r1 = self.results()
            self.assertEqual(h.update(r1, derive.derive(r1)), [])  # (first look: nothing to compare)
            # PR 9 closed, PR 10 opened, run 1 finished (failure), testing 1.18.7, a unit failed, an entry done
            r2 = self.results(prs=[{"number": 10, "title": "New", "head": "x", "base": "testing"}],
                              runs={"running": [], "recent": [{"id": 1, "conclusion": "failure", "branch": "testing",
                                                               "name": "build-and-release"}]},
                              testing={"version": "1.18.7", "sha": "aaaaaaa1111"})
            r2["oracle"] = env(oracle.parse(ORACLE_OUTPUT.replace("ActiveState=active\nSubState=running\n"
                                                                  "ActiveEnterTimestamp=Fri", "ActiveState=failed\n"
                                                                  "SubState=failed\nActiveEnterTimestamp=Fri")))
            dd = json.loads(json.dumps(DOCS_SUMMARY))
            dd["inProgress"] = dd["inProgress"][1:] + [{"id": "z", "title": "Brand new", "waitingOn": "CI"}]
            r2["docs"] = env(dd)
            ed = json.loads(json.dumps(EDITORS_SUMMARY))
            ed["provenance"]["human"] = 120
            r2["editors"] = env(ed)
            ev = [e["text"] for e in h.update(r2, derive.derive(r2))]
            self.assertIn("testing is now 1.18.7 (aaaaaaa)", ev)
            self.assertIn("PR #10 opened: New", ev)
            self.assertIn("PR #9 closed: Release 1.18.6", ev)
            self.assertIn("CI failed on testing (build-and-release)", ev)
            self.assertIn("quark: active -> failed", ev)
            self.assertIn("In progress: Brand new (waiting on CI)", ev)
            self.assertIn("Finished: Map editor", ev)
            self.assertTrue(any(e.startswith("Human-made assets 17.0% -> ") for e in ev))
            # the same state again: nothing; a new History from disk: same events, same snapshot
            self.assertEqual(h.update(r2, derive.derive(r2)), [])
            h2 = history.History(paths)
            self.assertEqual(h2.update(r2, derive.derive(r2)), [])
            self.assertEqual(len(h2.latest(1000)), len(ev))
            self.assertEqual(h2.latest(1)[0]["text"], h.events[-1]["text"])  # newest first
            # a source going down produces no "closed" / "finished" events
            down = dict(r2)
            down["github"] = {"ok": False, "error": "x", "data": None}
            down["docs"] = {"ok": False, "error": "x", "data": None}
            self.assertEqual(h2.update(down, derive.derive(down)), [])
            with open(paths.pm("history.jsonl"), encoding="utf-8") as f:
                lines = [json.loads(l) for l in f]
            self.assertEqual(len(lines), len(ev))
            self.assertTrue(all(set(l) <= {"time", "kind", "text", "link"} for l in lines))

    def test_testing_source_switch_is_not_an_event(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = history.History(config.Paths(PM_ROOT, tmp))
            r = self.results()
            h.update(r, derive.derive(r))
            r2 = dict(r)
            r2["github"] = {"ok": False, "error": "x", "data": None}
            r2["repo"] = env({"version": "1.18.5", "log": [{"sha": "bbbbbbbbbb", "time": "x", "subject": "s"}]})
            self.assertEqual(h.update(r2, derive.derive(r2)), [])
            self.assertEqual(h.update(r, derive.derive(r)), [])

    def test_kept_data_is_not_an_event(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = history.History(config.Paths(PM_ROOT, tmp))
            gh = {"testing": {"version": "1.18.6", "sha": "aaaaaaa1"}}
            res = {"github": env(gh), "repo": env({"version": "1.18.5", "log": [{"sha": "bbbbbbb2"}]}),
                   "docs": env({"inProgress": [{"id": "x", "title": "X"}]})}
            h.update(res, derive.derive(res))
            # the GitHub Manager and the Claude Manager go down, keeping their data: no "testing is now", no "Finished"
            res2 = dict(res, github={"ok": False, "kept": True, "data": gh}, docs={"ok": False, "kept": True, "data": res["docs"]["data"]})
            self.assertEqual(h.update(res2, derive.derive(res2)), [])
            res3 = dict(res2, docs=env({"inProgress": []}))
            self.assertEqual([e["text"] for e in h.update(res3, derive.derive(res3))], ["Finished: X"])

    def test_no_duplicate_ci(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = history.History(config.Paths(PM_ROOT, tmp))
            seq = [{"running": [{"id": 5, "branch": "testing"}]}, {"running": []},
                   {"running": [], "recent": [{"id": 5, "conclusion": "success", "branch": "testing"}]}]
            out = []
            for runs in seq:
                r = self.results(runs=runs)
                out += [e["text"] for e in h.update(r, derive.derive(r))]
            self.assertEqual(len([e for e in out if e.startswith("CI ")]), 1, out)


# ---- scheduler --------------------------------------------------------------------------------------------------

class FakeSource:
    def __init__(self, key, fn, interval=1000, min_gap=None):
        self.KEY, self.INTERVAL, self.fetch = key, interval, fn
        if min_gap is not None:
            self.MIN_GAP = min_gap


class TestScheduler(unittest.TestCase):
    def test_isolation_and_retry(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)

            def boom(ctx):
                raise ZeroDivisionError

            def retry(ctx):
                raise util.Retry("later", 1)
            mods = (FakeSource("good", lambda ctx: {"x": 1}), FakeSource("bad", boom),
                    FakeSource("down", lambda ctx: (_ for _ in ()).throw(util.SourceError("nope"))),
                    FakeSource("wait", retry))
            st = scheduler.Store(paths, None, mods)
            quiet, util.log = util.log, lambda msg: None
            try:
                for r in st.runners.values():
                    r.run_once()
            finally:
                util.log = quiet
            res = st.results()
            self.assertEqual(res["good"]["data"], {"x": 1})
            self.assertTrue(res["good"]["ok"])
            self.assertEqual(res["bad"]["error"], "internal error (ZeroDivisionError)")
            self.assertEqual(res["down"]["error"], "nope")
            self.assertTrue(res["wait"]["pending"])
            for _ in range(scheduler.RETRY_LIMIT):
                st.runners["wait"].run_once()
            self.assertNotIn("pending", st.results()["wait"])

    def test_failure_keeps_last_good_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            box = {"fail": False}

            def fetch(ctx):
                if box["fail"]:
                    raise util.SourceError("Claude Manager not running")
                return {"halting": [1]}
            st = scheduler.Store(paths, None, (FakeSource("docs", fetch),))
            r = st.runners["docs"]
            box["fail"] = True
            r.run_once()
            self.assertIsNone(st.results()["docs"]["data"])  # (never fetched: nothing to keep)
            self.assertNotIn("kept", st.results()["docs"])
            box["fail"] = False
            r.run_once()
            box["fail"] = True
            r.run_once()
            r.run_once()  # (kept across several failures)
            env_ = st.results()["docs"]
            self.assertEqual((env_["ok"], env_["kept"], env_["data"]), (False, True, {"halting": [1]}))
            self.assertIsNotNone(env_["lastOkAt"])
            box["fail"] = False
            r.run_once()
            self.assertNotIn("kept", st.results()["docs"])

    def test_rate_limits_and_oracle_persistence(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, tmp)
            calls = []
            mods = (FakeSource("oracle", lambda ctx: calls.append(1) or {"uptime": "up"}, 600, 120),
                    FakeSource("other", lambda ctx: {}))
            st = scheduler.Store(paths, None, mods)
            st.runners["oracle"].run_once()
            self.assertEqual(st.runners["oracle"].request(), (False, 120.0))
            ok, _ = st.refresh_all()
            self.assertTrue(ok)
            self.assertEqual(st.refresh_all()[0], False)
            self.assertLess(st.runners["oracle"].next_at, time.time() + 601)
            self.assertGreater(st.runners["oracle"].next_at, time.time() + 500)  # (refresh_all skips the Oracle box)
            st2 = scheduler.Store(paths, None, mods)  # a restart: the last check is shown, no new SSH yet
            self.assertEqual(st2.results()["oracle"]["data"], {"uptime": "up"})
            self.assertGreater(st2.runners["oracle"].next_at, time.time() + 500)
            self.assertEqual(st2.runners["oracle"].request()[0], False)
            self.assertEqual(len(calls), 1)


# ---- the HTTP server end to end ---------------------------------------------------------------------------------

class TestServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.quark = make_tree(cls.tmp, with_git=HAVE_GIT)
        cls.gh = FakeHTTP({"/api/summary": (200, GITHUB_SUMMARY)})
        cls.docs = FakeHTTP({"/api/summary": (200, DOCS_SUMMARY)})
        cls.worker = FakeHTTP({"/list": (200, "1.2.3.4:27960\n"), "/queue": (200, "duel 1 2\n")})
        cls.saved = (github.URL, docs.URL, editors.URL, servers.BASE, util._DEFAULT_OPENER)
        github.URL, docs.URL = cls.gh.url + "/api/summary", cls.docs.url + "/api/summary"
        editors.URL = "http://127.0.0.1:9/api/summary"  # (down)
        servers.BASE, util._DEFAULT_OPENER = cls.worker.url, util._LOCAL_OPENER
        paths = config.Paths(PM_ROOT, cls.quark)
        cls.app = server.App(paths, port=0, prober=lambda p: False)
        cls.httpd = server.make_server(cls.app, 0)
        cls.port = cls.httpd.server_address[1]
        cls.app.port = cls.app.tabs.own_port = cls.port
        cls.app.start()
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()
        deadline = time.time() + 15
        while time.time() < deadline:
            res = cls.app.store.results()
            if all(not r.get("pending") for k, r in res.items()):
                break
            time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.app.store.shutdown()
        cls.httpd.shutdown()
        cls.httpd.server_close()
        for f in (cls.gh, cls.docs, cls.worker):
            f.close()
        github.URL, docs.URL, editors.URL, servers.BASE, util._DEFAULT_OPENER = cls.saved
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def req(self, path, method="GET", headers=None, body=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            c.request(method, path, body=body, headers=headers or {})
            r = c.getresponse()
            return r.status, dict(r.getheaders()), r.read()
        finally:
            c.close()

    def test_health(self):
        self.assertEqual(self.req("/api/health")[0::2], (200, b"ok"))

    def test_pm_shape(self):
        code, hdr, body = self.req("/api/pm")
        self.assertEqual(code, 200)
        d = json.loads(body)
        self.assertEqual(set(d) >= {"generatedAt", "sources", "derived", "history", "tabs"}, True)
        self.assertEqual(set(d["sources"]), {"github", "docs", "editors", "builds", "repo", "servers", "oracle", "checklist"})
        for k, s in d["sources"].items():
            self.assertTrue({"ok", "fetchedAt", "error", "data"} <= set(s), k)
        self.assertTrue(d["sources"]["github"]["ok"])
        self.assertEqual(d["sources"]["editors"]["error"], "Editor Manager not running")
        self.assertEqual(d["sources"]["oracle"]["error"], "SSH key not found")
        self.assertEqual(d["sources"]["servers"]["data"]["production"]["count"], 1)
        self.assertEqual(d["sources"]["checklist"]["data"]["headings"], ["0. Before you start", "1. Install", "16. Sign-off"])
        if HAVE_GIT:
            self.assertTrue(d["sources"]["repo"]["ok"], d["sources"]["repo"])
        der = d["derived"]
        for k in ("versions", "pipeline", "inProgress", "nextSteps", "attention", "humanPct"):
            self.assertIn(k, der)
        self.assertEqual(der["versions"]["nextFree"], "1.18.7")
        self.assertEqual([h["source"] for h in der["halting"]], ["oracle"])  # (SSH key not found: q must fix it)
        self.assertEqual(der["haltingCount"], 1)
        self.assertFalse([a for a in der["attention"] if a["source"] == "oracle"])
        self.assertIn("no-store", hdr["Cache-Control"])
        self.assertEqual(self.req("/api/pm?refresh=1")[0], 200)

    def test_server_now_and_checklist(self):
        before = time.time() * 1000
        d = json.loads(self.req("/api/pm")[2])
        self.assertLessEqual(before - 1, d["serverNow"])
        self.assertLessEqual(d["serverNow"], time.time() * 1000 + 1)
        js = {"Content-Type": "application/json"}
        body = json.dumps({"version": "1.18.6", "section": "1. Install", "done": True}).encode()
        code, _, out = self.req("/api/checklist", "POST", js, body)
        self.assertEqual((code, json.loads(out)["checklist"]["done"].keys() >= {"Install"}), (200, True))
        der = json.loads(self.req("/api/pm")[2])["derived"]
        self.assertEqual((der["checklist"]["version"], der["checklist"]["count"]), ("1.18.6", 1))
        self.assertEqual(self.req("/api/checklist", "POST", {"Content-Type": "text/plain"}, body)[0], 415)
        self.assertEqual(self.req("/api/checklist", "POST", js, b'{"version": "nope", "section": "x"}')[0], 400)
        self.assertEqual(self.req("/api/checklist", "POST", js, b'[1]')[0], 400)
        for bad in ({"version": "1.18.6", "section": {"a": 1}, "done": True}, {"version": "1.18.6", "section": "1. Install", "done": "yes"},
                    {"version": "1.18.6", "section": "7. Not in the file", "done": True}, {"version": "9.9.9", "section": "1. Install", "done": True}):
            self.assertEqual(self.req("/api/checklist", "POST", js, json.dumps(bad).encode())[0], 400, bad)
        self.assertEqual(self.req("/api/checklist", "POST", dict(js, Origin="https://evil.example"), body)[0], 403)
        self.req("/api/checklist", "POST", js, json.dumps({"version": "1.18.6", "section": "1. Install", "done": False}).encode())

    def test_slim_etag_and_checklist_items(self):
        code, hdr, body = self.req("/api/pm")
        d = json.loads(body)
        self.assertEqual((d["sources"]["docs"]["data"], d["sources"]["docs"]["omitted"]), ({}, True))  # (derived has it)
        self.assertNotIn("sections", d["sources"]["checklist"]["data"])
        self.assertEqual(d["sources"]["checklist"]["data"]["itemCounts"], [0, 0, 0])
        self.assertEqual(json.loads(self.req("/api/pm?raw=1")[2])["sources"]["docs"]["data"]["questions"][0]["id"], "q-14")
        self.assertTrue(hdr["ETag"].startswith('"'))
        self.assertLessEqual(abs(int(hdr["X-Server-Now"]) - d["serverNow"]), 0)
        c, h2, b2 = self.req("/api/pm", headers={"If-None-Match": hdr["ETag"]})
        if c == 200:  # (a source refreshed in between: then it is a new ETag)
            self.assertNotEqual(h2["ETag"], hdr["ETag"])
        else:
            self.assertEqual((c, b2), (304, b""))
            self.assertIn("X-Server-Now", h2)
        self.assertEqual(self.req("/api/pm", headers={"If-None-Match": '"nope"'})[0], 200)
        ck = json.loads(self.req("/api/checklist")[2])
        self.assertEqual([s["heading"] for s in ck["sections"]], ["0. Before you start", "1. Install", "16. Sign-off"])
        self.assertEqual(ck["version"], "1.18.6")

    def test_install_log_and_retry_one(self):
        logs = self.app.paths.pm("logs")
        os.makedirs(logs, exist_ok=True)
        with open(os.path.join(logs, "install-docs.log"), "w") as f:
            f.write("x" * (server.LOG_MAX + 10) + "\nlast line\n")
        code, h, body = self.req("/api/updates/log?id=docs")
        self.assertEqual(code, 200)
        self.assertTrue(body.endswith(b"last line\n") and body.startswith(b"[... the first"))
        for bad in ("../x", "", "a/b", "nope"):
            self.assertEqual(self.req("/api/updates/log?id=" + bad)[0], 404, bad)
        if not self.app.can_retry_one():
            self.assertEqual(self.req("/api/updates/run?id=docs", "POST")[0], 501)

    def test_tabs_history(self):
        d = json.loads(self.req("/api/tabs")[2])
        ids = [t["id"] for t in d["tabs"]]
        self.assertEqual(ids[:6], ["project", "play", "docs", "github", "tests", "dashboard"])
        self.assertEqual([x["depth"] for x in d["tabs"]][:7], [0, 1, 1, 1, 2, 1, 2])
        self.assertTrue(all("installed" in t and "up" in t for t in d["tabs"]))
        self.assertEqual(json.loads(self.req("/api/history?limit=5")[2]), {"events": []})

    def test_cors_and_host(self):
        _, h, _ = self.req("/api/pm", headers={"Origin": "http://localhost:8765"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://localhost:8765")
        _, h, _ = self.req("/api/pm", headers={"Origin": "http://127.0.0.1:8770"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8770")
        _, h, _ = self.req("/api/pm", headers={"Origin": "https://evil.example"})
        self.assertNotIn("Access-Control-Allow-Origin", h)
        self.assertEqual(self.req("/api/pm", headers={"Host": "evil.example"})[0], 403)
        self.assertEqual(self.req("/api/refresh?source=oracle", "POST", {"Origin": "https://evil.example"})[0], 403)
        self.assertEqual(self.req("/api/refresh", "OPTIONS", {"Origin": "http://127.0.0.1:8767"})[0], 204)

    def test_refresh(self):
        code, _, body = self.req("/api/refresh?source=oracle", "POST")
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(body)["accepted"], False)  # (checked at startup < 120 s ago)
        self.assertEqual(self.req("/api/refresh?source=nope", "POST")[0], 400)

    def test_static(self):
        code, h, body = self.req("/")
        self.assertEqual(code, 200)
        self.assertIn("text/html", h["Content-Type"])
        for bad in ("/static/../server.py", "/static/..%2fserver.py", "/static/%2e%2e/server.py", "/static/C:/x"):
            self.assertEqual(self.req(bad)[0], 404, bad)
        if os.path.isfile(SAMPLE):
            code, h, _ = self.req("/static/fixtures/pm-sample.json")
            self.assertEqual((code, h["Content-Type"].split(";")[0]), (200, "application/json"))


if __name__ == "__main__":
    unittest.main()
