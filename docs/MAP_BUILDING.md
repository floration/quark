# Building Quark maps

The project's map-building document: how to build or change a map in Quark, from the file format to the design rules,
the movement numbers and the checks before a map ships. Written 2026-11-10 against `testing` at 1e7d2bd (VERSION
1.19.8, map format 14).

Contents
0. How to use this document
1. Quick start: a new map end to end in 10 steps
2. The map file and the pipeline
3. HARD RULES (never do)
4. Movement numbers for map makers
5. Design principles for Quark
6. Case studies
7. Workflow
8. Checklist before shipping a map
9. Open questions for q
10. Sources

---

## 0. How to use this document

**Who it's for:** every Claude thread and every person who builds or changes a Quark map. That includes generated maps
(`tools/maps/<name>.py`) and hand-made maps (q's, made in the in-game editor or Quarkology's map editor).

**Read it before you build or change any map.** At the least, read sections 3 (hard rules) and 4 (movement) and the
part of section 5 for your mode.

**q's rules win.** q is the owner and a human mapper. His rulings (listed below) override everything else here,
including the research in sections 5 and 6. When this document and a newer ruling from q disagree, q's ruling wins.
Add it here when you learn it.

q's standing map rules (newest first):

| date | rule |
|---|---|
| 2026-11-10 | When you judge maps or propose changes, **ignore the hard rules that come from bots or automated checks** (bot navigation from every spawn, ladder climbability, staircase-walkable checks and the like). Judge on how humans play. |
| 2026-11-10 | **ALL LADDER RUNGS MUST BE VISIBLE** in every map, no matter what. Never hidden in walls, floors or other pieces. A hard rule. |
| 2026-11-10 | The map fixers (z-fighting, merge + hidden, gaps, spawns) run on every map. Generated maps get them from the tidy pass in the build; hand-made maps ship with the editor's fixers applied. |
| 2026-11-03 | The qk maps are competitive 1v1 only: never retarget them. q rates QK3 the strongest. Don't judge a map weak from its brush count or size. |
| 2026-10-31 | Legacy maps stay (behind a toggle), never deleted. Ranked: qk1-8 plus the defaults greenhouse (vanilla 1v1), dojo (laser vampire 1v1) and spire (instagib FFA). Hiders get a 5 s head start. |
| 2026-10-30 | Bots needn't walk everywhere: "the grapple is insanely easy to use so even a complete noob can grapple across a gap or up a platform." Grapple-only routes are fine; don't add stairs just for bots. |
| 2026-10-30 | "begin with a layout, verify... then decorations, dont spam too many random object because momentum is huge in quark". The workflow is layout, then checks, then decoration, then adversarial review. |
| 2026-10-30 | Maps never have to be symmetrical. The test: "imagine people playing on this map: would it be fair even though it's asymmetrical?" |
| 2026-10-30 | Any 1/8 m thick surface counts as wallbang (hitscan goes through at the wallbang damage). `wallbang` brushes are dark grey. |
| 2026-10-30 | Bomb timing: attackers reach the site in about 5 s at 24 m/s (~120 m); defenders about 1 s later (~144 m). Bomb spawns follow the attacker/defender role, not the team. |
| 2026-10-30 | Any map that can be played 1v1 also offers duel and vs. Every mode has at least 2 maps built for it and at least 5 playable (tourney, duel, surf, one-way payload, points and race are exempt from the 5). |
| 2026-10-30 | qk1-8 may use slopes (wedges, corners, cut boxes); their materials are box and accent; no healing tiles on qk maps. |
| 2026-10-30 | Teleporters have a toggle: carry speed, or cap at walking speed. |
| standing | Physics: uncapped strafe-hop is intended; grapple ceiling pinning is intended; the hyper goes 90% up; rockets launch at 48 m/s with 16 m/s² of thrust. **There is no crouching in the game.** |
| standing | Walls collapse when 80% broken; glass takes at most 50 damage per pane. |
| standing | Netcode: everything is client-side and predicted; only deaths are server-authoritative. Don't propose map features that need the server to decide damage. |
| standing | Never tune or gate maps on bot runs. Ship to q to playtest once the checks pass. |
| standing | Dark player models must stay visible on dark maps. Maps must never be so dark that players vanish. |
| standing | Performance first, judged by 1% and 0.1% lows. Cobblestone's lava tunnel was a GPU hot spot (q's screenshot, 2026-10-28: ~120 fps there against ~1770 on the street). Heavy materials or effects in one view are a design cost. |

**Other documents** (linked, not repeated here):
- [MAP_STRUCTURES.md](MAP_STRUCTURES.md): the design record. One section per map: the mode it's built for, why its
  layout suits that mode and what every structure is for. Every new or changed map updates it.
- [CLAUDE.md](../CLAUDE.md), section "Maps": the official pool by mode, the ranked maps, the legacy maps, the format
  converter rule and the checks `quark_tests` runs.
- [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md): q's hand test before production. Keep it current when the pool changes.
- [ASSETS.md](ASSETS.md): overrides, including `assets/overrides/maps/<name>.qmap`.
- Each generated map's own docstring at the top of `tools/maps/<name>.py`.

**Words used here:**
- *generated map*: written by `tools/make_maps.py` from `tools/maps/<name>.py`. The file starts with
  `# generated by tools/make_maps.py`.
- *hand-made map*: has a `handmade` line, or wasn't written by make_maps.py. q's shipped ones: accelerator,
  amphitheatre, cobblestone, museum, reactors, riverside and surfy.
- *good player*: q's own level. He strafes at about 50-75% of perfect (section 4.4).
- Units are metres, speeds m/s, Y is up.

---

## 1. Quick start: a new map end to end in 10 steps

### 1.1 A generated map

Before step 1, build the game once: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`.
Headless Linux needs `Xvfb :99 &` and `DISPLAY=:99` in front of every `build/quark` command.

1. **Pick one mode and a name.** One mode only: the map's `for` line. The name is 1-40 letters, digits, `-` or `_`,
   and never a game mode's name. Read section 5's patterns for that mode.
2. **Sketch the loop graph and the timings on paper** (section 5.1 and 5.6). Write down the rooms and the links, the
   loops, each spawn's time to each objective, and the heights (section 4.5).
3. **Write `tools/maps/<name>.py`: the layout only.** Use plain boxes, wedges and objectives, with no decoration yet. A
   minimal module:

   ```python
   """YARD: a small king of the hill yard. Why the layout suits koth, and what each structure is for."""
   from .prefabs import *

   def build():
       m = M("Yard", "koth")                       # title, and the one mode it's built for (the "for" line)
       m.also = ["vs"]                             # more modes for its "modes" line (save() adds ffa)
       m.room(24, 24, h=8, style="hall")           # floor top at y = 0, four 8 m walls, clip over them
       m.box(-5, 0, -5, 5, 1.5, 5, accent=True)    # the hill platform, 1.5 m up
       m.stairs(-5, 5, 5, 8, 1.5, "z", -1, mirror=True)
       m.raw("hill -5 1.5 -5 5 4.5 5")
       for z in (-6, -2, 2, 6):                    # 4 spawns a side, mirrored onto team b
           m.spawn("a", -20, 0, z, -90, mirror_team="b")
       m.box(-17, 0, -9, -16, 3, 9, mirror=True)   # a screen: the spawn rows can't see each other
       m.light(0, 6, 0, 18, 0.8)
       m.raw("setting a walled storage yard")
       m.save("yard", dress=False)                  # dress=False while it's a layout
   ```

4. **Add the name to `MAPS`** in `tools/make_maps.py` (line 22). CMake picks it up on the next build, embeds it and makes
   it official.
5. **Build only this map:** `python3 tools/make_maps.py --map <name>`. This writes `maps/<name>.qmap` and runs the tidy
   pass. Read the file's head. It should show the generated marker, `version 14`, `title`, `for` and `modes`.
6. **Check it:** `echo '{"file":"maps/<name>.qmap"}' | build/quark_maptool check`. Add `"outOfBounds": false` for a
   fast run without the out-of-bounds check. Fix every *design* failure (section 3). For the bot and route checks, see
   section 3.2.
7. **Look at it and play it** (section 7.3):
   - `build/quark --backdrop <name> --cam x y z yaw pitch --screenshot shots/<name>-01.png`
   - `build/quark --host <mode> --map <name>`, played with full movement.
8. **Decorate, sparingly** (section 5.15). Then turn on `dress=True` if the map wants the generated dressing, rebuild,
   re-check and re-shoot.
9. **Full build and tests:** `cmake --build build -j`, then `build/quark_tests maps`, `build/quark_tests bounds` and
   finally the whole `build/quark_tests` (it must pass before any commit). After that comes the adversarial review
   (section 7.4).
10. **Write it up:**
    - add its section to [MAP_STRUCTURES.md](MAP_STRUCTURES.md);
    - add it to the pool line in CLAUDE.md "Maps";
    - update [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) if the pool changed;
    - add notes to `changelogs/next.md` and `changelogs/full/next.md`;
    - commit locally (never push until told);
    - hand it to q to playtest.

### 1.2 A hand-made map

Hand-made maps are q's. A Claude thread never hand-edits a shipped hand-made `.qmap`: those change only when q saves a
new version from the editor.

1. **Pick one mode and a name** (as above).
2. **Sketch the loop graph and the timings** (as above).
3. **Open the editor:** `quark --editor <name>`, or Quarkology's map editor (`map_editor/`, its **Open in game** runs
   the same command). Block out the layout with plain boxes, wedges and groups; no decoration yet.
4. **Place the mode's pieces:** spawns, objectives, `for` and `setting` (section 2.8). In Quarkology, the save warnings
   name the missing objectives.
5. **Run the fixers in this order:** Fix z-fighting, Fix map (merges the slabs the carve left), Fix gaps, Fix spawns.
   In game, use FIX MAP and FIX GAPS. Each fixer dry-runs and asks first.
6. **Save** (Ctrl+S). It writes `<user maps folder>/<name>.qmap` with the `# made with the quark map editor` header and
   a `handmade` line. The user maps folder is `%APPDATA%\quark\maps`, `~/Library/Application Support/quark/maps` or
   `~/.local/share/quark/maps` (`--data-dir` moves it).
7. **Play test:** TEST PLAY in the editor, or `quark --host <mode> --map <name>`. Run the checks with Quarkology's
   Tests tab, or `echo '{"file":"<path>"}' | build/quark_maptool check`.
8. **Decorate, sparingly**, then run the fixers again.
9. **To ship it:**
   - copy the file to `maps/<name>.qmap` in the repository;
   - run the tidy pass on it: `python3 tools/maps/tidy.py --qmap maps/<name>.qmap maps/<name>.qmap`;
   - add `!/maps/<name>.qmap` to `.gitignore`;
   - build and run `quark_tests`. Its misses print as `HUMAN-MADE` and don't fail the run, except the spawn sanity and
     cart sweep checks (section 9.2).
10. **Write it up** as in 1.1 step 10, and mark it hand-made (*) in the CLAUDE.md pool line.

---

## 2. The map file and the pipeline

Condensed from the code. The authorities are `src/game/map.h`, `src/game/map.cpp`, `tools/maps/prefabs.py` and
`tools/maps/tidy.py`. Re-read them when in doubt.

### 2.1 Units, axes, the hull

- **Metres.** Speeds in m/s. Every coordinate is within ±4096 m (`kMapExtent`, map.h:1424).
- **Y is up**; X and Z are the ground. A floor's top is its box's `y1`. A spawn's position is the **feet** (the bottom
  centre of the hull), so a spawn stands at y = the floor top.
- **Yaw** is in degrees. Facing is `(-sin yaw, 0, -cos yaw)`:

  | yaw | faces |
  |---|---|
  | 0 | -Z |
  | 90 | -X |
  | -90 (or 270) | +X |
  | 180 | +Z |

  Pitch is negative looking down.
- **A box is two opposite corners** (`x0 y0 z0 x1 y1 z1`). The loader sorts each axis; the tools write min then max.

| what | value | where |
|---|---|---|
| Hull | 0.8 x 0.8 m, 1.8 m tall | `kPlayerHalfWidth` 0.4, `kPlayerHeight`, gamedefs.h:42-43 |
| Eyes | 1.6 m | `kEyeHeight`, gamedefs.h:45 |
| Hitbox | two upright cylinders; the head is the top 20% (0.36 m) | gamedefs.h:472-473 |
| Crouching | **none** (removed from the game: pmove.cpp:321). Every gap a player must pass needs the full 1.8 m plus margin | |
| Step up (walking and in the air) | 0.55 m (0.56 m blocks) | `kStepHeight`, pmove.cpp:35 |
| Walkable slope | up to 45 degrees (face normal y ≥ 0.7071); steeper faces are walls, or surf ramps | `floorFace`, pmove.cpp:185 |
| Size modifiers | duel "double size" x2.0 (a giant is 3.6 m tall), tiny x0.6. 1.19.6 maps use 4.5 m doors | `kBigScale`, pmove.h:102; `DOOR_H`, tools/maps/qk1.py:21 |
| Payload cart | 2.4 m long, 1.8 m wide, 1.5 m tall: keep its sweep clear | `Map::payloadCartBox` |
| Ball | a 1 m cube | `kBallHalf` 0.5 |
| Fall damage | none | |

### 2.2 The `.qmap` file

- **Plain text, one item per line**: a keyword, then numbers. `#` starts a comment. Blank lines are ignored.
- **Version:** the first real line is `version 14` (`kMapFormatVersion`, map.h:1466). A file with no `version` is read
  as version 1 and converted.
- **Unknown keywords** are skipped with a warning, so an older game loads a newer map. A bad value on a *known*
  keyword is a parse error naming the line. Shipped maps must parse with **zero warnings**.
- **Required:** at least one box and one spawn.
- **The first line `# generated by tools/make_maps.py` marks a generated map.** Anything else, or a `handmade` line,
  is human-made.
