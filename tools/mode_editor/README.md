# Gamemode editor

A local page for making your own game modes (`.qmode`): start from one of the 13 built-in modes, change its numbers,
or rewrite what happens when (scoring, rounds, respawns, win conditions) as code, blocks, a graph or a table of values.
It shows which phase each part runs in and simulates matches with bots (the game's own server, headless).

It runs on your computer only: a small Python server (`serve.py`, standard library only) on `127.0.0.1` serves the
page. Nothing is fetched from the internet.

## Run it

- Windows: double-click `run.cmd` (no console window; `run.cmd --no-open` for no browser).
- Linux / macOS: `./run.sh` (or `python3 serve.py`).

Then open http://127.0.0.1:8771/ (it opens by itself unless `--no-open` is given or `QUARK_SHELL=1` is set).
`?embed=1` hides the page's own header (the Supervisor Board shows the page in its own window); the left panel then
shows the same badges and the folder modes are saved to in one line.

Flags (or the same keys in `mode_editor.json` next to `serve.py`; `mode_editor.example.json` lists them):

| flag | default |
|---|---|
| `--port` | 8771 |
| `--data-dir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark`. Your modes are in its `custom_modes/` |
| `--maptool` | `quark_maptool` in this repository's build folders (`build`, `build-release`, `build-editors`, any other `build*`; the newest wins), or the checkout `install.py` copied from, then on `PATH` |
| `--quark-tests` | `quark_tests` likewise |
| `--no-open` | don't open a browser |

### What the game's binaries add

The header shows three badges. Each says plainly where its part comes from:

| badge | with the binary | without it |
|---|---|---|
| catalog | `quark_tests exportmode`: the game's slots, fields, actions, params (types, units, ranges, defaults) | **SNAPSHOT CATALOG**: `web/js/catalog_snapshot.js`, the same export taken from a build (`snapshot_catalog.py` refreshes it; the header says at which commit) |
| built-in texts | `exportmode`, else the repository's `tools/modes/*.qlogic` | the page's `web/fixtures/`, a copy of `tools/modes/*.qlogic` (`snapshot_catalog.py --fixtures-only` refreshes it; the web tests fail while they differ) |
| sim | `quark_maptool modesim` | **no modesim**: the Sim view says it is unavailable and runs nothing. There is no stand-in simulation |

On save, `quark_tests checkmode` (or `quark_maptool modecheck`) compiles the file the way the game will, when the
build has it; otherwise the checks on the right are the editor's own. serve.py only runs a `quark_tests` command
after finding its name inside the binary (an unknown command would run the whole test suite).

## The page

One mode is open at a time: a built-in one (read-only: Save asks for a name) or one of yours. **New** starts a mode
from a base mode (a built-in mode's name or an existing file's is refused in the dialog). **Save**, **Save as**,
**⇩ .qmode** (download) and **⇧ Open file** do what they say; Ctrl+S saves (one save at a time: Save is disabled until
the answer is in), Ctrl+Z / Ctrl+Y undo and redo (every view). A save with check errors asks first; the game's own
errors (`checkmode`) then show in Checks at their Code view line. **Rename…**, **Duplicate…** and **Delete** work on
your saved mode: each asks first, the file a rename or delete leaves stays as `<name>.qmode.bak` (the game only loads
`*.qmode`), the new file's `custommode=` line takes the new name, and its provenance item moves (or is copied) with it.

All views edit the same thing: the mode's whole logic, which is the built-in common text, then the base mode's text,
then your file on top. A save writes only what differs from the built-in (tick "save the whole logic" for all of it).
A slot your file doesn't change runs as native code; a changed one (or one calling a changed proc) is *interpreted*,
marked **i** in the list.

- **Code**: the logic as text, highlighted. An error shows at its line:col (and heads Checks) and the other views keep
  the last version that parsed; your text is kept (also across restarts). Leaving the box re-prints it in the canonical layout. Above
  it, the file's key lines, read-only.
- **Blocks**: drag blocks from the palette (logic, maths, players / teams, sensors, actions, objects, timers, state,
  params) into slots.
- **Graph**: one slot or proc as nodes (white edges: statement order; coloured: values), or the overview of who reads
  what.
- **Values**: this editor's keys (time limit, score limit, rounds, map pool); the other editors' keys (read-only);
  every param with its unit, range, built-in value and a reset; state, timers, objects in use, curves.
- **Diff vs built-in**: every declaration and key that differs, each with a reset.
- **Phases**: warmup, freeze, round, overtime, round end, match end, with the slots that run in each (interpreted /
  native / not written), the objects with their events, timers, state, and what stays engine whatever the logic says.
  Clicking a slot nobody writes offers to add it.
- **Sim**: map, bots, skill, the bots' brain (the bot editor's `<data>/bots/*.qbrain`; *vs* gives the odd bots, team
  B in team modes, another one), seed, the most game seconds, tick rate, real time or flat out; Run streams
  `quark_maptool modesim` (`native/MODESIM.md`) on the text as it is now, saved or not (an unchanged built-in mode runs
  as that mode). It draws a timeline (team or player scores, phase bands, a tick per round result in the winner's
  colour, kill and objective markers, and with *trace slots* a strip of the interpreted slots' cost per frame), a
  top-down plot of the map (boxes shaded by height, sites, hill, points, checkpoints, cart track, spawns) with where
  everyone went and is now, an event log, and an end card (winner, reason, rounds and team score where the mode has
  them, length, the scoreboard, x real time, server ns per tick, **logic ns per tick avg / p99 / max**, and each
  interpreted slot's calls and cost). *Compare* runs the built-in base mode with the same seed beside it, on the same
  time and score axes. *Batch* streams `modebatch` over N seeds (every core but one): win rate per team, draws,
  timeouts, match length, rounds, score spread, logic ns per tick, a row per bot, and a CSV of the matches.
  The same request and seed always give the same match. Each run says whether the server ran the file's **logic**.
  The form is kept when you leave the tab and across reloads; a reloaded page picks up its runs where they are (a run
  no page watches is stopped after 30 s). **Scenario** saves the form under a name (`<data>/mode_editor/scenarios/`).

## The file, and the other editors

A `.qmode` is the game's `key=value` lines, then the logic block from the first line the game's `isModeLogicLine`
takes (customrules.cpp: `logic`, then the end of the line, a space, a tab or a `\r`: `logic 1`, `logic`, `logic x`).
Two editors write the same file, so each owns some lines:

- this editor: the logic block and `custommode`, `mode`, `timelimit`, `scorelimit`, `rounds`, `maps`;
- the physics editor (port 8772): the movement keys (`gravity`, `speed`, `jump`, `knockback`, `dash`, `grapple`,
  `selfdamage`, `move_*` / `move.*`);
- everything else (`weapons`, `weapon`, `health`, `instagib`, `vampire`, `regen`, keys a newer game added, comments)
  is kept exactly as written.

A save changes only owned lines, in place (a missing key goes just above `logic`), never reorders, keeps CRLF line
ends, and never writes a key line after `logic`. If the file changed on disk since you opened it (the physics editor
saved it, say), the save is refused and nothing is written; **Reload and keep my edits** reads it again and puts your
logic and keys back on top. serve.py checks all of this again on its side.

## Who made it

Each save records `gamemodes/<name>` in `provenance.json` next to `serve.py` (docs/PROVENANCE.md): `ai` unless
"I wrote this myself" is ticked, then `human`. The box is set from that entry when a mode opens (and cleared for a
built-in or new one), so re-saving a human-made mode keeps it human. Other entries are kept (merged, never replaced). A
download records it too.

## Install (Supervisor Board)

From a repository checkout:

    python3 tools/mode_editor/install.py "<Quark>/gamemode-editor"

Non-interactive and safe to run again: it copies the program files, `web/` and `tools/modes/*.qlogic` (when they
exist), removes files an older install left, keeps `mode_editor.json` and `provenance.json` (merged), writes
`installed_from.json` (so the installed copy finds this checkout's `build/`), and opens nothing. Run it again when
`tools/mode_editor/` or `tools/modes/` change.

## Files

| file | what |
|---|---|
| `serve.py` | the server: page, modes, catalog / texts, checkmode, modesim runs (SSE) |
| `install.py` | copy / update into an install folder |
| `snapshot_catalog.py` | refresh `web/js/catalog_snapshot.js` from `quark_tests exportmode` and `web/fixtures/` from `tools/modes/` (run it when the mode catalog or the built-in texts change) |
| `run.cmd`, `run.sh` | start it |
| `web/js/qmode.js` | the logic language: parser, canonical printer, highlighter (from the bot editor's dsl.js) |
| `web/js/lang.js` | catalog, checks, cost, merge / diff with the built-in texts (from ast.js) |
| `web/js/qfile.js` | the `.qmode` file: key lines, ownership, patching |
| `web/js/catalog.js` | what the export has no word for (a slot's phase, an object's map need, mode info, typed builtins) and the design's catalog under it |
| `web/js/catalog_snapshot.js` | GENERATED: the game's catalog from `exportmode`, used when serve.py can't run it |
| `web/js/blocks.js`, `graph.js`, `values.js`, `curves.js` | the views (from the bot editor's) |
| `web/js/phases.js`, `sim.js` | Phases and Sim |
| `native/modesim_cmd.cpp` | `quark_maptool modesim` / `modebatch` / `modecheck` (built into quark_maptool by CMake); formats in `native/MODESIM.md`, checks in `native/test_modesim.py` |
| `web/js/modeui.js`, `app.js`, `api.js` | the page |
| `web/fixtures/*.qlogic` | a copy of `tools/modes/*.qlogic`, for an install that has neither `exportmode` nor `tools/modes/` |

The JS is copied from `tools/bot_editor/web/js` and adapted (each file says where it came from); the bot editor
itself is not changed.

## Develop

    node tools/mode_editor/web/test/run.mjs

Round trips (text, AST, blocks, graph) on every built-in text merged for all 13 modes, the checks, the file rule
(foreign lines byte for byte, CRLF, refusals, the game's logic-line rule), game error lines mapped to the Code view,
the Sim models (trace frames, shared axes, end card fields), and serve.py itself (save, 409 when the file changed on
disk, 400 when a save would touch a foreign line, Host / header checks, provenance read back, rename / duplicate /
delete, scenarios, brains). `QME_VERBOSE=1` logs every request.

    python3 tools/mode_editor/native/test_modesim.py

The sim helper against the real `build/quark_maptool`: every mode on its own map, determinism, key rules taking
effect, logic labels and errors, slot trace and logic ns, the map view, bot brains, the controls, and batch == modesim
for one seed (by hand; not in CI).
