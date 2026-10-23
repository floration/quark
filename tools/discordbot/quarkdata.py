"""Quark game data, no Discord: master server list, UDP server queries, ranked leaderboards,
GitHub releases/changelogs, and the in-game INFO pages parsed from src/client/game.cpp.

Real endpoints (mirrors the game's C++):
  * MASTER_URL (the game's built-in Worker address, src/game/master.cpp kPinnedMasterUrl) -> master base URL
  * GET <master>/list?v=<VERSION>  -> "ip:port" per line, this version   (master.cpp fetchServerList)
  * UDP PKT_INFO_REQUEST to each ip:port -> PKT_INFO_REPLY               (src/game/protocol.h, server.cpp, netclient.cpp)
  * GET <master>/leaderboard?ladder=L&n=N -> "rating games name" lines   (src/game/ranked.cpp RankService::run)
  * GET <master>/raceboard?map=M&n=N      -> "ms name" lines
  * GitHub releases: normal release = production (tag v1.2.3), prerelease = testing build (tag test-1.2.3-test.4,
    version 1.2.3-test.4; its changelog is changelogs/1.2.3.md, the base version's: release.yml)

Self-contained: nothing here imports or reads the game's repository. The constants below are COPIES of the game's
(protocol and mode tables). While the bot lives in the game repo, tests/tests.cpp (cross-language constants) checks
them against the C++; once it has its own repo, re-copy them by hand when the game changes them (MOVING.md).
"""
from __future__ import annotations

import asyncio
import os
import re
import struct
import time
from dataclasses import dataclass, field

import aiohttp

# The ranking / server-list Worker: a COPY of the game's built-in kPinnedMasterUrl (src/game/master.cpp; quark_tests
# "cross-language constants" checks it). Pinned like the game, and stricter: the bot sends BOT_KEY there, so it never
# follows the website's master.txt (the game only follows a release-key-signed one; the bot doesn't verify it). If the
# owner moves the Worker, the bot's operator sets master_url in config.json (or QUARK_MASTER) to the new address.
MASTER_URL = "https://quark-master.jamesrandallmcamis.workers.dev"
GITHUB_REPO = "floration/quark"
RAW_BASE = f"https://raw.githubusercontent.com/{GITHUB_REPO}"
SITE_URL = "https://floration.github.io/quark"
DISCORD_INVITE = "https://discord.gg/RERYWNWSXg"

# ---- copied from the game (src/game/gamedefs.h, src/game/protocol.h): keep in sync
PROTOCOL_MAGIC = 0x51524B31   # "QRK1", gamedefs.h kProtocolMagic
PKT_INFO_REQUEST = 4           # protocol.h: PKT_CONNECT=1, INPUT, DISCONNECT, INFO_REQUEST
PKT_INFO_REPLY = 67            # protocol.h: PKT_ACCEPT=64, REJECT, SNAPSHOT, INFO_REPLY
INFO_REQUEST_BYTES = 160       # protocol.h kInfoRequestBytes

# gamedefs.h modeName / modeTitle (index = GameModeId)
MODE_NAMES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"]
MODE_TITLES = ["VS", "BOMB", "RACE", "KING OF THE HILL", "PAYLOAD", "FREE FOR ALL", "NEUTRAL PAYLOAD", "DUEL", "1V1",
               "TOURNEY", "HIDE AND SEEK", "TAG", "MULTI POINT"]
# gamedefs.h arenaVariantRanked: 0 vanilla (1v1), 1 vampire (1v1), 5 instagib (FFA, 5 min)
RANKED_LADDERS = [("vanilla", "RANKED VANILLA"), ("vampire", "RANKED LASER VAMPIRE"), ("instagib", "RANKED INSTAGIB")]
RACE_MAP = "viaduct"  # TIME TRIAL is the ranked race on Viaduct (game.cpp INFO page)
# ---- end of copied constants
VERSION_RE = re.compile(r"\d+\.\d+\.\d+(?:-test\.\d+)?")   # 1.2.3 (production) or 1.2.3-test.4 (testing build)


