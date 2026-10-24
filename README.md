# quark

A fast, minimal FPS on a fully custom C++17 engine (OpenGL 3.3, custom UDP netcode).
Runs on Windows 7/10/11, macOS and Linux.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # or build/quark_tests
```
Linux needs X11/GL dev packages (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`).
Windows: MinGW-w64 (gcc; what releases are built with; MSVC isn't supported). macOS: Xcode command line tools.
The build needs `python3` (standard library only): it generates the recorded announcer lines from `assets/voice/`.
Executables: `quark` (game), `quark_server` (dedicated server), `quark_tests` (headless tests).
Keep the `maps/` folder next to the executables.
On Windows, `tools\build_windows.ps1` installs the tools, builds, and keeps each build in its own folder; `tools\fetch_ci_build.ps1` downloads a CI build ([docs/LOCAL_BUILD.md](docs/LOCAL_BUILD.md)).

### Branches: the launcher, the website and the Discord bot
New work goes to `testing` (each push is a signed testing build); the default branch takes only PRs from it. Three
parts live on branches of their own (docs/HANDOVER.md section 4):
- `launcher`: the launcher that releases ship. Release builds compile `quark_launcher` from that branch
  (`tools/launcher_build.sh`), so it only changes when that branch does; raise its `VERSION` with a launcher change,
  or installed launchers won't take it. The game's own launcher-upgrade code stays in `src/launcher/`.
- `site`: the website. To work on it, or run the site tools (`tools/site_*.py`) locally, check it out into `site/`:
  `git fetch origin site && git worktree add site site`. Production releases publish it from that branch.
- `discord-bot`: the bot (setup and updates: docs/OPERATIONS.md section 2).

## Multiplayer
- **Host in the game:** Host Game -> pick map/mode/port -> Start. "Auto port forward (UPnP)" asks your router to open the UDP port.
- **Dedicated server:** `quark_server --mode bomb --map arena --port 27500 --upnp` (type `help`-style commands into its console: `map`, `mode`, `forcestart`, `kick`, `status`, `quit`).
- **Always-on Linux servers (systemd):** from a checkout, `tools/server/install.sh --public --name "my server"` (plus
  any `quark_server` options). Each box runs **two** servers, each always on the latest build of its channel:
  - **production** (`quark.service`, your port, user `quark`, `/opt/quark`, data in `/var/lib/quark`): every minute
    `tools/server/autoupdate.sh` checks for a new **production** release;
  - **testing** (`quark-testing.service`, the next port up, user `quark-testing`, `/opt/quark-testing`, data in
    `/var/lib/quark-testing`, listed as "<name> [TESTING]"): every 2 minutes it follows the newest **testing**
    prerelease, and it only runs while that is newer than production: when production catches up (same update), the
    testing server stops after its current match and starts again with the next newer testing build. Ranked is played
    there but never rated.
    `--no-testing` leaves it out, `--testing-port N` moves it (forward that UDP port too).

  Both updaters verify the release's signed manifest (its channel, tag, version and commit) and Linux package, build
  exactly the signed commit (one build at a time), and the server restarts once no match is being played. A failed or
  stuck update logs at critical priority naming the instance (`journalctl -p crit -t quark-update`) and can post to a
  Discord webhook (`QUARK_ALERT_WEBHOOK` in `/etc/quark/update.env`; an optional `GITHUB_TOKEN` there raises GitHub's
  rate limit). Both servers run sandboxed, testing at a lower CPU priority.
  **Load governor:** the two share the box through `/var/lib/quark-governor` (`--governor-dir`; `src/game/governor.h`).
  When it can't carry both (a server falls behind its ticks in two rate windows in a row, or its players lose
  snapshots), the empty server is
  throttled first, a tick-rate step at a time down to 125 Hz, then the one not playing a ranked match, then the other;
  if both are at 125 Hz and production is still overloaded for 30 s, the testing server closes (its players are told
  why; it leaves the list) until production has been calm for 5 minutes. Production never closes. Rates climb back one
  step a minute once calm. Older installs: `git pull`, then run `install.sh` again (options and data carry over; the
  existing server becomes the production one).
- **`--own-addr host:port` (ranked behind a forwarded port or in a container):** a player's ranked id is signed for
  the address their game dialled, and a server only takes signatures for an address it knows is its own (its port on
  its own interfaces, its public IP, what UPnP reports). If players reach it somewhere it can't see itself (a router
  forwarding a *different* external port to it, a container or VM with its own network), start it with
  `quark_server --own-addr <that host:port>` (repeat it for more than one), or those players join without a ranked
  id. The server log says when this happens and which address to use (`ranked key proof is for <addr>, not this
  server ... --own-addr <addr>`), and so does the host screen of a game hosted from the menu. For the systemd
  install, pass it to `tools/server/install.sh` like any other `quark_server` option.
- **Join:** Join Game -> enter `ip:port`, or pick a LAN game.
- If UPnP fails, forward **UDP 27500** (or your chosen port) on your router to the host machine.

## Modes
`vs` (rounds, no respawn, health drains on stalemate) · `bomb` · `race` · `koth` · `payload` · `ffa` (overtime tiebreak)

## Controls
WASD move · Space jump · Ctrl/C crouch · Mouse1 fire · 1/2/3 weapons · E plant/defuse · Tab scores · T chat · Esc menu · F12 screenshot

## Maps
Plain text files in `maps/` (see the header of `maps/bastion.qmap`). Lighting is baked on first load and cached as `.qlm`.

## Fonts
The in-game UI text is **Rubik Bold** by default (Copyright 2015 The Rubik Project Authors, SIL Open Font License 1.1: `assets/fonts/Rubik-OFL.txt`), baked into `src/client/font_ui.h` as a distance-field atlas by `python3 tools/make_font_atlas.py` (Pillow + numpy; re-run it after changing a font). Rubik is also the logo, launcher and website title font. SETTINGS > FONT offers other fonts, baked by the same tool into `src/client/font_builtin.h`: **Work Sans Bold** (Copyright 2019 The Work Sans Project Authors, SIL OFL 1.1: `assets/fonts/WorkSans-OFL.txt`), **QUARK CLASSIC** (the original 5x7 pixel font), free look-alikes of game fonts, each with its licence in `assets/fonts/` ([docs/ASSETS.md](docs/ASSETS.md)): **Bebas Neue** (Copyright 2010 Dharma Type, SIL OFL 1.1), **Decalotype Bold** (Copyright 2016 Alfredo Marco Pradil, SIL OFL 1.1), **LibreQuake's CONCHARS** (Copyright 2019-2023 Contributors to the LibreQuake project, BSD-3-Clause), **DejaVu Sans Bold** (Bitstream Vera Fonts Copyright 2003 Bitstream, Inc., and the DejaVu changes, Bitstream Vera licence), **Lilita One** (Copyright 2011 Juan Montoreano, SIL OFL 1.1), **Jost** (Copyright 2020 The Jost Project Authors, SIL OFL 1.1) and **Barlow Condensed Bold** (Copyright 2017 The Barlow Project Authors, SIL OFL 1.1), and any font you put in `mods/fonts/`. Game fonts found on your own computer (a Steam install, the system's fonts) are used in place of the look-alikes and never shipped.
