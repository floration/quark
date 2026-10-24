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
AMT = "https://apexmovement.tech/wiki/tech/"
AMG = AMT + "General%20Tech%3EFundamentals%3EGround%3EFundamental%20Ground%20Movement%20article"
AMA = AMT + "General_Tech%3EFundamentals%3EAir%3EFundamental_Air_Tech_article"
# Apex Movement Wiki: a 56 u jump with 0.74 s airtime on flat ground. Rise time 0.37 s: g = 8h/T^2, take-off v = 4h/T.
AJH, AJT = 56, 0.74
g.u("gravity", 8 * AJH / AJT**2, AMG + " (jump height 56 u, airtime 0.74 s on flat ground)", conf="estimated", rawname="8 x 56 / 0.74^2 =", per="/s2",
    note="derived from the measured jump: g = 8 x height / airtime^2 (Titanfall 2's pilots: 750 x 0.8 = 600 u/s2)")
g.u("jump", 4 * AJH / AJT, AMG + " (jump height 56 u, airtime 0.74 s)", conf="estimated", rawname="4 x 56 / 0.74 =", per="/s",
    note="derived from the measured jump: take-off speed = 4 x height / airtime")
g.u("air_cap", 60, AMA + " (air speed 60)", conf="measured", rawname="air speed", per="/s", note="the movement wiki's core air value, read as Source's air wish cap (Titanfall 2's player dump has the same airSpeed 60)")
g.v("air_accel", 500 / 60 / 10, AMA + " (air acceleration 500)", "estimated",
    "as Titanfall 2: if the 500 is an absolute u/s2 push toward the 60 u/s cap, the Quark-equivalent is 500 / 60 / 10; the engine's formula isn't public", raw="air acceleration 500")
g.u("step", 30, AMG + " (overwalkable ledge about 30 hu, higher on rounded or sloped ledges)", conf="measured", rawname="ledge", note="the highest ledge you walk over without jumping or mantling")
g.toggle("dash", False, AW, "no dash for most legends (Octane / Horizon / Pathfinder have movement abilities)")
g.toggle("grapple", False, "https://apexlegends.wiki.gg/wiki/Pathfinder", "Pathfinder only")
g.fact("walkSpeed", 199.53 * S72, "m/s", AW + " (walking 199.53)", "measured")
g.fact("crouchSpeed", 92 * S72, "m/s", AW + " (crouchwalking 92)", "measured")
g.fact("slideTime", 1, "s", AW + " (about 1 s on flat ground before it slows)", "measured")
g.fact("superglideBoost", 10.3, "m/s", "https://apexlegends.fandom.com/wiki/Movement", "measured")
g.fact("climbHeight", 6, "m", "https://apexlegends.fandom.com/wiki/Movement (wall climb about 6 m)", "measured")
g.fact("jumpHeight", AJH * S72, "m", AMG + " (56 units)", "measured", "Quark: 1.22 m")
g.fact("airTime", AJT, "s", AMG + " (0.74 s on flat ground)", "measured", "Quark: 2 x 7.34 / 22 = 0.67 s")
g.fact("jumpFatigue", "16.3 u for 0.15 s after landing, back to 56 u by 0.75 s", "", AMG + " (antiMultiJumpTimeMin 0.15, antiMultiJumpTimeMax 0.75, antiMultiJumpHeightFrac 0.3)", "measured",
       "a jump right after landing is lower")
g.d["extras"] = ["slide (about 1 s on flat ground, longer downhill)", "wall climb about 6 m, mantle", "superglide (10.3 m/s), tap-strafe", "ziplines, jump pads",
                 "jump fatigue: a jump within 0.15 s of landing reaches only about 30% height, recovering by 0.75 s (Apex Movement Wiki)",
                 "bunny hops above sprint speed lose up to 100 u/s each; no air friction (horizontal speed is kept in the air)",
                 "legend hulls are 66, 72 or 80 u tall (16 u radius), so the 72 u scale here is the middle size"]
g.d["notes"] = ("Apex's data files aren't public; numbers are community measurements. The Fandom wiki converts the same 299 u/s at 1 u = 0.75 in (5.7 m/s); this file uses the player-height scale. "
                "Gravity and jump speed are derived from the Apex Movement Wiki's measured jump (56 u high, 0.74 s in the air), so they are estimates; the air values are the same 60 / 500 Titanfall 2 uses.")
g.src("Apex Legends Wiki (wiki.gg): Sliding", AW); g.src("Apex Legends Wiki (wiki.gg): Pathfinder", "https://apexlegends.wiki.gg/wiki/Pathfinder")
g.src("Apex Legends Wiki (Fandom): Movement", "https://apexlegends.fandom.com/wiki/Movement")
g.src("Apex Movement Wiki: Fundamental Ground Movement", AMG); g.src("Apex Movement Wiki: Fundamental Air Tech", AMA)
g.src("Apex Movement Wiki: How the Apex engine handles movement", "https://apexmovement.tech/wiki/articles/Wiki%20help%3EHow%20the%20Apex%20engine%20handles%20Movement")
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
OWG = "https://us.forums.blizzard.com/en/overwatch/t/fyi-how-to-counter-gravity/357645"
g.v("gravity", 17.532, OWG + " (a Workshop 'start accelerating' up at 17.532 m/s2 cancels gravity)", "measured",
    "found by Workshop users: the upward acceleration that holds a hero still in the air", raw="17.532 m/s2")
g.v("jump", math.sqrt(2 * 17.532 * 0.98), OW + "Character (jump height 0.98 m) with " + OWG + " (gravity 17.532 m/s2)", "estimated",
    "take-off speed for the wiki's 0.98 m jump at the Workshop-measured gravity: sqrt(2 x 17.532 x 0.98)", raw="sqrt(2 x 17.532 x 0.98)")
g.toggle("dash", False, OW + "Character", "hero abilities only (Tracer, Genji...)")
g.toggle("grapple", False, OW + "Widowmaker", "hero abilities only (Widowmaker, Ashe...)")
g.fact("jumpHeight", 0.98, "m", OW + "Character (jump height 0.98 m)", "measured", "Quark: 1.22 m")
g.fact("crouchSpeed", 3, "m/s", OW + "Character", "measured")
g.fact("blinkDistance", 7.5, "m", OW + "Tracer", "measured", "Quark's dash covers 2.84 m")
g.d["extras"] = ["hero-specific movement abilities (blinks, dashes, wall-ride, jet packs, double jumps)", "no air strafing to speak of"]
g.d["notes"] = ("Acceleration isn't documented. Gravity is the Workshop community's counter-gravity figure (17.532 m/s2), and the jump take-off speed is derived from it and the wiki's 0.98 m jump, so both are estimates of a kind. "
                "The dash and hook rows are single heroes (Tracer, Widowmaker).")
g.src("Overwatch Wiki: Character", OW + "Character"); g.src("Overwatch Wiki: Tracer", OW + "Tracer"); g.src("Overwatch Wiki: Widowmaker", OW + "Widowmaker")
g.src("Overwatch Forums: how to counter gravity (Workshop)", OWG)
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
U99 = GH + "/Slipyx/UT99/blob/master/"
g.u("gravity", 950, U99 + "Engine/ZoneInfo.uc#L187 (ZoneGravity Z=-950)", rawname="ZoneGravity", per="/s2")
g.u("jump", 325, U99 + "Engine/Pawn.uc#L1877 (JumpZ=325; TournamentPlayer keeps it)", rawname="JumpZ", per="/s",
    note="Classic; Hardcore (DeathMatchPlus's default, bHardCoreMode=True, DeathMatchPlus.uc#L1625) scales JumpZ x1.1 (#L1062-1063) and runs the game at x1.1 speed (#L139)")
g.v("ground_accel", 2048 / 400, U99 + "Botpack/TournamentPlayer.uc#L1639 (AccelRate=2048) and #L1637 (GroundSpeed=400)", "estimated",
    "Unreal adds a fixed AccelRate (2048 UU/s2) up to GroundSpeed; Quark adds ground_accel x wish speed per second, so the same push is 2048 / 400", raw="AccelRate 2048 UU/s2")
g.v("air_accel", 0.35 * 2048 / 400 / 10, U99 + "Botpack/TournamentPlayer.uc#L1640 (AirControl=0.35) x AccelRate 2048, toward AirSpeed 400 (#L1638)", "estimated",
    "if the air push is AirControl x AccelRate (717 UU/s2) toward AirSpeed: Quark-equivalent 0.35 x 2048 / 400 / 10; Unreal's native physics code isn't public", raw="AirControl 0.35")
