#!/usr/bin/env python3
"""Builds tools/weapon_editor/web/data/games.json from hand-checked, sourced values.
Every number below comes from the source named next to it (see SOURCES.md)."""
import json, sys

INCH = 0.0254          # idTech / Source "1 unit = 1 inch" convention
UU = 0.01905           # Unreal Engine 1/2: 1 uu = 0.75 in (~52.5 uu per metre)

def r(x, n=3):
    return None if x is None else round(x, n)

def W(name, cls, body, head=None, limb=None, splash=None, pellets=1, fi=None, rpm=None,
      ps=None, rng=None, falloff=None, alt=None, notes="", source=None, **extra):
    if fi is not None and rpm is None:
        rpm = round(60.0 / fi, 1)
    if rpm is not None and fi is None:
        fi = round(60.0 / rpm, 4)
    d = {"name": name, "class": cls, "body": body, "head": head, "limb": limb,
         "splash": splash, "pellets": pellets, "fireInterval": r(fi, 4), "rpm": rpm,
         "projectileSpeed": r(ps, 2), "range": r(rng, 2), "falloff": falloff, "alt": alt,
         "notes": notes, "source": source}
    d.update(extra)
    return d

def S(maxd, radius_m, mind=None):
    s = {"max": maxd, "radius": r(radius_m, 3)}
    if mind is not None:
        s["min"] = mind
    return s

games = []

# ---------------------------------------------------------------- Doom (1993)
src = "https://github.com/id-Software/DOOM/tree/master/linuxdoom-1.10"
pspr = "https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_pspr.c"
info = "https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/info.c"
T = 1 / 35.0
games.append({"id": "doom", "name": "Doom (1993) deathmatch", "health": 100,
 "armor": "Green and blue armor pickups (values not taken from the cited source).",
 "healthNote": "Player spawn health 100 (mobjinfo spawnhealth).",
 "units": "Map units, 1 unit = 2.54 cm by the same convention used for Quake (Doom's real proportions are looser). Weapon timing in tics, 35 tics per second; fire intervals are the attack-state tics with A_ReFire looping back.",
 "source": src, "patch": "linuxdoom 1.10 source (v1.9 gameplay)",
 "weapons": [
  W("Fist", "melee", 11, fi=17*T, rng=64*INCH, notes="2-20 per punch (2*(1d10)), average 11; Berserk multiplies by 10. Interval 4+4+5+4 tics.", source=pspr),
  W("Chainsaw", "melee", 11, fi=4*T, rng=65*INCH, notes="2-20 per tick (2*(1d10)), average 11; a hit every 4 tics.", source=pspr),
  W("Pistol", "pistol", 10, fi=14*T, rng=2048*INCH, notes="5-15 per bullet (5*(1d3)), average 10; first shot perfectly accurate, refire spreads. 14 tics per shot.", source=pspr),
  W("Shotgun", "shotgun", 10, pellets=7, fi=37*T, rng=2048*INCH, notes="7 pellets of 5-15 (average 10 each). 37 tics per shot.", source=pspr),
  W("Super Shotgun", "shotgun", 10, pellets=20, fi=57*T, rng=2048*INCH, notes="20 pellets of 5-15 (average 10 each), wide spread. 57 tics per shot (Doom II).", source=pspr),
  W("Chaingun", "auto-rifle", 10, fi=4*T, rng=2048*INCH, notes="5-15 per bullet, average 10; 4 tics per bullet (8.75/s).", source=pspr),
  W("Rocket Launcher", "rocket", 90, splash=S(128, 128*INCH), fi=20*T, ps=20*35*INCH, notes="Direct 20-160 (20*(1d8)), average 90, plus radius damage 128 at the centre falling 1 per unit to 0 at 128 units. Speed 20 units/tic = 700 u/s. 20 tics per shot.", source=info),
  W("Plasma Rifle", "plasma/projectile-auto", 22.5, fi=3*T, ps=25*35*INCH, notes="5-40 per bolt (5*(1d8)), average 22.5; 3 tics per bolt (11.7/s); 25 units/tic = 875 u/s.", source=info),
  W("BFG9000", "other", 450, fi=40*T, ps=25*35*INCH, notes="Ball 100-800 (100*(1d8)), average 450, then 40 tracers of 15-120 each (15 rolls of 1d8, average 67.5). Fire cycle 20+10+10 tics derived from the weapon states (the A_ReFire state is skipped while held).", source=info),
 ]})

# ---------------------------------------------------------------- Quake
qc = "https://github.com/id-Software/Quake-Tools/blob/master/qcc/v101qc/weapons.qc"
games.append({"id": "q1", "name": "Quake", "health": 100,
 "armor": "Green / yellow / red armor pickups (values not taken from the cited source).",
 "healthNote": "Spawn health 100 (client.qc).",
 "units": "Quake units, 1 unit = 2.54 cm (1 inch) convention.",
 "source": qc, "patch": "QuakeC v1.01 source (id-Software/Quake-Tools)",
 "weapons": [
  W("Axe", "melee", 20, fi=0.5, rng=64*INCH, notes="Trace 64 units.", source=qc),
  W("Shotgun", "shotgun", 4, pellets=6, fi=0.5, notes="6 pellets x 4, spread 0.04.", source=qc),
  W("Super Shotgun", "shotgun", 4, pellets=14, fi=0.7, notes="14 pellets x 4, spread 0.14 x 0.08; 2 shells.", source=qc),
  W("Nailgun", "plasma/projectile-auto", 9, fi=0.1, ps=1000*INCH, notes="9 per nail; two barrels alternate each 0.1 s animation frame (10 nails/s); 1000 u/s.", source=qc),
  W("Super Nailgun", "plasma/projectile-auto", 18, fi=0.1, ps=1000*INCH, notes="18 per nail, 10/s, 2 nails of ammo each.", source=qc),
  W("Grenade Launcher", "grenade", 120, splash=S(120, 160*INCH), fi=0.6, ps=600*INCH, notes="Explodes on contact with a player or after 2.5 s; radius damage 120 minus half the distance (findradius 160 units). Launched at 600 u/s plus 200 up.", source=qc),
  W("Rocket Launcher", "rocket", 110, splash=S(120, 160*INCH), fi=0.8, ps=1000*INCH, notes="Direct 100-120 (100+random*20), average 110; the target hit gets no extra splash. Splash 120 minus half the distance, radius 160 units.", source=qc),
  W("Thunderbolt", "beam", 30, fi=0.1, rng=600*INCH, notes="30 per 0.1 s (300 DPS), trace 600 units; discharges all cells (35 x cells radius damage) in water.", source=qc),
 ]})