def changelog_lookup(version: str) -> tuple[str, str, str, bool]:
    """What a /changelog argument names: (version, release tag, changelog file's version, testing).
    Production: "1.2.3" / "v1.2.3" -> tag v1.2.3, changelogs/1.2.3.md. Testing builds are tagged test-<VERSION>-test.N
    (release.yml) and described by their base version's file: "1.2.3-test.4" / "test-1.2.3-test.4" ->
    tag test-1.2.3-test.4, changelogs/1.2.3.md. Raises ValueError for anything else."""
    v = version.strip()
    if v.lower().startswith("test-"):
        v = v[5:]
    v = v[1:] if v[:1] in ("v", "V") else v
    if not VERSION_RE.fullmatch(v):
        raise ValueError("version must look like 1.2.3 (or 1.2.3-test.4 for a testing build)")
    base, _, suffix = v.partition("-")
    testing = bool(suffix)
    return v, (f"test-{v}" if testing else f"v{v}"), base, testing


def mode_title(m: int) -> str:
    return MODE_TITLES[m] if 0 <= m < len(MODE_TITLES) else "?"


# ---------------------------------------------------------------- UDP server query
def build_info_request() -> bytes:
    # padded to INFO_REQUEST_BYTES (protocol.h kInfoRequestBytes): servers ignore smaller requests
    return struct.pack("<IB", PROTOCOL_MAGIC, PKT_INFO_REQUEST).ljust(INFO_REQUEST_BYTES, b"\0")


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


def board_name(s: str) -> str:
    """A player name inside the leaderboard's code block: it can't close the block or start a new line."""
    return re.sub(r"[\x00-\x1f\x7f`]", "'", s)


def format_board(title: str, rows: list[LeaderRow], race: bool = False, n: int = 10) -> str:
    lines = [title, "-" * max(len(title), 34)]
    if not rows:
        lines.append("  no ranked players yet")
    for i, r in enumerate(rows[:n], 1):
        name = board_name(r.name)[:20]
        if race:
            lines.append(f"{i:>2}. {name:<20} {fmt_race_time(r.rating):>10}")
        else:
            lines.append(f"{i:>2}. {name:<20} {r.rating:>5}  {r.games:>4}g")
    return "\n".join(lines)


def master_base(url: str) -> str:
    """A configured Worker URL, checked: https only (BOT_KEY and players' data go there), except a local test Worker
    on http://localhost / 127.0.0.1. Raises ValueError for anything else."""
    u = str(url).strip().rstrip("/")
    local = re.fullmatch(r"http://(localhost|127\.0\.0\.1)(:\d+)?", u)
    if not (local or re.fullmatch(r"https://[A-Za-z0-9.-]+(:\d+)?(/[A-Za-z0-9._~/-]*)?", u)):
        raise ValueError(f"master_url must be an https:// address, not {u[:80]!r}")
    return u


