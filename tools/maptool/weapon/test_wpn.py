#!/usr/bin/env python3
"""Checks the weapons editor's quark_maptool commands (README.md here): wpnparams, wpncheck, wpnstats, wpnrange,
wpnrangemap, and the weapon set keys of the bot editor's sim / simbatch (weapons, qmode, weaponsAllowed).

    python3 tools/maptool/weapon/test_wpn.py [path/to/quark_maptool]

The tool defaults to $QUARK_MAPTOOL, then build/quark_maptool. python3 stdlib only. When node is on PATH, wpnstats is
also compared row by row with the page's own arithmetic (web/js/stats.js) and its built-in catalog checked, when the weapons
editor is found (weapon_editor() below). Exit code 0 = pass."""
import json
import math
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def weapon_editor():
    """The weapons editor (weapon_editor/ in floration/quarkology) for the page's cross-checks: $QUARK_APP_REPO, else a
    quarkology or quarkology-repo clone beside this checkout. None: those checks are skipped."""
    for r in (os.environ.get("QUARK_APP_REPO"), os.path.join(ROOT, "..", "quarkology-repo"), os.path.join(ROOT, "..", "quarkology")):
        if r and os.path.isfile(os.path.join(r, "weapon_editor", "build_catalog.py")):
            return os.path.abspath(os.path.join(r, "weapon_editor"))
    return None


EDITOR = weapon_editor()


def find_tool():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in (os.environ.get("QUARK_MAPTOOL"), os.path.join(ROOT, "build", "quark_maptool")):
        for q in (p, p and p + ".exe"):
            if q and os.path.isfile(q):
                return q
    sys.exit("no quark_maptool: build it (cmake --build build --target quark_maptool) or pass its path")


TOOL = find_tool()
FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)
    return cond


def near(a, b, tol):
    return isinstance(a, (int, float)) and abs(a - b) <= tol


def run(cmd, req, timeout=300):
    t = time.time()
    p = subprocess.run([TOOL, cmd], input=json.dumps(req).encode(), capture_output=True, timeout=timeout, cwd=ROOT)
    wall = time.time() - t
    try:
        out = json.loads(p.stdout.decode())
    except ValueError:
        out = {"error": "not JSON: " + p.stdout.decode()[-300:] + p.stderr.decode()[-300:]}
    return out, p.returncode, wall


def run_lines(cmd, req, timeout=600):
    p = subprocess.run([TOOL, cmd, "--stream"], input=(json.dumps(req) + "\n").encode(), capture_output=True, timeout=timeout, cwd=ROOT)
    return [json.loads(l) for l in p.stdout.decode().splitlines() if l.strip()], p.returncode


def strip(a):   # an answer without its timings (determinism checks)
    if isinstance(a, dict):
        return {k: strip(v) for k, v in a.items() if k not in ("ms", "perf", "wallSeconds")}
    if isinstance(a, list):
        return [strip(x) for x in a]
    return a


SET_HOT = 'version 1\nweapons "hot"\nlabel rifle "HOT RIFLE"\nparam rifle.damage f32 = 9.9\nparam rocket.damage f32 = 300\n'
SET_LOGIC = 'version 1\nweapons "logic"\nslot hit rifle:\n  damage = damage * 2\n'
SET_FALL = ('version 1\nweapons "fall"\nparam rifle.falloffStart f32 = 10\nparam rifle.falloffEnd f32 = 40\nparam rifle.falloffMin f32 = 0.5\n'
            'param shotgun.pellets int = 12\ncurve sniper.falloff(dist f32) = points 0: 1, 20: 0.6\n')
SET_BAD = 'version 1\nweapons "bad"\nparam rocket.damagez f32 = 40\n'
SET_CLAMP = 'version 1\nweapons "clamp"\nparam rocket.damage f32 = 99999\n'

# ---------------------------------------------------------------------------------------------------------------
print("help")
h, rc, _ = run("help", {})
names = {c["name"] for c in h.get("commands", [])}
check({"wpnparams", "wpncheck", "wpnstats", "wpnrange", "wpnrangemap"} <= names, "help lists the wpn commands")

