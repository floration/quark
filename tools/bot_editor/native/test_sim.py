#!/usr/bin/env python3
"""Checks quark_maptool's `sim`, `simbatch` and `simcheck` (SIM.md): runs a few short matches and checks the NDJSON
shapes, match-end handling, objective overrides, the stdin controls, determinism and batch == sim for one seed.

    python3 tools/bot_editor/native/test_sim.py [path/to/quark_maptool]

The tool defaults to $QUARK_MAPTOOL, then build/quark_maptool, then build-sim/quark_maptool. Exit code 0 = pass.
Prints throughput (ticks per second per match, matches per second in batch)."""
import json
import math
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def find_tool():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in (os.environ.get("QUARK_MAPTOOL"), os.path.join(ROOT, "build", "quark_maptool"), os.path.join(ROOT, "build-sim", "quark_maptool")):
        for q in (p, p and p + ".exe"):
            if q and os.path.isfile(q):
                return q
    sys.exit("no quark_maptool: build it (cmake --build build --target quark_maptool) or pass its path")


TOOL = find_tool()
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("  FAIL", what)
    return cond


def run(cmd, req, stream=False, timeout=300):
    args = [TOOL, cmd] + (["--stream"] if stream else [])
    p = subprocess.run(args, input=json.dumps(req) + "\n", capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    lines = [json.loads(l) for l in p.stdout.splitlines() if l.strip()]
    return p.returncode, lines


START_KEYS = {"type", "map", "mode", "tickRate", "rate", "seconds", "until", "realtime", "speed", "seed", "teams", "brainSupport", "bots"}
START_BOT_KEYS = {"id", "name", "team", "skill", "brain", "brainLabel", "brainStatus", "objective"}
FRAME_BOT_KEYS = {"id", "team", "alive", "pos", "yaw", "pitch", "vel", "health", "weapon", "weaponId", "state", "decision", "target",
                  "goal", "waypoint", "onGround", "crouched", "hooked", "kills", "deaths", "score", "damage"}
EVENT_KEYS = {"t", "kind", "pos"}
END_KEYS = {"type", "t", "reason", "winner", "bots", "result", "perf"}
END_BOT_KEYS = {"id", "name", "team", "skill", "brain", "brainLabel", "kills", "deaths", "score", "damage", "objTime", "shots", "hits", "aliveTime", "won"}


def vec(v):
    return isinstance(v, list) and len(v) == 3 and all(isinstance(x, (int, float)) for x in v)


def check_sim_lines(name, lines, nbots):
    ok = check(len(lines) >= 3, f"{name}: start, frames, end")
    if not ok:
        return
    s, frames, e = lines[0], [l for l in lines if l.get("type") == "frame"], lines[-1]
    check(s.get("type") == "start" and START_KEYS <= set(s), f"{name}: start line keys {START_KEYS - set(s)}")
    check(len(s.get("bots", [])) == nbots and all(START_BOT_KEYS <= set(b) for b in s["bots"]), f"{name}: start bots")
    check(len(frames) >= 1, f"{name}: frames")
    with_path = without_path = 0
    times = []
    for f in frames:
        times.append(f["t"])
        if not check(len(f["bots"]) == nbots, f"{name}: a frame has every bot"):
            break
        for b in f["bots"]:
            if not check(FRAME_BOT_KEYS <= set(b), f"{name}: frame bot keys {FRAME_BOT_KEYS - set(b)}"):
                return
            check(vec(b["pos"]) and vec(b["vel"]), f"{name}: pos / vel vectors")
            check(isinstance(b["decision"], str) and b["decision"], f"{name}: decision label")
            check(b["goal"] is None or vec(b["goal"]), f"{name}: goal")
            if "path" in b:
                with_path += 1
                check(all(vec(p) for p in b["path"]), f"{name}: path points")
            else:
                without_path += 1
        for ev in f["events"]:
            check(EVENT_KEYS <= set(ev), f"{name}: event keys")
    check(times == sorted(times), f"{name}: frame times rise")
    check(with_path >= nbots, f"{name}: paths sent (first frame at least)")
    check(without_path > 0 or len(frames) < 3, f"{name}: paths only when changed")
    check(e.get("type") == "end" and END_KEYS <= set(e), f"{name}: end line keys {END_KEYS - set(e)}")
    check(len(e.get("bots", [])) == nbots and all(END_BOT_KEYS <= set(b) for b in e["bots"]), f"{name}: end bots")
    check("ticksPerSecond" in e.get("perf", {}), f"{name}: perf")


def main():
    print("tool:", TOOL)
    perf = []
    # 1. a duel, a free for all to its match end, a bomb match with every kind of override
    rc, L = run("sim", {"map": "temple", "mode": "duel", "seconds": 15, "realtime": False, "rate": 10,
                        "bots": [{"team": "a", "skill": "expert"}, {"team": "b", "skill": "advanced"}]})
    check(rc == 0, "sim temple: exit 0")
    check_sim_lines("sim temple duel", L, 2)
    check(L[-1]["reason"] == "time" and abs(L[-1]["t"] - 15) < 0.2, "sim temple: ran its 15 s")
    perf.append(("temple duel, 2 bots", L[-1]["perf"]))

    rc, L = run("sim", {"map": "harbor", "mode": "ffa", "timeLimit": 20, "seconds": 300, "realtime": False, "rate": 5, "bots": 6, "skill": "expert"})
    check_sim_lines("sim harbor ffa", L, 6)
    e = L[-1]
    check(e["reason"] == "matchEnd", "sim harbor: stopped at the match end")
    check(e["t"] < 60, "sim harbor: the 20 s match ended before 60 s")
    check(e["winner"] is not None and (e["winner"]["player"] is not None or "draw" in e["winner"]["text"] or "over" in e["winner"]["text"]), "sim harbor: a winner")
    check(sum(1 for b in e["bots"] if b["won"]) <= 1, "sim harbor: one winner")
    check(any(ev["kind"] == "match" for f in L if f["type"] == "frame" for ev in f["events"]), "sim harbor: a match event")
    check(e["result"]["phase"] == "match end", "sim harbor: the server is on its end screen (no map change, no new match)")
    perf.append(("harbor ffa, 6 bots", e["perf"]))

    parse = json.loads(subprocess.run([TOOL, "parse"], input=json.dumps({"name": "depot"}), capture_output=True, text=True, cwd=ROOT).stdout)
    spawns = [s["pos"] for s in parse["spawns"]]
    b_spawn = [s["pos"] for s in parse["spawns"] if s["team"] == "b"][0]
    req = {"map": "depot", "mode": "bomb", "seconds": 40, "realtime": False, "rate": 4, "until": "seconds",
           "bots": [{"team": "b", "skill": "expert", "objective": {"type": "goto", "pos": spawns[-1]}},
                    {"team": "b", "skill": "expert", "pos": b_spawn, "objective": {"type": "hold", "pos": b_spawn, "radius": 3, "chase": False}},
                    {"team": "a", "skill": "expert", "objective": {"type": "route", "points": [spawns[0], spawns[-1]], "loop": True}},
                    {"team": "a", "skill": "expert", "objective": {"type": "attack", "objectiveIndex": 1}},
                    {"team": "b", "skill": "advanced", "objective": {"type": "defend"}},
                    {"team": "a", "skill": "advanced", "pos": spawns[1], "yawDeg": 90}]}
    rc, L = run("sim", req)
    check_sim_lines("sim depot overrides", L, 6)
    decisions = {b["decision"] for f in L if f["type"] == "frame" for b in f["bots"]}
    for kind in ("goto", "hold", "route", "defend"):
        check(any(d.startswith("override:" + kind) for d in decisions), f"sim depot: override:{kind} seen")
    holds = [f["bots"][1] for f in L if f["type"] == "frame"]
    first_life = holds[:next((i for i, b in enumerate(holds) if not b["alive"]), len(holds))]
    far = max(math.dist(b["pos"], b_spawn) for b in first_life)
    check(far < 6, f"sim depot: the hold bot (placed on its post, chase off) stays within 6 m ({far:.1f} m)")
    check(L[0]["bots"][0]["objective"]["type"] == "goto", "sim depot: start echoes the objective")
    check(L[-1]["reason"] == "time", "sim depot: until seconds runs the time")
    perf.append(("depot bomb, 6 bots (5 overrides)", L[-1]["perf"]))

    # 2. determinism: the same request twice gives the same match
    rc, L2 = run("sim", req)
    strip = lambda e: json.dumps({k: v for k, v in e.items() if k != "perf"}, sort_keys=True)
    check(strip(L[-1]) == strip(L2[-1]), "sim depot: same seed, same match")

    # 3. controls (--stream): pause, step, speed, stop
    p = subprocess.Popen([TOOL, "sim", "--stream"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT, bufsize=1)
    p.stdin.write(json.dumps({"map": "temple", "mode": "ffa", "bots": 4, "seconds": 600, "rate": 10, "realtime": True, "speed": 2}) + "\n")
    p.stdin.flush()
    got = []
    reader = threading.Thread(target=lambda: [got.append(json.loads(l)) for l in p.stdout if l.strip()], daemon=True)
    reader.start()
    def frames():
        return [g for g in got if g.get("type") == "frame"]
    t0 = time.time()
    while len(frames()) < 3 and time.time() - t0 < 20:
        time.sleep(0.02)
    p.stdin.write("pause\n"); p.stdin.flush()
    time.sleep(0.3)
    n_paused = len(frames())
    time.sleep(0.4)
    check(len(frames()) == n_paused, "controls: pause holds")
    p.stdin.write("step\nstep\n"); p.stdin.flush()
    time.sleep(0.3)
    check(len(frames()) == n_paused + 2, f"controls: two steps, two frames ({len(frames()) - n_paused})")
    p.stdin.write("speed 0\nresume\n"); p.stdin.flush()
    time.sleep(0.5)
    fast = frames()
    check(len(fast) > n_paused + 20 and fast[-1]["t"] > 20, "controls: speed 0 runs as fast as it goes")
    p.stdin.write("stop\n"); p.stdin.flush()
    p.wait(timeout=20)
    reader.join(timeout=5)
    check(got and got[-1].get("type") == "end" and got[-1]["reason"] == "stopped", "controls: stop ends the run")
    check(got[0].get("realtime") is True, "controls: realtime echoed")

    # 4. simbatch: 4 matches, every core; the seed-1 match equals sim's
    breq = {"map": "harbor", "mode": "ffa", "timeLimit": 30, "seconds": 300, "matches": 4, "bots": 6, "skill": "expert",
            "progressEvery": 0.05}
    t = time.time()
    rc, B = run("simbatch", breq, stream=True)
    wall = time.time() - t
    check(rc == 0 and B and B[0]["type"] == "start" and B[-1]["type"] == "end", "simbatch: start ... end")
    matches = [l for l in B if l["type"] == "match"]
    check(len(matches) == 4 and sorted(m["seed"] for m in matches) == [1, 2, 3, 4], "simbatch: 4 match lines, seeds 1-4")
    check(all(m["reason"] == "matchEnd" and m["winner"] for m in matches), "simbatch: every match ended")
    e = B[-1]
    check(len(e["bots"]) == 6 and all(b["matches"] == 4 for b in e["bots"]), "simbatch: per bot")
    check(len(e["brains"]) == 1 and e["brains"][0]["botMatches"] == 24, "simbatch: per brain")
    check(sum(w["wins"] for w in e["playerWins"]) + e["draws"] == 4, "simbatch: wins add up")
    check(len(e["matches"]) == 4 and "matchesPerSecond" in e["perf"], "simbatch: per match + perf")
    rc, L = run("sim", {k: v for k, v in breq.items() if k not in ("matches", "progressEvery")} | {"realtime": False, "seed": 1})
    m1 = [m for m in matches if m["seed"] == 1][0]
    check([b["kills"] for b in L[-1]["bots"]] == [b["kills"] for b in m1["bots"]] and abs(L[-1]["t"] - m1["t"]) < 1e-6,
          "simbatch: the seed-1 match is sim's seed-1 match")
    perf.append(("simbatch harbor ffa x4", e["perf"]))

    # 4b. simbatch variants: several setups in one pool; each variant's matches are its own simbatch's
    vreq = {"map": "temple", "mode": "1v1", "matches": 3, "seconds": 300, "progressEvery": 0, "bots": [{"team": "a"}, {"team": "b"}],
            "maps": {"T": "temple"}, "variants": [{"map": "T"}, {"map": "dojo"}, {"map": "T", "bots": [{"team": "b"}, {"team": "a", "skill": "expert"}]}]}
    rc, VB = run("simbatch", vreq, stream=True)
    check(rc == 0 and VB and VB[0]["type"] == "start" and len(VB[0].get("variants", [])) == 3 and VB[0]["matches"] == 9, "simbatch variants: start lists 3 setups, 9 matches")
    vm = [l for l in VB if l["type"] == "match"]
    check(len(vm) == 9 and sorted(m["variant"] for m in vm) == [0, 0, 0, 1, 1, 1, 2, 2, 2], "simbatch variants: 3 match lines per variant")
    ve = VB[-1]
    check(ve["type"] == "end" and ve["matchesPlayed"] == 9 and [v["map"] for v in ve.get("variants", [])] == ["temple", "dojo", "temple"], "simbatch variants: per variant reports")
    check(ve["variants"][2]["bots"][1]["skill"] == "EXPERT", "simbatch variants: a variant's own bots")
    rc, D = run("simbatch", {k: v for k, v in vreq.items() if k not in ("variants", "maps")} | {"map": "dojo"}, stream=True)
    key = lambda m: (m["seed"], round(m["t"], 6), [b["kills"] for b in m["bots"]])
    check(sorted(key(m) for m in vm if m["variant"] == 1) == sorted(key(m) for m in D if m["type"] == "match"), "simbatch variants: dojo's matches equal its own simbatch's")
    rc, L = run("simbatch", vreq | {"variants": [{"map": "temple"}, {"bots": [{"objective": {"type": "goto"}}]}]})
    check(rc == 1 and "variant 1" in L[-1].get("error", ""), "simbatch variants: a bad variant is named")

    # 5. errors and simcheck
    rc, L = run("sim", {"map": "temple", "bots": [{"objective": {"type": "goto"}}]})
    check(rc == 1 and L and "error" in L[-1], "sim: a goto without pos is an error")
    rc, L = run("sim", {"map": "temple", "bots": [{"brain": "no_such_brain_anywhere"}]})
    check(rc == 1 and "not found" in L[-1].get("error", ""), "sim: a missing brain is an error")
    rc, L = run("simcheck", {"text": "nonsense"})
    check(rc == 0 and "brainSupport" in L[-1], "simcheck answers")

    print("\nthroughput:")
    for name, p in perf:
        if "ticksPerSecond" in p:
            print(f"  {name:36} {p['ticksPerSecond']:9.0f} ticks/s ({p['speedup']:.0f}x real time at 125 Hz)")
        else:
            print(f"  {name:36} {p['ticksPerSecondPerMatch']:9.0f} ticks/s per match, {p['matchesPerSecond']:.1f} matches/s on {p['threads']} threads")
    print("\n" + ("PASS" if not FAILS else f"FAIL ({len(FAILS)})"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
