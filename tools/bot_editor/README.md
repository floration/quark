# Bot editor and simulator

A local page for placing bots on any map, giving them objectives, editing their brain (`.qbrain`) as code, blocks,
a graph or a table of values, and running them against each other: live, or in batches.

It runs on your computer only: a small Python server (`serve.py`, standard library only) on `127.0.0.1` serves the
page and runs the game's helper, `quark_maptool`. Nothing is fetched from the internet.

## Run it

- Windows: double-click `run.cmd` (or `run.cmd --no-open`).
- Linux / macOS: `./run.sh` (or `python3 serve.py`).

Then open http://127.0.0.1:8769/ (it opens by itself unless `--no-open` is given or `QUARK_SHELL=1` is set).
`?embed=1` hides the page's own header (the project manager shows the page in its own window).

Flags (or the same keys in `bot_editor.json` next to `serve.py`; `bot_editor.example.json` lists them):

| flag | default |
|---|---|
| `--port` | 8769 |
| `--data-dir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark` |
| `--maptool` | `quark_maptool` in this repository's build folders, then on `PATH` |
| `--quark-tests` | `quark_tests` likewise (brain checks and the brain catalog, when it has them) |
| `--maps-dir` | another folder of `.qmap` files to list (repeatable) |
| `--no-open` | don't open a browser |

Needs Python 3.8+ and a current Chrome, Edge or Firefox.

### Real helper or MOCK

With `quark_maptool` found (the badge in the header says `quark_maptool`), runs are the game's own server and bots,
headless, tick by tick: live runs use its `sim` command, batches `simbatch` (every core but one), and a saved brain
is compiled with `simcheck` (`native/SIM.md` has the formats). Each bot's objective override is played; each bot's
brain is sent as text (saved brains and the unsaved "(editor)" one alike).

Without the helper the badge says **MOCK SIMULATION**: a stand-in simulation in the page (simple paths, sight
cones, shooting) that uses the brain's values, so the page can be tried before the helper is built. Add `?mock` to
the address to use the mock even when the helper is there. MOCK results are not the game's bots.

Build the helper: `cmake --build build --target quark_maptool` (`build-sim/` is found too).

Brains are played only by a build with the game's brain code (`src/game/botbrain.h`, the bot-brains branch). Without
it the helper answers `brainSupport: false`: every bot plays the built-in brain, the page says so when a run or
batch uses another brain, and marks those bots' brain "not applied". An older `quark_maptool` without `sim` is used
through its `bots` command (no brains, no overrides).

## The page

### Simulator

- **Map**: every map the game ships (built by `tools/make_maps.py`; generated into `.cache/maps` the first time if
  the repository has none yet) plus your own maps (the data folder's `maps/` and any `--maps-dir`). The
  view is top-down, coloured by height; ladders green, spawns as team arrows, objectives (sites, hill, points,
  checkpoints, payload path) outlined. Right-drag or Alt-drag pans, the wheel zooms, **Fit** frames the map.
  A map of yours with the same name as a shipped one is marked "(yours, hides the shipped one)". Changing the map
  switches to the mode it's built for (unless you picked a mode by hand) and clears the bots' positions and their
  objective points and targets, which belonged to the old map.
  **Show below** hides everything above a height (indoor maps, Hightown's floors). **3D** toggles an orbit view
  (drag: orbit, right-drag: pan, wheel: zoom).
- **Mode**: any mode on any map. The page warns when the map lacks what the mode needs, or isn't offered for it.
- **Bots**: **+ Bot**, **Fill 2 v 2**, **4 v 4**. Each bot has a name, team, skill, brain (the default, your saved
  brains, or "(editor)" for the brain open in the Brain tab) and an objective: the brain's, go to a point, hold an
  area, follow waypoints, or attack / defend one of the map's objectives (an override bot still fights what it
  sees; untick "chase" and it also forgets enemies it lost sight of). **Place bot** puts a bot on the map: click
  for the position, drag for the facing. Placed bots and objective points can be dragged. A spot with no floor
  under it (outside the map) is refused.
- **Scenario**: name, seed and length. **Save** writes it to the data folder; **Load** reads it back; ⇩ / ⇧ export
  and import the JSON.
- **Run**: ▶ starts the match. Pause, Step (one frame), speed (¼x to 8x real time, or max: as fast as it goes),
  Stop. A run stops when the match ends or at the scenario's seconds, and also when its page goes away (reload,
  tab closed): the page asks the server to stop it, and the server stops any run or batch 5 s after its last
  page stopped listening. The timeline scrubs back through
  what has run; **Live** jumps back to now. On the map: each bot's view cone, path, goal, target line and trail; its
  decision under its name. Click a bot (or a score row) for the inspector; **camera follows** keeps it centred.
  The kill feed and scores are on the right.