q2 = "https://github.com/id-Software/Quake-2/blob/master/game/p_weapon.c"
games.append({"id": "q2", "name": "Quake II", "health": 100,
 "armor": "Jacket / combat / body armor and power armor (values not taken from the cited source).",
 "healthNote": "Spawn health 100 (p_client.c).",
 "units": "Quake units, 1 unit = 2.54 cm. Server runs 10 frames/s; fire interval = (FRAME_FIRE_LAST - FRAME_ACTIVATE_LAST + 1) x 0.1 s from Weapon_Generic, deathmatch values.",
 "source": q2, "patch": "3.21 source (id-Software/Quake-2)",
 "weapons": [
  W("Blaster", "plasma/projectile-auto", 15, fi=0.5, ps=1000*INCH, notes="15 in deathmatch (10 single player).", source=q2),
  W("Shotgun", "shotgun", 4, pellets=12, fi=1.2, notes="12 pellets x 4.", source=q2),
  W("Super Shotgun", "shotgun", 6, pellets=20, fi=1.2, notes="20 pellets x 6 (two barrels of 10).", source=q2),
  W("Machinegun", "auto-rifle", 8, fi=0.1, notes="8 per bullet, 10/s, climbing recoil.", source=q2),
  W("Chaingun", "lmg", 6, fi=1/30, notes="6 per bullet in deathmatch (8 SP); spins up from 1 to 2 to 3 bullets per 0.1 s frame (30/s at full spin).", source=q2),
  W("Grenade Launcher", "grenade", 120, splash=S(120, 160*INCH), fi=1.2, ps=600*INCH, notes="120 damage, radius damage+40 = 160 units, 2.5 s fuse.", source=q2),
  W("Hand Grenade", "grenade", 125, splash=S(125, 165*INCH), fi=None, ps=None, notes="125, radius 165 units; thrown at 400-800 u/s by hold time, 3 s fuse.", source=q2),
  W("Rocket Launcher", "rocket", 110, splash=S(120, 120*INCH), fi=0.9, ps=650*INCH, notes="Direct 100-120 (average 110) plus 120 radius damage over 120 units (1-sqrt(dist/radius) falloff).", source=q2),
  W("HyperBlaster", "plasma/projectile-auto", 15, fi=0.1, ps=1000*INCH, notes="15 per bolt in deathmatch (20 SP), 10/s.", source=q2),
  W("Railgun", "railgun", 100, fi=1.6, notes="100 in deathmatch (150 SP), pierces. Community wikis list ~38 shots/min.", source=q2),
  W("BFG10K", "other", 200, fi=2.5, ps=400*INCH, notes="Ball 200 in deathmatch (500 SP), lasers hit targets within 256 units of the ball; explosion radius 1000 units. Fires on frame 17 after a 0.8 s wind-up.", source=q2),
 ]})

q3w = "https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/g_weapon.c"
q3m = "https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/g_missile.c"
q3p = "https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_pmove.c"
games.append({"id": "q3a", "name": "Quake III Arena", "health": 100,
 "armor": "Yellow / red armor and shards (values not taken from the cited source).",
 "healthNote": "Spawn health is max health + 25 (125), counting down to 100 (g_client.c).",
 "units": "Quake units, 1 unit = 2.54 cm. Fire intervals from PM_Weapon addTime (bg_pmove.c), speeds/splash from g_missile.c.",
 "source": q3w, "patch": "1.32 source (id-Software/Quake-III-Arena)",
 "weapons": [
  W("Gauntlet", "melee", 50, fi=0.4, notes="50 per hit.", source=q3w),
  W("Machinegun", "auto-rifle", 7, fi=0.1, notes="7 per bullet (5 in team modes), spread 200.", source=q3w),
  W("Shotgun", "shotgun", 10, pellets=11, fi=1.0, notes="11 pellets x 10, spread 700.", source=q3w),
  W("Grenade Launcher", "grenade", 100, splash=S(100, 150*INCH), fi=0.8, ps=700*INCH, notes="Bounces, 2.5 s fuse.", source=q3m),
  W("Rocket Launcher", "rocket", 100, splash=S(100, 120*INCH), fi=0.8, ps=900*INCH, source=q3m),
  W("Lightning Gun", "beam", 8, fi=0.05, rng=768*INCH, notes="8 per 50 ms (160 DPS), range 768 units.", source=q3w),
  W("Railgun", "railgun", 100, fi=1.5, notes="Pierces up to 4 targets.", source=q3w),
  W("Plasma Gun", "plasma/projectile-auto", 20, splash=S(15, 20*INCH), fi=0.1, ps=2000*INCH, source=q3m),
  W("BFG10K", "other", 100, splash=S(100, 120*INCH), fi=0.2, ps=2000*INCH, source=q3m),
 ]})

qlsrc = "https://github.com/quakelive-server-standards/quakelive-server-standards/blob/master/factories/cvars.md"
games.append({"id": "ql", "name": "Quake Live", "health": 100,
 "armor": "Armor pickups as in Quake III; the absorb split depends on the ruleset (not taken from the cited source).",
 "healthNote": "Standard 100 (not restated by the cited cvar list; QL factories can change starting health).",
 "units": "Quake units, 1 unit = 2.54 cm. Values are the default cvars (g_damage_*, g_splash*, g_velocity_*, weapon_reload_* in ms).",
 "source": qlsrc, "patch": "Steam-era default cvars (community cvar reference)",
 "weapons": [
  W("Gauntlet", "melee", 50, fi=0.4, source=qlsrc),
  W("Machinegun", "auto-rifle", 5, fi=0.1, notes="g_damage_mg 5 (g_damage_mg_tdm 4).", source=qlsrc),
  W("Shotgun", "shotgun", 5, pellets=None, fi=1.0, notes="5 per pellet (inner and outer ring); pellet count not in the cited list.", source=qlsrc),
  W("Grenade Launcher", "grenade", 100, splash=S(100, 150*INCH), fi=0.8, ps=700*INCH, source=qlsrc),
  W("Rocket Launcher", "rocket", 100, splash=S(84, 120*INCH), fi=0.8, ps=1000*INCH, notes="Splash 84 (lower than Q3's 100).", source=qlsrc),
  W("Lightning Gun", "beam", 7, fi=0.05, notes="7 per 50 ms (140 DPS), g_damage_lg_falloff 1.", source=qlsrc),
  W("Railgun", "railgun", 80, fi=1.5, source=qlsrc),
  W("Plasma Gun", "plasma/projectile-auto", 20, splash=S(15, 20*INCH), fi=0.1, ps=2000*INCH, source=qlsrc),
  W("BFG", "other", 100, splash=S(100, 80*INCH), fi=0.3, ps=1800*INCH, source=qlsrc),
  W("Chaingun", "lmg", 8, fi=0.05, source=qlsrc),
  W("Nailgun", "shotgun", 12, pellets=10, fi=1.0, notes="g_nailcount 10 nails per shot (projectiles).", source=qlsrc),
  W("Proximity Mine Launcher", "grenade", 0, splash=S(100, 150*INCH), fi=0.8, notes="No impact damage; 100 splash.", source=qlsrc),
 ]})

coq = "https://churchofquake.com/wiki/weapons/"
games.append({"id": "qc", "name": "Quake Champions", "health": None,
 "armor": "Champion-dependent health and armor (light / medium / heavy); values not verified here.",
 "healthNote": "No verified base health: normalise per champion by hand.",
 "units": "Quake units (u, ups), 1 unit = 2.54 cm.",
 "source": coq, "patch": "Church of Quake wiki pages updated 2025-11-14 to 2026-02-12",
 "weapons": [
  W("Gauntlet", "melee", 75, rng=30*INCH, notes="75 direct, 500 ms cooldown on hit; 'rate of fire 25' without units (page updated 2019).", source="https://churchofquake.com/wiki/gauntlet/"),
  W("Machinegun", "auto-rifle", 6, fi=0.1, rng=6000*INCH, source="https://churchofquake.com/wiki/machinegun/"),
  W("Heavy Machinegun", "auto-rifle", 9, fi=0.1, rng=6000*INCH,
    alt=W("Heavy Machinegun (zoomed)", "auto-rifle", 15, fi=0.15, rng=6000*INCH, source="https://churchofquake.com/wiki/heavy-machinegun/"),
    source="https://churchofquake.com/wiki/heavy-machinegun/"),
  W("Shotgun", "shotgun", 5, pellets=16, fi=0.85, rng=2000*INCH, falloff={"start": r(768*INCH), "note": "pellet damage -1 every 256 u past 768 u; +10 range bonus inside 128 u"}, source="https://churchofquake.com/wiki/shotgun/"),
  W("Super Shotgun", "shotgun", 120, pellets=1, fi=1.0, rng=2000*INCH, falloff={"start": r(768*INCH), "note": "+40 bonus before 200 u, none after 400 u"}, notes="120 total per shot as listed (body = whole shot); the page's '5 x 16 pellets' does not add up to 120 and namu.wiki lists 30 x 4.", source="https://churchofquake.com/wiki/super-shotgun/"),
  W("Nailgun", "plasma/projectile-auto", 15, splash=S(6, None), fi=0.1, ps=1800*INCH, notes="Page lists DPS 120 (15 x 10/s = 150).", source="https://churchofquake.com/wiki/nailgun/"),
  W("Super Nailgun", "plasma/projectile-auto", 20, splash=S(15, None), fi=0.1, ps=2000*INCH, source="https://churchofquake.com/wiki/super-nailgun/"),
  W("Tri-bolt", "grenade", 35, pellets=3, splash=S(35, None, 10), fi=1.3, ps=1600*INCH, notes="3 bolts x 35 direct, splash 10-35 per bolt, radius not listed.", source="https://churchofquake.com/wiki/tri-bolt/"),
  W("Rocket Launcher", "rocket", 100, splash=S(100, None, 10), fi=0.8, ps=1000*INCH, notes="Splash 10-100, radius not listed.", source="https://churchofquake.com/wiki/rocket-launcher/"),
  W("Lightning Gun", "beam", 6, fi=0.044, rng=768*INCH, notes="6 per 44 ms (page DPS 136).", source="https://churchofquake.com/wiki/lightning-gun/"),
  W("Railgun", "railgun", 80, fi=1.5, notes="Church of Quake (2026-02-12) lists 80; namu.wiki lists 90 regardless of zoom.", source="https://churchofquake.com/wiki/railgun/"),
 ]})

