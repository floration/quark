# Quark Discord bot

Runs the community server: update announcements, live server channels, game pings, mode queues,
ranked leaderboards and an info channel generated from the game's own INFO screen.

| File | What |
|---|---|
| `bot.py` | the bot (`python bot.py`), and `python bot.py --dry-run` |
| `quarkdata.py` | game data with no Discord: master list, UDP server query, leaderboards, GitHub, INFO parser |
| `test_quarkdata.py` | offline tests (`python -m unittest test_quarkdata`) |
| `config.example.json` | copy to `config.json` |
| `requirements.in` / `requirements.txt` | direct deps / the hash-pinned lock (`pip install --require-hashes -r requirements.txt`) |
| `install.sh` | one-command install as a systemd service on a Linux server |
| `MOVING.md` | moving the bot to its own private repository |

The folder is self-contained: it imports nothing from the game and reads no game files (when it sits inside a game
checkout it uses the local `src/client/game.cpp` and `VERSION` to save a download; `QUARK_REPO=<checkout>` points at
one elsewhere). The protocol and mode constants in `quarkdata.py` are copies of the game's (marked there).

## Where the data comes from (same as the game)
- **Server list**: base URL read from `https://floration.github.io/quark/master.txt` (re-read hourly), then
  `GET <master>/list?v=<version>` gives `ip:port` lines of servers on that version (`src/game/master.cpp`).
  Each server is then asked directly over UDP: `u32 0x51524B31 "QRK1", u8 4` zero-padded to 160 bytes (PKT_INFO_REQUEST; servers ignore shorter requests), reply type 67
  with name, map, mode, players, max players, port (`src/game/protocol.h`, `server.cpp`, `netclient.cpp`).
  Only servers that answer are shown, like the in-game browser. The version is the latest GitHub release.
- **Leaderboards**: `GET <master>/leaderboard?ladder=vanilla|vampire|instagib&n=10` (`rating games name`) and
  `GET <master>/raceboard?map=viaduct&n=10` (`ms name`) (`src/game/ranked.cpp`, `tools/master/worker.js`).
- **Updates**: `https://api.github.com/repos/floration/quark/releases/latest` (production: never a prerelease) and
  `.../releases?per_page=10` (testing builds = GitHub **prereleases**, tags like `v1.17.0-test.3`); the release body is the changelog.
