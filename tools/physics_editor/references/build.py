#!/usr/bin/env python3
"""Writes tools/physics_editor/references/*.json from the raw engine numbers below (python3 build.py; reads Quark's settings from `quark_maptool physparams`).
Every raw number carries its source; conversions are done here so the arithmetic is reproducible."""
import json, math, os, sys

def _physparams():
    import subprocess
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
    for b in ("build", "build-release", "build-editors"):
        exe = os.path.join(root, b, "quark_maptool" + (".exe" if os.name == "nt" else ""))
        if os.path.isfile(exe):
            return json.loads(subprocess.run([exe, "physparams"], input=b"{}", capture_output=True, check=True).stdout)
    raise SystemExit("build quark_maptool first (cmake --build build --target quark_maptool)")
PP = _physparams()
BASE, CLAMP, UNIT = {}, {}, {}
for p in PP["params"] + PP["top"]:
    BASE[p["key"]] = p["base"]; CLAMP[p["key"]] = (p["min"], p["max"]); UNIT[p["key"]] = p["unit"]
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)

def r4(x): return float(f"{x:.4g}") if abs(x) >= 1000 else round(x, 4)

def ground_sim(W, a, f, stop, dt=0.001):
    """Quake / Source / Quark ground model (friction, then accelerate, every 1 ms): time to 90% of W from a standstill,
    and the distance / time to stop from W with no keys held."""
    v = t = 0.0
    while v < 0.9 * W and t < 10:
        if v > 0: v = max(v - max(v, stop) * f * dt, 0)
        add = W - v
        if add > 0: v += min(a * W * dt, add)
        t += dt
    t90 = t
    v, t, d = W, 0.0, 0.0
    while v > 0 and t < 30:
        v = max(v - max(v, stop) * f * dt, 0); d += v * dt; t += dt
    return round(t90, 3), round(d, 3), round(t, 3)

class Game:
    def __init__(s, gid, name, year, engine, group, unit, mpu, why):
        s.d = {"id": gid, "name": name, "year": year, "engine": engine, "group": group,
               "scale": {"unit": unit, "metresPerUnit": round(mpu, 6), "why": why},
               "values": {}, "facts": {}, "extras": [], "notes": "", "sources": []}
        s.mpu = mpu
    def v(s, key, value, source, conf, note=None, raw=None, unit=None):
        assert key in BASE, key
        val = r4(value)
        e = {"value": val, "unit": unit if unit is not None else UNIT[key], "mult": round(val / BASE[key], 4),
             "source": source, "confidence": conf}
        if raw is not None: e["raw"] = raw
        lo, hi = CLAMP[key]
        if not (lo <= e["mult"] <= hi):
            e["outsideClamp"] = True
            note = ((note + " ") if note else "") + f"(x{e['mult']} is outside the editor's {lo}..{hi} range: applying it clamps.)"
        if note: e["note"] = note
        s.d["values"][key] = e
    def u(s, key, units, source, conf="exact", note=None, rawname=None, per=""):
        """a speed / length / acceleration in engine units, converted by the scale"""
        s.v(key, units * s.mpu, source, conf, note, raw=f"{rawname + ' ' if rawname else ''}{units:g} {s.d['scale']['unit']}{per}")
    def toggle(s, key, on, source, note):
        s.d["values"][key] = {"value": bool(on), "source": source, "confidence": "exact", "note": note}
    def fact(s, key, value, unit, source, conf, note=None):
        e = {"value": r4(value) if isinstance(value, (int, float)) else value, "unit": unit, "source": source, "confidence": conf}
        if note: e["note"] = note
        s.d["facts"][key] = e
    def ground_facts(s, W, a, f, stop, note=""):
        t90, sd, st = ground_sim(W, a, f, stop)
        n = "Quake ground model at 1 ms steps (friction then accelerate), the same way Quark's physsim measures its own run_up / stop presets" + note
        s.fact("timeTo90", t90, "s", "derived from this file's speed, ground_accel, friction, stop_speed", "estimated", n + "; Quark: 0.072 s")
        s.fact("stopDistance", sd, "m", "derived from this file's speed, friction, stop_speed", "estimated", n + "; Quark: 1.21 m")
        s.fact("stopTime", st, "s", "derived from this file's speed, friction, stop_speed", "estimated", n + "; Quark: 0.364 s")
    def src(s, title, url): s.d["sources"].append({"title": title, "url": url})
    def write(s):
        json.dump(s.d, open(os.path.join(OUT, s.d["id"] + ".json"), "w"), indent=1, ensure_ascii=False)
        open(os.path.join(OUT, s.d["id"] + ".json"), "a").write("\n")
        return s.d

GH = "https://github.com"
Q3 = GH + "/id-Software/Quake-III-Arena/blob/master/code/game/"
QW = GH + "/id-Software/Quake/blob/master/QW/"
NQ = GH + "/id-Software/Quake/blob/master/WinQuake/"
QC = GH + "/id-Software/Quake-Tools/blob/master/qcc/v101qc/"
Q2 = GH + "/id-Software/Quake-2/blob/master/"
SDK = GH + "/ValveSoftware/source-sdk-2013/blob/master/src/game/"
HL = GH + "/ValveSoftware/halflife/blob/master/"
REHLDS = GH + "/rehlds/ReHLDS/blob/master/rehlds/engine/"
REGAME = GH + "/rehlds/ReGameDLL_CS/blob/master/regamedll/"
WF = GH + "/Warfork/warfork-qfusion/blob/master/source/"
XON = "https://gitlab.com/xonotic/xonotic-data.pk3dir/-/blob/master/"
TF2T = GH + "/Syampuuh/Titanfall2/blob/master/scripts/players/"
CEL = GH + "/NoelFB/Celeste/blob/master/Source/Player/Player.cs"

def air_uncapped(g, accel, wish_u, cap_u, src_accel, src_cap, wish_src):
    """Quake 1 / QuakeWorld / GoldSrc / Source air: the push is accel x FULL wish speed x dt, the gain is limited by a 30 u
    cap. Quark pushes accel x 10 x min(wish, cap) x dt, so the same push needs air_accel = accel x wish / cap / 10."""
    eq = accel * wish_u / cap_u / 10
    g.v("air_accel", eq, f"{src_accel}; {src_cap}; {wish_src}", "estimated",
        f"Quark-equivalent: this engine pushes {accel:g} x the FULL wish speed ({wish_u:g} u/s) per second but caps the gain at {cap_u:g} u/s; "
        f"Quark pushes air_accel x 10 x min(wish, air_cap), so the same push is air_accel = {accel:g} x {wish_u:g} / {cap_u:g} / 10 "
        f"(Xonotic's Quake port does the same: sv_airaccelerate 106.67 with sv_maxairspeed 30 for 10 x 320 / 30).",
        raw=f"accelerate {accel:g}, wish {wish_u:g} u/s, cap {cap_u:g} u/s")
    g.u("air_cap", cap_u, src_cap, note="the air wish-speed cap (Quark's air strafe cap, W-only; A/D raises Quark's x1.1)", per="/s")
    g.v("overstrafe", 1.0, "same code: the push is applied in full whatever its angle to your velocity", "estimated",
        "no overstrafe softening: the whole push against your momentum applies (Quark lets 35% through)")

def air_capped(g, accel, cap_u, src, note):
    """Quake 2 / Quake 3 / CPM / Warsow-style: the push is accel x min(wish, cap) x dt, Quark's own shape (Quark x10)."""
    g.v("air_accel", accel / 10, src, "exact", note + f" Same formula as Quark's (accel x min(wish, cap)); Quark multiplies air_accel by 10, so {accel:g} -> {accel/10:g}.",
        raw=f"air accelerate {accel:g}")
    g.u("air_cap", cap_u, src, note="the wish speed the air push aims at", per="/s")

games = []

# ------------------------------------------------------------------ Quake family
S = 1.8 / 56
g = Game("quake", "Quake / QuakeWorld", 1996, "Quake engine (NetQuake, QuakeWorld)", "Quake family", "u", S,
         "player hull 56 u tall (VEC_HULL_MIN '-16 -16 -24' .. VEC_HULL_MAX '16 16 32'), scaled to Quark's 1.8 m: 0.03214 m/u")
g.u("gravity", 800, f"{QW}server/sv_phys.c#L44 (sv_gravity 800); {NQ}sv_phys.c#L44", rawname="sv_gravity", per="/s2")
g.u("speed", 320, f"{QW}server/sv_phys.c#L46 (sv_maxspeed 320); {NQ}sv_user.c#L167", rawname="sv_maxspeed", per="/s",
    note="the server's cap; cl_forwardspeed is 200 unless +speed / always run (400, clamped to 320)")
