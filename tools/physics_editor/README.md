# Physics editor

A local page for trying custom movement physics without opening the game: change gravity, run speed, jump, dash,
grapple, ladder and every other movement multiplier, then see what they do on scripted scenarios (graphs, paths,
numbers against the standard physics) or drive them yourself with WASD and the mouse. A physics set is saved as a
custom mode (`<data dir>/custom_modes/<name>.qmode`), so the game can play it at once (Host › Custom modes).

It runs on your computer only: a small Python server (`serve.py`, standard library only) on `127.0.0.1` serves the
page and runs the game's helper, `quark_maptool`, which runs the game's own `pmove()` (see `INTERFACE.md` and
`native/README.md`). Nothing is fetched from the internet.

## Run it

- Windows: double-click `run.cmd` (or `run.cmd --no-open`).
- Linux / macOS: `./run.sh` (or `python3 serve.py`).

Then open http://127.0.0.1:8772/ (it opens by itself unless `--no-open` is given or `QUARK_SHELL=1` is set).
`?embed=1` hides the page's own header (the Quark Project app shows the page in its own window). `?mock` uses the
page's MOCK physics even when the helper is there.

Flags (or the same keys in `physics_editor.json` next to `serve.py`; `physics_editor.example.json` lists them):

| flag | json key | default |
|---|---|---|
| `--port` | `port` | 8772 |
| `--data-dir` | `dataDir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark` |
| `--maptool` | `maptool` | `quark_maptool` next to `serve.py` (an installed copy), then this repository's build folders, then `PATH` |
| `--maps-dir` | `mapsDirs` | another folder of `.qmap` files to list (repeatable) |
| | `defaultMapsDir` | the game's maps: the repository's `maps/`, or `maps/` next to an installed `serve.py` |
| | `helperArgs` | extra arguments put before every helper command |
| `--no-open` | | don't open a browser (`QUARK_SHELL=1` does the same) |
| `--install [FOLDER]` | | copy the tool to FOLDER and stop (below) |

Needs Python 3.8+ and a current Chrome, Edge or Firefox (WebGL for the Drive tab and the 3D path).

### Install (the Quark Project app)

`py -3 tools\physics_editor\serve.py --install` (from a repository checkout) copies the tool to
`<Quark folder>/physics-editor` (or the folder given): `serve.py`, `web/`, `run.cmd`, `run.sh`, this README, the
example settings, `INTERFACE.md`, the game's default maps, and `quark_maptool(.exe)` from the checkout's build
folders (with its MinGW DLLs on Windows). It asks nothing and opens nothing, exits 0, and is safe to run again: your
`physics_editor.json` and your data are kept, only the program files are replaced. With no built `quark_maptool`
the installed one is kept (a one-line note says so). The app then starts it from that folder with
`py -3 serve.py --port 8772 --no-open` and `QUARK_SHELL=1`, and shows `http://127.0.0.1:8772/?embed=1`.

### Real helper or MOCK

With a `quark_maptool` that has the physics commands (the header badge says `quark_maptool`), every number is the
game's: each run is the real `pmove()`, 1 ms at a time like the 1000 Hz server, with the set applied through the
game's own custom-mode code (`CustomRules::fromText`, so values are clamped exactly as the game clamps them).

Without it the badge says **MOCK PHYSICS**: a port of `pmove` in the page (`web/js/mockpm.js`) on a stand-in test
room, so the page can be tried before the helper is built. It agrees with the game on the basic numbers (a 1.22 m
standing jump, 9.5 m/s, a 6.27 m running jump, a 1.21 m stop) but it is not the game: trust only the real helper.
Build the helper: `cmake --build build --target quark_maptool`.

## The page

The left panel is always there:

- **Physics set**: open a saved custom mode (`(n changed)` and `· logic` show what it holds), give it a name,
  **New** (the standard physics), **Save** / **Save as…** (writes `custom_modes/<name>.qmode`), **Open file** (any
  .qmode from disk), **.qmode** (download it), **Reset all**. The line under it counts the changed values and says
  whether the set is saved.