g.u("air_cap", 400, U99 + "Botpack/TournamentPlayer.uc#L1638 (AirSpeed=400)", conf="estimated", rawname="AirSpeed", per="/s", note="the air push aims at AirSpeed (the same as the run speed)")
g.u("step", 25, U99 + "Engine/Pawn.uc#L1878 (MaxStepHeight=25)", rawname="MaxStepHeight")
g.toggle("dash", True, UTM, "the dodge")
g.toggle("grapple", False, UTM, "no grapple (the translocator teleports)")
g.fact("jumpHeightFromCode", 325**2 / 1900 * SU, "m", "325^2 / (2 x 950) = 55.6 UU", "estimated", "v^2/2g from JumpZ and ZoneGravity: agrees with the measured 55 UU")
g.fact("jumpHeight", 55 * SU, "m", UTM + " (55 UU, Classic running)", "measured", "Hardcore 67 UU; Quark: 1.22 m")
g.fact("dodgeHeight", 23 * SU, "m", UTM + " (23 UU)", "measured")
g.fact("dodgeDistance", 237 * SU, "m", UTM + " (237 UU)", "measured", "Quark's dash covers 2.84 m")
g.fact("airControl", "35%", "", UTM, "measured", "UT's AirControl is a share of ground acceleration in the air; Quark's air_control isn't the same thing")
g.d["extras"] = ["dodging (double-tap a direction)", "impact hammer jumps", "translocator", "jump boots"]
g.d["notes"] = ("Speeds and heights measured by the Unreal Wiki community; gravity, JumpZ, AccelRate, AirControl and step from the UnrealScript defaults (UT 469b scripts, exported in Slipyx/UT99), "
                "which match the measurements (325^2 / 1900 = 55.6 UU against the measured 55). Ground / air acceleration are Quark-equivalents of Unreal's fixed-rate acceleration (estimated). "
                "UT's ground friction (ZoneGroundFriction 8, ZoneInfo.uc#L189) is a different formula and isn't mapped.")
