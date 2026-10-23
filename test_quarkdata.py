"""Offline tests: python -m unittest test_quarkdata  (set QUARK_SERVER_BIN to also query a real local quark_server).

Self-contained: the tests that compare against the game's own files (game.cpp INFO pages, worker.js queue sizes)
run when a game checkout is found (QUARK_REPO, or the repo this folder sits in) and are skipped otherwise.
"""
import ast
import asyncio
import os
import struct
import subprocess
import time
import unittest
from pathlib import Path

import quarkdata as qd
import bot

HERE = Path(__file__).resolve().parent
ROOT = bot.GAME_REPO   # None outside the game repo


def server_info(addr, players):
    return qd.ServerInfo(addr, addr, players=players, max_players=8, replied=True)


SAMPLE_GAME_CPP = r'''
static const char* kInfoTabs[] = {"MODES", "CONTROLS"};
static const std::vector<const char*> kInfoPages[] = {
    {   // MODES
        "# VS", "two teams", // a comment with "quotes"
        "a line with \"escapes\"",
    },
    {"# MOVE", "WASD"},
};
'''


def reply(name, map_, mode, players, maxp, port):
    b = struct.pack("<IB", qd.PROTOCOL_MAGIC, qd.PKT_INFO_REPLY)
    for s in (name, map_):
        b += bytes([len(s)]) + s.encode()
    return b + struct.pack("<BBBH", mode, players, maxp, port)


class Safety(unittest.TestCase):
    """Server and player names go into Discord messages (REVIEW BOT-1, BOT-2)."""
    def test_safe_text(self):
        s = qd.safe_text("@everyone [free](https://evil.example) **x** <@123> `y`\nline2 discord.gg/abc")
        self.assertNotIn("@everyone", s)
        self.assertNotIn("https://", s)
        self.assertNotIn("discord.gg", s)
        self.assertNotIn("\n", s)
        for md in ("**", "[free]", "<@", "`"):
            self.assertNotIn(md, s.replace("\\" + md[0], ""))
        self.assertEqual(qd.safe_text("Im Quarking It"), "Im Quarking It")
        self.assertEqual(len(qd.safe_text("x" * 500, 64)), 64)

    def test_board_name(self):
        board = qd.format_board("T", [qd.LeaderRow(100, 3, "a```\n@everyone")])
        self.assertNotIn("```", board)
        self.assertEqual(len(board.split("\n")), 3)

    def test_no_default_mentions(self):
        src = (Path(__file__).resolve().parent / "bot.py").read_text(encoding="utf-8")
        self.assertIn("allowed_mentions=discord.AllowedMentions.none()", src)


