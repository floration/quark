# Map editor: decorations

Generates decorations on a map from **rules** you can edit, save as presets, and apply to the whole map, a filter
("every wall taller than 3 m of material 0"), a captured selection, a named tag (saved selection) or a group. The
output is ordinary brushes: **no map format change**. The sidecar (`<map>.mapedit.json`, key `deco`) keeps the rules,
the tags and the exact boxes each rule made, so they can be removed or regenerated later, even after the brushes
were reordered (matched by exact box + material).

Files: `js/deco.js` (engine), `js/deco_rules.js` (generators), `js/deco_ui.js` (panel), `deco_presets/*.json`
(shipped presets, `index.json` lists them), `test/deco_test.js` + `test/deco_qmap.js` (node tests). Plain JS, no build
step, no imports: the page loads them as modules in name order (`deco.js`, `deco_rules.js`, `deco_ui.js`), node
`require`s them; everything hangs off `globalThis.MapDeco`.

## Engine API (`window.MapDeco`)

| call | what |
|---|---|
| `generate(map, rules, targets?, opts?)` | `{brushes, perRule: {id: [boxes]}, report}`. Runs the rules in order (each sees what the earlier ones made). `targets` (optional) overrides every rule's target: a target object or a live selection `{brushes: [i], groups: [i]}`. `opts.deco`: the sidecar entry (tags; boxes any rule made are never targets). `opts.exclude`: boxes to treat as gone. Does not change `map`. |
| `apply(map, deco, ruleIds?, targets?)` | `{map, deco, report, added, removed, missing}`: takes out what those rules made last time, runs them again, appends the new boxes to `map.brushes`, records them in `deco`. No ids: every enabled rule (Regenerate all). |
| `remove(map, deco, ruleIds?)` | `{map, deco, removed, missing}`: takes out what the rules made (the rules stay). `missing`: recorded boxes no longer in the map (the user moved / deleted them: they are left alone). |
| `regenerateAll(map, deco)` | `apply(map, deco, null)` |
| `preview(map, deco, rules)` | `{add, remove, report}`: what `apply` would do. `rules`: ids, or rule objects (an unsaved draft replaces the stored rule with its id). |
| `newRule(gen, extra?)`, `ruleFromPreset(p)`, `ruleToPreset(r)` | rules with every parameter defaulted |
| `tagFromSelection(map, sel)`, `selectionOf(map, deco, target)`, `decoIndices(map, deco)` | tags from / to live selections; per rule, the indices of the brushes it made |
| `resolveTarget(map, target, deco)`, `cutsRoute(map, boxes, constraints?)`, `detectSymmetry(map)`, `groupBoxes(group)` | the pieces, for tools and tests |
| `mountPanel(el, api)`, `unmount()` | the panel (below) |
| `generators` | the registry: `{label, help, target, params: [{key, label, type: number|mat|bool|select|text|shapes, default, min, max, step, options}], run(ctx, P, T)}` |

## Rule, target, sidecar JSON

```
deco (sidecar key 'deco') = {
  "version": 1,
  "rules": [ rule, ... ],                         // run top to bottom
  "tags": { "<name>": { "brushes": [{"mat","min","max"}], "groups": [groupIndex] } },
  "rulesApplied": ["<rule name>", ...]            // names of rules whose boxes are in the map (the page's save note)
}
rule = {
  "id": "r...", "name": "Skirting", "gen": "edgeTrim", "enabled": true, "seed": 1,
  "params": { ...the generator's fields... },
  "target": target,
  "constraints": { "symmetry": "auto"|"none"|"point"|"mirrorX"|"mirrorZ", "maxCount": 0, "spawnClear": 0.75,
                   "objectives": "all"|"solid", "passMargin": 0.1, "allowBlockDrops": false,
                   "keepOut": [{"min","max","why","solidOnly"}] },
  "generated": [{"mat","min","max"}, ...],        // exactly what it made last time
  "last": {"units","boxes","rejected"}            // the last run's summary
}
target = {"kind": "all"} | {"kind": "filter", "filter": F} | {"kind": "set", "brushes": [{"mat","min","max"}], "groups": [i]}
       | {"kind": "tag", "name": "..."} | {"kind": "group", "index": i}       (+ optional "filter": F on any kind)
F = {"role": "wall"|"floor"|"ceiling"|"block"|"step"|"any", "mats": [0, ...], "minHeight", "maxHeight", "minLength",
     "maxLength", "minThick", "yMin", "yMax", "region": {"min","max"}}
```

