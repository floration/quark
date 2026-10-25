# Other games' movement values

Reference data for the physics editor's Values tab ("compare with games"): other games' default player movement,
converted to Quark's units and settings. `serve.py` serves this folder at `/references/`; `web/js/refs.js` reads it.

- `index.json`: the games, grouped (Quake family, Source family, arena shooters, hero/tactical shooters, movement
  shooters, mainstream shooters, other), each `{id, name, year, engine, file, values, distance?, distanceKeys?}`.
- `<game-id>.json`: one game.
- `check.py`: the checker (python3 stdlib). Run it after any change:
  `python3 tools/physics_editor/references/check.py [build/quark_maptool | physparams.json]`.

## A game file

```
{id, name, year, engine, group,
 scale:  {unit, metresPerUnit, why},
 values: {<quark key>: {value, unit, mult, source, confidence, raw?, note?, outsideClamp?},
          dash | grapple: {value: true|false, source, confidence, note}},
 facts:  {<name>: {value, unit, source, confidence, note?}},
 extras: ["mechanics Quark doesn't have, or has differently"],
 notes, sources: [{title, url}]}
```

- `values` keys are Quark's settings exactly as `quark_maptool physparams` lists them (`gravity`, `speed`, `jump`
  and the `move` keys: `ground_accel`, `friction`, `stop_speed`, `air_accel`, `air_cap`, `overstrafe`, `step`,
  `ladder_speed`, `ladder_side`, `dash_*`, `hook_*`, `super_dash_jump`...). A game only lists what it has an analogue
  for; everything else stays at Quark's standard when the page applies a game.
- `value` is in Quark's unit for that key (m, m/s, m/s2, s, or the unitless factor); `mult` = value / Quark's base,
  so it can be typed straight into a custom mode. `raw` is the game's own number. `outsideClamp: true` marks a
  multiplier outside the editor's range (gravity 0.1..3, speed and jump 0.25..3, move keys 0.1..5): applying it clamps.
- `dash` / `grapple` say whether the game has a dash / grapple in standard play, so "load all" can switch Quark's off.
- `facts` are comparisons with no Quark setting: `jumpHeight` (Quark 1.22 m), `timeTo90` / `stopDistance` /
  `stopTime` (Quark 0.072 s / 1.21 m / 0.364 s), walk / sprint / crouch speeds, ability distances.

## How values are converted

**Scale.** Engine units become metres by the game's own player height, so every game is compared as if its player
were Quark's 1.8 m: Quake 56 u (0.03214 m/u), Warsow/Warfork 64 u, Xonotic 69 u, GoldSrc / Source / Titanfall 72 u
(0.025 m/u; Apex assumed the same), TF2 82 u, UT99 78 UU, Halo CE 0.70127 WU, Celeste 11 px, UT2004 / UT3 88 UU,
Halo 3 / Reach a 7 ft Spartan, UT4 its 216 UU capsule (UE4 cm), Diabotical its 57 u hitbox. Games that document metres (Overwatch, Valorant, Minecraft, whose player is 1.8 m, DOOM
Eternal, Fortnite, Quake Champions at its own 40 u = 1 m) are used as given; Call of Duty 4 at 1 u = 1 inch; Quake Live (VQL and PQL) and
Reflex at Quake's 56 u and Portal 2 at Source's 72 u (assumed). Each file's `scale.why` says which.
Speeds, lengths and accelerations scale; time and unitless factors don't.

**Ground.** Quake, Source, Warsow, Xonotic and Quark share one ground model: friction removes
`max(speed, stop_speed) x friction x dt`, then acceleration adds `accel x wish speed x dt` up to the wish speed. So
`ground_accel` and `friction` are the game's cvars as they are (e.g. Quake 3: pm_accelerate 10, pm_friction 6), and
`stop_speed` is its stop speed scaled.

**Air.** Two shapes exist (plus Unreal's, below):
- Quark, Quake 2, Quake 3 (VQ3), CPM and Warsow push `accel x min(wish, cap) x dt` toward the cap. Quark multiplies
  `air_accel` by 10, so a game's air accelerate maps to `air_accel = accel / 10` and its cap to `air_cap`
  (CPM's strafe accel 70 toward 30 u/s = air_accel 7.0, air_cap 0.96 m/s; VQ3's accel 1 toward 320 u/s = 0.1 and
  10.3 m/s).
- Quake 1 / QuakeWorld, GoldSrc and Source push `accel x FULL wish speed x dt` but stop gaining at a 30 u/s cap. The
  same push in Quark's shape is `air_accel = accel x wish / 30 / 10` with `air_cap` = 30 u/s scaled (Half-Life:
  10 x 320 / 30 / 10 = 10.67). This is marked `estimated`: it matches the push, not every detail. Xonotic's own
  Quake port makes the same conversion (sv_airaccelerate 106.67 with sv_maxairspeed 30).