class QuarkAPI:
    def __init__(self, session: aiohttp.ClientSession, github_token: str | None = None, master_override: str | None = None):
        self.s = session
        self.token = github_token
        self._master = master_base(master_override or os.environ.get("QUARK_MASTER") or MASTER_URL)

    async def _text(self, url: str, timeout: float = 8, headers: dict | None = None) -> str:
        async with self.s.get(url, timeout=aiohttp.ClientTimeout(total=timeout), headers=headers) as r:
            r.raise_for_status()
            return await r.text()

    async def master(self) -> str:
        """The Worker's base URL: configured (master_url / QUARK_MASTER) or the built-in MASTER_URL, never fetched."""
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

    # shared queues / crowns (defined below with their parsers)
    async def crowns(self):
        return await _crowns(self)

    async def queue_counts(self):
        return await _queue_counts(self)

    async def filled(self, since: int):
        return await _filled(self, since)

    async def queue_action(self, bot_key: str, discord_name: str, mode: str, action: str, name: str = "") -> str:
        return await _queue_action(self, bot_key, discord_name, mode, action, name)

    async def link_code(self, bot_key: str, discord_name: str) -> str:
        """A one-time code (6 digits) that links this Discord user to whoever types it in game; "" if refused."""
        return await _link_code(self, bot_key, discord_name)

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

    async def releases(self, n: int = 10) -> list[dict]:
        """The newest releases, prereleases (testing builds) included, newest first."""
        async with self.s.get(f"https://api.github.com/repos/{GITHUB_REPO}/releases?per_page={int(n)}", headers=self._gh_headers(),
                              timeout=aiohttp.ClientTimeout(total=10)) as r:
            r.raise_for_status()
            return await r.json()

    async def release_by_tag(self, tag: str) -> dict | None:
        """The GitHub release with this tag, or None when there is none."""
        async with self.s.get(f"https://api.github.com/repos/{GITHUB_REPO}/releases/tags/{tag}", headers=self._gh_headers(),
                              timeout=aiohttp.ClientTimeout(total=10)) as r:
            if r.status == 404:
                return None
            r.raise_for_status()
            return await r.json()

    async def changelog(self, version: str | None = None) -> tuple[str, str]:
        """(title, markdown): the release notes of a version (latest production release when None), else the
        changelogs/<VERSION>.md file in the repo. A testing build's title says "(testing)"; its notes are the
        testing-build line plus its base version's changelog."""
        if not version:
            rel = await self.latest_release()
            version = rel["tag_name"].lstrip("v")
            if rel.get("body"):
                return version, rel["body"]  # the release body is changelogs/<VERSION>.md
        v, tag, base, testing = changelog_lookup(version)
        title = f"{v} (testing)" if testing else v
        rel = await self.release_by_tag(tag)
        if rel and rel.get("body"):
            return title, rel["body"]
        for ref in (tag, "HEAD"):  # fall back to the file in the repo (a testing build: its base version's)
            try:
                return title, await self._text(f"{RAW_BASE}/{ref}/changelogs/{base}.md")
            except aiohttp.ClientResponseError:
                continue
        raise LookupError(f"no changelog for {v}")

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


_URL_RE = re.compile(r"(?i)\b(?:[a-z][a-z0-9+.-]*://|www\.|discord(?:app)?\.(?:gg|com)/)\S*")
_DOMAIN_RE = re.compile(r"(?i)\b[a-z0-9-]+(?:\.[a-z0-9-]+)*\.(?:com|net|org|gg|io|xyz|ru|co|me|app|link|ly|tk|info|biz|site|online|shop|top)\b\S*")
_MD_RE = re.compile(r"([\\*_~`|>\[\]()#:<-])")


def safe_text(text: str, maxlen: int = 64) -> str:
    """Attacker-controlled text (server names, maps, player names) made inert for a Discord message: control
    characters and URLs / bare domains removed (no masked phishing links), markdown escaped, mentions broken."""
    t = "".join(ch for ch in str(text) if ch.isprintable())
    t = _URL_RE.sub("", t)
    t = _DOMAIN_RE.sub("", t)
    t = " ".join(t.split())[:maxlen]
    t = _MD_RE.sub(r"\\\1", t)
    t = t.replace("@", "@​")   # @everyone / @here / <@id> never ping
    return t or "?"


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


# ---------------------------------------------------------------- Discord link, crowns, shared queues (master 1.12.0+)
FALLBACK_BODY = "quark server list"  # what the Worker answers for a path it doesn't know (not deployed yet)


class EndpointMissing(Exception):
    """The master doesn't have this endpoint yet (old worker.js deployed)."""


def to_hex(s: str) -> str:
    return s.encode("utf-8", "replace").hex()


def from_hex(h: str) -> str:
    if h in ("", "-"):
        return ""
    try:
        return bytes.fromhex(h).decode("utf-8", "replace")
    except ValueError:
        return ""


@dataclass
class Crown:
    board: str     # vanilla | vampire | instagib | race:viaduct
    place: int
    id: str
    name: str
    discord: str   # "" when not linked


def parse_crowns(body: str) -> list[Crown]:
    out = []
    for line in body.splitlines():
        p = line.split()
        if len(p) != 5:
            continue
        try:
            out.append(Crown(p[0], int(p[1]), p[2], from_hex(p[3]), from_hex(p[4])))
        except ValueError:
            continue
    return out


@dataclass
class QueueCount:
    mode: str
    count: int
    size: int


