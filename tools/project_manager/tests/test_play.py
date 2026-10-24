"""Tests for the Play tab (pm/play.py and its endpoints): python3 -m unittest discover tools/project_manager/tests

No network and no real game: a fake GitHub (a local HTTP server with a fixture release list, zip and SHA256SUMS),
a tiny script standing in for quark.exe, and a Claude Documents clone of a bare repo made here.
"""
import hashlib
import http.client
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PM_ROOT = os.path.dirname(HERE)
sys.path.insert(0, PM_ROOT)
sys.path.insert(0, HERE)

import server  # noqa: E402
from pm import config, play, tabs, util  # noqa: E402
from test_pm import FakeHTTP, write  # noqa: E402

HAVE_GIT = shutil.which("git") is not None
DOCSPARSE = os.path.join(os.path.dirname(PM_ROOT), "docs_manager")


def git(cwd, *args):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.invalid", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    return subprocess.run(["git"] + list(args), cwd=cwd, check=True, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL).stdout.decode()


def wait_for(fn, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.05)
    return fn()


def fake_game(folder, seconds=0.3, code=3):
    """quark.exe that sleeps and exits with `code` (a shell script: POSIX only)."""
    p = os.path.join(folder, "quark.exe")
    write(p, "#!/bin/sh\necho started \"$@\"\nsleep %s\nexit %d\n" % (seconds, code))
    os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return p


def make_zip(files, top="quark-windows"):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(top + "/", "")
        for name, data in files.items():
            z.writestr(top + "/" + name, data)
    return buf.getvalue()


def releases_fixture(base, sha_line=True):
    return [
        {"tag_name": "test-1.18.14-test.240", "name": "Testing 1.18.14", "prerelease": True, "draft": False,
         "published_at": "2026-10-24T05:00:00Z", "html_url": "https://github.com/floration/quark/releases/tag/x",
         "assets": [{"name": "quark-windows.zip", "size": 1234, "browser_download_url": base + "/dl/test.zip"},
                    {"name": "SHA256SUMS", "size": 99, "browser_download_url": base + "/dl/SHA256SUMS"}]},
        {"tag_name": "v1.18.3", "name": "1.18.3", "prerelease": False, "draft": False,
         "published_at": "2026-10-20T05:00:00Z",
         "assets": [{"name": "quark-windows.zip", "size": 1234, "browser_download_url": base + "/dl/v.zip"}]},
        {"tag_name": "v0.9.0", "prerelease": False, "published_at": "2026-01-01T00:00:00Z",
         "assets": [{"name": "quark-linux.tar.gz", "browser_download_url": base + "/dl/l.tgz"}]},
        {"tag_name": "v9.9.9", "draft": True, "published_at": "2027-01-01T00:00:00Z", "assets": []},
        {"tag_name": "../evil", "published_at": "2026-10-23T00:00:00Z", "assets": []},
        {"tag_name": "v1.18.4", "prerelease": False, "published_at": "2026-10-21T00:00:00Z",
         "assets": [{"name": "quark-windows.zip", "browser_download_url": "ftp://example.invalid/x.zip"}]},
    ]