Roles: wall = 2 m or taller; floor = a top at least 1.5 x 1.5 m with standing room over it; ceiling = an underside with
2.2 m+ under it; block = low cover; step = a box of a recorded staircase (`stairs` lines, stairs groups). With no
`mats`, targets are the visible solids (materials 0-4) and boxes thinner than 12 cm (other decorations) are skipped.
Boxes a rule made are never targets of another. Targets are taken in canonical (box) order and random numbers come
from the seed + a world-aligned grid cell, so the output does not depend on the order of the brushes.

Presets: shipped ones in `deco_presets/*.json` (listed in `index.json`); your own saved from the panel go to disk through
the server (`PUT /api/deco-presets/<name>` writes `deco_presets/user/<name>.json` in the editor's folder, name rule as for
maps; `GET /api/deco-presets` lists them), so they survive a fresh browser profile (the Electron shell); only when the
server can't be reached are they kept in the browser's storage. Export downloads the rules as a .json; Import reads one.
Shape:
`{"name", "description", "rules": [{"name", "gen", "params", "target", "constraints", "seed"}]}` (anything left out
takes the default). Shipped: Minimal trim, Industrial hall, Temple, Outdoor yard, Team map (point symmetric).

## Generators (after the decor in `tools/maps/prefabs.py`)

| gen | what | main params |
|---|---|---|
| `edgeTrim` | band along the bottom / top edges of faces: skirting, cornice (prefabs `trim`, `detail_pass`) | edges bottom/top/both, where (floor/ceiling meets it, free edges), height, depth, sink, mat (8), facing |
| `bands` | horizontal bands at heights over the floor: chair rail, glow line (`dress`) | heights "1.1, 0.25", from floor/absolute, height, depth, mat |
| `wallStrips` | vertical strips every N m and / or inset panels between them (`detail_pass` pilasters, `dress` bay panels) | style strip/panel/both, spacing, width, depth, gaps, panel from/to, mat |
| `pillars` | columns in inside corners, round outside corners, or along faces; base + capital (`pillar`, `dress` corner columns) | where inner/outer/spacing, size, depth, spacing, base, flare, mat (1 accent, solid) |
| `floorBorder` | inlaid border where walls stand on a floor and / or along its free edges; grid seams (`dress` floor seams) | border walls/edges/both/none, width, sink, raise, seams every N m, mat |
| `lightStrip` | glow strips or lamp fixtures under ceilings with headroom (`light` fixtures, roof-beam lamps) | style strip/lamps, along long/x/z, spacing, width, hang, margin, min headroom, mat (4 glow) |
| `windowFrames` | finds openings (two jambs facing each other, lintel over, sill/floor under, open both sides); frames both faces, sill ledge, optional lining | doors/windows/both, frame, depth, sill, lining, mat |
| `railings` | posts + rails along free floor edges over drops of at least N m (`catwalk` rails) | min drop, height, post spacing, post/rail size, mid rail, inset, mat (8) |
| `scatter` | crates / props on a jittered world grid over open floor, seeded (`clutter`, `scatter`, `crates`) | step, jitter, chance, shapes (w d h mat), stack chance, clearance, edge clearance, near walls, top crate mat |
| `kickPlates` | plate at the foot of every riser + nosing on every tread, on recorded staircases | plate height (0: whole riser), depth, mat, nosing, nosing width / mat |

Side faces are only decorated where they face play (a point 30 cm out is not in a wall and has ground with standing
room under it): not the outsides of the outer walls, not under the floor. Faces are the exposed parts only (what
stands against a face hides it), with coplanar faces of several boxes merged.

## Constraints (every unit, every generator)

A generator proposes **units** (boxes that go together: a pillar with its base and capital, a railing run, a crate
stack); the engine keeps or drops each whole unit:

