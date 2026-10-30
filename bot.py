#!/usr/bin/env python3
"""Quark community Discord bot.

  python bot.py              run the bot (needs DISCORD_TOKEN, GUILD_ID or config.json)
  python bot.py --dry-run    print what it would post (servers, leaderboard, info, changelog), no Discord

See README.md for setup. Game data access lives in quarkdata.py.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import re
import sys
import time
from pathlib import Path

import aiohttp

import quarkdata as qd

HERE = Path(__file__).resolve().parent


def find_game_repo() -> Path | None:
    """A local checkout of the game, if there is one: QUARK_REPO, else the repo this folder sits in (tools/discordbot).
    The bot doesn't need it (it reads everything from the master and GitHub); it only saves a download of game.cpp."""
    for p in (os.environ.get("QUARK_REPO"), HERE.parent.parent):
        if p and (Path(p) / "src" / "client" / "game.cpp").is_file():
            return Path(p).resolve()
    return None


GAME_REPO = find_game_repo()
log = logging.getLogger("quarkbot")
S = qd.safe_text   # every piece of external text that goes into a message passes through this
MAX_LIVE_SERVERS = 5     # owner: at most 5 servers listed (channels, /servers); fake servers can't flood the guild
MIN_SEEN_SECONDS = 120   # a server must keep answering this long before it gets channels

DEFAULT_CONFIG = {
    "guild_id": 0,
    "github_token": "",
    "master_url": "",                 # the Worker (else quarkdata.MASTER_URL, the game's built-in one; master.txt is never read)
    "game_version": "",               # override (else the latest GitHub release)
    "live_category": "🟢 LIVE SERVERS",
    "info_category": "QUARK",
    "updates_channel": "updates",
    "testing_channel": "testing-updates",   # testing builds (GitHub prereleases, tags test-<VERSION>-test.N)
    "leaderboard_channel": "leaderboard",
    "info_channel": "info",
    "queue_channel": "queues",
    "ping_role": "Game Pings",
    "update_role": "Update Pings",    # pinged by production release announcements
    "testing_role": "Testing Updates",  # pinged by testing (prerelease) announcements
    "other_role": "Other Pings",      # for anything else worth a ping (events, tournaments...)
    "pings_channel": "get-pings",     # new members are greeted here and pick their pings (no Discord onboarding needed)
    "welcome_delete_seconds": 900,    # the greeting that mentions a new member goes away after this
    "game_source": str(GAME_REPO / "src" / "client" / "game.cpp") if GAME_REPO else "",  # local file, else fetched from GitHub
    "state_file": str(HERE / "state.json"),
    "server_poll_seconds": 60,
    "max_live_servers": MAX_LIVE_SERVERS,
    "min_seen_seconds": MIN_SEEN_SECONDS,
    "leaderboard_seconds": 60,
    "release_poll_seconds": 300,
    "rename_min_seconds": 300,        # Discord allows 2 renames per channel per 10 min
    "server_ping_cooldown_seconds": 1800,
    "bot_key": "",                    # BOT_KEY env: same secret as the Worker's BOT_KEY variable
    "role_first": "🥇 #1",
    "role_top10": "Top 10",
    "queue_poll_seconds": 20,
    "filled_poll_seconds": 10,
    "crowns_seconds": 300,
    "queue_pings": 5,
    "queue_ping_spacing_seconds": 3,
}

# Queues: key -> (title used in the ping, players needed).
# Ranked sizes from gamedefs.h: vanilla/vampire are 1v1 (2), instagib is a 5 min FFA (4 is a good game).
QUEUES: dict[str, tuple[str, int]] = {
    "ranked-vanilla": ("RANKED VANILLA", 2),
    "ranked-vampire": ("RANKED LASER VAMPIRE", 2),
    "ranked-instagib": ("RANKED INSTAGIB", 4),
    "vs": ("VS", 6),
    "bomb": ("BOMB", 6),
    "koth": ("KING OF THE HILL", 6),
    "payload": ("PAYLOAD", 6),
    "npayload": ("NEUTRAL PAYLOAD", 6),
    "points": ("MULTI POINT", 6),
    "duel": ("DUEL", 2),
    "1v1": ("1V1", 2),
    "tourney": ("TOURNEY", 4),
    "ffa": ("FREE FOR ALL", 4),
    "race": ("RACE", 2),
    "hide": ("HIDE AND SEEK", 4),
    "tag": ("TAG", 4),
}


def load_config() -> dict:
    cfg = dict(DEFAULT_CONFIG)
    p = Path(os.environ.get("QUARKBOT_CONFIG", HERE / "config.json"))
    if p.exists():
        cfg.update(json.loads(p.read_text()))
    if os.environ.get("GUILD_ID"):
        cfg["guild_id"] = int(os.environ["GUILD_ID"])
    if os.environ.get("GITHUB_TOKEN"):
        cfg["github_token"] = os.environ["GITHUB_TOKEN"]
    if os.environ.get("BOT_KEY"):
        cfg["bot_key"] = os.environ["BOT_KEY"]
    if os.environ.get("QUARK_MASTER"):
        cfg["master_url"] = os.environ["QUARK_MASTER"]
    cfg["guild_id"] = int(cfg["guild_id"] or 0)
    return cfg


class State:
    """Small JSON file so restarts don't repost or duplicate channels/messages."""

    def __init__(self, path: str):
        self.path = Path(path)
        self.d: dict = {}
        if self.path.exists():
            try:
                self.d = json.loads(self.path.read_text())
            except Exception:
                log.exception("state file unreadable, starting fresh")
        self.d.setdefault("servers", {})   # address -> {channel, msg, name, renamed}
        self.d.pop("queues", None)         # queues live on the master now
        self.d.setdefault("messages", {})  # purpose -> message id (or list)

    def save(self):
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.d, indent=1))
        tmp.replace(self.path)


def local_version() -> str:
    try:
        return (GAME_REPO / "VERSION").read_text().strip() if GAME_REPO else ""
    except OSError:
        return ""


