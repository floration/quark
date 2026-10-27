# Weapons editor: design (v1)

The owner's ask (2026-10-24): a Weapons Editor tab under the Editor Manager in the Quarkology app, where they can
create and modify weapons, test them against static targets and bots, watch bot fights on maps, write custom weapon
logic in Code / Blocks / Graph views (like the bot brain and gamemode editors), and compare damage and DPS (head vs
body, alt fires) in a values table, also against as many other games' weapon numbers as can be found.

Three parts, each owned by one worker:

1. **Game**: weapon sets (`.qweapons` files) the game loads, applies and sends to clients; a small interpreter
   for the logic slots. `src/game/weaponset.{h,cpp}` (new), small hooks in `customrules.*`, `gamedefs.h`,
   `server.cpp`, the client and `bots.cpp`. Tests in `tests/`.
2. **Helper**: `quark_maptool` commands for the editor, in `tools/weapon_editor/native/*.cpp` (CMake globs it in).
3. **Page**: `tools/weapon_editor/serve.py` + `web/` on `127.0.0.1:8773`, installed by the Quarkology app
   (`installs.json` + tabs).

Performance comes first (CLAUDE.md): a weapon set without logic costs nothing per shot (the tuned table is built
once when the rules change); a set with a `hit` slot runs compiled code per hit (the laser hits up to 1000 times a
second per player), so the interpreter is a flat bytecode / tree walk with no allocation per call.

---------------------------------------------------------------------------------------------------------------

## 1. Weapon sets in the game