print("wpnparams")
p, rc, wall = run("wpnparams", {})
check(rc == 0 and p.get("format") == "qweapons-catalog", "the catalog (format qweapons-catalog)")
check(len(p.get("fields", [])) > 200 and all(f["key"] in p["values"] for f in p["fields"]), "values: every field (%d)" % len(p.get("fields", [])))
check(p.get("changed") == [] and p.get("ok") is True and p.get("name") is None, "the standard set: nothing changed")
check(len(p.get("weapons", [])) == 11 and p.get("defaultText", "").startswith("version 1"), "11 weapons, the default text")
check("range" in p.get("features", []) and "stats" in p.get("features", []), "features")
RIFLE = p["values"]["rifle.damage"]   # the standard rifle's damage (gamedefs.h), never a copy of it
qt = os.path.join(ROOT, "build", "quark_tests")
if os.path.isfile(qt):
    out = os.path.join(ROOT, "build", "wpn_test_catalog.json")
    subprocess.run([qt, "exportweapons", out], capture_output=True, timeout=120)
    if os.path.isfile(out):
        with open(out, encoding="utf-8") as f:
            cat = json.load(f)
        os.remove(out)
        check([x["key"] for x in cat["fields"]] == [x["key"] for x in p["fields"]] and cat["defaultText"] == p["defaultText"], "== quark_tests exportweapons")
        if EDITOR:
            bc = subprocess.run([sys.executable, os.path.join(EDITOR, "build_catalog.py"), "--quark-tests", qt, "--check"], capture_output=True, text=True, timeout=120)
            check(bc.returncode == 0, "the page's built-in catalog (web/js/catalog_data.js) is the game's: " + bc.stdout.strip())
p2, rc, _ = run("wpnparams", {"weapons": SET_HOT})
check(rc == 0 and set(p2["changed"]) == {"rifle.damage", "rocket.damage"} and p2["values"]["rocket.damage"] == 300, "a set: changed, values")
check(p2["labels"].get("rifle") == "HOT RIFLE" and p2["name"] == "hot" and set(p2["writes"]) == {"rifle.damage", "rocket.damage"}, "labels, name, writes")
p3, rc, _ = run("wpnparams", {"weapons": SET_BAD})
check(rc == 0 and p3["ok"] is False and p3["errors"] and p3["errors"][0]["line"] == 3 and "fields" in p3, "a bad set: the catalog, ok false, errors with line")

print("wpncheck")
c, rc, _ = run("wpncheck", {"weapons": SET_LOGIC})
check(rc == 0 and c["ok"] and c["hasLogic"] and c["ast"]["format"] == "qweapons-ast", "a set with logic: ok, hasLogic, the AST")
c, rc, _ = run("wpncheck", {"weapons": SET_BAD})
check(rc == 0 and not c["ok"] and c["errors"][0]["line"] == 3 and c["errors"][0]["col"] == 1 and "error" not in c, "errors with line:col (exit 0, no error key)")
c, rc, _ = run("wpncheck", {"weapons": SET_CLAMP})
check(c["ok"] and len(c["warnings"]) == 1, "a clamped value: a warning")
c, rc, _ = run("wpncheck", {"weapons": SET_HOT, "canonical": True})
check(c.get("canonical", "").startswith("version 1"), "canonical text")

print("wpnstats")
s, rc, wall = run("wpnstats", {})
ids = [r["id"] for r in s.get("rows", [])]
check(ids == ["rifle:primary", "rifle:alt", "rocket:primary:direct", "rocket:primary:splash", "rocket:alt:direct", "rocket:alt:splash",
              "knife:primary", "knife:alt", "sniper:primary", "laser:primary", "laser:alt", "shotgun:primary", "shotgun:alt",
              "sticky:primary", "crossbow:primary", "bubble:primary", "bubble:alt", "syringe:primary", "syringe:alt", "locator:primary"],
      "the page's row ids, in its order")
