"""Offline tests: python -m unittest test_quarkdata  (set QUARK_SERVER_BIN to also query a real local quark_server)."""
import asyncio
import os
import struct
import subprocess
import time
import unittest
from pathlib import Path

import quarkdata as qd
import bot

ROOT = Path(__file__).resolve().parent.parent.parent


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

    def test_sizes_match_worker(self):
        import re
        js = (ROOT / "tools/master/worker.js").read_text()
        block = re.search(r"QUEUE_SIZES = \{(.*?)\};", js, re.S).group(1)
        sizes = {k.strip('"'): int(v) for k, v in re.findall(r'("?[\w-]+"?): (\d+)', block)}
        self.assertEqual(sizes, {k: n for k, (_, n) in bot.QUEUES.items()})


@unittest.skipUnless(os.environ.get("QUARK_SERVER_BIN"), "set QUARK_SERVER_BIN=build/quark_server")
class LiveServer(unittest.TestCase):
    def test_query(self):
        p = subprocess.Popen([os.environ["QUARK_SERVER_BIN"], "--port", "27613", "--mode", "ffa", "--map", "harbor"],
                             cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