- **Scenario**: a **Preset** (the helper's: standing / running jump, run-up, stop, bhop chains plain and with the
  strafe bot, dash, dash jump, super dash, dash into a wall, grapple swing, ladder climb, stairs, max gap, fall), the
  **Map** (the built-in test room, or any game or user map: then **Start at** picks a spawn), saved scenarios
  (**Load…** / **Save**: `physics_editor/scenarios/<name>.json`), **Run**, and **re-run on every change** (on by
  default: each edit re-runs the scenario, debounced, one run at a time; a run of every set takes a few ms).
- **Compare with**: the standard physics (always) and any of your saved sets, run side by side.
- **Results**: the scenario's main numbers for the edited set, with the difference from the standard (green up,
  red down). The preset's own metrics come first.

### Values

Every movement setting in groups (general, ground, air, dash, super dash, grapple, ladder), each with a slider
(logarithmic, it snaps to 1), the multiplier (what the .qmode stores; 1 = standard), the absolute value in its
unit (m/s, s, m...) and the standard value. Changed rows are orange; ↺ resets a value, **reset group** a group,
**Reset all** everything. Ranges are what the game allows (from `physparams`). Hover a row for what it does and the
C++ constants it scales. **dash** and **grapple** are switches. `crouch_speed` is struck through: crouching was
removed from the game. The filter box searches names, units and notes; **only changed** hides the rest.

### Code

The set as the raw .qmode text, two-way: edit the text and Values follow; change a value and the text follows.
Movement lines are coloured, the gamemode's other lines dim; problems (a number that isn't one, a value out of
range and clamped, an unknown key, a duplicate that the game ignores) are marked in the gutter and listed under the
text (click one to jump to its line). From a `logic` line on, the file belongs to the gamemode editor: it is shown
read-only under the text and never changed. **Tidy** rewrites the movement lines the way the game writes them.

### Blocks

The scenario as a stack of blocks on a timeline: drag blocks from the palette into the stack, drag them to reorder,
drag one back to the palette to delete it. Blocks: **Hold keys**, **Wait**, **Turn**, **Look at**, **Jump**
(press length), **Hold jump** (bunny hops), **Jump at the edge**, **Dash** (aim pitch, press length), **Dash
jump**, **Super dash**, **Grapple** (aim pitch), **Crouch** (ignored by the game), **Strafe-bot hops** (jump held,
A / D at the best air-strafe angle every ms; swap off, at each landing, or weave 25° around the course) and a raw
**Step** (any step field). The header holds the start: position, yaw, pitch, a start speed, on the floor, and the
time after the last block. Under the stack the timeline shows each block and the edited set's horizontal speed.
**{ } JSON** edits the scenario as JSON; a scenario the blocks can't express exactly is shown as raw steps.

### Graph

Horizontal speed, speed, height, vertical speed, distance and dash charges against time, one chart each (tick the
ones to show): the edited set solid, the standard dashed, compared sets and Drive recordings in their own colours.
Event markers (jumps, landings, dashes, hooks, ladders, bounces) on top; hover for every run's values at that time.
Under the charts, the metrics table: every number the helper reports for each run, with the edited set minus the
standard and the change in %.

### Path