- **Queues / crowns**: `POST|GET /queue`, `GET /filled?since=N`, `GET /crowns` on the master (`tools/master/worker.js`).
- **Info**: `kInfoTabs` / `kInfoPages` parsed from `src/client/game.cpp` (local checkout if present, else from GitHub's raw file).

## What it does
- **#updates**: posts each new **production** release's changelog as embeds and pings **Update Pings**. The last announced
  tag is kept in `state.json` (on a fresh install it announces the current release once). `/changelog [version]` shows
  any version (`1.17.0` or `1.17.0-test.3`).
- **#testing-updates**: each new **testing build** (a GitHub prerelease, `v<VERSION>-test.N`) is posted here instead and
  pings **Testing Updates** only. A prerelease is never announced in #updates, and a release whose tag has a suffix but
  isn't marked prerelease is announced nowhere (`release_kind` in `bot.py`, tested).
- **🟢 LIVE SERVERS** category: a locked voice channel on top named `🟢 13 players live`, then one text channel per
  live server, e.g. `im-quarking-it-3-8`, with a pinned message (map, mode, exact players, address) edited every minute.
  **At most 5 servers are listed** (`max_live_servers`; `/servers` shows the same 5): listed servers keep their channels,
  free slots go to the busiest new servers, and a server must keep answering for 2 minutes (`min_seen_seconds`)
  before it gets channels. Each server is handled on its own: one failed channel create is logged and retried next
  minute without stopping the others.
  *Rate limits*: Discord allows only 2 renames per channel per 10 minutes, so channel names (and the total) are
  renamed at most once every 5 minutes and only when the count changed; the edited message is always current.
  A channel is deleted when its server is missing from two polls in a row.
- **Ping roles**: **Update Pings**, **Testing Updates**, **Game Pings** and **Other Pings**, each toggled by its button in
  #get-pings (`/pings` toggles Game Pings).
- **Game Pings**: when a server goes from 0 to some players
  it's pinged in that server's channel (at most once per 30 min per server, never on the first poll after a restart).
- **#queues**: queues live on the master server and are shared with players queueing from inside the game.
  The panel has one button per mode (join) plus **Leave all**; commands `/queue`, `/leave [mode]`, `/queues`.
  The bot sends `POST <master>/queue` with `bot=<BOT_KEY>`, `discord=<hex of member.name>`, `mode`, `action`, `name`,
  and refreshes the panel counts from `GET <master>/queue` every 20 s and after each press. Entries drop after 30 min.
  Sizes (same as `QUEUE_SIZES` in `tools/master/worker.js`, checked by a test): ranked vanilla 2, ranked laser vampire 2,
  ranked instagib 4, duel/1v1/race 2, ffa/tourney/hide/tag 4, team modes 6.
  Every 10 s the bot reads `GET <master>/filled?since=<last id>`; for each new fill it pings the members 5 times, 3 s apart:
  `@a and @b your queue for RANKED LASER VAMPIRE has been filled, please proceed to a server and begin the game`.
  Discord entries (`d:<name>`) and game players who linked a Discord name are mentioned if found in the server
  (by username, then display name/nickname, case-insensitive); anyone else appears as their **in-game name**.
  Fills from before the bot started are not pinged. Local state keeps only the last fill id and message ids.
- **Leaderboard roles**: every 5 min the bot reads `GET <master>/crowns` (top 10 of vanilla, vampire, instagib and
  the Viaduct time trial, with each player's linked Discord name). Linked members who are #1 on any board get
  **🥇 #1** (gold, `role_first`); places 2-10 get **Top 10** (silver, `role_top10`), never both. Members who drop
  out lose the role. Only **verified** links count: a member runs `/link`, the bot asks the Worker for a one-time
  6-digit code (`POST /linkcode` with `BOT_KEY`, valid 10 min) and shows it to them only (ephemeral); they type it in
  game (Settings -> DISCORD CODE), which links their ranked id to their Discord username. Links made the old way
  (any name typed in game) stay unverified: no roles, no queue pings.
- **#leaderboard**: one message, edited every minute, with the top 10 of each ladder and the time trial.
- **#info**: install, hosting and joining, then every in-game INFO tab. Re-checked every 6 h, edited in place on change.
- Also `/servers`, `/leaderboard` and `/link`.

Everything it needs (category, channels, roles) is created on startup if missing; channels it posts in are read-only for @everyone.

**Roles it changes**: only its own: the four ping roles (members toggle them) and the two leaderboard roles (🥇 #1,
Top 10). It looks them up by their configured names and refuses to give out any role that grants permissions beyond
what @everyone already has, or that sits above its own role. Roles it creates have no permissions.
**Mentions**: every message is sent with mentions off; a send opts in only for its own ping role (announcements,
server live) or for the members of a filled queue / a new member's greeting. It never needs Mention Everyone: its
ping roles are mentionable (it turns that on for them if it can).

## Create the bot
1. https://discord.com/developers/applications -> New Application -> **Bot** -> Reset Token, copy it.
   Under **Privileged Gateway Intents** turn on **Server Members Intent** (needed to find members by name
   for queue pings and leaderboard roles).
2. Invite it (replace `APP_ID` with the Application ID from General Information):
   `https://discord.com/oauth2/authorize?client_id=APP_ID&scope=bot+applications.commands&permissions=268528656`
   (Manage Channels, Manage Roles, View Channels, Send Messages, Manage Messages (pinning), Embed Links,
   Read Message History. **No Mention Everyone**: if an older invite granted it, untick it on the bot's role.)
   Or tick those boxes in OAuth2 -> URL Generator.
3. **Role hierarchy**: in Server Settings -> Roles, drag the bot's role **above** "Update Pings", "Testing Updates",
   "Game Pings", "Other Pings", "🥇 #1" and "Top 10" (it can only hand out roles below its own), and keep it **below**
   every staff / moderator role, so Manage Roles can't touch anything else. Don't give those six roles any permissions.
4. Enable Developer Mode (User Settings -> Advanced), right-click your server -> Copy Server ID.

## Master server (queues, links, crowns)
The queue / link / crowns endpoints need the 1.12.0 Worker:
1. Cloudflare dashboard -> D1 -> the quark database -> Console: run the `-- Discord link, queues` part of
   `tools/master/schema.sql` (the `CREATE TABLE IF NOT EXISTS` lines after that comment).
2. Workers -> the quark master worker -> Settings -> Variables and Secrets: add `BOT_KEY` (type Secret) with a long
   random value, e.g. `python -c "import secrets;print(secrets.token_hex(32))"`.
3. Redeploy `tools/master/worker.js`.
4. Give the bot the same value as `BOT_KEY` (env var or `bot_key` in config.json).
Until the Worker is updated those endpoints answer with the fallback text `quark server list`; the bot treats that
as "not available yet" (panel shows a note, no roles touched) and keeps running everything else.

## Configure and run
```
cd tools/discordbot
python -m pip install --require-hashes -r requirements.txt
cp config.example.json config.json      # set guild_id; optional names/overrides
export DISCORD_TOKEN=...                # never put it in git
export GUILD_ID=...                     # or in config.json
export BOT_KEY=...                      # same as the Worker's BOT_KEY secret
python bot.py --dry-run                 # see what it would post, no Discord
python bot.py
```
Optional env: `GITHUB_TOKEN` (higher GitHub API limits), `QUARK_MASTER` (other master URL), `QUARKBOT_CONFIG` (config path).
All config keys and defaults are in `DEFAULT_CONFIG` in `bot.py` (poll intervals, rename spacing, ping count...).
The bot needs outbound UDP to reach game servers (same as the game's Join screen).

## Run 24/7 (systemd)
`sudo ./install.sh` does all of this. By hand: put the secrets in an environment file only the bot's user can read,
never in the unit file (unit files are world-readable) or on a command line (process lists are):
```
sudo install -m 600 -o quark -g quark /dev/null /etc/quark-discord.env
sudoedit /etc/quark-discord.env        # DISCORD_TOKEN=...  GUILD_ID=...  BOT_KEY=...  one per line
```
`/etc/systemd/system/quark-discord.service`:
```
[Unit]
Description=quark discord bot
After=network-online.target
Wants=network-online.target
[Service]
User=quark
WorkingDirectory=/home/quark/quark/tools/discordbot
EnvironmentFile=/etc/quark-discord.env
ExecStart=/home/quark/quark/tools/discordbot/.venv/bin/python bot.py
Restart=always
RestartSec=10
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
[Install]
WantedBy=multi-user.target
```
`sudo systemctl daemon-reload && sudo systemctl enable --now quark-discord`, logs: `journalctl -u quark-discord -f`.
(`ExecStart` uses the venv made by `python3 -m venv .venv && .venv/bin/pip install --require-hashes -r requirements.txt`.)

**After redeploying with the environment file: rotate both secrets.** Older installs passed the token and key on a
`sudo env ...` command line (visible to every user in `ps`) or kept them in a world-readable unit file. Discord
developer portal -> your app -> Bot -> **Reset Token**; Cloudflare -> the master worker -> Variables and Secrets ->
new **BOT_KEY**; put both new values in `/etc/quark-discord.env` (or re-run `sudo ./install.sh`), delete any old unit
file's `Environment=` lines, then `sudo systemctl daemon-reload && sudo systemctl restart quark-discord`.

**Updating the pins**: edit `requirements.in`, then
`uv pip compile requirements.in --universal --generate-hashes --python-version 3.9 -o requirements.txt`
(or `pip-compile --generate-hashes requirements.in` on the oldest Python you deploy to).

## Tests
`python -m unittest test_quarkdata` (add `QUARK_SERVER_BIN=../../build/quark_server` to also query a real local server over UDP).
Two tests compare with the game's own files (INFO pages in `game.cpp`, queue sizes in `worker.js`); they run when a game
checkout is found (this folder inside one, or `QUARK_REPO=<checkout>`) and are skipped otherwise.
