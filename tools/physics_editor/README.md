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
`?embed=1` hides the page's own header (the Quarkology app shows the page in its own window). `?mock` uses the
page's MOCK physics even when the helper is there.

Flags (or the same keys in `physics_editor.json` next to `serve.py`; `physics_editor.example.json` lists them):

| flag | json key | default |
|---|---|---|
| `--port` | `port` | 8772 |
| `--data-dir` | `dataDir` | the game's user folder: `QUARK_DATA_DIR`, else `%APPDATA%\quark`, `~/Library/Application Support/quark` or `~/.local/share/quark` |
| `--maptool` | `maptool` | `quark_maptool` next to `serve.py` (an installed copy), then this repository's build folders, then `PATH` |
| `--maps-dir` | `mapsDirs` | another folder of `.qmap` files to list (repeatable) |
| | `defaultMapsDir` | the game's maps: the repository's `maps/`, or `maps/` next to an installed `serve.py` |
| `--references` | `referencesDir` | other games' movement values: `references/` next to `serve.py` (served at `/references/`) |
| | `helperArgs` | extra arguments put before every helper command |
| `--no-open` | | don't open a browser (`QUARK_SHELL=1` does the same) |
| `--install [FOLDER]` | | copy the tool to FOLDER and stop (below) |

Needs Python 3.8+ and a current Chrome, Edge or Firefox (WebGL for the Drive tab and the 3D path).
The page follows the system's light or dark theme; the header's ◐ / ☀ / ☾ button forces one (remembered in the
browser; `?theme=light|dark` in the address does the same).

### Install (the Quarkology app)

`py -3 tools\physics_editor\serve.py --install` (from a repository checkout) copies the tool to
`<Quark folder>/physics-editor` (or the folder given): `serve.py`, `web/`, `run.cmd`, `run.sh`, this README, the
example settings, `INTERFACE.md`, `references/` (other games' values), `source/` (a read-only copy of the movement
code the Code tab shows: `pmove.cpp`, `pmove.h`, `gamedefs.h`, `customrules.cpp`), the game's default maps, and `quark_maptool(.exe)` from the checkout's build
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

`serve.py` keeps two `quark_maptool physserve --stream` running and sends them the page's runs, equation checks and
exports as lines (a map sent as text is parsed once and kept): an edit's result comes back in about 7 ms instead of
about 20 ms with a new process per request (Linux; a process costs more on Windows). An older `quark_maptool` without
`physserve` gets a process per request, as before; a helper that dies, hangs or is rebuilt is restarted.
`QPE_NO_WORKER=1` turns it off.

## The page

The left panel is always there:

- **Physics set**: open a saved custom mode (`(n changed)`: the settings it really changes, as the game reads it;
  `· logic`: it has a gamemode logic section), give it a name, **New** (the standard physics), **Save** / **Save
  as…** (writes `custom_modes/<name>.qmode`), **Open file** (any .qmode from disk), **.qmode** (download it), **Reset
  all**, and **↶ / ↷** undo / redo (**Ctrl+Z**, **Ctrl+Shift+Z** or **Ctrl+Y**; in a text field the browser's own
  undo). Every change to the set is a step (values, equations, the Code text, a game's values, a reset; a slider drag
  or typing in one field is one step); opening another set starts a new history. Switching sets, New, Open file and
  Reset all ask first when the set has unsaved changes (in the page). The line under it counts the changed values
  and says whether the set is saved.
- **Scenario**: a **Preset** (the helper's: standing / running jump, run-up, stop, bhop chains plain and with the
  strafe bot, dash, dash jump, super dash, dash into a wall, grapple swing, ladder climb, stairs, max gap, fall), the
  **Map** (the built-in test room, or any game or user map: then **Start at** picks a spawn), saved scenarios
  (**Load…** / **Save**: `physics_editor/scenarios/<name>.json`, with its map), **Run**, and **re-run on every
  change** (on by default: each edit re-runs the scenario, debounced, one run at a time; a run of every set takes a
  few ms). The presets are made for the test room: on a real map the panel says so (a wall or a ledge ahead cuts them
  short) and **aim along open floor** starts from the spawn and direction with the longest run of floor ahead (walls
  and drops checked every 0.5 m, 40 m at most). Back in the test room from a map, a preset starts where it always
  does, and a custom scenario (the Blocks tab, a saved one) keeps its steps and starts where it started in the room
  before (the room's origin if it never was there). When a run fails, the line under **Run** says why, and links to
  the line of the .qmode the error names (click: that line selected in the Code tab); the results of the last good
  run stay, greyed and labelled "last good run" (Results, the Movement graph, Path), or are cleared when that run was
  of another scenario or on another map.
- **Compare with**: the standard physics (always) and any of your saved sets, run side by side.
- **Results**: the scenario's main numbers for the edited set, with the difference from the standard (green up,
  red down). The preset's own metrics come first, always: one a run hasn't got says why (`jump distance: didn't
  land within 2 s`, `no dash in 1 s`), here and in the Movement graph's table.

