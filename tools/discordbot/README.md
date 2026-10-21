# Quark Discord bot

Runs the community server: update announcements, live server channels, game pings, mode queues,
ranked leaderboards and an info channel generated from the game's own INFO screen.

| File | What |
|---|---|
| `bot.py` | the bot (`python bot.py`), and `python bot.py --dry-run` |
| `quarkdata.py` | game data with no Discord: master list, UDP server query, leaderboards, GitHub, INFO parser |
| `test_quarkdata.py` | offline tests (`python -m unittest test_quarkdata`) |
| `config.example.json` | copy to `config.json` |

## Where the data comes from (same as the game)
- **Server list**: base URL read from `https://floration.github.io/quark/master.txt` (re-read hourly), then
  `GET <master>/list?v=<version>` gives `ip:port` lines of servers on that version (`src/game/master.cpp`).
  Each server is then asked directly over UDP: `u32 0x51524B31 "QRK1", u8 4` (PKT_INFO_REQUEST), reply type 67
  with name, map, mode, players, max players, port (`src/game/protocol.h`, `server.cpp`, `netclient.cpp`).
  Only servers that answer are shown, like the in-game browser. The version is the latest GitHub release.
- **Leaderboards**: `GET <master>/leaderboard?ladder=vanilla|vampire|instagib&n=10` (`rating games name`) and
  `GET <master>/raceboard?map=viaduct&n=10` (`ms name`) (`src/game/ranked.cpp`, `tools/master/worker.js`).
- **Updates**: `https://api.github.com/repos/floration/quark/releases/latest`; the release body is `changelogs/<VERSION>.md`.
- **Info**: `kInfoTabs` / `kInfoPages` parsed from `src/client/game.cpp` (local checkout if present, else from GitHub).

## What it does
- **#updates**: posts each new release's changelog as embeds. The last announced tag is kept in `state.json`
  (on a fresh install it announces the current release once). `/changelog [version]` shows any version.
- **🟢 LIVE SERVERS** category: a locked voice channel on top named `🟢 13 players live`, then one text channel per
  live server, e.g. `im-quarking-it-3-8`, with a pinned message (map, mode, exact players, address) edited every minute.
  *Rate limits*: Discord allows only 2 renames per channel per 10 minutes, so channel names (and the total) are
  renamed at most once every 5 minutes and only when the count changed; the edited message is always current.
  A channel is deleted when its server is missing from two polls in a row.
- **Game Pings** role: toggled by the button in #queues or `/pings`. When a server goes from 0 to some players
  it's pinged in that server's channel (at most once per 30 min per server, never on the first poll after a restart).
- **#queues**: a panel with one button per queue (press to join, again to leave) and `/queue`, `/leave`, `/queues`.
  Sizes: ranked vanilla 2, ranked laser vampire 2, ranked instagib 4, duel/1v1/race 2, ffa/tourney/hide/tag 4,
  team modes 6 (edit `QUEUES` in `bot.py`). When one fills, its players are pinged 5 times, 3 s apart:
  `@a and @b your queue for RANKED LASER VAMPIRE has been filled, please proceed to a server and begin the game`,
  and they're removed from every queue.
- **#leaderboard**: one message, edited every minute, with the top 10 of each ladder and the time trial.
- **#info**: install, hosting and joining, then every in-game INFO tab. Re-checked every 6 h, edited in place on change.
- Also `/servers` and `/leaderboard`.

Everything it needs (category, channels, role) is created on startup if missing; channels it posts in are read-only for @everyone.

## Create the bot
1. https://discord.com/developers/applications -> New Application -> **Bot** -> Reset Token, copy it.
   No privileged intents are needed.
2. Invite it (replace `APP_ID` with the Application ID from General Information):
   `https://discord.com/oauth2/authorize?client_id=APP_ID&scope=bot+applications.commands&permissions=268659728`
   (Manage Channels, Manage Roles, View Channels, Send Messages, Manage Messages (pinning), Embed Links,
   Read Message History, Mention Everyone (lets it ping the role even if not mentionable).)
   Or tick those boxes in OAuth2 -> URL Generator.
3. In Server Settings -> Roles, drag the bot's role **above** "Game Pings" so it can hand that role out.
4. Enable Developer Mode (User Settings -> Advanced), right-click your server -> Copy Server ID.

## Configure and run
```
cd tools/discordbot
python -m pip install -r requirements.txt
cp config.example.json config.json      # set guild_id; optional names/overrides
export DISCORD_TOKEN=...                # never put it in git
export GUILD_ID=...                     # or in config.json
python bot.py --dry-run                 # see what it would post, no Discord
python bot.py
```
Optional env: `GITHUB_TOKEN` (higher GitHub API limits), `QUARK_MASTER` (other master URL), `QUARKBOT_CONFIG` (config path).
All config keys and defaults are in `DEFAULT_CONFIG` in `bot.py` (poll intervals, rename spacing, ping count...).
The bot needs outbound UDP to reach game servers (same as the game's Join screen).

## Run 24/7 (systemd)
`/etc/systemd/system/quark-discord.service`:
```
[Unit]
Description=quark discord bot
After=network-online.target
Wants=network-online.target
[Service]
User=quark
WorkingDirectory=/home/quark/quark/tools/discordbot
Environment=DISCORD_TOKEN=xxxxx
Environment=GUILD_ID=123456789012345678
ExecStart=/usr/bin/python3 bot.py
Restart=always
RestartSec=10
[Install]
WantedBy=multi-user.target
```
`sudo systemctl daemon-reload && sudo systemctl enable --now quark-discord`, logs: `journalctl -u quark-discord -f`.
(Use a venv's python in `ExecStart` if you installed the requirements in one.)

## Tests
`python -m unittest test_quarkdata` (add `QUARK_SERVER_BIN=../../build/quark_server` to also query a real local server over UDP).