class Parsing(unittest.TestCase):
    def test_request(self):
        self.assertEqual(qd.build_info_request(), bytes([0x31, 0x4B, 0x52, 0x51, 4]) + bytes(qd.INFO_REQUEST_BYTES - 5))

    def test_reply(self):
        i = qd.parse_info_reply(reply("Im Quarking It", "harbor", 5, 3, 8, 27500), "1.2.3.4:27500")
        self.assertEqual((i.name, i.map, i.mode_title, i.players, i.max_players, i.port), ("Im Quarking It", "harbor", "FREE FOR ALL", 3, 8, 27500))
        self.assertIsNone(qd.parse_info_reply(reply("a", "b", 0, 0, 0, 1)[:-3]))
        self.assertIsNone(qd.parse_info_reply(b"junk"))

    def test_list(self):
        self.assertEqual(qd.parse_server_list("1.2.3.4:27500\n\n5.6.7.8:1\n"), ["1.2.3.4:27500", "5.6.7.8:1"])

    def test_boards(self):
        rows = qd.parse_leaderboard("336 3 ACE 1\n0 3 Kev\nbad\n")
        self.assertEqual([(r.rating, r.games, r.name) for r in rows], [(336, 3, "ACE 1"), (0, 3, "Kev")])
        race = qd.parse_leaderboard("31435 cocaine al gaib\n", race=True)
        self.assertEqual(qd.fmt_race_time(race[0].rating), "0:31.435")
        self.assertIn("ACE 1", qd.format_board("X", rows))

    def test_names(self):
        i = qd.ServerInfo("a", "I'm Quarking It #3!", players=3, max_players=8)
        self.assertEqual(bot.server_channel_name(i), "i-m-quarking-it-3-3-8")
        self.assertEqual(bot.total_name(13), "🟢 13 players live")

    def test_fill(self):
        self.assertEqual(bot.fill_message(["@a", "@b"], "ranked-vampire"),
                         "@a and @b your queue for RANKED LASER VAMPIRE has been filled, please proceed to a server and begin the game")
        self.assertTrue(bot.fill_message(["@a", "@b", "@c", "@d"], "ranked-instagib").startswith("@a, @b, @c and @d your"))

    def test_split(self):
        text = "\n".join("x" * 300 for _ in range(20)) + "\n" + "y" * 5000
        parts = qd.split_message(text)
        self.assertTrue(all(len(p) <= 1990 for p in parts))
        self.assertEqual("".join(parts).replace("\n", ""), text.replace("\n", ""))

    def test_info_pages_sample(self):
        pages = qd.parse_info_pages(SAMPLE_GAME_CPP)
        self.assertEqual(pages, [("MODES", ["# VS", "two teams", 'a line with "escapes"']), ("CONTROLS", ["# MOVE", "WASD"])])
        self.assertTrue(all(len(m) <= 2000 for m in bot.info_messages(pages)))

    @unittest.skipUnless(ROOT, "no game checkout (set QUARK_REPO)")
    def test_info_pages(self):
        pages = qd.parse_info_pages((ROOT / "src/client/game.cpp").read_text())
        self.assertEqual([t for t, _ in pages][:2], ["MODES", "CONTROLS"])
        self.assertTrue(all(lines for _, lines in pages))
        self.assertTrue(all(len(m) <= 2000 for m in bot.info_messages(pages)))


class MasterQueues(unittest.TestCase):
    def test_link_code(self):
        self.assertEqual(qd.parse_link_code("code 012345\n"), "012345")
        self.assertEqual(qd.parse_link_code("bad request"), "")
        self.assertEqual(qd.parse_link_code("code 12345"), "")

    def test_hex(self):
        self.assertEqual(qd.from_hex(qd.to_hex("Kev")), "Kev")
        self.assertEqual(qd.from_hex("-"), "")

    def test_crowns(self):
        h = qd.to_hex
        body = "\n".join([f"vanilla 1 {'a'*32} {h('Kev')} {h('kevin')}", f"vampire 5 {'a'*32} {h('Kev')} {h('kevin')}",
                          f"instagib 2 {'b'*32} {h('Ace')} {h('ACE_d')}", f"race:viaduct 1 {'c'*32} {h('nolink')} -", "bad line"])
        cr = qd.parse_crowns(body)
        self.assertEqual(len(cr), 4)
        self.assertEqual((cr[3].board, cr[3].place, cr[3].name, cr[3].discord), ("race:viaduct", 1, "nolink", ""))
        self.assertEqual(qd.crown_roles(cr), {"kevin": "first", "ace_d": "top10"})
        # #5 seen before #1 still ends up first
        self.assertEqual(qd.crown_roles(list(reversed(cr[:2]))), {"kevin": "first"})

    def test_filled(self):
        h = qd.to_hex
        body = f"7 ranked-vampire 1700000000000 d:alice:{h('Alice')}:{h('alice')},{'f'*32}:{h('Bob')}:-\nnot a row"
        f = qd.parse_filled(body)
        self.assertEqual(len(f), 1)
        self.assertEqual((f[0].id, f[0].mode, f[0].created), (7, "ranked-vampire", 1700000000000))
        a, b = f[0].members
        self.assertTrue(a.from_discord); self.assertEqual((a.who, a.name, a.discord), ("d:alice", "Alice", "alice"))
        self.assertFalse(b.from_discord); self.assertEqual((b.name, b.discord), ("Bob", ""))
        self.assertEqual(bot.fill_message(["<@1>", "**Bob**"], "ranked-vampire"),
                         "<@1> and **Bob** your queue for RANKED LASER VAMPIRE has been filled, please proceed to a server and begin the game")

    def test_queue_counts(self):
        c = qd.parse_queue_counts("ranked-vanilla 1 2\nffa 3 4\nquark server list")
        self.assertEqual((c["ffa"].count, c["ffa"].size), (3, 4))
        self.assertEqual(len(c), 2)

    def test_not_deployed(self):
        with self.assertRaises(qd.EndpointMissing):
            qd._check_deployed("quark server list\n")

    @unittest.skipUnless(ROOT and (ROOT / "tools/master/worker.js").is_file(), "no game checkout (set QUARK_REPO)")
    def test_sizes_match_worker(self):
        import re
        js = (ROOT / "tools/master/worker.js").read_text()
        block = re.search(r"QUEUE_SIZES = \{(.*?)\};", js, re.S).group(1)
        sizes = {k.strip('"'): int(v) for k, v in re.findall(r'("?[\w-]+"?): (\d+)', block)}
        self.assertEqual(sizes, {k: n for k, (_, n) in bot.QUEUES.items()})