### Brain

One brain, four views of the same tree; an edit in any view shows in the others.

- **Code**: the `.qbrain` text (`tools/bots/GRAMMAR.md`), highlighted, with errors at their line and column. It is
  re-printed in the canonical form when you leave it. A saved brain starts with its own header line, not the
  default file's comments.
- **Blocks**: opens on **Whole brain**: every param table, curve, list and slot on one canvas, grouped by area
  (perception, targeting, aim, weapons, fight movement, roam, objectives per mode) under collapsible headers, with a
  mini-map and outline; zoom 25-150% (ctrl+wheel, -/+, Fit all). Click a slot's header for **One at a time**
  (breadcrumb back; each of the two keeps its own zoom). Drag blocks from the palette (logic, maths, random, params, curves, sensors, actions, weapons /
  lists) into a slot's statements; drag one out to the palette to delete it.
- **Graph**: opens on **Whole brain**: every slot as a framed group of nodes, clustered by area; white edges give the
  order of statements, coloured edges carry values. **Show shared inputs** adds one node per param / curve / list /
  table (and sensors read by 2+ slots) wired to every use. Zoomed out, only frames, titles and edge bundles are
  drawn. Search (Enter steps, Esc clears), mini-map, Fit all; double-click a frame title for one slot.
  **Overview (who uses what)** fits the page's width (75% or more) and scrolls down; click a node to keep only its
  links bright. Drag from a
  port to connect, Alt-click an input to cut, Delete removes a node.
- **Values**: every param per skill level (beginner / intermediate / advanced / expert) with its unit and what it
  does; shares and per-second chances also as % (speeds, turn rates and gains get none). Curves as plots: drag points (steps / points curves) or the sliders (an
  expression's numbers). Lists and tables.
- **Diff**: what this brain changes compared with the default, its own saved version or any other saved brain
  (**Compare with**), each with **reset** to that brain's value.

With `quark_tests` there, the page uses the game's catalog (`exportbrain`: every slot, sensor, builtin and param with
its unit, range and doc), and each save is checked by the game (`checkbrain`): the toast and the Checks panel show
its label (`name#hash`) and which slots run in the interpreter (only slots whose code differs from the default; a
file that only changes numbers runs all native). **Check now** (Checks panel) runs the same check on the Code
view's text without saving. The game checks exactly the Code view's text, so its errors are at Code view lines:
they lead the Problems list (click one to go to it) and are marked in the gutter. **Save** checks first and asks
before writing a brain that fails the game's or the editor's checks. A broken edit in the Code view shows its line:col; the draft is
kept (also across a reload) and the other views show the last version that parsed.

Open, New, Open file and Reset all ask first (save / discard / cancel) when the brain has unsaved changes; ↻ next
to Open re-reads the list of saved brains (e.g. ones written outside the page). New and Save as check the name
as you type it, and replacing another saved brain asks first (also a Save of a brain never saved, when a file of
its name has appeared meanwhile). A file opened with **Open file** whose name is already a saved brain opens as
`<name>-2` (`-3`, ...), so a Save can't replace that brain unasked. Every confirm starts on its safe button
(Cancel, or the primary one), and the Enter that closed a name prompt can't press a button in the dialog after it.

The list on the left jumps to a slot, param or curve. **Save** writes `<data>/bots/<name>.qbrain` (only what
differs from the default, unless "save the whole brain" is ticked); **Save as**, **⇩ .qbrain** (download), **Open
file**. Undo / redo: Ctrl+Z / Ctrl+Y outside the code box.

