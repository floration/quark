# The weapons editor's helper commands (`quark_maptool`)

The weapons editor page (`tools/weapon_editor/serve.py` + `web/`, DESIGN.md section 4) asks the game for its numbers
through these commands, linked into `quark_maptool` (CMake globs `tools/weapon_editor/native/*.cpp` into it, next
to the map, bot and physics editors' commands). Like every `quark_maptool` command: the request is one JSON object
on stdin (or a file argument), the answer one JSON object on stdout; a bad request answers `{"error": "..."}` with
exit code 1.

| file | what |
|---|---|
| `wpn.h` | the shared pieces: the request's rules (`weapons` / `qmode` / `weaponsAllowed`), the per weapon tally |
| `wpn_rules.cpp` | the rules, the tally, `wpnparams`, `wpncheck` |
| `wpn_stats.cpp` | `wpnstats` |
| `wpn_range.cpp` | `wpnrange`, `wpnrangemap` |
| `test_wpn.py` | the tests: `python3 tools/weapon_editor/native/test_wpn.py [quark_maptool]` |

Everything is the game's own code: the sets are compiled by `compileWeaponSet` (src/game/weaponset.h), the numbers
read from the compiled set's values, the range runs the real `Server` through the bot editor's `BotSim`
(tools/map_editor/native/botsim.h). Nothing here changes game code.

## Rules keys (every command that plays: `wpnrange`, and the bot editor's `sim` / `simbatch`)

| key | what |
|---|---|
| `weapons` | a `.qweapons` text (or `{text}`). Errors fail the request: `"weapons line L:C: msg"`. |
| `qmode` | a custom mode text (`.qmode` key=value lines, or `{text}`; no `custommode=` line: named `wpn-editor`). Read by `CustomRules::fromText` (clamped as a file is). Its map pool is dropped; a `weaponset=<name>` in it loads `custom_weapons/<name>.qweapons` from the player's data folder when `weapons` isn't given. |
| `weaponsAllowed` | the pool: `["rocket", "rifle"]` (keys, titles, a set's labels, or ids) or a bitmask; `CustomRules::weapons`. |

They become one `CustomRules` (name `wpn-editor` unless the qmode names itself) put on the server as
`ServerConfig::custom`; the server thread applies it with `setActiveRules` (per thread, as `Server::frame` does).
Answers echo `weaponSet` (the set's `weapons "<name>"`, null without one), `weaponLogic` (it has hit / kill slots),
`weaponsAllowed` (names, null = every weapon), `qmode` (its name or null) and `weaponWarnings` (clamped values, a
qmode's set that didn't load) when there are any.

## `wpnparams`

```
{weapons?}  ->  the catalog of `quark_tests exportweapons` as is
                {format: "qweapons-catalog", version, fields: [{weapon, field, key, type, unit, min, max, default, doc,
                 alt?, readOnly?}], weapons: [{id, name, title, kind, altFire, slot}], slots, builtins, keywords, types,
                 defaultText}
                + values: {key: value}      every field's value in the set (the standard set without `weapons`)
                + changed: [key]            fields whose value differs from the standard
                + writes: [key]             fields the set writes
                + labels: {weapon: label}   the set's `label` lines
                + name                      the set's name (null without `weapons`)
                + hasLogic, ok, errors: [{line, col, msg}], warnings: [...]
                + features: ["params", "check", "stats", "range", "fight"], ms
```

A set with errors still answers the catalog (exit 0) with the standard values, `ok: false` and the errors. The page
probes with `{}` and treats the helper as real when `fields` is there.

## `wpncheck`

```
{weapons (or text), ast?: true, canonical?: false}
  ->  {ok, errors: [{line, col, msg}], warnings: [{line, col, msg}], name?, hasLogic?, writes?: [key],
       ast?  (quark_tests weaponsast's JSON AST, when the text parses), canonical?  (printweapons' text), ms}
```

Always exit 0 with no `error` key (errors are the answer); the checks are the game's own (parse, types, field names,
clamping), so they are authoritative over the page's.

## `wpnstats`

```
{weapons?, compare?: [text], health?: 100, dist?: 0}
  ->  {name, rows: [row], gameRows: [...], compare: [{name, rows, gameRows}], health, dist, ms}
row = {id: "<weapon>:<mode>[:<variant>]", weapon, mode: "primary"|"alt", variant: ""|"direct"|"splash", kind, label,
       notes: [text], headshots, support?, beam?, heal?, refire, pellets, range, reach, speed, splashRadius, splashMax,
       body, head, burst, stkBody, stkHead, ttkBody, ttkHead, dpsBody, dpsHead,
       t10, t20, t40, fall10, fall20, fall40, at10, at20, at40, knock, wallbang, selfDmg, per: [{d, body, head}]}
```

The rows are the page's `stats.js` rows: the same 20 ids in the same order, the same columns and the same arithmetic
(test_wpn.py compares them number by number and note by note with node when node is there), worked out from the
game's compiled set: its values, the falloff fields or the set's `<weapon>.falloff` curve evaluated by the game
(`evalWeaponCurve`), shotgun pellets in the game's fixed pattern (`pelletOffset`) against the game's hit box and head
box at `dist`. `body` / `head` are one shot's damage at `dist` (0: every hit lands); `stk*` = ceil(health / damage);
`ttk*` = (shots - 1) x refire (+ the sticky's arm time); `dps*` = damage / refire; a projectile's `reach` (and `range`)
is how far it hits a target on its own floor: speed x life, or for one that drops, the farthest it lands from the eye
(1.6 m) at the best angle within its life (the rocket's lob: 85 m, not 168); `knock` the field, `wallbang` one shot's
body damage through a wallbang wall (hitscan, beam; else null), `selfDmg` your own damage at your blast's centre; `t10..40` flight times (0
hitscan, null support / out of reach); `fall10..40` the damage factor; `at10..40` body damage there; `per` the body
and head damage at 0..100 m (the damage-vs-distance chart). Infinite numbers (no damage) are null. Numbers carry 7
significant digits. `gameRows` is the game's own simpler table (`computeWeaponStats`: every pellet lands, no
distance), for reference. A set with errors: exit 1, `"weapons line L:C: msg"`.

## `wpnrange`

```
{weapons?, qmode?, weaponsAllowed?,
 map?: absent | "range" | a name | a path | {name, text} | {text|file|name|map},   (absent: the built-in range)
 weapon: "rifle", alt?: false,
 targets?: [{pos: [x,y,z], moving?: {axis: "x"|"z", speed: 5, amplitude: 3}, health?, crouch?}],
           (the built-in range without targets: its five lanes)
 shooter?: {pos: [0,0,0], aim: "perfect"|"body"|"head"|"bot", skill?: 0.7, yaw?},
 seconds?: 10, seed?: 1, order?: "lanes"|"nearest", health?, tickRate?: 1000, respawn?: 1.0,
 frameEvery?: 0.02, frames?: true, maxShots?: 20000,
 compare?: text | [text | {weapons, qmode?}]}
  ->  {ok, via: "wpnrange", weapon, alt, seconds, map, builtinMap, aim, skill?, order, tickRate, seed,
       weaponSet, weaponLogic, weaponsAllowed, qmode, weaponWarnings?,
       targets: [{index, pos, hits, headHits, damage, kills, firstKill, shots, dist, health, moving?}],
       shots: [{t, hit, head, dmg, target, dist}], shotsCut?,
       frames: [{t, players: [{pos, yaw, pitch, health, alive, shooter?, weapon?, target?, crouched?}],
                 projectiles: [{pos, stuck, type}], shots: [{from, to, hit}]}],
       frameEvery, totals: {shots, hits, headHits, damage, kills, dps, shotsHit, accuracy},
       notes: [text], perf: {wallSeconds, simSeconds, ticks, speedup},
       compare?: [the same answer for each compare set], ms}
```

- **The game's server.** BotSim starts a free for all (the map, the shooter and the targets as bots, the warmup,
  the forced start); the rules go on; the round's 3 s freeze is stepped through; then `seconds` of ticks at
  `tickRate` (the game's 1000 Hz by default) through the real `simulate()`: `fireWeapon`, the lag-compensated
  hitscan (view time = now: no rewind), projectiles, splash, `weaponHit` with the set's hit / kill slots,
  `damagePlayer`. The weapon pool is narrowed to the weapon under test.
- **Targets** are bot slots whose input comes from `BotBrain::script` (the game's scripted-player hook): no AI; they
  stand (or crouch) facing the shooter and are put on their path every tick (a moving one slides along x or z:
  `base + amplitude x sin(speed / amplitude x t)`, so `speed` is its top speed). A dead target comes back `respawn`
  s later at its spot with its `health` (a target's own, else the request's `health`, else the rules' health).
- **The shooter** is its slot turned into a player (`isBot = false`) whose commands are queued one per tick, as a
  client's arrive; so it gets the game's hit feedback, `EV_HIT` (victim, damage, "h" = a headshot), which is the
  `shots` list. It can't die or be pushed (health and spot reset every tick: a note says so). Fire: held, or every
  other tick for click weapons (semi-auto, a throw: each press a click, the game's 0.15 s click buffer fires it the
  moment it's ready). `alt`: the alt shot for weapons whose alt fires (rocket lob, knife throw, shotgun bounce,
  bubble stream, syringe throw), fire with alt held for the scope (rifle, sniper, crossbow) and the laser's pull.
  The sticky launcher always holds alt too: stickies go off the moment they arm.
- **aim** `perfect` / `body`: the hit box's centre; `head`: the head box's centre; projectiles lead the target along
  its known path and aim above it for the drop (flight time iterated). `bot`: the game's bot (`botThinkDirect`, skill
  0..1 -> BEGINNER..EXPERT, or a skill name) picks its target, aims (its reaction, tracking and error) and decides when
  to fire; its movement keys are dropped.
- **order** `lanes` (the default): the targets in turn, each killed once, then the next; `nearest`: always the
  nearest one alive.
- **Numbers.** Per target `damage` and `kills` / `firstKill` (s from the start) come from the targets' health (exact).
  Shots and hits are counted in one unit, so `accuracy` means the same for every weapon: a shot is what the game
  counts in `ServerPlayer::shotsFired` (a hitscan or projectile shot, one 1 ms tick of the laser's beam), except the
  knife's swing, which the game counts once per tick of its cut window (~120 a swing) and the range counts once, and
  the knife's throw and the bubble stream, which the game doesn't count at all: there a shot is a projectile launched
  (one knife, one bubble of the stream) and a hit a target that took damage that tick. `hits` per target = shots (swings) that took health off it; `shots` per target = shots fired while it was the
  target; `totals.shotsHit` = shots that hit anyone, `accuracy` = shotsHit / shots. `headHits` and `shots[]` (the
  per hit feed) come from EV_HIT, which batches the laser's ticks every 50 ms (a feed, not a count); misses are in
  `shots[]` for hitscan weapons only (a shot that damaged nobody that tick). Frames every
  `frameEvery` s: every player, the shooter's projectiles, the tick's hitscan traces (the aim ray, to the target hit
  or the first wall; at most 24 per frame).
- **Deterministic**: the same request gives the same answer (each run on a fresh thread; `seed` seeds the server).
- **Fast**: 10 s of the rifle on the range at 1000 Hz takes ~0.09 s; `compare` sets run at the same time, one thread
  each.

## `wpnrangemap`

`{}` -> `{text, lanes: [{dist, pos}], shooter}`: the built-in range as a `.qmap` (the page's
`mockrange.js builtinRangeMap`: a 40 x 106 m floor, the shooter at the origin facing -z, lanes at x -3, 3, -2, 6, 0
for 5, 10, 20, 40, 80 m).

## `wpnfight` = `sim` / `simbatch` with the rules keys

There is no `wpnfight` command: the page's Fights tab runs the bot editor's `sim` / `simbatch`
(tools/bot_editor/native/SIM.md) with `weapons`, `qmode` and `weaponsAllowed`, so its live replay is the bot
editor's. The start lines echo `weaponSet` (and the rest above), and both end lines carry
`weapons: [{weapon, kills, damage, shots, hits, accuracy, ttk, ttkSamples, deaths, heldSeconds, dps}]` (every bot
together; `simbatch`: every match). How those are counted: SIM.md "Weapon sets".

## Limits

- `wpnrange`'s per hit list is the game's hit feedback: batched every 50 ms for rapid small hits, so the laser's feed
  entries are batches (its `hits` count every tick), and a hit's `dist` is to the target's centre. Projectile misses aren't listed (only counted:
  `totals.shots - totals.shotsHit`).
- Moving targets slide (no legs): their speed is set every tick, so the game's friction never slows them.
- The range's targets are free for all players: a rocket's splash hurts every target near it (as in a match).
- `sim` / `simbatch` per weapon shots / hits / damage go to the weapon held at the previous sample (every frame /
  0.2 s): a quick switch can put a few shots on the other weapon. Kills are exact.
