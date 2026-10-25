#!/usr/bin/env python3
"""Checks the other-games reference files (python3 stdlib only).

    python3 tools/physics_editor/references/check.py [quark_maptool | physparams.json]

Every file listed in index.json (and every .json here is listed): valid JSON, the fields the editor reads, every
value key a real Quark setting (from `quark_maptool physparams`), mult = value / Quark's base within rounding, the
unit Quark's, every value and fact with a source and a confidence (exact | measured | estimated), and outsideClamp
set exactly when mult is outside the editor's range. Exit status 1 on any error."""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
CONF = {"exact", "measured", "estimated"}
TOGGLES = {"dash", "grapple"}


def physparams(arg):
    if arg and arg.endswith(".json"):
        return json.load(open(arg))
    exe = arg or next((p for p in (os.path.join(ROOT, "build", "quark_maptool"), os.path.join(ROOT, "build", "quark_maptool.exe"))
                       if os.path.isfile(p)), None)
    if not exe:
        sys.exit("check.py: no quark_maptool (build it: cmake --build build --target quark_maptool), or pass a physparams .json")
    return json.loads(subprocess.run([exe, "physparams"], input="{}", capture_output=True, text=True, check=True).stdout)


def main():
    pp = physparams(sys.argv[1] if len(sys.argv) > 1 else None)
    params = {p["key"]: p for p in pp["params"] + pp["top"]}
    errs, warns = [], []
    err = lambda f, m: errs.append(f"{f}: {m}")

    idx = json.load(open(os.path.join(HERE, "index.json")))
    listed = []
    for gr in idx.get("groups", []):
        if not gr.get("name"): err("index.json", "a group without a name")
        for e in gr.get("games", []):
            if not isinstance(e, dict) or not e.get("id"): err("index.json", f"bad entry {e!r}"); continue
            listed.append((e["id"], e.get("file", e["id"] + ".json"), e))
    ids = [i for i, _, _ in listed]
    for i in sorted({i for i in ids if ids.count(i) > 1}): err("index.json", f"{i} listed twice")
    files = {f for f in os.listdir(HERE) if f.endswith(".json") and f != "index.json"}
    for f in sorted(files - {f for _, f, _ in listed}): err(f, "not listed in index.json")

    nvals = 0
    for gid, fn, ent in listed:
        path = os.path.join(HERE, fn)
        if not os.path.isfile(path): err(fn, "missing"); continue
        try:
            g = json.load(open(path, encoding="utf-8"))
        except ValueError as ex:
            err(fn, f"invalid JSON: {ex}"); continue
        if g.get("id") != gid: err(fn, f"id {g.get('id')!r} != index id {gid!r}")
        for k in ("name", "year", "engine", "notes"):
            if k not in g: err(fn, f"no {k}")
        sc = g.get("scale") or {}
        if not (sc.get("unit") is not None and isinstance(sc.get("metresPerUnit"), (int, float)) and sc["metresPerUnit"] > 0 and sc.get("why")):
            err(fn, "scale needs unit, metresPerUnit > 0 and why")
        if not g.get("sources"): err(fn, "no sources")
        for s in g.get("sources", []):
            if not (isinstance(s, dict) and s.get("url")) and not (isinstance(s, str) and s): err(fn, f"bad source {s!r}")
        if not isinstance(g.get("extras", []), list): err(fn, "extras must be a list")
        vals = g.get("values")
        if not isinstance(vals, dict) or not vals: err(fn, "no values"); continue
        for k, v in vals.items():
            where = f"{fn} values.{k}"
            if not isinstance(v, dict): err(where, "not an object"); continue
            if not v.get("source"): err(where, "no source")
            if v.get("confidence") not in CONF: err(where, f"confidence {v.get('confidence')!r} not one of {sorted(CONF)}")
            if k in TOGGLES:
                if not isinstance(v.get("value"), bool): err(where, "a toggle's value must be true / false")
                continue
            if k not in params: err(where, "not a Quark setting (physparams)"); continue
            p = params[k]
            val, mult = v.get("value"), v.get("mult")
            if not isinstance(val, (int, float)) or not isinstance(mult, (int, float)): err(where, "value and mult must be numbers"); continue
            exp = val / p["base"]
            if abs(mult - exp) > 5e-4 + 1e-3 * abs(exp): err(where, f"mult {mult} != value / base = {exp:.5f}")
            if v.get("unit") != p["unit"]: err(where, f"unit {v.get('unit')!r} != Quark's {p['unit']!r}")
            out = not (p["min"] <= mult <= p["max"])
            if bool(v.get("outsideClamp")) != out: err(where, f"outsideClamp should be {out} (range {p['min']}..{p['max']})")
            if p.get("inactive"): warns.append(f"{where}: {k} is inactive in Quark")
            nvals += 1
        for k, v in (g.get("facts") or {}).items():
            where = f"{fn} facts.{k}"
            if not isinstance(v, dict) or "value" not in v or "unit" not in v: err(where, "needs value and unit"); continue
            if not v.get("source"): err(where, "no source")
            if v.get("confidence") not in CONF: err(where, f"confidence {v.get('confidence')!r}")
        nv = len([k for k in vals if k not in TOGGLES])
        if ent.get("values") is not None and ent["values"] != nv: err("index.json", f"{gid}: values {ent['values']} but the file maps {nv}")

    for w in warns: print("warning:", w)
    for e in errs: print("error:", e)
    print(f"{len(listed)} games, {nvals} values checked against quark {pp.get('version')}: {'OK' if not errs else str(len(errs)) + ' errors'}")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