# ---------------------------------------------------------------- Unreal Tournament
ut99d = "https://project-archives1.etc.cmu.edu/2001/fall/coyote210/Docs/undox/"
ut99s = "https://www.madrixis.de/undox/Source_botpack/"
lia99 = "https://liandri.beyondunreal.com/"                            # fire rates (Hardcore-mode damage there)
ula = "https://unrealarchive.org/wikis/the-liandri-archives/"          # the same archive, mirrored
games.append({"id": "ut99", "name": "Unreal Tournament (1999)", "health": 100,
 "armor": "Thigh pads, body armor, shield belt (values not taken from the cited source).",
 "healthNote": "Standard 100 (not restated by the cited class docs). Hardcore mode (on by default in most setups) multiplies all damage by 1.5 and runs the game 10% faster; values here are the unscaled class defaults.",
 "units": "Unreal units, 1 uu = 1.905 cm (0.75 in, ~52.5 uu per metre).",
 "source": "https://www.madrixis.de/undox/botpack/", "patch": "Botpack class defaults (UnCodeX docs of 4xx-era Botpack)",
 "weapons": [
  W("Enforcer", "pistol", 17, fi=1/2.8, alt=W("Enforcer (alt)", "pistol", 17, fi=1/3.8, notes="Sideways rapid fire, less accurate; 3.8 shots/s per Liandri.", source=lia99 + "Enforcer?oldid=9220"), notes="HitDamage 17. Fire rate is animation-driven; 2.8 shots/s per Liandri (which lists 25 damage, the Hardcore 1.5x).", source=ut99s + "enforcer.html"),
  W("Shock Rifle (beam)", "hitscan-rifle", 40, fi=1/1.4,
    alt=W("Shock Rifle (ball)", "plasma/projectile-auto", 55, splash=S(55, 70*UU), ps=1000*UU, notes="Ball: HurtRadius(Damage, 70). Combo (beam into ball): Damage*3 = 165 over 250 uu (247 in Hardcore).", source=ut99s + "shockproj.html"),
    notes="HitDamage 40 (60 Hardcore). Liandri lists 1.4 shots/s.", source=ut99s + "shockrifle.html"),
  W("Shock Combo", "other", 165, splash=S(165, 250*UU), fi=1/0.85, notes="ShockProj SuperExplosion: Damage*3 over 250 uu; liandri lists 0.85/s.", source=ut99s + "shockproj.html"),
  W("Sniper Rifle", "sniper", 45, head=100, fi=0.6, notes="Fixed 45 body / 100 head, no random range. Fire rate animation-driven; Liandri lists 0.6 s (and 67 / 150 head, the Hardcore numbers): " + ula + "Sniper_Rifle.html.", source=ut99s + "sniperrifle.html"),
  W("Minigun", "lmg", 11.5, fi=0.13, alt=W("Minigun (alt)", "lmg", 11.5, fi=0.08, notes="Faster, less accurate.", source=ut99s + "minigun2.html"), notes="9-14 per bullet (9+Rand(6)), average 11.5; Sleep(0.13) between shots.", source=ut99s + "minigun2.html"),
  W("Flak Cannon", "shotgun", 16, pellets=8, fi=1/1.4, ps=2500*UU,
    alt=W("Flak Cannon (shell)", "grenade", 70, splash=S(70, 150*UU), fi=1.0, ps=1200*UU, notes="Slug 70 radius damage over 150 uu, then 5 chunks.", source=ut99d + "Botpack.flakslug.html"),
    notes="8 UTChunks x 16 (24 in Hardcore), 2500-2700 uu/s; 1.4 shots/s per BeyondUnreal wiki.", source=ut99d + "Botpack.UTChunk.html"),
  W("Rocket Launcher", "rocket", 75, splash=S(75, 220*UU), fi=1/1.2, ps=900*UU, notes="RocketMk2 75 over 220 uu, 900 uu/s accelerating to 1600; loads up to 6. 1.2 rockets/s per Liandri (" + ula + "Rocket_Launcher.html; its 112 damage is Hardcore).", source=ut99d + "Botpack.RocketMk2.html"),
  W("Pulse Gun", "plasma/projectile-auto", 20, fi=1/6.1, ps=1450*UU, alt=W("Pulse Gun (beam)", "beam", 72, notes="PBolt: 72 damage per second (applied every 0.22 s of contact); BeamSize 81 uu per segment.", source="https://www.madrixis.de/undox/botpack/pbolt.html"), notes="PlasmaSphere 20; 6.1 shots/s per Liandri (" + ula + "Link_Gun.html, the UT section; its 30 damage is Hardcore).", source=ut99d + "Botpack.PlasmaSphere.html"),
  W("Ripper", "other", 30, head=105, fi=1/2.9, ps=1300*UU, alt=W("Ripper (explosive blade)", "grenade", 34, splash=S(34, 180*UU), fi=1/1.5, notes="1.5 shots/s per Liandri.", source=ut99d + "Botpack.Razor2Alt.html"), notes="Razor2 30; head hits 3.5x (105) instead. 2.9 shots/s per Liandri (" + lia99 + "Ripper; its 45 / 157 are Hardcore).", source=ut99d + "Botpack.Razor2.html"),
 ]})

lia = "https://liandri.beyondunreal.com/Weapon_comparison"
def sps(x):
    return 1.0 / x
games.append({"id": "ut2004", "name": "Unreal Tournament 2004", "health": 100,
 "armor": "Shield packs (values not taken from the cited source).",
 "healthNote": "Standard 100 (not restated by the cited page).",
 "units": "Fire rates converted from the liandri table's shots per second.",
 "source": lia, "patch": "3369 era (Liandri Archives weapon comparison)",
 "weapons": [
  W("Shield Gun", "melee", 40, fi=sps(1.83), notes="40-150 by charge (0.9 s).", source=lia),
  W("Assault Rifle", "auto-rifle", 7, fi=sps(6.87), alt=W("Assault Rifle (grenade)", "grenade", 70, fi=sps(1.1), source=lia), notes="Dual: 12.5 shots/s.", source=lia),
  W("Bio Rifle", "plasma/projectile-auto", 39, fi=sps(3.33), notes="Charged glob up to 210.", source=lia),
  W("Shock Rifle (beam)", "hitscan-rifle", 45, fi=sps(1.57), alt=W("Shock Rifle (ball)", "plasma/projectile-auto", 45, fi=sps(1.83), source=lia), source=lia),
  W("Shock Combo", "other", 200, fi=sps(0.84), source=lia),
  W("Link Gun", "plasma/projectile-auto", 30, fi=sps(5.5), alt=W("Link Gun (beam)", "beam", 9, fi=sps(9.16), source=lia), source=lia),
  W("Minigun", "lmg", 7.5, fi=sps(16.49), alt=W("Minigun (alt)", "lmg", 15, fi=sps(5.5), notes="14-16.", source=lia), notes="7-8 per bullet.", source=lia),
  W("Flak Cannon", "shotgun", 13, pellets=9, fi=sps(1.22), alt=W("Flak Cannon (shell)", "grenade", 90, fi=sps(1.0), notes="90 impact, up to 168 with shrapnel.", source=lia), notes="9 chunks x 13 (damage drops over flight time).", source=lia),
  W("Rocket Launcher", "rocket", 90, fi=sps(1.22), notes="Load-up of 3: up to 270.", source=lia),
  W("Lightning Gun", "sniper", 70, head=140, fi=sps(0.68), source=lia),
  W("Sniper Rifle (classic)", "sniper", 60, head=120, fi=sps(0.82), source=lia),
  W("Mine Layer", "grenade", 95, fi=1.0, source=lia),
  W("Grenade Launcher", "grenade", 100, fi=sps(0.66), notes="Sticky grenades, 100 each (up to 8 out).", source=lia),
  W("AVRiL", "rocket", 125, fi=sps(0.27), source=lia),
 ]})

