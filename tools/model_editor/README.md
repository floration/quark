# Quark Model Editor

A local page for looking at and replacing Quark's models: every model in the game's asset export (5 players,
11 first-person weapons, 7 projectiles, the crown, and any kind a later game adds), shown the way the game draws them,
editable box by box, with an export the game reads. It runs from disk (`file://`) in Chrome or Edge with no server and
no network. WebGL2, the glTF reader / writer and the zip are written here (no libraries).

## Build / rebuild

You need the game's asset export (`build/quark_tests exportassets <folder>`, see docs/ASSETS.md), then:

```
python3 tools/model_editor/build.py <export folder> <output folder>
tools\model_editor\build.cmd <export folder> [output folder]      (Windows; default output %USERPROFILE%\Desktop\Quark\model-editor)
```

The output folder gets `model-editor.html` (this folder's `index.html` with `editor.css` and `js/*.js` inlined) and
`data/manifest.js`: every model of the export (its .gltf), the model overrides already shipped in this repository
(`assets/overrides/models/...`, which the game draws in place of the built-in boxes), and each map's palette and sun
(read from the export's `maps/*.qmap`). A browser blocks `fetch()` on `file://`, so the data is a script. About 300 KB.
Python 3 standard library only. `--page-only` rewrites only the page and keeps `data/`.
Rebuild after a game update so the page shows the current defaults. Your work is not in these files: it is autosaved in
the browser (IndexedDB) and in the project files you save.

The page reads the manifest's `Models` items generically (`models/<kind>/<NAME>.gltf`): a new kind shows up in the
library and the orbit view on the next rebuild. The game-specific views (first person, third person, in flight, the
lineup) and the colour slots the game drives are known for player / weapon / projectile / crown; a new kind is drawn in
the export's colours (tint 1) with its team slot in the team colour until `js/data.js` (`runtimePalette`) and
`js/scene.js` (`modesFor`) learn where the game draws it.

## Export to the game

**Export to game** writes each edited model as `models/<kind>/<NAME>.glb`, laid out like the game's own exporter (one
primitive per material named `body` / `team` / `glow` / `metal` / `base`, `_translucent` with alphaMode BLEND under
alpha 1, the box renderer's face shades baked into COLOR_0, floats and uint32 indices). The boxes' names and their
shadow / outside-only flags ride along in the mesh's `extras` (the game ignores them; this page reads them back, so an
exported model opens as boxes again).

- **Choose folder and write** (Chrome / Edge): pick `%APPDATA%\quark\overrides` to try models in the game without
  rebuilding (restart the game), or the repository's `assets\overrides` to ship them. Picking `%APPDATA%\quark` or the
  repository folder works too (the overrides folder inside is used). A same-named `.gltf` there is moved to `.bak` (the
  game takes the `.glb` first); model files there for models you did not edit can be moved to `.bak` too.
- **Download .zip**: the same layout plus `provenance.json`, to unzip into the overrides folder.
- **Download .glb** (the model panel): one model.
- **Project > Load the models in an overrides folder**: reads every `models/<kind>/<NAME>.glb|.gltf` in a folder back
  as edits (to carry on, or to see what the game will draw).

What the game does with an override (src/client/gltf.cpp, game.cpp): it draws the mesh lit and shadowed in place of the
boxes, **without the 1 px outline**, every triangle casting a shadow, and **without the part animations** of the box
models (the rocket's flame flicker, the bubble's wobble, the sticky core's blink, the locator screen's pulse); the
sticky and thrown knife still spin. Colours in a slot are multiplied by (current colour / the export's colour). More
than 20 000 triangles and it refuses the file and keeps the default. A file it cannot read is logged
(`override models/...: ...`) and the default kept. Hitboxes never change with a model.

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
  - **Third person**: a player with the gun arm, or holding the weapon as "it" (the viewmodel boxes at 2x on the
    hand, base forced to bright steel), the crown on the head (with the board's letter), hitbox overlay.
  - **In flight**: a projectile flying (a quarter speed at 1x) with its trail, spun as the game spins it.
  - **Lineup**: the game's `--models` screenshot lineup (5 players 5 m ahead, 1.5 m apart, facing you; turned aside
    holding the weapon as "it"), your viewmodel; camera yaw / pitch = the game's `--yaw` / `--pitch`.
  - Map palette (default or any map's: base / metal / glow, sky, sun direction), team friendly / enemy, alternate
    teams, crouch (the override squash: 1.2 / 1.8 in height), animations, shadows, in sun / in shade, outline colour
    and width, brightness, viewmodel position / size / colour, hitbox (0.8 x 1.8 x 0.8 m, head = the top 20 % at 45 %
    of the width), grid, floor.
  - **Draw as**: auto (as the game will: built-in boxes for an unedited default, override for anything edited,
    imported or shipped), built-in boxes, override .glb.
- **Editing** box models: a part list and fields (centre, half size, linear colour, an sRGB picker with an intensity
  multiplier for colours over 1, tint slot, alpha, shadow, outside only, name); click a box in the viewport; a gizmo
  (W move / E resize, Alt both sides, Shift 1 cm steps; else 1 mm); add, duplicate, mirror across x = 0, delete;
  arrows / PgUp / PgDn nudge; undo / redo for everything; reset to default; start from scratch.
- **Import** a .glb / .gltf (drop it or Import; a .gltf with its .bin picked together): read exactly as the game's
  loader reads it (node transforms, strips / fans, normalised colours times baseColorFactor, BLEND = translucent,
  material name = slot, textures ignored), shown as the game draws an override. A file made of boxes the way the
  exporter writes them (this editor's own export) opens as editable boxes.
- **Warnings**: over (or near) the triangle limit, much bigger than the default, outside the hitbox, a material slot
  the game does not set for that kind (a `glow` / `metal` / `base` material on a player, crown or projectile is drawn
  at white / reference: glow x1, x1.8, x6.7), the part animations an export loses.

Keyboard: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, 1-5 views, F frame, W / E gizmo, arrows / PgUp / PgDn nudge (Shift: 1 cm),
Ctrl+D duplicate, M mirror, Delete, Space animations, Esc deselect, ? the list.

## Accuracy (checked against the game)

The views were compared with the game's own screenshots (Linux, Xvfb, `quark --host ffa --map temple --models --yaw 270
--pitch 0 --screenshot`, `--weapon N` for each viewmodel, `--host hide --models --weapon N` for held weapons, harbor for
a slanted sun) rendered here with the same camera: model faces match to the pixel value (0-1 of 255), viewmodels 97-99 %
of their pixels within 3 / 255 (the rest on outline edges: two rasterisers). A model exported here loads in the game
(`--data-dir DIR` with `DIR/overrides/models/...`) and draws as this page's override view.

Known differences:
- A few built-in parts are computed from the team colour with an offset in the game's box path (`grey = team * 0.6 +
  0.06` on HEAVY / ROBOT / KNIGHT, the SCOUT visor and ROBOT eye `team * 1.8 + 0.3` / `* 2 + 0.4`): the export only
  has their colour at white, so here (and in game, once exported as an override) they are `team * that colour`:
  slightly darker greys and eye in a non-white team colour.
- The built-in crouch scales only some boxes' heights; an override (and this page) squashes the whole model.
- The default box models' shadow / outside-only flags are not in the export: they start as "casts, always drawn".
- The floor here is a stand-in for the map (no lightmap); the models' lighting is the game's.

## Source

`index.html` + `editor.css` + `js/`: `data.js` (constants from the game, the model registry, tints, state, undo),
`gltf.js` (the loader port, the .glb writer, boxes <-> meshes), `render.js` (WebGL2: shaders, passes, GPU meshes),
`scene.js` (views, cameras, animations, overlays, picking, thumbnails), `export.js` (folder / zip, provenance, import,
problems, project file, autosave), `ui.js`, `boot.js` (`Q.shot({...})` renders a fixed-size picture of a view: the
comparisons above).
