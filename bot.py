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
import sys
import time
from pathlib import Path

import aiohttp

import quarkdata as qd

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
log = logging.getLogger("quarkbot")

DEFAULT_CONFIG = {
    "guild_id": 0,
    "github_token": "",
    "master_url": "",                 # override (else read from floration.github.io/quark/master.txt)
    "game_version": "",               # override (else the latest GitHub release)
    "live_category": "🟢 LIVE SERVERS",
    "info_category": "QUARK",
    "updates_channel": "updates",
    "leaderboard_channel": "leaderboard",
    "info_channel": "info",
    "queue_channel": "queues",
    "roles_channel": "",              # where the "get pings" button goes; default: the queue channel
    "ping_role": "Game Pings",
    "game_source": str(REPO_ROOT / "src" / "client" / "game.cpp"),  # local file, else fetched from GitHub
    "state_file": str(HERE / "state.json"),
    "server_poll_seconds": 60,
    "leaderboard_seconds": 60,
    "release_poll_seconds": 300,
    "rename_min_seconds": 300,        # Discord allows 2 renames per channel per 10 min
    "server_ping_cooldown_seconds": 1800,
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
        self.d.setdefault("queues", {})    # key -> [user ids]
        self.d.setdefault("messages", {})  # purpose -> message id (or list)

    def save(self):
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.d, indent=1))
        tmp.replace(self.path)


def local_version() -> str:
    try:
        return (REPO_ROOT / "VERSION").read_text().strip()
    except OSError:
        return ""


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
            version = version or rel["tag_name"].lstrip("v")
            print(f"latest release {rel['tag_name']}  {rel.get('html_url')}")
            for i, chunk in enumerate(qd.split_message(f"**Quark {rel['tag_name']} is out!**\n{rel.get('body') or ''}")):
                print(f"--- #{cfg['updates_channel']} message {i + 1} ({len(chunk)} chars)\n{chunk}")

        async def servers():
            v = version or local_version()
            print(f"master: {await api.master()}  version filter: {v}")
            infos = await api.live_servers(v)
            live = [i for i in infos if i.replied]
            total = sum(i.players for i in live)
            print(f"category '{cfg['live_category']}'  total channel: '{total_name(total)}'")
            for i in infos:
                if i.replied:
                    print(f"  #{server_channel_name(i)}  {i.name!r} {i.map} {i.mode_title} {i.players}/{i.max_players} ({i.address}, {i.ping_ms:.0f} ms)")
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
        print("\n===== QUEUES =====")
        for k, (t, n) in QUEUES.items():
            print(f"  {k:<16} {t:<22} {n} players")
        print("  fill message: " + fill_message(["@alice", "@bob"], "ranked-vampire"))


def server_channel_name(i: qd.ServerInfo) -> str:
    return f"{qd.slugify(i.name, 80)}-{i.players}-{i.max_players}"


def total_name(total: int) -> str:
    return f"🟢 {total} player{'s' if total != 1 else ''} live"