* A weapon set is a text file `<user data>/custom_weapons/<name>.qweapons` (grammar: section 2).
* A custom mode (`.qmode`) uses one with the line `weaponset=<name>` (owned by the weapons editor; the mode and
  physics editors keep lines they don't own verbatim). No line, or a set that doesn't load: the standard weapons.
* The host loads the file when the custom mode starts and keeps its text in `CustomRules::weaponSetText`. The
  network form of the rules (EV_RULES, already chunked 240 bytes x up to 255) carries the text as `wsline=<line>`
  lines, one per line of the set, so every client compiles the same set and predicts refire, spread, projectile
  speed / gravity and the scope with the same numbers. The disk form (`saveCustomMode`) writes only
  `weaponset=<name>` (the text stays in its own file).
* Applies on local, listen and unranked dedicated servers like any custom mode; never ranked or time trials (they
  never run custom modes). Demos record EV_RULES, so they replay with the set.
* Values are absolute (not multipliers). The mode editor's `weapon=<w>,<fireRate>,<damage>,<projSpeed>`
  multipliers still apply on top (refire / that, damage x that, speed x that), so the two editors never fight.
* Every weapon number the game uses becomes a field of the tuned table (`weaponDef()` and a new
  `weaponExtra(w)`), read on server and client through the per-thread tuned rules. Nothing reads the old
  constants directly any more except the standard table itself. Default set == today's game, bit for bit (a test).

### 1.1 Fields (per weapon; key `<weapon>.<field>`)

Weapon names: `rifle rocket knife sniper laser shotgun sticky crossbow bubble syringe locator`.

From `WeaponDef` (all weapons): `refire` (s), `damage` (hp per hit / pellet / blast centre), `range` (m, hitscan),
`projSpeed` (m/s), `splashRadius` (m), `spread` (rad), `scopedSpread` (rad, < 0 = no scope), `zoomFov` (deg),
`pellets` (int), `gravity` (m/s2), `life` (s), `semiAuto` (bool). `projectile`, `beam`, `sticky` are the weapon's
kind: shown, not editable (a variant keeps its base's behaviour).

Extra (all weapons, defaults from today's constants):

| field | default | what |
|---|---|---|
| `headDamage` | sniper 50, crossbow 75, rifle 7.5, shotgun 2.6, else 0 | damage of a hit on the head (0 = no headshots) |
| `knock` | weapon's own knockback (shotgun 0.30 per pellet's worth, crossbow 16, laser 20 m/s per s, projectile hit 3, splash 11) | knockback strength |
| `falloffStart`, `falloffEnd`, `falloffMin` | 0, 0, 1 | damage x 1 up to start, then linearly to x min at end (m); min 1 = no falloff |
| `directDamage` | rocket 50, else 0 | a projectile that hits the player itself does at least this (splash weapons) |
| `selfDamage` | 1 (splash below your feet: 0.1 of it, as today) | x your own splash damage |
| `wallbang` | 0.5 | x damage through a wallbang wall (hitscan) |

Alt fire (only the weapon that has it):

| field | default | what |
|---|---|---|
| `rifle.scopedRefire` | 1.111 | x refire while scoped |
| `rocket.lobSpeed`, `rocket.lobGravity` | 0.7, 13.5 | lob: x speed, its drop |
| `knife.throwSpeed`, `knife.throwGravity`, `knife.throwLife`, `knife.away` | 38, 14, 2, 4 | the throw |
| `knife.grow` | 0.04 | m wider hit boxes for a swing |
| `shotgun.bounces`, `shotgun.bounceDamage` | 2, 0.5 | bouncing pellets |
| `laser.pullScale`, `laser.pullDamage` | 1.82, 0.75 | the pull beam |
| `bubble.streamMin`, `streamMax`, `streamNear`, `streamFar`, `streamRefire`, `streamSlow`, `streamFast` | 5, 15, 5, 30, 1.8, 8.4, 24 | the stream |
| `syringe.throwHeal` | 30 | the throw's heal (its speed 30, drop 9, life 2 s and the 4 s away are fixed: `kSyringeThrow*`, `kSyringeAway`) |
| `sticky.max`, `sticky.armTime`, `sticky.inherit` | 6, 0.35, 0.9 | stickies |

Ranges (clamped like `CustomRules::fromText` clamps: out of range -> the nearest bound, nan -> default) are in the
catalog (`exportweapons`). A field a set doesn't write keeps its default. The page's built-in copy of the catalog
(`web/js/catalog_data.js`, for the MOCK) is generated from `exportweapons` by `build_catalog.py`, never typed by hand;
the page's tests fail while it differs from the game's.

## 2. The `.qweapons` grammar

The bot brain grammar (`tools/bots/GRAMMAR.md`) with these differences, so the bot editor's tokenizer, parser,
printer, Blocks and Graph code can be reused:

```
version 1
weapons "fast rockets"
label rocket "HOT ROCKET"          # optional: the weapon's display name in this set (<= 24 chars, A-Z 0-9 space)
param rocket.damage f32 = 40       # a field (section 1.1): one value (no skill levels)
param rocket.refire f32 = 0.9
param shotgun.pellets int = 16
param rifle.semiAuto bool = yes
param bonus f32 = 1.5              # the set's own constants: any name that isn't a field or keyword
curve rocket.falloff(dist f32) = points 0: 1, 20: 0.6     # optional per-weapon damage curve (replaces the
                                                          # falloff fields for that weapon)
slot hit:                          # every weapon without its own hit slot
  if head and dist > 30: damage = damage * bonus
slot hit sniper:                   # just the sniper
  if airborne: damage = damage * 2
slot kill:
  heal = 10
```

* Header: `version 1`, then `weapons "<name>"` (instead of `brain`). No `levels`; params take one value.
* Declarations: `label`, `param`, `curve`, `slot`. No `list` / `table` in v1.
* Slots: `hit`, `hit <weapon>`, `kill`, `kill <weapon>`. A weapon-specific slot runs instead of the general one.
* Outputs start at their **defaults** (not 0): `hit`: `damage` = the damage the game worked out (after falloff,
  head, wallbang, splash fraction), `knock` = 1 (x knockback), `heal` = 0 (hp to the shooter, up to max health).
  `kill`: `heal` = 0, `refund` = no (yes: the shooter's weapon cooldown resets).
* Sensors (read-only; f32 unless said): `weapon` (weapon), `alt` (bool: alt fire), `dist` (m, shooter eye to the
  hit), `head` (bool), `direct` (bool: projectile hit the player itself), `splash` (0..1: blast fraction, 1 for a
  non-splash hit), `self` (bool), `airborne` (bool: the victim isn't on the ground), `victimHealth`,
  `shooterHealth`, `victimSpeed`, `shooterSpeed` (m/s), `time` (f64: s since the round started), `streak` (int:
  this shooter's hits on this victim within 1 s, this one included). `kill` also: `head`, `airborne`, `dist`,
  `weapon`, `kills` (int: the shooter's kills this life).
* Builtins: `min max abs clamp sqrt floor ceil pow lerp` (as the bot grammar) and `f32()`, `f64()`, `int()`.
* Logic runs on the server only (damage is server-side); fields are compiled on both server and client.
* Errors come with `line:col`; `quark_tests checkweapons <file>` prints them as JSON.

## 3. Game-side tools (`quark_tests`)

* `quark_tests exportweapons <out.json>`: the catalog: `{fields:[{weapon, field, key, type, unit, min, max,
  default, doc, alt?, readOnly?}], weapons:[{id, name, title, kind: "hitscan"|"projectile"|"beam"|"melee"|
  "support", altFire: "<what alt does>"}], slots:[{name, sensors:[{name,type,unit,doc}], outputs:[...]}],
  builtins:[...], defaultText: "<the standard set as .qweapons text, every field>"}`.
* `quark_tests checkweapons <file>`: `{ok, errors:[{line,col,msg}], warnings:[...]}`, exit 1 on errors.
* `quark_tests weaponsast <file>` / `printweapons <file>`: JSON AST / canonical text (`print(parse(t)) == t`).

## 4. Helper commands (`quark_maptool`, the editor's)

JSON on stdin (or a file argument), one JSON answer (or NDJSON with `--stream`), like the bot and physics editors.
Every request may carry `weapons` (a `.qweapons` text) and `qmode` (a custom mode text); both applied with
`setActiveRules`, per thread.

* `wpnparams {weapons?}`: the catalog (section 3) plus `values`: every field's value in this set, `changed`.
* `wpncheck {weapons}`: errors / warnings with line:col, the AST.
* `wpnstats {weapons, compare?: [weapons...]}`: computed table per weapon and fire mode (primary / alt): damage
  body / head, shots to kill (100 hp, and `health` if given) body / head, time to kill body / head, DPS body /
  head (sustained, from refire), burst damage, range, projectile speed and time to 10/20/40 m, splash radius,
  falloff at 10/20/40 m. For each compare set the same, so the page can show differences.
* `wpnrange {weapons, map?, weapon, alt?, targets:[{pos, moving?: {axis, speed, amplitude}, health?, crouch?}],
  shooter: {pos, aim: "perfect"|"head"|"body"|"bot", skill?}, seconds, seed}`: the game's own `Server` with one
  shooter and still or moving dummy targets (no AI), fired for `seconds`. Answer: per target hits, head hits,
  damage, kills, time to first kill; per shot `{t, hit, head, dmg, target}`; a trace for replay (`frames:
  [{t, players:[{pos,yaw,pitch,health,alive}], projectiles:[...], shots:[...]}]`). Default map: a built-in range
  (a flat floor with targets at 5, 10, 20, 40, 80 m).
* `wpnfight`: `sim` / `simbatch` (bot editor) with the set applied and a weapon pool (`weaponsAllowed`), for bot
  vs bot on any map; reports per weapon kills, damage, accuracy, time to kill. Implemented by passing the rules to
  the existing sim (a `weapons` / `qmode` request key added to `tools/bot_editor/native/sim_cmd.cpp`), so the
  page's live replay is the bot editor's.

## 5. The page (`127.0.0.1:8773`)

Left panel: weapon set (open / new / save / save as / download, `custom_weapons/<name>.qweapons`), the weapon list
(the 11, with the set's label; changed ones marked), "Play in game": writes a `.qmode` `wpn-<set>` that points at
the set (base mode chosen) so Host > Custom modes can start it.

Tabs:

* **Weapon**: the selected weapon's fields in groups (damage, fire, projectile, spread / scope, alt fire,
  knockback, falloff), sliders + numbers with units, default and the change; a live mini table (TTK, DPS).
* **Values**: the comparison table: every weapon x fire mode: body / head damage, shots and time to kill, DPS body
  / head, range, speed, splash, falloff; standard vs edited (green / red difference); sort and filter; charts (DPS
  and TTK bars, damage vs distance curves).
* **Other games**: Quark's weapons next to other games' (data file `web/data/games.json`: game, weapon, class,
  damage body / head / limb, fire rate, health pool, source URL, patch / date). Normalised by each game's base
  health (shots to kill, TTK) so different health pools compare fairly; pick games and weapon classes; find "the
  closest weapon in other games" for each Quark weapon.
* **Code / Blocks / Graph**: the set's logic as text (the source of truth), blocks and a data-flow graph, three
  views of one AST (reusing the bot editor's dsl / blocks / graph code); errors in the gutter from `wpncheck`.
* **Range**: test against static and moving targets on the built-in range or any map (top-down and 3D replay,
  hit markers, per-target numbers), perfect-aim or bot-aim shooter.
* **Fights**: bot vs bot on maps with the set: watch one live (the bot editor's live view) or run a batch (win
  rates, per weapon kills / accuracy / TTK), standard vs edited side by side.
