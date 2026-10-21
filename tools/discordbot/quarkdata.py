"""Quark game data, no Discord: master server list, UDP server queries, ranked leaderboards,
GitHub releases/changelogs, and the in-game INFO pages parsed from src/client/game.cpp.

Real endpoints (mirrors the game's C++):
  * https://floration.github.io/quark/master.txt -> master base URL     (src/game/master.cpp kMasterUrlFile)
  * GET <master>/list?v=<VERSION>  -> "ip:port" per line, this version   (master.cpp fetchServerList)
  * UDP PKT_INFO_REQUEST to each ip:port -> PKT_INFO_REPLY               (src/game/protocol.h, server.cpp, netclient.cpp)
  * GET <master>/leaderboard?ladder=L&n=N -> "rating games name" lines   (src/game/ranked.cpp RankService::run)
  * GET <master>/raceboard?map=M&n=N      -> "ms name" lines
"""
from __future__ import annotations

import asyncio
import os
import re
import struct
import time
from dataclasses import dataclass, field

import aiohttp

MASTER_URL_FILE = "https://floration.github.io/quark/master.txt"
GITHUB_REPO = "floration/quark"
RAW_BASE = f"https://raw.githubusercontent.com/{GITHUB_REPO}"
SITE_URL = "https://floration.github.io/quark"
DISCORD_INVITE = "https://discord.gg/RERYWNWSXg"

PROTOCOL_MAGIC = 0x51524B31   # "QRK1", gamedefs.h kProtocolMagic
PKT_INFO_REQUEST = 4           # protocol.h: PKT_CONNECT=1, INPUT, DISCONNECT, INFO_REQUEST
PKT_INFO_REPLY = 67            # protocol.h: PKT_ACCEPT=64, REJECT, SNAPSHOT, INFO_REPLY

# gamedefs.h modeName / modeTitle (index = GameModeId)
MODE_NAMES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"]
MODE_TITLES = ["VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL", "1V1",
               "TOURNEY", "HIDE AND SEEK", "TAG", "MULTI POINT"]
# gamedefs.h arenaVariantRanked: 0 vanilla (1v1), 1 vampire (1v1), 5 instagib (FFA, 5 min)
RANKED_LADDERS = [("vanilla", "RANKED VANILLA"), ("vampire", "RANKED LASER VAMPIRE"), ("instagib", "RANKED INSTAGIB")]
RACE_MAP = "viaduct"  # TIME TRIAL is the ranked race on Viaduct (game.cpp INFO page)


def mode_title(m: int) -> str:
    return MODE_TITLES[m] if 0 <= m < len(MODE_TITLES) else "?"


# ---------------------------------------------------------------- UDP server query
def build_info_request() -> bytes:
    return struct.pack("<IB", PROTOCOL_MAGIC, PKT_INFO_REQUEST)


@dataclass
class ServerInfo:
    address: str
    name: str = ""
    map: str = ""
    mode: int = 0
    players: int = 0
    max_players: int = 0
    port: int = 0
    ping_ms: float = 0.0
    replied: bool = False

    @property
    def mode_title(self) -> str:
        return mode_title(self.mode)


def parse_info_reply(data: bytes, address: str = "") -> ServerInfo | None:
    """PKT_INFO_REPLY: u32 magic, u8 type, str name, str map, u8 mode, u8 players, u8 max, u16 port (LE, str = u8 len + bytes)."""
    try:
        magic, typ = struct.unpack_from("<IB", data, 0)
        if magic != PROTOCOL_MAGIC or typ != PKT_INFO_REPLY:
            return None
        off = 5
        strs = []
        for _ in range(2):
            n = data[off]; off += 1
            if off + n > len(data):
                return None
            strs.append(data[off:off + n].decode("latin-1")); off += n
        mode, players, maxp, port = struct.unpack_from("<BBBH", data, off)
        return ServerInfo(address, strs[0], strs[1], mode, players, maxp, port, replied=True)
    except (struct.error, IndexError):
        return None


class _QueryProto(asyncio.DatagramProtocol):
    def __init__(self, results: dict, sent: dict):
        self.results, self.sent = results, sent

    def datagram_received(self, data, addr):
        key = f"{addr[0]}:{addr[1]}"
        info = parse_info_reply(data, key)
        if info and key in self.sent and key not in self.results:
            info.ping_ms = (time.monotonic() - self.sent[key]) * 1000
            self.results[key] = info

    def error_received(self, exc):
        pass


async def query_servers(addresses: list[str], timeout: float = 1.5, tries: int = 2) -> list[ServerInfo]:
    """Ask every ip:port over UDP (like ServerBrowser); servers that never answer come back with replied=False."""
    loop = asyncio.get_running_loop()
    results: dict[str, ServerInfo] = {}
    sent: dict[str, float] = {}
    transport, _ = await loop.create_datagram_endpoint(lambda: _QueryProto(results, sent), local_addr=("0.0.0.0", 0))
    try:
        targets = []
        for a in addresses:
            host, _, port = a.rpartition(":")
            if host and port.isdigit():
                targets.append((a, host, int(port)))
        for _ in range(tries):
            for key, host, port in targets:
                if key in results:
                    continue
                sent.setdefault(key, time.monotonic())
                try:
                    transport.sendto(build_info_request(), (host, port))
                except OSError:
                    pass
            await asyncio.sleep(timeout / tries)
            if len(results) == len(targets):
                break
    finally:
        transport.close()
    return [results.get(k) or ServerInfo(k) for k, _, _ in targets]