- Unreal (UT99, UT2004, UT3) adds a fixed `AccelRate` (UU/s2) up to the run speed, and `AirControl x AccelRate` in the air.
  The same push in Quark's shape is `ground_accel = AccelRate / GroundSpeed` and `air_accel = AirControl x AccelRate /
  AirSpeed / 10` toward `air_cap = AirSpeed` (estimated: Unreal's native physics code isn't public).
- `overstrafe` 1 for the Quake / Source games: their push against your momentum applies in full (Quark lets 35%
  through).

**Jump.** `jump` is the take-off speed (Quark 7.34 m/s); the jump height goes in `facts.jumpHeight` (v^2/2g when the
game gives speed and gravity). When a game gives only a jump height and gravity, `jump` = sqrt(2gh) (DOOM Eternal,
Call of Duty 4, Overwatch); when only a measured height h and airtime T, gravity = 8h/T^2 and `jump` = 4h/T (Apex, Halo 3,
Halo: Reach). All of these are `estimated`.

## Confidence

- `exact`: read from the game's source code (or a public script dump / reverse-engineered rewrite), a shipped config
  file, a cvar default, or the developer's own published numbers (patch notes); then converted by the stated scale.
  Source files are cited by URL and line.
- `measured`: a community wiki or measurement, not the code (also a player's cvar dump, e.g. Quake Live's).
- `estimated`: derived here: an equivalence between two different formulas (the air rows above, Minecraft's
  per-tick friction), a v^2/2g, a simulation (`timeTo90`, `stopDistance`), an assumed scale, or a mapping of a
  mechanic onto a Quark setting that works differently (a dodge onto `dash_speed`).

No number here is invented: what couldn't be sourced is left out.

## Estimates (second pass)

Mostly estimated or measured, not read from code: Quake Live (a player's cvarlist dump; stop speed assumed from Quake III),
Quake Champions (published base speeds and air accel; the air formula assumed Quake III's), Reflex (run speed at an
assumed Quake scale; the third pass adds its ruleset dump), Halo 3 / Halo: Reach (one player's in-game measurements: gravity and jump derived from height and
airtime, rescaled from a 7 ft Spartan), Call of Duty 4 (run speed measured, jump speed from jump_height), Fortnite and
Portal 2 (speeds only), Apex's gravity / jump (from the measured jump), Overwatch's gravity (the Workshop community's
counter-gravity figure) and jump, and every Unreal ground / air acceleration. UT99's gravity / JumpZ / step, UT2004, UT3
(from UDK's copy of its scripts) and DOOM Eternal (cvar defaults) are exact apart from the formula equivalences.
`cs16-comp` is CS 1.6 as servers run it (sv_accelerate 5, sv_stopspeed 75), next to `cs16`'s engine defaults.

## Unofficial sources (third pass)

Forum posts and wikis had few numbers, but community repositories of game files and config dumps did. All found through
GitHub code search:

- **UT4** (`ut4`): `UUTCharacterMovement`'s constructor defaults and `DefaultEngine.ini`'s DefaultGravityZ -2154 from a public
  mirror of Epic's source (JimmieKJ/unrealTournament): run 940, jump 730, accel 4000, air control 0.45, step 51, dodge
  1500 / 500, sprint 1230 UU/s. `exact` apart from the Unreal acceleration equivalences.
- **Diabotical** (`diabotical`): the shipped `scripts/base.games` kept by a community patch tracker (ScheduleTracker/
  DiaboticalTracker): gravity 800, jump 275, speed 320, accel 10, friction 6, air accel 1 toward 320, 125 Hz, VQL-like.
  The Rogue tracker's UI copy has the same block. The formulas are assumed Quake III's (`estimated` where it matters).
- **PQL** (`quakelive-pql`): the hard-coded bundle `pmove_AirControl 1` switches in (walk accel 15, strafe accel 70
  toward 35, air accel 1.1, air control 150, air stop 2.5, circle-strafe friction 5.5), read from Kr3m/QuakeLive (a
  reconstruction of the retail pmove from the binaries) and matched by a player's saved PQL config (Danmer/ql-configs).
- **Reflex** (`reflex`, added to): the Competitive ruleset's gconst_* values from a community ruleset-comparison tool
  (Kyt0z/reflex-ruleset-comparer): gravity 750, jump 255 (+105 double), accel 4000, friction 6, bunny accel 60 toward 30,
  air control 150. `measured` (a dump), the mappings `estimated`.
- **Halo: Reach** (cross-check facts only): a Forge editor that reads Reach's tags (Sopitive/HaloMapStudio) gives run
  2.2 wu/s, jump 2.3 wu/s and gravity 4.17 wu/s2; at 1 wu = 10 ft they land within 2% (speed) and 5% (jump height) of the
  bungie.net measurements already used, so the existing values stay.

## Not covered (searched, no usable numbers)

Where the third pass looked: GitHub code search (config dumps, cvar / dvar lists, tag and script dumps, source
mirrors), Steam community discussions and guides, the Splitgate, Halopedia and Liquipedia wikis, ESReality and
bungie.net. Reddit and its archives (pullpush, arctic-shift) and Fandom were not reachable from this environment, so
Reddit threads weren't read.

- DOOM (2016): only stopwatch timings that disagree (230 vs 440 u/s, unknown units); the DOOMLegacyMod command list
  has names but no defaults.
- Splitgate (1 and 2, and Arena Reloaded): no movement numbers in its wikis, patch notes or Steam threads; no config dump found.
- Halo Infinite, Halo 2, 4, 5: no measured speeds or jumps found (the H2EK stats page repeats Halo CE's; the tag-reading
  tools that support Halo 3 / 4 / H2A read the numbers at run time and don't publish them).
- Modern Call of Duty (MW2019, MWII, BO6): only TrueGameData sprint speeds; the community dvar lists are hashed names
  without defaults, so no base speed, gravity or jump.
- Valorant's jump and gravity; Quake Champions' gravity, jump and ground acceleration (one Steam post recalls a 500-600
  u/s cap, nothing else).

To add one: add it to `build.py` with each number's source, run `python3 build.py` then `check.py`.
