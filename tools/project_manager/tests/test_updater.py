"""Tests for pm/updater.py (automatic installs and updates): real git repos in a temp dir (a bare origin, a seed clone
that commits, and Quark\\repository cloned from the origin), real commands run through the shell."""
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

HERE = os.path.dirname(os.path.abspath(__file__))
PM_ROOT = os.path.dirname(HERE)
sys.path.insert(0, PM_ROOT)

import server  # noqa: E402
from pm import config, updater, util  # noqa: E402

HAVE_GIT = shutil.which("git") is not None
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}

# The install script the test registry runs: counts its runs, copies a page into %QUARK_DIR%\<tool>\index.html,
# prints its environment and exits with the code in tools/<tool>/exitcode (committed, so a commit can break it).
INSTALL_PY = r'''import os, sys
here = os.path.dirname(os.path.abspath(__file__))
tool = os.path.basename(here)
q = os.environ["QUARK_DIR"]
with open(os.path.join(q, "runs-" + tool + ".txt"), "a") as f:
    f.write("run\n")
print("QUARK_SHELL=%s cwd=%s args=%s nice=%s" % (os.environ.get("QUARK_SHELL"), os.path.basename(os.getcwd()),
      " ".join(sys.argv[1:]), os.nice(0) if hasattr(os, "nice") else "?"))
print("stdin closed:", sys.stdin is None or sys.stdin.read() == "")
code = int(open(os.path.join(here, "exitcode")).read().strip() or 0)
if code == 0:
    os.makedirs(os.path.join(q, tool), exist_ok=True)
    with open(os.path.join(q, tool, "index.html"), "w") as f:
        f.write(open(os.path.join(here, "page.html")).read())
print("exiting with", code)
sys.exit(code)
'''


def git(cwd, *args):
    p = subprocess.run(["git"] + list(args), cwd=cwd, env=dict(os.environ, **GIT_ENV), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, check=True)
    return p.stdout.decode().strip()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def runs(q, tool):
    try:
        return read(os.path.join(q, "runs-" + tool + ".txt")).count("run")
    except FileNotFoundError:
        return 0


PY = '"%s"' % sys.executable if " " in sys.executable else sys.executable


def entry(tool, **kw):
    e = {"id": tool, "name": tool.title(), "sources": ["tools/" + tool],
         "run": "%s tools/%s/install.py" % (PY, tool), "check": tool + "\\index.html"}
    e.update(kw)
    return e


