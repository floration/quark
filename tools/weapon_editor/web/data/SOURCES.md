# Other games' weapon data: sources and confidence

`games.json` feeds the Weapons Editor's **Other games** tab. It was gathered on 2026-10-24. Every number comes
from the source named for that game or weapon. Where no source gave a value, the field is `null`. Nothing was
filled in from memory.

## Format notes (beyond the shape in DESIGN.md)

- `body` / `head` / `limb` are per hit in the game's own HP. For shotguns they are per pellet, with `pellets` set.
  A few entries give the whole shot with `pellets: 1`, and their notes say so (TF2 Scattergun and Shotgun, QC Super
  Shotgun).
- Random damage (Doom, Quake rockets, UT99 minigun) is stored as the **average**. The range is in `notes`.
- `fireInterval` is seconds between shots and `rpm = 60 / fireInterval`. For beams it is the damage tick (DPS =
  body / fireInterval). The Xonotic Arc beam is the exception: its `body` is already damage per second.
- `splash`: `{max, radius, min?}`, with radius in metres. `min` is the damage at the edge where the game defines one
  (Xonotic edgedamage, Warsow minimum splash, OW2 edge damage).
- `falloff` comes in several shapes, depending on what the source gives:
  - `{start, end, minDamage}`: Overwatch.
  - `{start, end, multiplier}`: THE FINALS, and Deadlock (multiplier 0.1 = 10% at the end).
  - `{brackets: [{from, to, head, body, leg}]}`: VALORANT.
  - `{rangeModifier, per}`: CS2, where damage is multiplied by `rangeModifier^(distance / per)`.
  - `{start, note}`: Quake Champions shotguns.