games.append({"id": "ut3", "name": "Unreal Tournament 3", "health": 100,
 "armor": "Helmet, thigh pads, vest, shield belt (values not taken from the cited source).",
 "healthNote": "Standard 100 (not restated by the cited page).",
 "units": "Fire rates from the liandri table's shots per second.",
 "source": lia, "patch": "2.1 era (Liandri Archives weapon comparison)",
 "weapons": [
  W("Impact Hammer", "melee", 20, fi=1.0, notes="20-140 by charge (2.27 s).", source=lia),
  W("Enforcer", "pistol", 20, fi=sps(3.05), alt=W("Enforcer (burst)", "pistol", 20, pellets=3, fi=sps(0.82), notes="3-round burst.", source=lia), notes="Dual: 6.11 shots/s.", source=lia),
  W("Bio Rifle", "plasma/projectile-auto", 21, fi=sps(3.14), notes="Charged glob up to 210.", source=lia),
  W("Shock Rifle (beam)", "hitscan-rifle", 45, fi=sps(1.46), alt=W("Shock Rifle (ball)", "plasma/projectile-auto", 55, fi=sps(1.83), source=lia), source=lia),
  W("Shock Combo", "other", 215, fi=sps(0.84), source=lia),
  W("Link Gun", "plasma/projectile-auto", 45, fi=sps(6.87), alt=W("Link Gun (beam)", "beam", 5, fi=sps(22), source=lia), notes="Value as listed by liandri (45 per bolt); verify in game.", source=lia),
  W("Stinger Minigun", "lmg", 14, fi=sps(11), alt=W("Stinger (shards)", "plasma/projectile-auto", 38, fi=sps(3.92), source=lia), source=lia),
  W("Flak Cannon", "shotgun", None, pellets=None, fi=1.0, alt=W("Flak Cannon (shell)", "grenade", 100, fi=1.0, notes="100-190.", source=lia), notes="13-202 per shot (all chunks); chunk count/damage not listed.", source=lia),
  W("Rocket Launcher", "rocket", 100, fi=sps(1.1), notes="Load-up of 3: up to 300.", source=lia),
  W("Sniper Rifle", "sniper", 70, head=140, fi=sps(0.82), source=lia),
  W("Longbow AVRiL", "rocket", 125, fi=sps(0.27), source=lia),
 ]})

# ---------------------------------------------------------------- Xonotic
xon = "https://gitlab.com/xonotic/xonotic-data.pk3dir/-/blob/master/bal-wep-xonotic.cfg"
games.append({"id": "xonotic", "name": "Xonotic", "health": 100,
 "armor": "Start armor 0, limit 200; armor blocks 70% (g_balance_armor_blockpercent 0.7).",
 "healthNote": "g_balance_health_start 100, limit 200 (balance-xonotic.cfg).",
 "units": "Quake units (qu), 1 qu = 2.54 cm. Splash 'min' = edgedamage at the radius.",
 "source": xon, "patch": "git master 2026-10-21 (0.8.x balance)",
 "weapons": [
  W("Blaster", "plasma/projectile-auto", 20, splash=S(20, 60*INCH, 10), fi=0.7, ps=6000*INCH, notes="Mostly used for blaster jumps.", source=xon),
  W("Shotgun", "shotgun", 4, pellets=12, fi=0.75, alt=W("Shotgun (melee slap)", "melee", 70, fi=1.25, rng=120*INCH, source=xon), source=xon),
  W("MachineGun", "auto-rifle", 6, fi=0.0625, alt=W("MachineGun (burst)", "auto-rifle", 6, pellets=5, fi=0.4375, notes="5-shot burst, 3/64 s apart, 7/16 s cooldown.", source=xon), notes="Sustained 6 per 1/16 s (mode 1). Mode 0: first shot 14, refire 0.125.", source=xon),
  W("Mortar", "grenade", 55, splash=S(55, 120*INCH, 25), fi=0.8, ps=1900*INCH, alt=W("Mortar (bouncing)", "grenade", 55, splash=S(55, 120*INCH, 30), fi=0.7, ps=1400*INCH, source=xon), source=xon),
  W("Electro", "plasma/projectile-auto", 40, splash=S(40, 100*INCH, 20), fi=0.6, ps=2500*INCH, alt=W("Electro (orbs)", "grenade", 30, pellets=3, splash=S(30, 150*INCH, 15), fi=1.2, ps=1000*INCH, notes="3 orbs 0.2 s apart; shooting an orb: combo 50 over 150 qu.", source=xon), source=xon),
  W("Crylink", "shotgun", 10, pellets=6, splash=S(10, 80*INCH, 5), fi=0.7, ps=2000*INCH, alt=W("Crylink (focused)", "shotgun", 8, pellets=5, splash=S(8, 100*INCH, 4), fi=0.7, ps=3000*INCH, source=xon), source=xon),
  W("Vortex", "railgun", 80, fi=1.5, notes="Charge: minimum 40 damage at low charge.", source=xon),
  W("Hagar", "plasma/projectile-auto", 25, splash=S(25, 65*INCH, 12.5), fi=1/6, ps=2200*INCH, alt=W("Hagar (load)", "rocket", 35, pellets=4, splash=S(35, 80*INCH, 17.5), fi=0.5, ps=2000*INCH, notes="Loads up to 4.", source=xon), source=xon),
  W("Devastator", "rocket", 80, splash=S(80, 110*INCH, 40), fi=1.1, ps=1300*INCH, notes="Starts at 1000 qu/s, accelerates to 1300; remote detonation 70.", source=xon),
  W("Arc", "beam", 100, fi=None, rng=1500*INCH, alt=W("Arc (bolts)", "plasma/projectile-auto", 25, splash=S(25, 65*INCH, 12.5), fi=1/6, ps=2300*INCH, source=xon), notes="Beam: 100 damage per second (beam_damage x frametime), range 1500 qu, overheats.", source=xon),
  W("Vaporizer", "railgun", 150, fi=1.0, notes="Instagib weapon (kills outright in instagib).", source=xon),
  W("Mine Layer", "grenade", 40, splash=S(40, 175*INCH, 20), fi=1.0, ps=800*INCH, notes="Mutator weapon.", source=xon),
  W("Rifle", "sniper", 80, head=None, fi=1.2, alt=W("Rifle (burst)", "sniper", 20, fi=0.9, source=xon), notes="Mutator weapon; headshot_multiplier 0 (no extra head damage in the default balance).", source=xon),
  W("HLAC", "plasma/projectile-auto", 10, splash=S(10, 60*INCH, 5), fi=0.078125, ps=6000*INCH, notes="Mutator weapon, 12.8/s.", source=xon),
 ]})