class TestPure(unittest.TestCase):
    def test_parse_releases(self):
        r = play.parse_releases(releases_fixture("http://127.0.0.1:1"))
        self.assertEqual([x["tag"] for x in r], ["test-1.18.14-test.240", "v1.18.4", "v1.18.3", "v0.9.0"])
        self.assertEqual([x["channel"] for x in r], ["testing", "production", "production", "production"])
        self.assertEqual(r[0]["version"], "1.18.14-test.240")
        self.assertEqual(r[0]["zip"]["size"], 1234)
        self.assertTrue(r[0]["sums"].endswith("/dl/SHA256SUMS"))
        self.assertIsNone(r[1]["zip"])  # (not https: refused)
        self.assertIsNone(r[3]["zip"])  # (no Windows zip: shown greyed)
        self.assertIsNone(r[2]["sums"])
        self.assertEqual(play.parse_releases({"message": "rate limit"}), [])

    def test_parse_sums(self):
        h = "a" * 64
        text = "%s  quark-linux.tar.gz\n%s *quark-windows.zip\n" % ("b" * 64, h)
        self.assertEqual(play.parse_sums(text), h)
        self.assertIsNone(play.parse_sums("nothing here"))

    def test_args_and_exe(self):
        self.assertEqual(play.parse_args(""), [])
        self.assertEqual(play.parse_args("--host ffa --map bastion"), ["--host", "ffa", "--map", "bastion"])
        self.assertEqual(play.parse_args('--name "big map"'), ["--name", "big map"])
        with self.assertRaises(play.PlayError):
            play.parse_args("--a\n--b")
        with self.assertRaises(play.PlayError):
            play.parse_args("x" * 500)
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(play.find_exe(d))
            write(os.path.join(d, "quark_launcher.exe"), "")
            write(os.path.join(d, "quark_server.exe"), "")
            self.assertIsNone(play.find_exe(d))
            write(os.path.join(d, "quark-old.exe"), "")
            self.assertEqual(os.path.basename(play.find_exe(d)), "quark-old.exe")
            write(os.path.join(d, "quark.exe"), "")
            self.assertEqual(os.path.basename(play.find_exe(d)), "quark.exe")

    def test_time(self):
        import datetime
        summer = datetime.datetime(2026, 7, 1, 17, 0, tzinfo=datetime.timezone.utc).timestamp()
        winter = datetime.datetime(2026, 12, 1, 17, 0, tzinfo=datetime.timezone.utc).timestamp()
        self.assertEqual(play.stamp(summer), "2026-07-01 12:00 CT")
        self.assertEqual(play.stamp(winter), "2026-12-01 11:00 CT")
        u = datetime.datetime(2026, 7, 1, tzinfo=datetime.timezone.utc)
        self.assertEqual(play._central_offset(u), datetime.timedelta(hours=-5))
        self.assertEqual(play._central_offset(u.replace(month=1)), datetime.timedelta(hours=-6))
        self.assertEqual(play.fmt_duration(42), "42 s")
        self.assertEqual(play.fmt_duration(23 * 60 + 5), "23 min")
        self.assertEqual(play.fmt_duration(3900), "1 h 5 min")

    def test_format_note(self):
        s = {"label": "testing", "version": "1.18.14-test.240", "commit": "9c2e1f0abc", "durationSec": 1385,
             "args": "--host ffa `x`", "exitCode": 0}
        line = play.format_note(s, {"did": "two FFA rounds\non bastion", "noticed": "stutter at spawn!",
                                    "change": "", "other": "nice"}, "2026-10-24 14:05 CT")
        self.assertEqual(line, "- **Play note: testing 1.18.14-test.240** Did: two FFA rounds on bastion. Noticed: "
                               "stutter at spawn! Also: nice. Played 23 min, commit 9c2e1f0abc, options "
                               "`--host ffa 'x'`. _2026-10-24 14:05 CT, q, open_")
        self.assertEqual(play.note_title({"label": "v1.18.3", "version": "1.18.3"}), "Play note: v1.18.3")
        crash = play.format_note(dict(s, exitCode=-11, args="", endApprox=True), {"did": "x"}, "2026-10-24 14:05 CT")
        self.assertIn("or less", crash)
        self.assertIn("exit code -11", crash)
        # the Claude Manager's parser reads it as an open entry with the time stamp
        if os.path.isfile(os.path.join(DOCSPARSE, "docsparse.py")):
            sys.path.insert(0, DOCSPARSE)
            import docsparse
            text = play.add_note_lines(None, [line])
            e = docsparse.parse_file(play.DOCS_FILE, text)["entries"]
            self.assertEqual(len(e), 1)
            self.assertEqual((e[0]["title"], e[0]["status"], e[0]["thread"], e[0]["date"], e[0]["section"]),
                             ("Play note: testing 1.18.14-test.240", "open", "q", "2026-10-24", "Notes"))
        self.assertEqual(play.parse_doc(line), [{"title": "Play note: testing 1.18.14-test.240",
                                                 "stamp": "2026-10-24 14:05 CT", "thread": "q", "status": "open"}])
        self.assertEqual(play.parse_doc(line.replace(", open_", ", in-progress: on it_"))[0]["status"], "in-progress")

    def test_add_note_lines(self):
        t = play.add_note_lines(None, ["- **a** x. _2026-10-24 14:05 CT, q, open_"])
        self.assertTrue(t.startswith("# Play notes from q"))
        self.assertTrue(t.endswith("## Notes\n\n- **a** x. _2026-10-24 14:05 CT, q, open_\n"))
        t2 = play.add_note_lines(t, ["- **b** y. _2026-10-24 15:05 CT, q, open_"])
        self.assertLess(t2.index("**b**"), t2.index("**a**"))  # (newest first)
        t3 = play.add_note_lines("# Mine\n\ntext\n", ["- **c**"])
        self.assertEqual(t3, "# Mine\n\ntext\n\n## Notes\n\n- **c**\n")
        t4 = play.add_note_lines("# T\n\n## Notes\n\n## Archive\n\n- **old**\n", ["- **n**"])
        self.assertEqual(t4, "# T\n\n## Notes\n\n- **n**\n\n## Archive\n\n- **old**\n")

    def test_extract_refuses_escape(self):
        with tempfile.TemporaryDirectory() as d:
            zp = os.path.join(d, "x.zip")
            with zipfile.ZipFile(zp, "w") as z:
                z.writestr("quark-windows/../../evil.txt", "x")
            with self.assertRaises(play.PlayError):
                play.extract_zip(zp, os.path.join(d, "out"))
            self.assertFalse(os.path.exists(os.path.join(d, "evil.txt")))

    def test_tabs(self):
        # the Play tab: in both default lists, under the Supervisor Board, on its port, kept up by the project tab
        shell = util.read_json_file(os.path.join(PM_ROOT, "shell", "tabs.default.json"))
        self.assertEqual(shell, tabs.DEFAULT_TABS)
        d = {t["id"]: t for t in tabs.DEFAULT_TABS["tabs"]}
        self.assertEqual((d["play"]["parent"], d["play"]["port"]), ("project", d["project"]["port"]))
        self.assertTrue(d["play"]["url"].startswith("http://127.0.0.1:8768/play"))
        self.assertNotIn("keepAlive", d["play"])
        with open(os.path.join(PM_ROOT, "shell", "main.js"), encoding="utf-8") as f:
            js = f.read()
        for needle in ("'/api/play/pending'", "flashFrame(true)", "const PLAY_FAST_MS = 2000;", "badgeKind",
                       "waiting.get(twin.id)", "u.pathname === p.pathname"):
            self.assertIn(needle, js)


