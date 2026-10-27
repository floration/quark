# tools/maptool: quark_maptool

The native helper of the owner's editors (the map, bot, gamemode, physics and weapons editors, which live in
floration/quarkology since 2026-10-27). It links the game's own code (the map loader, the nav graph and bots, the
mode logic, `pmove`, the weapon sets), so it stays here and changes with that code: CMake builds it from every
`tools/maptool/*/*.cpp` on every CI run (CMakeLists.txt, `quark_maptool`).

| folder | commands | spec |
|---|---|---|
| `map/` | the map JSON form, checks, metrics, measures, the bot run (`main.cpp` dispatches every command) | `map/README.md` |
| `bot/` | the bot editor's simulation | `bot/SIM.md` |
| `mode/` | the gamemode editor's simulation | `mode/MODESIM.md` |
| `physics/` | the physics editor's movement simulation and test room | `physics/INTERFACE.md` |
| `weapon/` | the weapons editor's range, rules and stats | `weapon/DESIGN.md` |

Each folder's `test_*.py` runs the built helper (`python3 tools/maptool/<part>/test_*.py`; the weapon one also checks
the editor's catalog when floration/quarkology is checked out beside this one, or `QUARK_APP_REPO` names it).
Quarkology builds it on the owner's PCs from `Quark\repository\` when an editor that needs it is installed.