class Releases(unittest.TestCase):
    """Testing builds are GitHub prereleases (test-1.2.3-test.4); they must never be announced as production."""
    def test_kind(self):
        k = bot.release_kind
        self.assertEqual(k({"tag_name": "v1.17.0"}), "production")
        self.assertEqual(k({"tag_name": "v1.17.0", "prerelease": False, "draft": False}), "production")
        self.assertEqual(k({"tag_name": "v1.17.0-test.3", "prerelease": True}), "testing")
        self.assertEqual(k({"tag_name": "v1.17.0", "prerelease": True}), "testing")
        self.assertIsNone(k({"tag_name": "v1.17.0-test.3", "prerelease": False}))   # suffixed tag but not a prerelease: neither
        self.assertIsNone(k({"tag_name": "v1.17.0", "draft": True}))
        self.assertIsNone(k({"tag_name": "v1.17.0-test.3", "prerelease": True, "draft": True}))
        self.assertIsNone(k({}))

    def test_newest_testing(self):
        rels = [{"tag_name": "v1.17.0"}, {"tag_name": "v1.17.1-test.2", "prerelease": True},
                {"tag_name": "v1.17.1-test.1", "prerelease": True}]
        self.assertEqual(bot.newest_testing(rels)["tag_name"], "v1.17.1-test.2")
        self.assertIsNone(bot.newest_testing(rels[:1]))

    def test_versions(self):
        self.assertTrue(qd.VERSION_RE.fullmatch("1.17.0") and qd.VERSION_RE.fullmatch("1.17.0-test.12"))
        self.assertFalse(qd.VERSION_RE.fullmatch("1.17.0-beta") or qd.VERSION_RE.fullmatch("../x"))

    def test_full_changelog_link(self):   # production versions link the website's full changelog page; testing builds don't
        self.assertEqual(qd.full_changelog_url("1.17.0"), "https://floration.github.io/quark/updates/1.17.0/")
        self.assertEqual(qd.full_changelog_url("v1.17.0"), "https://floration.github.io/quark/updates/1.17.0/")
        self.assertIsNone(qd.full_changelog_url("1.17.0-test.3"))
        self.assertIsNone(qd.full_changelog_url("test-1.17.0-test.3"))
        self.assertIsNone(qd.full_changelog_url("nonsense"))
        self.assertTrue(qd.with_full_link("# 1.17.0\n- a\n", "v1.17.0").endswith("(https://floration.github.io/quark/updates/1.17.0/)"))
        self.assertEqual(qd.with_full_link("x", "1.17.0-test.3"), "x")

    def test_changelog_lookup(self):
        L = qd.changelog_lookup
        self.assertEqual(L("1.17.0"), ("1.17.0", "v1.17.0", "1.17.0", False))
        self.assertEqual(L("v1.17.0"), ("1.17.0", "v1.17.0", "1.17.0", False))
        # testing builds: tag test-<VERSION>-test.N (release.yml), notes in the base version's changelog
        self.assertEqual(L("1.17.1-test.4"), ("1.17.1-test.4", "test-1.17.1-test.4", "1.17.1", True))
        self.assertEqual(L("test-1.17.1-test.4"), ("1.17.1-test.4", "test-1.17.1-test.4", "1.17.1", True))
        self.assertEqual(L(" v1.17.1-test.4 "), ("1.17.1-test.4", "test-1.17.1-test.4", "1.17.1", True))
        for bad in ("1.17", "test-", "1.17.0-beta", "../x", "1.17.0/../../y", "vv1.17.0"):
            with self.assertRaises(ValueError):
                L(bad)

    def test_changelog_testing(self):
        class Fake(qd.QuarkAPI):
            def __init__(self, releases, files):
                super().__init__(None)
                self.releases_, self.files, self.asked = releases, files, []

            async def release_by_tag(self, tag):
                self.asked.append(tag)
                return self.releases_.get(tag)

            async def _text(self, url, timeout=8, headers=None):
                self.asked.append(url)
                if url in self.files:
                    return self.files[url]
                raise qd.aiohttp.ClientResponseError(None, (), status=404)

        raw = qd.RAW_BASE
        api = Fake({"test-1.17.1-test.4": {"body": "**Testing build** 1.17.1-test.4"}}, {})
        self.assertEqual(asyncio.run(api.changelog("1.17.1-test.4")), ("1.17.1-test.4 (testing)", "**Testing build** 1.17.1-test.4"))
        # no release body: the base version's file, at the testing tag first
        api = Fake({}, {f"{raw}/HEAD/changelogs/1.17.1.md": "notes"})
        self.assertEqual(asyncio.run(api.changelog("test-1.17.1-test.5")), ("1.17.1-test.5 (testing)", "notes"))
        self.assertEqual(api.asked, ["test-1.17.1-test.5", f"{raw}/test-1.17.1-test.5/changelogs/1.17.1.md",
                                     f"{raw}/HEAD/changelogs/1.17.1.md"])
        api = Fake({"v1.17.0": {"body": "prod"}}, {})
        self.assertEqual(asyncio.run(api.changelog("v1.17.0")), ("1.17.0", "prod"))
        with self.assertRaises(LookupError):
            asyncio.run(Fake({}, {}).changelog("1.2.3"))

    def test_config(self):
        cfg = bot.DEFAULT_CONFIG
        self.assertNotEqual(cfg["testing_channel"], cfg["updates_channel"])
        self.assertNotEqual(cfg["testing_role"], cfg["update_role"])


