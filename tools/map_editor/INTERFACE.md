# Map editor: interface between the page and the native helper

Three parts, owned by separate workers:

- `tools/map_editor/native/` + a CMake target `quark_maptool` (C++, built from the game's own `src/game` map,
  pmove and bot code and `tests/walkgrid.h`): every map-format, physics and bot answer. JSON on stdout.
- `tools/map_editor/serve.py` (Python 3, stdlib only): serves the page on http://127.0.0.1:<port>/ and runs
  `quark_maptool` for the page (`POST /api/<command>`, body JSON, answer JSON). Also lists/reads/writes map files.
- The page (`tools/map_editor/index.html`, `editor.css`, `js/*.js`; `js/deco*.js` is the decoration worker's).

No map format change: the page edits a JSON form of the map and the helper writes the `.qmap` text (the game's own
`Map::serialize`). Editor-only data lives in a sidecar `<map>.mapedit.json` next to the map: `figures`, `botStarts`,
`tests`, `deco` (decoration rules), `objects` (custom objects: `{name, def, rot, min, max}`, see `js/repeat.js`; their
boxes are also written into the .qmap as plain boxes), `camera`, `bots`.

Server routes besides `POST /api/<command>` (see `serve.py`'s docstring): `PUT /api/map/mine/<name>` (refused with
422 when `quark_maptool parse` can't load the text), `DELETE /api/map/mine/<name>` (moved to `deleted/`),
`POST /api/rename`, `GET|PUT /api/clipboard`, `GET /api/library`, `PUT|DELETE /api/library/<name>`.

## Map JSON (`quark_maptool parse`, `quark_maptool serialize`)

```
{ "name": "temple", "title": "Temple", "formatVersion": 2, "handmade": false, "setting": "...",
  "preferredMode": "duel" | null, "allowedModes": ["ffa", ...] (empty = all), "ranked": ["vanilla", ...],
  "sun": [x,y,z], "palette": {"base":[r,g,b],"accent":..,"metal":..,"glow":..,"sky":..} | null,
  "void": y | null, "indoor": y | null, "holdfire": s,
  "brushes":  [{"mat": 0..9, "min":[x,y,z], "max":[x,y,z]}],          // editable boxes only, in file order
  "generated":[{"mat":..,"min":..,"max":..}],                          // ladders' rails/rungs + group parts (read-only)
  "groups":   [{"type":"custom|room|stairs","mat":..,"dir":0..3,"min":..,"max":..,
                "parts":[{"mat":..,"min":[fx,fy,fz],"max":[..]}]}],    // parts as 0..1 fractions (custom only)
  "spawns":   [{"pos":[x,y,z],"yaw":radians,"team":"a|b|any|race|att<N>|def<N>"}],
  "sites":[{"min","max"}], "hill":{"min","max"} | null, "points":[{"min","max"}], "checkpoints":[{"min","max"}],
  "path":[[x,y,z]], "cartChecks":[0..1], "ladders":[{"min","max","auto":bool}],
  "stairs":[{"x0","z0","x1","z1","base","top","axis":"x|z","rising":±1,"solid":bool}],
  "lights":[{"pos":[x,y,z],"radius":r,"intensity":i}],
  "materials": ["box","accent","wallbang","metal","glow","far","water","lava","trim","clip"],
  "player": {"halfWidth":0.4,"height":1.8,"crouchHeight":..,"eye":..,"crouchEye":..,"stepUp":..,"jumpHeight":..} }
```

Y is up, metres. `serialize` takes the same JSON (generated/materials/player ignored) and returns `{"text": "..."}`;
`parse` takes `{"text": "..."}` or `{"file": "path"}`. Errors: `{"error": "..."}` with a non-zero exit.

## Test commands (all take `{"text"|"file"|"name"|"map", ...options}`, all return JSON)

Full shapes, every field and the custom test kinds: `native/README.md` (and `quark_maptool help`, which returns
`{commands:[{name,help}], customKinds:[{kind,title,desc,fields:[{name,type,default,desc}]}], player}`).

- `check` `{"only"?:[ids], "outOfBounds"?:bool}`: the map safety checks (spawns not in walls, preferred mode
  supported, every spawn reaches every objective, ladders climbable, stairs walkable and cutting no path,
  out-of-bounds reach, ...). `{"checks":[{"id","name","pass","skipped"?,"detail","where":[[x,y,z],...],"items",
  "ms"}], "pass", "failed":[ids]}`.
- `metrics` `{"mode"}`: time to points etc. (from 07ef254:tests/mapreport.cpp, no pass/fail bar):
  `{"objectives":[{"name","kind","pos","attackersAre","defendersAre","attackersM","defendersM","attackers":s,
  "defenders":s,"diff":s}], "routes":[{"from":spawnIndex,"team","role","to":name,"objective":name,"metres",
  "seconds","path":[[x,y,z]...]}], "spawns", "reach":..., "numbers":{...}}`.
- `route` `{"from":[x,y,z],"to":[x,y,z],"radius"?,"nav"?}`: one walk-grid route `{"seconds","metres","path":[...],
  "reachable":bool,"fromStands","toStands"}`.
- `bots` `{"mode","bots":N|[specs],"seconds":S,"skill":..,"seed","tickRate","rate","placed":[{"pos","yaw","team",
  "skill","name","brain"}]}`: a headless match with the game's bots; `{"rate":10,"bots":[{"id","name","team",
  "skill","trace":[[t,x,y,z,yaw,state]...]}],"traceFields","events":[{"t","kind","who","victim","pos","detail"}],
  "result":{...},"ms"}`.
  `bots --stream` (request on stdin's first line, then control lines `pause` / `resume` / `speed <x>` / `step` /
  `stop`): NDJSON `{"type":"start"|"sample"|"event"|"warning"|"done"|"error",...}`, samples at `rate` Hz of game
  time paced at `speed`x real time, each bot's pos, yaw, pitch, vel, health, weapon, alive, state, target, goal,
  waypoint, path.
- `custom` `{"tests":[...]}`: a custom test set (shape in `native/README.md`), `{"results":[{"name","kind","pass",
  "value","detail","where"}], "pass", "failed"}`.

Additions to the map JSON from `parse`: `humanMade`, `modesSupported`, `modesPossible`, `killY`, `payloadLength`,
`bounds`, `warnings`; `player` also has `runSpeed`, `headFraction`, `headWidthFraction`, `headBox`, `grappleRange`,
`bodyScales`; `{"withText":true}` adds `serialized` (Map::serialize). `serialize` also returns `valid` and
`parseError` / `warnings`. Note JS/Python JSON turn `-0` into `0`: a page round trip can change the text (not the
geometry) of maps with `-0` coordinates.
