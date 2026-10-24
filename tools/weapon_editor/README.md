# Weapons editor

A local page for making weapon sets without opening the game: change any weapon's numbers (damage, refire, range,
falloff, splash, pellets, projectile speed and every other field), rename weapons, and add logic that runs on each
hit and each kill. You see what a change does at once: shots and time to kill, DPS, damage against distance,
the same numbers for the weapons of 19 other games, a firing range, and bot fights. A set is saved as
`<data dir>/custom_weapons/<name>.qweapons`, and **Play in game** writes a custom mode that uses it
(`custom_modes/wpn-<name>.qmode`, Host > Custom modes).

It runs on your computer only: a small Python server (`serve.py`, standard library only) on `127.0.0.1` serves the
page and runs the game's programs (`quark_maptool` for the range and fights, `quark_tests` for the weapon catalog and
the set check). Nothing is fetched from the internet. The file format and the plan are in `DESIGN.md`.

## Run it

- Windows: double-click `run.cmd` (or `run.cmd --no-open`).
- Linux / macOS: `./run.sh` (or `python3 serve.py`).

Then open http://127.0.0.1:8773/ (it opens by itself unless `--no-open` is given or `QUARK_SHELL=1` is set).
`?embed=1` hides the page's own header (the Quark Project app shows the page in its own window). `?mock` uses the
page's own numbers and the built-in catalog even when the game's programs are there.

Flags (or the same keys in `weapon_editor.json` next to `serve.py`; `weapon_editor.example.json` lists them):

| flag | json key | default |
|---|---|---|
| `--port` | `port` | 8773 |
| `--data-dir` | `dataDir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark` |
| `--maptool` | `maptool` | `quark_maptool` next to `serve.py` (an installed copy), then this repository's build folders, then `PATH` |
| `--quark-tests` | `quarkTests` | `quark_tests` next to `serve.py`, then the build folders, then `PATH` |
| `--maps-dir` | `mapsDirs` | another folder of `.qmap` files to list (repeatable) |
| | `defaultMapsDir` | the game's maps: the repository's `maps/`, or `maps/` next to an installed `serve.py` |
| | `helperArgs` | extra arguments put before every helper command |
| `--no-open` | | don't open a browser (`QUARK_SHELL=1` does the same) |
| `--install [FOLDER]` | | copy the tool to FOLDER and stop (below) |

Needs Python 3.8+ and a current Chrome, Edge or Firefox (WebGL for the 3D views).

### Install (the Quark Project app)

`py -3 tools\weapon_editor\serve.py --install` (from a repository checkout) copies the tool to
`<Quark folder>/weapons-editor` (or the folder given): `serve.py`, `web/`, `run.cmd`, `run.sh`, this README,
`DESIGN.md`, the example settings, the game's default maps, and `quark_maptool(.exe)` from the checkout's build
folders (with its MinGW DLLs on Windows). It asks nothing and opens nothing, exits 0, and is safe to run again: your
`weapon_editor.json` and your data are kept, only the program files are replaced. The app then starts it from that
folder with `py -3 serve.py --port 8773 --no-open` and `QUARK_SHELL=1`, and shows `http://127.0.0.1:8773/?embed=1`.

### The game's numbers or MOCK

The header badge says where the numbers come from.

- **quark_maptool**: the helper has the weapon commands (`wpnparams`, `wpncheck`, `wpnstats`, `wpnrange`). The
  catalog and the check are the game's, and the range is the game's server firing real shots at bots, 1 ms at a time.
  The tables and charts are worked out by the page (`web/js/stats.js`, instant on every edit) with the game's
  arithmetic; once per load the page compares them with `wpnstats` on the standard set at 0 and 20 m, and a
  mismatch (stats.js behind a change in `gamedefs.h`) puts **≠** on the badge with the differences in its tooltip.