- **Identity:** the sha256 of the text with CRs dropped (`mapContentId`). Ranked play, downloads and light caches go by
  it, so any edit makes a different map.

**Header and settings lines**

| line | meaning |
|---|---|
| `version N` | the format version (current 14) |
| `handmade` | made or changed by a person (the editor writes it) |
| `title <text>` | the display name, at most 40 printable ASCII characters, no `#` |
| `setting <words>` | where the map is (1-80 characters). Every generated map needs one |
| `for <mode>` | the one mode it's built for; menus pick the map by default for that mode. Required, and the map must support it |
| `modes <mode> ...` | the only modes it's offered in. Generated maps get it from `M.save` |
| `ranked <ladder> ...` | `vanilla`, `vampire` (1v1) and `instagib` (FFA). Counts only on an official (embedded) map |
| `sun x y z` | the direction toward the sun |
| `palette base r g b accent r g b metal r g b glow r g b sky r g b` | per-map colours, linear 0..1 |
| `void <y>` | falling below this kills. Default: 20 m below the lowest solid box |
| `indoor <y>` | a roof over everything: menu and shot cameras stay below it |
| `holdfire <s>` | nobody may fire for this long after a round goes live (0-30 s). Dojo 5, Blackhole and Stadium 3 |
| `racedefault <s>` | a race or surf map's default time on the ranked race total (1-3600 s) |
| `light x y z radius [intensity]` | a baked point light: radius 0.5-100, intensity above 0 and at most 10 (default 1) |

Mode words: `vs bomb race koth payload ffa npayload duel 1v1 tourney hide tag points surf power winner ball`. `winner`
is retired (1v1 is "winner stays" now).

**Geometry lines**

| line | what |
|---|---|
| `<material word> x0 y0 z0 x1 y1 z1` | a box. `box` is material 0; any of the 597 material words works |
| `teleporter <box> tp <link 0-255> <face 0-5> [walk]` | the same link pairs two teleporters. Faces: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z. `walk` caps the exit at run speed; without it players keep all their speed |
| `wedge <mat#> <dir+4*up> <box>` | the solid under a ramp rising toward dir (0 +x, 1 -x, 2 +z, 3 -z) |
| `corner <mat#> <dir+4*up> <box>` | a pyramid with its apex over one corner (0 (-x,-z), 1 (+x,-z), 2 (+x,+z), 3 (-x,+z)) |
| `tetra <mat#> <dir+4*up> <box>` | a three-sided pyramid on the base triangle at corner dir |
| `cut <mat#> <n> a b c d ... <box>` | the box less what lies past 1-6 planes `a*fx + b*fy + c*fz <= d` (fx, fy, fz are 0..1 fractions of the box). Any convex piece |
| `ladder <box> [auto]` | a climbable volume, at most 64 m tall. With `auto` the game draws its rails and rungs; without, separate boxes are the visible ladder |
| `group room\|stairs\|custom <mat#> <dir> <box>` (+ `part` lines) | editor objects whose boxes are made from the group's box. At most 64 m tall, 1024 parts |
| `stairs x0 z0 x1 z1 base top x\|z rising solid` | **a record only**: nothing is built from it. `M.stairs` writes it beside its steps so the tests can walk the flight |

In slope lines the material is a **number**. The `up` part (0-5) turns the frame: 0 on the floor, 1 hanging from the
ceiling, 2/3 on the side rising toward +x/-x, 4/5 toward +z/-z.

**Line types at a glance**

| kind | keywords |
|---|---|
| header | `version`, `handmade`, `title`, `setting`, `for`, `modes`, `ranked`, `sun`, `palette`, `void`, `indoor`, `holdfire`, `racedefault` |
| geometry | material words, `teleporter ... tp`, `wedge`, `corner`, `tetra`, `cut`, `ladder`, `group` / `part`, `stairs` (a record) |
| spawns | `spawn <team> x y z [yaw]` |
| objectives | `site`, `hill`, `point`, `checkpoint`, `path`, `cartcheck` |
| light | `light` |

Not in the format: `also`, `no_ffa` and `no_tidy` are builder attributes; `wallbang` and `nograpple` are materials;
there are no triggers or entities; Quarkology's Deco keeps its rules in a sidecar `<map>.mapedit.json`.

### 2.3 Materials (597, numbered 0-596)

| range | what | behaviour |
|---|---|---|
| 0 `box`, 1 `accent`, 3 `metal` | plain looks | coloured by the palette |
| 2 `wallbang` | dark grey, solid for movement | hitscan goes through at the weapon's wallbang damage (half) |
| 4 `glow` | emissive trim, solid | not lightmapped |
| 5 `far` | distant scenery outside the walls | **not solid**, flat-shaded, not counted in the bounds |
| 6 `water` | wade in it | not solid |
| 7 `lava` | a deadly pit | not solid; kills on touch; destroys projectiles |
| 8 `trim` | lit decoration | **not solid** |
| 9 `clip` | invisible | blocks **players only** (not shots, hooks or sight) |
| 10 `nograpple` | solid like base | the grapple can't hold on it |
| 11 `acid` | glowing green pool, not solid | 10% of max health at once, then 10% a second |
| 12-19 `ice sand wood plastic grass rubber steel diamondplate` | solid | ice: friction x0.08; rubber bounces 85% (Quark's only "jump pad") |
| 20-35 `brick glass asphalt mud ...` | solid | glass (21) is see-through and **never breaks**; asphalt runs x1.15; mud x0.6 |
| 36-38 `breakablebrick breakableglass breakablecobblestone` | breakable | brick and cobblestone blow open only at 1/8 m thick or less (`kBreakableThick`, map.h:723); breakable glass breaks to shots and blasts |
| 39-471 | brick, concrete, tile, stone, nature, buildings, industrial, sci-fi, wasteland looks | plain solid |
| 472-496 | 25 liquids | not solid; each changes movement, or hurts, heals or kills |
| 497-536 | large patterns | plain solid |
| 537-561 | see-through looks | each stops players, shots and/or sight as `kSemi` says |
| 562-585 | breakable blocks (`woodencrate`, `drywall`, ...) | break by hits, touch, dash or weight |
| 586-596 | `healingtile spikefloor lowgravityfield slowmofield gravityflip checkpointgate lightblock shieldwall crumbleplatform teleporter ballwall` | features. `shieldwall` stops shots only; the fields and the gate are passed through; `ballwall` is never drawn in play and only the ball collides with it |

`map.h` defines what each material stops: `solidMaterial`, `stopsPlayers`, `stopsShots`, `stopsSight` and
`wallbangBrush`. **Any shot-stopping brush 1/8 m thin or less on any axis is wallbang, whatever its material**
(`kWallbangThin = 0.126`, map.h:830). At most 512 holes exist at once per map.

### 2.4 Spawns and objectives

`spawn <team> x y z [yaw]`, with at most 256 spawns:

| team | used by |
|---|---|
| `a`, `b` | the two teams. Bomb assigns them by role: the attack always starts on `a`, the defence on `b` |
| `any` | free for all, hide, tag, a tourney's warm-up, and race when there are no `race` spawns |
| `race` | the race and surf start grid |
| `att0`..`att9`, `def0`..`def9` | multi point (and optionally payload). Stage N is used once N points or checkpoints have fallen |
| `<mode>-a`, `<mode>-b`, `<mode>-any` | a mode's own spawn set (e.g. Mine's `koth-a`). Not for race, points, winner or ball |

| objective | used by | rules |
|---|---|---|
| `site <box>` | bomb (1-16); ball (**exactly 2**: the goals, each belonging to the nearer team) | at most 16 |
| `hill <box>` | koth | **one per map** (a second is a parse error) |
| `point <box>` | multi point, **in attack order** | 2-16 |
| `checkpoint <box>` | race and surf; **the last is the finish** | at most 128. `checkpointgate` brushes count after them |
| `path x y z` | payload and npayload: the cart's waypoints | 2-512 |
| `cartcheck <f>` | payload checkpoints as fractions of the track, 0 < f < 1 | at most 64 |

### 2.5 Limits (`Map::validate`, on every load path)

| limit | value |
|---|---|
| brushes (including those made from ladders and groups) | 60,000 |
| spawns / lights / ladders / groups | 256 / 512 / 256 / 256 |
| a ladder's or a group's height | 64 m |
| sites / points / checkpoints / path points / cartchecks | 16 / 16 / 128 / 512 / 64 |
| stairs records | 512 |
| collision grid references | 16M |
| title / setting | 40 / 80 characters |

Quarkology's `validate.js` refuses a save that breaks one.

### 2.6 The generated pipeline

1. **CMake configure** runs `make_maps.py --list` / `--list-legacy` to learn the names (CMakeLists.txt ~278).
2. **CMake build** runs `PYTHONHASHSEED=0 python3 tools/make_maps.py --out <source>/maps`. It writes every map to
   `maps/<name>.qmap` (legacy maps to `maps/legacy/`) whenever `make_maps.py`, a `tools/maps/*.py` or an override
   changes.
3. **`tools/embed_maps.py`** writes `build/generated/official_maps.h` (names + sha256: ranked runs only on these) and
   `official_map_data.h` (the texts). It embeds the generated maps and the hand-made maps `.gitignore` un-ignores,
   never other files in `maps/`, never `zz_*` test maps.
4. **`copy_maps`** copies the shipped maps beside the executables.

The output is deterministic, byte for byte (seeded randomness, LF endings). **Maps are a build artifact:
`maps/*.qmap` is not in git.** An `assets/overrides/maps/<name>.qmap`, if one exists, replaces a generated map (none
today).

```
python3 tools/make_maps.py                     # every map (MAPS + LEGACY)
python3 tools/make_maps.py --map NAME          # one map (repeatable)
python3 tools/make_maps.py --list              # the MAPS names
python3 tools/make_maps.py --list-legacy       # the LEGACY names (in step with kLegacyMapNames, map.h)
python3 tools/make_maps.py --tidy-report FILE  # each map's tidy counts (JSON lines)
```

### 2.7 The builder API essentials (`tools/maps/prefabs.py`, class `M`)

Each module does `from .prefabs import *`, makes an `M`, places pieces and calls `m.save("<name>")` with its own name
(an assert checks it). `mirror=True` also places the **point-symmetric twin** `(x, z) -> (-x, -z)`.

| call | what |
|---|---|
| `M(title, mode, sun=...)` | starts the file: marker, `version`, `title`, `for` (mode `None`: no `for`), `sun` |
| `m.box(x0,y0,z0,x1,y1,z1, accent=False, mirror=False, mat=None)` | a box of any material word |
| `m.metal(...)`, `m.glow(...)`, `m.wallbang(...)` | boxes of those materials |
| `m.teleporter(..., link, face, speed="carry"\|"walk")` | a teleporter |
| `m.raw(line)` | any line as written (`hill`, `site`, `point`, `checkpoint`, `path`, `cartcheck`, `ranked`, `void`, `setting`, `holdfire`, ...) |
| `m.spawn(team, x, y, z, yaw, mirror_team=None)` | a spawn; `mirror_team="b"` adds the twin for b, facing yaw+180 |
| `m.light(x, y, z, radius=9, intensity=0.8, fixture=True)` | a baked light plus a small lamp box |
| `m.room(hx, hz, h=8, pits=(), sunken=(), style="hall", ...)` | the arena: floor (top y=0), 2 m walls to h, clip over them. Pits write `void -2.5` |
| `m.clip_walls(x0, z0, x1, z1, h, up=80)` | invisible clip on the outer wall tops, 80 m up |
| `m.stairs(x0, z0, x1, z1, top, axis, rising, base=0, solid=True)` | steps of at most 0.5 m, plus the `stairs` record |
| `m.building(...)`, `m.house(...)`, `m.doorwall(...)`, `m.windowwall(...)`, `m.catwalk(...)` | structures |
| `m.ladder(x, z, y0, y1, face, width=0.9)` (prefabs.py:442) | rails and rungs every 0.4 m flush on a wall (0.06-0.12 m off it), plus a volume 0.7 m deep reaching 1 m over the top |
| `m.pillar`, `m.crates`, `m.container`, `m.tree`, `m.track`, `m.signal_gantry`, `m.dais`, `m.ring_cover`, `m.heap` | pieces |
| `m.clutter`, `m.scatter`, `m.cribs` | seeded random cover, kept off spawns, tracks, stairs and keep-clear volumes |
| `m.keep_clear_box(..., why)` | a volume nothing solid may stand in: decor there is dropped, a hand-placed piece **stops the build** |
| `m.spawn_spots`, `m.top_up_spawns`, `m.fix_facing`, `m.face`, `m.scale_xz` | spawn helpers and scaling |

Attributes: `m.also = [...]` (more modes), `m.no_ffa = True`, `m.no_tidy = True`. Newer maps use kits that mirror
slopes correctly: `mkit.py`, `m2kit.py`, `bigkit.py` (`Kit`), `m5kit.py` and `m6kit.py`.

**`M.save(name, dress=True)` (prefabs.py:901), in order:**
1. Writes `modes`: the `for` mode, plus `also`, plus `tourney` when 1v1 is in it, plus `ffa` (not for ffa or surf
   maps, or with `no_ffa`).
2. Tops up every offered mode's spawn pool to 8.
3. Adds the payload cart's keep-clear sweep when payload or npayload is offered.
4. With `dress=True`, runs the dressing passes: `props`, `dress`, `detail_pass` and `skyline_pass`. The qk maps use
   `dress=False`.
5. Runs `clear_doorways`, then `check_clear`, then `fix_facing`.
6. Adds the palette.
7. Runs `finish()`: the tidy pass, then writes the file.

### 2.8 What each mode needs placed

"Teams" means at least one `a` and one `b` spawn, or the mode's own `<mode>-a/-b` set. A full default server is 8.