class PinnedMaster(unittest.TestCase):
    """The bot sends BOT_KEY to the Worker: it uses the configured or built-in address, never master.txt."""
    def test_default_and_override(self):
        old = os.environ.pop("QUARK_MASTER", None)
        try:
            self.assertEqual(asyncio.run(qd.QuarkAPI(None).master()), qd.MASTER_URL)
            self.assertEqual(asyncio.run(qd.QuarkAPI(None, None, "https://w.example.dev/").master()), "https://w.example.dev")
            os.environ["QUARK_MASTER"] = "http://127.0.0.1:8787"
            self.assertEqual(asyncio.run(qd.QuarkAPI(None).master()), "http://127.0.0.1:8787")
        finally:
            os.environ.pop("QUARK_MASTER", None)
            if old is not None:
                os.environ["QUARK_MASTER"] = old
        for bad in ("http://w.example.dev", "ftp://x", "https://a b", "https://x/'y", "javascript:alert(1)", ""):
            with self.assertRaises(ValueError):
                qd.QuarkAPI(None, None, bad or " ")

    def test_never_fetches_master_txt(self):
        src = (HERE / "quarkdata.py").read_text(encoding="utf-8") + (HERE / "bot.py").read_text(encoding="utf-8")
        self.assertNotIn("floration.github.io/quark/master.txt", src)
        self.assertTrue(qd.MASTER_URL.startswith("https://"))