def pick_listed(infos: list, listed: set, first_seen: dict, now: float, limit: int = MAX_LIVE_SERVERS,
                min_seen: float = MIN_SEEN_SECONDS) -> list:
    """Which live servers get listed (channels): the ones already listed stay (no churn), then new ones that have
    answered for min_seen seconds, most players first, until `limit`. infos: servers that answered this poll."""
    keep = [i for i in infos if i.address in listed][:limit]
    new = [i for i in infos if i.address not in listed and now - first_seen.get(i.address, now) >= min_seen]
    new.sort(key=lambda i: (-i.players, i.address))
    return keep + new[:max(0, limit - len(keep))]


def top_servers(infos: list, limit: int = MAX_LIVE_SERVERS) -> list:
    """The servers /servers and the dry run show: most players first, at most `limit`."""
    return sorted(infos, key=lambda i: (-i.players, i.address))[:limit]


_PROD_TAG = re.compile(r"v?\d+\.\d+\.\d+")


def release_kind(rel: dict) -> str | None:
    """"production" for a normal release with a plain v1.2.3 tag, "testing" for a GitHub prerelease
    (tags like test-1.2.3-test.4), None for drafts and anything else. A prerelease is never production, and a
    non-prerelease with a suffixed tag isn't announced at all (it's neither)."""
    if not rel or rel.get("draft"):
        return None
    tag = str(rel.get("tag_name") or "")
    if rel.get("prerelease"):
        return "testing" if tag else None
    return "production" if _PROD_TAG.fullmatch(tag) else None


def newest_testing(releases: list[dict]) -> dict | None:
    """The newest prerelease in a GitHub /releases listing (it's newest first)."""
    for r in releases:
        if release_kind(r) == "testing":
            return r
    return None


# ---------------------------------------------------------------- dry run
async def dry_run(cfg: dict):
    async with aiohttp.ClientSession(trust_env=True) as s:
        api = qd.QuarkAPI(s, cfg["github_token"] or None, cfg["master_url"] or None)

        async def section(title, coro):
            print(f"\n===== {title} =====")
            try:
                await coro
            except Exception as e:
                print(f"  FAILED: {type(e).__name__}: {e}")

        version = cfg["game_version"]

        async def release():
            nonlocal version
            rel = await api.latest_release()
            if release_kind(rel) != "production":
                print(f"latest release {rel.get('tag_name')} is not a production release: not announced")
            else:
                version = version or rel["tag_name"].lstrip("v")
                print(f"latest release {rel['tag_name']}  {rel.get('html_url')}")
                for i, chunk in enumerate(qd.split_message(f"**Quark {rel['tag_name']} is out!**\n{rel.get('body') or ''}")):
                    print(f"--- #{cfg['updates_channel']} (@{cfg['update_role']}) message {i + 1} ({len(chunk)} chars)\n{chunk}")
            test = newest_testing(await api.releases())
            if test:
                print(f"--- #{cfg['testing_channel']} (@{cfg['testing_role']}): testing build {test['tag_name']}  {test.get('html_url')}")
            else:
                print(f"no testing build (prerelease) published: #{cfg['testing_channel']} stays quiet")

        async def servers():
            v = version or local_version()
            print(f"master: {await api.master()}  version filter: {v}")
            infos = await api.live_servers(v)
            live = [i for i in infos if i.replied]
            total = sum(i.players for i in live)
            shown = top_servers(live, cfg["max_live_servers"])
            print(f"category '{cfg['live_category']}'  total channel: '{total_name(total)}'  listing {len(shown)} of {len(live)}")
            for i in infos:
                if i in shown:
                    print(f"  #{server_channel_name(i)}  {i.name!r} {i.map} {i.mode_title} {i.players}/{i.max_players} ({i.address}, {i.ping_ms:.0f} ms)")
                elif i.replied:
                    print(f"  {i.address}: live but over the {cfg['max_live_servers']}-server limit (not listed)")
                else:
                    print(f"  {i.address}: listed but no UDP reply (skipped)")

        async def board():
            print("```\n" + await api.leaderboard_text() + "\n```")

        async def info():
            pages = qd.parse_info_pages(await api.game_source(cfg["game_source"]))
            for msg in info_messages(pages):
                print(f"--- ({len(msg)} chars)\n{msg}")

        await section("RELEASE / UPDATES", release())
        await section("LIVE SERVERS", servers())
        await section("LEADERBOARD", board())
        await section("INFO", info())

        async def queues():
            counts = await api.queue_counts()
            print("GET /queue:", ", ".join(f"{c.mode} {c.count}/{c.size}" for c in counts.values()) or "(all empty)")
            rows = await api.filled(0)
            print(f"GET /filled?since=0: {len(rows)} fill(s) in the last hour")
            for f in rows:
                names = [f"@{m.who[2:]}" if m.from_discord else (f"@{S(m.discord, 40)}" if m.discord else f"**{S(m.name, 40)}**") for m in f.members]
                print(f"  #{f.id} -> " + fill_message(names, f.mode, QUEUES.get(f.mode, (f.mode.upper(),))[0]))

        async def crowns():
            cr = await api.crowns()
            roles = qd.crown_roles(cr)
            print(f"GET /crowns: {len(cr)} rows, {sum(1 for c in cr if c.discord)} linked")
            for c in cr:
                print(f"  {c.board:<14} #{c.place:<2} {c.name:<20} discord={c.discord or '-'}")
            for name, kind in sorted(roles.items()):
                print(f"  would give @{name}: {cfg['role_first'] if kind == 'first' else cfg['role_top10']}")

        await section("SHARED QUEUES (master)", queues())
        await section("LEADERBOARD ROLES (/crowns)", crowns())
        print("\n===== QUEUE PANEL =====")
        for k, (t, n) in QUEUES.items():
            print(f"  {k:<16} {t:<22} {n} players")
        print("  fill message: " + fill_message(["@alice", "@bob"], "ranked-vampire"))


