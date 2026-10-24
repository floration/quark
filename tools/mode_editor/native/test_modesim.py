#!/usr/bin/env python3
"""Checks quark_maptool's `modesim`, `modebatch` and `modecheck` (MODESIM.md): a run of every mode on its own map, the
NDJSON shapes, determinism, a .qmode's key rules taking effect, logic labels and errors, the stdin controls, and a batch
whose matches equal modesim's for the same seeds.

    python3 tools/mode_editor/native/test_modesim.py [path/to/quark_maptool]

The tool defaults to $QUARK_MAPTOOL, then build/quark_maptool. Exit code 0 = pass. Prints the speed (x real time) and
batch throughput (matches per second). Not in CI (owner's rule for the editor helpers): run it by hand."""
import json
import os
import subprocess
import sys
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


def run(cmd, req, stream=False, timeout=600, controls=None):
    args = [TOOL, cmd] + (["--stream"] if stream else [])
    if controls is None:
        p = subprocess.run(args, input=json.dumps(req) + "\n", capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        out, code = p.stdout, p.returncode
    else:   # write the request, then the controls after the first frame
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT)
        p.stdin.write(json.dumps(req) + "\n"); p.stdin.flush()
        lines = []
        for line in p.stdout:
            lines.append(line)
            if len(lines) == 3:
                for c in controls: p.stdin.write(c + "\n")
                p.stdin.flush()
        code = p.wait(timeout)
        out = "".join(lines)
    return code, [json.loads(l) for l in out.splitlines() if l.strip()]


def strip_perf(lines):
    out = []
    for l in lines:
        l = dict(l)
        l.pop("perf", None)
        out.append(l)
    return out


START_KEYS = {"type", "map", "base", "mode", "label", "custom", "logicApplied", "interpreted", "logic", "teams", "tickRate", "seconds", "until",
              "warnings", "seed", "rate", "realtime", "speed", "trace", "bots"}
FRAME_KEYS = {"type", "t", "tick", "phase", "round", "clock", "scoreA", "scoreB", "flags", "players"}
PLAYER_KEYS = {"id", "team", "alive", "pos", "score", "kills", "deaths", "objTime"}
END_KEYS = {"type", "t", "reason", "label", "logicApplied", "winner", "rounds", "roundResults", "result", "perf"}
PHASES = {"warmup", "freeze", "live", "roundEnd", "matchEnd"}

# every mode on the map built for it (CLAUDE.md's pool), with bots it plays with
MODES = [("vs", "bastion", 8), ("bomb", "depot", 8), ("race", "viaduct", 4), ("koth", "summit", 8), ("payload", "railyard", 8),
         ("ffa", "harbor", 6), ("npayload", "canal", 8), ("duel", "clockwork", 2), ("1v1", "temple", 2), ("tourney", "colosseum", 4),
         ("hide", "hamlet", 6), ("tag", "foundry", 6), ("points", "midtown", 8)]

print("help / modecheck")
code, lines = run("help", {})
names = [c["name"] for c in lines[-1]["commands"]]
check(all(n in names for n in ("modesim", "modebatch", "modecheck")), "help lists modesim, modebatch, modecheck")
code, lines = run("modecheck", {"text": open(os.path.join(ROOT, "tools", "modes", "koth.qlogic")).read(), "map": "summit"})
c = lines[-1]
check(code == 0 and c["ok"] and c["label"].startswith("koth#") and "serverRunsLogic" in c, "modecheck: koth's default compiles, label")
code, lines = run("modecheck", {"text": open(os.path.join(ROOT, "tools", "modes", "koth.qlogic")).read(), "map": "viaduct"})
check(code == 1 and any("hill" in e["msg"] for e in lines[-1]["errors"]), "modecheck --map: viaduct has no hill")
code, lines = run("modecheck", {"text": "custommode=x\nmode=koth\nlogic 1\nslot tick:\n  foo = \n"})
e = lines[-1]
check(code == 1 and e["errors"][0]["line"] == 5 and e["error"].startswith("5:"), "modecheck: an error at line:col of the whole file")