# ---------------------------------------------------------------- Warsow / Warfork
wf = "https://github.com/Warfork/warfork-qfusion/blob/master/source/gameshared/gs_weapondefs.c"
games.append({"id": "warfork", "name": "Warsow / Warfork", "health": 100,
 "armor": "Armor pickups (values not taken from the cited source).",
 "healthNote": "Standard 100 (not restated in gs_weapondefs.c).",
 "units": "Quake units, 1 unit = 2.54 cm. 'Strong' = with ammo, 'weak' = infinite weak ammo. Splash 'min' = splash minimum damage. Qfusion (Warsow 2.x) has the same damages; only knockback differs.",
 "source": wf, "patch": "Warfork master (gs_weapondefs.c), matches Qfusion master",
 "weapons": [
  W("Gunblade (blast)", "plasma/projectile-auto", 35, splash=S(35, 70*INCH, 8), fi=0.6, ps=3000*INCH, alt=W("Gunblade (blade)", "melee", 50, fi=0.6, rng=64*INCH, source=wf), notes="Charges over time (1 shot).", source=wf),
  W("Machinegun", "auto-rifle", 10, fi=0.1, source=wf),
  W("Riotgun", "shotgun", 5, pellets=20, fi=0.9, alt=W("Riotgun (weak ammo)", "shotgun", 4, pellets=25, fi=0.9, source=wf), source=wf),
  W("Grenade Launcher", "grenade", 80, splash=S(80, 125*INCH, 15), fi=0.8, ps=1000*INCH, notes="Weak ammo radius 135.", source=wf),
  W("Rocket Launcher", "rocket", 80, splash=S(80, 125*INCH, 15), fi=0.95, ps=1150*INCH, notes="Weak ammo radius 135.", source=wf),
  W("Plasmagun", "plasma/projectile-auto", 15, splash=S(15, 45*INCH, 5), fi=0.1, ps=2500*INCH, source=wf),
  W("Lasergun", "beam", 7, fi=0.05, rng=850*INCH, source=wf),
  W("Electrobolt", "railgun", 75, fi=1.25, source=wf),
  W("Instagun", "railgun", 200, splash=S(None, 80*INCH), fi=1.3, notes="Instagib.", source=wf),
 ]})

# ---------------------------------------------------------------- Team Fortress 2
sw = "https://strategywiki.org/wiki/Team_Fortress_2/"
tfw2 = "https://wiki.teamfortress.com/wiki/"      # official wiki: attack intervals (offline 2026-10, see SOURCES.md)
tfc = "https://tf2console.fandom.com/wiki/"       # console wiki: cross-check of intervals where it lists them
games.append({"id": "tf2", "name": "Team Fortress 2", "health": 125,
 "armor": "No armor; Medic overheal to 150%.",
 "healthNote": "Class health: Scout 125, Medic 150, Demoman 175, Soldier 200, Heavy 300 (StrategyWiki class pages); Engineer, Sniper, Spy 125 and Pyro 175 are not verified here. 125 used as the base.",
 "units": "Hammer units; TF2 Wiki is offline (2026-10), so damage comes from StrategyWiki's class pages. Base damage = critical / 3 where the crit and the point-blank range agree (crits do 3x base); ranges are in notes. Attack intervals are the official TF2 Wiki's 'Damage and function times' values (known from the wiki before it went offline, not re-fetched), checked against tf2console.fandom.com where that lists one; see SOURCES.md.",
 "source": sw + "Classes", "patch": "StrategyWiki class pages (mirroring the official wiki)",
 "weapons": [
  W("Scattergun", "shotgun", 6, pellets=10, fi=0.625, user="Scout", notes="10 pellets x 6 (base 60, crit 180); point blank 85-105, mid 10-40, long 3-10. Attack interval 0.625 s and pellets: " + tfw2 + "Scattergun.", source=sw + "Scout"),
  W("Pistol", "pistol", 15, fi=0.17, user="Scout", notes="Crit 45; point blank 20-22, mid 10-15, long 8-9. Attack interval 0.17 s (" + tfw2 + "Pistol); tf2console lists ~6 rounds/s.", source=sw + "Scout"),
  W("Bat", "melee", 35, fi=0.5, user="Scout", notes="Crit 105.", source=sw + "Scout"),
  W("Rocket Launcher", "rocket", 90, fi=0.8, ps=1100*UU, user="Soldier", notes="Crit 270; point blank 105-112, mid 50-90, long 45-60. ~1100 HU/s (Fandom: ~47 mph, i.e. ~21 m/s). Attack interval 0.8 s (" + tfw2 + "Rocket_Launcher); reload 0.92 s first / 0.8 s each.", source=sw + "Soldier"),
  W("Shotgun", "shotgun", 6, pellets=10, fi=0.625, user="Soldier/Pyro/Heavy/Engineer", notes="10 pellets x 6 (base 60); point blank 86-90 (= 60 x 1.5 ramp-up), mid 24-67, long 3-26. The page's crit value (120) does not match. 6 per pellet and attack interval 0.625 s: " + tfc + "Shotgun (and " + tfw2 + "Shotgun).", source=sw + "Soldier"),
  W("Flame Thrower", "beam", 6.82, fi=6.82/153.45, user="Pyro", notes="6.82 per particle point blank (~153 DPS), 4.09 mid (~92 DPS); afterburn 60 over 10 s.", source=sw + "Pyro"),
  W("Grenade Launcher", "grenade", 100, fi=0.6, user="Demoman", notes="Direct 84-123, splash 22-64, crit 270-330 (so base 100 +-10%); 3 s fuse. Attack interval 0.6 s (" + tfw2 + "Grenade_Launcher).", source=sw + "Demoman"),
  W("Stickybomb Launcher", "grenade", 120, splash=S(124, None, 60), fi=0.6, user="Demoman", notes="Direct 120, splash 60-124; arm time 1 s. Attack interval 0.6 s (" + tfw2 + "Stickybomb_Launcher).", source=sw + "Demoman"),
  W("Minigun", "lmg", 9, pellets=4, fi=0.105, user="Heavy", notes="Per bullet: crit 27; point blank 50-54 per volley, mid 5-30, long 5-10. 4 bullets every 0.105 s once spun up (" + tfw2 + "Minigun).", source=sw + "Heavy"),
  W("Syringe Gun", "plasma/projectile-auto", 10, fi=0.105, ps=990*INCH, user="Medic", notes="Crit 30; point blank 10-12, mid 5-10, long 5-6. Attack interval 0.105 s (" + tfw2 + "Syringe_Gun; tf2console lists ~0.1 s); ~990 HU/s per " + tfc + "Syringe_Gun.", source=sw + "Medic"),
  W("Crusader's Crossbow", "crossbow/bow", 38, user="Medic", notes="38 / 56 / 75 rising with distance travelled; crit 113. One bolt per reload; its interval wasn't confirmed from a source, so it stays empty.", source=sw + "Medic"),
  W("Sniper Rifle", "sniper", 50, head=150, fi=1.5, user="Sniper", alt=W("Sniper Rifle (full charge)", "sniper", 150, head=450, notes="Body 128-172; ~4 s to charge.", source=sw + "Sniper"), notes="Uncharged body 43-57, headshot 150. Attack interval 1.5 s (" + tfc + "Sniper_Rifle, and " + tfw2 + "Sniper_Rifle).", source=sw + "Sniper"),
  W("SMG", "smg", 8, fi=0.105, user="Sniper", notes="Crit 24; point blank 8-12, mid 4-6, long 4-5. Attack interval 0.105 s (" + tfw2 + "SMG; tf2console lists ~10 bullets/s).", source=sw + "Sniper"),
  W("Revolver", "pistol", 40, fi=0.5, user="Spy", notes="Crit 120; point blank 55-60, mid 35-45, long 16-25. Attack interval 0.5 s (" + tfw2 + "Revolver); the console version lists 0.58 s (" + tfc + "Revolver).", source=sw + "Spy"),
  W("Knife", "melee", 40, fi=0.8, user="Spy", notes="Backstab: 6x the victim's current health.", source=sw + "Spy"),
  W("Melee (Shovel / Fire Axe / Fists / Bottle / Bonesaw / Kukri)", "melee", 65, fi=0.8, notes="Crit 195.", source=sw + "Soldier"),
 ]})