- **Keep-outs**: spawns (the hull + `spawnClear`; and for solid boxes 2.5 m straight ahead at eye height, as the game's
  `spawn_facing` check wants 2 m), bomb sites, hill, capture points, checkpoints, the payload cart's
  sweep (`objectives: all` keeps every box out, `solid` only solid ones); ladders (+1.2 m round, +2.2 m over the top),
  staircases (+0.6 m at the ends) and detected doorways (+1 m in front and behind) keep out solid boxes; `keepOut`
  adds boxes of your own. Strips are cut round keep-outs instead of dropped.
- **Walk test** for solid boxes (materials 0-4 and 9: glow is solid, trim 8 is not): a local walk graph round the new
  boxes, before and after (standing spots every 0.1 m on every solid top, the player's 0.8 x 1.8 m hull, anything no
  higher than the 0.55 m step-up is stepped onto, neighbours joined within a step-up); every two spots on the edge of
  the area that were joined before must still be joined, with the new boxes grown by `passMargin` (so a passage keeps
  the player's width and height plus the margin). Run standing and crouched. Boxes inside an existing solid skip it.
- **Drops**: a solid railing would stop players walking off the edge, so it is refused unless `allowBlockDrops`.
- **Symmetry**: each unit is placed with its mirror or not at all. `auto`: point symmetric for ranked maps (the game
  checks `ranked_mirror`) and for maps whose solids are 90 % mirrored already (the shipped team maps), else none.
- **maxCount** units per rule; exact duplicates of any existing box are dropped; seeded randomness.

The report per rule: targets, units, boxes, why units were not placed (`keepout:<what>`, `blocks a passage`,
`blocks a drop`, `maxCount`, `dup`) with sample positions, warnings (targets not found, unknown tag).

## The panel and the page

`MapDeco.mountPanel(el, api)` with the hook of `README.md` ("Decoration hook"): `getMap`, `setMap(map, label)` (one
undo step per apply / remove / regenerate), `getSelection`, `select`, `sidecar.get/set('deco')`, `redraw`, `on('map')`,
`layer` + `THREE`, `maptool`. Panel: add a rule (by generator) or a preset (shipped ones fetched from
`deco_presets/index.json`, your own from the browser, or Import a .json), the rule list (enable, order), and per rule:
name, Preview / Apply (Regenerate) / Remove / Select made / Apply to selection, the target form (kind, capture the
selection, tag, group, filter: role, materials, sizes, region from the selection's bounds), the generator's
parameter form, constraints and seed (New seed re-previews), Duplicate / Save as preset (this rule, or every rule, under the name typed
next to it) / Export / Delete. Tags: save the
selection under a name, select a tag, add the selection to it. Global: Preview all, Regenerate all, Remove all, Check
map (the helper's `check` on the map as it is). The report shows under it.

**Preview** is drawn by the panel itself into `api.layer` (the page's `deco` THREE.Group): added boxes green,
boxes that would be taken out red (an InstancedMesh of translucent boxes plus their edges), then `api.redraw()`.
A page that would rather draw it can provide `api.setPreview({add: [boxes], remove: [boxes]} | null)`: when it exists,
the panel calls it instead and draws nothing. Without either, it calls `api.redraw({preview: {add, remove}})`.

Undo: `setMap` is one step, and a `sidecar.set` in the same task (the panel's apply / remove / delete rule, either
order) rides on it: one undo restores the map and the `deco` entry together, one redo puts both back. A `sidecar.set`
with no `setMap` in its task (a parameter, a rule added) marks the map unsaved and is not an undo step. Should the
records still name boxes that are no longer there (moved or deleted by hand), Remove / Regenerate report them as
`missing` and carry on, so the next Regenerate puts things right.

## Tests

`node tools/map_editor/test/deco_test.js [--maps DIR] [--maptool EXE] [--map NAME] [--quick]`: intervals and regions,
opening detection, the walk test (corridor, doorway, stairs, headroom, step-over), every generator on a hand-made
map (counts, nothing in spawns / objectives, deterministic, same output with the brushes shuffled, no solid box cuts a
passage), symmetry, tags / groups / selections / filters, apply / remove / regenerate after reordering, preview,
presets; then every map in `maps/` with every preset and every generator: deterministic, nothing in a spawn or an
objective, remove gives the map back, and with `build/quark_maptool` the game's own checks (walk routes, nav routes,
ladders, stairs, ranked mirror, ...) still pass on the decorated map wherever they passed before.