class LiveListing(unittest.TestCase):
    """Owner: at most 5 servers listed; one needs MIN_SEEN_SECONDS of answers before it gets channels (BOT-3)."""
    def test_limit_and_uptime(self):
        now = 1000.0
        infos = [server_info(f"10.0.0.{k}:1", k) for k in range(12)]
        seen = {i.address: now - 500 for i in infos}
        seen["10.0.0.11:1"] = now - 10   # most players but only just appeared
        got = bot.pick_listed(infos, set(), seen, now)
        self.assertEqual(len(got), bot.MAX_LIVE_SERVERS)
        self.assertEqual([i.players for i in got], [10, 9, 8, 7, 6])

    def test_listed_stay(self):
        now = 1000.0
        infos = [server_info(f"10.0.0.{k}:1", k) for k in range(8)]
        seen = {i.address: 0 for i in infos}
        got = bot.pick_listed(infos, {"10.0.0.0:1", "10.0.0.1:1"}, seen, now, limit=3)
        self.assertEqual([i.address for i in got], ["10.0.0.0:1", "10.0.0.1:1", "10.0.0.7:1"])
        # more already listed than the limit (old config): trimmed, the rest get removed by the loop
        self.assertEqual(len(bot.pick_listed(infos, {i.address for i in infos}, seen, now, limit=5)), 5)

    def test_top(self):
        infos = [server_info(f"10.0.0.{k}:1", k % 4) for k in range(9)]
        top = bot.top_servers(infos)
        self.assertEqual(len(top), 5)
        self.assertEqual(top[0].players, 3)


class SelfContained(unittest.TestCase):
    """The bot is moving to its own private repo: it may only import the stdlib, its deps and itself."""
    ALLOWED = {"discord", "aiohttp", "quarkdata", "bot", "__future__"}

    def test_imports(self):
        import sys
        for f in ("bot.py", "quarkdata.py"):
            tree = ast.parse((HERE / f).read_text(encoding="utf-8"))
            for n in ast.walk(tree):
                mods = [a.name for a in n.names] if isinstance(n, ast.Import) else [n.module or ""] if isinstance(n, ast.ImportFrom) else []
                for m in mods:
                    top = m.split(".")[0]
                    self.assertTrue(top in self.ALLOWED or top in getattr(sys, "stdlib_module_names", {top}), f"{f} imports {m}")

    def test_pinned_with_hashes(self):
        reqs = (HERE / "requirements.txt").read_text()
        pins = [l for l in reqs.splitlines() if l and not l[0].isspace() and not l.startswith("#")]
        self.assertTrue(pins)
        for l in pins:
            self.assertIn("==", l)
            self.assertTrue(l.rstrip().endswith("\\"), f"{l}: no --hash lines")   # pip install --require-hashes


@unittest.skipUnless(os.environ.get("QUARK_SERVER_BIN"), "set QUARK_SERVER_BIN=build/quark_server")
class LiveServer(unittest.TestCase):
    def test_query(self):
        p = subprocess.Popen([os.environ["QUARK_SERVER_BIN"], "--port", "27613", "--mode", "ffa", "--map", "harbor"],
                             cwd=ROOT or Path(os.environ["QUARK_SERVER_BIN"]).resolve().parent.parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1.5)
            res = asyncio.run(qd.query_servers(["127.0.0.1:27613", "127.0.0.1:27614"]))
            self.assertTrue(res[0].replied)
            self.assertEqual((res[0].map, res[0].mode_title, res[0].port), ("harbor", "FREE FOR ALL", 27613))
            self.assertFalse(res[1].replied)
        finally:
            p.terminate(); p.wait()


if __name__ == "__main__":
    unittest.main()