# ---------------------------------------------------------------- Overwatch 2
ow = "https://overwatch.fandom.com/wiki/"
games.append({"id": "ow2", "name": "Overwatch 2", "health": 250,
 "armor": "Health pools mix health, armor (reduces small hits) and shields per hero.",
 "healthNote": "Hero-dependent; 250 used as the base (Ashe/Junkrat 250, Widowmaker 225, Tracer 175, Reaper 300 per the hero pages). Per-weapon userHealth is the wielder's.",
 "units": "Metres (the game's own unit). Headshots 2x (Widowmaker scoped 2.5x) per the Fandom Headshot page.",
 "source": ow + "Headshot", "patch": "Overwatch Wiki (Fandom) hero pages, retrieved 2026-10-24",
 "weapons": [
  W("Heavy Pulse Rifle (Soldier: 76)", "auto-rifle", 19, head=38, fi=1/8.66, falloff={"start": 30, "end": 50, "minDamage": 9.5}, user="Soldier: 76", source=ow + "Soldier:_76"),
  W("Helix Rockets (Soldier: 76 ability)", "rocket", 120, splash=S(80, 2.0), fi=6.0, ps=50, user="Soldier: 76", notes="Ability, 6 s cooldown; page lists 120 direct and 80 splash.", source=ow + "Soldier:_76"),
  W("Widow's Kiss (scoped, full charge)", "sniper", 120, head=300, fi=0.5, falloff={"start": 50, "end": 70, "minDamage": 60}, user="Widowmaker", userHealth=225, notes="0.9 s charge to full; 2.5x headshot.", alt=W("Widow's Kiss (unscoped auto)", "smg", 14, head=28, fi=0.1, falloff={"start": 20, "end": 40, "minDamage": 4.2}, source=ow + "Widowmaker"), source=ow + "Widowmaker"),
  W("Rocket Launcher (Pharah)", "rocket", 40, splash=S(80, 2.5, 24.6), fi=0.8, ps=40, user="Pharah", notes="Splash full inside 0.8 m, 24.6 at 2.5 m.", source=ow + "Pharah"),
  W("Hellfire Shotguns (Reaper)", "shotgun", 5.75, head=11.5, pellets=20, fi=0.5, falloff={"start": 10, "end": 20, "minDamage": 1.725}, user="Reaper", userHealth=300, source=ow + "Reaper"),
  W("Peacekeeper (Cassidy)", "pistol", 70, head=140, fi=0.5, falloff={"start": 25, "end": 35, "minDamage": 21}, user="Cassidy", alt=W("Fan the Hammer", "pistol", 50, fi=1/8.33, falloff={"start": 20, "end": 30, "minDamage": 15}, source=ow + "Cassidy"), source=ow + "Cassidy"),
  W("Pulse Pistols (Tracer)", "smg", 6, pellets=2, fi=0.05, falloff={"start": 10, "end": 20, "minDamage": 1.8}, user="Tracer", userHealth=175, notes="2 bullets per shot at 20 shots/s as listed; the page's 220 DPS figure does not match exactly.", source=ow + "Tracer"),
  W("The Viper (Ashe, hip)", "hitscan-rifle", 40, fi=0.266, falloff={"start": 20, "end": 40, "minDamage": 12}, user="Ashe", userHealth=250, alt=W("The Viper (ADS)", "sniper", 75, head=150, fi=0.65, falloff={"start": 30, "end": 50, "minDamage": 22.5}, source=ow + "Ashe"), source=ow + "Ashe"),
  W("Frag Launcher (Junkrat)", "grenade", 45, splash=S(80, 2.0, 10), fi=1/1.5, ps=30, user="Junkrat", userHealth=250, notes="Splash full inside 0.5 m.", source=ow + "Junkrat"),
 ]})

# ---------------------------------------------------------------- Valorant
va = "https://valorant-api.com/v1/weapons/"
def V(name, cls, uuid, rpm_s, brackets, pellets=1, alt=None, notes=""):
    (s, e, h, b, l) = brackets[0]
    fo = [{"from": x[0], "to": x[1], "head": x[2], "body": x[3], "leg": x[4]} for x in brackets]
    return W(name, cls, b, head=h, limb=l, pellets=pellets, fi=1.0 / rpm_s, falloff={"brackets": fo} if len(fo) > 1 else None, alt=alt, notes=notes, source=va + uuid)
games.append({"id": "valorant", "name": "VALORANT", "health": 100,
 "armor": "Bought shields on top of 100 health; the wiki's Operator page speaks of 100 / 125 / 150 effective HP (no shields / light / heavy).",
 "healthNote": "100 health plus shields.",
 "units": "Metres; damage brackets by distance (start-end m) from the game data exposed by valorant-api.com. fireInterval = 1 / fireRate. limb = leg.",
 "source": "https://valorant-api.com/v1/weapons", "patch": "13.08 (build 2026-10-15)",
 "weapons": [
  V("Classic", "pistol", "29a0cfab-485b-f5d5-779a-b59f85e204a8", 6.75, [(0, 30, 78, 26, 22.1), (30, 50, 66, 22, 18.7)], notes="Alt: 3-pellet burst at 2.22/s."),
  V("Ghost", "pistol", "1baa85b4-4c70-1284-64bb-6481dfc3bb4e", 6.75, [(0, 30, 105, 30, 25.5), (30, 50, 87.5, 25, 21.25)]),
  V("Sheriff", "pistol", "e336c6b8-418d-9340-d77f-7a9e4cfe0702", 4, [(0, 30, 159.5, 55, 46.75), (30, 50, 145, 50, 42.5)]),
  V("Stinger", "smg", "f7e1b454-4ad4-1063-ec0a-159e56b58941", 16, [(0, 15, 67.5, 27, 22.95), (15, 50, 57, 23, 19)], notes="ADS: 4-round burst at 8.47/s."),
  V("Spectre", "smg", "462080d1-4035-2937-7c09-27aa2a5c27a7", 13.333, [(0, 15, 78, 26, 22.1), (15, 30, 66, 22, 18.7), (30, 50, 60, 20, 17)]),
  V("Bucky", "shotgun", "910be174-449b-c412-ab22-d0873436b21b", 1.1, [(0, 8, 34, 17, 14), (8, 12, 26, 13, 11.05), (12, 50, 18, 9, 7.65)], pellets=15, notes="Per pellet. Alt: airburst of 5 pellets at 7.5 m."),
  V("Judge", "shotgun", "ec845bf4-4f79-ddda-a3da-0db3774b2794", 3.5, [(0, 10, 34, 17, 14.45), (10, 15, 20, 10, 8.5), (15, 50, 14, 7, 5.95)], pellets=12, notes="Per pellet."),
  V("Bulldog", "auto-rifle", "ae3de142-4d85-2547-dd26-4e90bed35cf7", 10, [(0, 50, 115.5, 35, 29.75)], notes="ADS: 3-round burst at 6.32/s."),
  V("Guardian", "hitscan-rifle", "4ade7faa-4cf1-8376-95ef-39884480959b", 5.25, [(0, 50, 195, 65, 48.75)]),
  V("Phantom", "auto-rifle", "ee8e8d15-496b-07ac-e5f6-8fae5d4c7b1a", 11, [(0, 20, 156, 39, 33.15), (20, 50, 140, 35, 29.75)], notes="ADS 9.9/s."),
  V("Vandal", "auto-rifle", "9c82e19d-4575-0200-1a81-3eacf00cf872", 9.75, [(0, 50, 160, 40, 34)], notes="No falloff. ADS 8.775/s."),
  V("Marshal", "sniper", "c4883e50-4494-202c-3ec3-6b8a9284f00b", 1.5, [(0, 50, 202, 101, 85.85)], notes="ADS 1.2/s."),
  V("Outlaw", "sniper", "5f0aaf7a-4289-3998-d5ff-eb9a5cf7ef5c", 2.75, [(0, 50, 238, 140, 119)], notes="2-round magazine."),
  V("Operator", "sniper", "a03b24d3-4319-996d-0f8c-94bbfba1dfc7", 0.6, [(0, 50, 255, 150, 120)]),
  V("Ares", "lmg", "55d8a0f4-4274-ca67-fe2c-06ab45efdf58", 13, [(0, 30, 75, 30, 25.5), (30, 50, 70, 28, 23.8)]),
  V("Odin", "lmg", "63e6c2b6-4a8e-869c-3d4c-e38355226584", 12, [(0, 30, 95, 38, 32.3), (30, 50, 77.5, 31, 26.35)], notes="Spins up; ADS fires at 15.6/s at once."),
 ]})