print("modesim: every mode on its own map (60 s of game time)")
speeds = []
for mode, mp, bots in MODES:
    req = {"map": mp, "mode": mode, "bots": bots, "seed": 7, "seconds": 60, "until": "seconds", "rate": 2}
    code, lines = run("modesim", req)
    if not check(code == 0 and lines and lines[0].get("type") == "start" and lines[-1].get("type") == "end", "%s on %s runs (exit %d)" % (mode, mp, code)):
        print("   ", lines[-1] if lines else "(no output)")
        continue
    s, end = lines[0], lines[-1]
    frames = [l for l in lines if l["type"] == "frame"]
    check(START_KEYS <= set(s), "%s start keys (missing %s)" % (mode, START_KEYS - set(s)))
    check(s["base"] == mode and s["map"] == mp and len(s["bots"]) == bots, "%s start: base, map, bots" % mode)
    check(not s["logicApplied"] and s["logic"]["present"] is False, "%s: the built-in mode has no logic to apply" % mode)
    check(frames and all(FRAME_KEYS <= set(f) for f in frames), "%s frame keys" % mode)
    check(all(f["phase"] in PHASES for f in frames), "%s phases are named" % mode)
    check(all(PLAYER_KEYS <= set(p) for f in frames for p in f["players"]), "%s player keys" % mode)
    check(all(frames[k]["t"] < frames[k + 1]["t"] for k in range(len(frames) - 1)), "%s frames in time order" % mode)
    check(END_KEYS <= set(end), "%s end keys (missing %s)" % (mode, END_KEYS - set(end)))
    check(end["reason"] in ("time", "matchEnd") and end["t"] <= 60.01, "%s ends at 60 s or the match end (%s at %s)" % (mode, end["reason"], end["t"]))
    check(end["perf"]["tickNs"]["avg"] > 0 and end["perf"]["speedup"] > 1, "%s perf" % mode)
    if mode in ("bomb", "payload", "points", "duel"):
        check(frames[-1].get("obj") is not None or mode == "duel", "%s frames carry the objective state" % mode)
    speeds.append((mode, end["perf"]["speedup"], end["perf"]["tickNs"]["avg"], end["perf"]["tickNs"]["p99"]))
for mode, sp, avg, p99 in speeds:
    print("    %-9s %5.0fx real time, server tick avg %6.0f ns, p99 %6.0f ns" % (mode, sp, avg, p99))

print("modesim: whole matches, determinism, rules")
req = {"map": "depot", "mode": "bomb", "bots": 8, "seed": 5, "rate": 1}
t0 = time.time()
code, a = run("modesim", req)
wall = time.time() - t0
code2, b = run("modesim", req)
check(code == 0 and a[-1]["reason"] == "matchEnd" and a[-1]["winner"]["team"] in ("a", "b"), "bomb on depot plays to the match end with a winning team")
check(strip_perf(a) == strip_perf(b), "the same request and seed give the same NDJSON (perf aside)")
rounds = a[-1]["roundResults"]
check(len(rounds) == a[-1]["rounds"] and rounds and all(r["winner"] in ("a", "b", None) for r in rounds), "round results")
evs = [e for f in a if f["type"] == "frame" for e in f.get("events", [])]
check(any(e["type"] == "kill" for e in evs) and any(e["type"] == "roundEnd" for e in evs) and any(e["type"] == "matchEnd" for e in evs), "kill / roundEnd / matchEnd events")
check(sum(1 for e in evs if e["type"] == "phase") >= 2 * len(rounds), "phase changes")
print("    bomb on depot: %.0f s of game time in %.2f s wall (%.0fx real time incl. start)" % (a[-1]["t"], wall, a[-1]["t"] / wall))
code, c = run("modesim", dict(req, seed=6))
check(strip_perf(c) != strip_perf(a), "another seed gives another match")

code, f = run("modesim", {"map": "harbor", "bots": 6, "seed": 2, "rate": 1})
check(code == 0 and f[0]["base"] == "ffa" and f[-1]["reason"] == "matchEnd" and f[-1]["winner"]["player"] is not None, "ffa on harbor (its for mode) ends with a player winning")
code, g = run("modesim", {"map": "harbor", "bots": 6, "seed": 2, "rate": 1, "rules": "custommode=short\nmode=ffa\ntimelimit=60\n"})
check(code == 0 and g[0]["custom"] == "short" and g[0]["label"].startswith("short") and g[-1]["t"] < 150 < f[-1]["t"],
      "a .qmode's key rules take effect: timelimit=60 ends the match early (%s s vs %s s)" % (g[-1]["t"], f[-1]["t"]))

