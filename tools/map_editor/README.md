# Quark Map Editor

A local page for building and testing Quark maps in 3D: every shipped map and your own, boxes, groups, spawns,
objectives, ladders and lights, with the game's own checks, time-to-point metrics, routes, custom tests and headless
bot matches played back in 3D. It runs in Chrome or Edge on http://127.0.0.1 from a small Python server (`serve.py`)
that asks the native helper `quark_maptool` (the game's own map, pmove and bot code) every map-format, physics and bot
question. Nothing leaves your PC: the server binds 127.0.0.1 only.

## Build / run

```
cmake --build build -j                                 (builds quark_maptool and the maps)
python3 tools/map_editor/build.py [output folder]      (default build/map_editor)
tools\map_editor\build.cmd [output folder]             (Windows; default %USERPROFILE%\Desktop\Quark\map-editor)
<output folder>/run.sh   or   <output folder>\run.cmd  (starts serve.py on a free port and opens the browser)
```

The output folder gets `index.html`, `editor.css`, `js/`, `vendor/three.module.min.js` (three.js 0.160.0, downloaded
at build time and checked against the sha256 in build.py; it is not in git), `serve.py`, `run.cmd` / `run.sh`,
`quark_maptool` (or `.exe`), `maps/` (copies of the default maps the build generated) and `models/player/*.gltf` (the
player models from `quark_tests exportassets`, when the build has `quark_tests`). Python 3 standard library only.
`build.py --page-only` copies only the page and serve.py (after editing the page). Rebuild after a game update so the defaults, the helper and the models match the game.

`serve.py [--port N] [--maps DIR] [--game EXE] [--no-browser]`. Your own maps are read and written in the game's
user maps folder (Windows `%APPDATA%\quark\maps`, macOS `~/Library/Application Support/quark/maps`, Linux
`$XDG_DATA_HOME/quark/maps` or `~/.local/share/quark/maps`; `QUARK_DATA_DIR` or `--maps` overrides it). The default
maps are read-only: save a copy under a new name. Settings (game exe, maps folder) are kept in `config.json` next to
`serve.py` and can be changed on the page (Settings).

**Provenance**: every save records the map in `provenance.json` next to `serve.py` (one entry per map, the latest save
wins): `{"format":1,"tool":"map-editor","items":{"maps/<name>.qmap":{"origin","by","date","note"}}}`. A map started
with New is `human`; a default (generated) map you edited stays `ai` ("edited from <default>") unless you tick
"I laid this map out" in the Save as dialog. The map list shows each map's origin.

Saving writes the map through the helper (`Map::serialize`, so it is a `handmade` map exactly as the in-game editor
writes it) and its sidecar `<map>.mapedit.json` next to it (custom tests, bot starts, decoration rules, the camera).
A map saved without edits to it (or with every edit undone) is written as its file's own text, byte for byte (a copy of
a default map is that map, its sha256 too); sidecar-only changes don't count as edits. `-0` coordinates (downtown,
foundry, gantry, summit) stay `-0` through the page, serve.py and the helper (`api.js` `toJson`).

`?embed=1` on the page's URL (e.g. `http://127.0.0.1:8770/?embed=1`, a tab inside another app's window) hides the
page's brand / title line; the toolbar and every panel stay, and the parameter is kept when a map opens.
**Open in game** starts `quark --editor <name>`, **Play test** starts `quark --host <mode> --map <name>` (the exe path
is set in Settings, e.g. `C:\Users\user\Desktop\Quark\testing\quark.exe`).

## Using it

- **Maps** (left): defaults and yours; New, Save (Ctrl+S), Save as. A default map opens read-only until you save a copy.
- **View**: fly (hold the right mouse button + WASD, Space / C up / down, Shift fast, like the in-game editor),
  orbit (Alt + drag, or the Orbit button: left drag orbits, right drag pans, wheel zooms), top-down (T), frame the
  selection (F). A 1 m grid on the floor and a scale bar.
