#!/usr/bin/env python3
"""Tests for the physics editor's quark_maptool commands (README.md here). python3 stdlib only.

    python3 tools/physics_editor/native/test_phys.py [path/to/quark_maptool]

Runs every command, checks the numbers against the standard movement (a 1.22 m standing jump, 9.5 m/s run, ...),
checks that a physics set changes them the way physics says it should (gravity x0.5 doubles the jump height), and
checks the constants and names this helper hardcodes against src/game/pmove.cpp / pmove.h (test time only: the
helper never reads the sources)."""
import json
import os
import re
import selectors
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "quark_maptool" + (".exe" if os.name == "nt" else ""))
failures = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        failures.append(what)


def near(a, b, tol):
    return a is not None and abs(a - b) <= tol


def run(cmd, req):
    t = time.time()
    p = subprocess.run([EXE, cmd], input=json.dumps(req).encode(), capture_output=True, timeout=60)
    wall = (time.time() - t) * 1000
    out = json.loads(p.stdout.decode())
    return out, p.returncode, wall


def source(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def const_value(text, name):
    m = re.search(r"constexpr\s+(?:float|int)\s+" + name + r"\s*=\s*([-0-9.e]+)f?", text)
    return float(m.group(1)) if m else None


print("physparams")
pp, rc, wall = run("physparams", {})
check(rc == 0 and len(pp["params"]) == 34, "34 movement settings (MV_COUNT)")
keys = {p["key"]: p for p in pp["params"]}
check(all(p["min"] == 0.1 and p["max"] == 5 for p in pp["params"]), "move_* range 0.1..5 (CustomRules::fromText's clamp)")
top = {p["key"]: p for p in pp["top"]}
check(top["gravity"]["min"] == 0.1 and top["gravity"]["max"] == 3, "gravity 0.1..3")
check(top["speed"]["min"] == 0.25 and top["jump"]["max"] == 3, "speed 0.25..3, jump ..3")
check(near(pp["constants"]["standingJumpHeight"], 1.2244, 0.001), "standing jump 1.22 m (v^2/2g)")
check(keys["crouch_speed"].get("inactive") is True, "crouch speed marked inactive (crouching was removed)")
groups = {p["group"] for p in pp["params"]}
check(groups == {"ground", "air", "dash", "super dash", "grapple", "ladder", "general"}, "groups: %s" % sorted(groups))

# every constant name and base value the helper hardcodes matches the source
pm = source("src/game/pmove.cpp") + source("src/game/pmove.h")
bad = []
for p in pp["params"] + pp["top"]:
    for c in p["constants"]:
        v = const_value(pm, c["name"])
        if v is None or abs(v - c["base"]) > 1e-4:
            bad.append("%s: %s=%s (source %s)" % (p["key"], c["name"], c["base"], v))
        if c["file"] == "src/game/pmove.h" and const_value(source("src/game/pmove.h"), c["name"]) is None:
            bad.append("%s not in pmove.h" % c["name"])
check(not bad, "physexport's constant table matches pmove.cpp / pmove.h " + "; ".join(bad))
# the strafe bot's copies (sim.cpp strafeYaw)
check(const_value(pm, "kStrafeCapAD") == 1.1 and const_value(pm, "kStrafeAccelAD") == 1.15, "strafe bot's A/D factors (1.1, 1.15) match pmove.cpp")
check("(h - cross) / (0.25f * cross)" in pm and "kAirAccel * mt.m[MV_AIR_ACCEL]) * 10.0f" in pm, "strafe bot's boost ramp and x10 air accel match pmove.cpp")

print("physroom")
room, rc, wall = run("physroom", {})
check(rc == 0 and room["text"].startswith("#") and "version 2" in room["text"], "room is .qmap text, version 2")
check({p["id"] for p in room["parts"]} == {"flat", "wall", "ladder", "drop", "gaps", "stairs", "grapple"}, "room parts")
check(room["brushes"] > 30 and room["gaps"][0]["width"] == 2, "room parsed by Map::parse (%d brushes)" % room["brushes"])
parsed, rc, _ = run("parse", {"text": room["text"], "mapName": "physics_room"})
check(rc == 0 and len(parsed.get("ladders", [])) == 1, "the map editor's parse reads the room (1 ladder)")

print("physpresets (standard physics)")
pr, rc, wall = run("physpresets", {"run": True})
res = {p["id"]: p["result"] for p in pr["presets"]}
check(len(pr["presets"]) >= 14 and all(p["map"].startswith("room:") for p in pr["presets"]), "%d presets, each in a room part" % len(pr["presets"]))
for p in pr["presets"]:
    part = p["map"][5:]
    start = p["scenario"]["start"]["pos"]
    box = next(r["box"] for r in room["parts"] if r["id"] == part)
    check(box["min"][0] - 0.5 <= start[0] <= box["max"][0] + 0.5 and box["min"][2] - 0.5 <= start[2] <= box["max"][2] + 0.5,
          "%s starts in room:%s" % (p["id"], part))
r = res
check(near(r["standing_jump"]["jumpHeight"], 1.22, 0.01), "standing jump height %.4f ~ 1.22" % r["standing_jump"]["jumpHeight"])
check(near(r["standing_jump"]["airTime"], 0.667, 0.012), "standing jump air time %.3f ~ 2v/g" % r["standing_jump"]["airTime"])
check(r["run_up"]["topSpeed"] == 9.5 and r["run_up"]["timeToTopSpeed"] < 0.1, "run speed 9.5, at it in %.3f s (~80 ms)" % r["run_up"]["timeToTopSpeed"])
check(near(r["running_jump"]["jumpDistance"], 9.5 * 0.66, 0.1), "running jump %.2f m ~ 9.5 x air time" % r["running_jump"]["jumpDistance"])
check(0.5 < r["stop"]["stopDistance"] < 3 and r["stop"]["stopTime"] < 1, "stop %.2f m in %.3f s" % (r["stop"]["stopDistance"], r["stop"]["stopTime"]))
check(all(near(h, 9.5, 0.01) for h in r["bhop_plain"]["hopSpeeds"]), "bhop with W only keeps 9.5")
hs = r["bhop_strafe"]["hopSpeeds"]
check(len(hs) >= 9 and all(b > a for a, b in zip(hs, hs[1:])) and hs[0] > 9.5, "strafe bot gains every hop: %s" % hs[:4])
check(near(r["dash"]["dashDistance"], 24 * 0.12, 0.1) and r["dash"]["dashExitSpeed"] == 11, "dash 2.88 m, leaves at 11")
check(r["dash_jump"]["dashJumpSpeed"] == 24, "dash jump keeps the 24 m/s burst")
check(near(r["super_dash"]["superDashSpeed"], 14.5, 0.01) and near(r["super_dash"]["jumpHeight"], 0.79, 0.02), "super dash 9.5 + 5, 0.79 m hop at 45 deg")
check(r["dash_wall"]["bounceSpeed"] == 16, "wall bounce 16 m/s")
check(r["grapple_swing"]["hookTopSpeed"] > 23 and r["grapple_swing"]["distance"] > 20, "grapple pulls to its 24 m/s")
check(near(r["ladder_climb"]["climbHeight"], 9, 0.05) and 1.0 < r["ladder_climb"]["ladderTime"] < 1.5, "ladder: up the 8 m tower (volume to 9 m)")
check(near(r["stairs"]["maxHeight"], 3, 0.01), "stairs: up the 0.5 m risers, stopped by 0.75 m ones")
check(r["max_gap"]["widestGap"] == 7, "running jump clears the 7 m gap, not the 8 m")
check(near(r["fall"]["landImpact"], (2 * 22 * 20) ** 0.5, 0.2), "20 m fall lands at ~29.7 m/s")

print("physsim")
sim, rc, wall = run("physsim", {"physics": {"gravity": 0.5}, "preset": "standing_jump", "compare": ["standard"]})
a, b = sim["runs"][0]["metrics"], sim["runs"][1]["metrics"]
check(near(a["jumpHeight"] / b["jumpHeight"], 2.0, 0.02), "gravity x0.5: jump %.3f m = 2 x %.3f" % (a["jumpHeight"], b["jumpHeight"]))
check(near(a["airTime"] / b["airTime"], 2.0, 0.03), "gravity x0.5: air time x2")
sim, rc, _ = run("physsim", {"physics": {"jump": 1.5}, "preset": "standing_jump"})
check(near(sim["runs"][0]["metrics"]["jumpHeight"], 1.22 * 2.25, 0.03), "jump x1.5: height x2.25")
sim, rc, _ = run("physsim", {"physics": {"speed": 1.2}, "preset": "run_up"})
check(near(sim["runs"][0]["metrics"]["topSpeed"], 11.4, 0.01), "speed x1.2: 11.4 m/s")
sim, rc, _ = run("physsim", {"physics": {"move": {"step": 1.4}}, "preset": "stairs"})
check(near(sim["runs"][0]["metrics"]["maxHeight"], 6, 0.01), "step x1.4 (0.77 m): climbs the 0.75 m risers too")
sim, rc, _ = run("physsim", {"physics": {"move": {"ladder_speed": 2}}, "preset": "ladder_climb"})
check(sim["runs"][0]["metrics"]["ladderTime"] < 0.75 * r["ladder_climb"]["ladderTime"], "ladder speed x2 climbs faster")
sim, rc, _ = run("physsim", {"physics": {"dash": False}, "preset": "dash"})
check(sim["runs"][0]["metrics"]["dashes"] == 0, "dash off: no dash")
sim, rc, _ = run("physsim", {"qmode": "custommode=x\nmove_dash_speed=1.5\n", "preset": "dash"})
check(near(sim["runs"][0]["metrics"]["dashDistance"], 36 * 0.12, 0.15), "qmode text: dash speed x1.5")
sim, rc, _ = run("physsim", {"physics": {"gravity": 99}, "preset": "standing_jump"})
check(sim["runs"][0]["clamped"] == ["gravity"] and sim["runs"][0]["physics"]["gravity"] == 3, "gravity 99 clamped to 3")
sim, rc, _ = run("physsim", {"preset": "nope"})
check(rc == 1 and "error" in sim, "unknown preset is an error")
ev = run("physsim", {"preset": "dash_wall"})[0]["runs"][0]["events"]
check([e["kind"] for e in ev][:2] == ["dash", "bounce"], "events: dash then bounce: %s" % [e["kind"] for e in ev])
ev = run("physsim", {"preset": "grapple_swing"})[0]["runs"][0]["events"]
kinds = [e["kind"] for e in ev]
check("hook" in kinds and "unhook" in kinds and "jump" in kinds and "land" in kinds, "grapple events: %s" % kinds)
ev = run("physsim", {"preset": "ladder_climb"})[0]["runs"][0]["events"]
check("ladder" in [e["kind"] for e in ev], "ladder event")
ev = run("physsim", {"preset": "stairs"})[0]["runs"][0]["events"]
check([e["kind"] for e in ev].count("step") == 6 and "wall" in [e["kind"] for e in ev], "6 steps up then a wall")
ev = run("physsim", {"preset": "super_dash"})[0]["runs"][0]["events"]
check("superDash" in [e["kind"] for e in ev], "superDash event")
ev = run("physsim", {"preset": "dash_jump"})[0]["runs"][0]["events"]
check("dashJump" in [e["kind"] for e in ev], "dashJump event")
sim, rc, _ = run("physsim", {"preset": "running_jump", "map": "temple",
                             "scenario": None})
check(rc == 0 and sim["map"] == "temple", "a real map by name")
# the strafe bot's optimal angle against a search through pmove itself
scen = {"start": {"pos": [0, 0, 0], "yaw": -90}, "durationMs": 4000,
        "steps": [{"atMs": 0, "forward": 127}, {"atMs": 500, "side": 127, "jump": True, "strafeBot": True, "strafeSwitch": True}]}
fast = run("physsim", {"scenario": scen})[0]["runs"][0]["metrics"]["hopSpeeds"]
scen["steps"][1]["strafeBot"] = "search"
slow = run("physsim", {"scenario": scen})[0]["runs"][0]["metrics"]["hopSpeeds"]
check(all(f >= s - 0.05 for f, s in zip(fast, slow)), "the strafe bot's angle is as good as searching pmove: %s vs %s" % (fast[:3], slow[:3]))

# timing: 10 s x 3 physics sets
scen10 = {"name": "10 s", "start": {"pos": [0, 0, 0], "yaw": -90}, "durationMs": 10000,
          "steps": [{"atMs": 0, "forward": 127}, {"atMs": 500, "side": 127, "jump": True, "strafeBot": True, "strafeSwitch": True},
                    {"atMs": 5000, "forward": 127, "dash": True}, {"atMs": 5020, "forward": 127, "jump": True}]}
best = 1e9
for _ in range(3):
    sim, rc, wall = run("physsim", {"physics": {"gravity": 0.8}, "compare": ["standard", {"speed": 1.2}], "scenario": scen10})
    best = min(best, wall)
check(rc == 0 and len(sim["runs"]) == 3 and best < 100, "10 s x 3 sets: %.1f ms whole process (sim %.1f ms)" % (best, sim["simMs"]))

print("physexport")
ex, rc, _ = run("physexport", {"physics": {"gravity": 0.8, "move": {"dash_charges": 1.5, "dash_cooldown": 0.5}}, "name": "Test Set"})
check("src/game/pmove.cpp: kGravity = 22.0f -> 17.6f" in ex["cpp"], "cpp: kGravity line")
check("src/game/pmove.h: kDashCharges = 2 -> 3" in ex["cpp"] and "kDashRecharge = 1.2f -> 0.6f" in ex["cpp"], "cpp: dash charges and both cooldowns")
check("custommode=Test Set" in ex["qmode"] and "move_dash_charges=1.5" in ex["qmode"], "qmode text")
std = run("physexport", {})[0]
check("no change" in std["cpp"] and std["changes"] == [], "standard: no change")

print("equations (v2: expr.<key>= lines, src/game/moveexpr.h)")
ex = pp["expr"]
check([i["name"] for i in ex["inputs"]] == ["p.speed", "p.hspeed", "p.vspeed", "p.air", "p.ground", "p.onground", "p.charges",
                                           "p.dashing", "p.hooked", "p.height"], "physparams lists the inputs")
check({"sqrt", "pow", "curve", "ease", "clamp", "lerp", "step"} <= {f["name"] for f in ex["functions"]} and "inout" in ex["ease"],
      "physparams lists the functions and ease modes")
pe, rc, _ = run("physexpr", {"expr": {"friction": "curve(p.hspeed, 2, 12, 2, 0.6, \"inout\")", "stop_speed": "friction * 2",
                                      "jump": "sqrt(speed)", "speed": "1.21", "air_cap": "sped", "air_accel": "air_control",
                                      "air_control": "air_accel"}, "samples": 11})
it = {i["key"]: i for i in pe["items"]}
check(rc == 0 and it["friction"]["dynamic"] and it["friction"]["inputs"] == ["p.hspeed"] and it["friction"]["value"] == 2,
      "a dynamic curve: reads p.hspeed, x2 at rest")
fs = it["friction"]["samples"][0]
check(fs["input"] == "p.hspeed" and len(fs["x"]) == 11 and fs["y"][0] == 2 and near(fs["y"][-1], 0.6, 1e-6) and
      all(b <= a for a, b in zip(fs["y"], fs["y"][1:])), "sampled over p.hspeed 0..50: 2 down to 0.6, never rising")
check(it["stop_speed"]["dynamic"] and it["stop_speed"]["deps"] == ["friction"] and it["stop_speed"]["direct"] == []
      and near(it["stop_speed"]["samples"][0]["y"][-1], 1.2, 1e-6), "dynamic through another setting (friction x 2)")
check(not it["jump"]["dynamic"] and near(it["jump"]["value"], 1.1, 1e-6) and pe["order"].index("speed") < pe["order"].index("jump"),
      "static: sqrt(speed) = 1.1, after speed")
check(not it["air_cap"]["ok"] and "unknown name 'sped'" in it["air_cap"]["error"], "a parse error, with where and why")
check("cycle" in it["air_accel"]["error"] and "cycle" in it["air_control"]["error"], "a cycle is an error on each")
pe2, rc, _ = run("physexpr", {"expr": "p.air > 0.25 ? 1.6 : 0.8", "key": "gravity", "inputs": {"p.air": [0, 0.5]}, "samples": 3})
g = pe2["items"][0]["samples"][0]
check(g["x"] == [0, 0.25, 0.5] and g["y"] == [0.8, 0.8, 1.6] and near(g["abs"][2], 35.2, 1e-4), "a string expr with key, a given input range")
sim, rc, _ = run("physsim", {"physics": {"expr": {"friction": "curve(p.hspeed, 2, 9.5, 2, 1)"}}, "preset": "stop", "compare": ["standard"]})
a, b = sim["runs"][0]["metrics"], sim["runs"][1]["metrics"]
check(b["stopDistance"] * 0.6 < a["stopDistance"] < b["stopDistance"] * 0.75 and a["stopTime"] < b["stopTime"],
      "physsim plays a friction curve: stop %.3f m in %.3f s (standard %.3f m)" % (a["stopDistance"], a["stopTime"], b["stopDistance"]))
check(sim["runs"][0]["physics"]["expr"] == {"friction": "curve(p.hspeed, 2, 9.5, 2, 1)"} and not sim["runs"][0]["physics"]["standard"],
      "the answer echoes the equation")
sim, rc, _ = run("physsim", {"qmode": "custommode=q\nexpr.gravity=p.air > 0.15 ? 2 : 1\n", "preset": "standing_jump"})
check(near(sim["runs"][0]["metrics"]["jumpHeight"], 1.0393, 0.002), "qmode expr line: gravity x2 after 0.15 s in the air (%.4f m)" % sim["runs"][0]["metrics"]["jumpHeight"])
sim, rc, _ = run("physsim", {"physics": {"expr": {"friction": "1 + 0 * p.speed"}}, "preset": "stop", "sampleMs": 1, "compare": ["standard"]})
check(sim["runs"][0]["trace"] == sim["runs"][1]["trace"], "an always-1 equation runs exactly the standard trace")
bad, rc, _ = run("physsim", {"physics": {"expr": {"nothing": "1"}}, "preset": "stop"})
check(rc == 1 and "isn't a movement setting" in bad["error"], "an unknown key is an error")
ex2, rc, _ = run("physexport", {"physics": {"expr": {"friction": "curve(p.hspeed, 2, 9.5, 2, 1)", "jump": "sqrt(speed)", "speed": "1.21"}}})
check("expr.friction=curve(p.hspeed, 2, 9.5, 2, 1)" in ex2["qmode"] and "move_friction=2\n" in ex2["qmode"] and "\njump=1.1\n" in ex2["qmode"],
      "physexport: expr lines, and the plain lines at the value at rest / resolved")
check("// dynamic: friction = curve(p.hspeed, 2, 9.5, 2, 1)" in ex2["cpp"] and "// static: jump = sqrt(speed)" in ex2["cpp"],
      "physexport: the C++ list names the equations")
st = None

print("physdrive --stream")
p = subprocess.Popen([EXE, "physdrive", "--stream"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
sel = selectors.DefaultSelector()
sel.register(p.stdout, selectors.EVENT_READ)


def readline():
    if not sel.select(timeout=10):
        p.kill()
        raise SystemExit("physdrive: no answer within 10 s")
    return json.loads(p.stdout.readline().decode())


def send(o):
    p.stdin.write(((o if isinstance(o, str) else json.dumps(o)) + "\n").encode())
    p.stdin.flush()
    return readline()


try:
    first = send({"map": "room:flat"})
    check(first["type"] == "start" and first["onGround"] and first["pos"] == [0, 0, 0], "start line")
    worst, kinds = 0, []
    for i in range(80):
        btn = ["jump"] if 20 <= i < 22 else []
        st = send({"cmd": [{"forward": 127, "yaw": -90, "buttons": btn, "msec": 1}] * 16})
        worst = max(worst, st["us"])
        kinds += [e["kind"] for e in st["events"]]
    check(near(st["t"], 1.28, 1e-9) and st["hspeed"] == 9.5, "1.28 s of forward: 9.5 m/s (t %.3f)" % st["t"])
    check(kinds == ["jump", "land"], "drive events: %s" % kinds)
    check(worst < 1000, "each 16 ms batch answered in %.1f us of compute (< 1 ms)" % worst)
    st = send({"physics": {"gravity": 0.5}})
    check(st["physics"]["changed"] == ["gravity"], "physics swapped live")
    st = send("reset")
    check(st["t"] == 0 and st["pos"] == [0, 0, 0], "reset")
    st = send({"cmd": [{"buttons": {"jump": True}, "msec": 8}] + [{"msec": 8}] * 200})
    land = [e for e in st["events"] if e["kind"] == "land"]
    check(land and near(land[0]["height"], 2.44, 0.03), "gravity x0.5 in drive: 2.44 m jump")
    st = send({"physics": {"expr": {"gravity": "p.air > 0.15 ? 2 : 1"}}})
    check(st["physics"]["exprs"][0]["dynamic"], "an equation swapped in live")
    st = send("reset")
    st = send({"cmd": [{"buttons": {"jump": True}, "msec": 8}] + [{"msec": 8}] * 200})
    land = [e for e in st["events"] if e["kind"] == "land"]
    check(land and near(land[0]["height"], 1.0393, 0.003), "the equation in drive: %.4f m jump" % (land[0]["height"] if land else 0))
    st = send("bogus")
    check(st["type"] == "warning", "a bad line is a warning")
    st = send("stop")
    check(st["type"] == "done", "stop")
    p.wait(timeout=10)
    check(p.returncode == 0, "exit 0")
finally:
    if p.poll() is None:
        p.kill()

print()
print("%d failed" % len(failures) if failures else "all passed")
sys.exit(1 if failures else 0)