def fill_message(mentions: list[str], key: str) -> str:
    who = " and ".join([", ".join(mentions[:-1]), mentions[-1]]) if len(mentions) > 1 else mentions[0]
    return f"{who} your queue for {QUEUES[key][0]} has been filled, please proceed to a server and begin the game"


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

    token = os.environ.get("DISCORD_TOKEN") or cfg.get("token")
    if not token or not cfg["guild_id"]:
        sys.exit("set DISCORD_TOKEN and GUILD_ID (env or config.json)")

    intents = discord.Intents.default()
    intents.members = False  # not needed: slash commands and buttons carry the member
    state = State(cfg["state_file"])
    queue_lock = asyncio.Lock()

    class QuarkBot(commands.Bot):
        session: aiohttp.ClientSession
        api: qd.QuarkAPI
        guild: discord.Guild | None = None
        version: str = ""
        first_server_pass = True
        missing: dict[str, int] = {}
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
            await self.session.close()
            await super().close()

    bot = QuarkBot(command_prefix="!quark ", intents=intents)

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

    async def ensure_role() -> discord.Role:
        r = discord.utils.get(bot.guild.roles, name=cfg["ping_role"])
        if not r:
            r = await bot.guild.create_role(name=cfg["ping_role"], mentionable=True, reason="quark bot: game pings")
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
            except Exception:
                log.exception("%s failed (will retry next tick)", fn.__name__)
        wrapper.__name__ = fn.__name__
        return wrapper

    # ---------- 1/7: release announcements
    @tasks.loop(seconds=cfg["release_poll_seconds"])
    @guarded
    async def release_loop():
        rel = await bot.api.latest_release()
        tag = rel["tag_name"]
        if not cfg["game_version"]:
            bot.version = tag.lstrip("v")
        if state.d.get("last_tag") == tag:
            return
        ch = await ensure_text(cfg["updates_channel"], await ensure_category(cfg["info_category"]))
        body = rel.get("body") or "(no changelog)"
        chunks = qd.split_message(body, 4000)  # embed descriptions allow 4096
        for i, chunk in enumerate(chunks):
            e = discord.Embed(description=chunk, color=0x3BA55D)
            if i == 0:
                e.title, e.url = f"Quark {tag} is out!", rel.get("html_url")
            if i == len(chunks) - 1:
                e.set_footer(text="Update from the launcher, or download from the release page.")
            await ch.send(embed=e)
        state.d["last_tag"] = tag
        state.save()

    # ---------- 2/3: live servers
    @tasks.loop(seconds=cfg["server_poll_seconds"])
    @guarded
    async def server_loop():
        version = bot.version or cfg["game_version"] or local_version()
        infos = [i for i in await bot.api.live_servers(version) if i.replied]
        cat = await ensure_category(cfg["live_category"])
        role = await ensure_role()
        now = time.time()
        total = sum(i.players for i in infos)

        # total: a locked voice channel at the top (voice names may have spaces/emoji), renamed <= every 5 min
        tid = state.d["messages"].get("total_channel")
        tch = bot.guild.get_channel(tid) if tid else None
        if not tch:
            tch = await bot.guild.create_voice_channel(total_name(total), category=cat, position=0,
                                                       overwrites={bot.guild.default_role: discord.PermissionOverwrite(connect=False)})
            state.d["messages"]["total_channel"] = tch.id
            bot.last_total_rename = now
        elif tch.name != total_name(total) and now - bot.last_total_rename >= cfg["rename_min_seconds"]:
            await tch.edit(name=total_name(total))
            bot.last_total_rename = now

        servers = state.d["servers"]
        seen = set()
        for i in infos:
            seen.add(i.address)
            bot.missing.pop(i.address, None)
            rec = servers.get(i.address)
            ch = bot.guild.get_channel(rec["channel"]) if rec else None
            want = server_channel_name(i)
            if not ch:
                ch = await bot.guild.create_text_channel(want, category=cat, overwrites=readonly(), topic=f"{i.name} ({i.address})")
                rec = servers[i.address] = {"channel": ch.id, "msg": 0, "renamed": now, "players": 0, "pinged": 0}
            elif ch.name != want and now - rec.get("renamed", 0) >= cfg["rename_min_seconds"]:
                await ch.edit(name=want)
                rec["renamed"] = now
            text = (f"**{i.name}**\nMap **{i.map}** · Mode **{i.mode_title}** · **{i.players}/{i.max_players}** players\n"
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
                await ch.send(f"{role.mention} **{i.name}** is live: {i.map} {i.mode_title} {i.players}/{i.max_players} - `{i.address}`",
                              allowed_mentions=discord.AllowedMentions(roles=[role]))
                rec["pinged"] = now
            rec["players"] = i.players

        for addr in list(servers):
            if addr in seen:
                continue
            bot.missing[addr] = bot.missing.get(addr, 0) + 1
            if bot.missing[addr] >= 2:  # gone two polls in a row: not just a dropped UDP reply
                ch = bot.guild.get_channel(servers[addr]["channel"])
                if ch:
                    await ch.delete(reason="server went offline")
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

    # ---------- 4: queues
    def queue_text() -> str:
        lines = ["**Queues** - press a button to join (press again to leave). When a queue fills, everyone in it gets pinged."]
        for k, (title, n) in QUEUES.items():
            users = state.d["queues"].get(k, [])
            who = " ".join(f"<@{u}>" for u in users)
            lines.append(f"`{title:<21}` **{len(users)}/{n}** {who}")
        return "\n".join(lines)

    async def refresh_queue_panel():
        try:
            ch = await ensure_text(cfg["queue_channel"], await ensure_category(cfg["info_category"]))
            await upsert_message(ch, "queue_panel", queue_text(), view=QueueView(),
                                 allowed_mentions=discord.AllowedMentions.none())
        except Exception:
            log.exception("queue panel refresh failed")

    async def fire_queue(key: str, users: list[int], channel: discord.abc.Messageable):
        text = fill_message([f"<@{u}>" for u in users], key)
        am = discord.AllowedMentions(users=[discord.Object(u) for u in users], roles=False, everyone=False)
        for k in range(cfg["queue_pings"]):
            try:
                await channel.send(text, allowed_mentions=am)
            except Exception:
                log.exception("queue ping failed")
            if k + 1 < cfg["queue_pings"]:
                await asyncio.sleep(cfg["queue_ping_spacing_seconds"])

    async def join_queue(user: discord.abc.User, key: str, toggle: bool) -> str:
        async with queue_lock:
            q = state.d["queues"].setdefault(key, [])
            title, n = QUEUES[key]
            if user.id in q:
                if toggle:
                    q.remove(user.id); state.save()
                    return f"You left the {title} queue."
                return f"You're already in the {title} queue ({len(q)}/{n})."
            q.append(user.id)
            filled = None
            if len(q) >= n:
                filled = q[:n]
                state.d["queues"][key] = q[n:]
                for other in state.d["queues"]:  # people in the game don't stay queued elsewhere
                    state.d["queues"][other] = [u for u in state.d["queues"][other] if u not in filled]
            state.save()
        if filled:
            ch = bot.get_channel(state.d["messages"].get("queue_channel_id", 0)) or discord.utils.get(bot.guild.text_channels, name=cfg["queue_channel"])
            asyncio.create_task(fire_queue(key, filled, ch))
            return f"The {title} queue is full - pinging everyone!"
        return f"You joined the {title} queue ({len(q)}/{n})."

    class QueueButton(discord.ui.Button):
        def __init__(self, key: str, row: int):
            super().__init__(label=QUEUES[key][0].title(), custom_id=f"quark:q:{key}", row=row,
                             style=discord.ButtonStyle.danger if key.startswith("ranked") else discord.ButtonStyle.secondary)
            self.key = key

        async def callback(self, it: discord.Interaction):
            msg = await join_queue(it.user, self.key, toggle=True)
            await it.response.send_message(msg, ephemeral=True)
            await refresh_queue_panel()

    class QueueView(discord.ui.View):
        def __init__(self):
            super().__init__(timeout=None)
            for k, key in enumerate(QUEUES):
                self.add_item(QueueButton(key, min(k // 4, 4)))

    class RoleView(discord.ui.View):
        def __init__(self):
            super().__init__(timeout=None)

        @discord.ui.button(label="Toggle game pings", custom_id="quark:role", style=discord.ButtonStyle.success)
        async def toggle(self, it: discord.Interaction, _b):
            await it.response.send_message(await toggle_role(it.user), ephemeral=True)

    async def toggle_role(member: discord.Member) -> str:
        role = await ensure_role()
        if role in member.roles:
            await member.remove_roles(role, reason="self-unassign")
            return f"Removed **{role.name}**: no more game pings."
        await member.add_roles(role, reason="self-assign")
        return f"Added **{role.name}**: you'll be pinged when a server goes live."

    queue_choices = [app_commands.Choice(name=t.title(), value=k) for k, (t, _) in QUEUES.items()]

    @bot.tree.command(name="queue", description="Join the queue for a game mode")
    @app_commands.choices(mode=queue_choices)
    async def queue_cmd(it: discord.Interaction, mode: app_commands.Choice[str]):
        await it.response.send_message(await join_queue(it.user, mode.value, toggle=False), ephemeral=True)
        await refresh_queue_panel()

    @bot.tree.command(name="leave", description="Leave a queue (or all queues)")
    @app_commands.choices(mode=queue_choices)
    async def leave_cmd(it: discord.Interaction, mode: app_commands.Choice[str] | None = None):
        async with queue_lock:
            for k, q in state.d["queues"].items():
                if (mode is None or k == mode.value) and it.user.id in q:
                    q.remove(it.user.id)
            state.save()
        await it.response.send_message("Left " + (mode.name if mode else "all queues") + ".", ephemeral=True)
        await refresh_queue_panel()

    @bot.tree.command(name="queues", description="Show every queue")
    async def queues_cmd(it: discord.Interaction):
        await it.response.send_message(queue_text(), ephemeral=True, allowed_mentions=discord.AllowedMentions.none())

    @bot.tree.command(name="pings", description="Toggle the role that gets pinged when a server goes live")
    async def pings_cmd(it: discord.Interaction):
        await it.response.send_message(await toggle_role(it.user), ephemeral=True)

    @bot.tree.command(name="changelog", description="Show the changelog of a version (default: latest)")
    async def changelog_cmd(it: discord.Interaction, version: str | None = None):
        await it.response.defer(thinking=True)
        try:
            v, text = await bot.api.changelog(version)
        except Exception as e:
            await it.followup.send(f"Couldn't get that changelog ({type(e).__name__}).")
            return
        chunks = qd.split_message(text, 4000)
        for k, c in enumerate(chunks[:5]):  # cap: 5 embeds of a very long changelog
            await it.followup.send(embed=discord.Embed(title=f"Quark {v}" if k == 0 else None, description=c, color=0x3BA55D))

    @bot.tree.command(name="servers", description="List live servers")
    async def servers_cmd(it: discord.Interaction):
        await it.response.defer(ephemeral=True)
        try:
            infos = [i for i in await bot.api.live_servers(bot.version or local_version()) if i.replied]
        except Exception as e:
            await it.followup.send(f"Server list unavailable ({type(e).__name__}).", ephemeral=True)
            return
        lines = [f"**{i.name}** - {i.map} {i.mode_title} {i.players}/{i.max_players} `{i.address}`" for i in infos]
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
            await ensure_role()
            qch = await ensure_text(cfg["queue_channel"], await ensure_category(cfg["info_category"]))
            state.d["messages"]["queue_channel_id"] = qch.id
            rch = discord.utils.get(bot.guild.text_channels, name=cfg["roles_channel"]) if cfg["roles_channel"] else qch
            await upsert_message(rch, "role_panel", f"Want a ping when a game goes live? Toggle the **{cfg['ping_role']}** role:",
                                 view=RoleView())
            await refresh_queue_panel()
            await ensure_category(cfg["live_category"])
        except Exception:
            log.exception("setup failed (check the bot's Manage Channels / Manage Roles permissions)")
        for loop in (release_loop, server_loop, leaderboard_loop, info_loop):
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