def parse_queue_counts(body: str) -> dict[str, QueueCount]:
    out = {}
    for line in body.splitlines():
        p = line.split()
        if len(p) == 3 and p[1].isdigit() and p[2].isdigit():
            out[p[0]] = QueueCount(p[0], int(p[1]), int(p[2]))
    return out


@dataclass
class FilledMember:
    who: str       # ranked id of a game player, or "d:<discord name>" for Discord-sourced entries
    name: str
    discord: str   # "" when unknown

    @property
    def from_discord(self) -> bool:
        return self.who.startswith("d:")


@dataclass
class Filled:
    id: int
    mode: str
    created: int
    members: list[FilledMember] = field(default_factory=list)


def parse_filled(body: str) -> list[Filled]:
    out = []
    for line in body.splitlines():
        p = line.split()
        if len(p) != 4 or not p[0].isdigit() or not p[2].isdigit():
            continue
        f = Filled(int(p[0]), p[1], int(p[2]))
        for m in p[3].split(","):
            # who may itself contain ':' ("d:name"), so split the two hex fields off the right
            parts = m.rsplit(":", 2)
            if len(parts) == 3:
                f.members.append(FilledMember(parts[0], from_hex(parts[1]), from_hex(parts[2])))
        out.append(f)
    return out


def _check_deployed(body: str):
    if body.strip() == FALLBACK_BODY:
        raise EndpointMissing("master has no such endpoint yet (redeploy tools/master/worker.js)")


async def _new_api_get(self: "QuarkAPI", path: str) -> str:
    url = f"{await self.master()}{path}"
    async with self.s.get(url, timeout=aiohttp.ClientTimeout(total=8)) as r:
        if r.status == 404:
            raise EndpointMissing(path)
        r.raise_for_status()
        body = await r.text()
    _check_deployed(body)
    return body


async def _crowns(self: "QuarkAPI") -> list[Crown]:
    return parse_crowns(await _new_api_get(self, "/crowns"))


async def _queue_counts(self: "QuarkAPI") -> dict[str, QueueCount]:
    return parse_queue_counts(await _new_api_get(self, "/queue"))


async def _filled(self: "QuarkAPI", since: int) -> list[Filled]:
    return parse_filled(await _new_api_get(self, f"/filled?since={int(since)}"))


async def _queue_action(self: "QuarkAPI", bot_key: str, discord_name: str, mode: str, action: str, name: str = "") -> str:
    """POST /queue as the bot for a Discord member. Replies: queued <mode> n/size, filled <mode>, left <mode>, left all, ..."""
    form = {"bot": bot_key, "discord": to_hex(discord_name), "mode": mode, "action": action, "name": to_hex(name[:20])}
    async with self.s.post(f"{await self.master()}/queue", data=form, timeout=aiohttp.ClientTimeout(total=8)) as r:
        if r.status == 404:
            raise EndpointMissing("/queue")
        r.raise_for_status()
        body = (await r.text()).strip()
    _check_deployed(body)
    return body




def parse_link_code(body: str) -> str:
    """The Worker's /linkcode reply "code NNNNNN" -> "NNNNNN" ("" for anything else)."""
    m = re.fullmatch(r"code (\d{6})", body.strip())
    return m.group(1) if m else ""


async def _link_code(self: "QuarkAPI", bot_key: str, discord_name: str) -> str:
    form = {"bot": bot_key, "discord": to_hex(discord_name)}
    async with self.s.post(f"{await self.master()}/linkcode", data=form, timeout=aiohttp.ClientTimeout(total=8)) as r:
        if r.status == 404:
            raise EndpointMissing("/linkcode")
        r.raise_for_status()
        body = (await r.text()).strip()
    _check_deployed(body)
    return parse_link_code(body)


def crown_roles(crowns: list[Crown]) -> dict[str, str]:
    """lowercased Discord name -> "first" (place 1 on any board) or "top10" (places 2-10 on any board, never #1)."""
    out: dict[str, str] = {}
    for c in crowns:
        if not c.discord or not 1 <= c.place <= 10:
            continue
        k = c.discord.strip().lower()
        if c.place == 1:
            out[k] = "first"
        else:
            out.setdefault(k, "top10")
    return out