class TestTabsUpgrade(unittest.TestCase):
    """q's tabs.json from before the Play tab: the upgrade puts Play under the Supervisor Board, first among its
    children (where the defaults have it), never at the end after the editors (bug: it showed at the bar's right end)."""

    def order(self, paths):
        r = tabs.Tabs(paths).resolved()["tabs"]  # (tree order = the tab bar's order, as main.js treeOrder)
        return [(t["id"], t["depth"], t["parent"]) for t in r]

    def old_tabs(self):
        return [dict(t) for t in tabs.DEFAULT_TABS["tabs"] if t["id"] != "play"]

    def test_new_play_goes_under_project(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            seen = sorted(t["id"] for t in self.old_tabs())
            write(paths.pm("tabs.json"), json.dumps({"tabs": self.old_tabs(), "defaultsSeen": seen}))
            self.assertEqual(tabs.upgrade(paths), ["play"])
            o = self.order(paths)
            self.assertEqual(o[:3], [("project", 0, None), ("play", 1, "project"), ("github", 1, "project")])
            self.assertEqual([x for x in o if x[1] == 0], [("project", 0, None)])
            self.assertEqual(tabs.upgrade(paths), [])  # (idempotent)

    def test_play_appended_by_an_older_upgrade_is_moved_once(self):
        # what q's PC had: play appended at the end (and an operator's keepAlive, here also without its parent)
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            play_tab = dict(next(t for t in tabs.DEFAULT_TABS["tabs"] if t["id"] == "play"), keepAlive=True)
            play_tab.pop("parent")
            lst = self.old_tabs() + [play_tab]
            write(paths.pm("tabs.json"), json.dumps({"tabs": lst, "defaultsSeen": sorted(t["id"] for t in lst)}))
            self.assertEqual(self.order(paths)[-1], ("play", 0, None))  # (the bug: top level, at the right end)
            self.assertEqual(tabs.upgrade(paths), ["play"])
            o = self.order(paths)
            self.assertEqual(o[1], ("play", 1, "project"))
            saved = util.read_json_file(paths.pm("tabs.json"))
            self.assertTrue(saved["tabs"][1]["keepAlive"])  # (q's / the operator's own setting stays)
            self.assertEqual(saved["defaultsPlaced"], ["play"])
            # q moves it somewhere else afterwards: left alone
            t = saved["tabs"].pop(1)
            saved["tabs"].append(t)
            write(paths.pm("tabs.json"), json.dumps(saved))
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(self.order(paths)[-1][0], "play")


@unittest.skipIf(util.IS_WINDOWS, "the fake quark.exe is a shell script")
class TestPlay(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.quark = os.path.join(self.tmp, "Quark")
        self.paths = config.Paths(PM_ROOT, self.quark)
        write(os.path.join(self.quark, "testing", "BUILD.txt"), "test-1.18.14-test.240 (GitHub release, published x)\n")
        write(os.path.join(self.quark, "production", "BUILD.txt"), "v1.18.3 (GitHub release, published x)\n")
        fake_game(os.path.join(self.quark, "testing"))
        self.play = None

    def tearDown(self):
        if self.play:
            self.play.shutdown()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def new(self, **kw):
        if self.play:
            self.play.shutdown()
        self.play = play.Play(self.paths, **kw)
        return self.play

    def test_builds_launch_and_note_needed(self):
        p = self.new()
        p.start()
        b = {x["id"]: x for x in p.builds()}
        self.assertEqual(list(b)[:4], ["production", "testing", "working", "testing-f3"])
        self.assertTrue(b["testing"]["playable"])
        self.assertEqual(b["testing"]["version"], "1.18.14-test.240")
        self.assertFalse(b["production"]["playable"])
        self.assertEqual(b["production"]["missing"], "no quark.exe in it")
        self.assertEqual(b["working"]["missing"], "folder missing")
        with self.assertRaises(play.PlayError):
            p.launch("production")
        with self.assertRaises(play.PlayError):
            p.launch("../../etc")  # (only a listed build)
        s = p.launch("testing", "--host ffa --map bastion")
        self.assertEqual(s["state"], "running")
        self.assertEqual(p.pending()["running"], 1)
        self.assertEqual(p.builds()[1]["running"], [s["id"]])
        done = wait_for(lambda: p.pending()["pending"])
        self.assertEqual([x["id"] for x in done], [s["id"]])
        got = p._session(s["id"])
        self.assertEqual((got["state"], got["exitCode"]), ("needsNote", 3))
        self.assertGreaterEqual(got["durationSec"], 0.2)
        with open(got["log"], encoding="utf-8") as f:
            self.assertIn("started --host ffa --map bastion", f.read())  # (args reached the game; cwd = its folder)
        saved = util.read_json_file(self.paths.pm("play-sessions.json"))["sessions"]
        self.assertEqual(saved[0]["state"], "needsNote")
        # a restarted server still has it pending
        p2 = self.new()
        self.assertEqual([x["id"] for x in p2.pending()["pending"]], [s["id"]])
        p2.skip(s["id"])
        self.assertEqual(p2.pending()["pending"], [])
        self.assertTrue(p2.skip(s["id"])["noop"])  # (skipped twice: a no-op)

    def test_injected_spawner_and_adoption(self):
        # any command can stand in for the game (cross-platform): it runs, exits, the session wants a note
        def spawner(argv, cwd, log):
            pr = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.2)"], cwd=cwd)
            return play.Proc(pr.pid, popen=pr, created=time.time())
        p = self.new(spawner=spawner)
        s = p.launch("testing")
        self.assertTrue(wait_for(lambda: p._session(s["id"])["state"] == "needsNote"))
        self.assertEqual(p._session(s["id"])["exitCode"], 0)
        # a session left "running" by a server that stopped, whose game is gone: picked up as ended (approx)
        dead = subprocess.Popen([sys.executable, "-c", "pass"])
        dead.wait()
        sess = {"sessions": [{"id": "p20261024120000-abcd", "build": "testing", "label": "testing", "state": "running",
                              "pid": dead.pid, "startedAt": "2026-10-24T17:00:00Z", "exe": "/x/quark.exe"}]}
        util.write_json_atomic(self.paths.pm("play-sessions.json"), sess)
        p = self.new()
        p.start()
        s2 = p._session("p20261024120000-abcd")
        self.assertEqual((s2["state"], s2.get("endApprox")), ("needsNote", True))
        # a live one is watched until it exits
        live = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.4)"])
        sess["sessions"] = [dict(sess["sessions"][0], id="p20261024120001-abcd", pid=live.pid, exe=sys.executable)]
        util.write_json_atomic(self.paths.pm("play-sessions.json"), sess)
        p = self.new()
        p.start()
        self.assertEqual(p._session("p20261024120001-abcd")["state"], "running")
        live.wait()
        self.assertTrue(wait_for(lambda: p._session("p20261024120001-abcd")["state"] == "needsNote", 8))

    def test_releases_install_remove(self):
        zipped = make_zip({"quark.exe": "#!/bin/sh\nexit 0\n", "maps/a.qmap": "version 9\n", "README.md": "hi"})
        sums = "%s  quark-windows.zip\n%s  quark-linux.tar.gz\n" % (hashlib.sha256(zipped).hexdigest(), "0" * 64)
        etag = {"n": 0}

        def rel_route(path):
            etag["n"] += 1
            return (200, releases_fixture(fake.url))
        fake = FakeHTTP({"/releases": rel_route, "/dl/test.zip": (200, zipped), "/dl/v.zip": (200, b"not a zip"),
                         "/dl/SHA256SUMS": (200, sums)})
        try:
            p = self.new(releases_url=fake.url + "/releases")
            r = p.releases()
            self.assertTrue(r["busy"] or r["ok"])
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is True))
            self.assertEqual(p.releases()["list"][0]["tag"], "test-1.18.14-test.240")
            self.assertEqual(etag["n"], 1)  # (cached: no second request within 10 min)
            with self.assertRaises(play.PlayError):
                p.install("v0.9.0")  # (no Windows zip)
            p.install("test-1.18.14-test.240")
            st = wait_for(lambda: p.installs["test-1.18.14-test.240"]["state"] in ("done", "failed") and
                          p.installs["test-1.18.14-test.240"])
            self.assertEqual(st["state"], "done", st)
            self.assertTrue(st["verified"])
            dest = os.path.join(self.quark, "versions", "test-1.18.14-test.240")
            self.assertTrue(os.path.isfile(os.path.join(dest, "quark.exe")))
            self.assertTrue(os.path.isfile(os.path.join(dest, "maps", "a.qmap")))
            with open(os.path.join(dest, "BUILD.txt"), encoding="utf-8") as f:
                self.assertEqual(f.read(), "test-1.18.14-test.240 (GitHub release, published 2026-10-24T05:00:00Z)\n")
            self.assertEqual(sorted(os.listdir(os.path.join(self.quark, "versions"))), ["test-1.18.14-test.240"])
            b = {x["id"]: x for x in p.builds()}["versions/test-1.18.14-test.240"]
            self.assertEqual((b["kind"], b["version"], b["label"]), ("version", "1.18.14-test.240", "test-1.18.14-test.240"))
            v = p.view()
            self.assertTrue(v["releases"]["list"][0]["installed"])
            # a broken zip (no SHA256SUMS for it): fails, leaves nothing behind
            p.install("v1.18.3")
            st = wait_for(lambda: p.installs["v1.18.3"]["state"] in ("done", "failed") and p.installs["v1.18.3"])
            self.assertEqual(st["state"], "failed")
            self.assertFalse(os.path.exists(os.path.join(self.quark, "versions", "v1.18.3")))
            # a checksum mismatch fails too
            fake.routes["/dl/SHA256SUMS"] = (200, "%s  quark-windows.zip\n" % ("f" * 64))
            p.remove("test-1.18.14-test.240")
            self.assertFalse(os.path.exists(dest))
            p.install("test-1.18.14-test.240")
            st = wait_for(lambda: p.installs["test-1.18.14-test.240"]["state"] in ("done", "failed") and
                          p.installs["test-1.18.14-test.240"])
            self.assertEqual(st["state"], "failed")
            self.assertIn("SHA256SUMS", st["error"])
            self.assertEqual(os.listdir(os.path.join(self.quark, "versions")), [])
            for bad in ("..", "../testing", "nope"):
                with self.assertRaises(play.PlayError):
                    p.remove(bad)
            self.assertTrue(os.path.isdir(os.path.join(self.quark, "testing")))
        finally:
            fake.close()

    def test_releases_offline_and_rate_limit(self):
        p = self.new(releases_url="http://127.0.0.1:9/releases")
        p.releases()
        self.assertTrue(wait_for(lambda: p.releases()["ok"] is False))
        self.assertIn("unreachable", p.releases()["error"])
        fake = FakeHTTP({"/releases": (403, {"message": "API rate limit exceeded"})})
        try:
            p = self.new(releases_url=fake.url + "/releases")
            p.releases()
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is False))
            self.assertIn("403", p.releases()["error"])  # (the fake sends no X-RateLimit headers)
        finally:
            fake.close()