| mode | needs | spawn pool for a full server |
|---|---|---|
| vs (team deathmatch) | teams | 4 `a` + 4 `b` |
| duel | teams | 2 a side |
| 1v1 | teams | 1 a side (the qk maps have 2) |
| tourney | `any` spawns (warm-up) and teams | 8 `any` |
| ffa | spawns | 8 `any` |
| power | teams | 4 a side |
| bomb | teams + 1-16 `site`s | 4 a side; attack on `a`, defence on `b` |
| ball | teams + exactly 2 `site`s (goals); `ballwall` keeps the ball in | plain `a`/`b` |
| koth | teams + one `hill` | 4 a side (optional `koth-a/b`) |
| payload | teams + `path` (≥2), optional `cartcheck`s | 4 a side, or staged `att`/`def` |
| npayload | teams + `path` (≥2); the cart starts in the middle | 4 a side |
| race | ≥1 `checkpoint` or `checkpointgate` | 8 `race` behind the start line |
| surf | as race; run-only movement (no dash, no grapple) | 8 `race` (or `any`) |
| points | ≥2 `point`s in attack order | `att0` + `def0` at least |
| hide | ≥2 spawns | 8 `any` |
| tag | ≥2 spawns | 8 `any` |

### 2.9 The tidy pass (`tools/maps/tidy.py`)

The editor's fixers, run on every generated map as it's saved (`run`, tidy.py:672). It touches only box, material,
wedge, corner, tetra and cut lines, never markers, zones, ladders, lights, stairs, groups, or the shape of a slope or
cut box. It runs up to 8 rounds:

1. **Z-fighting:** two pieces that overlap AND share a face plane facing the same way. The smaller stays and is carved
   out of the bigger; of two identical pieces the later goes. Where carving would change what a shot passes through,
   the smaller is trimmed instead.
2. **Merge:** same-material plain boxes whose union is exactly one box become one box.
3. **Hidden:** a plain box covered on every face by solid opaque boxes (which also fill it, so no void is left) goes.
4. **Gaps:** facing faces less than 1/8 m apart close. The off-grid face moves to the 1/8 m grid line; if both are
   off-grid, both move to the line between them. It never raises a floor top or moves a wall into the open.
5. **Spawns:** a spawn inside solid rises 1/8 m at a time until clear. Still stuck after 64 m: **the build stops.**

Steps 1-3 never change solid space, shot-stopping or gameplay materials. So pairs with a slope or cut box, liquids,
lava, acid, ice, rubber, asphalt, mud, nograpple, wallbang, clip, ballwall, glass, breakables, see-through looks,
feature blocks, teleporters, same-material pairs and solid-with-non-solid pairs **are left as they are**.
`--tidy-report` names each pair left and why.

**Hand-made maps, file mode:** `python3 tools/maps/tidy.py --qmap IN.qmap OUT.qmap [--report FILE]`. The file must be
the current version. Unchanged lines stay byte for byte and custom group parts are never changed. **This is a manual
step: the build does not tidy hand-made maps.**

### 2.10 Official, ranked, hand-made, legacy, user maps

- **Official:** embedded by `embed_maps.py`. An edited copy is no longer official. **Ranked runs only on official maps
  with a `ranked` line.**
- **Hand-made shipped:** `.qmap` files in `maps/`, un-ignored by `!/maps/<name>.qmap` in `.gitignore`. They change
  only when q saves a new version from the editor.
- **Legacy:** the pool before 1.19.6 (`LEGACY` in make_maps.py, in step with `kLegacyMapNames`, map.h). Built
  untidied into `maps/legacy/`, never official or ranked, listed only with Settings > GAMEPLAY > LEGACY MAPS on; they
  always load by name. Never delete one.
- **User maps:** the user maps folder (2.2, 1.2 step 6) is searched before the shipped maps.
- **Hidden:** `tutorial` is hidden from every list (`mapHiddenFromLists`); it's still checked.

### 2.11 The converter rule

**Any change to the map format, now or later, raises `kMapFormatVersion` AND adds a converter step to `kMapUpgrades`
(map.cpp; `static_assert` at map.cpp:510 holds the count to version - 1) AND adds a `testMapConverter` case converting a
file of the old version AND raises `MAP_FORMAT` in prefabs.py:12.**

Old maps are converted on load. Players' own map files are converted on disk at startup (game and dedicated server),
with the original kept in `maps/old/v<N>/`. Demos work the same way (`src/game/demo.cpp`, `demos/old/<version>/`).

---

## 3. HARD RULES (never do)

### 3.1 The rules

These stand whatever q's ruling on bots, because players see or feel every one of them. "Catches it" says what finds
the problem. When it says *nothing*, you must check by eye.