R = {r["id"]: r for r in s["rows"]}
ROW_KEYS = {"id", "weapon", "mode", "variant", "kind", "label", "notes", "refire", "pellets", "range", "speed", "splashRadius", "splashMax",
            "body", "head", "stkBody", "stkHead", "ttkBody", "ttkHead", "dpsBody", "dpsHead", "t10", "t20", "t40", "fall10", "fall20", "fall40",
            "headshots", "per"}
check(all(ROW_KEYS <= set(r) for r in s["rows"]), "row keys")
sn = R["sniper:primary"]
check(sn["head"] == 50 and sn["stkHead"] == 2 and near(sn["ttkHead"], sn["refire"], 1e-6), "sniper: 50 on the head, 2 shots, one refire")
check(R["rocket:primary:direct"]["body"] == 50 and R["rocket:primary:splash"]["headshots"] is False, "rocket: a direct hit 50, splash no headshots")
check(R["locator:primary"].get("support") and R["locator:primary"]["stkBody"] is None, "locator: support, no shots to kill")
check(isinstance(s.get("gameRows"), list) and s["gameRows"], "gameRows (computeWeaponStats)")
s2, rc, _ = run("wpnstats", {"weapons": SET_HOT, "compare": [""], "health": 200})
check(s2["rows"][0]["body"] == 9.9 and s2["compare"][0]["rows"][0]["body"] == RIFLE, "a set and the compare set")
check(s2["compare"][0]["rows"][8]["stkHead"] == 4, "health 200: the sniper needs 4 head shots")
s3, rc, _ = run("wpnstats", {"weapons": SET_FALL, "dist": 25})
f = {r["id"]: r for r in s3["rows"]}
check(near(f["rifle:primary"]["fall20"], 1 - 0.5 * 10 / 30, 1e-4) and near(f["sniper:primary"]["fall20"], 0.6, 1e-4), "falloff fields and a curve")
s4, rc, _ = run("wpnstats", {"weapons": SET_BAD})
check(rc == 1 and "line 3" in s4.get("error", ""), "a bad set: an error with its line")
if shutil.which("node") and EDITOR:
    js = os.path.join(EDITOR, "web", "js")
    if os.path.isfile(os.path.join(js, "stats.js")):
        script = r"""
const [js, text, dist, rowsJson, valuesJson] = process.argv.slice(1);
const { computeStats, NUM_COLS } = await import("file://" + js + "/stats.js");
const { parse } = await import("file://" + js + "/dsl.js");
const values = new Map(Object.entries(JSON.parse(valuesJson)));
const mine = computeStats(values, { health: 100, dist: +dist, set: text ? parse(text) : null });
const by = new Map(JSON.parse(rowsJson).map((r) => [r.id, r]));
let bad = [];
for (const r of mine.rows) {
  const g = by.get(r.id);
  if (!g) { bad.push("missing " + r.id); continue; }
  for (const k of [...NUM_COLS, "at10", "at20", "at40"]) {
    const a = Number.isFinite(r[k]) ? r[k] : null, b = g[k];
    if (a === null && b === null) continue;
    if (a === null || b === null || Math.abs(a - b) > 1e-3 * Math.max(1, Math.abs(a))) bad.push(r.id + " " + k + " " + a + " vs " + b);
  }
  if (JSON.stringify(r.notes) !== JSON.stringify(g.notes)) bad.push(r.id + " notes");
}
console.log(JSON.stringify(bad));
"""
        for text, dist in (("", 0), (SET_FALL, 25), (SET_HOT, 10)):
            st, _, _ = run("wpnstats", {"weapons": text, "dist": dist}) if text else run("wpnstats", {"dist": dist})
            pv, _, _ = run("wpnparams", {"weapons": text} if text else {})
            r = subprocess.run(["node", "--input-type=module", "-e", script, js, text, str(dist), json.dumps(st["rows"]), json.dumps(pv["values"])],
                               capture_output=True, text=True, timeout=60)
            try:
                bad = json.loads(r.stdout.strip().splitlines()[-1])
            except (ValueError, IndexError):
                bad = ["node: " + (r.stderr or r.stdout)[-200:]]
            check(bad == [], "== the page's stats.js (%s, %g m)%s" % (text.split('"')[1] if text else "standard", dist, "" if not bad else ": " + "; ".join(bad[:4])))