- **MOCK**: the page works it out itself (`web/js/stats.js` for the numbers, `web/js/mockrange.js` for the range)
  from the same fields and the same rules as `src/game/gamedefs.h`. With `quark_tests` built, the catalog (fields,
  ranges, defaults, sensors) and the check (the **checked** mark at the top) are still the game's, through
  `quark_tests exportweapons` and `checkweapons`. Without it the page uses its built-in copy of the catalog,
  `web/js/catalog_data.js`, which `build_catalog.py` generates from `quark_tests exportweapons` (so it is the
  game's too, as of the last rebuild; the tests below fail while it is stale: `python3 build_catalog.py` updates it).
- **Fights** always need `quark_maptool` (its `sim` command, the bot editor's fights). Until the helper takes a
  weapon set (`weapons` in the request) the fights run with the standard weapons and the tab says so.

Build them: `cmake --build build --target quark_maptool quark_tests`.

## The page

The left panel is always there: open / save / rename / delete a set (`custom_weapons/*.qweapons`, the old file kept
as `.bak`), open or download a `.qweapons` file, the weapon list (how many values each weapon changes, and an
ƒ mark for weapons with their own logic), **Play in game** (base mode, writes `wpn-<set>.qmode`), what to
compare with (the standard weapons or another set), the target's health, the distance for the numbers, and the
selected weapon's quick numbers. An unsaved set is kept as a draft and comes back next time.

| tab | what it does |
|---|---|
| Weapon | one weapon: its label (the name shown in game), a slider, number and reset for every field, grouped (damage, fire, projectile, spread / scope, alt fire, knockback, falloff, the weapon's own fields), shots and time to kill against the comparison, damage against distance for each fire mode, and the logic that applies to it. **Copy values from…** puts another weapon's numbers (this set, the comparison or the standard) on this one in one edit: a variant is one of the 11 retuned (a 12th weapon needs game code: the game has 11 slots) |
| Values | every weapon and fire mode in one table (damage, shots to kill, time to kill, DPS, refire, range, speed, splash, falloff, knockback, wallbang, self damage; the syringe's healing in its own table; a dropping projectile's range is how far it really reaches from eye height), changes marked green (stronger) or red (weaker), filters, and charts: DPS and time to kill against the comparison, damage against distance for each weapon |
| Other games | Quark's weapons next to 228 weapons from 19 games (`web/data/games.json`, sources in `web/data/SOURCES.md`), normalised by each game's health: shots and time to kill, DPS as a share of health per second. The weapons closest to the selected one, a time-to-kill against shots-to-kill chart, and per game the target health (hero / class games: whose health) and a damage multiplier |
| Code | the set's text: highlighted, problems in the gutter (the game's check when there is one), Tidy, and **Try a hit**: run the hit or kill logic with sensors you set and see the outputs and the lines that ran |
| Blocks | the same text as blocks: drag from the palette (logic, maths, params, curves, sensors, outputs, weapons), one card per weapon with its values. **+ Add** starts a slot (hit / kill, for every weapon or one), a curve or a param; a set with no logic says what slots and curves are and offers one-click starts |
| Graph | the logic as nodes and wires, one frame per weapon; the same **+ Add** and empty-set starts as Blocks |
| Range | fire the selected weapon at targets at 5, 10, 20, 40 and 80 m (still, strafing, a group) on the built-in range or any map, or targets you **Place** on a map (click: a target, shift+click: the shooter, drag to move, ctrl+click to remove, 16 at most), with perfect body, perfect head or bot aim; watch it top-down or in 3D, scrub the timeline, and compare hits, damage, kills and first-kill times with the comparison set |
| Fights | bots fight on any map in any of 12 modes (ffa, vs, duel, 1v1, tourney, koth, bomb, payload, npayload, points, tag, hide; a warning when the map isn't built for it) with the weapons you allow: watch one match live, or run a batch of matches with the comparison set and then the edited set on the same seeds and compare win rates (team modes: each team's share of the matches that ended; matches that ran out of time are counted apart) and, per weapon, kills, fights won, damage, accuracy, TTK and DPS |

## Files

- `serve.py`: the server (port 8773). Writes need the `X-Quark-Editor: 1` header the page sends.
- `build_catalog.py`: writes `web/js/catalog_data.js` from `quark_tests exportweapons` (`--check` only compares).
- `web/`: the page. `js/dsl.js` reads and writes `.qweapons`, `js/ast.js` checks and runs it, `js/catalog.js` reads
  the catalog (`js/catalog_data.js`, generated: never edit it by hand), `js/stats.js` the numbers, `js/mockrange.js` the MOCK range, `js/helper.js` talks to the game's
  programs.
- `web/data/games.json`: other games' weapon data (with `SOURCES.md`).
- `web/test/run.mjs`: the checks: `node tools/weapon_editor/web/test/run.mjs` (node 18+, Playwright from the
  environment). It runs serve.py on a free port (or `--port N`) with a temporary data folder, checks the text round trips, the
  Blocks / Graph round trips, the numbers, the range, the server's endpoints and the page in Chromium, and saves a
  screenshot of each tab into `--shots DIR` (default `/mnt/project-files/weapons-editor/shots/`). `--mock-only`
  ignores the game's programs; `--no-browser` skips Chromium.
