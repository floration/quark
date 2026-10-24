# Gamemode editor

A local page for making your own game modes (`.qmode`): start from one of the 13 built-in modes, change its numbers,
or rewrite what happens when (scoring, rounds, respawns, win conditions) as code, blocks, a graph or a table of values.
It shows which phase each part runs in and, once the game's helper can run modes, simulates matches with bots.

It runs on your computer only: a small Python server (`serve.py`, standard library only) on `127.0.0.1` serves the
page. Nothing is fetched from the internet.

## Run it

- Windows: double-click `run.cmd` (no console window; `run.cmd --no-open` for no browser).
- Linux / macOS: `./run.sh` (or `python3 serve.py`).

Then open http://127.0.0.1:8771/ (it opens by itself unless `--no-open` is given or `QUARK_SHELL=1` is set).
`?embed=1` hides the page's own header (the Supervisor Board shows the page in its own window).

Flags (or the same keys in `mode_editor.json` next to `serve.py`; `mode_editor.example.json` lists them):

| flag | default |
|---|---|
| `--port` | 8771 |
| `--data-dir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark`. Your modes are in its `custom_modes/` |
| `--maptool` | `quark_maptool` in this repository's build folders (or the checkout `install.py` copied from), then on `PATH` |
| `--quark-tests` | `quark_tests` likewise |
| `--no-open` | don't open a browser |

### What the game's binaries add

The header shows three badges. Each says plainly where its part comes from:

| badge | with the binary | without it |
|---|---|---|
| catalog | `quark_tests exportmode`: the game's slots, fields, actions, params (types, units, ranges, defaults) | **BUILT-IN CATALOG**: `web/js/catalog.js`, transcribed from the mode-editor design (section 2) |
| built-in texts | `exportmode`, else the repository's `tools/modes/*.qlogic` | the page's `web/fixtures/` (common, FFA, KOTH, BOMB from the design, section 3): other modes diff against common only, and the page says so |
| sim | `quark_maptool modesim` | **no modesim**: the Sim view says it is unavailable and runs nothing. There is no stand-in simulation |

On save, `quark_tests checkmode` (or `quark_maptool modecheck`) compiles the file the way the game will, when the
build has it; otherwise the checks on the right are the editor's own. serve.py only runs a `quark_tests` command
after finding its name inside the binary (an unknown command would run the whole test suite).

## The page

One mode is open at a time: a built-in one (read-only: Save asks for a name) or one of yours. **New** starts a mode
from a base mode. **Save**, **Save as**, **⇩ .qmode** (download) and **⇧ Open file** do what they say; Ctrl+S saves,
Ctrl+Z / Ctrl+Y undo and redo (every view).

All views edit the same thing: the mode's whole logic, which is the built-in common text, then the base mode's text,
then your file on top. A save writes only what differs from the built-in (tick "save the whole logic" for all of it).
A slot your file doesn't change runs as native code; a changed one (or one calling a changed proc) is *interpreted*,
marked **i** in the list.

- **Code**: the logic as text, highlighted. An error shows at its line:col and the other views keep the last version
  that parsed; your text is kept (also across restarts). Leaving the box re-prints it in the canonical layout. Above
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
- **Sim**: map, bots, skill, seed, seconds; Run streams `quark_maptool modesim` (timeline with scores, phase bands,
  markers and slot calls; player positions; event log; end card with the logic's cost per tick). *Compare* runs the
  built-in base mode with the same seed. *Batch* runs `modebatch`.

## The file, and the other editors

A `.qmode` is the game's `key=value` lines, then the logic block from the first line that is exactly `logic <N>`.
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
"I wrote this myself" is ticked, then `human`. Other entries are kept (merged, never replaced). A download records it
too.

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
| `run.cmd`, `run.sh` | start it |
| `web/js/qmode.js` | the logic language: parser, canonical printer, highlighter (from the bot editor's dsl.js) |
| `web/js/lang.js` | catalog, checks, cost, merge / diff with the built-in texts (from ast.js) |
| `web/js/qfile.js` | the `.qmode` file: key lines, ownership, patching |
| `web/js/catalog.js` | the built-in catalog (until `exportmode`) |
| `web/js/blocks.js`, `graph.js`, `values.js`, `curves.js` | the views (from the bot editor's) |
| `web/js/phases.js`, `sim.js` | Phases and Sim |
| `web/js/modeui.js`, `app.js`, `api.js` | the page |
| `web/fixtures/*.qlogic` | the design's texts, until `tools/modes/` exists |

The JS is copied from `tools/bot_editor/web/js` and adapted (each file says where it came from); the bot editor
itself is not changed.

## Develop

    node tools/mode_editor/web/test/run.mjs

Round trips (text, AST, blocks, graph) on every built-in text merged for all 13 modes, the checks, the file rule
(foreign lines byte for byte, CRLF, refusals), and serve.py itself (save, 409 when the file changed on disk, 400 when
a save would touch a foreign line, Host / header checks). `QME_VERBOSE=1` logs every request.