@unittest.skipUnless(HAVE_GIT, "git missing")
class TestNotesToDocs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.quark = os.path.join(self.tmp, "Quark")
        self.paths = config.Paths(PM_ROOT, self.quark)
        self.bare = os.path.join(self.tmp, "docs.git")
        git(self.tmp, "init", "-q", "--bare", "-b", "main", self.bare)
        seed = os.path.join(self.tmp, "seed")
        git(self.tmp, "clone", "-q", self.bare, seed)
        write(os.path.join(seed, "README.md"), "# Claude Documents\n")
        git(seed, "add", "-A")
        git(seed, "commit", "-q", "-m", "seed")
        git(seed, "push", "-q", "origin", "HEAD:main")
        self.seed = seed
        self.docs = os.path.join(self.quark, "Claude Documents")
        os.makedirs(self.quark)
        git(self.quark, "clone", "-q", "-b", "main", self.bare, self.docs)
        git(self.docs, "config", "user.name", "q")
        git(self.docs, "config", "user.email", "q@example.invalid")
        self.saved_env = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")}
        os.environ.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        self.play = play.Play(self.paths)

    def tearDown(self):
        self.play.shutdown()
        for k, v in self.saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.tmp, ignore_errors=True)

    def session(self, sid, label="testing", version="1.18.14-test.240"):
        s = {"id": sid, "build": label, "label": label, "version": version, "commit": "9c2e1f0", "state": "needsNote",
             "durationSec": 600, "args": "--host ffa", "startedAt": "2026-10-24T19:00:00Z"}
        self.play.sessions.append(s)
        return s

    def remote_file(self):
        try:
            return git(self.tmp, "--git-dir", self.bare, "show", "main:" + play.DOCS_FILE)
        except subprocess.CalledProcessError:
            return None

    def test_save_commit_push_offline_retry_status(self):
        p = self.play
        self.session("p20261024190000-aaaa")
        with self.assertRaises(play.PlayError):
            p.save_note("p20261024190000-aaaa", {"did": " ", "noticed": ""})  # (empty)
        with self.assertRaises(play.PlayError):
            p.save_note("p20261024190000-zzzz", {"did": "x"})
        n = p.save_note("p20261024190000-aaaa", {"did": "FFA on bastion", "noticed": "spawn stutter"})
        self.assertEqual(p._session("p20261024190000-aaaa")["state"], "noted")
        with self.assertRaises(play.PlayError):
            p.save_note("p20261024190000-aaaa", {"did": "again"})
        self.assertTrue(n["line"].endswith(", q, open_"))
        self.assertEqual(len(p.notes()), 1)  # (the local copy)
        self.assertTrue(p.needs_sync())
        st = p.sync_docs()
        self.assertTrue(st["pushed"], st)
        self.assertFalse(p.needs_sync())
        text = self.remote_file()
        self.assertIn("# Play notes from q", text)
        self.assertIn(n["line"], text)
        log = git(self.docs, "log", "-1", "--format=%s%n%an", "--name-only")
        self.assertIn("Play note: testing 1.18.14-test.240", log)
        self.assertTrue(log.strip().endswith(play.DOCS_FILE))  # (only that file)
        self.assertEqual(p.open_count(), 1)
        # offline: the note is committed on this PC and goes up later
        git(self.docs, "remote", "set-url", "origin", os.path.join(self.tmp, "gone.git"))
        self.session("p20261024190500-bbbb", label="v1.18.3", version="1.18.3")
        p.save_note("p20261024190500-bbbb", {"change": "louder footsteps"})
        st = p.sync_docs()
        self.assertFalse(st["pushed"])
        self.assertEqual(st["unpushed"], 1)
        self.assertIn("Saved on this PC", st["error"])
        self.assertTrue(p.needs_sync())
        v = p.view()
        self.assertEqual(v["notes"][0]["where"], "saved on this PC, will go up later")
        # meanwhile an agent sets the first note to done on GitHub
        git(self.seed, "pull", "-q", "origin", "main")
        path = os.path.join(self.seed, play.DOCS_FILE)
        with open(path, encoding="utf-8") as f:
            t = f.read()
        write(path, t.replace(", q, open_", ", q, done_"))
        git(self.seed, "commit", "-q", "-am", "done")
        git(self.seed, "push", "-q", "origin", "HEAD:main")
        git(self.docs, "remote", "set-url", "origin", self.bare)
        st = p.sync_docs()
        self.assertTrue(st["pushed"], st)
        text = self.remote_file()
        self.assertLess(text.index("Play note: v1.18.3"), text.index("Play note: testing"))  # (newest first)
        v = p.view()
        self.assertEqual([x["status"] for x in v["notes"]], ["open", "done"])
        self.assertEqual(v["notes"][1]["where"], "in Claude Documents")
        self.assertEqual(v["openNotes"], 1)
        # offline again, and on GitHub an agent puts a line right under "## Notes" (where the new note goes): the
        # rebase stops on a conflict, our own play-notes commit is dropped and the note is written again on top
        git(self.docs, "remote", "set-url", "origin", os.path.join(self.tmp, "gone.git"))
        self.session("p20261024191000-cccc", label="working", version="1.18.15")
        p.save_note("p20261024191000-cccc", {"noticed": "menus lag"})
        self.assertEqual(p.sync_docs()["unpushed"], 1)
        git(self.seed, "pull", "-q", "origin", "main")
        with open(path, encoding="utf-8") as f:
            t = f.read()
        write(path, t.replace("## Notes\n", "## Notes\n\n- **Agent line** x. _2026-10-24, some thread, open_\n"))
        git(self.seed, "commit", "-q", "-am", "agent")
        git(self.seed, "push", "-q", "origin", "HEAD:main")
        git(self.docs, "remote", "set-url", "origin", self.bare)
        st = p.sync_docs()
        self.assertTrue(st["pushed"], st)
        text = self.remote_file()
        self.assertLess(text.index("Play note: working 1.18.15"), text.index("Agent line"))
        self.assertEqual(text.count("Play note: working 1.18.15"), 1)
        self.assertEqual(git(self.docs, "status", "--porcelain").strip(), "")
        # an agent removing a note never brings it back
        with open(os.path.join(self.docs, play.DOCS_FILE), encoding="utf-8") as f:
            t = f.read()
        write(os.path.join(self.docs, play.DOCS_FILE), "\n".join(l for l in t.split("\n") if "v1.18.3" not in l))
        git(self.docs, "commit", "-q", "-am", "archive")
        p.sync_docs()
        self.assertNotIn("v1.18.3", self.remote_file())

    def test_no_docs_folder(self):
        shutil.rmtree(self.docs)
        self.session("p20261024190000-aaaa")
        p = self.play
        p.save_note("p20261024190000-aaaa", {"did": "x"})
        st = p.sync_docs()
        self.assertFalse(st["ok"])
        self.assertIn("No Claude Documents folder", st["error"])
        self.assertEqual(len(p.notes()), 1)  # (kept on this PC)