@unittest.skipUnless(HAVE_GIT, "git not installed")
class TestUpdater(unittest.TestCase):
    def setUp(self):
        self._env = {k: os.environ.get(k) for k in GIT_ENV}
        os.environ.update(GIT_ENV)
        self.tmp = tempfile.mkdtemp()
        self.origin = os.path.join(self.tmp, "origin.git")
        self.seed = os.path.join(self.tmp, "seed")
        self.q = os.path.join(self.tmp, "Quark")
        self.repo = os.path.join(self.q, "repository")
        os.makedirs(self.q)
        git(self.tmp, "init", "-q", "--bare", "-b", "testing", self.origin)
        git(self.tmp, "clone", "-q", self.origin, self.seed)
        git(self.seed, "checkout", "-q", "-b", "testing")
        for tool in ("alpha", "beta"):
            write(os.path.join(self.seed, "tools", tool, "install.py"), INSTALL_PY)
            write(os.path.join(self.seed, "tools", tool, "exitcode"), "0\n")
            write(os.path.join(self.seed, "tools", tool, "page.html"), "<p>%s v1</p>\n" % tool)
        write(os.path.join(self.seed, "VERSION"), "1.0.0\n")
        self.set_registry([entry("alpha"), entry("beta", sources=["tools/beta", "VERSION", "assets/not-there"])],
                          commit=False)
        self.commit("first")
        git(self.q, "clone", "-q", "-b", "testing", self.origin, self.repo)
        self.paths = config.Paths(PM_ROOT, self.q)

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- helpers -------------------------------------------------------------------------------------------------
    def commit(self, msg):
        git(self.seed, "add", "-A")
        git(self.seed, "commit", "-q", "-m", msg)
        git(self.seed, "push", "-q", "origin", "testing")
        return git(self.seed, "rev-parse", "HEAD")

    def set_registry(self, entries, commit=True):
        write(os.path.join(self.seed, "tools", "project_manager", "installs.json"), json.dumps({"entries": entries}))
        if commit:
            self.commit("registry")

    def run_pass(self, **kw):
        u = updater.Updater(self.paths, enabled=True, **kw)
        u.run_once()
        v = u.view()
        return u, v, {e["id"]: e for e in v["entries"]}

    # -- the repository ----------------------------------------------------------------------------------------
    def test_fast_forward(self):
        write(os.path.join(self.seed, "tools", "alpha", "page.html"), "<p>alpha v2</p>\n")
        head = self.commit("alpha v2")
        self.assertNotEqual(git(self.repo, "rev-parse", "HEAD"), head)
        _, v, ents = self.run_pass()
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), head)
        self.assertEqual((v["repo"]["head"], v["repo"]["behind"], v["repo"]["ffOk"], v["repo"]["error"]),
                         (head, 0, True, None))
        self.assertEqual(v["repo"]["pulled"], 1)
        self.assertEqual(read(os.path.join(self.q, "alpha", "index.html")), "<p>alpha v2</p>\n")
        self.assertEqual(ents["alpha"]["state"], "ok")

    def test_dirty_or_other_branch_is_left_alone(self):
        old = git(self.repo, "rev-parse", "HEAD")
        write(os.path.join(self.seed, "VERSION"), "1.0.1\n")
        new = self.commit("bump")
        write(os.path.join(self.repo, "VERSION"), "1.0.0 local edit\n")
        _, v, _ = self.run_pass()
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), old)
        self.assertFalse(v["repo"]["ffOk"])
        self.assertEqual(v["repo"]["behind"], 1)
        self.assertIn("local changes", v["repo"]["error"])
        self.assertEqual(read(os.path.join(self.repo, "VERSION")), "1.0.0 local edit\n")  # (never reset)
        # another branch: also left alone
        git(self.repo, "checkout", "-q", "--", "VERSION")
        git(self.repo, "checkout", "-q", "-b", "mine")
        _, v, _ = self.run_pass()
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), old)
        self.assertIn("not testing", v["repo"]["error"])
        # untracked files (a build folder) do not count as changes
        git(self.repo, "checkout", "-q", "testing")
        write(os.path.join(self.repo, "build-model-editor", "x.o"), "obj")
        _, v, _ = self.run_pass()
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), new)
        self.assertTrue(v["repo"]["ffOk"])

    def test_local_commit_refuses_fast_forward(self):
        write(os.path.join(self.seed, "VERSION"), "1.0.1\n")
        self.commit("bump")
        write(os.path.join(self.repo, "mine.txt"), "x")
        git(self.repo, "add", "mine.txt")
        git(self.repo, "commit", "-q", "-m", "local")
        mine = git(self.repo, "rev-parse", "HEAD")
        _, v, _ = self.run_pass()
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), mine)
        self.assertFalse(v["repo"]["ffOk"])
        self.assertIn("fast-forward refused", v["repo"]["error"])

    def test_no_repository(self):
        shutil.rmtree(self.repo)
        _, v, ents = self.run_pass()
        self.assertEqual(v["repo"]["error"], "Quark\\repository\\ not found")
        # (the registry next to server.py is the fallback; nothing runs without the repository)
        self.assertEqual(v["registry"], "installed")
        self.assertTrue(ents)
        el = ents.pop("electron")  # (needs no repository; here there is no electron\ to update)
        self.assertEqual(el["state"], "ok")
        self.assertIn("nothing to update", el["note"])
        for e in ents.values():
            self.assertEqual((e["state"], e["note"]), ("pending", "Quark\\repository\\ not found"))
        self.assertEqual(os.listdir(self.paths.pm("logs")), ["install-electron.log"])

    # -- installs -------------------------------------------------------------------------------------------------
    def test_stamp_change_triggers_run(self):
        u, _, ents = self.run_pass()
        self.assertEqual((runs(self.q, "alpha"), runs(self.q, "beta")), (1, 1))
        self.assertEqual(ents["alpha"]["state"], "ok")
        self.assertEqual(ents["alpha"]["installedStamp"], ents["alpha"]["currentStamp"])
        self.assertEqual(ents["alpha"]["currentStamp"], git(self.repo, "rev-parse", "HEAD:tools/alpha"))
        # (a missing later source counts as "-"; a file source is its blob id)
        self.assertEqual(ents["beta"]["currentStamp"], "%s+%s+-" % (git(self.repo, "rev-parse", "HEAD:tools/beta"),
                                                                     git(self.repo, "rev-parse", "HEAD:VERSION")))
        saved = json.loads(read(self.paths.pm("installs-state.json")))
        self.assertEqual(saved["entries"]["alpha"]["stamp"], ents["alpha"]["currentStamp"])
        self.assertIn("QUARK_SHELL=1 cwd=repository", read(self.paths.pm("logs", "install-alpha.log")))
        self.assertIn("stdin closed: True", read(self.paths.pm("logs", "install-alpha.log")))
        # nothing changed: nothing runs
        self.run_pass()
        self.assertEqual((runs(self.q, "alpha"), runs(self.q, "beta")), (1, 1))
        # VERSION changed: only beta (which lists it) runs again
        write(os.path.join(self.seed, "VERSION"), "1.0.1\n")
        self.commit("bump")
        _, _, ents = self.run_pass()
        self.assertEqual((runs(self.q, "alpha"), runs(self.q, "beta")), (1, 2))
        self.assertEqual(ents["beta"]["state"], "ok")

    def test_failure_keeps_old_stamp(self):
        _, _, ents = self.run_pass()
        good = ents["alpha"]["installedStamp"]
        write(os.path.join(self.seed, "tools", "alpha", "exitcode"), "3\n")
        self.commit("break alpha")
        _, _, ents = self.run_pass()
        a = ents["alpha"]
        self.assertEqual((a["state"], a["exitCode"], a["installedStamp"]), ("failed", 3, good))
        self.assertNotEqual(a["currentStamp"], good)
        self.assertIn("exiting with 3", "\n".join(a["logTail"]))
        saved = json.loads(read(self.paths.pm("installs-state.json")))["entries"]["alpha"]
        self.assertEqual((saved["stamp"], saved["exitCode"]), (good, 3))
        # retried on the next pass (app open / Update now) and fixed by the next commit
        self.run_pass()
        self.assertEqual(runs(self.q, "alpha"), 3)
        write(os.path.join(self.seed, "tools", "alpha", "exitcode"), "0\n")
        self.commit("fix alpha")
        _, _, ents = self.run_pass()
        self.assertEqual(ents["alpha"]["state"], "ok")
        self.assertEqual(ents["alpha"]["installedStamp"], ents["alpha"]["currentStamp"])

    def test_missing_check_triggers_run(self):
        self.run_pass()
        os.remove(os.path.join(self.q, "alpha", "index.html"))
        _, _, ents = self.run_pass()
        self.assertEqual((runs(self.q, "alpha"), runs(self.q, "beta")), (2, 1))
        self.assertTrue(os.path.isfile(os.path.join(self.q, "alpha", "index.html")))

    def test_waiting_disabled_and_timeout(self):
        self.set_registry([
            entry("alpha", enabled=False),
            {"id": "later", "name": "Not merged yet", "sources": ["tools/later", "VERSION"], "run": "exit 1"},
            entry("beta", run="%s -c \"import time; time.sleep(30)\"" % PY, timeoutSec=1),
            {"id": "bad id!", "sources": ["x"], "run": "exit 0"}, {"id": "norun", "sources": ["x"]}])
        t0 = time.time()
        _, v, ents = self.run_pass()
        self.assertLess(time.time() - t0, 20)
        self.assertEqual(list(ents), ["alpha", "later", "beta"])
        self.assertEqual(ents["alpha"]["state"], "disabled")
        self.assertEqual(ents["later"]["state"], "waiting")
        self.assertIn("tools/later is not in repository", ents["later"]["note"])
        self.assertIsNone(ents["later"]["exitCode"])
        self.assertEqual((ents["beta"]["state"], ents["beta"]["exitCode"]), ("failed", -2))
        self.assertIn("timed out after 1 s", ents["beta"]["note"])
        self.assertEqual(runs(self.q, "alpha"), 0)
        self.assertEqual(v["registry"], "repository")

    def test_restart_requests_and_rate_limit(self):
        self.set_registry([entry("alpha", restartsPort=[8769, 8770]), entry("beta", restartsPort=8766)])
        u, v, _ = self.run_pass()
        rs = v["restart"]
        self.assertEqual(sorted(r["port"] for r in rs), [8766, 8769, 8770])
        self.assertEqual({r["folder"] for r in rs}, {"alpha", "beta"})
        self.assertTrue(u.ack(rs[0]["id"]))
        self.assertFalse(u.ack(rs[0]["id"]))
        self.assertEqual(len(u.view()["restart"]), 2)
        self.assertEqual(len(u.view(now=time.time() + updater.RESTART_TTL + 1)["restart"]), 0)
        # nothing installed: no restart request
        u2, v2, _ = self.run_pass()
        self.assertEqual(v2["restart"], [])
        # at most once per MIN_GAP
        self.assertEqual(u2.request(), (True, 0.0))
        self.assertEqual(u2.request()[0], False)  # (pending)
        u2.pending = False
        ok, wait = u2.request()
        self.assertFalse(ok)
        self.assertGreater(wait, 50)
        self.assertEqual(updater.Updater(self.paths, enabled=False).request(), (False, None))

    @unittest.skipUnless(hasattr(os, "nice"), "no os.nice")
    def test_low_priority(self):
        self.set_registry([entry("alpha", lowPriority=True), entry("beta")])
        self.run_pass()
        base = os.nice(0)
        a = read(self.paths.pm("logs", "install-alpha.log"))
        b = read(self.paths.pm("logs", "install-beta.log"))
        self.assertIn("nice=%d" % min(19, base + 10), a)
        self.assertIn("nice=%d" % base, b)

    # -- self-update ----------------------------------------------------------------------------------------------
    def test_self_update(self):
        src = os.path.join(self.seed, "tools", "project_manager")
        for d in ("pm", "web", "shell"):
            shutil.copytree(os.path.join(PM_ROOT, d), os.path.join(src, d),
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        for f in ("server.py", "project-manager.cmd", "project-manager-hidden.vbs"):
            shutil.copy2(os.path.join(PM_ROOT, f), os.path.join(src, f))
        self.set_registry([{"id": "project-manager", "name": "Supervisor Board", "sources": ["tools/project_manager"],
                            "run": "builtin:self-update", "check": "project-manager\\server.py"}])
        pm = self.paths.pm_dir
        # q's files, and an older install with a stale module and its __pycache__
        mine = {"tabs": [{"id": "mine", "name": "Mine", "folder": "mine-tool"}], "defaultsSeen": ["project"]}
        write(os.path.join(pm, "tabs.json"), json.dumps(mine))
        write(os.path.join(pm, "config.json"), '{"oracle": {"host": "203.0.113.9"}}')
        write(os.path.join(pm, "history.jsonl"), '{"kind": "x"}\n')
        write(os.path.join(pm, "state.json"), '{"lastTab": "mine"}')
        write(os.path.join(pm, "server.py"), "# old\n")
        write(os.path.join(pm, "pm", "gone.py"), "# removed from the repo\n")
        write(os.path.join(pm, "pm", "__pycache__", "x.pyc"), "")
        u, v, ents = self.run_pass()
        e = ents["project-manager"]
        self.assertEqual(e["state"], "ok", e["logTail"])
        self.assertTrue(v["selfUpdated"])
        self.assertEqual(read(os.path.join(pm, "server.py")), read(os.path.join(PM_ROOT, "server.py")))
        self.assertEqual(read(os.path.join(pm, "shell", "main.js")), read(os.path.join(PM_ROOT, "shell", "main.js")))
        self.assertTrue(os.path.isfile(os.path.join(pm, "installs.json")))
        self.assertFalse(os.path.exists(os.path.join(pm, "pm", "gone.py")))
        self.assertTrue(os.path.exists(os.path.join(pm, "pm", "__pycache__", "x.pyc")))
        self.assertEqual(read(os.path.join(pm, "config.json")), '{"oracle": {"host": "203.0.113.9"}}')
        self.assertEqual(read(os.path.join(pm, "history.jsonl")), '{"kind": "x"}\n')
        self.assertEqual(read(os.path.join(pm, "state.json")), '{"lastTab": "mine"}')
        tabs_after = json.loads(read(os.path.join(pm, "tabs.json")))
        ids = [t["id"] for t in tabs_after["tabs"]]
        self.assertEqual(ids[0], "mine")            # q's tab kept, first
        self.assertIn("github", ids)                # new default tabs added by the new code's tabs.upgrade()
        self.assertTrue(os.path.isfile(os.path.join(pm, "tabs.json.old")))
        self.assertIn("server.py --init: exit 0", "\n".join(e["logTail"]))
        # an identical copy is not an update (no banner on the first open after setup.ps1)
        os.remove(self.paths.pm("installs-state.json"))
        _, v, ents = self.run_pass()
        self.assertEqual(ents["project-manager"]["state"], "ok")
        self.assertFalse(v["selfUpdated"])


class TestElectronUpdate(unittest.TestCase):
    """builtin:electron-update against a local fake of GitHub's releases API (port 9238)."""
    PORT = 9238

    def setUp(self):
        import hashlib
        import io
        import zipfile
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        self.tmp = tempfile.mkdtemp()
        self.q = os.path.join(self.tmp, "Quark")
        self.paths = config.Paths(PM_ROOT, self.q)
        self.el = os.path.join(self.paths.pm_dir, "electron")
        write(os.path.join(self.el, "electron"), "old program")
        write(os.path.join(self.el, "VERSION"), "44.9.2\n")
        self.files = {}
        self.hits = []
        self.release = None
        self.set_release("45.1.0")
        outer = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.hits.append(self.path)
                body = json.dumps(outer.release).encode() if self.path == "/latest" else outer.files.get(self.path)
                if body is None:
                    self.send_response(404)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        self.httpd = ThreadingHTTPServer(("127.0.0.1", self.PORT), H)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self._old = updater.ELECTRON_RELEASES, updater.electron_platform
        updater.ELECTRON_RELEASES = "http://127.0.0.1:%d/latest" % self.PORT
        updater.electron_platform = lambda: ("linux-x64", "electron")
        self._hashlib, self._io, self._zipfile = hashlib, io, zipfile

    def tearDown(self):
        updater.ELECTRON_RELEASES, updater.electron_platform = self._old
        self.httpd.shutdown()
        self.httpd.server_close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def set_release(self, ver, corrupt=False, prerelease=False):
        import hashlib
        import io
        import zipfile
        name = "electron-v%s-linux-x64.zip" % ver
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("electron", "new program " + ver)
            z.writestr("resources/default_app.asar", "x")
            z.writestr("../escape.txt", "never outside")
        data = buf.getvalue()
        digest = hashlib.sha256(b"other" if corrupt else data).hexdigest()
        self.files["/" + name] = data
        self.files["/SHASUMS256.txt"] = ("%s *%s\n%s *electron-v%s-win32-x64.zip\n" % (digest, name, "0" * 64, ver)).encode()
        base = "http://127.0.0.1:%d" % self.PORT
        self.release = {"tag_name": "v" + ver, "prerelease": prerelease, "draft": False, "assets": [
            {"name": name, "size": len(data), "browser_download_url": base + "/" + name},
            {"name": "SHASUMS256.txt", "size": 100, "browser_download_url": base + "/SHASUMS256.txt"}]}

    def run_entry(self, now=None):
        u = updater.Updater(self.paths, enabled=True)
        u.entries = {"electron": {"id": "electron"}}
        u.electron({"id": "electron", "run": updater.ELECTRON_UPDATE}, now=now)
        return u, u.entries["electron"]

    def test_stage_and_gap(self):
        nxt = os.path.join(self.paths.pm_dir, "electron.next")
        u, e = self.run_entry()
        self.assertEqual(e["state"], "ok", e.get("logTail"))
        self.assertIn("45.1.0 is downloaded", e["note"])
        self.assertEqual(read(os.path.join(nxt, "electron")), "new program 45.1.0")
        self.assertEqual(read(os.path.join(nxt, "VERSION")).strip(), "45.1.0")
        self.assertEqual(read(os.path.join(self.el, "electron")), "old program")  # (the running one is never touched)
        self.assertFalse(os.path.exists(os.path.join(self.paths.pm_dir, "escape.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.paths.pm_dir, "electron.download")))
        self.assertEqual((e["installedVersion"], e["latestVersion"]), ("44.9.2", "45.1.0"))
        # within 12 h: no request at all
        n = len(self.hits)
        _, e = self.run_entry()
        self.assertEqual((len(self.hits), e["state"]), (n, "ok"))
        self.assertIn("45.1.0 is downloaded", e["note"])
        # after the gap, the same release already staged: only the API is asked
        _, e = self.run_entry(now=time.time() + updater.ELECTRON_CHECK_GAP + 5)
        self.assertEqual(self.hits[n:], ["/latest"])
        # the shell swapped it in (electron\ now 45.1.0): nothing newer, nothing staged
        shutil.rmtree(self.el)
        os.replace(nxt, self.el)
        _, e = self.run_entry(now=time.time() + 3 * updater.ELECTRON_CHECK_GAP)
        self.assertEqual(e["note"], "Electron 45.1.0 is the latest")
        self.assertFalse(os.path.exists(nxt))

    def test_corrupt_prerelease_and_missing(self):
        self.set_release("45.1.0", corrupt=True)
        _, e = self.run_entry()
        self.assertEqual(e["state"], "failed")
        self.assertIn("corrupt", e["note"])
        self.assertFalse(os.path.exists(os.path.join(self.paths.pm_dir, "electron.next")))
        # a failed check is tried again on the next pass (no 12 h wait)
        self.set_release("45.1.1", prerelease=True)
        _, e = self.run_entry()
        self.assertEqual(e["state"], "failed")
        self.assertIn("unexpected latest release", e["note"])
        # an older or equal release never replaces the installed one
        self.set_release("44.0.0")
        _, e = self.run_entry()
        self.assertEqual((e["state"], e["note"]), ("ok", "Electron 44.9.2 is the latest"))
        # no electron\ (setup -NoElectron): nothing is downloaded
        shutil.rmtree(self.el)
        n = len(self.hits)
        _, e = self.run_entry(now=time.time() + 2 * updater.ELECTRON_CHECK_GAP)
        self.assertEqual(e["state"], "ok")
        self.assertIn("nothing to update", e["note"])
        self.assertEqual(len(self.hits), n)


@unittest.skipIf(util.IS_WINDOWS, "posix process groups (the Windows path uses a process snapshot)")
class TestShellGuard(unittest.TestCase):
    """shell/guard.py: the servers the shell started stop when the shell's pipe closes without "quit"."""
    def spawn_server(self):
        return subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"], start_new_session=True)

    def guard(self):
        return subprocess.Popen([sys.executable, os.path.join(PM_ROOT, "shell", "guard.py")], stdin=subprocess.PIPE)

    def test_dies_with_the_shell(self):
        a, b, c = self.spawn_server(), self.spawn_server(), self.spawn_server()
        g = self.guard()
        g.stdin.write(("add %d\nadd %d\nadd %d\ndel %d\n" % (a.pid, b.pid, c.pid, c.pid)).encode())
        g.stdin.close()  # (the shell died: its end of the pipe closed)
        self.assertEqual(g.wait(10), 0)
        self.assertIsNotNone(a.wait(10))
        self.assertIsNotNone(b.wait(10))
        self.assertIsNone(c.poll())  # (it had exited as far as the shell knew: left alone)
        c.kill()
        c.wait()

    def test_normal_close_leaves_them(self):
        a = self.spawn_server()
        g = self.guard()
        g.stdin.write(("add %d\nquit\n" % a.pid).encode())
        g.stdin.close()
        self.assertEqual(g.wait(10), 0)
        time.sleep(0.2)
        self.assertIsNone(a.poll())
        a.kill()
        a.wait()


class TestHelpers(unittest.TestCase):
    def test_expand_and_python(self):
        p = config.Paths(PM_ROOT, os.path.join(tempfile.gettempdir(), "Q"))
        s = updater.expand("a %QUARK%\\x %pm% %Repo% %OTHER%", p)
        self.assertEqual(s, "a %s\\x %s %s %%OTHER%%" % (p.quark, p.pm_dir, os.path.join(p.quark, "repository")))
        if not util.IS_WINDOWS:
            self.assertEqual(updater.fix_python("py -3 tools\\x.py --q"), updater.python_command() + " tools\\x.py --q")
            self.assertEqual(updater.fix_python("py tools/x.py"), updater.python_command() + " tools/x.py")
            self.assertEqual(updater.shell_argv("x"), ["/bin/sh", "-c", "x"])
        self.assertEqual(updater.fix_python("tools\\m\\install.cmd"), "tools\\m\\install.cmd")
        self.assertEqual(updater.fix_python("pyx -3 a"), "pyx -3 a")

    def test_normalize(self):
        n = updater.normalize_entry({"id": "a", "sources": "tools\\a\\", "run": " x ", "restartsPort": [8769, "8770", 0, 8769]})
        self.assertEqual((n["sources"], n["run"], n["restartsPort"], n["timeoutSec"], n["enabled"], n["check"]),
                         (["tools/a"], "x", [8769, 8770], 600, True, None))
        self.assertIsNone(updater.normalize_entry({"id": "a", "sources": [], "run": "x"}))
        self.assertIsNone(updater.normalize_entry({"id": "../a", "sources": ["a"], "run": "x"}))
        self.assertIsNone(updater.normalize_entry("x"))
        self.assertEqual(updater.normalize_entry({"id": "electron", "run": updater.ELECTRON_UPDATE})["sources"], [])
        self.assertIsNone(updater.normalize_entry({"id": "a", "run": "x"}))  # (only the electron builtin has no sources)

    def test_shipped_registry(self):
        entries, _ = updater.load_registry(config.Paths(PM_ROOT, tempfile.gettempdir()))
        ids = [e["id"] for e in entries]
        self.assertEqual(ids[0], "project-manager")
        self.assertEqual(entries[0]["run"], updater.SELF_UPDATE)
        self.assertIn("docs", ids)
        for e in entries:
            self.assertTrue(e["check"], e["id"])
        self.assertEqual(entries[-1]["run"], updater.ELECTRON_UPDATE)  # (last: the big download never delays a tool)
        model = next(e for e in entries if e["id"] == "model")
        # install.cmd's default output is %USERPROFILE%\Desktop\Quark, which is not always the Quark folder
        self.assertTrue(model["run"].endswith('install.cmd "%QUARK%\\model-editor"'), model["run"])
        self.assertEqual(len(ids), len(json.loads(read(os.path.join(PM_ROOT, "installs.json")))["entries"]))


class TestServerUpdates(unittest.TestCase):
    def test_endpoints(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = server.App(config.Paths(PM_ROOT, tmp), port=0, modules=(), prober=lambda p: False)
            httpd = server.make_server(app, 0)
            port = httpd.server_address[1]
            threading.Thread(target=httpd.serve_forever, daemon=True).start()
            try:
                def req(path, method="GET"):
                    c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
                    c.request(method, path)
                    r = c.getresponse()
                    out = r.status, json.loads(r.read())
                    c.close()
                    return out
                code, d = req("/api/updates")
                self.assertEqual(code, 200)
                self.assertEqual({"repo", "entries", "selfUpdated", "lastRunAt", "restart", "enabled"} <= set(d), True)
                self.assertFalse(d["enabled"])
                self.assertIn("updates", req("/api/pm")[1])
                self.assertEqual(req("/api/updates/run", "POST")[1]["accepted"], False)
                self.assertEqual(req("/api/updates/ack?id=x", "POST")[1], {"removed": False})
            finally:
                httpd.shutdown()
                httpd.server_close()


if __name__ == "__main__":
    unittest.main()