# ---------------------------------------------------------------- Counter-Strike 2
cs = "https://github.com/SteamDatabase/GameTracking-CS2/blob/master/game/csgo/pak01_dir/scripts/weapons.vdata"
def C(name, cls, dmg, cyc, hs=4.0, rm=0.98, rng=8192, bullets=1, ar=None, notes=""):
    return W(name, cls, dmg, head=round(dmg * hs, 2), pellets=bullets, fi=cyc, rng=rng * INCH,
             falloff={"rangeModifier": rm, "per": r(500 * INCH), "note": "damage x rangeModifier^(distance/500 units)"},
             notes=(f"Armor penetration ratio {ar}. " if ar else "") + notes, source=cs)
games.append({"id": "cs2", "name": "Counter-Strike 2", "health": 100,
 "armor": "Kevlar and helmet; each weapon's armor ratio sets how much armor absorbs.",
 "healthNote": "100 health.",
 "units": "Source units, 1 unit = 2.54 cm (1 inch). head = damage x m_flHeadshotMultiplier (unarmoured, close range). Stomach/leg multipliers are in code and not included.",
 "source": cs, "patch": "weapons.vdata on GameTracking-CS2 master, retrieved 2026-10-24",
 "weapons": [
  C("Glock-18", "pistol", 30, 0.15, rm=0.85, rng=4096, ar=0.94, notes="Burst mode: 3 shots, 0.5 s between bursts."),
  C("USP-S", "pistol", 35, 0.17, rm=0.91, rng=4096, ar=1.01),
  C("P2000", "pistol", 35, 0.17, rm=0.91, rng=4096, ar=1.01),
  C("P250", "pistol", 38, 0.15, rm=0.9, rng=4096, ar=1.28),
  C("Five-SeveN", "pistol", 32, 0.15, rm=0.81, rng=4096, ar=1.823),
  C("Tec-9", "pistol", 33, 0.12, rm=0.79, rng=4096, ar=1.812),
  C("CZ75-Auto", "pistol", 31, 0.1, rm=0.85, rng=4096, ar=1.553),
  C("Dual Berettas", "pistol", 38, 0.12, rm=0.79, rng=4096, ar=1.15),
  C("Desert Eagle", "pistol", 53, 0.225, hs=3.9, rm=0.85, rng=4096, ar=1.864),
  C("R8 Revolver", "pistol", 86, 0.5, rm=0.94, rng=4096, ar=1.864, notes="Primary has a trigger delay; alt fan 0.4 s."),
  C("MAC-10", "smg", 29, 0.075, rm=0.8, rng=3600, ar=1.15),
  C("MP9", "smg", 26, 0.07, rm=0.87, rng=3600, ar=1.2),
  C("MP7", "smg", 30, 0.08, rm=0.87, rng=3600, ar=1.25),
  C("MP5-SD", "smg", 28, 0.08, rm=0.87, rng=3600, ar=1.25),
  C("UMP-45", "smg", 35, 0.09, rm=0.75, rng=3700, ar=1.3),
  C("P90", "smg", 26, 0.07, rm=0.86, rng=3700, ar=1.38),
  C("PP-Bizon", "smg", 27, 0.08, rm=0.8, rng=3600, ar=1.26),
  C("Nova", "shotgun", 26, 0.88, rm=0.7, rng=3000, bullets=9, ar=1.0, notes="Per pellet."),
  C("XM1014", "shotgun", 20, 0.35, rm=0.7, rng=3000, bullets=6, ar=1.6, notes="Per pellet."),
  C("MAG-7", "shotgun", 30, 0.85, rm=0.45, rng=1400, bullets=8, ar=1.5, notes="Per pellet."),
  C("Sawed-Off", "shotgun", 32, 0.85, rm=0.45, rng=1400, bullets=8, ar=1.5, notes="Per pellet."),
  C("Galil AR", "auto-rifle", 30, 0.09, ar=1.55),
  C("FAMAS", "auto-rifle", 30, 0.09, rm=0.96, ar=1.4, notes="Burst mode 0.55 s between bursts."),
  C("AK-47", "auto-rifle", 36, 0.1, ar=1.55),
  C("M4A4", "auto-rifle", 33, 0.09, rm=0.97, ar=1.4),
  C("M4A1-S", "auto-rifle", 38, 0.1, hs=3.475, rm=0.94, ar=1.4),
  C("SG 553", "auto-rifle", 30, 0.11, ar=2.0),
  C("AUG", "auto-rifle", 28, 0.1, ar=1.8),
  C("SSG 08", "sniper", 88, 1.25, ar=1.7),
  C("AWP", "sniper", 115, 1.455, rm=0.99, ar=1.95),
  C("G3SG1", "sniper", 80, 0.25, ar=1.65),
  C("SCAR-20", "sniper", 80, 0.25, ar=1.65),
  C("M249", "lmg", 32, 0.08, rm=0.97, ar=1.6),
  C("Negev", "lmg", 35, 0.075, rm=0.97, ar=1.42),
  W("HE Grenade", "grenade", 99, splash=S(99, 350*INCH), notes="m_nDamage 99, range 350 units.", source=cs),
 ]})

# ---------------------------------------------------------------- Apex Legends
ap = "https://apexlegends.wiki.gg/wiki/"
def A(name, cls, b, h, l, rpm, hu, page, pellets=1, notes=""):
    return W(name, cls, b, head=h, limb=l, pellets=pellets, rpm=rpm, ps=None if hu is None else hu * INCH, notes=notes, source=ap + page)
