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

## Multiplayer
- **Host in the game:** Host Game -> pick map/mode/port -> Start. "Auto port forward (UPnP)" asks your router to open the UDP port.
- **Dedicated server:** `quark_server --mode bomb --map arena --port 27500 --upnp` (type `help`-style commands into its console: `map`, `mode`, `forcestart`, `kick`, `status`, `quit`).
- **Always-on Linux server (systemd):** from a checkout, `tools/server/install.sh --public --name "my server"` (plus any
  `quark_server` options). It runs the server as a dedicated `quark` user from `/opt/quark` with systemd sandboxing,
  keeps data in `/var/lib/quark`, and every minute `tools/server/autoupdate.sh` checks for a new **production**
  release: it verifies the release's signed manifest and Linux package, builds exactly the signed commit, and the
  server restarts once no match is being played. A failed or stuck update logs at critical priority
  (`journalctl -p crit -t quark-update`) and can post to a Discord webhook (`QUARK_ALERT_WEBHOOK` in
  `/etc/quark/update.env`). Older installs: `git pull`, then run `install.sh` again (options and data carry over).
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
The in-game UI text is **Work Sans Bold** (Copyright 2019 The Work Sans Project Authors, SIL Open Font License 1.1: `assets/fonts/WorkSans-OFL.txt`), baked into `src/client/font_ui.h` as a distance-field atlas by `python3 tools/make_font_atlas.py` (Pillow + numpy; re-run it after changing the font). The logo, launcher and website title font is **Rubik** (SIL OFL 1.1: `assets/fonts/Rubik-OFL.txt`).