BOT_STARTED_MS = int(time.time() * 1000)


def server_channel_name(i: qd.ServerInfo) -> str:
    return f"{qd.slugify(i.name, 80)}-{i.players}-{i.max_players}"


def total_name(total: int) -> str:
    return f"🟢 {total} player{'s' if total != 1 else ''} live"


def fill_message(mentions: list[str], key: str, title: str | None = None) -> str:
    who = " and ".join([", ".join(mentions[:-1]), mentions[-1]]) if len(mentions) > 1 else mentions[0]
    return f"{who} your queue for {title or QUEUES[key][0]} has been filled, please proceed to a server and begin the game"


def info_messages(pages) -> list[str]:
    out = ["# QUARK INFO\nThe same text as the in-game **INFO** screen (generated from the game's source).\n\n" + qd.INSTALL_TEXT]
    for tab, md in qd.info_markdown(pages):
        out.extend(qd.split_message(f"## {tab}\n{md}"))
    return out


# ---------------------------------------------------------------- Discord bot
def run_bot(cfg: dict):
    import discord
    from discord import app_commands
    from discord.ext import commands, tasks
    # (annotations are strings here - "from __future__ import annotations" - and discord.py resolves the slash
    # command ones against the module globals, so these have to be visible there too)
    globals().update(discord=discord, app_commands=app_commands)

    token = os.environ.get("DISCORD_TOKEN") or cfg.get("token")
    if not token or not cfg["guild_id"]:
        sys.exit("set DISCORD_TOKEN and GUILD_ID (env or config.json)")

    intents = discord.Intents.default()
    intents.members = True  # privileged "Server Members" intent: find members by name for queue pings and leaderboard roles
    state = State(cfg["state_file"])

    class QuarkBot(commands.Bot):
        session: aiohttp.ClientSession
        api: qd.QuarkAPI
        guild: discord.Guild | None = None
        version: str = ""
        testing_version: str = ""   # the newest testing build's version (1.2.3.4): the testing servers announce it
        first_server_pass = True
        missing: dict[str, int] = {}
        first_seen: dict[str, float] = {}
        last_total_rename = 0.0

        async def setup_hook(self):
            self.session = aiohttp.ClientSession(trust_env=True)
            self.api = qd.QuarkAPI(self.session, cfg["github_token"] or None, cfg["master_url"] or None)
            self.add_view(QueueView())
            self.add_view(RoleView())
            g = discord.Object(id=cfg["guild_id"])
            self.tree.copy_global_to(guild=g)
            await self.tree.sync(guild=g)

        async def close(self):
            if getattr(self, "session", None): await self.session.close()
            await super().close()

    # nothing in a message pings anyone unless that send opts in (role pings, queue fills, the welcome): a server named
    # "@everyone" can't ping the guild
    bot = QuarkBot(command_prefix="!quark ", intents=intents, allowed_mentions=discord.AllowedMentions.none())

    # ---------- helpers
    async def ensure_category(name: str) -> discord.CategoryChannel:
        g = bot.guild
        cat = discord.utils.get(g.categories, name=name)
        if not cat:
            cat = await g.create_category(name, reason="quark bot setup")
        return cat

    def readonly() -> dict:
        g = bot.guild
        return {g.default_role: discord.PermissionOverwrite(send_messages=False, add_reactions=False),
                g.me: discord.PermissionOverwrite(send_messages=True, manage_messages=True, embed_links=True)}

    async def ensure_text(name: str, cat: discord.CategoryChannel | None, read_only: bool = True) -> discord.TextChannel:
        ch = discord.utils.get(bot.guild.text_channels, name=name)
        if not ch:
            ch = await bot.guild.create_text_channel(name, category=cat, overwrites=readonly() if read_only else {},
                                                      reason="quark bot setup")
        return ch

    # Manage Roles is only used on the bot's own roles: the ping roles and the two leaderboard roles, by their
    # configured names, and only while they grant nothing @everyone doesn't already have (so renaming a staff role
    # to "Game Pings" can't make the bot hand it out). The bot's own role must sit above them (README).
    def own_role_names() -> set[str]:
        return {cfg[k] for k in ("ping_role", "update_role", "testing_role", "other_role", "role_first", "role_top10")}

    def can_manage(role: discord.Role) -> bool:
        if role.name not in own_role_names():
            return False
        if role.permissions.value & ~bot.guild.default_role.permissions.value:
            log.warning("role %r grants permissions beyond @everyone: the bot won't give it out", role.name)
            return False
        if role >= bot.guild.me.top_role:
            log.warning("role %r is above the bot's role: drag the bot's role above it (Server Settings -> Roles)", role.name)
            return False
        return True

    async def ensure_role(name: str = None) -> discord.Role:
        """a ping role anyone can give themselves (game pings by default), made if missing. Ping roles are
        mentionable so the bot can ping them without Mention Everyone."""
        name = name or cfg["ping_role"]
        r = discord.utils.get(bot.guild.roles, name=name)
        if not r:
            r = await bot.guild.create_role(name=name, mentionable=True, permissions=discord.Permissions.none(),
                                            reason="quark bot: ping role")
        elif not r.mentionable and can_manage(r):
            try:
                await r.edit(mentionable=True, reason="quark bot: the bot pings this role")
            except discord.HTTPException:
                log.warning("role %r isn't mentionable: turn on 'Allow anyone to @mention this role' or pings won't notify", name)
        return r

    async def upsert_message(ch: discord.abc.Messageable, purpose: str, content: str = None, **kw) -> discord.Message:
        mid = state.d["messages"].get(purpose)
        if mid:
            try:
                msg = await ch.fetch_message(mid)
                await msg.edit(content=content, **kw)
                return msg
            except discord.NotFound:
                pass
        msg = await ch.send(content=content, **kw)
        state.d["messages"][purpose] = msg.id
        state.save()
        return msg

    def guarded(fn):
        async def wrapper(*a, **kw):
            try:
                await fn(*a, **kw)
            except (aiohttp.ClientError, asyncio.TimeoutError, ConnectionError) as e:   # a network blip: one line, no traceback
                log.warning("%s: network error, will retry next tick (%s)", fn.__name__, type(e).__name__)
            except Exception:
                log.exception("%s failed (will retry next tick)", fn.__name__)
        wrapper.__name__ = fn.__name__
        return wrapper

    # ---------- 1/7: release announcements
    async def announce(rel: dict, channel_key: str, role_key: str, title: str, footer: str, color: int):
        tag = rel["tag_name"]
        ch = await ensure_text(cfg[channel_key], await ensure_category(cfg["info_category"]))
        role = await ensure_role(cfg[role_key])
        body = rel.get("body") or "(no changelog)"
        if channel_key == "updates_channel":   # a production release: link the website's full changelog
            body = qd.with_full_link(body, tag)
        chunks = qd.split_message(body, 4000)  # embed descriptions allow 4096
        for i, chunk in enumerate(chunks):
            e = discord.Embed(description=chunk, color=color)
            if i == 0:
                e.title, e.url = title.format(tag=tag), rel.get("html_url")
            if i == len(chunks) - 1:
                e.set_footer(text=footer)
            await ch.send(content=role.mention if i == 0 else None, embed=e,
                          allowed_mentions=discord.AllowedMentions(roles=[role]) if i == 0 else discord.AllowedMentions.none())

    @tasks.loop(seconds=cfg["release_poll_seconds"])
    @guarded
    async def release_loop():
        rel = await bot.api.latest_release()   # GitHub's "latest" never is a prerelease or a draft
        if release_kind(rel) != "production":  # (and in case it ever were: testing builds don't go in #updates)
            log.warning("latest release %s isn't a production release: not announced", rel.get("tag_name"))
            return
        tag = rel["tag_name"]
        if not cfg["game_version"]:
            bot.version = tag.lstrip("v")
        if state.d.get("last_tag") == tag:
            return
        await announce(rel, "updates_channel", "update_role", "Quark {tag} is out!",
                       "Update from the launcher, or download from the release page.", 0x3BA55D)
        state.d["last_tag"] = tag
        state.save()

    @tasks.loop(seconds=cfg["release_poll_seconds"])
    @guarded
    async def testing_loop():
        """testing builds: GitHub prereleases (test-<VERSION>-test.N) go to #testing-updates and ping Testing Updates only"""
        rel = newest_testing(await bot.api.releases())
        if rel:
            bot.testing_version = qd.testing_version(rel["tag_name"])
        if not rel or state.d.get("last_test_tag") == rel["tag_name"]:
            return
        await announce(rel, "testing_channel", "testing_role", "Quark testing build {tag}",
                       "Testing build: in the launcher switch to the testing branch to play it. Expect bugs.", 0xE67E22)
        state.d["last_test_tag"] = rel["tag_name"]
        state.save()

    async def update_total_channel(total: int, now: float):
        tid = state.d["messages"].get("total_channel")
        tch = bot.guild.get_channel(tid) if tid else None
        if not tch:
            tch = await bot.guild.create_voice_channel(total_name(total), category=None, position=0,
                                                       overwrites={bot.guild.default_role: discord.PermissionOverwrite(connect=False)})
            state.d["messages"]["total_channel"] = tch.id
            bot.last_total_rename = now
        if (tch.category is not None or tch.position != 0):   # the very top of the server, above every category
            try:
                await tch.edit(category=None, position=0)
            except discord.Forbidden:
                log.warning("can't move the player-count channel to the top: give the bot Manage Channels")
        if tch.name != total_name(total) and now - bot.last_total_rename >= cfg["rename_min_seconds"]:
            bot.last_total_rename = now
            try:
                await tch.edit(name=total_name(total))
            except discord.Forbidden:   # (needs Manage Channels on that channel; don't fail the whole loop over it)
                log.warning("can't rename the player-count channel: give the bot Manage Channels (and View Channel) on it")

    async def update_server_channel(i: qd.ServerInfo, servers: dict, cat, role, now: float):
        rec = servers.get(i.address)
        ch = bot.guild.get_channel(rec["channel"]) if rec else None
        want = server_channel_name(i)
        if not ch:
            ch = await bot.guild.create_text_channel(want, category=cat, overwrites=readonly(), topic=f"{S(i.name)} ({i.address})")
            rec = servers[i.address] = {**(rec or {}), "channel": ch.id, "msg": 0, "renamed": now,
                                        "players": (rec or {}).get("players", 0), "pinged": (rec or {}).get("pinged", 0)}
            state.save()   # (saved right away: a later failure this tick mustn't orphan the new channel)
        elif ch.name != want and now - rec.get("renamed", 0) >= cfg["rename_min_seconds"]:
            rec["renamed"] = now
            await ch.edit(name=want)
        text = (f"**{S(i.name)}**\nMap **{S(i.map)}** · Mode **{S(i.mode_title)}** · **{i.players}/{i.max_players}** players\n"
                f"Join: `{i.address}` (Join Game > internet list)\n-# updated <t:{int(now)}:R>")
        try:
            msg = await ch.fetch_message(rec["msg"]) if rec.get("msg") else None
            if msg:
                await msg.edit(content=text)
        except discord.NotFound:
            msg = None
        if not msg:
            msg = await ch.send(text)
            rec["msg"] = msg.id
            try:
                await msg.pin()
            except discord.HTTPException:
                pass
        # ping when a game starts (players appear) on a server; not on the first pass after a restart
        if (i.players > 0 and rec.get("players", 0) == 0 and not bot.first_server_pass
                and now - rec.get("pinged", 0) >= cfg["server_ping_cooldown_seconds"]):
            rec["pinged"] = now
            await ch.send(f"{role.mention} **{S(i.name)}** is live: {S(i.map)} {S(i.mode_title)} {i.players}/{i.max_players} - `{i.address}`",
                          allowed_mentions=discord.AllowedMentions(roles=[role]))
        rec["players"] = i.players
        # a voice channel for the people on that server (open to everyone, gone when the server is)
        vname = f"🔊 {qd.slugify(i.name, 80)}"[:90]   # plain words only: no links or markdown in channel names
        vch = bot.guild.get_channel(rec.get("voice", 0)) if rec.get("voice") else None
        try:
            if not vch:
                vch = await bot.guild.create_voice_channel(vname, category=cat)
                rec["voice"] = vch.id
                state.save()
            elif vch.name != vname and now - rec.get("vrenamed", 0) >= cfg["rename_min_seconds"]:
                rec["vrenamed"] = now
                await vch.edit(name=vname)
        except discord.Forbidden:
            log.warning("can't make or rename a server's voice channel: give the bot Manage Channels")

    # ---------- 2/3: live servers
    @tasks.loop(seconds=cfg["server_poll_seconds"])
    @guarded
    async def server_loop():
        version = bot.version or cfg["game_version"] or local_version()
        infos = [i for i in await bot.api.live_servers(version, bot.testing_version) if i.replied]
        cat = await ensure_category(cfg["live_category"])
        role = await ensure_role()
        now = time.time()
        total = sum(i.players for i in infos)

        # total: a locked voice channel at the top (voice names may have spaces/emoji), renamed <= every 5 min
        try:
            await update_total_channel(total, now)
        except discord.HTTPException as e:   # (a failure here doesn't stop the server channels below)
            log.warning("player-count channel: %s", e)

        servers = state.d["servers"]
        live = {i.address for i in infos}
        for a in list(bot.first_seen):
            if a not in live:
                del bot.first_seen[a]
        for i in infos:
            bot.first_seen.setdefault(i.address, now)
        # at most max_live_servers listed; new ones wait min_seen_seconds (a server that answers once isn't listed)
        listed = pick_listed(infos, set(servers), bot.first_seen, now, cfg["max_live_servers"], cfg["min_seen_seconds"])
        seen = set()
        for i in listed:
            seen.add(i.address)
            bot.missing.pop(i.address, None)
            try:   # one server's channel failing (rate limit, a permission, a full category) doesn't stop the others
                await update_server_channel(i, servers, cat, role, now)
            except discord.HTTPException as e:
                log.warning("live server %s: %s (retrying next tick)", i.address, e)

        for addr in list(servers):
            if addr in seen:
                continue
            # gone two polls in a row (not just a dropped UDP reply), or pushed out by the limit after a config change
            bot.missing[addr] = bot.missing.get(addr, 0) + 1
            if bot.missing[addr] >= 2 or addr in live:
                try:
                    for cid in (servers[addr]["channel"], servers[addr].get("voice", 0)):
                        ch = bot.guild.get_channel(cid) if cid else None
                        if ch:
                            try:
                                await ch.delete(reason="server went offline")
                            except discord.NotFound:   # already gone (deleted by hand)
                                pass
                except discord.HTTPException as e:
                    log.warning("deleting channels of %s: %s (retrying next tick)", addr, e)
                    continue
                del servers[addr]
                bot.missing.pop(addr, None)
        bot.first_server_pass = False
        state.save()

    # ---------- 5: leaderboard
    @tasks.loop(seconds=cfg["leaderboard_seconds"])
    @guarded
    async def leaderboard_loop():
        ch = await ensure_text(cfg["leaderboard_channel"], await ensure_category(cfg["info_category"]))
        text = await bot.api.leaderboard_text(10)
        content = f"**Ranked leaderboards** (top 10, rating · games) - updated <t:{int(time.time())}:R>\n```\n{text}\n```"
        await upsert_message(ch, "leaderboard", qd.split_message(content)[0])

    # ---------- 6: info channel
    async def post_info():
        ch = await ensure_text(cfg["info_channel"], await ensure_category(cfg["info_category"]))
        pages = qd.parse_info_pages(await bot.api.game_source(cfg["game_source"]))
        msgs = info_messages(pages)
        old = state.d["messages"].get("info", [])
        if state.d.get("info_hash") == hash_text(msgs) and old:
            return
        ids = []
        for k, text in enumerate(msgs):  # edit existing messages in place, send new ones, delete extras
            m = None
            if k < len(old):
                try:
                    m = await ch.fetch_message(old[k]); await m.edit(content=text)
                except discord.NotFound:
                    m = None
            if not m:
                if k < len(old):  # order broke: start over
                    for mid in old[k:]:
                        try:
                            await (await ch.fetch_message(mid)).delete()
                        except discord.HTTPException:
                            pass
                    old = old[:k]
                m = await ch.send(text, suppress_embeds=True)
            ids.append(m.id)
        for mid in old[len(msgs):]:
            try:
                await (await ch.fetch_message(mid)).delete()
            except discord.HTTPException:
                pass
        state.d["messages"]["info"] = ids
        state.d["info_hash"] = hash_text(msgs)
        state.save()

    @tasks.loop(hours=6)
    @guarded
    async def info_loop():
        await post_info()

    # ---------- 4: queues, shared with the game on the master (POST/GET /queue, GET /filled)
    bot.queue_counts = {}
    bot.queue_note = ""

    def queue_text() -> str:
        lines = ["**Queues** (shared with players queueing in game) - press a mode to join, **Queue for all** to join every one, **Leave all** to leave. "
                 "Queues drop you after 30 min. When one fills, everyone in it gets pinged."]
        if bot.queue_note:
            lines.append(f"-# {bot.queue_note}")
        for k, (title, n) in QUEUES.items():
            c = bot.queue_counts.get(k)
            lines.append(f"`{title:<21}` **{c.count if c else 0}/{n}**")
        return "\n".join(lines)

    async def refresh_counts():
        try:
            bot.queue_counts = await bot.api.queue_counts()
            bot.queue_note = ""
        except qd.EndpointMissing:
            bot.queue_note = "queues unavailable: the master server hasn't been updated yet"
        except Exception as e:
            bot.queue_note = f"queues unavailable ({type(e).__name__})"
            log.warning("GET /queue failed: %s", e)

    async def refresh_queue_panel(fetch: bool = True):
        try:
            if fetch:
                await refresh_counts()
            ch = await ensure_text(cfg["queue_channel"], await ensure_category(cfg["info_category"]))
            text = queue_text()
            if text != state.d.get("queue_panel_text"):  # don't edit an unchanged message every 20 s
                await upsert_message(ch, "queue_panel", text, view=QueueView(), allowed_mentions=discord.AllowedMentions.none())
                state.d["queue_panel_text"] = text
                state.save()
        except Exception:
            log.exception("queue panel refresh failed")

    @tasks.loop(seconds=cfg["queue_poll_seconds"])
    @guarded
    async def queue_loop():
        await refresh_queue_panel()

    async def queue_request(member: discord.Member, key: str, action: str) -> str:
        if not cfg["bot_key"]:
            return "Queues aren't set up on this bot (no BOT_KEY)."
        title = QUEUES[key][0] if key in QUEUES else ""
        try:
            r = await bot.api.queue_action(cfg["bot_key"], member.name, key, action, member.display_name)
        except qd.EndpointMissing:
            return "The queue server isn't updated yet, try again later."
        except Exception as e:
            log.warning("POST /queue failed: %s", e)
            return f"Couldn't reach the queue server ({type(e).__name__})."
        if r.startswith("queued"):
            return f"You're in the {title} queue ({r.split()[-1]})."
        if r.startswith("filled"):
            asyncio.create_task(filled_loop_once())  # ping right away instead of waiting for the next poll
            return f"The {title} queue is full - pinging everyone!"
        if r == "left all":
            return "You left every queue."
        if r.startswith("left"):
            return f"You left the {title} queue."
        if r == "bad request":
            log.warning("the master rejected the bot's queue request: BOT_KEY in /etc/quark-discord.env must match the worker's BOT_KEY secret")
            return "The queue server didn't accept the bot (its BOT_KEY doesn't match the server's). Tell an admin."
        return f"Queue server said: {r}"

    # resolving names -> members (needs the Server Members intent)
    def find_member(name: str) -> discord.Member | None:
        n = name.strip().lower()
        if not n or not bot.guild:
            return None
        for m in bot.guild.members:
            if m.name.lower() == n:
                return m
        for m in bot.guild.members:  # fallback: global display name, then server nickname
            if (m.global_name or "").lower() == n or (m.nick or "").lower() == n:
                return m
        return None

    def mention_for(fm: qd.FilledMember) -> tuple[str, int | None]:
        dname = fm.who[2:] if fm.from_discord else fm.discord
        m = find_member(dname) if dname else None
        if m:
            return m.mention, m.id
        label = S(fm.name or dname or "player", 40)
        return f"**{label}**", None

    async def fire_queue(f: qd.Filled, channel: discord.abc.Messageable):
        pairs = [mention_for(m) for m in f.members]
        title = QUEUES[f.mode][0] if f.mode in QUEUES else qd.safe_text(f.mode.upper(), 32)
        text = fill_message([p[0] for p in pairs], f.mode, title)
        am = discord.AllowedMentions(users=[discord.Object(i) for _, i in pairs if i], roles=False, everyone=False)
        for k in range(cfg["queue_pings"]):
            try:
                await channel.send(text, allowed_mentions=am)
            except Exception:
                log.exception("queue ping failed")
            if k + 1 < cfg["queue_pings"]:
                await asyncio.sleep(cfg["queue_ping_spacing_seconds"])

    filled_lock = asyncio.Lock()

    async def filled_loop_once():
        async with filled_lock:
            since = int(state.d.get("filled_since", 0))
            try:
                rows = await bot.api.filled(since)
            except qd.EndpointMissing:
                return
            ch = bot.get_channel(state.d["messages"].get("queue_channel_id", 0)) or \
                discord.utils.get(bot.guild.text_channels, name=cfg["queue_channel"])
            for f in rows:
                state.d["filled_since"] = max(int(state.d.get("filled_since", 0)), f.id)
                # skip fills from before this bot started (first run / long downtime): /filled keeps an hour of them
                if ch and f.created >= BOT_STARTED_MS - 120000:
                    asyncio.create_task(fire_queue(f, ch))
            state.d.setdefault("filled_since", since)
            state.save()
        if rows:
            await refresh_queue_panel()

    @tasks.loop(seconds=cfg["filled_poll_seconds"])
    @guarded
    async def filled_loop():
        await filled_loop_once()

    class QueueButton(discord.ui.Button):
        def __init__(self, key: str, row: int):
            super().__init__(label=QUEUES[key][0].title(), custom_id=f"quark:q:{key}", row=row,
                             style=discord.ButtonStyle.danger if key.startswith("ranked") else discord.ButtonStyle.secondary)
            self.key = key

        async def callback(self, it: discord.Interaction):
            await it.response.defer(ephemeral=True, thinking=True)
            await it.followup.send(await queue_request(it.user, self.key, "join"), ephemeral=True)
            await refresh_queue_panel()

    class QueueAllButton(discord.ui.Button):
        def __init__(self):
            super().__init__(label="Queue for all", custom_id="quark:q:joinall", row=4, style=discord.ButtonStyle.success)

        async def callback(self, it: discord.Interaction):
            await it.response.defer(ephemeral=True, thinking=True)
            replies = [await queue_request(it.user, k, "join") for k in QUEUES]   # (one request a mode, in order)
            filled = [r for r in replies if "filled" in r.lower()]
            await it.followup.send(filled[0] if filled else f"Queued for all {len(QUEUES)} modes.", ephemeral=True)
            await refresh_queue_panel()

    class LeaveAllButton(discord.ui.Button):
        def __init__(self):
            super().__init__(label="Leave all", custom_id="quark:q:leaveall", row=4, style=discord.ButtonStyle.primary)

        async def callback(self, it: discord.Interaction):
            await it.response.defer(ephemeral=True, thinking=True)
            await it.followup.send(await queue_request(it.user, "", "leaveall"), ephemeral=True)
            await refresh_queue_panel()

    class QueueView(discord.ui.View):
        def __init__(self):
            super().__init__(timeout=None)
            for k, key in enumerate(QUEUES):
                self.add_item(QueueButton(key, min(k // 4, 3)))
            self.add_item(QueueAllButton())
            self.add_item(LeaveAllButton())

    PING_KINDS = {   # custom id -> (role config key, what it's for)
        "quark:role:update": ("update_role", "new versions of the game"),
        "quark:role:testing": ("testing_role", "new testing builds (the launcher's testing branch)"),
        "quark:role": ("ping_role", "a server going live"),          # (the old id: existing panels keep working)
        "quark:role:other": ("other_role", "events, tournaments and other news"),
    }

    class PingButton(discord.ui.Button):
        def __init__(self, cid: str, label: str, style):
            super().__init__(label=label, custom_id=cid, style=style)

        async def callback(self, it: discord.Interaction):
            await it.response.send_message(await toggle_role(it.user, self.custom_id), ephemeral=True)

    class RoleView(discord.ui.View):
        def __init__(self):
            super().__init__(timeout=None)
            self.add_item(PingButton("quark:role:update", "Update pings", discord.ButtonStyle.primary))
            self.add_item(PingButton("quark:role:testing", "Testing updates", discord.ButtonStyle.primary))
            self.add_item(PingButton("quark:role", "Game pings", discord.ButtonStyle.success))
            self.add_item(PingButton("quark:role:other", "Other pings", discord.ButtonStyle.secondary))

    async def toggle_role(member: discord.Member, cid: str = "quark:role") -> str:
        key, what = PING_KINDS.get(cid, PING_KINDS["quark:role"])
        role = await ensure_role(cfg[key])
        if not can_manage(role):
            return f"The bot can't change **{S(role.name)}** right now. Tell an admin (see the bot's log)."
        if role in member.roles:
            await member.remove_roles(role, reason="self-unassign")
            return f"Removed **{role.name}**: no more pings for {what}."
        await member.add_roles(role, reason="self-assign")
        return f"Added **{role.name}**: you'll be pinged for {what}. Press it again to turn it off."

    def ping_panel_text() -> str:
        return ("**Pick your pings** - press a button to turn it on, again to turn it off:\n"
                f"- **{cfg['update_role']}**: a new version of the game is out\n"
                f"- **{cfg['testing_role']}**: a new testing build is out (in #{cfg['testing_channel']})\n"
                f"- **{cfg['ping_role']}**: a server goes live\n"
                f"- **{cfg['other_role']}**: events, tournaments and other news")

    @bot.event
    async def on_member_join(member: discord.Member):
        """greet them in the pings channel (it mentions them, so they see it) and point at the buttons"""
        if not bot.guild or member.guild.id != bot.guild.id or member.bot:
            return
        try:
            ch = await ensure_text(cfg["pings_channel"], await ensure_category(cfg["info_category"]))
            await ch.send(f"Welcome {member.mention}! Choose which pings you want with the buttons above.",
                          allowed_mentions=discord.AllowedMentions(users=[member]), delete_after=cfg["welcome_delete_seconds"])
        except discord.HTTPException:
            log.warning("couldn't greet %s in #%s", member, cfg["pings_channel"])

    # ---------- leaderboard roles from GET /crowns
    async def ensure_named_role(name: str, colour: int) -> discord.Role:
        r = discord.utils.get(bot.guild.roles, name=name)
        if not r:
            r = await bot.guild.create_role(name=name, colour=discord.Colour(colour), hoist=True, permissions=discord.Permissions.none(),
                                            reason="quark bot: leaderboard role")
        return r

    @tasks.loop(seconds=cfg["crowns_seconds"])
    @guarded
    async def crowns_loop():
        try:
            crowns = await bot.api.crowns()
        except qd.EndpointMissing:
            return
        want = qd.crown_roles(crowns)  # discord name -> first/top10
        first = await ensure_named_role(cfg["role_first"], 0xF1C40F)
        top10 = await ensure_named_role(cfg["role_top10"], 0xC0C0C0)
        if not (can_manage(first) and can_manage(top10)):
            return
        target: dict[int, str] = {}
        for dname, kind in want.items():
            m = find_member(dname)
            if m and (target.get(m.id) != "first"):
                target[m.id] = kind
        for role, kind in ((first, "first"), (top10, "top10")):
            for m in list(role.members):
                if target.get(m.id) != kind:
                    await m.remove_roles(role, reason="no longer on the leaderboard")
            for mid, k in target.items():
                if k == kind:
                    m = bot.guild.get_member(mid)
                    if m and role not in m.roles:
                        await m.add_roles(role, reason="leaderboard")

    queue_choices = [app_commands.Choice(name=t.title(), value=k) for k, (t, _) in QUEUES.items()]

    @bot.tree.command(name="queue", description="Join the queue for a game mode (shared with players in game)")
    @app_commands.choices(mode=queue_choices)
    async def queue_cmd(it: discord.Interaction, mode: app_commands.Choice[str]):
        await it.response.defer(ephemeral=True, thinking=True)
        await it.followup.send(await queue_request(it.user, mode.value, "join"), ephemeral=True)
        await refresh_queue_panel()

    @bot.tree.command(name="leave", description="Leave a queue (or all queues)")
    @app_commands.choices(mode=queue_choices)
    async def leave_cmd(it: discord.Interaction, mode: app_commands.Choice[str] | None = None):
        await it.response.defer(ephemeral=True, thinking=True)
        await it.followup.send(await queue_request(it.user, mode.value if mode else "", "leave" if mode else "leaveall"), ephemeral=True)
        await refresh_queue_panel()

    # Linking is verified: the code only goes to the Discord user who ran /link (ephemeral), and only a player who
    # types it in game gets linked. Unverified (old) links never get leaderboard roles or queue pings.
    @bot.tree.command(name="link", description="Get a code to link your quark player to this Discord account")
    async def link_cmd(it: discord.Interaction):
        await it.response.defer(ephemeral=True, thinking=True)
        if not cfg["bot_key"]:
            await it.followup.send("Linking isn't set up on this bot (no BOT_KEY).", ephemeral=True)
            return
        try:
            code = await bot.api.link_code(cfg["bot_key"], it.user.name)
        except qd.EndpointMissing:
            await it.followup.send("The ranking server isn't updated yet, try again later.", ephemeral=True)
            return
        except Exception as e:
            log.warning("POST /linkcode failed: %s", e)
            await it.followup.send(f"Couldn't reach the ranking server ({type(e).__name__}).", ephemeral=True)
            return
        if not code:
            await it.followup.send("The ranking server refused (BOT_KEY mismatch?). Tell an admin.", ephemeral=True)
            return
        await it.followup.send(f"Your code: **{code}** (valid 10 minutes, works once).\nIn quark: Settings -> DISCORD CODE, type it, "
                               "then BACK. Your quark name and this Discord name will show on the public leaderboard.", ephemeral=True)

    @bot.tree.command(name="queues", description="Show every queue")
    async def queues_cmd(it: discord.Interaction):
        await it.response.defer(ephemeral=True)
        await refresh_counts()
        await it.followup.send(queue_text(), ephemeral=True)

    @bot.tree.command(name="pings", description="Toggle the role that gets pinged when a server goes live")
    async def pings_cmd(it: discord.Interaction):
        await it.response.send_message(await toggle_role(it.user), ephemeral=True)

    @bot.tree.command(name="changelog", description="Changelog of a version: 1.17.0, a testing build 1.17.0-test.3 (default: latest)")
    async def changelog_cmd(it: discord.Interaction, version: str | None = None):
        await it.response.defer(thinking=True)
        try:
            v, text = await bot.api.changelog(version)
        except Exception as e:
            await it.followup.send(f"Couldn't get that changelog ({type(e).__name__}).")
            return
        chunks = qd.split_message(qd.with_full_link(text, v.split(" ")[0]), 4000)
        for k, c in enumerate(chunks[:5]):  # cap: 5 embeds of a very long changelog
            await it.followup.send(embed=discord.Embed(title=f"Quark {v}" if k == 0 else None, description=c, color=0x3BA55D))

    @bot.tree.command(name="servers", description="List live servers")
    async def servers_cmd(it: discord.Interaction):
        await it.response.defer(ephemeral=True)
        try:
            infos = [i for i in await bot.api.live_servers(bot.version or local_version(), bot.testing_version) if i.replied]
        except Exception as e:
            await it.followup.send(f"Server list unavailable ({type(e).__name__}).", ephemeral=True)
            return
        shown = top_servers(infos, cfg["max_live_servers"])
        lines = [f"**{S(i.name)}** - {S(i.map)} {S(i.mode_title)} {i.players}/{i.max_players} `{i.address}`" for i in shown]
        if len(infos) > len(shown):
            lines.append(f"-# and {len(infos) - len(shown)} more: see Join Game in quark")
        await it.followup.send("\n".join(lines) or "No servers live right now.", ephemeral=True)

    @bot.tree.command(name="leaderboard", description="Ranked top 10s")
    async def leaderboard_cmd(it: discord.Interaction):
        await it.response.defer(ephemeral=True)
        await it.followup.send(qd.split_message("```\n" + await bot.api.leaderboard_text(10) + "\n```")[0], ephemeral=True)

    @bot.event
    async def on_ready():
        bot.guild = bot.get_guild(cfg["guild_id"])
        if not bot.guild:
            log.error("bot is not in guild %s", cfg["guild_id"])
            return
        log.info("ready as %s in %s", bot.user, bot.guild.name)
        try:
            for key in ("update_role", "testing_role", "ping_role", "other_role"): await ensure_role(cfg[key])
            qch = await ensure_text(cfg["queue_channel"], await ensure_category(cfg["info_category"]))
            state.d["messages"]["queue_channel_id"] = qch.id
            pch = await ensure_text(cfg["pings_channel"], await ensure_category(cfg["info_category"]))
            if state.d["messages"].get("role_panel_channel") != pch.id and state.d["messages"].get("role_panel"):
                old = bot.guild.get_channel(state.d["messages"].get("role_panel_channel") or qch.id)   # (it used to be in the queue channel)
                try:
                    if old: await (await old.fetch_message(state.d["messages"]["role_panel"])).delete()
                except discord.HTTPException:
                    pass
                state.d["messages"].pop("role_panel", None)
            state.d["messages"]["role_panel_channel"] = pch.id
            await ensure_text(cfg["updates_channel"], await ensure_category(cfg["info_category"]))
            await ensure_text(cfg["testing_channel"], await ensure_category(cfg["info_category"]))
            await upsert_message(pch, "role_panel", ping_panel_text(), view=RoleView())
            await refresh_queue_panel()
            await ensure_category(cfg["live_category"])
        except Exception:
            log.exception("setup failed (check the bot's Manage Channels / Manage Roles permissions)")
        for loop in (release_loop, testing_loop, server_loop, leaderboard_loop, info_loop, queue_loop, filled_loop, crowns_loop):
            if not loop.is_running():
                loop.start()

    bot.run(token, log_handler=None)


def hash_text(msgs: list[str]) -> str:
    import hashlib
    return hashlib.sha256("\x00".join(msgs).encode()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="print what would be posted, don't connect to Discord")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    cfg = load_config()
    if args.dry_run:
        asyncio.run(dry_run(cfg))
    else:
        run_bot(cfg)


if __name__ == "__main__":
    main()