games.append({"id": "apex", "name": "Apex Legends", "health": 100,
 "armor": "Evo shields on top of health; helmets reduce headshot damage (shield values not taken from the cited pages).",
 "healthNote": "100 health plus shields.",
 "units": "Hammer units/s converted at 1 HU = 2.54 cm (the wiki's own conversion, e.g. 18000 HU/s = 457 m/s). head = unhelmeted headshot; limb = leg.",
 "source": ap + "Weapons", "patch": "Apex Legends Wiki (wiki.gg), patches up to 2025-10-07",
 "weapons": [
  A("HAVOC Rifle", "auto-rifle", 18, 32, 15, 672, 30500, "Weapons"),
  A("VK-47 Flatline", "auto-rifle", 18, 32, 15, 600, 24000, "Weapons"),
  A("Hemlok Burst AR", "auto-rifle", 19, 33, 14, 384, 27500, "Weapons", notes="Single fire 384 RPM, burst 365 RPM."),
  A("R-301 Carbine", "auto-rifle", 13, 23, 10, 810, 29000, "Weapons"),
  A("Nemesis Burst AR", "auto-rifle", 16, 28, 12, 582, 31000, "Weapons"),
  A("Alternator SMG", "smg", 16, 20, 13, 600, 19000, "Weapons"),
  A("Prowler Burst PDW", "smg", 15, 23, 12, 579, 18000, "Weapons"),
  A("R-99 SMG", "smg", 14, 18, 11, 1080, 19000, "Weapons", notes="Damage drops to 11/14/9 at range."),
  A("C.A.R. SMG", "smg", 12, 15, 10, 930, 18000, "Weapons"),
  A("Devotion LMG", "lmg", 16, 24, 14, 900, 33500, "Weapons", notes="Spins up."),
  A("L-STAR EMG", "lmg", 17, 26, 14, 600, 24000, "Weapons"),
  A("M600 Spitfire", "lmg", 18, 27, 15, 540, 27500, "Weapons"),
  A("Rampage LMG", "lmg", 26, 39, 22, 300, 26500, "Weapons", notes="Revved: 390 RPM."),
  A("G7 Scout", "hitscan-rifle", 33, 58, 25, 252, 30000, "Weapons"),
  A("Longbow DMR", "sniper", 60, 108, 48, 78, 30500, "Longbow_DMR", notes="Skullpiercer: 129 head. Latest change 2025-05-06."),
  A("Kraber .50-Cal Sniper", "sniper", 150, 210, 120, 25, 29500, "Kraber_.50-Cal_Sniper", notes="25 RPM per the stats table (advanced stats say fire_rate 1.2/s). Changed 2025-02-11."),
  A("Wingman", "pistol", 50, 75, 45, 168, 18000, "Wingman", notes="Changed 2025-09-16."),
  A("Peacekeeper", "shotgun", 11, None, 11, 44, 20000, "Peacekeeper", pellets=9, notes="9 pellets x 11; headshot per pellet unclear on the page (15 in the 2025-02-12 notes). Rechamber 0.95 s; 44 RPM base. Changed 2025-10-07."),
  A("EVA-8 Auto", "shotgun", 7, 7, 7, 168, 16000, "EVA-8_Auto", pellets=8, notes="8 pellets x 7. Changed 2025-06-30."),
 ]})

# ---------------------------------------------------------------- The Finals
tf = "https://www.thefinals.wiki/wiki/"
games.append({"id": "thefinals", "name": "THE FINALS", "health": 250,
 "armor": "No armor; health by build.",
 "healthNote": "Light 150, Medium 250 (wiki weapon pages); Heavy not verified here. 250 used as the base.",
 "units": "Metres.",
 "source": tf + "Weapons", "patch": "THE FINALS Wiki, patches up to 12.0.0",
 "weapons": [
  W("AKM", "auto-rifle", 20, head=30, rpm=600, falloff={"start": 30, "end": 37.5, "multiplier": 0.55}, user="Medium", source=tf + "AKM"),
  W("Model 1887", "shotgun", 12, head=12, pellets=9, rpm=72, falloff={"start": 20, "end": 30, "multiplier": 0.65}, notes="No headshot bonus.", source=tf + "Model_1887"),
  W("SR-84", "sniper", 118, head=236, rpm=45, falloff={"start": 80, "end": 100, "multiplier": 0.75}, user="Light", userHealth=150, source=tf + "SR-84"),
  W("Recurve Bow (full draw)", "crossbow/bow", 124, head=186, notes="Full draw after 0.49 s; uncharged 60 / 90 head. 103 RPM per patch 9.4.0.", user="Light", source=tf + "Recurve_Bow"),
  W("Dagger", "melee", 60, notes="Backstab (secondary from behind) 320; secondary 75.", user="Light", source=tf + "Dagger"),
  W("M60", "lmg", 20, head=30, rpm=600, falloff={"start": 25, "end": 35, "multiplier": 0.5}, user="Heavy", source=tf + "M60"),
  W("Sledgehammer", "melee", 120, alt=W("Sledgehammer (overhead)", "melee", 200, notes="150 glancing.", source=tf + "Sledgehammer"), notes="90 glancing.", user="Heavy", source=tf + "Sledgehammer"),
 ]})

# ---------------------------------------------------------------- Deadlock
dl = "https://deadlock.wiki/"
games.append({"id": "deadlock", "name": "Deadlock", "health": 755,
 "armor": "Bullet resist / spirit resist from items and abilities.",
 "healthNote": "Hero base health 730-800 among the heroes listed (Haze 730, Vindicta 755, Abrams 800; all grow per Boon). 755 used as the base.",
 "units": "Metres. Falloff is linear from 100% at the start to 10% at the end. Weakpoint (head) hits do 1.65x minus the hero's Crit Bonus Scale (-20% to -45%), so head is left null.",
 "source": dl + "Headshot", "patch": "Deadlock Wiki, game updates up to 2026-09-16",
 "weapons": [
  W("Censure & Penance (Haze)", "smg", 5.26, fi=1/9.52, ps=762, falloff={"start": 20, "end": 46, "multiplier": 0.1}, user="Haze", userHealth=730, notes="+0.143 per Boon; DPS 50.", source=dl + "Haze"),
  W("Wallflower's Revenge (Vindicta)", "sniper", 12.3, fi=1/4.33, ps=660, falloff={"start": 20, "end": 64, "multiplier": 0.1}, user="Vindicta", userHealth=755, notes="Base gun (the scoped shot is an ability). DPS 53.", source=dl + "Vindicta"),
  W("Case Closed (Abrams)", "shotgun", 3.6, pellets=9, fi=1/1.59, ps=610, falloff={"start": 17, "end": 40, "multiplier": 0.1}, user="Abrams", userHealth=800, notes="DPS 51.", source=dl + "Abrams"),
 ]})

# ---------------------------------------------------------------- Titanfall 2 (partial)
tfw = "https://titanfall.fandom.com/wiki/"
tgg = "https://titanfall.wiki.gg/wiki/"
games.append({"id": "titanfall2", "name": "Titanfall 2 (partial)", "health": None,
 "armor": "None for pilots.",
 "healthNote": "Pilot health not verified from a cited page.",
 "units": "Metres; the wiki converts 1500 units = 28.575 m (1 unit = 1.905 cm).",
 "source": tfw + "R-201_Assault_Rifle", "patch": "Titanfall Wiki (Fandom) and titanfall.wiki.gg, retrieved 2026-10-24",
 "weapons": [
  W("R-201 Assault Rifle", "auto-rifle", 25, rpm=810, falloff={"start": 28.575, "end": 66.675}, notes="Far damage not listed.", source=tfw + "R-201_Assault_Rifle"),
  W("Kraber-AP Sniper", "sniper", 100, notes="The only pilot weapon doing 100 per shot; fire rate not listed.", source=tfw + "Kraber-AP_Sniper"),
  W("C.A.R. SMG", "smg", 25, rpm=846, falloff={"start": 19.05, "end": 57.15}, notes="25 per bullet, actual 846 RPM (the menu lists 40 / 70); falloff 1000-3000 units, far damage not listed. Both wikis agree.", source=tfw + "C.A.R._SMG"),
  W("Alternator", "smg", 35, rpm=600, notes="Titanfall 2 stat box: damage 35, 600 RPM; near/far split and falloff not listed.", source=tgg + "Alternator"),
  W("Hemlok BF-R", "auto-rifle", 33, rpm=930, notes="3-round burst at 930 RPM within the burst (time between bursts not listed); damage 33, falloff not listed.", source=tgg + "Hemlok_BF-R"),
  W("Spitfire LMG", "lmg", 35, rpm=540, notes="Titanfall 2 stat box: damage 35, 540 RPM (its prose says 50, which matches the Titanfall 1 numbers).", source=tgg + "Spitfire_LMG"),
 ]})

doc = {"version": 1, "updated": "2026-10-24",
       "notes": "Numbers come only from the sources named per game/weapon; null = not found. Damage in each game's own HP; distances and speeds in metres. See SOURCES.md for conversions and confidence.",
       "games": games}

out = sys.argv[1]
with open(out, "w") as f:
    json.dump(doc, f, indent=1, ensure_ascii=False)
    f.write("\n")
n = sum(len(g["weapons"]) for g in games)
print(len(games), "games,", n, "weapons")
for g in games:
    print(f'  {g["id"]:12s} {len(g["weapons"])}')