### Values

Every movement setting in groups (general, ground, air, dash, super dash, grapple, ladder), each with a slider
(logarithmic, it snaps to 1), the multiplier (what the .qmode stores; 1 = standard), the absolute value in its
unit (m/s, s, m...) and the standard value. Changed rows are orange; ↺ resets a value, **reset group** a group,
**Reset all** everything. Ranges are what the game allows (from `physparams`). Hover a row for what it does and the
C++ constants it scales. **dash** and **grapple** are switches. `crouch_speed` is struck through: crouching was
removed from the game. The filter box searches names, units and notes; **only changed** hides the rest.

**fixed / ƒ / ⌒**: each setting is a fixed multiplier, an equation or a curve (`INTERFACE.md`, v2):

- **ƒ equation**: type an expression (`expr.<key>=` in the .qmode). It can use other settings by key (their
  multiplier: `friction=sqrt(speed)`), `base.<key>` (a setting's standard absolute value), the player's state
  (`p.speed p.hspeed p.vspeed p.air p.ground p.onground p.charges p.dashing p.hooked p.height`), numbers, `pi`,
  `+ - * / ^`, comparisons, `&& || !`, `?:` and `sqrt abs min max pow exp log floor ceil round sign clamp lerp step
  ease curve`. The text is highlighted as you type; **Tab** / **Enter** complete a name (arrows pick); an error is
  shown live with its column. A badge says **static** (numbers and settings only: worked out once when the rules
  load) or **dynamic** (reads `p.`: worked out at every 1 ms pmove step), and the line under it shows the value
  standing still (× and absolute), any clamp to the setting's range, and the game's own evaluator's value
  (`physexpr`) next to the page's. A dynamic equation is plotted against the input it reads.
- **⌒ curve**: a visual editor for `curve(input, x0, x1, y0, y1, "ease")`: pick the input (any `p.` input or a
  setting), its bounds, the multiplier at each end (kept inside the setting's min..max), and one of the 13 easings
  (linear, in / out / inout, in3 / out3 / inout3, smooth, smoother, expo / outexpo, back / outback; each button
  draws its shape). Drag the two end points on the plot. It writes the expression text; a text that isn't a single
  curve opens in equation mode.

The setting's fixed line stays in the file with the equation's value standing still (so an older game still reads a
sensible value), and it follows when a setting the equation reads changes. Expressions run in pmove when the helper
has `physexpr` (it runs them on the server and the client alike); with an older helper the page says so and sends
the fixed values only. The MOCK runs them too.

**Compare with games**: a searchable picker of other games' default movement (`references/`: 37 games grouped by
engine family, or sorted **closest** to Quark's standard first). Each picked game adds a column: its value in
Quark's unit and as a multiplier, a confidence dot (green exact: the game's code or config; amber measured; red
estimated: derived here), ⚠ when the multiplier is outside what Quark allows (it is clamped when applied), and
**no analogue** where the game has nothing for that setting. Hover a cell for the game's own number, the source and
notes; hover the header for the scale used, the notes and the game's mechanics that Quark doesn't have. **use** takes
one value, **load all** every value the game maps (dash and grapple included; settings with no analogue keep
theirs). Both are recorded in the set as an import (`# imported from <game> (<id>): keys`, a comment the game
skips; a row taken from a game is marked until you change it). Under the groups, **reference facts** lists what has
no Quark setting behind it (jump height, time to 90% of run speed, stopping distance and time, walk / sprint speeds,
ability distances) next to Quark's standard. The picks are remembered in the browser. A picked game can be ticked
under **Compare with** and picked as A or B in the Movement graph: it runs as a physics set made of its mapped
settings only (the rest stay at Quark's standard; the page says so).

### Code

Two views (the choice is remembered):

- **.qmode file**: the set as the raw .qmode text, two-way: edit the text and Values follow; change a value and the
  text follows. Movement lines are coloured (an `expr.` line's expression highlighted like the editor's, an
  `# imported from` line marked), the gamemode's other lines dim; problems (a number that isn't one, a value out of
  range and clamped, an unknown key, a duplicate that the game ignores, an expression that doesn't parse) are marked
  in the gutter and listed under the text (click one to jump to its line). From a `logic` line on, the file belongs
  to the gamemode editor (a line that is `logic` at its start, then nothing, a space or a tab: the game's own rule):
  it is shown read-only under the text and never changed. **Tidy** rewrites the movement lines the way the game
  writes them: one line per setting (the earlier duplicates the game ignores go), `move_` then `expr.` lines in the
  game's order after `rounds=`; keys this build doesn't know, the other lines and the logic stay where they are.
- **Movement code**: the game's real movement code, read-only (`src/game/pmove.cpp`, `pmove.h`, `gamedefs.h`, as
  they are in the checkout, served by `/api/source`), syntax-highlighted. Every line that reads a setting carries a
  chip: `kConstant base × multiplier = value unit` for the edited set (orange when changed; an equation shows `ƒ`,
  its text and "at rest"); constants a setting scales get a dashed chip where they are defined. Click a chip to go to
  that setting in Values. **notes** puts a short plain-language note above each function and each step of
  `pmoveStep`; **only lines that use a setting** folds the rest; the find box searches. The side lists every setting
  with how often the code reads it (click: the next place) and every function and step (click: go there; overloads
  show their parameter names). Functions are found by brace matching however long they grow; if a game-code change
  leaves the page unable to find `pmoveStep`, one of its steps or a function with a note, a bar above the code (and
  above the Code graph) says what it couldn't find, instead of a quietly empty graph.

### Code graph

How the settings reach the movement, drawn from `pmove.cpp` itself: the settings (left) → the steps of one 1 ms
`pmoveStep`, in the order they run (middle: wish direction, jump, dash timers, dash start, dash jump / super dash,
dash burst, grapple, pulled by the grapple, ladder, ground friction + accelerate, air strafe / drift / control /
gravity, the move through the map, step-ups, wall bounce, blocked, landing, dash cooldown, charges back) → what they
change (right: velocity, position, on the floor, dash charges and state, cooldowns, the grapple, held buttons).
Dashed edges are reads inside a called function (**through called functions** shows them). Changed settings and
everything they reach are highlighted; **only changed settings** hides the rest. Hover for what a node is; click a
step or a setting to see its code; drag to pan, wheel to zoom, **Fit** (or double-click) to frame it. The layout is
automatic (no library).

### Blocks

The scenario as a stack of blocks on a timeline: drag blocks from the palette into the stack, drag them to reorder,
drag one back to the palette to delete it. Blocks: **Hold keys**, **Wait**, **Turn**, **Look at**, **Jump**
(press length), **Hold jump** (bunny hops), **Jump at the edge**, **Dash** (aim pitch, press length), **Dash
jump**, **Super dash**, **Grapple** (aim pitch), **Crouch** (ignored by the game), **Strafe-bot hops** (jump held,
A / D at the best air-strafe angle every ms; swap off, at each landing, or weave 25° around the course) and a raw
**Step** (any step field). The header holds the start: position, yaw, pitch, a start speed, on the floor, and the
time after the last block. The timeline shows each block and the edited and standard runs' horizontal speed: under
the stack on a narrow window; on a wide one (1800 px and more, q's 2560) in a column of its own beside the stack, the
page's full height, with the height curve in a second lane and the block edges carried down through both.
**{ } JSON** edits the scenario as JSON; a scenario the blocks can't express exactly is shown as raw steps.

### Movement graph

**Curves**: horizontal speed, speed, height, vertical speed, distance and dash charges against time, one chart each
(tick the ones to show): the edited set solid, the standard dashed, compared sets, games and Drive recordings in
their own colours. Event markers (jumps, landings, dashes, hooks, ladders, bounces) on top; hover for every run's
values at that time. Under the charts, the metrics table: every number the helper reports for each run, with the
edited set minus the standard and the change in %.

**3D**: the runs' trajectories over the map, orbit camera (drag: orbit, right drag or Alt: pan, wheel: zoom), a
trail per set in its colour, 0.1 s ticks, events as coloured posts, and a time scrubber (or ▶) that puts each run's
player box where it was then, with its speed and height. A re-run after a value change keeps the camera; a new
scenario or map refits (**Fit** does it by hand).

**lock units**: fix any axis (time, each series, and the 3D camera) at a range you type; a locked axis stays put
across runs and between sets (🔒 on the chart). Unlocked axes are automatic (their current range is shown as the
placeholder). The locks are remembered in the browser.

**A / B**: two sets against each other, each one of the edited set, the standard, any saved .qmode or any picked
game. **overlay** draws both on one chart; **side by side** draws two charts (and two 3D views with one camera)
sharing their ranges; ⇄ swaps. The table then shows A, B and B − A (and %). A game as A or B is flagged: only its
mapped settings are applied.

### Path

The runs from above and from the side (along the start's facing), over the map, with 0.1 s ticks and the events;
drag to pan, wheel to zoom, **Fit** to frame the runs. **3D** shows an orbit view of the map and the paths.

### Drive

Click the view to play (pointer lock): **WASD** + mouse, **Space** jump, **Left Shift** dash, **Q** grapple (the
game's default binds), **C** or **Ctrl** crouch (no bind in the game: it ignores crouch), **R** back to the start,
**V** first person / chase camera, **Esc** lets go of the mouse. The real helper runs your input through the game's
`pmove` (`physdrive --stream` over a WebSocket, or Server-Sent Events if WebSockets fail): commands are sent every
frame in pieces of at most 8 ms, and the view runs a few ms ahead of the last answer. With the mouse let go the view
is drawn only when something changes, and nothing is sent while the player stands still. The side panel picks the
physics (the edited set or the standard), the camera, mouse speed (0.005..2 degrees per pixel), FOV (40..150) and the
start (the scenario's, a room part, a map spawn); it shows the speed over the last 10 s, the events, and, while you
play, the frame rate (average, 1% and 0.1% lows) and the helper's round trip, a frame-time strip (the last 600 frames
played, lines at 60 / 30 / 15 fps, spikes in red) and a spike log (a frame over twice the median and 8 ms more: its
game time, length, × the median and the helper's round trip then; cleared by a restart). **→ Graph** sends the drive so far to the Movement graph and Path tabs as a run.

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
- from a `logic` line to the end of the file is the gamemode editor's section and is never touched (`logic` at the
  start of the line, then the end, a space or a tab: `isModeLogicLine` in `src/game/customrules.cpp`);
- a new file gets only the game's header (`custommode=<name>`, `mode=ffa`) and the movement lines;
- a `move_` line whose key isn't a `move_` setting (`move_gravity`, `move_speed`: the game ignores them, only the
  plain `gravity=` / `speed=` count) is warned about in the Code tab and left out of the set, as the game reads it.

The Code text's `custommode=` line follows the **Name** field (it is what a save will write). Names are checked the
way Windows sees them, on every system: a name that differs only in case from a saved set is that set's file (Save
asks before writing into it, and keeps its name), and Windows device names (`con`, `nul`, `aux`, `com1`...) are
refused by the page and by `serve.py` (custom modes and scenarios).

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
| `web/js/expr.js` | equations: the grammar of `src/game/moveexpr.cpp` (parse, check, compile, resolve in dependency order, the easings and `curve`), highlighting and completions |
| `web/js/exprui.js` | the equation editor and the curve editor in Values |
| `web/js/refs.js` | other games: `references/` index and files, the game picker, a game as a physics set |
| `web/js/source.js` | reads the movement code: highlighting, constants, every read of a setting, functions, the steps of `pmoveStep`, what each step changes, the notes |
| `web/js/movecode.js`, `codegraph.js` | Code › Movement code; the Code graph tab |
| `web/js/graph3d.js` | the Movement graph's 3D view |
| `references/` | other games' movement values (`references/README.md`; `check.py` checks them) |
| `web/js/gl.js`, `qmap.js` | WebGL drawing of a map; .qmap parsing |
| `web/js/mockpm.js`, `sim.js`, `room.js` | the MOCK: pmove in JS, the scenario runner and metrics, the stand-in room |
| `web/test/run.mjs`, `source_check.mjs` | the checks; the movement-code reading check |

`serve.py` also serves `/references/<file>` (the references folder, nothing outside it) and
`/api/source?file=pmove.cpp|pmove.h|gamedefs.h|customrules.cpp` (the checkout's file, else the installed `source/`
copy; any other name is refused).

Your data, all in the game's data folder: `custom_modes/<name>.qmode` (physics sets), `physics_editor/scenarios/`
(scenarios), `physics_editor/state/` (the page's last session).

## Checks

`node tools/physics_editor/web/test/run.mjs [--shots DIR] [--maptool PATH] [--mock-only] [--port N] [--maps-dir DIR]` (Node 18+,
Playwright already installed; it never installs browsers). It starts `serve.py` on a spare port (or `--port`) with a
temporary data folder (and `--maps-dir`, a folder of built maps, when the checkout's `maps/` isn't built: the map
checks need one) and checks:

- the page's .qmode merge against serve.py's on awkward files (CRLF, a logic section, duplicates, new files, `expr.`
  lines kept / dropped / added, an import line replaced in place);
- the expression evaluator: precedence, every function and easing, curve bounds, errors (chained comparisons, bad
  names, too long), static / dynamic, dependency order, cycles, clamps, NaN; with the real helper, the page's inputs,
  functions and easings against `physparams.expr` and its values against `physexpr` on 50 expressions;
- the MOCK running dynamic equations, and against the game's `physsim` on the same equations;
- `/api/source` and `/references/` (and that they serve nothing outside them); every preset round trip through
  Blocks; the MOCK against the game's numbers;
- in Chromium, with the real helper and with the MOCK: Values → Code and Code → Values, a preset run, dragging a
  block, a map run, Save keeping the logic section and the other lines, a new file, Drive with W and Space, the
  WebSocket and SSE round trips, Export; then the game picker (search, closest first, a column, use, load all and the
  import line, reference facts), an equation (the expr line and its value at rest, static / dynamic, Tab completion,
  a live error), a curve (easing and bounds in the text, clamped to the range), Movement code chips and the jump to
  Values, the Code graph and a click to the code, the 3D view and its scrubber, lock units, A / B with a game as B,
  B − A, side by side in curves and 3D;
- the 1.18.31 fixes: the logic line against the game's rule, label / weaponset / wsline, Tidy (one line per key, the
  same physics, idempotent), the Open list's count, physserve (the same answer as a process, a bad request), and in
  Chromium undo / redo, Reset all and the unsaved-changes questions, the session written on pagehide, a filter that
  matches nothing, a saved scenario loaded back, a preset on a map and "aim along open floor", the theme button and
  Drive's key caps in light, Tidy, Export's debounce, two columns of lined-up groups at 2560 and nothing wider than
  a 1280 window;
- the 1.18.32 fixes: the movement code read (every step of `pmoveStep`, the parser on a 5000-line function, braces
  in strings and comments, overloads), `move_gravity` / `move_speed` warned and left out (a run with them works), and
  in Chromium a failed run (greyed last good run, the link to its line, cleared on another map), a custom scenario
  back in the test room, the Name field's `custommode=` line, a name in another case and a Windows device name, a
  jump that doesn't land, Blocks' timeline beside the stack at 2560 and under it at 1280, Drive's spike log.

`python3 tools/physics_editor/native/test_phys.py [quark_maptool]` checks the helper's commands, physserve included.

`node tools/physics_editor/web/test/source_check.mjs [pmove.cpp]` (Node only, about 0.1 s) checks that the page still
reads the game's movement code: `pmoveStep`, every one of its steps and every function with a note found (exit 1 and a
line per miss otherwise), plus the parser's own cases (a 5000-line function, braces in strings and comments,
overloads). Run it after changing `src/game/pmove.cpp`; `run.mjs` runs it too.

Without `references/index.json` it writes a stub and passes it with `--references`. It saves a screenshot of each
tab and view (default `/mnt/project-files/physics-editor/shots-v2/`).