Each save also records who made the change in `provenance.json` next to `serve.py` (`bots/<area>` for each area it
changed, and `bots/brain/<name>`): "ai" unless **I wrote this behaviour myself** is ticked. Entries are merged, never
replaced wholesale.

### Batch runs

The Simulator's line-up, N matches on each chosen map (seed, seed + 1, ...), as fast as the helper goes. The mode
is the scenario's unless another is picked; maps are ticked in a grid with quick picks (**this map**, **for this
mode**, **ranked**, **all**, **none**). Settings are kept in the state folder.

Results are cards side by side (more across on a wide screen): per brain, with each brain's win rate and its 95%
interval (Wilson; n = matches the brain played, a match where several bots share one brain counts once) drawn as
a small chart; per bot (wins, kills, deaths, K/D, damage, accuracy, objective time, time alive), wins per map (team or
player wins, draws, matches that ran out of time, as a stacked bar) and per match. Each match runs until it ends; **Time cap**
(900 s by default; a free for all lasts 600 s) is the most game time one may take before it counts as a timeout. **⇩ CSV** downloads every bot-match row.

**Pin the latest run** keeps its results (the last 30, in the state folder) to compare later ones with, after a
brain edit, say. **Compare runs** puts any two (pinned runs or the latest run) side by side, per brain (matched by
name): win rates with intervals, B − A with its 95% interval, and "better", "worse" or "no clear difference" (when
that interval spans zero). It warns when the two runs differ in mode, maps or line-up.

## Files

All work is saved as files through the server, never only in the browser:

| what | where |
|---|---|
| brains | `<data>/bots/<name>.qbrain` (the previous version kept as `.bak`) |
| scenarios | `<data>/bot_editor/scenarios/<name>.json` |
| the open scenario and brain, settings, batch settings and pinned runs | `<data>/bot_editor/state/*.json` (autosaved; pins in `batchpins.json`) |
| who made what | `tools/bot_editor/provenance.json` |

The browser keeps only conveniences (which tab was open, the theme).

The header's theme button cycles auto (follows the system) / light / dark. The map views keep their own colours.

## Develop

- `web/js/`: `app.js` (start-up), `sim.js` (simulator), `live.js` (playback), `mapview.js` (2D map),
  `map3d.js` (3D), `qmap.js` (map reader), `helper.js` (the only place that talks to `quark_maptool`),
  `mocksim.js` (the MOCK), `dsl.js` (parser and printer), `ast.js` (the tree), `blocks.js`, `graph.js`,
  `values.js`, `curves.js`, `brainui.js`, `batch.js`.
- Tests: `node tools/bot_editor/web/test/run.mjs` (text round trips, the views' round trips, maps, the mock, the
  helper adapter).
- The server's endpoints are listed at the top of `serve.py`. Writes need the `X-Quark-Editor` header and a local
  `Host`, so another web page can't write your files.

## Installing / updating (one command)

From the repository folder (on the owner's PC, `repository\`):

    py -3 tools\bot_editor\install.py            (Linux/macOS: python3 tools/bot_editor/install.py)

It builds `quark_maptool` and `quark_tests` once into `build-editors\` (client off, below-normal priority; an unchanged
checkout rebuilds nothing), then installs the bot editor into `..\bot-editor` and the map editor (which shares the
helper) into `..\map-editor`. Non-interactive, opens no browser, safe to re-run; `bot_editor.json`, the map editor's
`config.json`, `provenance.json` and anything else kept there are left alone. `--bot-editor DIR` / `--map-editor DIR`
pick other folders (`-` skips one), `--build DIR` another build folder, `--jobs N` the build's jobs. Needs cmake, a
C++ compiler (MinGW-w64 on Windows) and Python, like the game build; the map editor's first install downloads
three.js 0.160.0 once (checked by sha256, cached in the build folder).

A bot editor that's running when its files change is restarted on the new ones: install.py asks it to quit
(`POST /api/quit`) and starts it again on the new files in the background with no window (`pythonw` on Windows;
its output in `serve.log` there), or, when the Quark Project app is open, leaves the restart to the app's
watchdog. The build itself opens no window when install.py runs without a console. A map editor that's running still needs restarting by hand (or by the app).