rush = "custommode=rush\nmode=bomb\nrounds=3\nlogic 1\nparam bombFuse f32 = 25\nstate match plants[team] int = 0\n\nslot plant:\n  plants[attackers] = plants[attackers] + 1\n"
code, r = run("modesim", {"map": "depot", "bots": 8, "seed": 5, "rate": 1, "rules": rush})
s = r[0]
check(code == 0 and s["label"].startswith("rush#") and s["logic"]["present"] and "plant" in s["logic"]["interpreted"], "logic: label name#hash, interpreted slots")
if s["logic"]["hook"] == "pending":
    check(s["logicApplied"] is False and r[-1]["logicApplied"] is False and "why" in s["logic"], "logic before the game hook: logicApplied false, and why")
else:
    check(s["logicApplied"] is True and s["interpreted"], "logic with the game hook: applied")
check(max(x["scoreA"] for x in r if x["type"] == "frame") <= 3 and max(x["scoreB"] for x in r if x["type"] == "frame") <= 3, "rounds=3: first to 3")
if s["logic"]["hook"] != "pending":   # an interpreted kill slot changes the result: 10 a kill
    ten = "custommode=ten\nmode=ffa\ntimelimit=120\nlogic 1\nslot kill:\n  if killer != none and killer != victim: addScore(killer, 10)\n"
    code, k = run("modesim", {"map": "harbor", "bots": 6, "seed": 2, "rate": 1, "seconds": 120, "until": "time", "rules": ten})
    ps = k[-1]["result"]["players"] if code == 0 else []
    check(k[0]["logicApplied"] is True and k[-1]["logicApplied"] is True and k[0]["interpreted"] == ["kill"], "kill slot logic: applied, interpreted")
    check(ps and any(p["kills"] > 0 for p in ps) and all(p["score"] == 10 * p["kills"] for p in ps), "kill slot logic: every kill scored 10")
code, e = run("modesim", {"map": "depot", "rules": "custommode=x\nmode=bomb\nlogic 1\nslot tick:\n  foo = \n"})
check(code == 1 and e[-1]["errors"][0]["line"] == 5, "a logic error refuses the run, with line:col")
code, e = run("modesim", {"map": "nosuchmap"})
check(code == 1 and "error" in e[-1], "an unknown map is an error")

print("modesim --stream: controls")
code, s = run("modesim", {"map": "harbor", "bots": 4, "seed": 3, "realtime": True, "speed": 2, "rate": 4}, stream=True, controls=["stop"])
check(code == 0 and s[-1]["type"] == "end" and s[-1]["reason"] == "stopped" and s[-1]["t"] < 10, "stop ends a paced run")

print("modebatch")
t0 = time.time()
code, bl = run("modebatch", {"map": "depot", "mode": "bomb", "bots": 8, "seeds": 6, "firstSeed": 3}, stream=True)
wall = time.time() - t0
end = bl[-1]
check(code == 0 and end["type"] == "end" and end["matchesPlayed"] == 6 and end["errors"] == 0, "6 matches played")
check(sum(1 for l in bl if l["type"] == "match") == 6, "a match line per match")
check(end["teams"]["a"]["wins"] + end["teams"]["b"]["wins"] + end["teams"]["draws"] + end["timeouts"] == 6, "wins + draws + timeouts = matches")
check(len(end["players"]) == 8 and all("winRate" in p for p in end["players"]), "per bot rows")
check(end["csv"].count("\n") == 7 and end["length"]["avg"] > 0, "csv and lengths")
one = next(m for m in end["matches"] if m["seed"] == 5)
check(abs(one["t"] - a[-1]["t"]) < 1e-6 and one["winner"] == a[-1]["winner"], "batch seed 5 == modesim seed 5 (same length and winner)")
print("    modebatch: %d matches in %.1f s on %d threads = %.2f matches/s, %.0fx real time per match, %.0fx all threads"
      % (end["matchesPlayed"], wall, end["perf"]["threads"], end["perf"]["matchesPerSecond"], end["perf"]["speedupPerMatch"], end["perf"]["speedupTotal"]))
code, bl = run("modebatch", {"map": "harbor", "bots": 6, "seeds": 2, "seconds": 20})
check(code == 0 and bl[-1]["timeouts"] == 2 and "teams" not in bl[-1] and "draws" in bl[-1], "free for all batch: timeouts at the max seconds, no teams")

print("\n%s" % ("all passed" if not FAILS else "%d FAILED: %s" % (len(FAILS), "; ".join(FAILS))))
sys.exit(1 if FAILS else 0)