g.src("Unreal Wiki: Movement Metrics (UT)", UTM); g.src("Unreal Wiki: General Scale And Dimensions", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:General_Scale_And_Dimensions.html")
g.src("UT99 469b UnrealScript (Slipyx/UT99): Pawn.uc, TournamentPlayer.uc, ZoneInfo.uc, DeathMatchPlus.uc", U99 + "Engine/Pawn.uc")
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
NEO = "https://docs.neoforged.net/docs/entities/attributes"
g.v("step", 0.6, NEO + " (step_height default 0.6 blocks)", "exact", "the vanilla attribute default (also lists jump_strength 0.42 and gravity 0.08, as above)", raw="step_height 0.6")
g.toggle("dash", False, MC + "Player", "no dash"); g.toggle("grapple", False, MC + "Player", "no grapple")
g.fact("jumpHeight", 1.2522, "m", MC + "Player (about 1.2522 blocks)", "measured", "Quark: 1.22 m")
g.fact("walkSpeed", 4.317, "m/s", MC + "Walking", "measured")
g.fact("sprintJumpSpeed", 7.127, "m/s", MC + "Sprinting (sprint-jumping average 7.127 m/s)", "measured")
g.fact("terminalVelocity", 78.4, "m/s", MC + "Player", "measured")
g.d["extras"] = ["air drag x0.91 per tick horizontally and x0.98 vertically (Quark has no drag)", "sprint jumping", "sneaking at 1.295 m/s, elytra, swimming"]
g.d["notes"] = "Minecraft's player happens to be 1.8 m, Quark's height. Its 20 Hz tick model (per-tick velocity multipliers) differs from Quake's, so friction and gravity are equivalents. Step-up height is the vanilla step_height attribute (0.6 blocks)."
g.src("NeoForged docs: attributes (vanilla defaults)", NEO)
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
g.v("super_dash_jump", 0.5, CEL + "#L46 (DuckSuperJumpYMult = .5f)", "exact", "the hyper (a super jump out of a crouched dash) jumps at half the jump speed: Quark's super dash hop started at the same 0.5 until 1.18.28 raised it to 0.866")
g.toggle("dash", True, CEL, "the dash"); g.toggle("grapple", False, CEL, "no grapple")
g.fact("superJumpSpeed", 260 * SC, "m/s", CEL + "#L69 (SuperJumpH = 260f)", "exact", "the super (dash jump) horizontal speed: 2.89 x run (Quark's dash jump leaves at the 24 m/s burst, 2.53 x)")
g.fact("hyperSpeed", 260 * 1.25 * SC, "m/s", CEL + "#L45 (DuckSuperJumpXMult = 1.25f)", "exact", "the hyper: 325 px/s, 3.6 x run (Quark's super dash: carried speed + 5 m/s)")
g.fact("jumpHeight", 28 * SC, "m", "derived from JumpSpeed, VarJumpTime, Gravity, HalfGravThreshold", "estimated", "about 28 px (21 px held + 7 px coasting) = 2.5 x her height; Quark: 1.22 m = 0.68 x")
g.d["extras"] = ["super / hyper / wavedash (dash techs Quark's dash jump and super dash are modelled on)", "climbing with stamina", "wall jumps", "coyote time 0.1 s (JumpGraceTime)"]
g.d["notes"] = "Celeste's Player.cs was published by its developers. A 2D game: its gravity and jumps are far beyond a shooter's; the dash numbers are the useful comparison."
g.src("Celeste Player.cs (NoelFB/Celeste)", CEL)
games.append(g.write())

# ------------------------------------------------------------------ second pass: estimates and community numbers
# Quake Live: a cvarlist dump from a Quake Live client (pastebin), the pmove_* cvars as the server ran them.
QLD = "https://pastebin.com/zJ6XqCNq"
QLF = "https://gist.github.com/cstewart90/a1163bad196977e351eb"
g = Game("quakelive", "Quake Live (VQL)", 2010, "id Tech 3", "Quake family", "u", S,
         "Quake III's hull (56 u tall: Quake Live is built on Quake III's code; its own hull isn't published), scaled to 1.8 m: 0.03214 m/u")
g.u("gravity", 800, QLD + " (cvarlist line 649: g_gravity 800)", conf="measured", rawname="g_gravity", per="/s2")
g.u("speed", 320, QLD + " (line 514: g_speed 320)", conf="measured", rawname="g_speed", per="/s")
g.u("jump", 275, QLD + " (line 431: pmove_JumpVelocity 275)", conf="measured", rawname="pmove_JumpVelocity", per="/s",
    note="Quake III's 270 raised to 275 (an ESReality thread says QL also raised the step from 18 to 22 in an Oct 2010 update)")
g.v("ground_accel", 10, QLD + " (line 418: pmove_WalkAccel 10)", "measured", "Quake accelerate factor, as Quake III's pm_accelerate")
g.v("friction", 6, QLD + " (line 417: pmove_WalkFriction 6; pmove_CircleStrafeFriction 6, line 438)", "measured")
g.u("stop_speed", 100, P3 + "#L34 (Quake III's pm_stopspeed 100)", conf="estimated", rawname="pm_stopspeed", per="/s",
    note="not a Quake Live cvar; assumed unchanged from Quake III")
g.v("air_accel", 0.1, QLD + " (line 447: pmove_AirAccel 1; strafe-only pmove_StrafeAccel 1 toward pmove_WishSpeed 400, lines 420 / 412)", "measured",
    "VQL air: accel 1 toward the full wish speed, Quake III's formula (Quark multiplies air_accel by 10, so 1 -> 0.1)", raw="pmove_AirAccel 1")
g.u("air_cap", 320, QLD + " (g_speed 320: the VQL air push aims at the full wish speed)", conf="estimated", rawname="wish speed", per="/s")
g.v("overstrafe", 1.0, P3 + "#L637 (Quake III's PM_Accelerate, which VQL keeps)", "estimated", "no overstrafe softening")
g.u("step", 22, QLD + " (line 423: pmove_StepHeight 22)", conf="measured", rawname="pmove_StepHeight")
g.u("hook_max", 800, QLD + " (line 419: pmove_velocity_gh 800)", conf="measured", rawname="pmove_velocity_gh", per="/s",
    note="the grappling hook's speed; the hook is only given in some factories (e.g. race / CTF variants)")
g.toggle("dash", False, QLD, "no dash")
g.toggle("grapple", False, QLF, "the grappling hook is a factory option, not standard play")
g.fact("jumpHeight", 275**2 / 1600 * S, "m", "275^2 / (2 x 800) = 47.3 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.fact("autoHop", "on", "", QLD + " (line 442: pmove_AutoHop 1; line 441: pmove_BunnyHop 1)", "measured", "holding jump re-jumps on landing, as in Quark (the default duel factory sets pmove_BunnyHop 0)")
g.ground_facts(320 * S, 10, 6, 100 * S, "; stop speed assumed from Quake III")
g.d["extras"] = ["chain jump: pmove_ChainJump 1, ChainJumpVelocity 110 (a jump soon after the last one adds height)",
                 "step jump: pmove_StepJump 1, StepJumpVelocity 48 (jumping off a step goes higher); crouch step jump",
                 "jump scaling: pmove_JumpVelocityScaleAdd 0.4 within pmove_JumpVelocityTimeThreshold 500 ms, capped at pmove_JumpVelocityMax 700",
                 "air steps: pmove_AirSteps 1 (step-ups while airborne), AirStepFriction 0.03",
                 "PQL (the 'Turbo' ruleset, e.g. the default race and vampiric CA factories): pmove_AirControl 1, pmove_RampJump 1, pmove_JumpTimeDeltaMin 50, weapon raise/drop 10 ms; CPM-like air control and crouch slide (pmove_CrouchSlide, 2 s, friction 0.5) are switched by these cvars"]
g.d["notes"] = ("VQL (vanilla Quake Live) from a player's cvarlist dump: pmove_WalkAccel 10, WalkFriction 6, AirAccel 1, JumpVelocity 275, StepHeight 22, g_gravity 800, g_speed 320. "
                "Quake Live's code isn't public, so these are 'measured' (a dump of what a server ran) and the stop speed is assumed from Quake III. PQL's CPM-style values (strafe accel and wish speed) weren't found published.")
g.src("Quake Live cvarlist dump (pastebin)", QLD); g.src("Default Quake Live factories (formatted, gist)", QLF)
g.src("ESReality: Question about QL/Q3 physics (VQL, PQL)", "https://www.esreality.com/post/2838908/questionaboutqlq3physicsvqlpql/")
g.src("Quake III Arena bg_pmove.c", P3)
games.append(g.write())

# PQL ("Turbo"): pmove_AirControl > 0 swaps in a hard-coded bundle. Two unofficial sources agree on it: a reconstruction of the
# retail game code from the binaries, and a player's saved config from a PQL server (QuakeCon 2015 configs).
QLRE = GH + "/Kr3m/QuakeLive/blob/master/src/code/game/bg_pmove.c"
QLPC = GH + "/Danmer/ql-configs/blob/master/qcon2015/madrigal.cfg"
g = Game("quakelive-pql", "Quake Live (PQL / Turbo)", 2014, "id Tech 3", "Quake family", "u", S,
         "Quake III's hull (56 u tall, as the VQL entry), scaled to 1.8 m: 0.03214 m/u")
g.u("gravity", 800, QLPC + "#L1226 (g_gravity 800)", conf="measured", rawname="g_gravity", per="/s2")
g.u("speed", 320, QLPC + "#L1249 (g_speed 320)", conf="measured", rawname="g_speed", per="/s")
g.u("jump", 275, QLD + " (line 431: pmove_JumpVelocity 275; the PQL bundle doesn't change it: " + QLRE + "#L169-178)", conf="measured",
    rawname="pmove_JumpVelocity", per="/s")
g.v("ground_accel", 15, QLRE + "#L175 (PQL bundle walkAccel 15); " + QLPC + "#L1272 (pmove_WalkAccel 15.0f)", "exact",
    "Quake accelerate factor (CPM's 15 instead of VQL's 10); the reconstruction and the player's config agree", raw="pmove_WalkAccel 15")
g.v("friction", 6, QLPC + "#L1273 (pmove_WalkFriction 6); " + QLRE + "#L173 (circle-strafe friction 5.5)", "measured",
    "walk friction 6; holding forward + strafe on the ground uses pmove_CircleStrafeFriction 5.5 instead")
g.u("stop_speed", 100, P3 + "#L34 (Quake III's pm_stopspeed 100)", conf="estimated", rawname="pm_stopspeed", per="/s",
    note="not a Quake Live cvar; assumed unchanged from Quake III (the reconstruction keeps 100)")
air_capped(g, 70, 35, QLRE + "#L174 / #L177 (PQL bundle strafeAccel 70, wishSpeed 35); " + QLPC + "#L1271 / #L1278 (pmove_StrafeAccel 70.0f, pmove_WishSpeed 35.0f)",
           "PQL air with only a strafe key held: accel 70 toward 35 u/s (CPM's 70 toward 30). With forward held it is pmove_AirAccel 1.1 toward 320 u/s plus CPM air control 150.")
g.v("overstrafe", 1.0, QLRE + " (PM_Accelerate, Quake III's)", "estimated", "no overstrafe softening")
g.u("step", 22, QLD + " (line 423: pmove_StepHeight 22, as VQL)", conf="measured", rawname="pmove_StepHeight")
g.toggle("dash", False, QLRE, "no dash")
g.toggle("grapple", False, QLF, "the grappling hook is a factory option, not standard play")
g.fact("jumpHeight", 275**2 / 1600 * S, "m", "275^2 / (2 x 800) = 47.3 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.fact("airAccelForward", 1.1, "", QLRE + "#L171 (airAccel 1.1); " + QLPC + "#L1264 (pmove_AirAccel 1.1f)", "exact", "the air accel with forward held (Quake III shape, toward 320 u/s)")
g.fact("airControl", 150, "", QLRE + "#L170 (airControl 150); " + QLRE + "#L751 (k = 32 x aircontrol x dot^2 x frametime)", "exact", "CPM air control, forward held")
g.fact("airStopAccel", 2.5, "", QLRE + "#L172; " + QLPC + "#L1266 (pmove_AirStopAccel 2.5f)", "exact", "the air accel when the wish direction opposes your velocity")
g.ground_facts(320 * S, 15, 6, 100 * S, "; stop speed assumed from Quake III")
g.d["extras"] = ["CPM-style air control (150) and A/D strafe acceleration (70 toward 35 u/s)", "air steps (3), ramp jumps, crouch slide",
                 "the VQL entry's chain / step jumps and auto-hop", "weapon switch 10 ms (pmove_WeaponRaiseTime / DropTime 10)"]
g.d["notes"] = ("PQL, Quake Live's CPM-like ruleset (the default race and vampiric CA factories): pmove_AirControl 1 switches in a hard-coded bundle "
                "(air control 150, air accel 1.1, air stop accel 2.5, circle-strafe friction 5.5, strafe accel 70, walk accel 15, air steps 3, wish speed 35). "
                "Read from Kr3m/QuakeLive (a reconstruction of the retail code from the binaries) and matched by a player's saved config from a PQL server; "
                "gravity, speed, jump and step are VQL's, unchanged by the bundle.")
g.src("Kr3m/QuakeLive bg_pmove.c (reconstructed retail pmove)", QLRE); g.src("A player's Quake Live config from a PQL server (Danmer/ql-configs, QuakeCon 2015)", QLPC)
g.src("Quake Live cvarlist dump (pastebin)", QLD); g.src("Quake III Arena bg_pmove.c", P3)
games.append(g.write())

QCN ="https://quake.bethesda.net/en/news/2fSdxgKqKYKgsyeOmggmuU"
QCF = "https://quake.bethesda.net/en/news/4ii1II7x0GWYdL28HEOJ34"
QCR = "https://quake.fandom.com/wiki/Ranger_(QC)"
g = Game("quake-champions", "Quake Champions (Ranger, a medium)", 2017, "Saber / id Software (id Tech derived)", "Quake family", "u", 1 / 40,
         "the game's own metres: the patch notes write 2.5 m = 100 u and 0.5 m = 20 u, so 40 u = 1 m (0.025 m/u); champions vary in size, so not rescaled to 1.8 m")
g.u("speed", 310, QCN + " (June 2018 patch: all mediums 310 base speed)", rawname="base speed", per="/s",
    note="the developer's published number; lights 320 (Anarki, Nyx, Slash...), heavies 300; Ranger is a medium")
g.v("air_accel", 0.9057232 / 10, QCN + " (all mediums 0.905723 air accel; lights 0.85, heavies 0.967111)", "estimated",
    "if Quake Champions keeps Quake III's air formula (accel toward the wish speed; Quake III's is 1): Quark-equivalent 0.9057 / 10. The formula isn't published", raw="Air-Accel 0.905723")
g.u("air_cap", 310, QCN + " (base speed 310: the wish speed a Quake III-style air push aims at)", conf="estimated", rawname="wish speed", per="/s")
g.toggle("dash", False, QCN, "champion abilities only (several champions have dashes or blinks)")
g.toggle("grapple", False, QCN, "no grapple")
g.fact("maxSpeed", 600 / 40, "m/s", QCR + " (speed 310, max 600)", "measured", "Ranger's listed top speed (600 u/s); forum posts cite a 680 u/s cap for light champions")
g.fact("lightSpeed", 320 / 40, "m/s", QCN + " (all lights 320 base speed)", "exact")
g.fact("heavySpeed", 300 / 40, "m/s", QCN + " (all heavies 300 base speed)", "exact")
g.fact("anarkiFriction", 8, "", QCN + " (Anarki and Sorlag: friction 9 -> 8; circle strafe friction 15 -> 12 / 14)", "exact",
       "per-champion friction; the other champions' values aren't published (9 before this patch for these two)")
g.fact("slayerDoubleJump", 6.875, "m/s", QCF + " (Fall 2021 update: DOOM Slayer double jump velocity 7.5 -> 6.875 m/s)", "exact")
g.d["extras"] = ["per-champion base speed, air acceleration and friction (light / medium / heavy)", "bunny hopping and strafe jumping with a speed cap per champion",
                 "champion active abilities (dashes, teleports, a hoverboard...)", "rocket jumping"]
g.d["notes"] = ("Base speeds and air acceleration are the developer's published numbers (June 2018 patch notes, which also give the 40 u = 1 m scale). "
                "Gravity, jump speed, ground acceleration and the air formula aren't published, so the air mapping is an estimate (assumes Quake III's formula).")
g.src("Quake Champions June patch update 07.06.2018", QCN); g.src("Quake Champions Fall 2021 update", QCF); g.src("Quake Wiki: Ranger (QC)", QCR)
g.src("Steam discussion: Quake Champions speed cap", "https://steamcommunity.com/app/611500/discussions/0/1694919808737834911")
games.append(g.write())

RFW = "http://wiki.reflexfiles.com/index.php/Tutorial_for_New_Players"
RFG = "https://steamcommunity.com/sharedfiles/filedetails?id=387675048"
g = Game("reflex", "Reflex Arena", 2017, "Reflex (Turbo Pixel Studios)", "arena shooters", "u", S,
         "assumed Quake's scale (a 56 u player, 0.03214 m/u): Reflex speeds follow Quake / CPM (320 ups), but its hull size isn't published")
g.u("speed", 320, RFW + " (base running speed 320 UPS)", conf="estimated", rawname="base run", per="/s", note="the number is the wiki's; the metre conversion assumes Quake's player size")
RFR = GH + "/Kyt0z/reflex-ruleset-comparer/blob/main/defaultRuleset.js"
g.u("gravity", 750, RFR + "#L19 (gconst_world_gravity 750, Competitive ruleset)", conf="measured", rawname="gconst_world_gravity", per="/s2",
    note="a community ruleset tool's dump of the Competitive (CR1) ruleset; metres at the assumed Quake scale")
g.u("jump", 255, RFR + "#L7 (gconst_jump_velocity 255)", conf="measured", rawname="gconst_jump_velocity", per="/s",
    note="plus gconst_double_jump_velocity 105 for a jump within 0.4 s of the last (#L6)")
g.v("ground_accel", 4000 / 320, RFR + "#L14 (gconst_playeronground_acceleration 4000) and #L16 (gconst_playeronground_speed 320)", "estimated",
    "if 4000 is a fixed rate in u/s2 (it is far too big for a Quake factor): Quark-equivalent 4000 / 320 = 12.5; Reflex's ground formula isn't published",
    raw="gconst_playeronground_acceleration 4000")
g.v("friction", 6, RFR + "#L15 (gconst_playeronground_friction 6)", "measured", "assumed to be the Quake friction factor (the same 6 as Quake III / CPM)")
air_capped(g, 60, 30, RFR + "#L10-11 (gconst_playerinair_bunny_accelerate 60, gconst_playerinair_bunny_wishspeed 30)",
           "Reflex air with only a strafe key held (CPM-style bunny strafe): accel 60 toward 30 u/s; with forward held it is gconst_playerinair_acceleration 0.95 plus air control 150. Assumes Reflex keeps CPM's formula.")
g.d["values"]["air_accel"]["confidence"] = "estimated"; g.d["values"]["air_cap"]["confidence"] = "measured"
g.fact("crouchSpeed", 120 * S, "m/s", RFR + "#L17 (gconst_playerongroundcrouched_speed 120)", "estimated", "120 u/s at the assumed Quake scale; Quark: 4 m/s")
g.toggle("dash", False, RFW, "no dash")
g.toggle("grapple", False, RFW, "no grapple")
g.fact("wallrunMaxSpeed", 507 * S, "m/s", RFW + " (wall running / circle strafe max 507 UPS)", "estimated", "507 UPS at the assumed Quake scale")
g.fact("circleJumpSpeed", 480 * S, "m/s", RFW + " (circle jump about 480 UPS or more)", "estimated")
g.fact("doubleJumpWindow", 0.4, "s", RFW + " (second jump within 400 ms)", "measured", "a jump within 0.4 s of the last one goes higher (stackable up to a triple jump)")
g.fact("doubleJumpBonus", 104 * S, "m", RFG + " (double jump height bonus 104 u)", "estimated", "104 u at the assumed Quake scale")
g.fact("jumpHeight", 255**2 / 1500 * S, "m", "255^2 / (2 x 750) = 43.4 u", "estimated", "v^2/2g from the ruleset dump; Quark: 1.22 m")
g.fact("doubleJumpVelocity", (255 + 105) * S, "m/s", RFR + "#L6-7 (255 + gconst_double_jump_velocity 105)", "estimated",
       "if the bonus adds to the take-off speed: 360 u/s, a 86.4 u jump (v^2/2g), 43 u higher than a single jump, not the Steam guide's 104 u, so one of the two may describe another ruleset")
g.fact("airAccelForward", 0.95, "", RFR + "#L8 (gconst_playerinair_acceleration 0.95)", "measured", "the air accel with forward held (Quake III's is 1)")
g.fact("airControl", 150, "", RFR + "#L9 (gconst_playerinair_aircontrol 150)", "measured", "CPM-style air control, forward held (PQL's is also 150)")
g.fact("airDeceleration", 2.5, "", RFR + "#L12 (gconst_playerinair_decceleration 2.5)", "measured", "the air accel against your velocity (PQL's air stop accel is also 2.5)")
g.d["extras"] = ["CPM-style air control and strafe jumping", "double and triple jumps (consecutive jumps within 0.4 s stack height)", "ramp jumps", "wall running (507 UPS cap)"]
g.d["notes"] = ("Reflex's physics constants became server-editable in 0.47.3 (gconst_* cvars). Their Competitive (CR1) ruleset values come from a community "
                "ruleset-comparison tool's dump (Kyt0z/reflex-ruleset-comparer): gravity 750, jump 255 (+105 double), ground speed 320, ground accel 4000, friction 6, "
                "air accel 0.95, air control 150, bunny accel 60 toward 30, air decel 2.5, crouch speed 120. They are close to CPM / PQL's. "
                "The metre conversion assumes Quake's player size and the air / ground mappings assume CPM's formulas, so those are estimates.")
g.src("Reflex Wiki: Tutorial for New Players", RFW); g.src("Steam guide: Reflex Arena mechanics", RFG)
g.src("Reflex Wiki: Changelog 0.47.x", "http://wiki.reflexfiles.com/index.php?title=Reflex_Changelog_0.47.x")
g.src("Reflex ruleset comparer: Competitive ruleset gconst dump", RFR)
games.append(g.write())

# Diabotical: its shipped game-mode script (scripts/base.games, kept by a community patch tracker) sets every phy_* setting in
# the `default` game, and no ranked / standard mode overrides them. The Rogue tracker's UI copy of the same block agrees.
DBB = GH + "/ScheduleTracker/DiaboticalTracker/blob/master/scripts/base.games"
DBR = GH + "/marconett/diabotical-rogue-release-tracker/blob/main/ui/93.js"
SD = 1.8 / 57
g = Game("diabotical", "Diabotical", 2020, "The GD Studio's own engine", "arena shooters", "u", SD,
         "player hitbox 57 u tall (game_player_hitbox_height 57, base.games#L75; collision 24 u below the origin, as Quake III's), scaled to 1.8 m: 0.03158 m/u")
g.u("gravity", 800, DBB + "#L107 (phy_gravity 800)", rawname="phy_gravity", per="/s2")
g.u("speed", 320, DBB + "#L115 (phy_speed 320); " + DBR + "#L1166", rawname="phy_speed", per="/s")
g.u("jump", 275, DBB + "#L108 (phy_jump_impulse 275); " + DBR + "#L1160", rawname="phy_jump_impulse", per="/s")
g.v("ground_accel", 10, DBB + "#L116 (phy_accel_ground 10)", "exact", "assumed to be the Quake accelerate factor (the same 10 as Quake III / VQL)", raw="phy_accel_ground 10")
g.v("friction", 6, DBB + "#L118 (phy_surface_friction 6)", "exact", "assumed to be the Quake friction factor (Quake III's 6)", raw="phy_surface_friction 6")
g.u("stop_speed", 100, DBB + "#L117 (phy_deceleration_ground 100)", conf="estimated", rawname="phy_deceleration_ground", per="/s",
    note="read as Quake's stop speed (the same default 100); the engine's formula isn't public")
g.v("air_accel", 0.1, DBB + "#L120-121 (phy_air_speed 320, phy_accel_air 1)", "estimated",
    "if the air push is Quake III's (accel 1 toward the full wish speed 320): Quark-equivalent 1 / 10. phy_strafe_jumping 1 (#L126) adds strafe-jump air control whose numbers aren't in the script",
    raw="phy_accel_air 1")
g.u("air_cap", 320, DBB + "#L120 (phy_air_speed 320)", conf="estimated", rawname="phy_air_speed", per="/s")
g.fact("crouchSpeed", 210 * SD, "m/s", DBB + "#L114 (phy_crouch_speed 210)", "exact", "210 u/s at the hitbox scale; Quark: 4 m/s")
g.toggle("dash", False, DBB + "#L134-145", "no dash in standard play: the Bolt (482 u/s, 3 s cooldown) is a mode skill (Wipeout / Rogue)")
g.toggle("grapple", False, DBB + "#L160 (phy_hook 0)", "no grapple")
g.fact("jumpHeight", 275**2 / 1600 * SD, "m", "275^2 / (2 x 800) = 47.3 u", "estimated", "v^2/2g; Quark: 1.22 m")
g.fact("tickrate", 125, "Hz", DBB + "#L106 (phy_target_tickrate 125)", "exact")
g.fact("boltSpeed", 482 * SD, "m/s", DBB + "#L138 (phy_bolt_base_speed 482) and #L135 (phy_bolt_cooldown 3)", "estimated", "the Bolt skill, at the hitbox scale; Quark's dash: 24 m/s")
g.ground_facts(320 * SD, 10, 6, 100 * SD, "; stop speed read from phy_deceleration_ground")
g.d["extras"] = ["auto-hop (phy_auto_jump 1) and strafe jumping (phy_strafe_jumping 1)", "the Bolt dash and weeballs (grenades) as mode items / skills",
                 "125 Hz physics tick", "Vintage physics (a later alternate preset) isn't covered: its values weren't in the script"]
g.d["notes"] = ("Diabotical's shipped scripts/base.games (as the community patch tracker kept it): gravity 800, jump 275, speed 320, ground accel 10, friction 6, "
                "ground deceleration 100, air speed 320, air accel 1, crouch speed 210, 125 Hz: VQL-like numbers. The duel / CA / TDM modes don't override them. "
                "The engine is closed, so the formulas (and the air mapping) are assumed to be Quake III's. "
                "The Rogue tracker's copy (ui/93.js#L1159-1171) has the same values; its newer rogue modes use speed 300 and air accel 1.3.")
g.src("Diabotical patch tracker: scripts/base.games", DBB); g.src("Diabotical Rogue release tracker: ui/93.js (the same default block)", DBR)
g.src("Diabotical Rogue release tracker: base.games (rogue modes)", GH + "/marconett/diabotical-rogue-release-tracker/blob/main/assets/shared/scripts/base.games")
games.append(g.write())

# Unreal Engine 2 / 3: Unreal adds a fixed AccelRate (UU/s2) up to GroundSpeed; in the air AirControl x AccelRate toward AirSpeed.
def ue_accel(g, accel, gspeed, airctl, aspeed, src_accel, src_air, src_aspeed):
    g.v("ground_accel", accel / gspeed, src_accel, "estimated",
        f"Unreal adds a fixed AccelRate ({accel:g} UU/s2) up to GroundSpeed; Quark adds ground_accel x wish speed per second, so the same push is {accel:g} / {gspeed:g}",
        raw=f"AccelRate {accel:g} UU/s2")
    g.v("air_accel", airctl * accel / aspeed / 10, src_air, "estimated",
        f"if the air push is AirControl x AccelRate ({airctl * accel:g} UU/s2) toward AirSpeed: Quark-equivalent {airctl:g} x {accel:g} / {aspeed:g} / 10; Unreal's native physics code isn't public",
        raw=f"AirControl {airctl:g}")
    g.u("air_cap", aspeed, src_aspeed, conf="estimated", rawname="AirSpeed", per="/s", note="the air push aims at AirSpeed (the same as the run speed)")

U4 = GH + "/Deaod/UT2004/blob/master/"
SU2 = 1.8 / 88
g = Game("ut2004", "Unreal Tournament 2004", 2004, "Unreal Engine 2", "arena shooters", "UU", SU2,
         "player cylinder 88 UU tall (xPawn CollisionHeight 44 x 2, xPawn.uc#L2573), scaled to 1.8 m: 0.02045 m/UU")
g.u("gravity", 950, U4 + "Engine/PhysicsVolume.uc#L307 (Gravity Z=-950); Engine/LevelInfo.uc#L523 (DefaultGravity -950)", rawname="Gravity", per="/s2")
g.u("speed", 440, U4 + "XGame/xPawn.uc#L2493 (GroundSpeed=440)", rawname="GroundSpeed", per="/s")
g.u("jump", 340, U4 + "XGame/xPawn.uc#L2496 (JumpZ=340)", rawname="JumpZ", per="/s")
ue_accel(g, 2048, 440, 0.35, 440, U4 + "Engine/Pawn.uc#L2779 (AccelRate=2048) and XGame/xPawn.uc#L2493 (GroundSpeed=440)",
         U4 + "UnrealGame/UnrealPawn.uc#L512 (AirControl=0.35) x AccelRate 2048, toward AirSpeed 440 (xPawn.uc#L2495)", U4 + "XGame/xPawn.uc#L2495 (AirSpeed=440)")
g.u("step", 35, U4 + "Engine/Actor.uc#L523 (const MAXSTEPHEIGHT = 35)", rawname="MAXSTEPHEIGHT")
g.u("ladder_speed", 200, U4 + "Engine/Pawn.uc#L2778 (LadderSpeed=200)", rawname="LadderSpeed", per="/s")
g.u("dash_speed", 1.5 * 440, U4 + "XGame/xPawn.uc#L1986 (Velocity = DodgeSpeedFactor x GroundSpeed x Dir) and #L2512 (DodgeSpeedFactor=1.5)", conf="estimated",
    rawname="1.5 x 440 =", per="/s", note="the dodge (double-tap) SETS your horizontal speed to 660 UU/s with a 210 UU/s hop; mapped to Quark's dash burst")
g.toggle("dash", True, U4 + "XGame/xPawn.uc#L1986", "the dodge (and wall dodge, bCanWallDodge)")
g.toggle("grapple", False, U4 + "XGame/xPawn.uc", "no grapple (the translocator teleports)")
g.fact("jumpHeight", 340**2 / 1900 * SU2, "m", "340^2 / (2 x 950) = 60.8 UU", "estimated", "v^2/2g; the Unreal Wiki measures 54 UU (stats mutator); Quark: 1.22 m")
g.fact("jumpHeightMeasured", 54 * SU2, "m", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:Movement_Metrics_(UT2004).html (J: 54 UU high, 315 UU long)", "measured")
g.fact("doubleJumpHeight", 126 * SU2, "m", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:Movement_Metrics_(UT2004).html (J, DJ: 126 UU)", "measured", "jump then double jump")
g.fact("doubleJumpUp", 365 * SU2, "m/s", U4 + "XGame/xPawn.uc#L2009 (Velocity.Z = JumpZ + MultiJumpBoost) and #L2455 (MultiJumpBoost=25)", "exact",
       "the double jump sets 365 UU/s up, allowed within 100 UU/s of the apex (#L2031)")
g.fact("dodgeUp", 210 * SU2, "m/s", U4 + "XGame/xPawn.uc#L2513 (DodgeSpeedZ=210)", "exact")
g.fact("dodgeDistance", 292 * SU2, "m", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:Movement_Metrics_(UT2004).html (D: 292 UU standing, 351 running)", "measured", "Quark's dash covers 2.84 m")
g.d["extras"] = ["dodge (660 UU/s, 210 up) and wall dodge", "double jump near the apex (JumpZ + 25), dodge jump", "shield gun jumps", "translocator",
                 "ground friction 8 (PhysicsVolume.uc#L308), a different formula from Quark's, not mapped"]
g.d["notes"] = ("UnrealScript defaults from the UT2004 v3369 script dump (Deaod/UT2004): Engine.Pawn, UnrealGame.UnrealPawn, XGame.xPawn, PhysicsVolume. "
                "Ground and air acceleration are Quark-equivalents of Unreal's fixed-rate acceleration (estimated). The measured jump (54 UU) is a little under v^2/2g's 60.8.")
g.src("UT2004 UnrealScript (Deaod/UT2004): xPawn.uc", U4 + "XGame/xPawn.uc"); g.src("UT2004 Engine Pawn.uc / PhysicsVolume.uc / Actor.uc", U4 + "Engine/Pawn.uc")
g.src("UnrealGame UnrealPawn.uc", U4 + "UnrealGame/UnrealPawn.uc"); g.src("Unreal Wiki: Movement Metrics (UT2004)", "https://unrealarchive.org/wikis/unreal-wiki/Legacy:Movement_Metrics_(UT2004).html")
g.src("Unreal Wiki: Unreal Unit (player heights)", "https://unrealarchive.org/wikis/unreal-wiki/Unreal_Unit.html")
games.append(g.write())

UDK = GH + "/npruehs/hostile-worlds/blob/master/src/Hostile%20Worlds/Development/Src/"
UTP = UDK + "UTGame/Classes/UTPawn.uc"
UT3C = "https://xgamingserver.com/configs/ut3"
g = Game("ut3", "Unreal Tournament 3", 2007, "Unreal Engine 3", "arena shooters", "UU", SU2,
         "player cylinder 88 UU tall (UTPawn CollisionHeight 44 x 2, UTPawn.uc#L5529), scaled to 1.8 m: 0.02045 m/UU")
g.u("gravity", 520, UT3C + " (UTGame.ini [Engine.WorldInfo] DefaultGravityZ=-520.0)", rawname="DefaultGravityZ", per="/s2",
    note="the game's config default (UDK ships the same line); much lower than UT2004's 950")
g.u("speed", 440, UTP + "#L5550 (GroundSpeed=440.0)", rawname="GroundSpeed", per="/s")
g.u("jump", 322, UTP + "#L5556 (JumpZ=322.0)", rawname="JumpZ", per="/s")
ue_accel(g, 2048, 440, 0.35, 440, UTP + "#L5555 (AccelRate=2048.0) and #L5550", UTP + "#L5573 (AirControl=+0.35) x AccelRate 2048, toward AirSpeed 440 (#L5551)", UTP + "#L5551 (AirSpeed=440.0)")
g.u("step", 26, UTP + "#L5590 (MaxStepHeight=26.0)", rawname="MaxStepHeight")
g.u("ladder_speed", 200, UDK + "Engine/Classes/Pawn.uc#L3552 (LadderSpeed=+200.0; UTPawn keeps it)", rawname="LadderSpeed", per="/s")
g.u("dash_speed", 600, UTP + "#L5553 (DodgeSpeed=600.0) and #L3282 (Velocity = DodgeSpeed x Dir)", conf="estimated", rawname="DodgeSpeed", per="/s",
    note="the dodge SETS your horizontal speed to 600 UU/s with a 295 UU/s hop; mapped to Quark's dash burst")
g.toggle("dash", True, UTP + "#L3282", "the dodge and wall dodge")
g.toggle("grapple", False, UTP, "no grapple (the translocator teleports)")
g.fact("jumpHeight", 322**2 / 1040 * SU2, "m", "322^2 / (2 x 520) = 99.7 UU", "estimated",
       "v^2/2g from JumpZ and the config gravity; the AI's MaxJumpHeight=49 / MaxDoubleJumpHeight=87 (UTPawn.uc#L5591-5592) suggest less, so treat this with care; Quark: 1.22 m")
g.fact("doubleJumpUp", 277 * SU2, "m/s", UTP + "#L3303 (Velocity.Z = JumpZ + MultiJumpBoost) and #L5583 (MultiJumpBoost=-45.0)", "exact",
       "the double jump sets 277 UU/s up, allowed within 160 UU/s of the apex (DoubleJumpThreshold, #L5627)")
g.fact("dodgeUp", 295 * SU2, "m/s", UTP + "#L5554 (DodgeSpeedZ=295.0)", "exact")
g.d["extras"] = ["dodge (600 UU/s, 295 up) and wall dodge; no dodge jump (Liandri Archives: Jumping)", "double jump near the apex (JumpZ - 45)", "impact hammer jumps", "translocator, hoverboard in vehicle modes",
                 "ground friction 8 (PhysicsVolume.uc#L260), a different formula from Quark's, not mapped"]
g.d["notes"] = ("UTGame defaults from UDK's copy of UT3's UnrealScript (the Hostile Worlds repository), which may differ in small ways from retail UT3; gravity from UT3's UTGame.ini. "
                "Ground / air acceleration are Quark-equivalents of Unreal's fixed-rate acceleration (estimated).")
g.src("UDK UTGame UTPawn.uc (npruehs/hostile-worlds)", UTP); g.src("UDK Engine Pawn.uc / PhysicsVolume.uc", UDK + "Engine/Classes/Pawn.uc")
g.src("UT3 server config (UTGame.ini)", UT3C); g.src("UDK game ini (DefaultGravityZ -520)", "https://pastebin.com/Xt9y42L3")
g.src("Liandri Archives: Jumping", "https://unrealarchive.org/wikis/the-liandri-archives/Jumping.html")
games.append(g.write())

# UT4 (the 2014-2017 pre-alpha): Epic's source needs an Epic-linked GitHub account, but a public mirror of it exists.
UT4M = GH + "/JimmieKJ/unrealTournament/blob/master/UnrealTournament/"
UT4C = UT4M + "Source/UnrealTournament/Private/UTCharacterMovement.cpp"
UT4P = UT4M + "Source/UnrealTournament/Private/UTCharacter.cpp"
SU4 = 1.8 / 216
g = Game("ut4", "Unreal Tournament (2014 pre-alpha, UT4)", 2014, "Unreal Engine 4", "arena shooters", "UU", SU4,
         "player capsule 216 UU tall (InitCapsuleSize(40, 108): half height 108, UTCharacter.cpp#L66; UE4 units are cm, so 2.16 m), scaled to 1.8 m: 0.008333 m/UU")
g.u("gravity", 2154, UT4M + "Config/DefaultEngine.ini#L414 ([/Script/Engine.PhysicsSettings] DefaultGravityZ=-2154)", rawname="DefaultGravityZ", per="/s2",
    note="2.2x Earth in UE4's cm: UT4 is tuned at a large player scale")
g.u("speed", 940, UT4C + "#L23 (MaxWalkSpeed = 940)", rawname="MaxWalkSpeed", per="/s",
    note="auto-sprint to 1230 UU/s after 2 s running straight (SprintSpeed #L40, AutoSprintDelayInterval #L43)")
g.u("jump", 730, UT4C + "#L94 (JumpZVelocity = 730)", rawname="JumpZVelocity", per="/s")
g.v("ground_accel", 4000 / 940, UT4C + "#L57 (MaxAcceleration = 4000) and #L23 (MaxWalkSpeed 940)", "estimated",
    "UE4 adds a fixed MaxAcceleration (4000 UU/s2) up to MaxWalkSpeed; Quark adds ground_accel x wish speed per second, so the same push is 4000 / 940. "
    "Below 220 UU/s UT4 uses FastInitialAcceleration 12000 (#L77-78); ground friction 10.5 and braking 520 UU/s2 (#L62, #L67) are UE4's own formula, not mapped",
    raw="MaxAcceleration 4000 UU/s2")
g.v("air_accel", 0.45 * 4000 / 940 / 10, UT4C + "#L52 (AirControl = 0.45) x #L58 (MaxFallingAcceleration = 4000), toward MaxWalkSpeed 940", "estimated",
    "UE4 air push is AirControl x MaxFallingAcceleration (1800 UU/s2) toward the walk speed: Quark-equivalent 0.45 x 4000 / 940 / 10", raw="AirControl 0.45")
g.u("air_cap", 940, UT4C + "#L23 (UE4 falling caps the lateral air push at MaxWalkSpeed)", conf="estimated", rawname="MaxWalkSpeed", per="/s")
g.u("step", 51, UT4C + "#L70 (MaxStepHeight = 51)", rawname="MaxStepHeight")
g.fact("crouchSpeed", 315 * SU4, "m/s", UT4C + "#L47 (MaxWalkSpeedCrouched = 315)", "exact", "Quark: 4 m/s")
g.u("dash_speed", 1500, UT4C + "#L95 (DodgeImpulseHorizontal = 1500), capped by DodgeMaxHorizontalVelocity 1700 (#L96)", conf="estimated", rawname="DodgeImpulseHorizontal", per="/s",
    note="the dodge (double-tap) sets about 1500 UU/s horizontal with a 500 UU/s hop (#L98); mapped to Quark's dash burst")
g.v("dash_cooldown", 0.35, UT4C + "#L37 (DodgeResetInterval = 0.35)", "estimated", "the time after landing before the next dodge; mapped to Quark's dash cooldown", raw="DodgeResetInterval 0.35 s")
g.toggle("dash", True, UT4C + "#L95", "the dodge, wall dodge and dodge jump")
g.toggle("grapple", False, UT4C, "no grapple in standard play (the translocator teleports)")
g.fact("jumpHeight", 730**2 / (2 * 2154) * SU4, "m", "730^2 / (2 x 2154) = 123.7 UU", "estimated", "v^2/2g; Quark: 1.22 m")
g.fact("sprintSpeed", 1230 * SU4, "m/s", UT4C + "#L40 (SprintSpeed = 1230)", "exact", "after 2 s of running straight")
g.fact("dodgeUp", 500 * SU4, "m/s", UT4C + "#L98 (DodgeImpulseVertical = 500)", "exact")
g.fact("wallDodge", 1300 * SU4, "m/s", UT4C + "#L99-100 (WallDodgeImpulseHorizontal 1300, Vertical 470)", "exact")
g.fact("floorSlideSpeed", 1350 * SU4, "m/s", UT4C + "#L82 (MaxInitialFloorSlideSpeed = 1350)", "exact", "the slide's start speed (MaxFloorSlideSpeed 900)")
g.d["extras"] = ["dodge (1500 UU/s, 500 up), wall dodge (1300 / 470), dodge jump", "auto-sprint (1230 UU/s after 2 s straight)", "floor slide (1350 UU/s start) and wall slide",
                 "no double jump by default (MaxMultiJumpCount 0, #L29)", "impact hammer jumps, translocator",
                 "UE4 ground friction 10.5 / braking 520 UU/s2 and a 12000 UU/s2 start below 220 UU/s: a different formula from Quark's, not mapped"]
g.d["notes"] = ("UUTCharacterMovement's constructor defaults and DefaultEngine.ini's gravity, from a public mirror of Epic's UT4 source (JimmieKJ/unrealTournament; Epic's own repository "
                "needs an Epic-linked GitHub account). Everything scales by the 2.16 m capsule; ground / air acceleration are Quark-equivalents of UE4's fixed-rate acceleration (estimated).")
g.src("UT4 UTCharacterMovement.cpp (public mirror)", UT4C); g.src("UT4 UTCharacter.cpp (capsule size)", UT4P)
g.src("UT4 Config/DefaultEngine.ini (DefaultGravityZ)", UT4M + "Config/DefaultEngine.ini")
games.append(g.write())

# Halo 3 / Reach (MCC): one player's in-game measurements (bungie.net forum): speed in Forge metres, jump height in % of the Spartan's height
# (standing foot to the top of the head at the apex), airtime. Rescaled to a 1.8 m player taking the Spartan as 7 ft (2.134 m, the H2EK reference).
HFM = "https://www.bungie.net/en/Forums/Post/3291870"
SPART = 7 * 0.3048
for gid, name, year, run, jpct, air, extra in [
        ("halo3", "Halo 3 (MCC)", 2007, 7.69, 245, 1.4, ["equipment (bubble shield, grav lift...)", "crouch jumping", "no sprint"]),
        ("halo-reach", "Halo: Reach (MCC)", 2010, 6.82, 195, 1.2, ["armour abilities: sprint, evade (a dodge), jetpack, armour lock", "crouch jumping"])]:
    g = Game(gid, name, year, "Blam!", "arena shooters", "Forge m", 1.8 / SPART,
             "speeds measured in Forge metres; scaled to 1.8 m taking the Spartan as about 7 ft (2.134 m, the H2EK player reference): 0.8435 m per Forge metre")
    rise = (jpct / 100 - 1) * 1.8
    g.u("speed", run, HFM + f" (default Slayer movement speed {run} m/s)", conf="measured", rawname="run", per="/s",
        note="one player's measurement over the Forge grid; the 1.8 m rescale is an assumption")
    g.v("gravity", 8 * rise / air**2, HFM + f" (jump peak {jpct} % of the Spartan's height, {air} s in the air)", "estimated",
        f"derived from the measured jump: rise = ({jpct} - 100) % of 1.8 m = {rise:.2f} m, g = 8 x rise / airtime^2", raw=f"{jpct} %, {air} s")
    g.v("jump", 4 * rise / air, HFM + f" (jump peak {jpct} %, {air} s)", "estimated", f"take-off speed = 4 x rise / airtime = 4 x {rise:.2f} / {air}", raw=f"{jpct} %, {air} s")
    g.toggle("dash", False, HFM, "no dash in the default loadouts (Reach's Evade armour ability is a dodge)" if gid == "halo-reach" else "no dash")
    g.toggle("grapple", False, HFM, "no grapple (Halo Infinite adds the Grappleshot)")
    g.fact("jumpHeight", rise, "m", HFM + f" ({jpct} % head height at the apex)", "estimated", f"{jpct - 100} % of the player's height, scaled to 1.8 m; Quark: 1.22 m")
    g.fact("airTime", air, "s", HFM, "measured", "Quark: 0.67 s")
    if gid == "halo3":
        g.fact("mlgSpeed", 8.45 * 1.8 / SPART, "m/s", HFM + " (Halo 3 MLG settings 8.45 m/s)", "measured", "MLG settings; the post's percentages for it disagree (110% in the settings list, 90% beside the measurement)")
    else:
        g.fact("mlgSpeed", 7.5 * 1.8 / SPART, "m/s", HFM + " (Reach MLG 7.5 m/s, 110% speed, 150% gravity)", "measured")
        # Cross-check: a Forge editor that reads Reach's tags (reverse-engineered) gives the authored numbers in world units (1 wu = 10 ft = 3.048 m).
        HMS = GH + "/Sopitive/HaloMapStudio/blob/main/crates/hms-app/src/"
        WU = 3.048 * 1.8 / SPART
        g.fact("tagRunSpeed", 2.2 * WU, "m/s", HMS + "film_anim.rs#L133 (Spartan run forward 2.200 wu/s, read from matg @player information)", "measured",
               "2.2 wu/s x 3.048 m/wu, rescaled like the rest of this file: within 2% of the bungie.net measurement (the speed value above)")
        g.fact("tagJumpApex", 2.3**2 / (2 * 4.1712594) * WU, "m", HMS + "play_phys.rs#L425-446 (jump velocity 2.300 wu/s, gravity 4.1712594 wu/s2 from the engine's physics constants)", "estimated",
               "v^2/2g = 0.634 wu, rescaled: within 5% of the measured jump height above; the tool notes the biped's integration isn't verified")
        g.fact("tagAirTime", 2 * 2.3 / 4.1712594, "s", HMS + "play_phys.rs#L444-446", "estimated", "2v/g = 1.10 s against the 1.2 s measured")
        g.src("HaloMapStudio (reads Reach's tags): play_phys.rs", HMS + "play_phys.rs")
    g.d["extras"] = extra + ["slow, high jumps compared with arena shooters: the airtime (" + str(air) + " s) is about twice Quark's"]
    g.d["notes"] = ("One player's measurements (bungie.net, 2011): run speed over the Forge grid, jump height in hundredths of the Spartan's height and airtime. "
                    "Gravity and jump speed are derived from the height and airtime (estimates), and everything is rescaled to a 1.8 m player taking the Spartan as 7 ft.")
    g.src("bungie.net: classic Halo movement speed, jump height, gravity", HFM); g.src("H2EK: Player Statistics and Metrics (Spartan about 7 ft)", "https://www.h2maps.net/editingkit/player%20statistics%20and%20metrics.html")
    games.append(g.write())

DE = "https://steamcommunity.com/sharedfiles/filedetails/?id=2214688600"
g = Game("doom-eternal", "DOOM Eternal", 2020, "id Tech 7", "movement shooters", "m", 1.0,
         "id Tech 7's cvars are in metres (eye height pm_normalViewHeight 1.657 m, i.e. about a 1.8 m player): values as given")
g.v("gravity", 20.307301, DE + " (g_gravity 20.307301)", "exact", "the cvar default (800 in/s2 in metres)", raw="g_gravity 20.307301")
g.v("speed", 9.525, DE + " (pm_runspeed 9.525; pm_sprintspeed 9.525)", "exact", "the run speed (375 in/s); backwards x0.9 (pm_backSpeedRatio)", raw="pm_runspeed 9.525")
g.v("jump", math.sqrt(2 * 20.307301 * 1.3716), DE + " (pm_jumpheight 1.3716 with g_gravity 20.307301)", "estimated",
    "take-off speed for the 1.3716 m jump height: sqrt(2 x 20.307 x 1.3716)", raw="pm_jumpheight 1.3716 m")
g.v("dash_cooldown", 0.5, DE + " (envSuit_dashRetriggerTime 500 ms)", "exact", "the delay between dashes; each charge recharges in about 2 s (1 s upgraded) per a mod page", raw="500 ms")
g.v("dash_charges", 2, "https://doomwiki.org/wiki/Dash (two dashes in quick succession)", "measured")
g.toggle("dash", True, "https://doomwiki.org/wiki/Dash", "the dash (after the first missions)")
g.toggle("grapple", True, "https://www.nexusmods.com/doometernal/mods/36", "the Super Shotgun's Meat Hook pulls you to a demon (a weapon mod)")
g.fact("jumpHeight", 1.3716, "m", DE + " (pm_jumpheight 1.3716)", "exact", "Quark: 1.22 m; plus a double jump")
g.fact("eyeHeight", 1.65735, "m", DE + " (pm_normalViewHeight 1.65735)", "exact")
g.fact("dashRecharge", 2, "s", "https://www.nexusmods.com/doometernal/mods/36 (vanilla dash cooldown 2 s, 1 s upgraded)", "measured")
g.d["extras"] = ["double jump", "dash (2 charges)", "monkey bars, wall climbing on climbable walls", "Meat Hook grapple on the Super Shotgun", "no bunny hopping or air strafing to speak of"]
g.d["notes"] = ("Cvar defaults from a console-command guide (the game's cvars are in metres): run 9.525 m/s, gravity 20.31 m/s2, jump height 1.37 m, all within 5% of Quark's 9.5 / 22 / 1.22. "
                "Acceleration and friction cvars weren't listed. DOOM (2016) is not covered: only stopwatch timings that disagree with each other (230 vs 440 u/s) were found.")
g.src("Steam guide: Console Commands for DOOM Eternal", DE); g.src("Doom Wiki: Dash", "https://doomwiki.org/wiki/Dash"); g.src("Nexus Mods: Locomotive (vanilla values it changes)", "https://www.nexusmods.com/doometernal/mods/36")
games.append(g.write())

# ------------------------------------------------------------------ mainstream shooters
COD = "https://wiki.zeroy.com/index.php/Call_of_Duty_:_A_Study_on_FPS"
CJ = GH + "/callofduty4x/CoD4x_Server/blob/master/src/bg_jump.cpp"
g = Game("cod4", "Call of Duty 4: Modern Warfare", 2007, "IW 3.0 (id Tech 3 derived)", "mainstream shooters", "u", 0.0254,
         "1 unit = 1 inch (the CoD modding wiki's study), 0.0254 m/u; the player box height isn't published, so not rescaled to 1.8 m")
g.u("gravity", 800, "https://xgamingserver.com/configs/cod4 (g_gravity: 'Default is 800')", rawname="g_gravity", per="/s2")
g.u("speed", 190, COD + " (max forward run 190 u/s with an SMG, trench gun or sniper class)", conf="measured", rawname="run", per="/s",
    note="weapon-dependent: assault class 180, machine gunner 166; strafe 152, back 133")
g.u("jump", math.sqrt(2 * 800 * 39), CJ + "#L358 (jump_height 39) with g_gravity 800", conf="estimated", rawname="sqrt(2 x 800 x 39) =", per="/s",
    note="CoD sets the jump by its height (jump_height), not a speed")
g.v("friction", 5.5, "https://www.gamerconfig.eu/commands/call-of-duty-4-modern-warfare/ (friction 5.5)", "measured", "the dvar list's default; the Quake friction formula (CoD's movement is Quake III-derived)")
g.toggle("dash", False, COD, "no dash")
g.toggle("grapple", False, COD, "no grapple")
g.fact("jumpHeight", 39 * 0.0254, "m", CJ + "#L358 (jump_height 39.0, 'the maximum height of a player's jump')", "exact", "measured 39.5-42 u depending on frame rate (125 fps: 41); Quark: 1.22 m")
g.fact("strafeSpeed", 152 * 0.0254, "m/s", COD, "measured")
g.fact("backSpeed", 133 * 0.0254, "m/s", COD, "measured")
g.d["extras"] = ["jump slowdown: landing cuts speed to x0.65 (x0.5 after a high fall) for a moment (bg_jump.cpp#L330-350, jump_slowdownEnable 1)",
                 "jump_stepSize 18: the step up allowed at the top of a jump arc", "sprint (limited time, scr_player_sprinttime 4 s)", "mantling, frame-rate dependent jump heights (the 125 / 250 / 333 fps tricks)"]
g.d["notes"] = ("CoD4's game code isn't public; the CoD4x server (a reverse-engineered rewrite) gives jump_height, a dvar list gives friction, a host's config gives gravity, and the run speeds are a modder's measurements. "
                "Modern Call of Duty (MW2019 / MWII / BO6) publishes no movement values; community testing (TrueGameData) gives sprint speeds (BO6 SMGs 6.6-6.7 m/s, pistols 6 m/s) but no base speed, gravity or jump, so those games aren't covered.")
g.src("CoD modding wiki: A Study on FPS (run speeds, jump heights)", COD); g.src("CoD4x Server bg_jump.cpp", CJ)
g.src("GAMERCONFIG: CoD4 console commands", "https://www.gamerconfig.eu/commands/call-of-duty-4-modern-warfare/"); g.src("XGamingServer: CoD4 server config", "https://xgamingserver.com/configs/cod4")
g.src("WZ Stats (TrueGameData): BO6 close range stats", "https://wzstats.gg/bo6/meta/close-range-meta")
games.append(g.write())

FNM = "https://fortnite.fandom.com/wiki/Movement"
g = Game("fortnite", "Fortnite (Battle Royale)", 2017, "Unreal Engine 4/5", "mainstream shooters", "m", 1.0, "the wiki gives metres; values as given (Unreal units are centimetres)")
g.v("speed", 5.48, FNM + " (running 5.48 m/s)", "measured", "running; sprinting 6.47 m/s, crouch-running 4.18 m/s, slow jog 2 m/s")
g.toggle("dash", False, FNM, "no dash (some seasons' items add one)")
g.toggle("grapple", False, FNM, "grapples are seasonal items, not standard movement")
g.fact("sprintSpeed", 6.47, "m/s", FNM + " (sprinting 6.47 m/s)", "measured")
g.fact("crouchSpeed", 4.18, "m/s", FNM + " (crouch running 4.18 m/s)", "measured")
g.fact("walkSpeed", 2, "m/s", FNM + " (jogging, slow 2 m/s)", "measured")
g.d["extras"] = ["sprint, slide, mantle (Chapter 5 on)", "building", "seasonal mobility items (grapples, launch pads, shockwave grenades)"]
g.d["notes"] = "Speeds from the Fortnite Wiki's movement-state table (a Pocket Tactics test over 1,836 m agreed: 5:39 actual against 5:35 predicted at 5.48 m/s). Gravity, jump and acceleration aren't published."
g.src("Fortnite Wiki: Movement", FNM); g.src("Pocket Tactics: racing across the Fortnite map", "https://www.pockettactics.com/fortnite/map-speed")
games.append(g.write())

g = Game("portal2", "Portal 2", 2011, "Source", "Source family", "u", S72, "assumed Source's 72 u player (as Half-Life 2), scaled to 1.8 m: 0.025 m/u")
g.u("gravity", 600, "https://developer.valvesoftware.com/wiki/Gravity (sv_gravity 600 in Half-Life 2, Portal and Portal 2)", rawname="sv_gravity", per="/s2")
g.u("speed", 175, "https://rules.portal2.sr/ (cl_forwardspeed / cl_sidespeed / cl_backspeed allowed 0 .. 175)", conf="measured", rawname="cl_forwardspeed", per="/s",
    note="the speedrun rules cap the move-speed cvars at 175, the game's default walk speed")
g.u("step", 18, MV + "#L100 (Source's sv_stepsize 18)", conf="estimated", rawname="sv_stepsize", note="Source's default, assumed unchanged")
g.toggle("dash", False, "https://rules.portal2.sr/", "no dash")
g.toggle("grapple", False, "https://rules.portal2.sr/", "no grapple")
g.d["extras"] = ["portals keep your momentum (flings)", "gels: repulsion (bounce) and propulsion (speed)", "aerial faith plates, excursion funnels", "air control is limited (a puzzle game, not a strafe game)"]
g.d["notes"] = "Portal 2's game code isn't public; gravity from the Valve Developer wiki, walk speed from the speedrun rules' cvar limits. Acceleration, friction and jump weren't found."
g.src("Valve Developer Community: Gravity", "https://developer.valvesoftware.com/wiki/Gravity"); g.src("Portal 2 speedrun rules (cvar limits)", "https://rules.portal2.sr/")
games.append(g.write())

CSI = GH + "/ValveSoftware/halflife/issues/1307"
CSG = "https://gist.github.com/Log1x/b99213403bcbef9b5f32e0b11c419f19"
g = Game("cs16-comp", "Counter-Strike 1.6 (server / competitive config)", 2003, "GoldSrc", "Source family", "u", S72, "player hull 72 u tall (as Half-Life), scaled to 1.8 m: 0.025 m/u")
g.u("gravity", 800, CSG + " (sv_gravity 800)", conf="measured", rawname="sv_gravity", per="/s2")
g.u("speed", 250, REGAME + "dlls/weapons.h#L1178 (KNIFE_MAX_SPEED 250)", rawname="knife max speed", per="/s", note="knife out, as the cs16 entry")
g.u("jump", math.sqrt(2 * 800 * 45), RP + "#L2629 (sqrt(2 x 800 x 45))", rawname="jump", per="/s")
g.v("ground_accel", 5, CSI + " (a Steam CS 1.6 server log: sv_accelerate 10 -> 5 once the game config runs); " + CSG + " (sv_accelerate 5)", "measured",
    "what CS 1.6 servers actually run (the engine default is 10): the slower acceleration makes counter-strafing matter")
g.v("friction", 4, CSI + " (sv_friction 4); " + CSG, "measured", "x2 near drops (edgefriction 2)")
g.u("stop_speed", 75, CSI + " (sv_stopspeed 100 -> 75); " + CSG + " (sv_stopspeed 75)", conf="measured", rawname="sv_stopspeed", per="/s")
air_uncapped(g, 10, 250, 30, CSI + " (sv_airaccelerate 10)", RP + " (air cap 30, as Half-Life)", "knife speed 250")
g.u("step", 18, CSI + " (sv_stepsize 18)", conf="measured", rawname="sv_stepsize")
g.u("ladder_speed", 200, REGAME + "pm_shared/pm_shared.h#L54 (MAX_CLIMB_SPEED 200)", rawname="MAX_CLIMB_SPEED", per="/s")
g.toggle("dash", False, RP, "no dash"); g.toggle("grapple", False, RP, "no grapple")
g.fact("jumpHeight", 45 * S72, "m", RP + "#L2629 (45 u)", "exact", "Quark: 1.22 m")
g.ground_facts(250 * S72, 5, 4, 75 * S72, "; edge friction not modelled")
g.d["extras"] = ["the jump penalty and bunny-hop limit of the cs16 entry", "fun / kreedz servers often raise sv_airaccelerate to 100 (as the cited server config does)"]
g.d["notes"] = ("The movement CS 1.6 servers actually run: sv_accelerate 5 and sv_stopspeed 75 instead of the engine's 10 and 100 (cs16 has the engine defaults). "
                "Sources: a server log in Valve's halflife issue tracker showing the values switching once the game's config runs, and a public server config. League configs weren't found published.")
g.src("ValveSoftware/halflife issue 1307 (CS 1.6 server cvar log)", CSI); g.src("Counter-Strike 1.6 server config (gist)", CSG); g.src("ReGameDLL_CS pm_shared.cpp", RP)
games.append(g.write())

# ------------------------------------------------------------------ index
CORE = ["gravity", "speed", "jump", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "step"]
def closeness(gd):
    ks = [k for k in CORE if k in gd["values"]]
    if not ks: return None, 0
    return round(sum(abs(math.log(gd["values"][k]["mult"])) for k in ks if gd["values"][k]["mult"] > 0) / len(ks), 3), len(ks)
order = ["Quake family", "Source family", "arena shooters", "hero/tactical shooters", "movement shooters", "mainstream shooters", "other"]
descs = {"Quake family": "Quake-engine and id-derived games: Quake-style acceleration and friction, the model Quark's pmove uses",
         "Source family": "GoldSrc and Source: Quake-style movement with a 30 u air cap and full-wish-speed air acceleration",
         "arena shooters": "other arena shooters (Unreal, Halo, Reflex, Diabotical): different movement models, from scripts, config dumps or community measurements",
         "hero/tactical shooters": "hero and tactical shooters with documented metric speeds",
         "movement shooters": "wallrun / slide / dash / grapple shooters (Respawn, DOOM Eternal)",
         "mainstream shooters": "Call of Duty and Fortnite: config values and community measurements",
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