else:
    print("  (node not found: the stats.js comparison skipped)")

# ---------------------------------------------------------------------------------------------------------------
print("wpnrange")
TOP = {"ok", "weapon", "alt", "seconds", "targets", "shots", "frames", "totals", "notes", "weaponSet", "map", "tickRate"}
a, rc, wall = run("wpnrange", {"weapon": "rifle", "seconds": 10, "seed": 1})
check(rc == 0 and TOP <= set(a), "answer keys %s" % (TOP - set(a)))
check(len(a["targets"]) == 5 and [t["dist"] for t in a["targets"]] == [5.83, 10.44, 20.1, 40.45, 80], "the built-in range's five lanes")
check(all({"index", "pos", "hits", "headHits", "damage", "kills", "firstKill", "shots"} <= set(t) for t in a["targets"]), "target keys")
check(a["totals"]["kills"] >= 3 and a["totals"]["accuracy"] == 1 and a["totals"]["headHits"] == 0, "perfect body aim: kills, every shot hits, no heads")
rifle_ttk = R["rifle:primary"]["ttkBody"]
check(near(a["targets"][0]["firstKill"], rifle_ttk, 0.01), "first kill at the rifle's time to kill (%.3f vs %.3f s)" % (a["targets"][0]["firstKill"] or -1, rifle_ttk))
check(len(a["frames"]) == 501 and near(a["frames"][1]["t"] - a["frames"][0]["t"], 0.02, 1e-6), "a frame every 20 ms")
fr = a["frames"][100]
check(fr["players"][0].get("shooter") and len(fr["players"]) == 6 and all({"pos", "yaw", "pitch", "health", "alive"} <= set(x) for x in fr["players"]), "frame players")
check(any(f["shots"] for f in a["frames"]) and all({"from", "to", "hit"} <= set(s) for f in a["frames"] for s in f["shots"]), "frame shot traces")
check(all({"t", "hit", "head", "dmg", "target", "dist"} <= set(x) for x in a["shots"]) and a["shots"][0]["dmg"] == RIFLE, "shots: per hit damage")
b, _, _ = run("wpnrange", {"weapon": "rifle", "seconds": 10, "seed": 1})
check(strip(a) == strip(b), "deterministic (the same request, the same answer)")
print("    10 s of rifle at 1000 Hz: %.3f s wall (%.0fx real time)" % (a["perf"]["wallSeconds"], a["perf"]["speedup"]))
hd, _, _ = run("wpnrange", {"weapon": "sniper", "seconds": 4, "shooter": {"aim": "head"}})
check(hd["totals"]["headHits"] == hd["totals"]["hits"] > 0 and near(hd["targets"][0]["firstKill"], R["sniper:primary"]["ttkHead"], 0.01), "head aim: headshots, the sniper's head time to kill")
# shots and hits in one unit: the laser's beam ticks, the knife's swings (not the game's per-tick count of its cut)
lz, _, _ = run("wpnrange", {"weapon": "laser", "seconds": 4, "frames": False, "targets": [{"pos": [0, 0, -10]}]})
check(lz["totals"]["hits"] == lz["totals"]["shotsHit"] and lz["totals"]["accuracy"] > 0.95, "laser: hits counted per beam tick like shots (accuracy %.3f)" % (lz["totals"]["accuracy"] or 0))
kn, _, _ = run("wpnrange", {"weapon": "knife", "seconds": 6, "frames": False, "targets": [{"pos": [0, 0, -2]}]})
swings = 6 / R["knife:primary"]["refire"]
check(0 < kn["totals"]["shots"] <= swings + 1 and kn["totals"]["accuracy"] == 1, "knife: one shot per swing (%d in 6 s), every swing hits" % kn["totals"]["shots"])
kt, _, _ = run("wpnrange", {"weapon": "knife", "alt": True, "seconds": 6, "frames": False, "targets": [{"pos": [0, 0, -2]}]})
check(kt["totals"]["shots"] > 0 and kt["totals"]["shotsHit"] == kt["targets"][0]["hits"] > 0 and kt["targets"][0]["shots"] == kt["totals"]["shots"], "knife throw: shots counted (%d), every throw hits" % kt["totals"]["shots"])
bs, _, _ = run("wpnrange", {"weapon": "bubble", "alt": True, "seconds": 4, "frames": False, "targets": [{"pos": [0, 0, -4]}]})
check(bs["totals"]["shots"] >= bs["totals"]["shotsHit"] > 0 and 0 < bs["totals"]["accuracy"] <= 1, "bubble stream: one shot per bubble (%d shots, %d hit)" % (bs["totals"]["shots"], bs["totals"]["shotsHit"]))
lc, rc_lc, _ = run("wpnrange", {"weapon": "locator", "seconds": 1, "frames": False})
check(rc_lc == 0 and "error" not in lc and lc["totals"]["shots"] == 0 and any("no damage" in n for n in lc["notes"]), "the locator: no damage, like the syringe (not refused)")
mv, _, _ = run("wpnrange", {"weapon": "crossbow", "seconds": 6, "targets": [{"pos": [0, 0, -25], "moving": {"axis": "x", "speed": 6, "amplitude": 3}}]})
check(mv["totals"]["accuracy"] == 1 and mv["totals"]["kills"] >= 1, "a moving target: perfect lead hits every bolt")
rk, _, _ = run("wpnrange", {"weapon": "rocket", "seconds": 4, "targets": [{"pos": [0, 0, -15], "health": 200}]})
check(rk["targets"][0]["health"] == 200 and rk["targets"][0]["hits"] >= 4 and rk["targets"][0]["kills"] >= 1, "a projectile, a target's own health")
bt, _, _ = run("wpnrange", {"weapon": "rifle", "seconds": 8, "shooter": {"aim": "bot", "skill": "EXPERT"}, "targets": [{"pos": [0, 0, -12]}]})
check(bt["totals"]["shots"] > 0 and bt["totals"]["hits"] > 0 and bt["skill"] == "EXPERT", "a bot shooter fires and hits (accuracy %.2f)" % (bt["totals"]["accuracy"] or 0))
hot, _, _ = run("wpnrange", {"weapon": "rifle", "seconds": 4, "targets": [{"pos": [0, 0, -10]}], "weapons": SET_HOT, "compare": [SET_LOGIC]})
check(hot["weaponSet"] == "hot" and hot["shots"][0]["dmg"] == 9.9, "a set: its damage")
lg = hot.get("compare", [{}])[0]
check(lg.get("weaponSet") == "logic" and lg.get("weaponLogic") and near(lg["shots"][0]["dmg"], 2 * RIFLE, 1e-6), "compare: the other set ran too (its hit slot doubles the rifle's damage)")
check(near(hot["targets"][0]["firstKill"], 10 * R["rifle:primary"]["refire"], 0.01), "11 shots of 9.9: the first kill a set's way")
qm, _, _ = run("wpnrange", {"weapon": "rifle", "seconds": 7, "targets": [{"pos": [0, 0, -10]}], "qmode": "custommode=tanky\nhealth=200\n"})
check(qm["qmode"] == "tanky" and qm["targets"][0]["health"] == 200 and near(qm["targets"][0]["firstKill"], (math.ceil(200 / RIFLE - 1e-9) - 1) * R["rifle:primary"]["refire"], 0.01), "a qmode: its health (%d rifle shots)" % math.ceil(200 / RIFLE - 1e-9))
e, rc, _ = run("wpnrange", {"weapon": "rifle", "weaponsAllowed": ["rocket"]})
check(rc == 1 and "allow" in e.get("error", ""), "a weapon the pool doesn't allow: an error")
e, rc, _ = run("wpnrange", {"weapon": "rifle", "weapons": SET_BAD})
check(rc == 1 and "line 3" in e.get("error", ""), "a bad set: an error with its line")
mp, rc, _ = run("wpnrange", {"map": "temple", "weapon": "rifle", "seconds": 4, "shooter": {"pos": [-3.45, 0, 26.45]}, "targets": [{"pos": [-3.45, 0, 16.45]}]})
check(rc == 0 and mp["map"] == "temple" and mp["targets"][0]["kills"] >= 1, "a map by name")
rm, rc, _ = run("wpnrangemap", {})
check(rm.get("text", "").startswith("#") and len(rm.get("lanes", [])) == 5, "wpnrangemap")
sup, rc, _ = run("wpnrange", {"weapon": "syringe", "seconds": 1})
check(rc == 0 and sup["totals"]["damage"] == 0 and sup["notes"], "a support weapon: no damage, a note")