# ---------------------------------------------------------------- HTTP data
def parse_server_list(body: str) -> list[str]:
    out = []
    for line in body.splitlines():
        line = line.strip()
        if line and len(line) < 32 and ":" in line:
            out.append(line)
    return out[:256]


@dataclass
class LeaderRow:
    rating: int
    games: int | None
    name: str


def parse_leaderboard(body: str, race: bool = False) -> list[LeaderRow]:
    rows = []
    for line in body.splitlines():
        parts = line.strip().split(" ", 1 if race else 2)
        try:
            if race:
                rows.append(LeaderRow(int(parts[0]), None, parts[1] if len(parts) > 1 else "?"))
            else:
                rows.append(LeaderRow(int(float(parts[0])), int(parts[1]), parts[2] if len(parts) > 2 else "?"))
        except (ValueError, IndexError):
            continue
    return rows


def fmt_race_time(ms: int) -> str:
    return f"{ms // 60000}:{ms // 1000 % 60:02d}.{ms % 1000:03d}"


def format_board(title: str, rows: list[LeaderRow], race: bool = False, n: int = 10) -> str:
    lines = [title, "-" * max(len(title), 34)]
    if not rows:
        lines.append("  no ranked players yet")
    for i, r in enumerate(rows[:n], 1):
        name = r.name[:20]
        if race:
            lines.append(f"{i:>2}. {name:<20} {fmt_race_time(r.rating):>10}")
        else:
            lines.append(f"{i:>2}. {name:<20} {r.rating:>5}  {r.games:>4}g")
    return "\n".join(lines)


class QuarkAPI:
    def __init__(self, session: aiohttp.ClientSession, github_token: str | None = None, master_override: str | None = None):
        self.s = session
        self.token = github_token
        self.master_override = master_override or os.environ.get("QUARK_MASTER")
        self._master: str | None = None
        self._master_at = 0.0

    async def _text(self, url: str, timeout: float = 8, headers: dict | None = None) -> str:
        async with self.s.get(url, timeout=aiohttp.ClientTimeout(total=timeout), headers=headers) as r:
            r.raise_for_status()
            return await r.text()

    async def master(self) -> str:
        if self.master_override:
            return self.master_override.strip().rstrip("/")
        if not self._master or time.time() - self._master_at > 3600:  # re-read hourly: the site can move it
            body = (await self._text(MASTER_URL_FILE)).strip()
            if not body.startswith("https://") or any(c in body for c in " '\"\\\n"):
                raise ValueError(f"bad master.txt: {body[:80]!r}")
            self._master, self._master_at = body.rstrip("/"), time.time()
        return self._master

    async def server_addresses(self, version: str) -> list[str]:
        return parse_server_list(await self._text(f"{await self.master()}/list?v={version}", 6))

    async def live_servers(self, version: str) -> list[ServerInfo]:
        return await query_servers(await self.server_addresses(version))

    async def leaderboard(self, ladder: str, n: int = 10) -> list[LeaderRow]:
        return parse_leaderboard(await self._text(f"{await self.master()}/leaderboard?ladder={ladder}&n={n}"))

    async def raceboard(self, map_name: str = RACE_MAP, n: int = 10) -> list[LeaderRow]:
        return parse_leaderboard(await self._text(f"{await self.master()}/raceboard?map={map_name}&n={n}"), race=True)

    async def leaderboard_text(self, n: int = 10) -> str:
        blocks = []
        for ladder, title in RANKED_LADDERS:
            try:
                blocks.append(format_board(title, await self.leaderboard(ladder, n)))
            except Exception as e:  # one ladder failing doesn't blank the others
                blocks.append(f"{title}\n  unavailable ({type(e).__name__})")
        try:
            blocks.append(format_board(f"TIME TRIAL ({RACE_MAP.upper()})", await self.raceboard(RACE_MAP, n), race=True))
        except Exception as e:
            blocks.append(f"TIME TRIAL\n  unavailable ({type(e).__name__})")
        return "\n\n".join(blocks)

    def _gh_headers(self) -> dict:
        h = {"Accept": "application/vnd.github+json", "User-Agent": "quark-discord-bot"}
        if self.token:
            h["Authorization"] = f"Bearer {self.token}"
        return h

    async def latest_release(self) -> dict:
        async with self.s.get(f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest", headers=self._gh_headers(),
                              timeout=aiohttp.ClientTimeout(total=10)) as r:
            r.raise_for_status()
            return await r.json()

    async def changelog(self, version: str | None = None) -> tuple[str, str]:
        """(version, markdown) from changelogs/<version>.md on GitHub; latest release when version is None."""
        if not version:
            rel = await self.latest_release()
            version = rel["tag_name"].lstrip("v")
            if rel.get("body"):
                return version, rel["body"]  # the release body is changelogs/<VERSION>.md
        version = version.lstrip("v")
        if not re.fullmatch(r"\d+\.\d+\.\d+", version):
            raise ValueError("version must look like 1.2.3")
        async with self.s.get(f"https://api.github.com/repos/{GITHUB_REPO}/releases/tags/v{version}", headers=self._gh_headers(),
                              timeout=aiohttp.ClientTimeout(total=10)) as r:
            if r.status == 200:
                body = (await r.json()).get("body") or ""
                if body:
                    return version, body
        for ref in (f"v{version}", "HEAD"):  # fall back to the file in the repo
            try:
                return version, await self._text(f"{RAW_BASE}/{ref}/changelogs/{version}.md")
            except aiohttp.ClientResponseError:
                continue
        raise LookupError(f"no changelog for {version}")

    async def game_source(self, local_path: str | None, ref: str = "HEAD") -> str:
        if local_path and os.path.exists(local_path):
            with open(local_path, encoding="utf-8") as f:
                return f.read()
        return await self._text(f"{RAW_BASE}/{ref}/src/client/game.cpp", 20)


