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


TEST_SEED = bytes(range(32))


def ph_sign(msg, seed=TEST_SEED):
    """Ed25519ph signature (hex) of msg and the public key (hex): RFC 8032 signing, for the fake GitHub only."""
    h = hashlib.sha512(seed).digest()
    a = (int.from_bytes(h[:32], "little") & ((1 << 254) - 8)) | (1 << 254)
    pub = play._encode_pt(play._pt_mul(a, play._G))
    ph = hashlib.sha512(msg).digest()
    r = int.from_bytes(hashlib.sha512(play._DOM2_PH + h[32:] + ph).digest(), "little") % play._L
    big_r = play._encode_pt(play._pt_mul(r, play._G))
    k = int.from_bytes(hashlib.sha512(play._DOM2_PH + big_r + pub + ph).digest(), "little") % play._L
    return (big_r + ((r + k * a) % play._L).to_bytes(32, "little")).hex(), pub.hex()


TEST_PUB = ph_sign(b"")[1]


def signed_release(zipped, tag="test-1.18.14-test.240", channel="testing", version=None, zip_sha=None):
    """The fake GitHub's signature files for a release: {path: body} (zip .sig, manifest, manifest .sig)."""
    man = ("quark release\nchannel %s\ntag %s\nversion %s\ncommit %s\nsha256 quark-windows.qpak %s\n"
           "sha256 quark-windows.zip %s\n" % (channel, tag, version or play.version_from_tag(tag), "c" * 40, "0" * 64,
                                               zip_sha or hashlib.sha256(zipped).hexdigest()))
    return {"/dl/test.zip.sig": (200, ph_sign(zipped)[0] + "\n"), "/dl/rel.txt": (200, man),
            "/dl/rel.txt.sig": (200, ph_sign(man.encode())[0])}


