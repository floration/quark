#!/usr/bin/env python3
"""Which quark_tests install.py exports with (fake builds: shell scripts that write a manifest; POSIX only).

    python3 tools/sound_editor/test/test_install.py
"""
import importlib.util, json, os, sys, tempfile, time, unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sound_install", os.path.join(HERE, "..", "install.py"))
inst = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inst)


def fake_tests(folder, version, age):
    """A quark_tests in `folder` whose export names `version`; `age` seconds old (mtime)."""
    os.makedirs(folder, exist_ok=True)
    exe = os.path.join(folder, "quark_tests")
    with open(exe, "w") as f:
        f.write('#!/bin/sh\nmkdir -p "$2"\necho \'{"version": "%s", "items": []}\' > "$2/manifest.json"\n' % version)
    os.chmod(exe, 0o755)
    t = time.time() - age
    os.utime(exe, (t, t))
    return exe


@unittest.skipIf(os.name == "nt", "fake builds are shell scripts")
class FindTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.quark = self.tmp.name
        self.root = os.path.join(self.quark, "repository")
        os.makedirs(self.root)
        with open(os.path.join(self.root, "VERSION"), "w") as f: f.write("1.18.31\n")
        self.patch = mock.patch.multiple(inst, ROOT=self.root, QUARK=self.quark, BUILD_DIR=os.path.join(self.root, "build-editors"))
        self.patch.start()
        os.environ.pop("QUARK_TESTS", None)

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def pick(self):
        """(the quark_tests main() exported with or None, exit code)."""
        out = os.path.join(self.quark, "sound-editor")
        calls = []
        real_run = inst.subprocess.run
        def run(cmd, *a, **kw):   # (build.py is not run: only which export it was handed)
            if cmd[1:2] == [os.path.join(inst.HERE, "build.py")]:
                calls.append(cmd); return mock.Mock(returncode=0)
            return real_run(cmd, *a, **kw)
        with mock.patch.object(inst.subprocess, "run", run), mock.patch.object(sys, "argv", ["install.py", out]), \
             mock.patch.object(inst.shutil, "copyfile"):
            rc = inst.main()
        if rc != 0: return "", rc
        with open(os.path.join(out, "INSTALLED.txt")) as f: return f.read(), rc

    def test_base_version(self):
        self.assertEqual(inst.base_version("1.18.16-test.140+abc"), "1.18.16")
        self.assertEqual(inst.base_version("1.19.0.testing.5"), "1.19.0")   # (testing builds from 1.19.0 on)
        self.assertEqual(inst.base_version("1.19.0"), "1.19.0")

    def test_newest_matching_build_wins(self):
        fake_tests(os.path.join(self.root, "build-win"), "1.18.30", 10)                 # stale, newest
        old = fake_tests(os.path.join(self.root, "build-model-editor"), "1.18.31", 500)
        new = fake_tests(os.path.join(self.quark, ".src", "testing", "build-win"), "1.18.31-test.5", 100)
        self.assertEqual(inst.find_tests()[0], os.path.join(self.root, "build-win", "quark_tests"))
        with mock.patch.object(inst, "build_tests", side_effect=AssertionError("no build needed")):
            how, rc = self.pick()
        self.assertEqual(rc, 0)
        self.assertIn(new, how)
        self.assertNotIn(old, how)

    def test_every_installers_folder_is_searched(self):
        for d in ("build-model-editor", "build-editors"):
            exe = fake_tests(os.path.join(self.root, d), "1.18.31", 50)
            self.assertIn(exe, inst.find_tests())

    def test_only_stale_builds_builds_one(self):
        fake_tests(os.path.join(self.root, "build-win"), "1.18.30", 10)
        built = os.path.join(self.root, "build-editors", "quark_tests")
        with mock.patch.object(inst, "build_tests", side_effect=lambda: fake_tests(os.path.dirname(built), "1.18.31", 0)) as b:
            how, rc = self.pick()
        b.assert_called_once()
        self.assertEqual(rc, 0)
        self.assertIn(built, how)

    def test_no_build_possible_and_no_data_fails(self):
        fake_tests(os.path.join(self.root, "build-win"), "1.18.30", 10)
        with mock.patch.object(inst, "build_tests", return_value=None):
            _, rc = self.pick()
        self.assertEqual(rc, 1)

    def test_explicit_quark_tests_first(self):
        fake_tests(os.path.join(self.root, "build"), "1.18.31", 0)
        mine = fake_tests(os.path.join(self.quark, "mine"), "1.18.31", 1000)
        with mock.patch.dict(os.environ, {"QUARK_TESTS": mine}):
            self.assertEqual(inst.find_tests()[0], mine)


if __name__ == "__main__":
    unittest.main()