# ---------------------------------------------------------------- INFO pages from game.cpp
def _c_strings(block: str) -> list[str]:
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', block):
        out.append(bytes(m.group(1), "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8", "replace"))
    return out


def _strip_comments(src: str) -> str:
    # remove // comments that are outside string literals
    res = []
    for line in src.splitlines():
        in_str, i, cut = False, 0, len(line)
        while i < len(line):
            c = line[i]
            if c == "\\" and in_str:
                i += 2; continue
            if c == '"':
                in_str = not in_str
            elif not in_str and line.startswith("//", i):
                cut = i; break
            i += 1
        res.append(line[:cut])
    return "\n".join(res)


def parse_info_pages(src: str) -> list[tuple[str, list[str]]]:
    """[(tab title, lines)] from kInfoTabs / kInfoPages; lines starting '# ' are headings (as drawInfo treats them)."""
    m = re.search(r"kInfoTabs\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise ValueError("kInfoTabs not found")
    tabs = _c_strings(m.group(1))
    start = src.find("{", src.index("kInfoPages[]"))
    body = _strip_comments(src[start:])
    pages, depth, cur = [], 0, None
    i = 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            if depth == 2 and cur is not None:
                cur.append(body[i:j + 1])
            i = j + 1; continue
        if c == "{":
            depth += 1
            if depth == 2:
                cur = []
        elif c == "}":
            if depth == 2:
                pages.append(_c_strings("\n".join(cur)) if cur else [])
                cur = None
            depth -= 1
            if depth == 0:
                break
        i += 1
    # adjacent literals ("a" "b") inside one entry would be split; game.cpp uses one literal per line
    return list(zip(tabs, pages))


def info_markdown(pages: list[tuple[str, list[str]]]) -> list[tuple[str, str]]:
    out = []
    for tab, lines in pages:
        md = []
        for line in lines:
            if line.startswith("# "):
                md.append(f"\n**{line[2:]}**")
            else:
                md.append(f"- {line}")
        out.append((tab, "\n".join(md).strip()))
    return out


INSTALL_TEXT = f"""**Install / update**
- Download the launcher for your platform from the latest release: https://github.com/{GITHUB_REPO}/releases/latest
  (Windows .exe, Linux, macOS). The launcher installs the game and keeps it updated automatically.
- Website: {SITE_URL}
- Build from source: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` (keep `maps/` next to the executables).

**Host a server**
- In game: Host Game -> pick map / mode / port -> Start. "Auto port forward (UPnP)" opens the UDP port; tick public to appear on the internet server list.
- Dedicated: `quark_server --mode bomb --map depot --port 27500 --upnp --public --name "my server"`.
- 24/7 Linux server with auto-updates: `tools/server/install.sh --public --name "my server"` (systemd service + update timer).
- If UPnP fails, forward **UDP 27500** (or your port) to the host machine.

**Join**: Join Game -> pick a server from the internet list, or enter `ip:port`."""


def split_message(text: str, limit: int = 1990) -> list[str]:
    """Split on line boundaries to fit Discord's 2000 char limit (hard-splits overlong lines)."""
    chunks, cur = [], ""
    for line in text.split("\n"):
        while len(line) > limit:
            if cur:
                chunks.append(cur); cur = ""
            chunks.append(line[:limit]); line = line[limit:]
        if len(cur) + len(line) + 1 > limit:
            chunks.append(cur); cur = line
        else:
            cur = f"{cur}\n{line}" if cur else line
    if cur.strip():
        chunks.append(cur)
    return chunks


def slugify(name: str, maxlen: int = 80) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return (s or "server")[:maxlen].strip("-") or "server"
