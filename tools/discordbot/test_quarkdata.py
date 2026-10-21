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


class Parsing(unittest.TestCase):
    def test_request(self):
        self.assertEqual(qd.build_info_request(), bytes([0x31, 0x4B, 0x52, 0x51, 4]))

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