@unittest.skipIf(util.IS_WINDOWS, "the fake quark.exe is a shell script")
class TestPlayServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.quark = os.path.join(cls.tmp, "Quark")
        write(os.path.join(cls.quark, "testing", "BUILD.txt"), "test-1.18.14-test.240 (GitHub release, published x)\n")
        fake_game(os.path.join(cls.quark, "testing"), seconds=0.2, code=0)
        paths = config.Paths(PM_ROOT, cls.quark)
        cls.app = server.App(paths, port=0, modules=[], prober=lambda p: False,
                             play_opts={"releases_url": "http://127.0.0.1:9/releases"})
        cls.httpd = server.make_server(cls.app, 0)
        cls.port = cls.httpd.server_address[1]
        cls.app.port = cls.app.tabs.own_port = cls.port
        cls.app.start()
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.app.store.shutdown()
        cls.app.play.shutdown()
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def req(self, path, method="GET", body=None, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            h = dict(headers or {})
            data = None
            if body is not None:
                data = json.dumps(body).encode()
                h.setdefault("Content-Type", "application/json")
            c.request(method, path, body=data, headers=h)
            r = c.getresponse()
            return r.status, r.read()
        finally:
            c.close()

    def test_endpoints(self):
        code, body = self.req("/play")
        self.assertEqual(code, 200)
        self.assertIn(b"play.js", body)
        for f in ("/static/play.js", "/static/play.css"):
            self.assertEqual(self.req(f)[0], 200)
        code, body = self.req("/api/play/pending")
        self.assertEqual(json.loads(body), {"running": 0, "pending": [], "openNotes": 0})
        code, body = self.req("/api/play")
        v = json.loads(body)
        self.assertEqual([b["id"] for b in v["builds"]][:2], ["production", "testing"])
        self.assertEqual(json.loads(self.req("/api/pm")[1])["play"], {"running": 0, "pending": 0, "openNotes": 0})
        # a JSON body only, from an allowed origin only
        self.assertEqual(self.req("/api/play/launch", "POST", body={"build": "testing"},
                                  headers={"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.req("/api/play/launch", "POST", body={"build": "testing"},
                                  headers={"Origin": "https://evil.example"})[0], 403)
        self.assertEqual(self.req("/api/play/launch", "POST", body={"build": "nope"})[0], 404)
        code, body = self.req("/api/play/launch", "POST", body={"build": "testing", "args": "--host ffa"},
                              headers={"Origin": "http://127.0.0.1:%d" % self.port})
        self.assertEqual(code, 200, body)
        sid = json.loads(body)["session"]["id"]
        pend = wait_for(lambda: json.loads(self.req("/api/play/pending")[1])["pending"])
        self.assertEqual(pend[0]["id"], sid)
        self.assertEqual(self.req("/api/play/note", "POST", body={"session": sid})[0], 400)
        code, body = self.req("/api/play/note", "POST", body={"session": sid, "did": "a quick FFA"})
        self.assertEqual(code, 200, body)
        self.assertIn("12-play-notes.md", json.loads(body)["message"])
        self.assertEqual(json.loads(self.req("/api/play/pending")[1])["pending"], [])
        # a stale form (the note was saved from another page): a clear 409, never a second note
        code, body = self.req("/api/play/note", "POST", body={"session": sid, "did": "again"})
        self.assertEqual((code, json.loads(body)["reason"]), (409, "already-noted"))
        code, body = self.req("/api/play/skip", "POST", body={"session": sid})
        self.assertEqual((code, json.loads(body)["reason"]), (409, "already-noted"))
        self.assertEqual(len(self.app.play.notes()), 1)
        # skipped elsewhere, then Skip again from a stale form: ok, nothing changes
        code, body = self.req("/api/play/launch", "POST", body={"build": "testing"})
        sid2 = json.loads(body)["session"]["id"]
        self.assertTrue(wait_for(lambda: json.loads(self.req("/api/play/pending")[1])["pending"]))
        self.assertEqual(self.req("/api/play/skip", "POST", body={"session": sid2})[0], 200)
        code, body = self.req("/api/play/skip", "POST", body={"session": sid2})
        self.assertEqual((code, json.loads(body)["session"]["noop"]), (200, True))
        code, body = self.req("/api/play/remove", "POST", body={"tag": "../testing"})
        self.assertEqual(code, 400)
        self.assertEqual(self.req("/api/play/sync", "POST", body={})[0], 200)


if __name__ == "__main__":
    unittest.main()
