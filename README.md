# quark

A fast, minimal FPS on a fully custom C++17 engine (OpenGL 3.3, custom UDP netcode).
Runs on Windows 7/10/11, macOS and Linux.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
Linux needs X11/GL dev packages (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`).
Windows: Visual Studio 2017+ or MinGW-w64. macOS: Xcode command line tools.
Executables: `quark` (game), `quark_server` (dedicated server), `quark_tests` (headless tests).
Keep the `maps/` folder next to the executables.

## Multiplayer
- **Host in the game:** Host Game -> pick map/mode/port -> Start. "Auto port forward (UPnP)" asks your router to open the UDP port.
- **Dedicated server:** `quark_server --mode bomb --map arena --port 27500 --upnp` (type `help`-style commands into its console: `map`, `mode`, `forcestart`, `kick`, `status`, `quit`).
- **Join:** Join Game -> enter `ip:port`, or pick a LAN game.
- If UPnP fails, forward **UDP 27500** (or your chosen port) on your router to the host machine.

## Modes
`vs` (rounds, no respawn, health drains on stalemate) · `bomb` · `race` · `koth` · `payload` · `ffa` (overtime tiebreak)

## Controls
WASD move · Space jump · Ctrl/C crouch · Mouse1 fire · 1/2/3 weapons · E plant/defuse · Tab scores · T chat · Esc menu · F12 screenshot

## Maps
Plain text files in `maps/` (see the header of `maps/bastion.qmap`). Lighting is baked on first load and cached as `.qlm`.