- Optional extra fields: `user` (the hero or class that uses the weapon) and `userHealth` (that hero's health).
  These are for hero games, where `health` at game level is only a representative target.
- Game-level `health` is `null` where no verified value was found (Quake Champions, Titanfall 2). The tab should
  skip shots-to-kill normalisation for those games.
- Game-level `units` explains each game's distance conversion.

## Distance conversions

| Engine family | Conversion used | Games |
|---|---|---|
| idTech / Quake units | 1 unit = 2.54 cm (the common "1 unit = 1 inch" convention) | Doom, Quake 1-3, QL, QC, Xonotic, Warsow |
| Source / Hammer units | 1 unit = 2.54 cm | CS2, Apex (the Apex wiki's own conversion: 18000 HU/s = 457 m/s) |
| Unreal Engine 1/2 | 1 uu = 1.905 cm (0.75 in, about 52.5 uu per metre) | UT99. UT2004 and UT3 speeds are not filled in. |
| TF2 rocket speed | 1100 HU/s, taken as about 21 m/s (the Fandom page says about 47 mph) | TF2 |
| Titanfall | the wiki's own conversion, 1500 units = 28.575 m (1 unit = 1.905 cm) | Titanfall 2 |
| Metres native | as given | OW2, VALORANT, THE FINALS, Deadlock |

Quake units are often argued to be closer to 3 cm (a 56-unit player is 1.7-1.8 m tall). The 1-inch convention was
kept so that all idTech games compare with each other. Splash radii and speeds carry this conversion; damage does
not.

## Per game

### Doom (1993): high confidence
- Source: id Software's released source, `linuxdoom-1.10`: `p_pspr.c` (hitscan damage and pellet counts),
  `info.c` (weapon state tics and missile speed/damage), `p_map.c` (missile damage = (1d8) x info damage,
  radius attack 128).
- Fire intervals are derived by adding the attack-state tics at 35 tics/s, with `A_ReFire` looping back. These
  match the commonly quoted figures: pistol 14, shotgun 37, SSG 57, chaingun 4 per bullet, rocket 20, plasma 3.
- The BFG's 40-tic cycle is also derived (20+10+10). Some references quote a longer cycle, so verify it if it
  matters.

### Quake: high confidence
- Source: QuakeC v1.01 (`id-Software/Quake-Tools/qcc/v101qc/weapons.qc`, `combat.qc`, `player.qc`, `client.qc`).
- Nailgun rate: 10 nails/s, from the 0.1 s alternating-barrel animation. `attack_finished` 0.2 is per barrel.
- Splash: `T_RadiusDamage` does damage minus half the distance, over findradius(damage + 40).

### Quake II: high confidence for damage, medium for fire rates
- Source: `id-Software/Quake-2/game/p_weapon.c`, `g_weapon.c`, `g_local.h`. Values are for deathmatch (single
  player differs and is noted).
- Fire intervals are derived from `Weapon_Generic` at 10 Hz: (FRAME_FIRE_LAST - FRAME_ACTIVATE_LAST + 1) x 0.1 s.
  The railgun comes out at 1.6 s. The Quake Wiki lists 38 shots/min (1.58 s), which agrees.

### Quake III Arena: high confidence
- Source: `id-Software/Quake-III-Arena` 1.32: `g_weapon.c` (hitscan damage), `g_missile.c` (projectile damage,
  splash, speed), `bg_pmove.c` (addTime fire intervals), `bg_public.h` (shotgun count 11, LG range 768),
  `g_client.c` (spawn health = max + 25).

### Quake Live: medium confidence
- Source: the quakelive-server-standards `factories/cvars.md` list of default cvars. QL is closed source.
- Missing: shotgun pellet count and HMG values. Starting health is assumed to be 100.

### Quake Champions: medium-low confidence
- Source: Church of Quake wiki weapon pages (updated 2025-11-14 to 2026-02-12), cross-checked against namu.wiki.
- The two sources conflict on:
  - Railgun: 80 (CoQ) vs 90 (namu).
  - Super Shotgun pellets: CoQ says "5 x 16" but lists 120 direct; namu says 30 x 4. Stored as 120 per shot.
  - Shotgun: 16 x 5 (CoQ) vs 20 x 4 (namu).
  - Nailgun: CoQ lists 15 x 10/s but a DPS of 120.
- Gauntlet rate is listed without units. Splash radii are not given.
- Champion health and armor are not verified, so `health` is null.

### Unreal Tournament (1999): medium-high confidence for damage
- Sources: UnCodeX Botpack docs (madrixis.de and the CMU mirror) for class defaults and code:
  - Enforcer 17, Shock beam 40, ball 55 (radius 70), combo Damage x 3 = 165 (radius 250).
  - Sniper 45 / 100 head; Minigun 9+Rand(6).
  - UTChunk 16 (x8), flak slug 70 (radius 150), RocketMk2 75 (radius 220, 900-1600 uu/s).
  - PlasmaSphere 20, PBolt 72/s, Razor2 30 (x3.5 head), Razor2Alt 34 (radius 180).
- Values are the unscaled defaults. **Hardcore** mode (common default) multiplies damage by 1.5. Liandri
  (https://liandri.beyondunreal.com/Shock_Rifle) and the BeyondUnreal wiki list Hardcore numbers (shock 60, chunk
  24).
- Fire rates are animation-driven, so the class docs give none. The shock and flak rates (1.4/s, combo 0.85/s, flak
  shell 1/s) are from Liandri and the BeyondUnreal wiki. Minigun 0.13 / 0.08 s is from the `Sleep()` calls.
- The other fire rates are from the Liandri Archives' Unreal Tournament sections (retrieved 2026-10-24): Enforcer
  2.8/s (alt 3.8/s), https://liandri.beyondunreal.com/Enforcer?oldid=9220 ; Ripper 2.9/s (alt 1.5/s),
  https://liandri.beyondunreal.com/Ripper ; and, through its mirror on unrealarchive.org, Sniper Rifle 0.6 s
  (https://unrealarchive.org/wikis/the-liandri-archives/Sniper_Rifle.html), Rocket Launcher 1.2/s (…/Rocket_Launcher.html)
  and Pulse Gun 6.1/s (…/Link_Gun.html). Liandri's damage figures there are the Hardcore ones (Enforcer 25, Sniper
  67 / 150, Ripper 45 / 157, Rocket 112, Pulse 30 = 1.5x the class defaults), so its rates are probably at
  Hardcore's 1.1x speed too; it doesn't say. Medium confidence.
- The Bio Rifle is left out: its damage scales with glob size and the formula wasn't confirmed.

### Unreal Tournament 2004 and UT3: medium confidence
- Source: Liandri Archives "Weapon comparison" (shots/s, min/max/avg damage per shot). Fire intervals are
  1 / shots-per-second.
- Doubtful: the UT3 Link Gun primary is listed at 45 per bolt; verify it. UT3 flak chunk count and damage aren't
  given (13-202 per shot), so its `body` is null.
- No projectile speeds or splash radii. Health 100 is assumed (standard).

### Xonotic: high confidence
- Source: `bal-wep-xonotic.cfg` and `balance-xonotic.cfg` on xonotic-data.pk3dir master (commit 2026-10-21).
  `arc.qc` confirms the Arc beam damage is per second (multiplied by frametime).
- Machine gun: mode 1 (default) is sustained 6 per 1/16 s; mode 0 has a 14-damage first shot.
- Mutator weapons (Mine Layer, Rifle, HLAC) are marked as such.

### Warsow / Warfork: high confidence
- Source: `gs_weapondefs.c` in Warfork/warfork-qfusion master. Qfusion/qfusion master has identical damages, with
  lower knockback on some weapons.
- `reload frametime` is used as the fire interval. Splash `min` is the "splash minimum damage" field. The Lasergun
  range is the 850-unit "timeout" for instant weapons.
- Health 100 is assumed (standard).

### Team Fortress 2: medium confidence
- The official TF2 Wiki (wiki.teamfortress.com) was **offline** for a PHP security fix when this was gathered. Its
  Wayback copies were blocked here.
- Numbers come from StrategyWiki's class pages (point-blank / mid / long ranges and crit values). Base damage is
  taken as crit / 3 wherever that agrees with the point-blank range.
- The Shotgun crit listed (120) does not match its range (86-90 point blank = 60 x 1.5), so base 60 comes from the
  range.
- StrategyWiki gives no fire intervals except melee (0.8 s, bat 0.5 s). The attack intervals now filled in are the
  official TF2 Wiki's "Damage and function times" values, known from that wiki before it went offline and not
  re-fetched (cited per weapon as wiki.teamfortress.com/wiki/<weapon>): Scattergun and Shotgun 0.625 s (10 pellets
  x 6), Pistol 0.17 s, Rocket Launcher 0.8 s, Grenade and Stickybomb Launcher 0.6 s, Minigun 4 bullets per 0.105 s,
  Syringe Gun and SMG 0.105 s, Sniper Rifle 1.5 s, Revolver 0.5 s.
- Cross-check, tf2console.fandom.com (the console wiki, community-tested, retrieved 2026-10-24): Shotgun 6 per
  pellet and 0.625 s, Sniper Rifle 1.5 s, Syringe Gun 0.1 s and about 990 HU/s, SMG about 10 bullets/s, Pistol about
  6 rounds/s: all agree. Its Revolver is 0.58 s (the console build); the PC value 0.5 s is kept.
- The Crusader's Crossbow's interval wasn't confirmed, so it stays null.
- Health: Scout 125, Medic 150, Demoman 175, Soldier 200 and Heavy 300 are from StrategyWiki. The rest are not
  verified.
- **Redo from the official wiki when it is back.**

### Overwatch 2: medium confidence
- Source: Overwatch Wiki (Fandom) hero pages and the Headshot page (2x; 2.5x for Widowmaker scoped).
- Balance changes often, so these are a snapshot of 2026-10-24.
- Doubtful: Soldier: 76 Helix Rockets "120 direct / 80 splash", and Tracer's DPS (page says 220; 12 x 20/s = 240).
  Pharah's health had conflicting patch lines on the page.

### VALORANT: high confidence
- Source: valorant-api.com (`/v1/weapons/<uuid>`), which serves the game's own weapon data. Version 13.08
  (build 2026-10-15).
- Brackets are full damage per range band, with no interpolation. Melee isn't in the data.

### Counter-Strike 2: high confidence
- Source: `scripts/weapons.vdata` in SteamDatabase/GameTracking-CS2, extracted from the game files.
- Fields used: `m_nDamage`, `m_flCycleTime` (first value = primary mode), `m_flHeadshotMultiplier`,
  `m_flRangeModifier`, `m_flRange`, `m_nNumBullets`, `m_flArmorRatio`.
- `head` = damage x multiplier at 0 m without a helmet. Stomach (x1.25) and leg (x0.75) multipliers live in code
  and are not included. The knife is in code and also left out.

### Apex Legends: medium-high confidence
- Source: Apex Legends Wiki (apexlegends.wiki.gg): the Weapons comparison table plus individual weapon pages for
  Longbow, Kraber, Wingman, Peacekeeper and EVA-8. Patches range up to 2025-10-07.
- `head` is the unhelmeted value.
- Doubtful: the Kraber page conflicts (25 RPM vs fire_rate 1.2/s), and the Peacekeeper's headshot per pellet is
  unclear.
- Not captured: Triple Take, 30-30, Bocek, Charge Rifle, Sentinel, Mastiff, Mozambique, P2020, RE-45 and Volt (the
  comparison table was cut off).

### THE FINALS: medium-high confidence
- Source: THE FINALS Wiki (thefinals.wiki) weapon pages, patches up to 12.0.0.
- Light health is 150 and Medium 250; Heavy isn't verified.
- The M60 page lists 600 RPM while its patch table ends at 590.

### Deadlock: high confidence (values move every patch)
- Source: deadlock.wiki hero pages (Haze, Vindicta, Abrams) and the Headshot page. Game updates up to 2026-09-16.
- Head damage is null because it depends on each hero's crit bonus scale.

### Titanfall 2: low confidence, partial
- Sources: Titanfall Wiki (Fandom) and titanfall.wiki.gg, retrieved 2026-10-24.
  - Fandom: R-201 (25 damage, 810 RPM, falloff 28.6-66.7 m), Kraber (100), C.A.R. (25 damage, 846 RPM actual,
    falloff 1000-3000 units = 19.05-57.15 m; wiki.gg agrees).
  - wiki.gg Titanfall 2 stat boxes: Alternator 35 / 600 RPM, Hemlok BF-R 33 / 930 RPM (3-round burst), Spitfire
    35 / 540 RPM.
- Most other pilot weapons' Titanfall 2 stat boxes only carry the menu's 0-100 bars (e.g. Flatline "damage 70,
  rate of fire 60"), which aren't damage or RPM, so they are left out. Pilot health isn't verified.

## Not covered (no reachable, citable numbers)

- **Halo Infinite**: Halopedia's BR75 numbers don't reconcile.
- **Splitgate**: only the Pistol and SMG on Fandom; the official developer spreadsheet (Google Sheets) wasn't
  reachable.
- **Diabotical**: no stats page found.
- **Doom Eternal Battlemode**: not attempted beyond a search.
- **Rainbow Six Siege** and **Marvel Rivals**: Fandom infoboxes didn't render and other wikis were blocked.
- **Fortnite**: the weapon pool rotates every season.
- **Call of Duty** and **Battlefield**: no open data source found.

Good next sources, if network access allows: the official TF2 wiki (once online), sym.gg (Battlefield), siege.gg or
the R6 Fandom raw wikitext (Siege), and Liquipedia's arenafps wiki (QC, QL, Diabotical). Liquipedia and the
Wayback Machine were blocked from this environment.