The runs from above and from the side (along the start's facing), over the map, with 0.1 s ticks and the events;
drag to pan, wheel to zoom, **Fit** to frame the runs. **3D** shows an orbit view of the map and the paths.

### Drive

Click the view to play (pointer lock): **WASD** + mouse, **Space** jump, **Left Shift** dash, **Q** grapple (the
game's default binds), **C** or **Ctrl** crouch (no bind in the game: it ignores crouch), **R** back to the start,
**V** first person / chase camera, **Esc** lets go of the mouse. The real helper runs your input through the game's
`pmove` (`physdrive --stream` over a WebSocket, or Server-Sent Events if WebSockets fail): commands are sent every
frame in pieces of at most 8 ms, and the view runs a few ms ahead of the last answer. The side panel picks the
physics (the edited set or the standard), the camera, mouse speed, FOV and the start (the scenario's, a room part,
a map spawn); it shows the speed over the last 10 s, the events, and the frame rate (average and 1% low) and the
helper's round trip. **→ Graph** sends the drive so far to the Graph and Path tabs as a run.

### Export

**Save** as a custom mode (the name, and where the file goes), **download** or **copy** the .qmode, and the list of
C++ constants the set would change if it became the game's standard (`physexport`: `file: name = old -> new`). The
list is for the owner: nothing in the game is changed.

## Saving and the gamemode editor

The gamemode editor writes the same `custom_modes/*.qmode` files. The physics editor only ever rewrites the
movement keys (`gravity`, `speed`, `jump`, `dash`, `grapple` and the `move_*` keys, as `CustomRules::toText` writes
them):

- every other line stays exactly as it was, in order (line endings too);
- a movement key already in the file is changed in place (the last one, if it is there twice: the game reads that
  one); a `move_` key back at 1 is removed (the game writes only changed ones); a changed key that is missing is
  added where the game would write it;
- from a `logic` line to the end of the file is the gamemode editor's section and is never touched;
- a new file gets only the game's header (`custommode=<name>`, `mode=ffa`) and the movement lines.

The merge is done by `serve.py` on the file as it is on disk at save time (the old file is kept as `.bak`), so
changes the gamemode editor made meanwhile are kept. **Save as** another name copies the open file's other lines and
renames its `custommode=` line.

## Files

| file | what |
|---|---|
| `serve.py` | the local server: the page, custom modes, scenarios, maps, the helper and the Drive relay, `--install` |
| `run.cmd`, `run.sh` | start it |
| `physics_editor.example.json` | the settings `physics_editor.json` can hold |
| `INTERFACE.md` | what the page asks the helper, and its answers |
| `native/` | the helper's physics commands in `quark_maptool` (`native/README.md`) |
| `web/index.html`, `web/style.css`, `web/quark-tools.css` | the page |
| `web/js/app.js` | the page's state: the set, the scenario, runs, the side panel |
| `web/js/helper.js` | every helper call (and the MOCK's answers), the Drive transports |
| `web/js/params.js`, `qmode.js` | the settings catalog; .qmode parsing and the save merge (the same as serve.py's) |
| `web/js/values.js`, `code.js`, `blocks.js`, `graph.js`, `pathview.js`, `drive.js`, `export.js` | the tabs |
| `web/js/gl.js`, `qmap.js` | WebGL drawing of a map; .qmap parsing |
| `web/js/mockpm.js`, `sim.js`, `room.js` | the MOCK: pmove in JS, the scenario runner and metrics, the stand-in room |
| `web/test/run.mjs` | the checks |

Your data, all in the game's data folder: `custom_modes/<name>.qmode` (physics sets), `physics_editor/scenarios/`
(scenarios), `physics_editor/state/` (the page's last session).

## Checks

`node tools/physics_editor/web/test/run.mjs [--shots DIR] [--maptool PATH] [--mock-only]` (Node 18+, Playwright
already installed; it never installs browsers). It starts `serve.py` on a spare port with a temporary data folder and
checks: the page's .qmode merge against serve.py's on awkward files (CRLF, a logic section, duplicates, new files),
every preset round trip through Blocks, the MOCK against the game's numbers, then in Chromium, with the real helper
and with the MOCK: Values → Code and Code → Values, a preset run, dragging a block, a map run, Save keeping the
logic section and the other lines, a new file, Drive with W and Space, the WebSocket and SSE round trips, Export.
It saves a screenshot of each tab (default `/mnt/project-files/physics-editor/shots/`).
