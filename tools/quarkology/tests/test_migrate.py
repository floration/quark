"""Tests for the move from Quark Project to Quarkology: qo/migrate.py (through server.py --init, as setup.ps1 and the
self-update run it) and the hand-over folder tools/project_manager/ that an install from before the rename updates
from (SPEC.md section 4, "Moving from Quark Project")."""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
PM_ROOT = os.path.dirname(HERE)
BRIDGE = os.path.join(os.path.dirname(PM_ROOT), "project_manager")
sys.path.insert(0, PM_ROOT)

from qo import config, migrate, updater  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def snapshot(folder):
    """{relative path: (sha256, mtime_ns)} of every file under folder."""
    out = {}
    for root, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            p = os.path.join(root, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, folder)] = (hashlib.sha256(fh.read()).hexdigest(), os.stat(p).st_mtime_ns)
    return out


MINE = {"tabs": [{"id": "mine", "name": "Mine", "folder": "mine-tool"},
                 {"id": "project", "name": "Supervisor Board", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
                  "start": "py -3 \"%PM%\\server.py\"", "keepAlive": True}],
        "defaultsSeen": ["project"]}


class Migrate(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="qo-migrate-")
        self.q = os.path.join(self.tmp, "Quark")
        self.old = os.path.join(self.q, "project-manager")
        self.new = os.path.join(self.q, "quarkology")
        o = self.old
        # Quark Project as setup.ps1 and the self-update left it: its code ...
        write(os.path.join(o, "server.py"), "# old server\n")
        write(os.path.join(o, "pm", "updater.py"), "# old\n")
        write(os.path.join(o, "pm", "__pycache__", "updater.cpython-312.pyc"), "")
        write(os.path.join(o, "web", "index.html"), "<html>old</html>")
        write(os.path.join(o, "shell", "main.js"), "// old shell\n")
        write(os.path.join(o, "project-manager.cmd"), "@echo off\r\n")
        write(os.path.join(o, "project-manager-hidden.vbs"), "' old\r\n")
        write(os.path.join(o, "installs.json"), '{"entries": []}')
        write(os.path.join(o, "quark.ico"), "ico")
        # ... and q's files
        write(os.path.join(o, "tabs.json"), json.dumps(MINE))
        write(os.path.join(o, "tabs.json.old"), '{"tabs": []}')
        write(os.path.join(o, "config.json"), '{"oracle": {"host": "203.0.113.9"}}')
        write(os.path.join(o, "history.jsonl"), '{"kind": "x"}\n')
        write(os.path.join(o, "installs-state.json"), '{"entries": {"botmap": {"stamp": "abc"}}}')
        write(os.path.join(o, "play-notes.jsonl"), '{"note": 1}\n')
        write(os.path.join(o, "checklist-ticks.json"), "{}")
        write(os.path.join(o, "logs", "install-botmap.log"), "built\n")
        write(os.path.join(o, "logs", "play", "s1.log"), "game\n")
        write(os.path.join(o, "electron", "electron.exe"), "exe")
        write(os.path.join(o, "electron", "VERSION"), "45.0.0\n")
        write(os.path.join(o, "electron", "resources", "default_app.asar"), "asar")
        write(os.path.join(o, "electron.next", "electron.exe"), "exe2")
        write(os.path.join(o, "electron.next", "VERSION"), "45.1.0\n")
        write(os.path.join(o, "electron.old", "electron.exe"), "older")
        write(os.path.join(o, "electron.rejected"), "44.9.0\n")
        self.paths = config.Paths(PM_ROOT, self.q)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install_new(self):
        """What setup.ps1 copies (the code), then server.py --init as it runs it."""
        os.makedirs(self.new, exist_ok=True)
        for f in ("server.py", "quarkology.cmd", "quarkology-hidden.vbs", "installs.json"):
            shutil.copy2(os.path.join(PM_ROOT, f), os.path.join(self.new, f))
        for d in ("qo", "web", "shell"):
            if not os.path.isdir(os.path.join(self.new, d)):
                shutil.copytree(os.path.join(PM_ROOT, d), os.path.join(self.new, d),
                                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        return self.init()

    def init(self):
        p = subprocess.run([sys.executable, os.path.join(self.new, "server.py"), "--quark", self.q, "--init"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(p.returncode, 0, p.stdout.decode())
        return p.stdout.decode()

    # -- step 1: server.py --init ------------------------------------------------------------------------------------
    def test_init_moves_q_files(self):
        before = snapshot(self.old)
        out = self.install_new()
        self.assertIn("moved from", out)
        n = self.new
        tabs = json.loads(read(os.path.join(n, "tabs.json")))
        ids = [t["id"] for t in tabs["tabs"]]
        self.assertEqual(ids[0], "mine")                      # q's tabs, not the defaults ...
        self.assertIn("github", ids)                          # ... brought up to date by the same --init
        for f in ("config.json", "history.jsonl", "installs-state.json", "play-notes.jsonl", "checklist-ticks.json",
                  os.path.join("logs", "install-botmap.log"), os.path.join("logs", "play", "s1.log"), "electron.rejected"):
            self.assertEqual(read(os.path.join(n, f)), read(os.path.join(self.old, f)), f)
        # Electron whole (no download), a staged one too; never the rollback copy
        self.assertEqual(read(os.path.join(n, "electron", "VERSION")), "45.0.0\n")
        self.assertTrue(os.path.isfile(os.path.join(n, "electron", "resources", "default_app.asar")))
        self.assertEqual(read(os.path.join(n, "electron.next", "VERSION")), "45.1.0\n")
        self.assertFalse(os.path.exists(os.path.join(n, "electron.old")))
        self.assertFalse(os.path.exists(os.path.join(n, "electron.migrating")))
        # no old code came over; the new code is untouched
        for f in ("pm", "project-manager.cmd", "project-manager-hidden.vbs", "quark.ico"):
            self.assertFalse(os.path.exists(os.path.join(n, f)), f)
        self.assertEqual(read(os.path.join(n, "server.py")), read(os.path.join(PM_ROOT, "server.py")))
        self.assertEqual(read(os.path.join(n, "installs.json")), read(os.path.join(PM_ROOT, "installs.json")))
        # the old folder: unchanged but for MOVED.txt
        after = snapshot(self.old)
        self.assertIn("now called Quarkology", read(os.path.join(self.old, "MOVED.txt")))
        del after["MOVED.txt"]
        self.assertEqual(after, before)
        m = json.loads(read(os.path.join(n, migrate.MARKER)))
        self.assertIn("config.json", m["files"])
        self.assertIsNone(m["syncedAt"])

    def test_second_init_changes_nothing(self):
        self.install_new()
        new, old = snapshot(self.new), snapshot(self.old)
        out = self.init()
        self.assertNotIn("moved from", out)
        self.assertEqual(snapshot(self.new), new)
        self.assertEqual(snapshot(self.old), old)
        self.assertIsNone(migrate.copy_old(self.paths))

    def test_fresh_install_without_old_folder(self):
        shutil.rmtree(self.old)
        self.install_new()
        self.assertEqual([t["id"] for t in json.loads(read(os.path.join(self.new, "tabs.json")))["tabs"]][0], "project")
        self.assertFalse(os.path.exists(os.path.join(self.new, migrate.MARKER)))
        self.assertFalse(os.path.exists(self.old))

    def test_never_overwrites(self):
        write(os.path.join(self.new, "config.json"), '{"mine": "new"}')
        write(os.path.join(self.new, "logs", "install-botmap.log"), "newer\n")
        r = migrate.copy_old(self.paths)
        self.assertTrue(r["done"])
        self.assertEqual(read(os.path.join(self.new, "config.json")), '{"mine": "new"}')
        self.assertEqual(read(os.path.join(self.new, "logs", "install-botmap.log")), "newer\n")
        self.assertEqual(read(os.path.join(self.new, "tabs.json")), json.dumps(MINE))

    def test_failed_copy_is_tried_again(self):
        real = shutil.copy2

        def flaky(src, dst, *a, **kw):
            if os.path.basename(src) == "history.jsonl":
                raise PermissionError(13, "in use")
            return real(src, dst, *a, **kw)
        with mock.patch.object(migrate.shutil, "copy2", flaky):
            r = migrate.copy_old(self.paths)
        self.assertFalse(r["done"])
        self.assertTrue(any("history.jsonl" in e for e in r["errors"]))
        self.assertFalse(os.path.exists(os.path.join(self.new, migrate.MARKER)))   # (so the next --init tries again)
        r = migrate.copy_old(self.paths)
        self.assertTrue(r["done"])
        self.assertEqual(r["copied"], ["history.jsonl"])
        self.assertTrue(os.path.exists(os.path.join(self.new, migrate.MARKER)))

    def test_electron_all_or_nothing(self):
        def broken(src, dst, *a, **kw):
            os.makedirs(dst)
            write(os.path.join(dst, "electron.exe"), "half")
            raise shutil.Error([(src, dst, "in use")])
        with mock.patch.object(migrate.shutil, "copytree", broken):
            r = migrate.copy_old(self.paths)
        self.assertFalse(r["done"])
        for d in ("electron", "electron.next", "electron.migrating", "electron.next.migrating"):
            self.assertFalse(os.path.exists(os.path.join(self.new, d)), d)
        self.assertTrue(os.path.isfile(os.path.join(self.new, "config.json")))   # (the rest still came over)
        self.assertTrue(migrate.copy_old(self.paths)["done"])
        self.assertEqual(read(os.path.join(self.new, "electron", "VERSION")), "45.0.0\n")

    # -- step 2: what the old app wrote after the copy ---------------------------------------------------------------
    def test_sync_after_old_app_closed(self):
        migrate.copy_old(self.paths)
        # the old app ran on for a while: a play note, a new log, a tab dragged; Quarkology changed config.json itself
        write(os.path.join(self.old, "play-notes.jsonl"), '{"note": 1}\n{"note": 2}\n')
        write(os.path.join(self.old, "logs", "install-new.log"), "late\n")
        write(os.path.join(self.old, "config.json"), '{"oracle": {"host": "203.0.113.10"}}')
        write(os.path.join(self.new, "config.json"), '{"oracle": {"host": "198.51.100.1"}}')
        again = migrate.sync_old(self.paths)
        self.assertIn("play-notes.jsonl", again)
        self.assertEqual(read(os.path.join(self.new, "play-notes.jsonl")), '{"note": 1}\n{"note": 2}\n')
        self.assertEqual(read(os.path.join(self.new, "logs", "install-new.log")), "late\n")
        self.assertEqual(read(os.path.join(self.new, "config.json")), '{"oracle": {"host": "198.51.100.1"}}')
        self.assertNotIn("config.json", again)
        # once only
        write(os.path.join(self.old, "play-notes.jsonl"), "changed again\n")
        self.assertIsNone(migrate.sync_old(self.paths))
        self.assertEqual(read(os.path.join(self.new, "play-notes.jsonl")), '{"note": 1}\n{"note": 2}\n')

    def test_server_start_syncs_before_reading(self):
        self.install_new()
        write(os.path.join(self.old, "history.jsonl"), '{"kind": "x"}\n{"kind": "late"}\n')
        import server  # (the copy in the repository: the same code)
        with mock.patch.object(server, "App") as app, mock.patch.object(server, "make_server") as ms:
            def made(*a, **kw):
                # the App is made only after the sync: it reads the synced history
                self.assertIn("late", read(os.path.join(self.new, "history.jsonl")))
                return mock.MagicMock()
            app.side_effect = made
            ms.return_value.serve_forever.side_effect = KeyboardInterrupt
            self.assertEqual(server.main(["--quark", self.q, "--port", "0", "--no-update"]), 0)
        self.assertTrue(json.loads(read(os.path.join(self.new, migrate.MARKER)))["syncedAt"])


class HandOver(unittest.TestCase):
    """tools/project_manager/: what an install from before the rename (Quark\\project-manager\\) updates from."""

    def test_registry(self):
        data = json.loads(read(os.path.join(BRIDGE, "installs.json")))
        entries = [updater.normalize_entry(e) for e in data["entries"]]
        self.assertTrue(all(entries))
        first, second = entries
        # Quarkology is installed (and q's files copied) before the old app replaces itself with the hand-over
        self.assertEqual(first["id"], "quarkology")
        self.assertEqual(first["sources"], ["tools/quarkology"])
        self.assertIn("tools\\quarkology\\setup.ps1", first["run"])
        self.assertIn("-NonInteractive", first["run"])
        self.assertTrue(os.path.isfile(os.path.join(PM_ROOT, "setup.ps1")))
        self.assertEqual(first["check"], "quarkology\\server.py")
        self.assertEqual((second["id"], second["run"], second["sources"]),
                         ("project-manager", updater.SELF_UPDATE, ["tools/project_manager"]))

    def test_what_the_old_self_update_copies(self):
        # the old updater copies these files and mirrors shell\ (pm\ and web\ are not here: it leaves them alone)
        for f in ("server.py", "project-manager.cmd", "project-manager-hidden.vbs", "installs.json"):
            self.assertTrue(os.path.isfile(os.path.join(BRIDGE, f)), f)
        for d in ("pm", "web"):
            self.assertFalse(os.path.exists(os.path.join(BRIDGE, d)), d)
        pkg = json.loads(read(os.path.join(BRIDGE, "shell", "package.json")))
        self.assertEqual(pkg["main"], "main.js")
        main = read(os.path.join(BRIDGE, "shell", "main.js"))
        self.assertIn("quarkology", main)
        self.assertNotIn("electron-data", main.split("app.setPath('userData'")[1].split("\n")[0])
        # the old shell keeps loading its preload and tab bar until it restarts
        for f in ("preload.js", "tabbar.html", "tabbar.css", "tabbar.js"):
            self.assertTrue(os.path.isfile(os.path.join(BRIDGE, "shell", f)), f)
        if shutil.which("node"):
            p = subprocess.run(["node", "--check", os.path.join(BRIDGE, "shell", "main.js")], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
            self.assertEqual(p.returncode, 0, p.stdout.decode())

    def test_server_stand_in(self):
        tmp = tempfile.mkdtemp(prefix="qo-bridge-")
        try:
            q = os.path.join(tmp, "Quark")
            old = os.path.join(q, "project-manager")
            os.makedirs(old)
            shutil.copy2(os.path.join(BRIDGE, "server.py"), old)
            run = lambda *a: subprocess.run([sys.executable, os.path.join(old, "server.py")] + list(a),
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            self.assertEqual(run("--quark", q, "--init").returncode, 0)          # (the old self-update's --init)
            self.assertEqual(sorted(os.listdir(old)), ["server.py"])
            self.assertEqual(run("--port", "8768").returncode, 2)                # (no Quarkology yet)
            write(os.path.join(q, "quarkology", "server.py"),
                  "import sys, os\nopen(os.path.join(os.path.dirname(__file__), 'args.txt'), 'w').write(' '.join(sys.argv[1:]))\n")
            self.assertEqual(run("--port", "8768").returncode, 0)
            self.assertEqual(read(os.path.join(q, "quarkology", "args.txt")), "--port 8768")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
