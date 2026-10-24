# Quark Model Editor

A local page for looking at and replacing Quark's models: every model in the game's asset export (5 players,
11 first-person weapons, 7 projectiles, the crown, 6 props: the arms, the bomb carried and on the ground, the payload
cart and its checkpoint gates, and any kind a later game adds), shown the way the game draws them, editable box by box,
with an export the game reads. It runs from disk (`file://`) in Chrome or Edge with no server and no network. WebGL2, the
glTF reader / writer and the zip are written here (no libraries).

## Build / rebuild

You need the game's asset export (`build/quark_tests exportassets <folder>`, see docs/ASSETS.md), then:

```
python3 tools/model_editor/build.py <export folder> <output folder>
tools\model_editor\build.cmd <export folder> [output folder]      (Windows; default output %USERPROFILE%\Desktop\Quark\model-editor)
```

On Windows, `tools\model_editor\install.cmd [output folder]` from any repository checkout installs or updates the editor in one step: it builds only `quark_tests` (MinGW, in `build-model-editor\`), exports the assets and runs build.cmd (non-interactive, safe to re-run).

The output folder gets `model-editor.html` (this folder's `index.html` with `editor.css` and `js/*.js` inlined) and
`data/manifest.js`: every model of the export (its .gltf, with its box list), the model overrides already shipped in this
repository (`assets/overrides/models/...`, which the game draws in place of the built-in models), and each map's
palette and sun (read from the export's `maps/*.qmap`). A browser blocks `fetch()` on `file://`, so the data is a
script. About 400 KB. Python 3 standard library only. `--page-only` rewrites only the page and keeps `data/`.
Rebuild after a game update so the page shows the current defaults. Your work is not in these files: it is autosaved in
the browser (IndexedDB) and in the project files you save.

The page reads the manifest's `Models` items generically (`models/<kind>/<NAME>.gltf`): a new kind shows up in the
library and the orbit view on the next rebuild. Where the game draws each model, and which colour slots it drives, is
this page's copy of the game's code: `js/data.js` (`referencePalette`, `drivenSlots`, `runtimePalette`) and
`js/scene.js` (`modesFor`, the views). A new kind is drawn with the team slot in the team colour and every other slot
as authored until they learn it. At load the page checks its reference colours against the export's boxes and warns
(console and panel) when the game has changed them.

## Box models

Every model the game exports is a **box model**: its first mesh carries the boxes it is drawn from in
`meshes[0].extras.quark` (version 2, docs/ASSETS.md "Box models"): per box its centre and half size, its colour slot
(`body` / `team` / `glow` / `metal` / `base` / `accent`), its colour at the export's palette, and how that colour
follows the slot: drawn = slot colour x `mul` + team colour x `teamMul` + `add` (the HEAVY's grey is team x 0.6 + 0.06),
plus `alpha`, `shadow`, `visOnly` (face plates: not drawn on your own body) and `crouch` (players: a crouch squashes the
box's height too; every box's centre follows it). This page reads that list, draws it with the same formulas and writes
it back on export, so the game draws an exported model through its own box path: 1 px outlines, crouch per box,
outside-only boxes, per-box shadows, team colours exactly like a built-in model. An unchanged default exported here is
the same box list the game exports (number for number), and the game draws it pixel-identically to the default.

Editing a colour edits the colour at the export's palette (team white). For a slot box the page rewrites `mul` so it
draws as shown, keeping the default's `add` / `teamMul` while they still fit under the new colour (a recoloured grey stays
light in a dark team colour), dropping them otherwise. `mul`, `add` and `teamMul` can be edited directly too (the
fields under a slot box). Moving a box to another slot keeps the colour it shows.

Before export every box is checked against the game's limits: 1 to 256 boxes, centres within 64 m, half sizes over 0
and at most 32 m, colours / `mul` / `add` / `teamMul` from 0 to 16, alpha over 0 and at most 1. A model that fails is not
exported (the game would log it and keep the default). A file imported here whose box list fails them is refused the
same way.

## Export to the game

**Export to game** writes each edited model as `models/<kind>/<NAME>.glb`, laid out like the game's own exporter (one
primitive per material named `body` / `team` / `glow` / `metal` / `base` / `accent`, `_translucent` with alphaMode BLEND
under alpha 1, the face shades baked into COLOR_0 at the export's palette, floats and uint32 indices: what Blender or the
website show) with the box list in the mesh's extras (what the game draws).

- **Choose folder and write** (Chrome / Edge): pick `%APPDATA%\quark\overrides` to try models in the game without
  rebuilding (restart the game), or the repository's `assets\overrides` to ship them. Picking `%APPDATA%\quark` or the
  repository folder works too (the overrides folder inside is used). A same-named `.gltf` there is moved to `.bak` (the
  game takes the `.glb` first); model files there for models you did not edit can be moved to `.bak` too.
- **Download .zip**: the same layout plus `provenance.json`, to unzip into the overrides folder.
- **Download .glb** (the model panel): one model.
- **Project > Load the models in an overrides folder**: reads every `models/<kind>/<NAME>.glb|.gltf` in a folder back
  as edits (to carry on, or to see what the game will draw).

What the game does with an override (src/client/gltf.cpp, models.cpp, game.cpp):
- a **box model** (this page's export) is drawn exactly like a built-in model, except that a projectile's own part
  animations stay still: the rocket's flame flicker, the sticky's core blink and the bubble's wobble (the sticky and the
  thrown knife still spin, and the locator's screen still pulses);
- a **mesh** (an imported .glb / .gltf that is not made of boxes) is drawn lit and shadowed in place of the boxes,
  **without the 1 px outline**, every triangle casting a shadow, squashed whole when a player crouches; more than
  20 000 triangles and the game refuses it;
- colours: a box with `mul` uses the formula above; a mesh's colour in a slot is multiplied by (the current colour / the
  export's colour of that slot); a slot the game does not colour for that model (`glow` on a player, anything on an arm)
  is drawn as authored;
- a file it cannot read is logged (`override models/...: ...`) and the default kept. Ranked matches and time trials
  ignore your own overrides (only the shipped ones). Hitboxes and the cart's collision never change with a model.

### provenance.json

Every export writes or updates a provenance sidecar, `{"format":1,"tool":"model-editor","items":{"models/<kind>/<NAME>.gltf":
{"origin","by","date","source","note"(,"licence")}}}`, one entry per model id (the export / override path with .gltf),
the latest export winning, other entries kept. The zip carries it; a folder export merges it into
`<Quark\model-editor folder>\provenance.json` once that folder is chosen in the export dialog (remembered); otherwise
**Download provenance.json**. Origins: `human` (made from scratch here: **Start from scratch**, or chosen in the panel),
`ai` (a tweak of the AI-made default; the note says what changed, rewritten as you edit until you write your own),
`third-party` (an imported file from a pack: the import asks for its source and licence). The library shows each
model's origin. The editor never writes `assets/provenance/*.json` in the repository.

## What it does

- **Library**: every model by kind with a thumbnail (in the chosen map's colours), badges for edited, origin and
  problems; filter, edited only.
- **Viewer** (the game's renderer ported to WebGL2: the dynamic-object shader with sky light, sun, sun visibility,
  fog, the 2048 shadow map with its 4 taps, the 1 px screen-space box outlines, translucent parts unlit and blended, the
  sky shader; camera near 0.05 m, horizontal fov 100; no tone curve on models, as the game). Views:
  - **Orbit**: the model alone, to edit (drag to orbit, right drag to pan, wheel to zoom, F to frame).
  - **First person**: the weapon as your viewmodel (camera space 0.2, -0.17, -0.55 plus the HUD's viewmodel x / y /
    depth / size, drawn after a depth clear, unshadowed), optionally the lineup ahead; drag to look, wheel = fov.
  - **Third person**: a player with the gun arm (`GUN_ARM`), or as "it" the arm (`ARM`) holding the weapon's own model
    at 2x on the hand with its base forced to bright steel; the bomb on the back (`BOMB_PACK`); the crown on the head
    (with the board's letter); hitbox overlay.
  - **In flight**: a projectile flying (a quarter speed at 1x) with its trail, spun as the game spins it.
  - **Lineup**: the game's `--models` screenshot lineup (5 players 5 m ahead, 1.5 m apart, facing you; as "it" turned
    aside holding the weapon; in bomb mode turned away carrying the bomb with a dropped `BOMB` 3 m ahead), your
    viewmodel; camera yaw / pitch = the game's `--yaw` / `--pitch`.
  - **Track**: a payload track: the `CART` pushed toward -Z, a checkpoint `GATE` ahead (not reached: grey) and one
    behind (reached: the attackers' colour), the track line.
  - Map palette (default or any map's: base / metal / glow / accent, sky, sun direction), team friendly / enemy,
    alternate teams, crouch (per box, as the game; a mesh squashed whole), animations, shadows, in sun / in shade,
    outline colour and width, brightness, viewmodel position / size / colour, bomb mode, a planted bomb's red blink, a
    neutral cart, a reached gate, hitbox (0.8 x 1.8 x 0.8 m, head = the top 20 % at 45 % of the width), grid, floor.
  - **Draw as**: auto (as the game will: its own boxes for an unedited default, the box model file for anything edited
    or shipped as boxes, a mesh for an imported mesh), the game's own boxes, box model file, mesh (as a file without
    the box list would draw).
- **Editing** box models: a part list and fields (centre, half size, colour, an sRGB picker with an intensity
  multiplier for colours over 1, tint slot, `mul` / `add` / `teamMul`, alpha, shadow, outside only, crouch, name); click a
  box in the viewport; a gizmo (W move / E resize, Alt both sides, Shift 1 cm steps; else 1 mm); add, duplicate, mirror
  across x = 0, delete; arrows / PgUp / PgDn nudge; undo / redo for everything; reset to default; start from scratch.
- **Import** a .glb / .gltf (drop it or Import; a .gltf with its .bin picked together): read exactly as the game's
  loader reads it (node transforms, strips / fans, normalised colours times baseColorFactor, BLEND = translucent,
  material name = slot, textures ignored, the box list when it has one). A box model (or a mesh made of boxes the way
  the exporter lays them out) opens as editable boxes; anything else is shown as the game draws a mesh.
- **Warnings**: a box list over the game's limits, over (or near) the triangle limit, much bigger than the default,
  outside the hitbox, a slot the game does not colour for that model, translucent boxes outside projectiles (drawn
  opaque), the projectile animations a box model loses.

Keyboard: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, 1-6 views, F frame, W / E gizmo, arrows / PgUp / PgDn nudge (Shift: 1 cm),
Ctrl+D duplicate, M mirror, Delete, Space animations, Esc deselect, ? the list.

## Accuracy (checked against the game)

The views were compared with the game's own screenshots (Linux, Xvfb), rendered here with the same camera, on the
pixels of the models:
- the lineup (`quark --host ffa --map temple --models --yaw 270 --pitch 0 --screenshot`), viewmodels (`--weapon N`),
  "it" holding a weapon (`--host hide --map hamlet --models --weapon 1`), bomb mode (`--host bomb --map depot --models
  --weapon 1 --yaw 180`) and the cart (`--host payload --map railyard --cam 3 2.5 36 143.1 -18.8`, here: Track, camera
  3, 2.5, -4 from the cart): model faces match to 0-1 of 255; what differs is outline edges, the map hiding parts, and
  what the game's map lighting does to the floor;
- the 30 defaults exported here unchanged and installed as overrides (`--data-dir DIR` with `DIR/overrides/models/...`):
  the game's pictures are pixel-identical to the defaults (and the box lists number-for-number the game's export);
- edited models exported here (a bigger head, new team-coloured and outside-only boxes, recoloured greys, a longer gun
  arm, a bigger bomb and pack, a cart with a flag) look in the game as they do here.

Known differences: the floor here is a stand-in for the map (no lightmap, no map shadows: choose "in sun" off for a
model in the map's shade); the lineup's players are always the enemy colour in the game (warmup).

## Source

`index.html` + `editor.css` + `js/`: `data.js` (constants from the game, reference / runtime palettes, box colour
formulas, the model registry, state, undo), `gltf.js` (the loader port, the box list reader with the game's checks and
writer, the .glb writer, boxes <-> meshes), `render.js` (WebGL2: shaders, passes, GPU meshes), `scene.js` (views,
cameras, animations, overlays, picking, thumbnails), `export.js` (folder / zip, provenance, import, problems, project
file, autosave), `ui.js`, `boot.js` (`Q.shot({...})` renders a fixed-size picture of a view: the comparisons above).