| # | Rule: never ... | Why | What catches it |
|---|---|---|---|
| 1 | **Z-fighting:** let two pieces overlap and share a face plane facing the same way (both min or both max equal on an axis). Typical causes: a trim strip or coat sunk into a wall flush with its face, a floor patch overlapping the floor with the same top, a duplicate box, a wedge side flush with a wall it overlaps | coplanar faces of different looks flicker | Generated: tidy step 1. Hand-made: Quarkology **Fix z-fighting**. The renderer nudges overlapping coplanar faces apart (`lightmap.cpp`), but that is a safety net, not a licence. **Not fixed, so never make them:** pairs with a slope or cut box, glass or a breakable as the bigger piece, clip or ballwall, solid with non-solid (water set into a floor), and in the build any gameplay material |
| 2 | **Blocks inside or overlapping other blocks:** bury a piece wholly or partly inside another | wasted draw calls and collision cells; z-fights where planes meet | tidy steps 2-3, **Fix map**, in-game **FIX MAP** (`mapFixRedundant`). Not removed: a buried gameplay material (`hiddenGameplay`) or a piece whose removal leaves a sealed void (`hiddenVoid`), so don't make them |
| 3 | **Hidden pieces:** leave anything nobody can ever see (inside solids, behind outer walls, under floors, above sealed ceilings) | frame time: lows come first | tidy hidden step, Fix map, FIX MAP, but only for boxes fully covered by solid opaque boxes. Faces only visible from outside the map, and pieces behind glass/water/trim/clip covers, are **not** caught: look |
| 4 | **Needless pieces:** build from more pieces than needed (5 flush same-material boxes that could be 1) | load, bake and draw cost; outline seams in hand-made maps | tidy merge, Fix map, FIX MAP (glass and breakables never merge) |
| 5 | **Gaps and seams:** leave a gap under 1/8 m (0.125 m) between facing pieces, or put faces off the 1/8 m grid | no player fits, but shots slip through and light leaks | tidy step 4, **Fix gaps**, FIX GAPS: only where one face is **off** the grid. On-grid gaps are taken as meant. Gaps next to slopes, exposed faces, gameplay materials and wallbang are left |
| 6 | **Seams in hand-made maps:** expect flush plain boxes to read as one surface | outlines are drawn between any two pieces that aren't parts of the same group (`lightmap.cpp`, Quarkology `seams.js`) | Quarkology Seams view; fix with Fix map (merge) or by grouping |
| 7 | **Spawns in walls:** place a spawn whose 0.8 x 1.8 x 0.8 m hull, standing on its feet point, touches anything solid | the player spawns stuck | tidy step 5 (raises it; stuck after 64 m stops the build), Fix spawns, the save warning, `testAllMaps`, maptool `spawns_in_walls`. Put spawns exactly on a floor top |
| 8 | **Spawns facing a wall or the void,** or in an enemy's sight in team modes | what a player sees and does in the first second | maptool `spawn_facing` / `spawn_sight`, `M.fix_facing`, but judge as a player (section 5.10) |
| 9 | **Reachable out of bounds:** let a human reach any floor outside the playable footprint, with any tech (strafe-hop, dashes, super, hyper, the pop, 45 m grapple, **rocket jumps to 27 m, launches and sticky stacks to ~50 m, bubble launch to 73 m**) | players leave the map, camp unreachable spots, see the unfinished outside | `quark_tests bounds` / `mapbounds`/`mapbounds2`, maptool `out_of_bounds` (`OOB_TRACE=<map>` lists spots). **That simulation covers hops, dashes and the grapple, not rockets, stickies or bubbles: check rocket tech by hand.** `M.clip_walls` puts clip 80 m up on the outer walls |
| 10 | **1/8 m = wallbang:** make a piece that should stop bullets 1/8 m thick or less on any axis (floors, ceilings, panels, doors, roofs) | any shot-stopping brush that thin is wallbang whatever its material (`kWallbangThin`, map.h:830): hitscan goes through at half damage | **nothing** flags it. For shoot-through on purpose, use the `wallbang` material (dark grey) so players can see it. Thin trims and coats flush on a thicker wall are fine, because the wall behind still stops the shot |
| 11 | **Hidden ladder rungs (q, hard rule):** let any rung or rail of any ladder sit inside or behind a wall, floor, roof, catwalk, parapet, slab or any other piece | q: "ALL LADDER RUNGS MUST BE VISIBLE in every map, no matter what" | **nothing automated.** Check every ladder by eye and in screenshots, and check that no rung box intersects another solid piece. `M.ladder` rungs run from y0+0.35 every 0.4 m to y1 at 0.06-0.12 m off the wall. An `auto` ladder's rungs run along the **depth centre** of its volume from y0+0.35 to top-1 m (map.cpp:1428), so the volume's middle must be in open air. Floors crossed by a ladder need a hole the full ladder width. A rung missing from the built file was buried, and tidy removed or trimmed it |
| 12 | **Format change without a version:** change the format without raising `kMapFormatVersion`, a `kMapUpgrades` step, a `testMapConverter` case and `MAP_FORMAT` | old maps and demos would load wrong | the `static_assert` (map.cpp:510) catches the count; the test only exists if you write it |
| 13 | **Hand-edit or commit a generated `.qmap`**, or hand-edit q's shipped hand-made maps | the build overwrites generated maps; q's maps are his | `.gitignore` (`maps/*.qmap`); review. Edit `tools/maps/<name>.py` and rebuild. A hand-made map ships by `!/maps/<name>.qmap` |
| 14 | **Break the ranked rules:** ranked only on official maps with a `ranked` line. qk1-qk8 are competitive 1v1 only: never retarget them; slopes are allowed; materials are box and accent; no healing tiles. Each ladder's default map stays ranked for it (greenhouse vanilla, dojo vampire, spire instagib). Both sides fair: equal spawns, and within 1 s of each other to the enemy and to the middle. Dojo keeps its empty mat and `holdfire 5`, and the `holdfire` start gives both players the same options | ranked results must be fair and comparable | `testMaps` (≥2 maps a ladder, `rankedDefaultMap`, `rankedFairness`), maptool `ranked_mirror`; then a stopwatch and q's judgment (fair, not necessarily symmetric) |
| 15 | **Concentrate heavy materials or effects in one view:** big lava or liquid surfaces, many lights, stacked see-through looks, dense `far` scenery or glow in one sightline | frame-time lows come first: Cobblestone's lava tunnel ran ~120 fps against ~1770 on the street | **nothing automated.** F3 (avg / 1% / 0.1% lows) while playing, `--screenshot` runs, `tools/release_check.sh` (hosts every map). Spread them out or cut them |
| 16 | **Make a map so dark that players vanish** (q) | dark player models must stay visible | nothing automated: look at spawn and floor shots with dark models |
| 17 | **Rely on non-solid materials to block:** `trim` (8) and `far` (5) block nothing; water, lava, acid and liquids hold nobody up; `clip` stops players only and must never be a visible wall. Every pit needs a `void` | players fall through, shoot through or get stuck on invisible walls | nothing: know the table in 2.3 |
| 18 | **Put a solid piece inside a keep-clear volume** (the payload cart's sweep, declared doorways) | the cart jams; doorways block | `M.save` stops the build (`placed in a keep-clear volume`); `testCartSweep` |
| 19 | **Break the format's limits or strict keywords:** a second `hill`, an unknown spawn team, a bad number on a known keyword, an unknown group type, anything past the limits in 2.5 | the map doesn't load | the parser (error names the line), `Map::validate`, Quarkology's save checks |
| 20 | **Name a map after a game mode,** or ship one without one supported `for <mode>` and a `setting` | owner rules; menus and tests rely on them | `testMapFormat`, `testAllMaps` (`preferred_mode`) |

### 3.2 Automated checks that are NOT design rules (q, 2026-11-10)

The checks below are not design rules. When you judge a map or propose a change, judge how a real human plays it, not
whether these pass. Never bend a design to them, and don't add stairs just for bots: grapple-only routes are fine.

| check | test / maptool id |
|---|---|
| Bot navigation from every spawn to every objective | `testAllMaps` nav graph part, `nav_routes` |
| Every ladder climbable by the auto-climb | `testLadders`, `ladders` |
| Every staircase walkable, jumpable, not cutting a path | `testStairs`, `stairs_walkable` / `stairs_jump` / `stairs_cut` |
| Every route walkable without movement tech | `testAllMaps` walk routes, `walk_routes` |

Notes:
- **They still run.** `quark_tests` must pass before a commit (CLAUDE.md), and a generated map that fails one fails the
  run. If a check fights a design a human would play well, bring it to q (section 9) rather than quietly add bot
  stairs.
- **Some neighbouring checks describe real player problems.** Spawns facing a wall, enemy spawns in sight, a too-small
  spawn pool, a jammed cart track, out of bounds and ranked fairness are things a human also runs into. Judge those as
  a player would, and treat the check as a pointer, not the verdict.

---

## 4. Movement numbers for map makers

**These are measured, not guessed.** Each figure comes from the game's real movement code (`pmove()`) and the real
server (weapons, blasts, knockback), driven by inputs a person can make, at the server's 1000 Hz tick. No bot movement
was used. The "human" figures were calibrated on q's own race runs. Movement is still marked placeholder tuning
(CLAUDE.md), so re-measure when `pmove.cpp` or the weapon table changes.

### 4.1 Constants that shape geometry (src/game/pmove.cpp unless named)

| what | value | where |
|---|---|---|
| Gravity | 24 m/s² | :9 |
| Jump take-off | 7.67 m/s (1.22 m standing jump, 0.63 s in the air) | :10 |
| Run speed | 9.5 m/s (full speed in ~0.08 s; no diagonal bonus) | :11 |
| Step up | 0.55 m, walking and in the air | :35 |
| Floor vs wall | ≤ 45 degrees floor, steeper wall/surf | :185 |
| Bhop bounce | landing faster than 7.67 m/s with jump held: +50% of the extra, at most +8 m/s | :108-109 |
| Jump buffer | a jump tapped up to ~0.09 s before landing still hops | :84 |
| Dash | 24 m/s for 0.12 s (~2.9 m), no gravity during it; exit at least 11 m/s, never over 32 | :36-45 |
| Dash vertical | 50% of it up looking straight up, 90% down looking down | :75, :78 |
| Dash charges | 2; 0.6 s cooldown after a dash; 1.2 s recharge. **A floor refills all charges, a bhop landing one** | pmove.h:18-21 |
| Dash wall bounce | a dash within 45 degrees of head-on bounces off at 16 m/s (2 per dash) | :104-107 |
| Grapple | range 45 m from the eyes, pull 38 m/s² to 24 m/s, half gravity while hooked, lets go 1.2 m from the anchor, 1 s cooldown. Won't hold on `nograpple` or `clip` | pmove.h:111, :124 |
| Ladder | 6.75 m/s up/down, 4.5 m/s sideways | :116-117 |
| Rocket | 48 m/s launch, +16 m/s² thrust, 3.6 m splash, 1.2 s refire, 5 s life. Knockback 22 m/s at the centre; your own x1.3; enemies x2 vertical (up to 44 m/s up). Self damage 10% (~3 hp) for a blast at your feet | gamedefs.h:408, :507; server.cpp:3724-3734 |
| Floors | ice friction x0.08; rubber bounces 85%; asphalt run x1.15 (10.9 m/s); mud run x0.6, jump x0.75 | :61-73 |
| Fall damage, jump pads | none exist | |

### 4.2 Basic moves

| move | measured |
|---|---|
| Run from standing | 9.5 m/s after 83 ms; 9.15 m in the first second |
| Stop from 9.5 | 0.365 s, 1.2 m |
| Standing jump | apex 1.22 m (feet), head 3.02 m |
| Running jump (9.5) | 6.0 m takeoff to landing |
| Prestrafe (turning on the floor while running) | 13.3 m/s after 0.1 s, 14.3 m/s max |
| Hop after a fall (jump held) | 2 m drop: 1.6 m hop; 4 m: 2.4 m; 8 m: 3.9 m; ≥ 11.7 m: 5.1 m (the cap) |
| Stairs | any riser up to 0.55 m, any tread, at full speed |
| Running off a ledge at 9.5 (no jump) | carries 2.7 m out over a 1 m drop, 4.8 m over 3 m, 6.7 m over 6 m, 8.7 m over 10 m |

### 4.3 Heights: the highest ledge each tech reaches

A ledge reached while moving toward it gets up to 0.55 m extra from the air step-up.

| tech | ledge | who |
|---|---|---|
| Walk | 0.55 m | everyone |
| Plain jump (standing or running) | **1.75 m** (1.22 apex + 0.55 step) | everyone |
| Hyper from a run (straight down) | 4.05 m | good |
| Running jump + 2 dashes 60 degrees up | 5.1 m | good |
| Hyper from a run + a dash up | 6.7 m | good |
| Ramp dash up a 50 / 60 degree slope | 8.3 / 10.5 m up its face | good |
| Super looking up + 2 dashes up (needs ~36 m of run-out ahead) | 11.25 m | good to top |
| The pop / the pop + a dash | 14.5 / 15.25 m | good to top |
| Rocket, fire straight down / jump + fire / best timing | **17 / 24.3 / 27.3 m** apex | anyone with rockets |
| Running jump + fire straight down | 23.5 m apex, lands 42 m on | |
| Sticky jump / sticky stack (3) | 10.0 / 53.2 m | |
| Rocket pop, the launch (grapple walk + jump + dash + rocket) | ~50 m | top |
| Bubble launch | 72.8 m | |
| Grapple | anything within 45 m of the eyes (a 10 m wall in 2.3 s, 40 m in 3.9 s) | everyone |
| Ladder | any height (10 m in 1.5 s) | everyone |

A dash **replaces** your velocity: dashing during a rocket jump throws away its height (24.3 m drops to 12.7 m).

**What it means for ledges:**

| height | who gets up |
|---|---|
| ≤ 0.55 m | walk |
| ≤ 1.75 m | anyone, with a jump |
| 1.8-4 m | a hyper or dashes (good players), a hop after a drop, a ladder, the grapple |
| 4-11 m | dashes at speed, ramp dash, the pop (good to top); grapple; rockets |
| 11-27 m | rockets or the grapple |
| over 45 m | only launches, stacks, bubbles, or ladders |

Avoid ledges of about 1.8-2.2 m on routes meant for everyone: they look jumpable and aren't. Make them ≤ 1.7 m or
clearly taller (2.5 m+).

### 4.4 Speed over time: perfect, good and q-calibrated players

Strafe-hopping is uncapped (intended). Holding jump hops every landing with no friction, so **speed grows with time in
the air, not with the number of hops**. "X%" means a player who takes X% of the best possible gain every moment.

**Speed (m/s) after the first jump** (0.4 s run-up first):

| seconds | 1 | 2 | 3 | 5 | 10 | 15 | 20 |
|---|---|---|---|---|---|---|---|
| Perfect A/D (100%) | 21.1 | 29.4 | 36.4 | 48.2 | 71.8 | 91.0 | 107.9 |
| Very good (90%) | 20.1 | 27.9 | 34.4 | 45.5 | 67.5 | 85.6 | 101.4 |
| **Good (75%)** | 18.6 | 25.5 | 31.2 | 41.1 | 60.8 | 76.9 | 91.0 |
| Decent (50%) | 16.0 | 21.1 | 25.5 | 33.0 | 48.2 | 60.8 | 71.8 |
| W only, perfect | 17.9 | 23.5 | 27.9 | 35.2 | 48.9 | 59.5 | 68.5 |

**Run-up: time and straight lane needed from standing to reach a speed:**

| speed | 15 m/s | 20 | 25 | 30 | 35 | 45 | 60 |
|---|---|---|---|---|---|---|---|
| 100% | 0.82 s, 9 m | 1.29 s, 17 m | 1.85 s, 29 m | 2.48 s, 47 m | 3.19 s, 69 m | 4.82 s, 134 m | 7.7 s, 286 m |
| 75% | 0.95 s, 10 m | 1.58 s, 21 m | 2.33 s, 38 m | 3.17 s, 61 m | 4.12 s, 91 m | 6.29 s, 177 m | 10.2 s, 380 m |
| 50% | 1.23 s, 14 m | 2.18 s, 30 m | 3.29 s, 55 m | 4.56 s, 90 m | 5.98 s, 135 m | 9.2 s, 264 m | - |

Rule of thumb: **about 30 m of open run-up for 20-25 m/s, 60-90 m for 30-35 m/s** for a good player.

**q's own race runs (the real-human calibration).** 19 runs, 2026-10-26 to 11-03, against the maps' checkpoint boxes.
Distances are straight lines between checkpoints, so the speeds are lower bounds; the maps have changed since, so treat
them as approximate.

| map | best total | course | average | fast open legs | slow legs |
|---|---|---|---|---|---|
| accelerator | 66.4 s | 1511 m | 22.8 m/s | 30.9-38.8 m/s | 11.1 (a 110 m climb), 4.3 |
| jungle | 54.1 s | 1045 m | 19.3 m/s | 30.4-38.7 (best leg of any run 52.0) | 4.8, 8.4 |
| rooftoprun | 49.8 s | 1022 m | 20.5 m/s | 29.8-35.4 | 5.7-10.9 (climbs) |

Against the simulation, q's opening legs match the 50-75% strafer. So **q, a good player, strafes at 50-75% of
perfect, reaches 30-39 m/s a few seconds into an open stretch, and averages about 20-23 m/s over a whole course.**

**Who moves how fast:**

| player | typical travel | peaks |
|---|---|---|
| Casual | 9.5 m/s running, 10-14 with some hops and dashes | 24 with a super |
| Good (q) | 20-30 m/s within 2-4 s of open ground | 35-40 on long straights |
| Top | 30-45 m/s on open stretches | 50+ after 5-10 s; rocket super 33, hook boost 40, launches 49 up, surf 60 |

In fights people slow down to aim; these are travel figures.

### 4.5 Gaps (same height both sides)

| tech | at the very edge | jumping 0.5 m early |
|---|---|---|
| Running jump (9.5) | **7.45 m** | 6.95 m |
| Hopping at 12 / 15 m/s | 9.2 / 11.35 m | 8.7 / 10.85 m |
| Hopping at 20 / 25 / 30 / 35 m/s | 14.85 / 18.35 / 21.9 / 25.4 m | about 0.5 m less |
| Super (24 m/s) | 18.35 m | |
| Super + a second dash in the air | 24.9 m | |
| Running jump + 1 air dash, level / 20 / 40 / 60 degrees up | 14.3 / 17.6 / 20.4 / 22.1 m | |
| Running jump + 2 air dashes 40 / 60 degrees up | 37.8 / 41.2 m | |
| Super then dashes held, 20 / 30 / 40 / 60 degrees up | 33.6 / 59.0 / 63.9 / 68.3 m | |

**What it means:**

| gap | who crosses |
|---|---|
| ≤ 6.5 m | anyone (7.45 m at the very edge) |
| 7-14 m | hops at speed, or one air dash |
| 8-11 m | a gap that stops runners but not bhoppers |
| 14-25 m | super, fast hops, super + dash |
| 25-68 m | supers and dashes aimed up |
| beyond | rockets, the grapple (45 m) |

### 4.6 Turning at speed, lane and corner widths

The tightest arc a perfect air-strafer holds without losing speed:

| speed | turn rate | radius | a 90 degree turn takes |
|---|---|---|---|
| 9.5 m/s | 432 deg/s | 1.3 m | 0.21 s, 2.0 m of arc |
| 15 | 295 | 2.9 m | 0.30 s, 4.6 m |
| 20 | 251 | 4.6 m | 0.36 s, 7.2 m |
| 25 | 217 | 6.6 m | 0.42 s, 10.4 m |
| 30 | 193 | 8.9 m | 0.47 s, 14.0 m |
| 35 | 174 | 11.5 m | 0.52 s, 18.1 m |

**Plan for about twice these radii for humans:** ~6 m at 15 m/s, ~13 m at 25, ~23 m at 35. A 90 degree corridor
corner taken at speed needs a corridor width of about **2.5 m at 15 m/s, 4.7 m at 25 m/s and 7.5 m at 35 m/s**.
Narrow bends (under 2.5 m) drop everyone to about 15 m/s.

**Straight lanes don't need width.** A player who flips the strafe key at 15 degrees off the lane sways only 2.3-3.0 m,
so **4 m is enough to bunny-hop straight down at full gain**. Players who flip every hop sweep 9-25 m, so a 10 m+ lane
lets lazy strafers keep up.

### 4.7 Time to cross open flat ground from standing

| tech | 50 m | 100 m | 200 m |
|---|---|---|---|
| Run | 5.29 s | 10.54 s | 21.04 s |
| Run with dash held | 4.15 | 8.31 | 16.64 |
| Hyper chain | 2.93 | 4.59 | 7.44 |
| Strafe-hop perfect / 75% / 50% | 2.58 / 2.78 / 3.06 | 3.99 / 4.34 / 4.87 | 6.15 / 6.74 / 7.66 |
| Super, then strafe-hop 75% | 1.75 | 3.11 | 5.34 |
| Grapple walk to 24, then strafe-hop perfect | 2.03 | 3.32 | 5.38 |

A room is crossed in 1 s by a runner if it's 9.5 m across, by a good player if it's 25 m, by a top player if it's
40 m.

**The bomb timing rule checked:** 120 m from a standing spawn takes a 75% strafe-hopper about 4.9 s and a 50% one about
5.5 s; a runner 12.7 s; a super-start strafe-hopper about 3.5 s. So q's "5 s at 24 m/s" matches a good player hopping
from spawn without dashes.

### 4.8 What kills momentum (and what doesn't)

**Snags:** hopping at 20 m/s into a lip across the path (64 hop timings tried):

| lip height | speed kept | hops that lost everything |
|---|---|---|
| 0.05-0.55 m | 100% | 0 of 64 |
| 0.60 m | 98% | 1 |
| 0.80 m | 89% | 7 |
| 1.00 m | 80% | 13 |
| 1.50 m | 49% | 33 |
| 1.80 m | 0 | 64 |

| kills momentum | doesn't |
|---|---|
| Anything 0.6-1.8 m tall standing in a fast path: crates, railings, kerbs over 0.55 m, pillars, low walls. A hop meeting it low stops dead on that axis (pmove.cpp:828-829) | lips, kerbs, steps and trim **under 0.55 m** (stepped over at any speed) |
| Head-on walls and pillars; a glancing hit kills only the part of the speed into it | floor slopes up to 45 degrees, up or down (neither slow nor speed you) |
| Dash-bounce walls at the end of dash lanes (thrown back at 16 m/s) | **low ceilings**: hopping under 2.0-3.0 m gives the same speed as open sky (only hop height is cut). Ceilings at 3.1 m+ are never touched by hops |
| Ice and mud floors; > 45 degree faces you can only slide on | stairs with risers ≤ 0.55 m |
| Sharp turns in narrow corridors (4.6) | |
| Long airborne routes with no floor: dash charges run out (a floor refills them all) | |

### 4.9 Ceilings, landing zones, cover spacing

- **Ceilings:**
  - 3.1 m or more never touches a hop.
  - A hyper needs 5.3 m clear, the pop 16 m.
  - Rocket jumps need 19 m (no jump) to 26-29 m (jump + rocket).
  - Sticky jump 12 m; launches, rocket pops and sticky stacks 52-55 m; bubble launch 75 m.
  - Any ceiling within 45 m is a grapple perch: players hang 1.8 m under it (q: intended).
  - **To stop a room being skipped by rocket jumps, keep its ceiling under ~19 m or make it `nograpple`.**
- **Landing zones** (there are no jump pads):
  - a super lands 15 m on;
  - a hyper from 24 m/s 27-34 m on;
  - the pop 27-44 m;
  - a rocket jump while hopping at 15 / 25 m/s 65 / 95 m on;
  - a rocket super 85 m;
  - a launch or rocket pop 80-90 m.
  Leave that much open floor, or a deliberate wall, where these start.
- **Run-ups:**
  - a running jump needs nothing;
  - a super needs ~1 m of floor;
  - a grapple walk needs a floor point ~18 m ahead in view;
  - bhop speed needs the run-up table in 4.4.
- **Cover spacing:** time in the open = gap / speed. For 1 s of exposure, cover goes ~9.5 m apart for runners and
  20-25 m apart on bhop routes. **Cover closer than ~8 m apart on a fast lane is decoration that snags hoppers.**
- **Surf** (run-only): speed comes from the height you lose. The bottom of a 55 m descent gives 59-64 m/s.
- **Grapple:** never slows a player faster than 24 m/s, only bends their path. A slingshot (hook 20 m ahead and 20 m
  up, let go at 24) flies 7.2 m high and lands 33.6 m on. A low hook (40 m ahead, 15 m up) only drags you along the
  floor.

---

## 5. Design principles for Quark

Each principle gives what the sources say (short; the sources are in section 10) and **the Quark rule of thumb**. The
numbers are starting points to test with real players, not laws. The two things that make Quark different from
classic quakelikes are its movement (section 4: 20-40 m/s travel, 17-27 m rocket jumps, a 45 m grapple, no fall
damage) and **no pickups** (no health, armour, ammo or power-up items: players spawn with their mode's loadout).

### 5.1 Flow and loops

- **Sources:** flow is the most important thing (Burkart). "A map needs to have a circular flow on the macro level"
  (Howard, quoting McDonald). Dust2 is a figure of eight (Johnston); Blood Run's upper levels tie its branches into a
  figure 8. Never force a 180 degree turn to find the way on (Pearson).
- **Quark rule:**
  - Draw the room-and-link graph first. It has at least one big loop through every major area, and every major area
    sits on at least two loops.
  - Size the main loop by time: **10-20 s at run speed** (about 95-190 m) in duel and FFA maps, which is 5-8 s for a
    good hopper. Add one or two cut-throughs so a chased player can be cut off.
  - Exits face the way players arrive, so momentum carries through. Put sharp turns where you *want* players slow
    (objectives, chokes), never on the main loop.

### 5.2 Connectivity: the three-exit rule, dead ends

- **Sources:** three entrances to a defensible area (Pearson; Red Eclipse). Lost World's two-door YA room is a known
  trap (Howard). A dead end is allowed only as a focus point (Unreal Wiki). 1 to 3 lanes; more reads as a maze (The
  Level Design Book).
- **Quark rule:**
  - Every room you fight in has **at least 3 walk exits, not all on one side**. A perch has at least 2 ways off (one
    can be a drop: there's no fall damage).
  - Count *walk exits* and *tech exits* (grapple anchor, rocket ledge, dash gap) separately. Tech exits are extra.
  - A dead end only holds something worth the risk and is quick to leave, counting only the exits every player has in
    that mode (surf has no dash or grapple).

### 5.3 No pickups: what players fight over instead

- **Sources:** classic duel is a fight over the item cycle (Howard, cityy). In games without pickups, the "items" are
  positions: high ground, strong spots with drawbacks, and objectives (The Level Design Book, Balance).
- **Quark rule:** the things players fight over on a Quark map are:
  1. **Objectives** (hill, sites, points, cart, ball, goals). Place them the way Howard places RA and MH: not in sight
     of each other, linked by a route with a choke, each approachable from 3 sides.
  2. **High ground and perches.** They are Quark's "armour". Each needs a cost: an exposed approach, no cover from
     above, or a long way to anywhere useful.
  3. **Floors that refill dashes.** A long airborne route empties charges, so a floor in the middle of it is a
     "pickup". Put such floors where you want fights.
  4. **Sightline control:** rooms that see two entrances at once are worth holding.
  5. **Surfaces as map items:** asphalt strips (+15% run), rubber (a bounce), ice, teleporters. Use few, give each a
     reason, and make them unmistakable.
  6. **Cover to break line of sight** where regeneration (10 hp/s after 3 s untouched) or vampire healing applies: a
     place to reset is an item.

  Item timing becomes **spawn and round timing**: in round modes, who reaches the strong position first from the fixed
  spawns decides the start, so measure it in seconds both ways.

### 5.4 Verticality versus huge rocket jumps

- **Sources:** 2 to 4 levels. The highest areas are furthest from the centre and have the smallest footprint (Howard).
  Three floor planes in one area at most (The Level Design Book). Overwatch assumed "everyone is going to be able to
  get literally everywhere", so the issue became containment (Goodman).
- **Quark rule:**
  - **2 to 3 floor planes per area.**
  - Height is cheap in Quark (a rocket jump is 14-22 standing jumps, nearly free at ~3 hp). So defend a perch by
    **nothing to hide behind up there and a long trip to anywhere useful**, not by being hard to reach. Keep it small,
    away from the centre, and exposed to at least two lower positions.
  - Space the floors so the move up is a choice (heights from 4.3):

    | rise | how | use |
    |---|---|---|
    | ≤ 1.7 m | a jump | |
    | 2.5-4 m | stairs, ramp, ladder, hyper, drop-then-hop, grapple | |
    | 4-8 m | grapple, rockets, dash combinations, a real staircase | a good Quark "storey" |
    | over ~17-27 m | out of single-rocket reach | boundaries and landmarks |

  - In modes with rockets and the grapple, assume **any surface under ~27 m can be stood on**. For each roof decide:
    either it is play space (then give it a purpose and a counter), or it isn't (then make it unstandable: sloped over
    45 degrees, `clip`, or out of bounds by design).
  - There's no fall damage, so drops are free one-way routes down. A drop into a hop is a skill line (4.2).

### 5.5 Ceilings and boundaries

- **Sources:** low ceilings let defenders delay pushes (Chill Out study). Red Eclipse: rooms 8-12 m, atria 12-16 m+.
  Overwatch's main risk with mobility was flying out of the map (Goodman). Hydro's low fences told players "out of
  bounds" without walls (Dubuk).
- **Quark rule:** a ceiling is a decision about rocket jumps and the grapple.

  | ceiling | what it does | where |
  |---|---|---|
  | Low, 3.1-4.5 m | no rocket jumps, strong splash, short grapple pulls | corridors, close rooms |
  | Medium, 6-12 m | rocket jumps to upper floors, grapple swings | most fighting rooms |
  | High or open, over ~19-29 m | free vertical play, sniping | needs a containment plan |

  - Where the sky is open, the edge must be **solid or deadly by design, with a visible boundary**. A clear signal
    beats an invisible wall.
  - Boundary walls must be very tall, roofed, or backed by a kill volume. Remember rocket jumps of 17-27 m, launches of
    ~50 m and the bubble launch of 73 m. `M.clip_walls` clips to 80 m above the wall top.
  - Check rocket escapes by hand, because the out-of-bounds test doesn't simulate rockets (rule 9).

### 5.6 Map size in seconds

- **Sources:** Halo 4 tracks corner-to-corner time and grew maps with sprint (Holloway). "If it feels long, it's too
  long" (Splash Damage). UT2004 duel maps often felt too large (Unreal Wiki).
- **Quark rule:** size maps in seconds at run speed (9.5 m/s), then check at a good player's speed (section 4.4).

  | mode | target |
  |---|---|
  | 1v1 rounds | each spawn to the middle in 4-6 s (40-55 m), both sides within 1 s; the whole footprint crossable in 10-15 s |
  | FFA, 8 players | any spawn to a likely fight in 3-5 s (30-50 m) |
  | Team vs, power (4v4) | first contact 6-10 s after the round starts |
  | Objective modes | section 5.17 |

  - **Corridors:** none longer than about 30-40 m (3-4 s at run speed) without a turn, a height change or a side exit.
  - **Widths:** nothing a player is meant to use under 2.4 m wide, except a deliberate squeeze. Main routes 4 m or
    more. A corner taken at speed needs 4.7 m at 25 m/s and 7.5 m at 35 m/s (4.6).
  - **Doorways:** at least 2.2 m high and 1.6 m wide for a normal player (no crouching: 1.8 m plus margin). On maps
    that offer duel, size modifiers can make a 3.6 m giant, so use 4.5 m doors (as the 1.19.6 maps do) and 2.4 m+
    widths.

### 5.7 Sightlines and cover

- **Sources:** err on too little cover; rounded corners keep sightlines in chases; avoid "cover boxes", use
  architecture (The Level Design Book). One sniper must not shut down most approaches (TF2; Johnston tuned Dust2's
  crates for one AWP). Cover at least 3 m tall in Red Eclipse; obstructions are no substitute for layout (no-lex).
- **Quark rule:**
  - **Ranges from Quark's weapons:**

    | range | distance | weapons |
    |---|---|---|
    | close | under 10 m | knife 3.5 m, shotgun spread, laser up to 24 m |
    | medium | 10-30 m | |
    | long | over 30 m | rifle hitscan to 300 m, sniper to 500 m, instagib |

    Every map has all three.
  - **No single spot sees more than two of a room's entrances down a long line.**
  - **Instagib maps** (ranked FFA, Spire): keep most lines under 30-40 m, and break long ones with height changes
    rather than boxes.
  - **Cover is architecture:** pillars, low walls, slopes, floor-height steps. It's at least 2 m tall to hide a 1.8 m
    player, with a clean run past it.
  - Rockets cross a 50 m room in about a second. Open rooms favour hitscan; corridors and low ceilings favour splash.
    Use both, on purpose.
  - `wallbang` cover is a bet (shooting through costs position). Make it look like wallbang (dark grey).

### 5.8 Choke points

- **Sources:** 2 or 3 main paths, one choke per path; at most two entrances to an objective within a choke (WoLD).
  Impossible to cover all chokes from one point (The Level Design Book). Payload chokes only just before or on a
  checkpoint (VDC). Rialto's bridges were widened so a team can't be knocked into the water at once (Keller).
- **Quark rule:**
  - 2-3 lanes into each objective, one choke per lane.
  - A choke must also be a choke for movement tech. A grapple anchor or rocket line that skips it is a fourth lane:
    either want it (a skill flank that costs time and noise) or deny it (`nograpple`, a low ceiling, an
    out-of-play roof).
  - Doorways under ~3 m wide are true chokes: a rocket's 3.6 m blast covers them.
  - **Ring-outs:** enemy knockback is strong (up to 44 m/s up). A choke beside a void or acid is a ring-out trap. Make
    bridges and ledges over a void wide enough that one rocket doesn't decide it (see the open question in 9.1).

### 5.9 Landmarks and readability

- **Sources:** players always know where they're going (CliffyB); test with someone who has never seen the map
  (Epic). Halo 4 used landmarks, not red/blue decals (Holloway). Landmarks are tall and visible from wide sightlines
  (The Level Design Book).
- **Quark rule:**
  - Every major area gets **one big, unique, readable shape**, visible from the routes into it. Name areas so players
    can call them ("the bridge", "the pit").
  - At 20-30 m/s players read the map in glances, so **the route ahead and the next exit must be clear from 20-30 m
    away**. Put the landmark at the end of the line you want players to run.
  - Objectives read as objectives from every approach: lit, coloured, a unique shape.

### 5.10 Colour, lighting and visible players

- **Sources:** Valve's read order is team colour, then silhouette, then weapon, with "muted colors dominating and small
  areas of saturation" (Mitchell, Francke, Eng). Under-decorate (Burkart). Lights near-white, slightly tinted (VDC).
  No fog that blocks the view, no flashing lights (q3df). Levelord's top beginner mistake: "Framerate!".
- **Quark rule:**
  - Play surfaces in mid values and low saturation, so players and team colours are the most saturated,
    highest-contrast things on screen. No large surface in a team colour, except to mark a side.
  - **Dark maps against visible players (q):** light every route and exit. A dark corner must never hide a player by
    accident, and a dark player model must stay visible everywhere. Darkness is a deliberate, local choice (hide and
    seek hiding spots), never a map's default. Check spawn and floor shots with dark models.
  - **Movement surfaces look distinct and the same everywhere:** ice, rubber, asphalt, acid, `nograpple`, `wallbang`
    and surf ramps. If a player can't tell ice from floor before landing, it's a trap.
  - Readable maps with less clutter are also faster maps (rule 15).

### 5.11 Spawns

- **Sources:** 2-3 times as many spawns as players, not visually obvious (Burkart). Facing sets early movement
  (Holloway). Blood Run's clustered RA-room spawns cause spawn frags (QuakeWorld wiki). TF2 spawn rooms never have a
  single exit (VDC).
- **Quark rule.** The server picks the free spawn farthest from other living players (from enemies in team modes) and
  strongly avoids any spawn someone can see. So the map's job is to offer enough spawns that one is always unseen.
  - **FFA:** at least 2x the player count (16 for 8), spread over every area. Each has a wall or pillar between it and
    the nearest long sightline. Never two spawns in one small room.
  - **Team modes:** a cluster per side with 2-3 exits that no enemy can watch from a safe spot. Spawn exits never face
    down a long lane to the enemy (use a screen, as the generator does).
  - **Round modes** (1v1, vs, bomb, duel, tourney): fixed and fair, with equal times to the middle and to each
    objective (within 1 s). Each spawn faces into the map with a clear first few metres.
  - **Every spawn faces somewhere useful** (a route or the objective), never a wall or the void.

### 5.12 Routes for movement tech

- **Sources:** "What tricks can I put in that someone else can master?" (Jaquays, via Craddock). "Trick jumps are best
  when they're not obligatory" (The Level Design Book). Keep the shortcuts players find (q3df).
- **Quark rule:**
  - **One long strafe line on the main loop:** a straight or gently curving run of 40 m+ (30 m reaches 20-25 m/s,
    60-90 m reaches 30-35 m/s for a good player). It has a floor to hop on, no ledges to snag, a slight downhill if
    possible, and it ends where speed pays (a gap, an objective, a jump up).
  - **At least one tech shortcut per area** (a rocket ledge, a grapple anchor over a gap, a hyper up 2.5-4 m, a
    drop-then-hop) that saves time but is noisy or exposed. **Never the only way to anything important**, except in
    race.
  - **Drop-then-hop:** a 4-8 m drop followed by a 2.4-3.9 m step up is a skill line (the bhop bounce clears it), while
    walkers take the stairs.
  - Use rubber floors (bounces) and asphalt (speed lanes) sparingly, each with a clear reason. There are no jump pads.
  - Teleporters: choose **carry** (keeps all speed, for flow) or **walk** (caps to run speed, for fairness or to stop
    a speed exploit) on purpose (q's toggle).

### 5.13 Grapple geometry

There is little published writing on grapple-specific design. These rules come from Quark's numbers.

- **An anchor is a route.** Every solid surface within 45 m is a potential exit. Plan anchors (beams, arches,
  overhangs, the undersides of bridges) at 6-25 m from where players stand, so a pull gains height and speed and lets
  go somewhere useful.
- **Deny the grapple on purpose:**
  - open sky (nothing within 45 m);
  - `nograpple` (make it look different);
  - `clip`, which the hook doesn't hold on (and which is invisible: use it only for containment);
  - ceilings too low to swing.
  Deny it around objectives you want contested on foot, and at the map's edges for containment.
- **Ceilings are perches** (pinning is intended). A ceiling over an objective within 45 m is a place to hang and
  shoot from: decide whether you want it.
- **Close rooms:** the grapple pulls players together (10 damage), so in close rooms with low ceilings it is a weapon.
  Rooms over 10-15 m across give room to grapple away.
- **Pillars are turning posts** (a swing turns 41 degrees at 24 m/s).
- q: grapple-only routes are fine ("even a complete noob can grapple across a gap or up a platform"). Don't add stairs
  just for bots.

### 5.14 Smooth collision

- **Sources:** if something isn't big enough to hide behind, clip it off; ramp-clip stairs on strafe routes (Quake 4
  docs). Getting stuck on decoration is a serious problem in 1on1 (Unreal Wiki). Surf ramp seams cause "rampbugs"
  (Momentum Mod).
- **Quark rule:**
  - **No lip 0.6-1.8 m tall on main routes or strafe lines** (4.8). Kerbs and steps under 0.55 m are free.
  - Nothing sticks out of walls along strafe lines, and there are no gaps narrower than a player that look passable.
  - Stairs on main routes are covered by a wedge, or built as a ramp. The wedge is Quark's ramp clip. (Stairs at ≤ 0.55
    m risers don't cost speed, but a ramp reads and lands cleaner.)
  - Detail near routes is either big enough to be cover, `clip`ped smooth, or removed.
  - **Surf:** one wedge per ramp face where possible. Where ramps must join, join them on a straight edge at the same
    angle, and test every seam at speed.

### 5.15 Decoration versus momentum

- **Sources:** make it fun before making it pretty; decoration changes gameplay (Mapcore). Dust2's trim rules: never
  on anything you stand on, at most twice on a wall (Johnston). Under-decorate (Burkart).
- **Quark rule.** q: "dont spam too many random object because momentum is huge in quark".
  - **Decoration goes outside the movement envelope:** above head height plus a hop (over ~3 m) on walls, beyond the
    playable footprint, or as `far` scenery. Inside the space players move through, every object is cover, route,
    landmark, or gone (MAP_STRUCTURES: "a structure earns its place").
  - **No loose small props on the floors of main routes, ever.** Cover closer than ~8 m apart on a fast lane is
    decoration (4.9).
  - Tell the theme with big shapes and a few unique landmarks, not many small things.
  - Decoration comes **after** the layout is checked and played (section 7).

### 5.16 Symmetry and fairness

- **Sources:** asymmetry is "really hard to get right" (The Level Design Book). Blizzard aims at 50/50 per map and
  changed a point data called fair because players felt it wasn't (Adams, Keller).
- **Quark rule.** q: maps never have to be symmetrical; "would it be fair even though it's asymmetrical?"
  - **Equal-sides team modes** (vs, power, ball, koth, npayload, points): mirroring or rotating is the easy way to
    fairness (`mirror=True` is point-symmetric). Then make the two halves *look* different (warm and cool, Valve's
    bases), so players know where they are.
  - **Attack and defence modes** (bomb, payload): asymmetric by nature. Balance by measured times, and swap sides.
  - **1v1 and FFA:** no mirror needed, but each spawn's times to the key places must match within 1 s.
  - Fairness is judged by a stopwatch and by imagining real players, never by a bot run.

### 5.17 Per-mode layout patterns

Mode facts from the code:
- **One life a round:** vs, bomb, duel, 1v1, tourney, hide.
- **Respawn delays:** FFA 2 s, koth 4 s, points 5 s, payload 5 s, ball 3 s, power 2 s (falls only), race and surf 1 s.
- **Stalemate drain:** after 30 s with no damage dealt, health drains 5 hp/s.

**1v1, duel and tourney** (one life a round: like Clan Arena, not Quake Live item timing). Duel and tourney play every
1v1 map; a map offering 1v1 also offers duel, vs and tourney.
- 2-4 major rooms on one loop with cut-throughs. Each spawn to the middle in 4-6 s at run speed, both within 1 s.
- The strongest position is equally far from both spawns. The 30 s stalemate drain handles campers, but a perch
  nobody can approach without 3 s of exposure will still decide rounds.
- With `holdfire` (Dojo 5 s: both start facing, no firing), the start area gives both players the same options for
  those seconds.
- Duel can have size modifiers (4.5 m doors). The qk maps stay competitive 1v1: box and accent, slopes allowed, no
  healing tiles.

**FFA** (2 s respawn)
- A ring of medium rooms round a central contested space. Every room has 3 exits. No strong perch sees most of the
  map.
- 16+ spawns for 8 players, any spawn 3-5 s from a likely fight.
- **Instagib FFA** (ranked, Spire): break long lines; many short sightlines.

**Team vs** (rounds)
- 3 lanes from each spawn cluster to a central area, linked near the middle. No single position covers all chokes.
  Flank routes cost time. First contact 6-10 s after the round starts.

**Power** (team health pools of 2,500 hp a player; nobody dies of damage, only by falling)
- As team vs, but fights are long: each lane needs cover to retreat behind and a place to regroup. Keep the middle wide
  enough for 4v4 movement.
- Falls are the only deaths, so voids and edges are the kill mechanism. Place them deliberately: an edge by every main
  fight, but no bridge so narrow that one hit ends it.

**Bomb** (plant 3 s, defuse 6 s, fuse 40 s, the blast kills within 18 m, a dropped bomb returns in 15 s)
- **q's timing rule:** attackers reach a site in about **5 s at 24 m/s (~120 m along the walked route)**; defenders
  about **1 s later (~144 m)**. Measured: a 75% strafe-hopper covers 120 m from spawn in about 4.9 s.
- Bomb spawns follow the role, not the team: the attack always starts on `a`, the defence on `b`.
- **Layout:** two sites, each with 2-3 approaches, one choke per approach, and at most two entrances within a choke,
  both visible from one defender spot. At least 2 retake routes from the defender side.
- **Retakes:** a retake fits inside the 40 s fuse with a 6 s defuse.
- **Post-plant:** attacker spots outside the 18 m blast with a view of the bomb.
- **Roofs:** site roofs are reachable by rockets and the grapple. Decide which are play space.
- The research's CS advice (defenders at chokes 2-5 s first) is an open question against q's rule (9.1). q's rule wins
  for the sites.

**King of the hill** (one `hill`; 4 s respawn)
- The hill near the middle with 4+ approaches at 2-3 heights, and cover on the hill itself so holders aren't just
  targets. Overlooks round it are themselves exposed.
- Spawn to hill 6-10 s at run speed. Shorter makes holding impossible; longer makes it unbreakable.
- Use `koth-a/-b` spawn sets on maps built for another mode (as Mine does).

**Payload** (one team pushes; the cart 1.6 m/s, +25% per extra pusher up to 3.2 m/s; it rolls back at 0.6 m/s after
10 s alone, never past a checkpoint; +60 s a checkpoint; 5 s respawn)
- The fight is about the cart's surroundings, not travel. Segments take 30-45 s to push uncontested at 2 pushers
  (2.0 m/s): **60-90 m of track a segment**.
- The track runs on low ground with defended high ground beside it. Cover along it keeps pushers in push range.
- Chokes go only just before or on a checkpoint, and each checkpoint sits in front of a holdable space.
- Staged `att`/`def` spawns move forward as checkpoints fall. Wrap a long track (spiral or S, as Upward does) to keep
  the map compact.
- **Keep the 2.4 x 1.8 x 1.5 m cart's sweep clear** (rule 18).

**Neutral payload (npayload)** (the cart starts in the middle; A pushes to the end, B to the start)
- The track is symmetric about the middle, with equal spawn-to-cart times. Each half has the same high ground and
  cover. Otherwise as payload.

**Points (multi point)** (12 s to capture alone, faster with more, up to twice; +90 s per point; 5 s respawn; sides
swap)
- 2-4 points (3 is the default shape: near, middle, far), with the middle one contested. Each point has cover from at
  least two directions and more than one way in and out.
- Point to point 6-10 s at run speed, so rotating is a choice.
- Staged `att1..9`/`def1..9` spawns as points fall. Neither team spawns closer to the next point than the rules mean.

**Ball** (a 1 m ball with ice physics at half gravity, moved by shots, blasts and bodies; the grapple catches it; two
goals; 10 minutes; 3 s respawn)
- An open pitch or a large hall with goals at each end, side lanes and a high level for passes.
- Goals are hard to score into from far away.
- `ballwall` keeps the ball in play (only the ball collides with it).
- Leave clear air for grapple catches and rocket passes. Avoid any geometry where the ball can come to rest out of
  reach.
- Equal team starts relative to the ball's spawn.

**Hide and seek** (5 s head start while the seeker faces the wall; hiders win if anyone lasts 300 s; one life)
- Many small connected rooms and loops, so being found isn't the end. Hiding spots near loops; varied heights.
- Lighting can be darker *locally*, but players must stay visible once seen (q). The netcode always sends hiders
  within 24 m (`kVisNearHide`, server.cpp:88).
- Every spawn reaches every other.

**Tag** (60 s rounds; whoever is "it" is out; a new tagger is frozen 2 s)
- Loops above all: a tagger chasing with the knife means every room needs 3 exits and no dead ends.
- Mix fast lanes with tight turns so both chaser and chased have choices.

**Race** (1 s respawn; checkpoints in order, the last is the finish; `racedefault` sets the ranked total's default
time)
- A clear main line any player can finish, with harder, faster alternatives (rocket jumps, grapple chains, supers,
  the pop).
- Checkpoints after each hard section and roughly every 10 s of an optimised run, mid-straight, never skippable.
- The start zone is big enough to prestrafe. A fall costs seconds, not a minute.
- No fog, flashing lights or invisible clips that hurt the run. Keep the shortcuts players find.
- Race may gate routes behind tech; that's its point. Race courses set `no_ffa`.

**Surf** (run only: no dash, no grapple; touching a ramp face doubles air acceleration and gives back half of
gravity)
- Faces steeper than 45 degrees are surfable. **About 50-65 degrees is the forgiving range** (Momentum's 5:4 ramp is
  about 51).
- Flat tops or safe landings between ramps. Wide ramps first, narrow ramps later.
- One clean collision face per ramp, every joint tested at speed (rule 1 also applies: a wedge overlapping a wall is a
  pair the fixers don't fix).
- Height lost is speed gained: a 55 m descent gives ~60 m/s. Plan the next ramp for that speed.
- Teleporters are surf's usual resets (choose carry or walk).

### 5.18 Blockout first, playtest always

- **Sources:** "You can't playtest a design document"; "99% of the time, your blockout will not survive a playtest";
  the commonest mistake is scale (The Level Design Book). Respawn built 100-200 rough prototypes and threw most away
  (Zampella). Watch players more than you listen to them; when feelings and data disagree, fix the feeling (Keller).
- **Quark rule:** sketch the loop graph, block out at real size in plain boxes and wedges, play it with full movement
  (run, bhop, dash, grapple, rockets) before any dressing, and change or delete freely. A change in a playtest must be
  big enough to feel: double or halve it. Bot and automated checks are not a substitute (q).

### 5.19 Metrics converted from other games (for reading sources)

Converted by player height: Q3 1 u = 3.2 cm, Source 1 u = 2.54 cm, TF2 1 u = 2.17 cm. **Time transfers between games;
distance doesn't.**

| measurement | source | Quark scale | note |
|---|---|---|---|
| Q3 step | 18 u | 0.58 m | Quark 0.55 m |
| Q3 jump height | 45.6 u | 1.47 m | Quark 1.22 m (1.75 m ledge with the air step) |
| Q3 run | 320 u/s | 10.3 m/s | Quark 9.5 |
| Q3 rocket | 900 u/s, 120 u splash | 28.9 m/s, 3.86 m | Quark 48 rising to 80 m/s, 3.6 m |
| Q3 storey (Howard) | 192 u | 6.2 m | still a good storey |
| Q3 RA-MH choke hall | 256 x 128 u | 8.2 x 4.1 m | 0.9 s across at run speed |
| TF2 Soldier rocket jump | 448 u | 9.7 m | Quark ~17-27 m |
| TF2 2Fort long hallway | 1024 x 128 x 180 u | 22 x 2.8 x 3.9 m | 2.3 s at Quark run |
| TF2 combat ranges | 256 / 1024 / 2048 u | 5.6 / 22 / 44 m | Quark: under 10 / 10-30 / over 30 m |
| Source minimum hallway | 64 in | 1.6 m | Quark: 2.4 m minimum, 4 m+ main routes |
| Red Eclipse level spacing | within 16 m | | |

---

## 6. Case studies

Short lessons from classic maps.

| map | why it works | Quark lesson |
|---|---|---|
| **Q3DM6 The Camping Grounds** (id; QC's Blood Covenant) | three stacked levels whose footprint shrinks going up: middle about half the ground floor, top only slim paths and bridges (Howard). The quickest route between MH and RA, the staircase, is exposed to the LG (cityy) | shrink the footprint as you climb; put the fastest route between two strong places through danger |
| **Q3DM17 The Longest Yard** (Brandon James) | floating islands and jump pads, no hiding spots; the far platform lets snipers dodge rockets and hit players on predictable arcs (Craddock; The Level Design Book) | predictable flight paths (rubber, fixed anchors, launch lines) are targets. Give fliers a choice of arcs, or make open air the point on purpose (Black Hole's zero-g). With 44 m/s enemy launches, void maps turn every rocket into a ring-out |
| **Aerowalk** (Preacher) | one central atrium with three control points read from it, teleporters as one-way shortcuts (QuakeWorld wiki) | one big multi-level room with many entrances can carry a duel map if short loops lead back into it |
| **Blood Run / ZTNDM3** (ztn) | most paths lead back to a central atrium; four levels; upper levels tie the branches into a figure 8 (Howard). Clustered spawns in the RA room cause spawn frags (QuakeWorld wiki) | the figure 8 with a hub is a strong duel shape; never cluster spawns in the hub |
| **Toxicity** (TheFury) | two levels with small height gaps, chaotic control shifts with few resources (Howard; cityy) | a flat-ish, tightly looped map shifts control fast: good for FFA and rounds nobody should dominate |
| **Lost World / Q3DM13** | an atrium with two levels below; the low RA is attacked with rockets from above (cityy). The two-door YA room is a trap (Howard) | a strong low spot under overlooking ledges balances itself through splash; a two-exit room on the edge is a coffin |
| **2Fort** (Valve) | mirrored forts across a canal, a covered bridge, three levels of entry; battlement windows see most of the map (TF2 Wiki) | one exposed crossing plus a perch over it makes stalemates: give crossings several heights and a covered option |
| **Badwater Basin** (Valve) | four checkpoints, each with overlooking high ground (cliffs, roof, attic, catwalks); spawns move with the front (TF2 Wiki) | each checkpoint is an arena with its own high ground; spawns move so travel never dominates |
| **Upward** (Valve) | the track spirals up a mountain; spawns move forward; a shortcut shuts once C falls (TF2 Wiki; VDC) | wrap long tracks round themselves to stay compact; routes can open and close with the front |
| **Viaduct** (Valve, KOTH) | a raised point under a bridge with approaches at several heights; spawn sheds with 2 exits then 3 routes; capture cut from 6 to 3 s (TF2 Wiki) | a raised hill, approaches at different heights, contestable overlooks, a short clear spawn exit system |
| **de_dust2** (Dave Johnston) | a figure of eight; close and long play styles; arches make the chokes and block sightlines; crates tuned so one AWP can't hold a lane; spawns and sites tuned by time (Johnston) | for bomb: two routes per site plus a mid linking them, chokes made of architecture, positions tuned by measured times. Simplicity beats cleverness |

---

## 7. Workflow

q's order (2026-10-30): **layout, checks, decoration, adversarial review, then q playtests.** Never tune or gate a map
on bot runs.

### 7.1 Layout

1. **Read the mode's pattern** (5.17) and write the targets down: times, exits, heights, lanes.
2. **Sketch the room-and-link graph:** loops, exits per room, walk and tech exits, chokes, perches, spawns.
3. **Block out at real size** with plain boxes, wedges and the objectives, `dress=False`, no props. For each room,
   note in the module docstring what it is for (MAP_STRUCTURES style).
4. **Measure with a stopwatch:**
   - spawn to every objective, both sides, at run speed;
   - then at a good player's speed (use 4.4 and 4.7, or play it);
   - write the numbers in the docstring.
5. **Walk every ledge, gap and ceiling against section 4.** Who gets up here? What does a rocket jump reach from here?
   Can anyone reach outside the map?

### 7.2 Checks

- `python3 tools/make_maps.py --map <name>`, then `echo '{"file":"maps/<name>.qmap"}' | build/quark_maptool check`.
  Read each failed check's `detail` and `where`.
- `python3 tools/make_maps.py --map <name> --tidy-report /tmp/<name>-tidy.jsonl` lists what tidy fixed and what it left
  (pairs it won't fix are rule 1-5 problems you must fix in the module).
- `build/quark_tests maps` and `build/quark_tests bounds` (`OOB_TRACE=<name>` lists escape spots). Then the whole
  `build/quark_tests` before a commit.
- Hard rules 10, 11, 15 and 16 have **no automated check**. Look:
  - every ladder's rungs (screenshot each ladder);
  - every thin piece (is it meant to be wallbang?);
  - F3 lows in the heaviest view;
  - dark corners with a dark player model.
- For the bot and route checks, see section 3.2.

### 7.3 Screenshots

Headless Linux: `Xvfb :99 &`, then `DISPLAY=:99` before each command.

```
build/quark --backdrop <map> --cam x y z yaw pitch --screenshot shots/<map>-01.png   # a fixed camera, no match
build/quark --backdrop <map> --orbit 45 --screenshot shots/<map>-orbit.png           # the orbit camera at 45 degrees
build/quark --host <mode> --map <map> --screenshot shots/<map>-spawn.png             # in a match, from a spawn
```

- `--cam x y z yawDeg pitchDeg` puts the camera at a fixed spot (pitch negative looks down). It also works in a match.
- `--shot-delay S` waits before the shot (give the first light bake time).
- `--yaw` / `--pitch` turn the player at spawn.
- **Overviews with fog off:** make a data folder whose `quark.cfg` has the line `fog=0` and pass it with
  `--data-dir DIR`. A map in `DIR/maps/` also loads by name, so the same folder can hold a work-in-progress map.

  ```
  mkdir -p /tmp/shotdata && echo "fog=0" > /tmp/shotdata/quark.cfg
  DISPLAY=:99 build/quark --data-dir /tmp/shotdata --backdrop <map> --cam -35 44 -24 -124 -48 --screenshot shots/<map>-01.png
  ```

**A useful shot set per map (about 20):**

| kind | how many | camera |
|---|---|---|
| overview | 4 | fog off, from each high corner looking at the middle (e.g. 44 m up, pitch about -48) |
| spawn | per spawn group | at a spawn, eye height (feet y + 1.6), the spawn's yaw: exactly what the player sees. A spawn shot staring at a wall is a real finding |
| spawn from behind and above | per spawn group | |
| objective | per objective | from each approach |
| floor | several | head height on a real floor, looking down the longest open line: sightlines and lanes |
| ladders | every ladder | front on, to show every rung (rule 11) |

Keep each shot's id, kind and camera line `[x, y, z, yaw, pitch]` next to it (a `shots.jsonl`), so a reviewer can
re-take it.

### 7.4 Decoration, then adversarial review

- **Decoration** only after the layout passes and plays well (5.15). Re-run the checks and the shots after it, and
  compare the floor shots: did anything land in a lane?
- **Adversarial review.** Several reviewers each take a point of view, for example:
  - a competitive duel player;
  - a casual newcomer;
  - a movement and speedrun player;
  - an objective-mode team player;
  - a performance and visuals critic.

  Each looks at every shot and the source, and writes per-shot notes: what's there, what's right or wrong, and why.
  They also write a whole-map note: loops, speed and flow, tightness, sightlines, readability, decor and odd design,
  mode fit, fun, a score, and the top strengths and issues. Reviewers judge human play (q's ruling), never the bot
  checks. A bad camera spot (inside a wall) is noted as such and not held against the map.
- **The review page.** Put the shots and the notes on one page per map (a published artifact page works well): each
  shot with its camera line and the reviewers' notes beside it, then the change list. q reads it there. Every change
  list includes the fixes for any hidden ladder rungs.

### 7.5 q playtests

Ship to q to play once the checks pass and the review's changes are in. q's judgment of play is final. Record his
rulings in section 0 and the map's reasons in MAP_STRUCTURES.md.

---

## 8. Checklist before shipping a map

**Files and pipeline**
- [ ] One `for <mode>` the map supports, a `setting`, a `title` (≤ 40 chars); the name isn't a mode's name.
- [ ] Generated: the module's `build()` calls `m.save("<name>")`; the name is in `MAPS`; the docstring says what every
      structure is for. Hand-made: tidied (`tidy.py --qmap`), `!/maps/<name>.qmap` in `.gitignore`.
- [ ] No generated `.qmap` edited by hand or committed; no format change without version + converter + test.
- [ ] Parses with zero warnings; `quark_maptool check` read through; `quark_tests maps` and `bounds` run; the whole
      `quark_tests` passes before the commit.

**Hard rules (section 3)**
- [ ] No z-fighting pairs, including pairs the fixers leave (slopes, glass, clip, liquids, gameplay materials).
- [ ] No pieces inside or overlapping others; no hidden pieces; no needless pieces; no gaps under 1/8 m; faces on the
      1/8 m grid.
- [ ] **Every ladder rung and rail visible**: screenshot of each ladder checked.
- [ ] No piece meant to stop bullets is 1/8 m thin; intended shoot-through uses `wallbang`.
- [ ] Spawns clear of solids, on floor tops, facing into the map.
- [ ] Nothing reachable out of bounds, including rocket jumps (27 m), launches (~50 m) and the bubble launch (73 m),
      checked by hand.
- [ ] No heavy materials or effects in one view; F3 1% and 0.1% lows checked in the heaviest view.
- [ ] Not so dark that a dark player model vanishes.
- [ ] Ranked maps: ranked rules (rule 14) and a fair start, checked with a stopwatch.

**Layout and flow**
- [ ] A big loop through every major area; every major area on 2+ loops; no forced 180 degree turns on main routes.
- [ ] Every fighting room has 3+ walk exits, not all on one side; every perch has 2+ ways off; no dead end without a
      reason and a quick exit.
- [ ] 2-3 floor planes per area; no corridor over 30-40 m without a turn, a height change or a side exit.
- [ ] Main routes 4 m+ wide; corners on fast routes wide enough for the speed (4.6); doorways ≥ 2.2 x 1.6 m (4.5 m tall
      on maps offering duel).

**Movement**
- [ ] One long strafe line (40 m+) on the main loop, free of snags.
- [ ] At least one tech shortcut per area, none required (except in race).
- [ ] No lips 0.6-1.8 m on fast paths; no 1.8-2.2 m "almost" ledges on everyone's routes.
- [ ] Every roof under ~27 m is play space with a purpose and a counter, or unstandable.
- [ ] Grapple anchors planned; `nograpple` and open sky used on purpose, and they look different.
- [ ] Ceilings chosen for their effect (5.5); landing zones left open after launch spots (4.9).
- [ ] Ring-outs (voids, acid, edges) only where meant; bridges over voids wide enough that one rocket doesn't decide
      it.
- [ ] Teleporters set to carry or walk on purpose.

**Sightlines, spawns, readability**
- [ ] Close, medium and long ranges present; no spot sees more than two of a room's entrances down a long line;
      instagib maps keep most lines under 30-40 m.
- [ ] Cover is architecture, 2 m+ tall, with a clean run past it.
- [ ] Spawns out of long sightlines; FFA 16+ spawns for 8 players; team spawn exits not down a lane to the enemy.
- [ ] One landmark per major area; objectives readable from every approach; special surfaces distinct and consistent.
- [ ] No decoration inside the movement envelope; no loose props on main-route floors.

**Mode**
- [ ] The mode's timings met (5.6, 5.17). Bomb: ~120 m (5 s at 24 m/s) for attackers, ~144 m for defenders, along the
      walked route.
- [ ] Every offered mode actually plays (each spawn pool full; objectives placed; cart sweep clear; the ball can't
      rest out of reach).
- [ ] Fair, judged by a stopwatch and by imagining real players: symmetric or not.

**Write-up and hand-off**
- [ ] MAP_STRUCTURES.md section; the CLAUDE.md pool line; RELEASE_CHECKLIST.md if the pool changed; changelog notes
      (`changelogs/next.md`, `changelogs/full/next.md`).
- [ ] Shots taken (overviews with fog off, spawns, objectives, floors, ladders) and reviewed adversarially.
- [ ] Committed locally, not pushed; handed to q to playtest.

---

## 9. Open questions for q

These are **OPEN**. Don't treat any answer as given until q rules. When he does, move the ruling into section 0 and
delete the question.

### 9.1 Design questions

| # | question | why it matters |
|---|---|---|
| Q1 | **Pickups:** Quark has no health, armour, ammo or power-up items. Is that permanent? | classic quakelike layouts are built round item control; this document replaces them with positions, objectives and dash-refill floors (5.3) |
| Q2 | **Rocket jump heights:** a rocket jump reaches 17 m (no jump), 24.3 m (jump + fire) and 27.3 m (best timing) for ~3 hp. Launches and sticky stacks reach ~50 m, the bubble launch 73 m. Is that intended? | it decides ceilings, roofs and boundaries on every rocket map (5.4, 5.5) |
| Q3 | **Ring-outs:** enemy rockets push up to 44 m/s up. Should void and acid edges be a deliberate feature (power is falls-only), or kept rare? | bridge and ledge widths (5.8) |
| Q4 | **Jump pads:** there are none; rubber floors (85% bounce), teleporters, low-gravity fields and the gravity flip are the only movement blocks. Do you want real jump pads? | vertical links between floors, Longest-Yard-style maps |
| Q5 | **Straights:** strafing is uncapped: 41 m/s after 5 s and 61 m/s after 10 s for a good player, 72-108 m/s for a perfect one. Should maps cap the length of open straights, and at what? | lane lengths, cover spacing (4.4, 5.12) |
| Q6 | **Dash up share:** the code is 50% up (pmove.cpp:75, your change from 40% on 2026-10-28); an older brief says 40%. Is 50% final? | the dash heights in 4.3 |
| Q7 | A super looking up then 2 dashes up reaches 10.8-11.25 m and crosses 59-68 m gaps from standing. Intended? | ledge and gap gating (4.3, 4.5) |
| Q8 | A dash replaces your velocity, so dashing during a rocket jump throws its height away (24.3 m to 12.7 m). Intended? | rocket-route design |
| Q9 | Prestrafe (turning on the floor while running) reaches 14.3 m/s on the ground. Intended? | run-up and gap figures |
| Q10 | Dashing up while facing a wall bounces you off it (the dash's level part hits it within 45 degrees). Intended? | "dash up the wall" routes don't work head on |
| Q11 | **Bomb:** your rule has defenders reach the site ~1 s *after* attackers (144 m against 120 m). The research (CS practice) has defenders reach their chokes 2-5 s *before* attackers. Should defenders still reach their own chokes first, with the site itself 1 s later? | every bomb layout (5.17) |
| Q12 | **The bot and route checks** (3.2) still fail `quark_tests` for generated maps. Should they become warnings (like HUMAN-MADE), or be removed, so a design isn't bent to pass them? | the conflict between your 2026-11-10 ruling and "quark_tests must pass" |
| Q13 | **Ladder rungs:** should a test check that no rung or rail intersects another piece, so rule 11 has an automated check? | rule 11 has no check today |
| Q14 | **Out of bounds with rockets:** the bounds test simulates hops, dashes and the grapple, not rockets, stickies or bubbles. Add them? | rule 9 |
| Q15 | **Darkness in hide and seek:** how dark may hiding spots be, given that players must stay visible? | 5.10, 5.17 |

### 9.2 Docs and code that don't match (found 2026-11-10)

| # | finding |
|---|---|
| D1 | CLAUDE.md and MAP_STRUCTURES.md say Cobblestone has no `for` line; `maps/cobblestone.qmap` has `for ffa` and `modes ffa 1v1 duel vs tourney`. The menus still default ffa to Sewer through `defaultMapName`. |
| D2 | `testSpawnSanity` (facing, apart, enemy spawns in sight) and `testCartSweep` have no `HumanMadeGrace`, so a hand-made map that misses them fails the run, unlike every other per-map check. |
| D3 | Riverside offers `1v1` and `duel` but not `tourney`. CLAUDE.md says tourney plays every 1v1 map; only `M.save` adds it, and hand-made maps get what the editor wrote. |
| D4 | Accelerator's modes are `bomb race`, with no ffa. That may be intended (race courses set `no_ffa`). |
| D5 | The build doesn't tidy hand-made maps; `tidy.py --qmap` is a manual step. Run from the repo, it also writes `tools/maps/__pycache__/`. |
| D6 | The `rocketjump` tutorial bay is 24 m tall, but its own demo reaches 24.3 m (feet): the ghost bonks the ceiling. The lesson still passes. Stale comments: pmove.cpp:92-98 says a hyper from a run goes up 3.87 m (it's 3.54 m at gravity 24); tutorial.cpp:95 says a rocket jump goes 17 m (jump + fire is 24.3 m). |

---

## 10. Sources

The research behind section 5 and 6. "Summary" marks sources only read through a summary.

**Developers' own writing, talks and commentary**
1. id Software, Quake III Arena GPL source (`bg_pmove.c`, `g_items.c`, ...): https://github.com/id-Software/Quake-III-Arena
2. David L. Craddock, "Rocket Jump: Quake and the Golden Age of First-Person Shooters" (Shacknews; Romero, Willits,
   Jaquays, Devine, Brandon James): https://www.shacknews.com/article/101156/rocket-jump-quake-and-the-golden-age-of-first-person-shooters?page=4
   and https://shacknews.com/article/101156/rocket-jump-quake-and-the-golden-age-of-first-person-shooters?page=16
3. id Software, Team Arena Mapping Help, "Design Tips and Suggestions":
   https://www.icculus.org/gtkradiant/documentation/Team_Arena_Mapping_Help/pages/design_tips.html
4. Quake 4 editor docs, "Clipping Your Level": https://iddevnet.dhewm3.org/quake4/LevelEditor_Clipping.html
5. Dave Johnston, "Making Dust2": https://www.johnsto.co.uk/design/making-dust2
6. Dave Johnston in "Breaking Down the Greatest FPS Map With Its Creator" (Fandom):
   https://www.fandom.com/articles/counter-strike-dust-2
7. Valve, TF2 developer commentary: Well https://wiki.teamfortress.com/wiki/Well_developer_commentary , Gravel Pit
   https://wiki.teamfortress.com/wiki/Gravelpit_developer_commentary , Hydro
   https://wiki.teamfortress.com/wiki/Hydro_developer_commentary
8. Mitchell, Francke, Eng (Valve), "Illustrative Rendering in Team Fortress 2" (NPAR 2007):
   https://www.cs.princeton.edu/courses/archive/fall07/cos597B/papers/mitchell-team-fortress.pdf
9. Valve, "Stylization with a Purpose" (GDC 2008):
   https://cdn.steamstatic.com/apps/valve/2008/GDC2008_StylizationWithAPurpose_TF2.pdf
10. Epic Games, "UT3 Multiplayer Map Theory": https://docs.unrealengine.com/udk/Three/UT3MultiplayerMapTheory.html
11. Blizzard on Overwatch maps: Dave Adams on Eichenwalde
    https://kotaku.com/an-in-depth-look-at-overwatchs-eichenwalde-map-1797348568 ; Aaron Keller on Rialto (summary)
    https://blizzardwatch.com/2018/05/16/overwatch-assistant-game-director-dives-design-rialtos-deadly-canals ; Geoff
    Goodman on mobility
    https://gamedeveloper.com/design/the-challenge-of-designing-i-overwatch-i-maps-when-players-can-get-literally-everywhere-
12. James Holloway, "Deathmatch Map Design: The Architecture of Flow" (Pearson, Clopper, Smith):
    https://www.gamedeveloper.com/design/deathmatch-map-design-the-architecture-of-flow
13. Bethesda / id, Quake Champions "Map Guide: Ruins of Sarnath": https://quake.bethesda.net/en/news/4DhumJtVzGqw26Em6Wiiow
14. Jean Paul LeBreton, "Making new arena maps" (Rune): https://www.moddb.com/tutorials/making-new-arena-maps
15. Respawn on Titanfall 2 prototyping (summary):
    https://gamedeveloper.com/design/understanding-i-titanfall-2-i-s-action-block-level-prototyping-process and
    https://80.lv/articles/gdc18-designing-titanfall-levels-with-action-blocks
16. CliffyB and Levelord on level design:
    https://quartertothree.com/features/doubletime/cliffyB_and_levelord/on_level_design.shtml
17. PC Gamer, "Building Crown, part two" (FMPONE and Volcano):
    https://pcgamer.com/building-crown-part-two-layout-design-textures-and-the-hammer-editor

**Mapper and designer writing**

18. The Level Design Book (Robert Yang and contributors):
    - Metrics https://book.leveldesignbook.com/process/blockout/metrics
    - Flow https://book.leveldesignbook.com/process/layout/flow
    - Verticality https://book.leveldesignbook.com/process/layout/flow/verticality
    - Circulation https://book.leveldesignbook.com/process/layout/flow/circulation
    - Balance https://book.leveldesignbook.com/process/combat/balance
    - Blockout https://book.leveldesignbook.com/process/blockout
    - Playtesting https://book.leveldesignbook.com/process/blockout/playtesting
    - Wayfinding https://book.leveldesignbook.com/process/blockout/wayfinding
    - Chill Out https://book.leveldesignbook.com/studies/mp/chill-out
19. Pat Howard, "Fundamentals of Gameplay Part I" https://quake3world.com/forum/viewtopic.php?p=980902 , "Connectivity
    Systems" https://www.quake3world.com/forum/viewtopic.php?p=999103 , "Q3A Item Placement Guide"
    https://quake3world.com/forum/viewtopic.php?p=989410
20. cityy, "The Language of Arena FPS Level Design": https://plusforward.net/post/21433/The-Language-of-Arena-FPS-Level-Design
21. Geezer, "Quake 2 CTF Map Making Guide": https://dondeq2.com/2017/05/27/quake-2-ctf-map-making-guide-by-geezer/
22. Ben Burkart, "8 secrets of a great multiplayer map": https://80.lv/articles/8-secrets-of-a-great-multiplayer-map/
23. AlexG, World of Level Design: CS layouts
    https://worldofleveldesign.com/categories/csgo-tutorials/csgo-how-to-design-gameplay-map-layouts.php , choke points
    https://worldofleveldesign.com/categories/csgo-tutorials/csgo-principles-choke-point-level-design.php
24. Brendon Chung, "Level Design: Readability": https://blendogames.com/news/post/2026-06-xx-leveldesign_readability/
25. Hullett and Whitehead, "Design Patterns in FPS Levels" (FDG 2010):
    https://scispace.com/pdf/design-patterns-in-fps-levels-34xmly9ii3.pdf

**Movement-game mapping**

26. q3df.org, "Advice for building a good Defrag run map": https://ee.ws.q3df.org/level_design/good_df_map
27. Momentum Mod: "Creating Surf Ramps" https://docs.momentum-mod.org/guide/mapping/create_surf_ramps/ and the review
    checklist https://docs.momentum-mod.org/guide/map_submission/review_checklist/
28. Halogene, "Newbie Corner part 3: Ramp Jumping" (Xonotic):
    https://xonotic.org/posts/2012/halogenes-newbie-corner-part-3-ramp-jumping/
29. no-lex, Red Eclipse map layout guide: https://www.github.com/redeclipse/base/discussions/1165
30. Counter-Strike Wiki, "Surfing" (weaker): https://counterstrikewiki.com/Surfing
31. shockrah, "Mapping in Reflex" (weak): https://shockrah.xyz/posts/mapping/

**Community wikis**

32. Valve Developer Community:
    - TF2 Design Theory https://developer.valvesoftware.com/wiki/TF2_Design_Theory
    - Payload Design Theory https://developer.valvesoftware.com/wiki/Payload_Design_Theory
    - TF2 Mapper's Reference https://developer.valvesoftware.com/wiki/Team_Fortress_2/Mapper%27s_Reference
    - TF2 King of the Hill https://developer.valvesoftware.com/wiki/TF2/King_of_the_Hill
    - Deathmatch Map Design Theory https://developer.valvesoftware.com/wiki/Deathmatch_Map_Design_Theory
    - Color Theory in Level Design https://developer.valvesoftware.com/wiki/ColorTheoryLevel
33. Official TF2 Wiki:
    - Badwater Basin https://wiki.teamfortress.com/wiki/Badwater_Basin
    - Upward https://wiki.teamfortress.com/wiki/Upward
    - Viaduct https://wiki.teamfortress.com/wiki/Viaduct
    - 2Fort https://wiki.teamfortress.com/wiki/2Fort
    - King of the Hill https://wiki.teamfortress.com/wiki/King_of_the_Hill
34. Unreal Wiki (legacy archive):
    - Mapping For 1on1 https://unrealarchive.org/wikis/unreal-wiki/Legacy:Mapping_For_1on1.html
    - Map Flow https://unrealarchive.org/wikis/unreal-wiki/Legacy:Map_Flow.html
    - Bombing Run https://unrealarchive.org/wikis/unreal-wiki/Legacy:Mapping_For_BR.html
    - Double Domination https://unrealarchive.org/wikis/unreal-wiki/Legacy:Mapping_For_DDOM.html
35. QuakeWorld wiki and FPS Wiki:
    - Aerowalk https://www.quakeworld.nu/wiki/Aerowalk
    - ZTNDM3 https://www.quakeworld.nu/wiki/Ztndm3
    - Toxicity https://www.quakeworld.nu/wiki/Toxicity
    - Lost World https://www.quakeworld.nu/wiki/Lostworld
    - Blood Run https://fps.fandom.com/wiki/ZTNDM3:_Blood_Run
36. Gaming Nexus on Blood Covenant: https://gamingnexus.com/News/39226/Quake-III-Arena-map-gets-re-imagined-for-Quake-Champions

**Not reached** (paywalls, video only or not found):
- Dan Taylor, "Ten Principles for Good Level Design" (GDC 2013, https://gdcvault.com/play/1017803/Ten-Principles-for-Good-Level);
- Snelling and Garozzo, "Community Level Design for Competitive CS:GO" (GDC 2015,
  https://gdcvault.com/play/1021868/Community-Level-Design-for-Competitive);
- Lunaran's essays, Joel McDonald's Competitive Level Design Guide, Robin Mendoza's "Loop wrangling in Duel";
- design writing on Diabotical, Reflex arenas, Furious Heights, grapple-specific multiplayer maps, and hide-and-seek
  and tag maps.

The bomb timings in 5.17 beyond q's rule are conversions of qualitative advice. The rocket and drop-hop figures were
measured in the real movement code and server, not in a live match.