def releases_fixture(base, sha_line=True):
    return [
        {"tag_name": "test-1.18.14-test.240", "name": "Testing 1.18.14", "prerelease": True, "draft": False,
         "published_at": "2026-10-24T05:00:00Z", "html_url": "https://github.com/floration/quark/releases/tag/x",
         "assets": [{"name": "quark-windows.zip", "size": 1234, "browser_download_url": base + "/dl/test.zip"},
                    {"name": "quark-windows.zip.sig", "size": 128, "browser_download_url": base + "/dl/test.zip.sig"},
                    {"name": "quark-release.txt", "size": 200, "browser_download_url": base + "/dl/rel.txt"},
                    {"name": "quark-release.txt.sig", "size": 128, "browser_download_url": base + "/dl/rel.txt.sig"},
                    {"name": "SHA256SUMS", "size": 99, "browser_download_url": base + "/dl/SHA256SUMS"}]},
        {"tag_name": "v1.18.3", "name": "1.18.3", "prerelease": False, "draft": False,
         "published_at": "2026-10-20T05:00:00Z",
         "assets": [{"name": "quark-windows.zip", "size": 1234, "browser_download_url": base + "/dl/v.zip"}]},
        {"tag_name": "v1.16.0", "name": "Before signing", "prerelease": False, "published_at": "2026-01-02T00:00:00Z",
         "assets": [{"name": "quark-windows.zip", "size": 1234, "browser_download_url": base + "/dl/old.zip"}]},
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
        self.assertEqual([x["tag"] for x in r], ["test-1.18.14-test.240", "v1.18.4", "v1.18.3", "v1.16.0", "v0.9.0"])
        self.assertEqual([x["channel"] for x in r], ["testing", "production", "production", "production", "production"])
        self.assertEqual(r[0]["version"], "1.18.14-test.240")
        self.assertEqual(r[0]["zip"]["size"], 1234)
        self.assertTrue(r[0]["sums"].endswith("/dl/SHA256SUMS"))
        self.assertIsNone(r[1]["zip"])  # (not https: refused)
        self.assertIsNone(r[4]["zip"])  # (no Windows zip: shown greyed)
        self.assertIsNone(r[2]["sums"])
        # signed releases: from 1.17.0 on (or with a .sig) the signature and signed manifest are needed
        self.assertTrue(r[0]["sig"].endswith("/dl/test.zip.sig") and r[0]["manifest"] and r[0]["manifestSig"])
        self.assertEqual([x["needsSig"] for x in r], [True, True, True, False, False])
        self.assertFalse(r[0]["nameSaysMore"])  # ("Testing 1.18.14" only repeats the tag)
        self.assertTrue(r[3]["nameSaysMore"])
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
        # (q's line breaks become " / ": an entry is one line)
        self.assertEqual(line, "- **Play note: testing 1.18.14-test.240** Did: two FFA rounds / on bastion. Noticed: "
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

    def test_ed25519ph(self):
        # RFC 8032 section 7.3 (Ed25519ph, "abc"), the signer the fake GitHub uses, and a tampered message
        pub = "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf"
        sig = ("98a70222f0b8121aa9d30f813d683f809e462b469c7ff87639499bb94e6dae4131f85042463c2a355a2003d062adf5aaa10b8c61e"
               "636062aaad11c2a26083406")
        self.assertTrue(play.ed25519ph_verify(hashlib.sha512(b"abc").digest(), sig, pub))
        self.assertFalse(play.ed25519ph_verify(hashlib.sha512(b"abd").digest(), sig, pub))
        self.assertFalse(play.ed25519ph_verify(hashlib.sha512(b"abc").digest(), "zz" + sig[2:], pub))
        seed = bytes.fromhex("833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42")
        self.assertEqual(ph_sign(b"abc", seed), (sig, pub))
        # the key is the one built into the launcher
        sign_cpp = os.path.join(os.path.dirname(os.path.dirname(PM_ROOT)), "src", "common", "sign.cpp")
        if os.path.isfile(sign_cpp):
            with open(sign_cpp, encoding="utf-8") as f:
                self.assertIn('kReleasePublicKey = "%s"' % play.RELEASE_PUBLIC_KEY, f.read())

    @unittest.skipUnless(os.environ.get("QUARK_SIGN"), "set QUARK_SIGN=<build>/quark_sign to check against it")
    def test_against_quark_sign(self):
        with tempfile.TemporaryDirectory() as d:
            out = subprocess.run([os.environ["QUARK_SIGN"], "keygen"], stdout=subprocess.PIPE, check=True).stdout.decode()
            priv = next(l.split()[-1] for l in out.splitlines() if l.startswith("private"))
            pub = next(l.split()[-1] for l in out.splitlines() if l.startswith("public"))
            f = os.path.join(d, "x.zip")
            write(f, "zip bytes " * 1000)
            subprocess.run([os.environ["QUARK_SIGN"], "sign", f], env=dict(os.environ, QUARK_SIGNING_KEY=priv),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            with open(f, "rb") as a, open(f + ".sig") as b:
                h, sig = hashlib.sha512(a.read()).digest(), b.read()
            self.assertTrue(play.ed25519ph_verify(h, sig, pub))
            self.assertFalse(play.ed25519ph_verify(h, sig, play.RELEASE_PUBLIC_KEY))

    def test_game_modes_match_the_game(self):
        h = os.path.join(os.path.dirname(os.path.dirname(PM_ROOT)), "src", "game", "gamedefs.h")
        if not os.path.isfile(h):
            self.skipTest("no game source here")
        with open(h, encoding="utf-8") as f:
            src = f.read()
        import re
        for fn, want in (("modeName", play.GAME_MODES), ("modeTitle", play.GAME_MODE_TITLES)):
            m = re.search(r"inline const char\* %s\(int m\) \{\s*static const char\* n\[\] = \{([^}]*)\}" % fn, src)
            self.assertIsNotNone(m, fn)
            self.assertEqual(tuple(re.findall(r'"([^"]*)"', m.group(1))), want, fn)

    def test_read_maps(self):
        d = tempfile.mkdtemp()
        try:
            write(os.path.join(d, "maps", "bastion.qmap"), "# generated\nversion 2\ntitle Bastion\nfor vs\nmodes vs ffa\nbox 0 0 0 1 1 1\n")
            write(os.path.join(d, "maps", "mine.qmap"), "version 2\nhandmade\ntitle My <b>map</b>\nfor zz9\n")
            write(os.path.join(d, "maps", "bad name.qmap"), "for ffa\n")
            write(os.path.join(d, "maps", "notes.txt"), "x")
            m = play.read_maps(d)
            self.assertEqual([(x["name"], x["title"], x["for"], x["modes"], x["handmade"]) for x in m],
                             [("bastion", "Bastion", "vs", ["vs", "ffa"], False), ("mine", "My <b>map</b>", "zz9", ["zz9"], True)])
            self.assertEqual(play.map_modes(m)[-1], "zz9")  # (a mode only a map names comes after the game's)
            self.assertEqual(play.read_maps(os.path.join(d, "nope")), [])
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_versions_and_manifest(self):
        self.assertEqual(play.version_from_tag("test-1.18.6-test.231"), "1.18.6-test.231")
        for a, b in (("1.17.0", "1.17.0-test.3"), ("1.17.0-test.3", "1.17.0-test.2"), ("1.17.0-test.10", "1.17.0-test.9"),
                     ("1.17.0-test.0", "1.17.0-0"), ("1.17.0-0", "1.16.9"), ("2.0", "1.99.99")):
            self.assertGreater(play.version_cmp(a, b), 0, (a, b))
            self.assertLess(play.version_cmp(b, a), 0, (a, b))
        self.assertEqual(play.version_cmp("1.2.3", "1.2.3"), 0)
        self.assertFalse(play.needs_signature({"tag": "v1.16.0"}))
        self.assertTrue(play.needs_signature({"tag": "test-1.17.0-test.1"}))
        self.assertTrue(play.needs_signature({"tag": "v1.0.0", "sig": "https://x/y.sig"}))
        self.assertIsNone(play.parse_manifest("not one"))
        m = play.parse_manifest("quark release\r\nchannel testing\ntag t-1.0.0\nversion 1.0.0\nsha256 quark-windows.zip AB\n")
        self.assertEqual((m["channel"], m["sha256"]), ("testing", {"quark-windows.zip": "ab"}))
        rel = {"tag": "t-1.0.0", "prerelease": True}
        self.assertIsNone(play.check_manifest(m, rel, "AB"))
        self.assertIn("not the one", play.check_manifest(m, rel, "cd"))
        self.assertIn("production", play.check_manifest(m, dict(rel, prerelease=False), "ab"))

    def test_note_fields_and_exit(self):
        self.assertEqual(play.clean_field("  a  \r\nb\n\n\n\nc\t d "), "a\nb\n\nc  d")
        with self.assertRaises(play.PlayError):
            play.clean_field({"a": 1}, "Did")  # (not text: refused, never stored as "{'a': 1}")
        with self.assertRaises(play.PlayError) as cm:
            play.clean_field("x" * 4001, "Did")  # (too long: refused, never cut)
        self.assertIn("4000", cm.exception.message)
        self.assertEqual(len(play.clean_field("x" * 4000)), 4000)
        self.assertEqual(play.flat("- one\n- two\n\nthree"), "- one / - two / three")
        self.assertEqual(play.exit_info(0)["kind"], "ok")
        self.assertEqual(play.exit_info(-11), {"kind": "crash", "text": "exit code -11 (SIGSEGV)"})
        self.assertEqual(play.exit_info(3221225477), {"kind": "crash", "text": "exit code 0xC0000005 (access violation)"})
        self.assertEqual(play.exit_info(3)["kind"], "error")
        self.assertEqual(play.exit_info(-15, stopped=True)["kind"], "stopped")
        self.assertEqual(play.exit_info(None)["kind"], "unknown")
        s = {"label": "testing", "version": "1.0.0", "durationSec": 60, "stopped": True, "exitCode": -15,
             "logTail": "a `b`\nboom"}
        line = play.format_note(s, {"did": "x"}, "2026-10-24 14:05 CT")
        self.assertIn("stopped from the Play tab. Log's last lines: `a 'b' / boom`.", line)
        self.assertNotIn("exit code", line)
        self.assertIn("Played outside the Supervisor Board", play.format_note({"label": "launcher", "freeform": True},
                                                                              {"did": "x"}, "2026-10-24 14:05 CT"))

    def test_change_entry(self):
        a = "- **Play note: a** x. _2026-10-24 14:05 CT, q, open_"
        b = "- **Play note: b** y. _2026-10-24 15:05 CT, q, open_"
        t = play.add_note_lines(None, [b, a])
        t2, what = play.change_entry(t, ("Play note: a", "2026-10-24 14:05 CT"), a.replace(" x.", " z."))
        self.assertEqual(what, "changed")
        self.assertIn(" z.", t2)
        self.assertEqual(play.change_entry(t2, ("Play note: a", "2026-10-24 14:05 CT"), a.replace(" x.", " z."))[1], "same")
        t3, what = play.change_entry(t, ("Play note: b", "2026-10-24 15:05 CT"), None)
        self.assertEqual(what, "removed")
        self.assertTrue(t3.endswith("## Notes\n\n" + a + "\n"), t3)
        t4, what = play.change_entry(t, ("Play note: a", "2026-10-24 14:05 CT"), None)
        self.assertTrue(t4.endswith("## Notes\n\n" + b + "\n"), t4)
        done = t.replace("14:05 CT, q, open_", "14:05 CT, q, in-progress_")
        self.assertEqual(play.change_entry(done, ("Play note: a", "2026-10-24 14:05 CT"), None), (done, "in-progress"))
        self.assertEqual(play.change_entry(t, ("Play note: c", "x"), None)[1], "gone")

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
            self.assertEqual(o[:3], [("project", 0, None), ("play", 1, "project"), ("docs", 1, "project")])
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


class TestTabsMoveOnce(unittest.TestCase):
    """The Claude Manager tab moved before the GitHub Manager (q, 2026-10-24): q's saved tabs.json with the older
    default order gets the new order once; an order q chose himself stays."""

    def order(self, paths):
        return [t["id"] for t in tabs.Tabs(paths).resolved()["tabs"]]

    def old_default(self):
        # (the default list before the swap: github + its Tests child, then docs)
        by = {t["id"]: dict(t) for t in tabs.DEFAULT_TABS["tabs"]}
        ids = [t["id"] for t in tabs.DEFAULT_TABS["tabs"]]
        ids.remove("docs")
        ids.insert(ids.index("tests") + 1, "docs")
        return [by[i] for i in ids]

    def save(self, paths, lst, **extra):
        data = dict({"tabs": lst, "defaultsSeen": sorted(t["id"] for t in tabs.DEFAULT_TABS["tabs"]),
                     "defaultsPlaced": ["play"]}, **extra)
        write(paths.pm("tabs.json"), json.dumps(data))

    def test_old_default_order_is_swapped_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            # (what q's PC has: the old order, plus a tab of q's own between them, which keeps its place)
            lst = self.old_default()
            lst.insert(4, {"id": "mine", "name": "Mine", "parent": "project", "folder": "x"})
            self.save(paths, lst)
            self.assertEqual(self.order(paths)[:7], ["project", "play", "github", "tests", "mine", "docs", "dashboard"])
            self.assertEqual(tabs.upgrade(paths), ["docs"])
            self.assertEqual(self.order(paths)[:7], ["project", "play", "docs", "github", "tests", "mine", "dashboard"])
            saved = util.read_json_file(paths.pm("tabs.json"))
            self.assertEqual(saved["defaultsMoved"], ["docs-before-github-2"])
            self.assertEqual(util.read_json_file(paths.pm("tabs.json.old"))["tabs"], lst)
            self.assertEqual(tabs.upgrade(paths), [])  # (idempotent)
            # q puts it back after the GitHub Manager: left there
            t = next(x for x in saved["tabs"] if x["id"] == "docs")
            saved["tabs"].remove(t)
            saved["tabs"].insert(5, t)
            write(paths.pm("tabs.json"), json.dumps(saved))
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(self.order(paths)[:6], ["project", "play", "github", "tests", "mine", "docs"])

    def test_tests_appended_after_editors(self):
        # q's PC: an older upgrade appended Tests after the editors, and the first try of this move
        # ("docs-before-github") skipped that file but marked itself done; the sibling-only check moves it
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            lst = self.old_default()
            t = next(x for x in lst if x["id"] == "tests")
            lst.remove(t)
            lst.insert([x["id"] for x in lst].index("gamemode"), t)
            self.save(paths, lst, defaultsMoved=["docs-before-github"])
            self.assertEqual(tabs.upgrade(paths), ["docs"])
            self.assertEqual(self.order(paths)[:6], ["project", "play", "docs", "github", "tests", "dashboard"])
            saved = util.read_json_file(paths.pm("tabs.json"))
            self.assertEqual(saved["defaultsMoved"], ["docs-before-github", "docs-before-github-2"])
            self.assertEqual(tabs.upgrade(paths), [])

    def test_own_order_stays(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            lst = self.old_default()
            dash = next(x for x in lst if x["id"] == "dashboard")
            lst.remove(dash)
            lst.insert(2, dash)  # (q's own order: the Editor Manager first)
            self.save(paths, lst)
            before = self.order(paths)
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(self.order(paths), before)
            self.assertEqual(util.read_json_file(paths.pm("tabs.json"))["defaultsMoved"], ["docs-before-github-2"])

    def test_different_parents_stay(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            lst = self.old_default()
            next(x for x in lst if x["id"] == "docs").pop("parent")  # (q made the Claude Manager top level)
            self.save(paths, lst)
            before = self.order(paths)
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(self.order(paths), before)

    def test_new_default_file_is_already_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.Paths(PM_ROOT, os.path.join(tmp, "Quark"))
            self.assertTrue(tabs.write_default(paths))
            self.assertEqual(tabs.upgrade(paths), [])
            self.assertEqual(self.order(paths)[:5], ["project", "play", "docs", "github", "tests"])


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

    def test_build_maps(self):
        p = self.new()
        t = os.path.join(self.quark, "testing")
        self.assertIsNone(p.find_build("testing")["maps"])  # (no maps folder: no pickers)
        self.assertEqual(p.build_maps("testing")["maps"], [])
        write(os.path.join(t, "maps", "summit.qmap"), "title Summit\nfor koth\nmodes koth ffa\n")
        sig = p.find_build("testing")["maps"]
        self.assertTrue(sig)
        got = p.api_get("maps", {"build": ["testing"]})
        self.assertEqual((got[0], got[1]["sig"], [x["name"] for x in got[1]["maps"]]), (200, sig, ["summit"]))
        self.assertEqual(got[1]["titles"]["koth"], "KING OF THE HILL")
        self.assertEqual(p.find_build("testing")["maps"], sig)  # (cached: same listing, same sig)
        write(os.path.join(t, "maps", "harbor.qmap"), "title Harbor\nfor ffa\n")
        self.assertNotEqual(p.find_build("testing")["maps"], sig)
        self.assertEqual(p.api_get("maps", {"build": ["../x"]})[0], 404)

    def test_stop_log_open(self):
        fake_game(os.path.join(self.quark, "testing"), seconds=30, code=0)
        opened = []
        p = self.new(opener=opened.append)
        p.start()
        s = p.launch("testing", "--host ffa")
        self.assertTrue(wait_for(lambda: "started" in p.game_log(s["id"])["text"]))
        lg = p.game_log(s["id"])
        self.assertEqual((lg["state"], lg["exit"]), ("running", None))
        t0 = time.time()
        st = p.stop_game(s["id"])
        self.assertTrue(st["stopping"])
        self.assertTrue(p.stop_game(s["id"])["noop"])
        self.assertTrue(wait_for(lambda: p._session(s["id"])["state"] == "needsNote", 8))
        self.assertLess(time.time() - t0, 4)  # (SIGTERM to its group: the script and its sleep end at once)
        got = p._session(s["id"])
        self.assertTrue(got["stopped"])
        self.assertNotIn("stopping", got)
        v = {x["id"]: x for x in p.view()["sessions"]}[s["id"]]
        self.assertEqual((v["exit"]["kind"], v["hasLog"]), ("stopped", True))
        with self.assertRaises(play.PlayError) as cm:
            p.stop_game(s["id"])
        self.assertEqual(cm.exception.reason, "not-running")
        # the note can carry the log's last lines, and says it was stopped (not a crash)
        n = p.save_note(s["id"], {"noticed": "line one\nline two", "attachLog": True})
        self.assertEqual(n["fields"]["noticed"], "line one\nline two")  # (line breaks kept in the local copy)
        self.assertIn("Noticed: line one / line two.", n["line"])
        self.assertIn("stopped from the Play tab", n["line"])
        self.assertIn("Log's last lines: `started --host ffa", n["line"])
        self.assertIn("started --host ffa", n["logTail"])
        # opening: a build's folder, the logs, a game's log, never a path the page sends
        p.open_path("build", "testing")
        p.open_path("logs")
        p.open_path("log", s["id"])
        self.assertEqual(opened, [os.path.join(self.quark, "testing"), self.paths.pm("logs"), got["log"]])
        for what, ref in (("build", "../.."), ("file", "/etc/passwd"), ("notes-file", None), ("log", "nope")):
            with self.assertRaises(play.PlayError):
                p.open_path(what, ref)
        self.assertEqual(len(opened), 3)
        # a log that is gone, or a session whose log path points elsewhere
        os.remove(got["log"])
        with self.assertRaises(play.PlayError) as cm:
            p.game_log(s["id"])
        self.assertEqual(cm.exception.reason, "no-log")
        got["log"] = "/etc/passwd"
        self.assertIsNone(p._log_path(got))

    def test_crash_flag_and_freeform_note(self):
        fake_game(os.path.join(self.quark, "testing"), seconds=0.1, code=139)
        p = self.new()
        s = p.launch("testing")
        self.assertTrue(wait_for(lambda: p._session(s["id"])["state"] == "needsNote"))
        v = {x["id"]: x for x in p.view()["sessions"]}[s["id"]]
        self.assertEqual(v["exit"], {"kind": "error", "text": "exit code 139"})
        # a note without a session: for a listed build, or for whatever q played through the launcher
        n = p.save_note(None, {"build": "testing", "did": "ranked through the launcher"})
        self.assertTrue(n["id"].startswith("n") and play.NOTE_RE.match(n["id"]))
        self.assertEqual((n["title"], n["session"]), ("Play note: testing 1.18.14-test.240", None))
        self.assertIn("Played outside the Supervisor Board", n["line"])
        n2 = p.save_note("", {"label": "launcher", "version": "1.18.31", "did": "x"})
        self.assertEqual(n2["title"], "Play note: launcher 1.18.31")
        for bad in ({"build": "nope", "did": "x"}, {"build": 5, "did": "x"}, {"did": ["x"]}, {"did": "x" * 4001}):
            with self.assertRaises(play.PlayError):
                p.save_note(None, bad)
        self.assertEqual(len(p.notes()), 2)
        v = p.view()
        self.assertEqual(v["notesTotal"], 2)
        self.assertTrue(v["notes"][0]["freeform"])
        self.assertEqual(v["notes"][0]["where"], "kept on this PC only (no Claude Documents folder)")

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
        routes = {"/releases": rel_route, "/dl/test.zip": (200, zipped), "/dl/v.zip": (200, b"not a zip"),
                  "/dl/old.zip": (200, b"not a zip"), "/dl/SHA256SUMS": (200, sums)}
        routes.update(signed_release(zipped))
        fake = FakeHTTP(routes)
        try:
            p = self.new(releases_url=fake.url + "/releases", public_key=TEST_PUB)
            r = p.releases()
            self.assertTrue(r["busy"] or r["ok"])
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is True))
            self.assertEqual(p.releases()["list"][0]["tag"], "test-1.18.14-test.240")
            self.assertEqual(etag["n"], 1)  # (cached: no second request within 10 min)
            self.assertTrue(p.releases()["complete"])
            with self.assertRaises(play.PlayError):
                p.install("v0.9.0")  # (no Windows zip)
            with self.assertRaises(play.PlayError) as cm:
                p.install("v1.18.3")  # (claims 1.18.3 with no signature: can't be genuine, like the launcher)
            self.assertEqual(cm.exception.reason, "unsigned")
            p.install("test-1.18.14-test.240")
            st = self.wait_install(p, "test-1.18.14-test.240")
            self.assertEqual(st["state"], "done", st)
            self.assertEqual((st["verified"], st["check"]), (True, "signed"))
            dest = os.path.join(self.quark, "versions", "test-1.18.14-test.240")
            self.assertTrue(os.path.isfile(os.path.join(dest, "quark.exe")))
            self.assertTrue(os.path.isfile(os.path.join(dest, "maps", "a.qmap")))
            with open(os.path.join(dest, "BUILD.txt"), encoding="utf-8") as f:
                self.assertEqual(f.read(), "test-1.18.14-test.240 (GitHub release, published 2026-10-24T05:00:00Z)\n")
            self.assertEqual(sorted(os.listdir(os.path.join(self.quark, "versions"))), ["test-1.18.14-test.240"])
            b = {x["id"]: x for x in p.builds()}["versions/test-1.18.14-test.240"]
            self.assertEqual((b["kind"], b["version"], b["label"]), ("version", "1.18.14-test.240", "test-1.18.14-test.240"))
            self.assertEqual(b["check"], "signed")  # (kept in play-install.json: still known after a restart)
            v = p.view()
            row = v["releases"]["list"][0]
            self.assertTrue(row["installed"])
            self.assertEqual(row["check"], "signed")
            self.assertEqual(row["folders"], ["testing"])  # (the testing folder holds this release)
            self.assertIsNone(p.view(rel_sig=v["releases"]["sig"])["releases"]["list"])  # (unchanged: not sent again)
            # a broken zip from before signing (no SHA256SUMS): fails, leaves nothing behind
            p.install("v1.16.0")
            st = self.wait_install(p, "v1.16.0")
            self.assertEqual(st["state"], "failed")
            self.assertFalse(os.path.exists(os.path.join(self.quark, "versions", "v1.16.0")))
            # an old release whose SHA256SUMS doesn't match fails too
            fx = releases_fixture(fake.url)
            fx[2]["assets"].append({"name": "SHA256SUMS", "browser_download_url": fake.url + "/dl/SHA256SUMS"})
            fake.routes["/releases"] = (200, fx)
            fake.routes["/dl/old.zip"] = (200, zipped)
            fake.routes["/dl/SHA256SUMS"] = (200, "%s  quark-windows.zip\n" % ("f" * 64))
            p.remove("test-1.18.14-test.240")
            self.assertFalse(os.path.exists(dest))
            p.releases(force=True)
            self.assertTrue(wait_for(lambda: next(x for x in p.releases()["list"] if x["tag"] == "v1.16.0").get("sums")))
            p.install("v1.16.0")
            st = self.wait_install(p, "v1.16.0")
            self.assertEqual(st["state"], "failed")
            self.assertIn("SHA256SUMS", st["error"])
            self.assertEqual(os.listdir(os.path.join(self.quark, "versions")), [])
            fake.routes["/dl/SHA256SUMS"] = (200, sums)  # (and installs when it matches)
            p.install("v1.16.0")
            self.assertEqual(self.wait_install(p, "v1.16.0")["check"], "sha256")
            for bad in ("..", "../testing", "nope"):
                with self.assertRaises(play.PlayError):
                    p.remove(bad)
            self.assertTrue(os.path.isdir(os.path.join(self.quark, "testing")))
        finally:
            fake.close()

    def wait_install(self, p, tag):
        return wait_for(lambda: p.installs[tag]["state"] not in play.ACTIVE and dict(p.installs[tag]))

    def test_signature_checks(self):
        # like the launcher: a signature that doesn't verify, a manifest for another release or another zip, or a
        # manifest signed by another key: nothing is installed
        zipped = make_zip({"quark.exe": "#!/bin/sh\nexit 0\n"})
        tag = "test-1.18.14-test.240"
        fake = FakeHTTP({"/releases": (200, releases_fixture("")), "/dl/test.zip": (200, zipped)})
        try:
            fake.routes["/releases"] = (200, releases_fixture(fake.url))
            p = self.new(releases_url=fake.url + "/releases", public_key=TEST_PUB)
            p.releases()
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is True))
            cases = [
                (dict(signed_release(zipped), **{"/dl/test.zip.sig": (200, ph_sign(b"other")[0])}), "signature does not verify"),
                (signed_release(zipped, tag="test-1.18.14-test.239"), "manifest is for test-1.18.14-test.239"),
                (signed_release(zipped, channel="production"), "production channel"),
                (signed_release(zipped, zip_sha="e" * 64), "not the one its signed manifest names"),
                (dict(signed_release(zipped), **{"/dl/rel.txt.sig": (200, ph_sign(b"x", bytes(32))[0])}), "manifest's signature"),
            ]
            for routes, why in cases:
                fake.routes.update(routes)
                p.install(tag)
                st = self.wait_install(p, tag)
                self.assertEqual(st["state"], "failed", why)
                self.assertIn(why, st["error"])
                self.assertFalse(os.path.exists(os.path.join(self.quark, "versions", tag)))
            # and with everything right it installs; another key (the real release key) refuses it
            fake.routes.update(signed_release(zipped))
            p.install(tag)
            self.assertEqual(self.wait_install(p, tag)["check"], "signed")
            p.remove(tag)
            p2 = self.new(releases_url=fake.url + "/releases")  # (default: the release key)
            p2.releases()
            self.assertTrue(wait_for(lambda: p2.releases()["ok"] is True))
            p2.install(tag)
            self.assertIn("signature does not verify", self.wait_install(p2, tag)["error"])
        finally:
            fake.close()

    def test_unsigned_old_release_and_cancel(self):
        zipped = make_zip({"quark.exe": "#!/bin/sh\nexit 0\n"})
        gate = threading.Event()

        def slow(path):
            gate.wait(5)
            return (200, zipped)
        fake = FakeHTTP({"/releases": (200, []), "/dl/old.zip": (200, zipped)})
        try:
            fake.routes["/releases"] = (200, releases_fixture(fake.url))
            p = self.new(releases_url=fake.url + "/releases", public_key=TEST_PUB)
            p.releases()
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is True))
            # from before signing, no SHA256SUMS: installs without a prompt (owner decision B6), marked unchecked
            p.install("v1.16.0")
            st = self.wait_install(p, "v1.16.0")
            self.assertEqual((st["state"], st["check"], st["verified"]), ("done", "none", False))
            b = {x["id"]: x for x in play.Play(self.paths).builds()}["versions/v1.16.0"]
            self.assertEqual(b["check"], "none")  # (after a restart too)
            self.assertIn("before signing", b["checkNote"])
            p.remove("v1.16.0")
            # Cancel during the download: nothing is left
            fake.routes["/dl/old.zip"] = slow
            p.install("v1.16.0")
            with self.assertRaises(play.PlayError):
                p.cancel_install("v1.18.3")  # (not being installed)
            self.assertEqual(p.cancel_install("v1.16.0"), {"cancelling": "v1.16.0"})
            gate.set()
            st = self.wait_install(p, "v1.16.0")
            self.assertEqual(st["state"], "cancelled", st)
            self.assertEqual(os.listdir(os.path.join(self.quark, "versions")), [])
        finally:
            gate.set()
            fake.close()

    def test_release_pages(self):
        # 149 releases: every page is read (like the launcher), not just the first 100
        def rel(i):
            return {"tag_name": "test-1.18.%d-test.%d" % (i // 10, i), "prerelease": True,
                    "published_at": "2026-%02d-%02dT00:00:00Z" % (1 + i // 28, 1 + i % 28), "assets": []}
        all_rel = [rel(i) for i in range(149)][::-1]
        hits = []

        def route(path):
            q = dict(x.split("=") for x in path.split("?", 1)[1].split("&"))
            hits.append(q["page"])
            n, per = int(q["page"]), int(q["per_page"])
            return (200, all_rel[(n - 1) * per:n * per])
        fake = FakeHTTP({"/releases": route})
        try:
            p = self.new(releases_url=fake.url + "/releases")
            p.releases()
            self.assertTrue(wait_for(lambda: p.releases()["ok"] is True and not p.releases()["busy"]))
            r = p.releases()
            self.assertEqual((len(r["list"]), r["complete"], hits), (149, True, ["1", "2"]))
            self.assertEqual(r["list"][-1]["tag"], "test-1.18.0-test.0")
            # page 2 fails: the newest 100 plus the older ones of the last list, and a note why
            fake.routes["/releases"] = lambda path: (500, "x") if "page=2" in path else route(path)
            p.rel["etag"] = None
            self.assertEqual(p.releases(force=True)["refreshIn"], 30)
            self.assertTrue(wait_for(lambda: not p.releases()["busy"]))
            r = p.releases()
            self.assertEqual((len(r["list"]), r["complete"], r["ok"]), (149, False, True))
            self.assertIn("only the newest 100", r["error"])
            # a second Refresh within 30 s is not taken: the page shows when it is
            n = len(hits)
            self.assertGreater(p.releases(force=True)["refreshIn"], 0)
            time.sleep(0.2)
            self.assertEqual(len(hits), n)
            # page 1 answering 304 to its ETag: nothing new, the list stays
            p.rel.update(etag='"abc"', complete=True, forcedAt=-1e9)
            fake.routes["/releases"] = (304, "")
            p.releases(force=True)
            self.assertTrue(wait_for(lambda: not p.releases()["busy"]))
            self.assertEqual((len(p.releases()["list"]), p.releases()["ok"]), (149, True))
        finally:
            fake.close()

    def test_release_list_survives_a_restart(self):
        etags = []
        data = releases_fixture("https://example.invalid")

        def page(self_, n, etag=None):
            etags.append(etag)
            if etag == '"e1"':
                return None, None  # (304)
            return (data if n == 1 else []), '"e1"'
        now = [1000000.0]
        orig = play.Play._get_page
        play.Play._get_page = page
        try:
            p = self.new(clock=lambda: now[0])
            p.releases()
            self.assertTrue(wait_for(lambda: p.releases()["ok"] and not p.releases()["busy"]))
            n = len(p.releases()["list"])
            saved = util.read_json_file(self.paths.pm(play.RELEASES_FILE))
            self.assertEqual((len(saved["list"]), saved["etag"], saved["complete"]), (n, '"e1"', True))
            # a restart within 10 min: the list is there at once and GitHub isn't asked
            p = self.new(clock=lambda: now[0] + 60)
            r = p.releases()
            self.assertEqual((len(r["list"]), r["ok"], r["busy"]), (n, True, False))
            self.assertEqual(etags, [None])
            # later: asked with the ETag (304: the same list, a new time saved)
            now[0] += 3600
            p = self.new(clock=lambda: now[0])
            self.assertEqual(len(p.releases()["list"]), n)
            self.assertTrue(wait_for(lambda: not p.releases()["busy"]))
            self.assertEqual(etags, [None, '"e1"'])
            self.assertEqual(util.read_json_file(self.paths.pm(play.RELEASES_FILE))["fetchedTs"], now[0])
            # a tampered file: bad rows are dropped
            saved = util.read_json_file(self.paths.pm(play.RELEASES_FILE))
            saved["list"][0]["zip"]["url"] = "ftp://evil.example/x.zip"
            saved["list"][1]["tag"] = "../x"
            util.write_json_atomic(self.paths.pm(play.RELEASES_FILE), saved)
            self.assertEqual(len(self.new(clock=lambda: now[0]).releases()["list"]), n - 2)
        finally:
            play.Play._get_page = orig

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

    def test_edit_and_remove(self):
        p = self.play
        self.session("p20261024190000-aaaa")
        self.session("p20261024190500-bbbb", label="v1.18.3", version="1.18.3")
        n = p.save_note("p20261024190000-aaaa", {"did": "FFA", "noticed": "typo hree"})
        # before it is written: the local copy changes, the file gets the edited text
        e = p.edit_note(n["id"], {"did": "FFA", "noticed": "typo here"})
        self.assertIn("typo here", e["line"])
        self.assertTrue(p.edit_note(n["id"], {"did": "FFA", "noticed": "typo here"})["noop"])
        self.assertTrue(p.sync_docs()["pushed"])
        text = self.remote_file()
        self.assertIn("typo here", text)
        self.assertNotIn("hree", text)
        self.assertFalse(p.needs_sync())
        # after it is pushed, while still open: changed in place, one commit, same title and stamp
        p.edit_note(n["id"], {"did": "FFA on bastion", "noticed": "typo here"})
        self.assertTrue(p.needs_sync())
        self.assertEqual(p.view()["notes"][0]["where"], "your edit is waiting to go into Claude Documents")
        self.assertTrue(p.sync_docs()["pushed"])
        text = self.remote_file()
        self.assertIn("Did: FFA on bastion.", text)
        self.assertEqual(text.count("Play note: testing"), 1)
        self.assertIn("(edited)", git(self.docs, "log", "-1", "--format=%s"))
        self.assertFalse(p.needs_sync())
        self.assertEqual(len(p.notes()), 1)
        # remove a second note: its entry leaves the file
        n2 = p.save_note("p20261024190500-bbbb", {"change": "louder"})
        p.sync_docs()
        self.assertIn("v1.18.3", self.remote_file())
        p.delete_note(n2["id"])
        self.assertEqual([x["id"] for x in p.notes()], [n["id"]])
        self.assertTrue(p.sync_docs()["pushed"])
        self.assertNotIn("v1.18.3", self.remote_file())
        self.assertIn("Play note: testing", self.remote_file())
        with self.assertRaises(play.PlayError):
            p.delete_note(n2["id"])  # (gone)
        # an agent takes the first note: no more edits from here
        git(self.seed, "pull", "-q", "origin", "main")
        path = os.path.join(self.seed, play.DOCS_FILE)
        with open(path, encoding="utf-8") as f:
            write(path, f.read().replace(", q, open_", ", q, in-progress_"))
        git(self.seed, "commit", "-q", "-am", "on it")
        git(self.seed, "push", "-q", "origin", "HEAD:main")
        p.sync_docs()
        with self.assertRaises(play.PlayError) as cm:
            p.edit_note(n["id"], {"did": "late"})
        self.assertEqual(cm.exception.reason, "locked")
        self.assertIn("in-progress", cm.exception.message)
        self.assertEqual(p.view()["notes"][0]["locked"], "an agent set it to in-progress")

    def agent_sets(self, old, new):
        """An agent's clone changes the status of the entries (a text replace) and pushes."""
        git(self.seed, "pull", "-q", "origin", "main")
        path = os.path.join(self.seed, play.DOCS_FILE)
        with open(path, encoding="utf-8") as f:
            write(path, f.read().replace(old, new))
        git(self.seed, "commit", "-q", "-am", "agent")
        git(self.seed, "push", "-q", "origin", "HEAD:main")

    def test_removal_an_agent_blocked_is_shown(self):
        # the audit's case: an agent takes the entry on GitHub, q removes the note before this PC pulls
        p = self.play
        self.session("p20261024190000-aaaa")
        n = p.save_note("p20261024190000-aaaa", {"did": "FFA"})
        self.assertTrue(p.sync_docs()["pushed"])
        self.agent_sets(", q, open_", ", agent-thread, in-progress_")
        p.delete_note(n["id"])  # (the local copy still says open: accepted)
        v = p.view()
        self.assertEqual((v["notes"][0]["removed"], v["notes"][0]["where"]),
                         ("pending", "your removal is waiting to go into Claude Documents"))
        self.assertEqual(v["notesTotal"], 0)
        p.sync_docs()
        self.assertIn("Play note: testing", self.remote_file())  # (the agent has it: it stays in the file)
        v = p.view()
        self.assertEqual(len(v["notes"]), 1)  # (still on the page, saying so)
        self.assertEqual(v["notes"][0]["removed"], "blocked")
        self.assertEqual(v["notes"][0]["where"], "not removed: an agent set it to in-progress first")
        self.assertEqual([(b["id"], b["change"], b["why"]) for b in v["blocked"]],
                         [(n["id"], "removal", "an agent set it to in-progress")])
        # the agent sets it back to open: the removal goes in on the next sync
        self.agent_sets(", agent-thread, in-progress_", ", agent-thread, open_")
        p.sync_docs()
        self.assertNotIn("Play note: testing", self.remote_file())
        v = p.view()
        self.assertEqual((v["notes"], v["blocked"]), ([], []))

    def test_dismiss_blocked_edit(self):
        p = self.play
        self.session("p20261024190000-aaaa")
        n = p.save_note("p20261024190000-aaaa", {"did": "FFA"})
        p.sync_docs()
        self.agent_sets(", q, open_", ", q, done_")
        p.edit_note(n["id"], {"did": "FFA on bastion"})
        p.sync_docs()
        v = p.view()
        self.assertEqual(v["notes"][0]["where"], "your edit is not in the file: an agent set it to done first")
        self.assertEqual([b["change"] for b in v["blocked"]], ["edit"])
        with self.assertRaises(play.PlayError):
            p.dismiss_blocked("n20261024190000-zzzz")
        p.dismiss_blocked(n["id"])
        self.assertEqual(p.view()["blocked"], [])
        self.assertEqual(p.view()["notes"][0]["where"], "in Claude Documents")
        # dismissed: not tried again, even when the entry is open again
        self.agent_sets(", q, done_", ", q, open_")
        p.sync_docs()
        self.assertNotIn("FFA on bastion", self.remote_file())
        self.assertEqual(util.read_json_file(self.paths.pm(play.SYNC_FILE))["dismissed"], {n["id"]: 1})

    def test_no_upstream_is_not_up_to_date(self):
        p = self.play
        git(self.docs, "branch", "--unset-upstream")
        self.session("p20261024190000-aaaa")
        p.save_note("p20261024190000-aaaa", {"did": "FFA"})
        st = p.sync_docs()
        self.assertEqual((st["ok"], st["pushed"], st["unpushed"], st["noUpstream"]), (False, False, 1, True), st)
        self.assertIn("no upstream branch", st["error"])
        self.assertIsNone(self.remote_file())
        self.assertTrue(p.needs_sync())  # (tried again: fine once q sets the upstream)
        self.assertEqual(p.view()["notes"][0]["where"], "saved on this PC, not going to GitHub (no upstream branch)")
        git(self.docs, "branch", "-u", "origin/main")
        st = p.sync_docs()
        self.assertEqual((st["ok"], st["pushed"], st["noUpstream"]), (True, True, False), st)
        self.assertIn("Play note: testing", self.remote_file())
        self.assertFalse(p.needs_sync())

    def test_no_docs_folder(self):
        shutil.rmtree(self.docs)
        self.session("p20261024190000-aaaa")
        p = self.play
        p.save_note("p20261024190000-aaaa", {"did": "x"})
        st = p.sync_docs()
        self.assertFalse(st["ok"])
        self.assertIn("No Claude Documents folder", st["error"])
        self.assertEqual(len(p.notes()), 1)  # (kept on this PC)
        v = p.view()["notes"][0]
        self.assertEqual((v["status"], v["where"]), (None, "kept on this PC only (no Claude Documents folder)"))


@unittest.skipIf(util.IS_WINDOWS, "the fake quark.exe is a shell script")
class TestPlayServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.quark = os.path.join(cls.tmp, "Quark")
        write(os.path.join(cls.quark, "testing", "BUILD.txt"), "test-1.18.14-test.240 (GitHub release, published x)\n")
        fake_game(os.path.join(cls.quark, "testing"), seconds=0.2, code=0)
        paths = config.Paths(PM_ROOT, cls.quark)
        cls.opened = []
        cls.app = server.App(paths, port=0, modules=[], prober=lambda p: False,
                             play_opts={"releases_url": "http://127.0.0.1:9/releases", "opener": cls.opened.append})
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
        # the routes pm/play.py answers: the game's log, Stop, Open, editing and removing a note, Cancel
        code, body = self.req("/api/play/log?session=" + sid)
        self.assertEqual(code, 200, body)
        self.assertIn("started --host ffa", json.loads(body)["text"])
        self.assertEqual(self.req("/api/play/log?session=nope")[0], 404)
        self.assertEqual(self.req("/api/play/nothing")[0], 404)
        code, body = self.req("/api/play/stop", "POST", body={"session": sid})
        self.assertEqual((code, json.loads(body)["reason"]), (409, "not-running"))
        code, body = self.req("/api/play/open", "POST", body={"what": "build", "build": "testing"})
        self.assertEqual(code, 200, body)
        self.assertEqual(self.opened, [os.path.join(self.quark, "testing")])
        self.assertEqual(self.req("/api/play/open", "POST", body={"what": "path", "build": "/"})[0], 400)
        nid = self.app.play.notes()[0]["id"]
        code, body = self.req("/api/play/note-edit", "POST", body={"id": nid, "did": "a long FFA"})
        self.assertEqual(code, 200, body)
        self.assertEqual(self.app.play.notes()[0]["fields"]["did"], "a long FFA")
        self.assertEqual(self.req("/api/play/note-edit", "POST", body={"id": nid, "did": {"a": 1}})[0], 400)
        self.assertEqual(self.req("/api/play/install-cancel", "POST", body={"tag": "v1.0.0"})[0], 409)
        code, body = self.req("/api/play/note-delete", "POST", body={"id": nid})
        self.assertEqual(code, 200, body)
        self.assertEqual(self.app.play.notes(), [])
        # the release list is sent once; a poll that names its sig gets it left out
        v = json.loads(self.req("/api/play")[1])
        self.assertIsNone(json.loads(self.req("/api/play?rel=" + v["releases"]["sig"])[1])["releases"]["list"])


if __name__ == "__main__":
    unittest.main()