g.u("jump", 270, f"{QW}client/pmove.c#L681 (velocity[2] += 270); {QC}client.qc#L811", rawname="jump velocity", per="/s")
g.v("ground_accel", 10, f"{QW}server/sv_phys.c#L48 (sv_accelerate 10); {NQ}sv_user.c#L168", "exact", "Quake accelerate factor (accel x wish speed per second), the same formula as Quark's")
g.v("friction", 4, f"{QW}server/sv_phys.c#L51 (sv_friction 4); {NQ}sv_phys.c#L42", "exact",
    "x2 when the floor drops away 16 u ahead (edgefriction 2; QW pmove.c#L348-360): Quark has no edge friction")
g.u("stop_speed", 100, f"{QW}server/sv_phys.c#L45 (sv_stopspeed 100); {NQ}sv_phys.c#L43", rawname="sv_stopspeed", per="/s")
air_uncapped(g, 10, 320, 30, f"{QW}client/pmove.c#L548 (air uses movevars.accelerate)", f"{QW}client/pmove.c#L422 (wishspd capped at 30); {NQ}sv_user.c#L213", "sv_maxspeed 320")
g.u("step", 18, f"{QW}client/pmove.c#L56 (STEPSIZE 18); {NQ}sv_phys.c#L957", rawname="STEPSIZE")
g.toggle("dash", False, f"{QW}client/pmove.c", "no dash")
g.toggle("grapple", False, f"{QW}client/pmove.c", "no grapple in the base game (mods add one)")
g.fact("jumpHeight", 270**2 / (2 * 800) * S, "m", "270^2 / (2 x 800) = 45.6 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.ground_facts(320 * S, 10, 4, 100 * S, "; edge friction not modelled")
g.d["extras"] = ["bunny hopping and strafe jumping (air acceleration above), but jump must be released between jumps in QuakeWorld ('don't pogo stick', pmove.c#L677)",
                 "edge friction: friction x2 near a drop", "QuakeWorld's sv_airaccelerate 0.7 cvar exists but pmove uses sv_accelerate in the air", "rocket / grenade jumping"]
g.d["notes"] = "Quake and QuakeWorld share these numbers; QuakeWorld's client-side pmove.c is the reference here (NetQuake's sv_user.c / sv_phys.c agree). No auto-hop: Quark's held-jump bhop is closer to later Quake mods."
g.src("QuakeWorld pmove.c", QW + "client/pmove.c"); g.src("QuakeWorld sv_phys.c (cvar defaults)", QW + "server/sv_phys.c")
g.src("WinQuake sv_user.c / sv_phys.c", NQ + "sv_user.c"); g.src("Quake QuakeC client.qc / defs.qc", QC + "client.qc")
games.append(g.write())

g = Game("quake2", "Quake II", 1997, "id Tech 2", "Quake family", "u", S,
         "player hull 56 u tall (mins z -24, maxs z 32, pmove.c#L994/#L1009), scaled to 1.8 m: 0.03214 m/u")
P2 = Q2 + "qcommon/pmove.c"
g.u("gravity", 800, Q2 + "game/g_save.c#L164 (sv_gravity 800)", rawname="sv_gravity", per="/s2")
g.u("speed", 300, P2 + "#L54 (pm_maxspeed = 300)", rawname="pm_maxspeed", per="/s")
g.u("jump", 270, P2 + "#L820 (velocity[2] += 270, at least 270)", rawname="jump", per="/s")
g.v("ground_accel", 10, P2 + "#L56 (pm_accelerate = 10)", "exact", "Quake accelerate factor, same formula as Quark's")
g.v("friction", 6, P2 + "#L59 (pm_friction = 6)", "exact")
g.u("stop_speed", 100, P2 + "#L53 (pm_stopspeed = 100)", rawname="pm_stopspeed", per="/s")
air_capped(g, 1, 300, P2 + "#L57 (pm_airaccelerate = 0) and #L657 (PM_Accelerate(wishdir, wishspeed, 1))",
           "pm_airaccelerate is 0, so the air uses PM_Accelerate with 1 and the full wish speed (no 30 u cap).")
g.v("overstrafe", 1.0, P2 + "#L657", "estimated", "no overstrafe softening: the whole push against your momentum applies")
g.u("step", 18, P2 + "#L25 (STEPSIZE 18)", rawname="STEPSIZE")
g.u("ladder_speed", 200, P2 + "#L447-456 (wishvel[2] = +-200)", rawname="ladder", per="/s")
g.u("ladder_side", 25, P2 + "#L460-470 (horizontal wish clamped to +-25 on a ladder)", rawname="ladder horizontal", per="/s",
    note="per axis, in Quake 2's world axes (+-25 u/s each)")
g.toggle("dash", False, P2, "no dash"); g.toggle("grapple", False, P2, "no grapple in the base game (CTF has one)")
g.fact("jumpHeight", 270**2 / 1600 * S, "m", "270^2 / (2 x 800)", "estimated", "v^2/2g; Quark: 1.22 m")
g.ground_facts(300 * S, 10, 6, 100 * S)
g.d["extras"] = ["crouch speed 100 u/s (pm_duckspeed, pmove.c#L55)", "no auto-hop (PMF_JUMP_HELD)", "air movement is weak (accel 1): little strafe-jumping compared with Quake 1"]
g.d["notes"] = "Quake II's player movement is shared client/server code (qcommon/pmove.c)."
g.src("Quake II qcommon/pmove.c", P2); g.src("Quake II game/g_save.c", Q2 + "game/g_save.c")
games.append(g.write())

g = Game("quake3", "Quake III Arena (VQ3)", 1999, "id Tech 3", "Quake family", "u", S,
         "player hull 56 u tall (MINS_Z -24, maxs 32: bg_public.h#L49, bg_pmove.c#L1293), scaled to 1.8 m: 0.03214 m/u")
P3 = Q3 + "bg_pmove.c"
g.u("gravity", 800, Q3 + "g_main.c#L143 (g_gravity 800); bg_public.h#L30 (DEFAULT_GRAVITY 800)", rawname="g_gravity", per="/s2")
g.u("speed", 320, Q3 + "g_main.c#L142 (g_speed 320)", rawname="g_speed", per="/s")
g.u("jump", 270, Q3 + "bg_local.h#L29 (JUMP_VELOCITY 270)", rawname="JUMP_VELOCITY", per="/s")
g.v("ground_accel", 10, P3 + "#L39 (pm_accelerate = 10.0f)", "exact", "Quake accelerate factor, same formula as Quark's")
g.v("friction", 6, P3 + "#L44 (pm_friction = 6.0f)", "exact")
g.u("stop_speed", 100, P3 + "#L34 (pm_stopspeed = 100.0f)", rawname="pm_stopspeed", per="/s")
air_capped(g, 1, 320, P3 + "#L40 (pm_airaccelerate = 1.0f), #L637 (PM_Accelerate with the full wish speed)",
           "VQ3 air: accel 1 toward the full wish speed (no 30 u cap), the strafe-jump that the 'strafe angle' trick exploits.")
g.v("overstrafe", 1.0, P3 + "#L637", "estimated", "no overstrafe softening")
g.u("step", 18, Q3 + "bg_local.h#L27 (STEPSIZE 18)", rawname="STEPSIZE")
g.u("hook_max", 800, P3 + "#L672-680 (PM_GrappleMove: velocity set to 800 u/s toward the hook, 10 x distance inside 100 u)", rawname="grapple pull",
    per="/s", note="the pull SETS your velocity (no acceleration, momentum not kept); the grapple is a CTF / mod weapon, not in standard play")
g.toggle("dash", False, P3, "no dash"); g.toggle("grapple", False, P3, "the grappling hook exists in the code but isn't placed in standard maps / modes")
g.fact("jumpHeight", 270**2 / 1600 * S, "m", "270^2 / (2 x 800) = 45.6 u", "estimated", "v^2/2g (higher at 125 fps from rounding in the original client); Quark: 1.22 m")
g.ground_facts(320 * S, 10, 6, 100 * S)
g.d["extras"] = ["strafe jumping / circle jumping (air accel 1 toward 320 u/s)", "crouch speed x0.25 (pm_duckScale, bg_pmove.c#L35)",
                 "no auto-hop in vanilla (jump must be re-pressed)",
                 "rocket jumping"]
g.d["notes"] = "These pm_* constants are the 'Quake engine cvars' (pm_accelerate 10, pm_airaccelerate 1, pm_friction 6, pm_stopspeed 100, g_gravity 800, jump 270, step 18). Xonotic's Q3 port uses sv_gravity 750."
g.src("Quake III Arena bg_pmove.c", P3); g.src("bg_local.h / bg_public.h", Q3 + "bg_local.h"); g.src("g_main.c (cvar defaults)", Q3 + "g_main.c")
games.append(g.write())

g = Game("cpma", "Challenge ProMode Arena (CPM physics)", 2000, "id Tech 3 (Quake III mod)", "Quake family", "u", S,
         "Quake III hull, 56 u tall, scaled to 1.8 m: 0.03214 m/u")
XC = XON + "physicsCPMA.cfg"
g.u("gravity", 800, Q3 + "g_main.c#L143 (Quake III's g_gravity 800; CPMA keeps the engine default)", conf="estimated", rawname="g_gravity", per="/s2",
    note="Xonotic's CPMA port uses sv_gravity 750")
g.u("speed", 320, XC + " (sv_maxspeed 320); " + Q3 + "g_main.c#L142", rawname="g_speed", per="/s")
g.u("jump", 270, XC + " (sv_jumpvelocity 270)", rawname="jump", per="/s")
g.v("ground_accel", 15, XC + " (sv_accelerate 15 'from cpm1_dev_docs')", "exact", "as transcribed from CPM's developer docs by Xonotic's CPMA physics port")
g.v("friction", 6, XC + " (sv_friction 6)", "exact")
g.u("stop_speed", 100, XC + " (sv_stopspeed 100)", rawname="stopspeed", per="/s")
air_capped(g, 70, 30, XC + " (sv_airstrafeaccelerate 70, sv_maxairstrafespeed 30 'from cpm1_dev_docs')",
           "CPM air with only a strafe key held (the bunny-hop strafe): accel 70 toward 30 u/s. With forward held too it is VQ3's accel 1 toward 320 u/s.")
g.u("step", 18, XC + " (sv_stepheight 18)", rawname="step")
g.toggle("dash", False, XC, "no dash"); g.toggle("grapple", False, XC, "no grapple in standard modes")
g.fact("jumpHeight", 270**2 / 1600 * S, "m", "270^2 / (2 x 800)", "estimated", "v^2/2g, before CPM's double jump; Quark: 1.22 m")
g.ground_facts(320 * S, 15, 6, 100 * S)
g.d["extras"] = ["air control 150 with W only (sv_aircontrol 150, 'from cpm1_dev_docs'): Quark's air_control is modelled on it but is a different formula (Quark 4, 'rad/s-ish')",
                 "air stop acceleration 2.5 when pushing against your velocity (sv_airstopaccelerate 2.5): Quark softens this instead (overstrafe)",
                 "double jump and ramp jumps (a jump soon after landing goes higher; numbers not sourced here)", "rocket / plasma climbing"]
g.d["notes"] = "Numbers from Xonotic's physicsCPMA.cfg, which cites the CPM developer docs line by line; CPMA's own source isn't public. Quark's air model (CPM-style W-only air control plus A/D strafing toward a small cap) is closest to this family."
g.src("Xonotic physicsCPMA.cfg (CPM numbers, from cpm1_dev_docs)", XC); g.src("Quake III g_main.c", Q3 + "g_main.c")
games.append(g.write())

SW = 1.8 / 64
g = Game("warfork", "Warsow / Warfork", 2005, "Qfusion (id Tech 2 derived)", "Quake family", "u", SW,
         "player box 64 u tall (playerbox_stand_mins z -24, maxs z 40: gs_pmove.c#L31-32), scaled to 1.8 m: 0.02813 m/u")
WP = WF + "gameshared/gs_pmove.c"; WG = WF + "gameshared/gs_public.h"
g.u("gravity", 850, WG + "#L64 (GRAVITY 850); " + WF + "game/g_main.cpp#L261 (g_gravity defaults to GRAVITY)", rawname="g_gravity", per="/s2")
g.u("speed", 320, WG + "#L69 (DEFAULT_PLAYERSPEED_STANDARD 320)", rawname="player speed", per="/s")
g.u("jump", 280 * 850 / 800, WG + "#L72 (DEFAULT_JUMPSPEED 280) x GRAVITY_COMPENSATE 850/800 (gs_pmove.c#L1806-1808)", rawname="jump 280 x 1.0625 =", per="/s")
g.v("ground_accel", 12, WP + "#L95 (pm_accelerate = 12)", "exact", "Quake accelerate factor")
g.v("friction", 8, WP + "#L91 (pm_friction = 8)", "exact")
g.u("stop_speed", 12, WP + "#L96 (pm_decelerate = 12) used as the stop speed in PM_Friction (#L485)", rawname="pm_decelerate", per="/s",
    note="Warsow's friction uses max(speed, 12 u/s): almost pure proportional friction, so stopping takes longer at the end")
air_capped(g, 70, 30, WP + "#L104-105 (pm_strafebunnyaccel = 70, pm_wishspeed = 30)", "Warsow air with only strafe held (the bunny-hop strafe): accel 70 toward 30 u/s; with forward held, accel 1 (pm_airaccelerate, #L98).")
g.u("step", 18, WG + "#L271 (STEPSIZE 18)", rawname="STEPSIZE")
g.u("ladder_speed", 250, WP + "#L89 (DEFAULT_LADDERSPEED 250)", rawname="ladder", per="/s")
g.u("dash_speed", 450, WG + "#L73 (DEFAULT_DASHSPEED 450); " + WP + "#L1142-1149", conf="estimated", rawname="dash", per="/s",
    note="Warsow's dash SETS your horizontal speed to max(450 u/s, current) with a hop (174 x 1.0625 u/s up); it isn't a timed burst like Quark's")
g.v("dash_cooldown", 1.0, WF + "game/g_main.cpp#L348 (g_pmove_dashjump_timedelay 1000 ms)", "exact", "time between dashes (also blocks walljumps for a moment)")
g.v("dash_charges", 1, WP + "#L1092-1155 (one dash, then the cooldown)", "estimated", "no charges: one dash per cooldown")
g.toggle("dash", True, WP + "#L1092", "dash (special key, on the ground)")
g.toggle("grapple", False, WP, "no grapple")
jw = 280 * 850 / 800
g.fact("jumpHeight", jw**2 / (2 * 850) * SW, "m", "297.5^2 / (2 x 850) = 52.1 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.fact("dashUpSpeed", 174 * 850 / 800 * SW, "m/s", WP + "#L107 (pm_dashupspeed 174 x GRAVITY_COMPENSATE)", "exact")
g.ground_facts(320 * SW, 12, 8, 12 * SW)
g.d["extras"] = ["walljump: 330 x 1.0625 u/s up (gs_pmove.c#L114), 1.3 s cooldown (g_pmove_walljump_timedelay 1300), failed walljump 0.7 s",
                 "air control 150 (pm_aircontrol, CPM style) and air deceleration 2 (pm_airdecelerate)", "dash jump (jump out of a dash)", "crouch slide (Warsow 2.x)"]
g.d["notes"] = "Warfork is the open continuation of Warsow; the movement code is the same gs_pmove.c. Quark's ground speed comment cites Warfork 9.0 m/s, the same conversion as here."
g.src("Warfork gs_pmove.c", WP); g.src("Warfork gs_public.h", WG); g.src("Warfork g_main.cpp (cvars)", WF + "game/g_main.cpp")
games.append(g.write())

SX = 1.8 / 69
g = Game("xonotic", "Xonotic", 2011, "DarkPlaces (Quake engine derived)", "Quake family", "u", SX,
         "player box 69 u tall (sv_player_mins z -24, sv_player_maxs z 45: xonotic-server.cfg#L85-86), scaled to 1.8 m: 0.02609 m/u")
XX = XON + "physicsX.cfg"
g.u("gravity", 800, XX + " (sv_gravity 800)", rawname="sv_gravity", per="/s2")
g.u("speed", 360, XX + " (sv_maxspeed 360)", rawname="sv_maxspeed", per="/s")
g.u("jump", 260, XX + " (sv_jumpvelocity 260)", rawname="sv_jumpvelocity", per="/s")
g.v("ground_accel", 15, XX + " (sv_accelerate 15)", "exact", "Quake accelerate factor")
g.v("friction", 6, XX + " (sv_friction 6; edgefriction 1 = none)", "exact")
g.u("stop_speed", 100, XX + " (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s")
g.v("air_accel", 1.8, XX + " (sv_airstrafeaccelerate 18, sv_airstrafeaccel_qw -0.95)", "estimated",
    "strafe-only air: accel 18 toward 100 u/s (sv_maxairstrafespeed), mostly Quake-style (airstrafeaccel_qw -0.95 blends in 5% of a speed-keeping push); with forward too: accel 2 toward 360 u/s (sv_airaccelerate 2, qw -0.8)",
    raw="sv_airstrafeaccelerate 18")
g.u("air_cap", 100, XX + " (sv_maxairstrafespeed 100)", rawname="sv_maxairstrafespeed", per="/s", note="strafe-only air wish speed: close to Quark's 2.3 / 2.53 m/s")
g.u("step", 31, XX + " (sv_stepheight 31)", rawname="sv_stepheight")
g.v("hook_cooldown", 0.2, XON + "balance-xonotic.cfg (g_balance_grapplehook_refire 0.2)", "exact", "the off-hand hook is a mutator (g_grappling_hook 0 by default)")
g.u("hook_max", 2000, XON + "balance-xonotic.cfg (g_balance_grapplehook_speed_pull 2000)", conf="estimated", rawname="speed_pull", per="/s",
    note="Xonotic's hook pulls like a rubber band (force_rubber 2000) with this pull speed; not the same model as Quark's capped pull")
g.u("dash_speed", 400, XON + "mutators.cfg (sv_dodging_horiz_force_slowest/fastest 400)", conf="estimated", rawname="dodge", per="/s",
    note="dodging is a mutator, off by default (g_dodging 0); a double-tap dodge adds 400 u/s sideways and 200 u/s up over 0.1 s")
g.v("dash_cooldown", 0.6, XON + "mutators.cfg (sv_dodging_delay 0.6)", "exact", "the dodging mutator's delay (off by default)")
g.toggle("dash", False, XON + "mutators.cfg (g_dodging 0)", "dodging exists as a mutator, off by default")
g.toggle("grapple", False, XON + "mutators.cfg (g_grappling_hook 0)", "the off-hand hook is a mutator, off by default")
g.fact("jumpHeight", 260**2 / 1600 * SX, "m", "260^2 / (2 x 800) = 42.3 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.ground_facts(360 * SX, 15, 6, 100 * SX)
g.d["extras"] = ["CPM-style air control 100 (sv_aircontrol 100, power 2) with W only", "air stop acceleration 3 (sv_airstopaccelerate 3)",
                 "step 31 u (just under 32 u stairs), steps down at speed (sv_gameplayfix_stepdown 2)", "the game ships other physics sets (physicsQ, Q3, CPMA, Warsow, Nexuiz) selectable by servers"]
g.d["notes"] = "The default ruleset (physicsX.cfg). Xonotic's 9.4 m/s run, 6.8 m/s jump, 20.9 m/s2 gravity and 2.6 m/s strafe cap are the closest of any game here to Quark's 9.5 / 7.34 / 22 / 2.3."
g.src("Xonotic physicsX.cfg", XX); g.src("Xonotic xonotic-server.cfg (player box)", XON + "xonotic-server.cfg")
g.src("Xonotic mutators.cfg", XON + "mutators.cfg"); g.src("Xonotic balance-xonotic.cfg", XON + "balance-xonotic.cfg")
games.append(g.write())

# ------------------------------------------------------------------ Source family
S72 = 1.8 / 72
HLP = HL + "pm_shared/pm_shared.c"
g = Game("hl1", "Half-Life", 1998, "GoldSrc", "Source family", "u", S72,
         "player hull 72 u tall (VEC_HULL_MIN -36 .. VEC_HULL_MAX 36: pm_shared.c#L85-86), scaled to 1.8 m: 0.025 m/u")
g.u("gravity", 800, REHLDS + "sv_phys.cpp#L49 (sv_gravity 800)", rawname="sv_gravity", per="/s2")
g.u("speed", 320, REHLDS + "sv_user.cpp#L51 (sv_maxspeed 320)", rawname="sv_maxspeed", per="/s",
    note="ReHLDS (the reverse-engineered GoldSrc engine) and Xash3D default to 320; the Valve Developer wiki's HL cvar list gives 270")
g.u("jump", math.sqrt(2 * 800 * 45), HLP + "#L2596-2601 (sqrt(2 x 800 x 45))", rawname="jump", per="/s")
g.v("ground_accel", 10, REHLDS + "sv_user.cpp#L52 (sv_accelerate 10)", "exact", "Quake accelerate factor")
g.v("friction", 4, REHLDS + "sv_phys.cpp#L52 (sv_friction 4)", "exact", "x2 near drops (edgefriction 2, sv_user.cpp#L50)")
g.u("stop_speed", 100, REHLDS + "sv_phys.cpp#L53 (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s")
air_uncapped(g, 10, 320, 30, REHLDS + "sv_main.cpp#L127 (sv_airaccelerate 10)", HLP + "#L1292 (wishspd capped at 30)", "sv_maxspeed 320")
g.u("step", 18, REHLDS + "sv_phys.cpp#L51 (sv_stepsize 18)", rawname="sv_stepsize")
g.u("ladder_speed", 200, HLP + "#L82 (MAX_CLIMB_SPEED 200)", rawname="MAX_CLIMB_SPEED", per="/s")
g.toggle("dash", False, HLP, "no dash (the long jump module is an item)")
g.toggle("grapple", False, HLP, "no grapple in multiplayer (the barnacle grapple in Opposing Force)")
g.fact("jumpHeight", 45 * S72, "m", HLP + "#L2601 (the formula's 45 u)", "exact", "Quark: 1.22 m")
g.ground_facts(320 * S72, 10, 4, 100 * S72, "; edge friction not modelled")
g.d["extras"] = ["long jump module: crouch + jump at speed sets 350 x 1.6 = 560 u/s forward and sqrt(2 x 800 x 56) up (pm_shared.c#L2589-2592)",
                 "bunny-hop limit: above 1.7 x maxspeed a jump crops speed to 0.65 x that (pm_shared.c#L2436-2465)", "crouch jumping", "edge friction x2"]
g.d["notes"] = "Engine cvar defaults from ReHLDS (a reverse-engineered GoldSrc engine; the original engine source isn't public); the game rules (pm_shared.c) from Valve's Half-Life SDK."
g.src("Half-Life SDK pm_shared.c", HLP); g.src("ReHLDS sv_user.cpp / sv_phys.cpp / sv_main.cpp", REHLDS + "sv_user.cpp")
g.src("Valve Developer Community: HL cvars", "https://developer.valvesoftware.com/wiki/HL_cvars")
games.append(g.write())

g = Game("cs16", "Counter-Strike 1.6", 2003, "GoldSrc", "Source family", "u", S72, "player hull 72 u tall (as Half-Life), scaled to 1.8 m: 0.025 m/u")
RP = REGAME + "pm_shared/pm_shared.cpp"
g.u("gravity", 800, REHLDS + "sv_phys.cpp#L49 (sv_gravity 800)", rawname="sv_gravity", per="/s2")
g.u("speed", 250, REGAME + "dlls/weapons.h#L1178 (KNIFE_MAX_SPEED 250)", rawname="knife max speed", per="/s", note="knife out; rifles are slower (AK-47 221, AWP 210)")
g.u("jump", math.sqrt(2 * 800 * 45), RP + "#L2629 (sqrt(2 x 800 x 45))", rawname="jump", per="/s")
g.v("ground_accel", 10, REHLDS + "sv_user.cpp#L52 (sv_accelerate 10)", "exact", "engine default; many servers and configs run 5")
g.v("friction", 4, REHLDS + "sv_phys.cpp#L52 (sv_friction 4); Valve wiki Sv_friction (CS 1.6: 4)", "exact", "x2 near drops (edgefriction 2)")
g.u("stop_speed", 100, REHLDS + "sv_phys.cpp#L53 (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s", note="engine default; competitive configs often use 75")
air_uncapped(g, 10, 250, 30, REHLDS + "sv_main.cpp#L127 (sv_airaccelerate 10)", RP + " (air cap 30, as Half-Life)", "knife speed 250")
g.u("step", 18, REHLDS + "sv_phys.cpp#L51 (sv_stepsize 18)", rawname="sv_stepsize")
g.u("ladder_speed", 200, REGAME + "pm_shared/pm_shared.h#L54 (MAX_CLIMB_SPEED 200)", rawname="MAX_CLIMB_SPEED", per="/s")
g.toggle("dash", False, RP, "no dash"); g.toggle("grapple", False, RP, "no grapple")
g.fact("jumpHeight", 45 * S72, "m", RP + "#L2629 (45 u)", "exact", "Quark: 1.22 m")
g.ground_facts(250 * S72, 10, 4, 100 * S72, "; edge friction not modelled")
g.d["extras"] = ["jump penalty: each jump sets a 1315.8 ms timer that cuts the next jump's speed to as little as 75% and slows you after landing (pm_shared.cpp#L2812-2820, #L1060)",
                 "bunny-hop limit: above 1.2 x maxspeed a jump crops speed to 0.8 x that (pm_shared.h#L74, pm_shared.cpp#L2596-2610)",
                 "weapon-dependent run speed", "crouch jumping, ladder strafing"]
g.d["notes"] = "Game code from ReGameDLL_CS (a reverse-engineered CS 1.6 game library), engine cvars from ReHLDS."
g.src("ReGameDLL_CS pm_shared.cpp / pm_shared.h / weapons.h", RP); g.src("ReHLDS cvars", REHLDS + "sv_phys.cpp")
g.src("Valve Developer Community: Sv_friction", "https://developer.valvesoftware.com/wiki/Sv_friction")
games.append(g.write())

for gid, name, year, eng, extra_note in [("csgo", "Counter-Strike: Global Offensive", 2012, "Source", ""),
                                          ("cs2", "Counter-Strike 2", 2023, "Source 2", " CS2 runs these on sub-tick input timing.")]:
    g = Game(gid, name, year, eng, "Source family", "u", S72, "player hull 32 x 32 x 72 u (CS:GO Mapper's Reference), scaled to 1.8 m: 0.025 m/u")
    TC = "https://totalcsgo.com/commands/"
    g.u("gravity", 800, "https://totalcsgo.com/command/svgravity (sv_gravity 800)", rawname="sv_gravity", per="/s2")
    g.u("speed", 250, "https://www.vlr.gg/416153/is-valorant-movement-slower-than-cs2 (knife 250 u/s); sv_maxspeed 320 is only the cap (https://csdb.gg/commands/movement/)", conf="measured",
        rawname="knife speed", per="/s", note="knife out (community-stated; the game's weapon data, not a cvar)")
    g.u("jump", 301.993377, "https://www.funplay.pro/en/convars/sv_jump_impulse (sv_jump_impulse 301.9933)", rawname="sv_jump_impulse", per="/s")
    g.v("ground_accel", 5.5, TC + "svaccelerate (sv_accelerate 5.5)", "exact", "Source accelerate factor (same formula as Quark's); the page's intro also says 5.6")
    g.v("friction", 5.2, TC + "svfriction (sv_friction 5.2); https://developer.valvesoftware.com/wiki/Sv_friction (CS:GO 5.2)", "exact")
    g.u("stop_speed", 80, TC + "svstopspeed (sv_stopspeed 80)", rawname="sv_stopspeed", per="/s")
    air_uncapped(g, 12, 250, 30, TC + "svairaccelerate (sv_airaccelerate 12)", SDK + "shared/gamemovement.h#L104 (Source's GetAirSpeedCap 30)", "knife speed 250")
    g.u("step", 18, "https://developer.valvesoftware.com/wiki/CS:GO_Mapper's_Reference (step 18 u)", rawname="step")
    g.toggle("dash", False, "https://csdb.gg/commands/movement/", "no dash"); g.toggle("grapple", False, "https://csdb.gg/commands/movement/", "no grapple")
    g.fact("jumpHeight", 301.993377**2 / 1600 * S72, "m", "301.99^2 / (2 x 800) = 57 u", "estimated", "v^2/2g; the Mapper's Reference measures 54-55 u blocks cleared standing, 65-66 crouch-jumping; Quark: 1.22 m")
    g.ground_facts(250 * S72, 5.5, 5.2, 80 * S72)
    g.d["extras"] = ["stamina: each jump costs sv_staminajumpcost 0.08 (sv_stamina_max 80), lowering consecutive jumps (https://csdb.gg/commands/movement/)",
                     "bunny hopping speed-capped (sv_enablebunnyhopping 0, no auto-hop: sv_autobunnyhopping 0)",
                     "counter-strafing: low accel 5.5 and friction 5.2 make stopping a skill", "crouch jumping, weapon-dependent speeds"]
    g.d["notes"] = "CS:GO and CS2 share these defaults." + extra_note
    g.src("Total CS command pages (cvar defaults)", TC + "svaccelerate"); g.src("csdb.gg movement commands", "https://csdb.gg/commands/movement/")
    g.src("Valve Developer Community: CS:GO Mapper's Reference", "https://developer.valvesoftware.com/wiki/CS:GO_Mapper's_Reference")
    games.append(g.write())

MV = SDK + "shared/movevars_shared.cpp"; GM = SDK + "shared/gamemovement.cpp"
g = Game("source", "Source engine defaults (SDK 2013)", 2004, "Source", "Source family", "u", S72,
         "player hull 32 x 32 x 72 u (gamerules.cpp#L50-51), scaled to 1.8 m: 0.025 m/u")
g.u("gravity", 800, MV + "#L22 (DEFAULT_GRAVITY_STRING 800; 600 in HL2)", rawname="sv_gravity", per="/s2")
g.u("speed", 320, MV + "#L54 (sv_maxspeed 320)", rawname="sv_maxspeed", per="/s", note="the server cap: each game sets its own player speed")
g.u("jump", 268.3281573, GM + "#L2438 (flMul = 268.33, i.e. 45 u at 800)", rawname="jump", per="/s")
g.v("ground_accel", 10, MV + "#L64 (sv_accelerate 10)", "exact", "Quake accelerate factor (gamemovement.cpp#L1841)")
g.v("friction", 4, MV + "#L88 (sv_friction 4)", "exact", "gamemovement.cpp#L1634-1660; sv_edgefriction is not used by the base movement")
g.u("stop_speed", 100, MV + "#L42 (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s")
air_uncapped(g, 10, 320, 30, MV + "#L77 (sv_airaccelerate 10)", SDK + "shared/gamemovement.h#L104 (GetAirSpeedCap 30)", "sv_maxspeed 320")
g.u("step", 18, MV + "#L100 (sv_stepsize 18)", rawname="sv_stepsize")
g.u("ladder_speed", 200, SDK + "shared/shareddefs.h#L103 (MAX_CLIMB_SPEED 200)", rawname="MAX_CLIMB_SPEED", per="/s")
g.toggle("dash", False, GM, "no dash"); g.toggle("grapple", False, GM, "no grapple")
g.fact("jumpHeight", 45 * S72, "m", GM + "#L2438 (268.33 = sqrt(2 x 800 x 45))", "exact", "Quark: 1.22 m")
g.ground_facts(320 * S72, 10, 4, 100 * S72)
g.d["extras"] = ["surfing (air strafing on steep ramps)", "crouch jump", "jump boost from the surface's jumpFactor"]
g.d["notes"] = "The base CGameMovement and movevars every Source game starts from (sv_accelerate, sv_airaccelerate, sv_friction, sv_stopspeed, sv_gravity, sv_maxspeed, jump, sv_stepsize)."
g.src("Source SDK 2013 movevars_shared.cpp", MV); g.src("Source SDK 2013 gamemovement.cpp / .h", GM); g.src("gamerules.cpp (hull)", SDK + "shared/gamerules.cpp")
games.append(g.write())

g = Game("hl2", "Half-Life 2", 2004, "Source", "Source family", "u", S72, "player hull 72 u tall (gamerules.cpp#L50-51), scaled to 1.8 m: 0.025 m/u")
HP = SDK + "server/hl2/hl2_player.cpp"
g.u("gravity", 600, MV + "#L20 (DEFAULT_GRAVITY_STRING 600 for HL2)", rawname="sv_gravity", per="/s2")
g.u("speed", 190, HP + "#L87 (hl2_normspeed 190)", rawname="hl2_normspeed", per="/s", note="normal run; sprint 320 (hl2_sprintspeed, suit power), walk 150")
g.u("jump", 160, GM + "#L2435 (flMul = 160, 'approx. 21 units')", rawname="jump", per="/s")
g.v("ground_accel", 10, MV + "#L64 (sv_accelerate 10)", "exact", "Quake accelerate factor")
g.v("friction", 4, MV + "#L88 (sv_friction 4)", "exact")
g.u("stop_speed", 100, MV + "#L42 (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s")
air_uncapped(g, 10, 190, 30, MV + "#L77 (sv_airaccelerate 10)", SDK + "shared/gamemovement.h#L104 (GetAirSpeedCap 30)", "hl2_normspeed 190")
g.u("step", 18, MV + "#L100 (sv_stepsize 18)", rawname="sv_stepsize")
g.u("ladder_speed", 200, SDK + "shared/shareddefs.h#L103 (MAX_CLIMB_SPEED 200)", rawname="MAX_CLIMB_SPEED", per="/s")
g.toggle("dash", False, HP, "no dash (sprint instead)"); g.toggle("grapple", False, HP, "no grapple")
g.fact("jumpHeight", 160**2 / 1200 * S72, "m", "160^2 / (2 x 600) = 21.3 u", "estimated", "v^2/2g; crouch-jumping adds the 36 u the hull shrinks; Quark: 1.22 m")
g.fact("sprintSpeed", 320 * S72, "m/s", HP + "#L88 (hl2_sprintspeed 320)", "exact")
g.fact("walkSpeed", 150 * S72, "m/s", HP + "#L86 (hl2_walkspeed 150)", "exact")
g.ground_facts(190 * S72, 10, 4, 100 * S72)
g.d["extras"] = ["sprint (suit power)", "single-player jumps add forward speed (gamemovement.cpp#L2465+: the source of accelerated back-hopping)", "crouch jumping", "gravity gun prop climbing"]
g.d["notes"] = "Single player numbers (HL2_DLL build of the SDK)."
g.src("Source SDK 2013 hl2_player.cpp", HP); g.src("movevars_shared.cpp", MV); g.src("gamemovement.cpp", GM)
games.append(g.write())

STF = 1.8 / 82
TFG = SDK + "shared/tf/tf_gamemovement.cpp"
for gid, name, spd, spsrc, spconf, spnote in [
        ("tf2", "Team Fortress 2 (Soldier)", 240, "https://strategywiki.org/wiki/Team_Fortress_2/Soldier ('Speed: 80% (240HU/s)')", "measured", "Soldier, the rocket-jumping class; Pyro/Engineer/Sniper 300, Medic/Spy 320, Heavy 230, Demoman 280"),
        ("tf2-scout", "Team Fortress 2 (Scout)", 400, "https://strategywiki.org/wiki/Team_Fortress_2/Scout (133% of normal speed) x 300 HU/s (240 / 0.8)", "estimated", "Scout: 133% of the 300 HU/s normal speed")]:
    g = Game(gid, name, 2007, "Source", "Source family", "u", STF, "player hull 48 x 48 x 82 u (tf_gamerules.cpp#L1345-1346), scaled to 1.8 m: 0.02195 m/u")
    g.u("gravity", 800, MV + "#L22 (sv_gravity 800); " + TFG + " (Assert gravity == 800)", rawname="sv_gravity", per="/s2")
    g.u("speed", spd, spsrc, conf=spconf, rawname="class speed", per="/s", note=spnote)
    g.u("jump", 289, TFG + "#L1160+ (CheckJumpButton: flMul = 289)", rawname="jump", per="/s")
    g.v("ground_accel", 10, MV + "#L64 (sv_accelerate 10)", "exact", "Quake accelerate factor")
    g.v("friction", 4, MV + "#L88 (sv_friction 4)", "exact")
    g.u("stop_speed", 100, MV + "#L42 (sv_stopspeed 100)", rawname="sv_stopspeed", per="/s")
    air_uncapped(g, 10, spd, 30, MV + "#L77 (sv_airaccelerate 10)", SDK + "shared/gamemovement.h#L104 (GetAirSpeedCap 30, used by tf_gamemovement.cpp#L2033+)", f"class speed {spd}")
    g.u("step", 18, MV + "#L100 (sv_stepsize 18)", rawname="sv_stepsize")
    g.u("hook_max", 750, TFG + "#L63 (tf_grapplinghook_move_speed 750)", rawname="tf_grapplinghook_move_speed", per="/s",
        note="the grappling hook exists only in Mannpower mode")
    g.toggle("dash", False, TFG, "no dash")
    g.toggle("grapple", False, TFG, "grapple only in Mannpower mode")
    g.fact("jumpHeight", 289**2 / 1600 * STF, "m", "289^2 / (2 x 800) = 52.2 u", "estimated", "v^2/2g; Quark: 1.22 m")
    g.ground_facts(spd * STF, 10, 4, 100 * STF)
    g.d["extras"] = ["bunny-hop limit: a jump above 1.2 x max speed crops speed to that (tf_gamemovement.cpp#L1087-1110, PreventBunnyJumping)", "crouch jump", "rocket / sticky jumping"]
    if gid == "tf2-scout":
        g.fact("doubleJumpUp", 268.3281573 * STF, "m/s", TFG + " (AirDash: flDashZ = 268.33)", "exact", "the double jump sets your velocity to the held direction and 268.33 u/s up")
        g.d["extras"].insert(0, "double jump: in the air, jump again to SET your velocity to the held direction plus 268.33 u/s up (tf_gamemovement.cpp, AirDash)")
    g.d["notes"] = "Team Fortress 2 game movement from the 2025 Source SDK 2013 release; class speeds from the game's class data (not in the SDK), via StrategyWiki."
    g.src("Source SDK 2013 tf_gamemovement.cpp", TFG); g.src("tf_gamerules.cpp (hull)", SDK + "shared/tf/tf_gamerules.cpp"); g.src("StrategyWiki TF2 classes", spsrc.split(" ")[0])
    games.append(g.write())

# ------------------------------------------------------------------ movement shooters
g = Game("titanfall2", "Titanfall 2 (multiplayer pilot)", 2016, "Source (Respawn)", "movement shooters", "u", S72,
         "pilot hull 72 u tall (pilot_base.set: stand hull_max 16 16 72), scaled to 1.8 m: 0.025 m/u (Respawn units are inches: 72 in = 1.83 m)")
PM = TF2T + "mp/pilot_mp.set"; PB = TF2T + "mp/pilot_base.set"
g.u("gravity", 750 * 0.8, "https://docs.northstar.tf/Wiki/using-northstar/commands/ (sv_gravity 750) x " + PM + " (gravityscale 0.80)", rawname="750 x 0.8 =", per="/s2")
g.u("speed", 260, PM + " (stand sprintspeed 260)", rawname="sprintspeed", per="/s", note="sprint, the normal way to move; run 173.5 u/s (stand speed)")
g.u("jump", math.sqrt(2 * 600 * 60), PM + " (jumpheight 60) with gravity 600", conf="estimated", rawname="sqrt(2 x 600 x 60) =", per="/s", note="take-off speed for the stated 60 u jump height")
g.u("air_cap", 60, GH + "/taskinoz/Titanfall-2-Wiki/blob/master/player-vars.txt (airSpeed 60)", conf="estimated", rawname="airSpeed", per="/s",
    note="a server dump of the player entity; read as Source's air wish cap")
g.v("air_accel", 500 * S72 / (60 * S72) / 10, GH + "/taskinoz/Titanfall-2-Wiki/blob/master/player-vars.txt (airAcceleration 500)", "estimated",
    "if airAcceleration is an absolute 500 u/s2 toward the 60 u/s cap: Quark-equivalent 500 / 60 / 10; the engine's formula isn't public", raw="airAcceleration 500")
g.u("step", 18, PM + " (stepheight 18)", rawname="stepheight")
g.u("hook_max", 420, PB + " (grapple_airSpeedMax 420)", conf="estimated", rawname="grapple_airSpeedMax", per="/s", note="the grapple's top speed while pulled (Grapple tactical)")
g.u("hook_accel", 650, PB + " (grapple_airAccel 650)", conf="estimated", rawname="grapple_airAccel", per="/s2")
g.toggle("dash", False, PM, "no dash in the base kit (Phase Shift / Pulse Blade are other tacticals)")
g.toggle("grapple", True, PB, "the Grapple tactical")
g.fact("jumpHeight", 60 * S72, "m", PM + " (jumpheight 60)", "exact", "Quark: 1.22 m")
g.fact("runSpeed", 173.5 * S72, "m/s", PM + " (stand speed 173.5)", "exact")
g.fact("crouchSpeed", 80 * S72, "m/s", PM + " (crouch speed 80)", "exact")
g.fact("wallrunMaxSpeed", 420 * S72, "m/s", PM + " (wallrunMaxSpeedHorizontal 420)", "exact")
g.fact("ziplineSpeed", 600 * S72, "m/s", PM + " (ziplineSpeed 600)", "exact")
g.d["extras"] = ["wallrun: up to 420 u/s horizontal, 1.75 s limit (wallrun_timeLimit), jump-off 205 out / 230 up", "double jump (doubleJump 1, 60 u)",
                 "slide (slidedecel 50, slidevelocitydecay 0.7)", "zipline 600 u/s", "grapple detach boosts (grapple_detachVerticalBoost 200)"]
g.d["notes"] = "From the game's player settings files (as extracted in a public repository) and a server dump; air movement semantics are not public, so the air values are estimates."
g.src("Titanfall 2 pilot_mp.set", PM); g.src("Titanfall 2 pilot_base.set", PB); g.src("Northstar commands (sv_gravity)", "https://docs.northstar.tf/Wiki/using-northstar/commands/")
g.src("Titanfall-2-Wiki player-vars.txt", GH + "/taskinoz/Titanfall-2-Wiki/blob/master/player-vars.txt")
games.append(g.write())

g = Game("apex", "Apex Legends", 2019, "Source (Respawn)", "movement shooters", "u", S72,
         "assumed like Titanfall 2: a 72 u (inch) tall hull scaled to 1.8 m, 0.025 m/u (the wiki converts at 1 u = 1 inch = 0.0254 m, 1.6% more)")
AW = "https://apexlegends.wiki.gg/wiki/Sliding"
g.u("speed", 299, AW + " (sprinting 299 u/s, 7.59 m/s)", conf="measured", rawname="sprint", per="/s", note="sprint; walk 199.53 u/s, crouch-walk 92 u/s; -13% holding a weapon")
g.v("hook_range", 21, "https://apexlegends.wiki.gg/wiki/Pathfinder (grappling hook max length 21 m)", "measured", "Pathfinder's Grappling Hook (tactical); the wiki lists a 20 s cooldown and 2 charges")
g.toggle("dash", False, AW, "no dash for most legends (Octane / Horizon / Pathfinder have movement abilities)")
g.toggle("grapple", False, "https://apexlegends.wiki.gg/wiki/Pathfinder", "Pathfinder only")
g.fact("walkSpeed", 199.53 * S72, "m/s", AW + " (walking 199.53)", "measured")
g.fact("crouchSpeed", 92 * S72, "m/s", AW + " (crouchwalking 92)", "measured")
g.fact("slideTime", 1, "s", AW + " (about 1 s on flat ground before it slows)", "measured")
g.fact("superglideBoost", 10.3, "m/s", "https://apexlegends.fandom.com/wiki/Movement", "measured")
g.fact("climbHeight", 6, "m", "https://apexlegends.fandom.com/wiki/Movement (wall climb about 6 m)", "measured")
g.d["extras"] = ["slide (about 1 s on flat ground, longer downhill)", "wall climb about 6 m, mantle", "superglide (10.3 m/s), tap-strafe", "ziplines, jump pads"]
g.d["notes"] = "Apex's data files aren't public; numbers are community measurements. The Fandom wiki converts the same 299 u/s at 1 u = 0.75 in (5.7 m/s); this file uses the player-height scale."
g.src("Apex Legends Wiki (wiki.gg): Sliding", AW); g.src("Apex Legends Wiki (wiki.gg): Pathfinder", "https://apexlegends.wiki.gg/wiki/Pathfinder")
g.src("Apex Legends Wiki (Fandom): Movement", "https://apexlegends.fandom.com/wiki/Movement")
games.append(g.write())

# ------------------------------------------------------------------ hero / tactical shooters
g = Game("overwatch2", "Overwatch 2", 2022, "Overwatch engine", "hero/tactical shooters", "m", 1.0,
         "the game documents metres; heroes vary in size, so values are as given (not rescaled to 1.8 m)")
OW = "https://overwatch.fandom.com/wiki/"
g.v("speed", 5.5, OW + "Character (base move speed 5.5 m/s)", "measured", "most heroes; Genji and Tracer 6 m/s; backwards -10%")
g.v("dash_charges", 3, OW + "Tracer (Blink: 3 charges)", "measured", "Tracer's Blink: a 7.5 m instant teleport, not a burst")
g.v("dash_cooldown", 0.1, OW + "Tracer (Blink: 0.1 s between uses)", "measured", "between blinks; each charge recharges in 3 s (Quark 1.2 s: x2.5), one multiplier can't express both")
g.v("hook_range", 20, OW + "Widowmaker (Grappling Hook: 20 m)", "measured", "Widowmaker's Grappling Hook")
g.v("hook_max", 20, OW + "Widowmaker (Grappling Hook: 20 m/s)", "measured", "pulls at a set speed, not an acceleration")
g.v("hook_cooldown", 10, OW + "Widowmaker (Grappling Hook: 10 s)", "measured")
g.toggle("dash", False, OW + "Character", "hero abilities only (Tracer, Genji...)")
g.toggle("grapple", False, OW + "Widowmaker", "hero abilities only (Widowmaker, Ashe...)")
g.fact("jumpHeight", 0.98, "m", OW + "Character (jump height 0.98 m)", "measured", "Quark: 1.22 m")
g.fact("crouchSpeed", 3, "m/s", OW + "Character", "measured")
g.fact("blinkDistance", 7.5, "m", OW + "Tracer", "measured", "Quark's dash covers 2.84 m")
g.d["extras"] = ["hero-specific movement abilities (blinks, dashes, wall-ride, jet packs, double jumps)", "no air strafing to speak of"]
g.d["notes"] = "Gravity and acceleration aren't documented, so jump take-off speed is left out (jump height only). The dash and hook rows are single heroes (Tracer, Widowmaker)."
g.src("Overwatch Wiki: Character", OW + "Character"); g.src("Overwatch Wiki: Tracer", OW + "Tracer"); g.src("Overwatch Wiki: Widowmaker", OW + "Widowmaker")
games.append(g.write())

g = Game("valorant", "Valorant", 2020, "Unreal Engine 4", "hero/tactical shooters", "m", 1.0, "the game documents metres; values as given")
g.v("speed", 6.75, "https://valorant.fandom.com/wiki/Melee (run speed 6.75 m/s with the melee)", "measured", "knife out; rifles slower (Vandal 5.4 m/s)")
g.toggle("dash", False, "https://valorant.fandom.com/wiki/Melee", "agent abilities only (Jett's dash, Neon's slide)")
g.toggle("grapple", False, "https://valorant.fandom.com/wiki/Melee", "no grapple")
g.fact("vandalSpeed", 5.4, "m/s", "https://liquipedia.net/valorant/Vandal (movement speed 5.4 m/sec)", "measured")
g.d["extras"] = ["agent movement abilities", "running inaccuracy (a tactical shooter: speed costs accuracy)"]
g.d["notes"] = "Only run speeds are documented; gravity, jump and acceleration aren't published."
g.src("Valorant Wiki: Melee", "https://valorant.fandom.com/wiki/Melee"); g.src("Liquipedia: Vandal", "https://liquipedia.net/valorant/Vandal")
games.append(g.write())

# ------------------------------------------------------------------ arena shooters
SU = 1.8 / 78
UTM = "https://unrealarchive.org/wikis/unreal-wiki/Legacy:Movement_Metrics_(UT).html"
g = Game("ut99", "Unreal Tournament (1999)", 1999, "Unreal Engine 1", "arena shooters", "UU", SU,
         "player cylinder 78 UU tall (CollisionHeight 39 x 2, Unreal Wiki: General Scale And Dimensions), scaled to 1.8 m: 0.02308 m/UU")
g.u("speed", 400, UTM + " (running 400 UU/s, Classic)", conf="measured", rawname="ground speed", per="/s", note="Classic speed; Hardcore (x1.1 game speed) 440, Turbo 560")
g.u("dash_speed", 600, UTM + " (dodge speed 600 UU/s)", conf="estimated", rawname="dodge", per="/s", note="UT's dodge (double-tap): a fixed 600 UU/s hop, 23 UU high; mapped to Quark's dash burst")
g.toggle("dash", True, UTM, "the dodge")
g.toggle("grapple", False, UTM, "no grapple (the translocator teleports)")
g.fact("jumpHeight", 55 * SU, "m", UTM + " (55 UU, Classic running)", "measured", "Hardcore 67 UU; Quark: 1.22 m")
g.fact("dodgeHeight", 23 * SU, "m", UTM + " (23 UU)", "measured")
g.fact("dodgeDistance", 237 * SU, "m", UTM + " (237 UU)", "measured", "Quark's dash covers 2.84 m")
g.fact("airControl", "35%", "", UTM, "measured", "UT's AirControl is a share of ground acceleration in the air; Quark's air_control isn't the same thing")
g.d["extras"] = ["dodging (double-tap a direction)", "impact hammer jumps", "translocator", "jump boots"]
g.d["notes"] = "Measured by the Unreal Wiki community (UnrealScript defaults not checked here). Gravity and JumpZ aren't on the page, so jump take-off speed is left out."
g.src("Unreal Wiki: Movement Metrics (UT)", UTM); g.src("Unreal Wiki: General Scale And Dimensions", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:General_Scale_And_Dimensions.html")
games.append(g.write())

HWU = 1.8 / 0.70127
HS = "https://nikon.bungie.org/misc/hek_tutorial/references/general/halo_player_stats.html"
g = Game("halo-ce", "Halo: Combat Evolved (MCC)", 2001, "Blam!", "arena shooters", "WU", HWU,
         "Master Chief stands 70.127 HEK (3ds Max) units = 0.70127 world units (100 HEK units per WU), scaled to 1.8 m: 2.567 m/WU")
g.u("speed", 2.25, HS + " (forward running 2.25 WU/s)", conf="estimated", rawname="run", per="/s", note="backward / strafe 2.0 WU/s; conversion assumes 100 HEK units per world unit")
g.u("step", 0.12, HS + " (step 12 HEK units standing)", conf="estimated", rawname="step")
g.toggle("dash", False, HS, "no dash"); g.toggle("grapple", False, HS, "no grapple (Halo Infinite adds the Grappleshot)")
g.fact("jumpReach", 0.80 * HWU, "m", HS + " (standing jump clears 80 HEK units; crouch jump 99)", "estimated", "the highest ledge cleared, not the take-off speed")
g.d["extras"] = ["crouch jumping (99 units)", "slow, floaty jumps relative to the body"]
g.d["notes"] = "From the Halo Editing Kit reference. Gravity and acceleration aren't listed."
g.src("HEK reference: Halo player stats", HS)
games.append(g.write())

# ------------------------------------------------------------------ other
MC = "https://minecraft.fandom.com/wiki/"
TC2 = GH + "/ddevault/TrueCraft/wiki/Entity-Movement-And-Physics"
g = Game("minecraft", "Minecraft (Java Edition)", 2011, "Minecraft", "other", "block", 1.0,
         "1 block = 1 m and the player is 1.8 blocks tall, so the scale is already Quark's (1 m/block)")
g.v("speed", 5.612, MC + "Sprinting (5.612 m/s)", "measured", "sprinting; walking 4.317 m/s")
g.v("gravity", 0.08 * 20 * 20, TC2 + " (0.08 blocks/tick^2 at 20 ticks/s)", "estimated",
    "0.08 b/tick each tick with 0.98 vertical drag per tick (terminal 78.4 m/s): about 32 m/s2 before drag", raw="0.08 blocks/tick^2")
g.v("jump", 0.42 * 20, TC2 + " (jump adds 0.42 blocks/tick)", "exact", "0.42 b/tick x 20", raw="0.42 blocks/tick")
g.v("friction", -math.log(0.6 * 0.91) * 20, MC + "Player (ground friction 0.546 per tick)", "estimated",
    "Minecraft multiplies ground speed by 0.546 each tick (slipperiness 0.6 x 0.91): an exponential decay of 12.1/s, which is what Quark's friction is above stop_speed", raw="x0.546 per tick")
g.v("ladder_speed", 2.35, MC + "Ladder (about 2.35 m/s up)", "measured", "down 3 m/s")
g.toggle("dash", False, MC + "Player", "no dash"); g.toggle("grapple", False, MC + "Player", "no grapple")
g.fact("jumpHeight", 1.2522, "m", MC + "Player (about 1.2522 blocks)", "measured", "Quark: 1.22 m")
g.fact("walkSpeed", 4.317, "m/s", MC + "Walking", "measured")
g.fact("sprintJumpSpeed", 7.127, "m/s", MC + "Sprinting (sprint-jumping average 7.127 m/s)", "measured")
g.fact("terminalVelocity", 78.4, "m/s", MC + "Player", "measured")
g.d["extras"] = ["air drag x0.91 per tick horizontally and x0.98 vertically (Quark has no drag)", "sprint jumping", "sneaking at 1.295 m/s, elytra, swimming"]
g.d["notes"] = "Minecraft's player happens to be 1.8 m, Quark's height. Its 20 Hz tick model (per-tick velocity multipliers) differs from Quake's, so friction and gravity are equivalents. Step-up height isn't sourced here."
g.src("Minecraft Wiki: Player", MC + "Player"); g.src("Minecraft Wiki: Sprinting", MC + "Sprinting"); g.src("Minecraft Wiki: Ladder", MC + "Ladder")
g.src("TrueCraft wiki: Entity Movement And Physics", TC2)
games.append(g.write())

SC = 1.8 / 11
g = Game("celeste", "Celeste", 2018, "Monocle (XNA / FNA)", "other", "px", SC,
         "Madeline's hitbox is 11 px tall (Player.cs: normalHitbox 8 x 11), scaled to 1.8 m: 0.1636 m/px. A 2D platformer: compare ratios (dash / run) more than raw speeds")
g.u("gravity", 900, CEL + "#L24 (Gravity = 900f)", rawname="Gravity", per="/s2", note="halved near the apex while jump is held (HalfGravThreshold 40)")
g.u("speed", 90, CEL + "#L30 (MaxRun = 90f)", rawname="MaxRun", per="/s")
g.u("jump", 105, CEL + "#L49 (JumpSpeed = -105f)", rawname="JumpSpeed", per="/s", note="held for up to 0.2 s while jump is held (VarJumpTime), so jumps are much higher than v^2/2g")
g.u("dash_speed", 240, CEL + "#L75 (DashSpeed = 240f)", rawname="DashSpeed", per="/s", note="2.67 x run speed (Quark: 24 / 9.5 = 2.53 x)")
g.v("dash_time", 0.15, CEL + "#L78 (DashTime = .15f)", "exact")
g.u("dash_exit_min", 160, CEL + "#L76 (EndDashSpeed = 160f)", rawname="EndDashSpeed", per="/s", note="the speed a dash ends at (Quark's exit is at least 11 m/s)")
g.v("dash_cooldown", 0.2, CEL + "#L79 (DashCooldown = .2f)", "exact")
g.v("dash_charges", 1, CEL, "exact", "one dash, refilled on landing (two with the second crystal heart power-up)")
g.v("super_dash_jump", 0.5, CEL + "#L46 (DuckSuperJumpYMult = .5f)", "exact", "the hyper (a super jump out of a crouched dash) jumps at half the jump speed: Quark's super dash hop starts at the same 0.5")
g.toggle("dash", True, CEL, "the dash"); g.toggle("grapple", False, CEL, "no grapple")
g.fact("superJumpSpeed", 260 * SC, "m/s", CEL + "#L69 (SuperJumpH = 260f)", "exact", "the super (dash jump) horizontal speed: 2.89 x run (Quark's dash jump leaves at the 24 m/s burst, 2.53 x)")
g.fact("hyperSpeed", 260 * 1.25 * SC, "m/s", CEL + "#L45 (DuckSuperJumpXMult = 1.25f)", "exact", "the hyper: 325 px/s, 3.6 x run (Quark's super dash: carried speed + 5 m/s)")
g.fact("jumpHeight", 28 * SC, "m", "derived from JumpSpeed, VarJumpTime, Gravity, HalfGravThreshold", "estimated", "about 28 px (21 px held + 7 px coasting) = 2.5 x her height; Quark: 1.22 m = 0.68 x")
g.d["extras"] = ["super / hyper / wavedash (dash techs Quark's dash jump and super dash are modelled on)", "climbing with stamina", "wall jumps", "coyote time 0.1 s (JumpGraceTime)"]
g.d["notes"] = "Celeste's Player.cs was published by its developers. A 2D game: its gravity and jumps are far beyond a shooter's; the dash numbers are the useful comparison."
g.src("Celeste Player.cs (NoelFB/Celeste)", CEL)
games.append(g.write())

# ------------------------------------------------------------------ index
CORE = ["gravity", "speed", "jump", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "step"]
def closeness(gd):
    ks = [k for k in CORE if k in gd["values"]]
    if not ks: return None, 0
    return round(sum(abs(math.log(gd["values"][k]["mult"])) for k in ks if gd["values"][k]["mult"] > 0) / len(ks), 3), len(ks)
order = ["Quake family", "Source family", "arena shooters", "hero/tactical shooters", "movement shooters", "other"]
descs = {"Quake family": "Quake-engine and id-derived games: Quake-style acceleration and friction, the model Quark's pmove uses",
         "Source family": "GoldSrc and Source: Quake-style movement with a 30 u air cap and full-wish-speed air acceleration",
         "arena shooters": "other arena shooters (Unreal, Halo): different movement models, community-measured",
         "hero/tactical shooters": "hero and tactical shooters with documented metric speeds",
         "movement shooters": "wallrun / slide / grapple shooters (Respawn)",
         "other": "not shooters, for scale: Minecraft (a 1.8 m player) and Celeste (Quark's dash techs come from it)"}
index = {"version": 1, "quark": PP.get("version"), "units": "metres, seconds (Quark's); mult = value / Quark's base",
         "confidence": {"exact": "read from source code, a shipped config or a cvar default (converted by the stated scale)",
                        "measured": "a community wiki / measurement, not the code",
                        "estimated": "derived here (an equivalence between different formulas, a v^2/2g, a simulation, an assumed scale)"},
         "groups": []}
for gn in order:
    lst = []
    for gd in games:
        if gd["group"] != gn: continue
        c, n = closeness(gd)
        ent = {"id": gd["id"], "name": gd["name"], "year": gd["year"], "engine": gd["engine"], "file": gd["id"] + ".json",
               "values": len([k for k in gd["values"] if k not in ("dash", "grapple")])}
        if c is not None and n >= 5: ent["distance"] = c; ent["distanceKeys"] = n
        lst.append(ent)
    index["groups"].append({"name": gn, "desc": descs[gn], "games": lst})
index["distance"] = "mean |ln(mult)| over the core keys the game maps (" + ", ".join(CORE) + "), given when it maps 5 or more: 0 = Quark's standard; lower is closer"
json.dump(index, open(os.path.join(OUT, "index.json"), "w"), indent=1); open(os.path.join(OUT, "index.json"), "a").write("\n")
for gr in index["groups"]:
    for e in gr["games"]: print(f"{e['id']:12} {e['values']:3} values  distance {e.get('distance')} over {e.get('distanceKeys')}")