# ---------------------------------------------------------------------------------------------------------------
print("sim / simbatch with a weapon set")
SET_ROCKETS = 'version 1\nweapons "big rockets"\nparam rocket.damage f32 = 200\n'
L, rc = run_lines("sim", {"map": "harbor", "mode": "ffa", "bots": 4, "seconds": 30, "until": "seconds", "realtime": False,
                          "weapons": SET_ROCKETS, "weaponsAllowed": ["rocket"]})
st, end = L[0], L[-1]
check(rc == 0 and st.get("weaponSet") == "big rockets" and st.get("weaponsAllowed") == ["rocket"], "sim: the start line echoes the set and the pool")
ws = end.get("weapons", [])
check(isinstance(ws, list) and all(w["weapon"] == "rocket" for w in ws if w["kills"] or w["shots"]), "sim: only rockets fired (%s)" % [w["weapon"] for w in ws])
L, rc = run_lines("sim", {"map": "harbor", "mode": "ffa", "bots": 2, "seconds": 2, "realtime": False})
check(L[0].get("weaponSet", "missing") is None, "sim without a set: weaponSet null")
t0 = time.time()
L, rc = run_lines("simbatch", {"map": "harbor", "mode": "ffa", "bots": 6, "seconds": 60, "until": "seconds", "matches": 8, "weapons": SET_ROCKETS})
end = L[-1]
ws = {w["weapon"]: w for w in end.get("weapons", [])}
check(rc == 0 and L[0].get("weaponSet") == "big rockets" and ws, "simbatch: the set, per weapon numbers")
check(all({"weapon", "kills", "damage", "shots", "hits", "accuracy", "ttk"} <= set(w) for w in ws.values()), "simbatch: weapon keys")
kills_b, kills_w = sum(b["kills"] for b in end["bots"]), sum(w["kills"] for w in ws.values())
check(kills_b >= kills_w >= kills_b * 0.9, "simbatch: weapon kills add up to the bots' (%d of %d)" % (kills_w, kills_b))
print("    simbatch 8 x 60 s harbor, 6 bots: %.2f s wall" % (time.time() - t0))
L, rc = run_lines("simbatch", {"map": "temple", "bots": 2, "seconds": 5, "matches": 2, "qmode": "custommode=q\nmode=ffa\nhealth=150\n"})
check(rc == 0 and L[0].get("qmode") == "q" and L[0].get("mode") == "ffa", "simbatch: a qmode's mode")
L, rc = run_lines("sim", {"map": "temple", "bots": 2, "seconds": 1, "weapons": SET_BAD})
check(rc == 1 and "line 3" in L[-1].get("error", ""), "sim: a bad set fails the request")

print()
if FAILS:
    print("FAILED (%d):" % len(FAILS))
    for f in FAILS:
        print("  " + f)
    sys.exit(1)
print("PASS")