- **Edit**: click selects (Shift adds, Ctrl+click a group's part), drag the arrows to move, the face handles to
  resize, all snapped to the grid (`[` `]`: 1/8 m .. 4 m like the in-game editor). Ins / B add a box, Ctrl+D
  duplicates, Del deletes, M cycles the material, Ctrl+Z / Ctrl+Y undo / redo. The properties panel edits numbers.
- **Objects**: spawns (team colour, yaw arrow), bomb sites, hill, capture points, race checkpoints, payload path and
  its checkpoints, ladders, lights, groups (select a group: its parts are listed and drawn).
- **Player**: drops life-size figures (the game's player models, or the 0.8 x 0.8 x 1.8 m hull with its eye height and
  crouched hull); a toggle shows one at every spawn; the hitbox / head box wireframe toggle.
- **Testing**: Checks (click a result to fly to its `where` points), Metrics (time to every objective per team, the
  routes drawn in 3D coloured by time), Route (click two points), Custom tests (a form, kept in the sidecar), Bots
  (place bot starts, choose mode / count / seconds, run: watch it live as the helper streams it, then scrub the
  timeline with speed, trails, per-bot toggles and event markers).

## Decoration hook

The decoration tools are a separate part (`js/deco*.js`, owned by the decoration worker). The page loads every
`js/deco*.js` file the server finds, as ES modules (`<script type="module">`), in name order, after three.js and
before the editor starts; `window.THREE` is set first, or import `../vendor/three.module.min.js`. If one of them
sets `window.MapDeco`, the page calls, once the first map is open:

```
MapDeco.mountPanel(panelElement, api)
```

`panelElement` is an empty `<div>` in the right dock's **Decoration** tab (the tab only shows when MapDeco exists).
`api`:

| call | what |
|---|---|
| `api.getMap()` | the open map as the map JSON of `INTERFACE.md` (a deep copy: change it freely) |
| `api.setMap(newMap, label)` | replaces the open map with `newMap` (same JSON) as ONE undo step named `label`, redraws, marks the map unsaved. `generated`, `materials`, `player` are ignored (the helper remakes them on save) |
| `api.getSelection()` | `{brushes: [i...], groups: [i...]}`: indices into `map.brushes` / `map.groups` |
| `api.select(sel)` | selects `{brushes, groups}` (either may be left out) and frames nothing |
| `api.sidecar.get(key)` / `api.sidecar.set(key, value)` | this map's sidecar entry (use key `'deco'`); JSON-able values; saved with the map |
| `api.redraw()` | redraw the 3D view (after you change something you draw yourself) |
| `api.on(event, fn)` | `'map'` (another map opened, or undo / redo changed it), `'selection'`, `'save'` |
| `api.layer` | a `THREE.Group` in the scene that is yours (the view's `deco` layer): draw previews there, then `api.redraw()` |
| `api.scene` / `api.THREE` | the three.js scene and module |
| `api.mapName` | the open map's file name (`""` for a new map not saved yet) |
| `api.maptool(command, body)` | `Promise<answer JSON>`: any `quark_maptool` command through the server |

`MapDeco.unmount()` (optional) is called before the page reloads the panel. The panel's presets are served from
`deco_presets/` (build.py copies `tools/map_editor/deco_presets/*.json`). Provenance: the page reads
`sidecar.deco.rulesApplied` (the rules whose boxes are in the map) and notes "decorations generated by rules ..." on save.

## Source

`serve.py` (server), `build.py` / `build.cmd` (assemble), `index.html` + `editor.css` + `js/`: `boot.js` (start,
layout, keys), `api.js` (server calls), `doc.js` (the open map, selection, undo, sidecar), `fallback_parse.js` (a JS
reader / writer of .qmap text, used only when the helper is missing), `view.js` (three.js scene: instanced boxes per
material, markers, cameras), `pick.js` (a uniform grid for picking), `edit.js` (tools, gizmo, snapping),
`panels.js` (map list, properties, outliner), `player.js` (figures, glTF reader), `testing.js` (checks, metrics,
routes, custom tests), `bots.js` (bot runs, live stream and playback).
