#!/usr/bin/env python3
"""Bundle changelogs/*.md into site/changelog.json (newest first) so the site works without the GitHub API,
and the weapon table from src/game/gamedefs.h into site/weapons.json (so the site always shows current stats)."""
import ast, json, math, operator, os, re, sys

# gamedefs.h fields are numbers or simple arithmetic (0.75*2.0/9.9): evaluated by walking the expression tree, never
# eval() (this runs in the job that deploys the site; REVIEW SUP-4)
_OPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul, ast.Div: operator.truediv}
def arith(text):
    def ev(n):
        if isinstance(n, ast.Constant) and type(n.value) in (int, float): return n.value
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.USub, ast.UAdd)):
            v = ev(n.operand); return -v if isinstance(n.op, ast.USub) else v
        if isinstance(n, ast.BinOp) and type(n.op) in _OPS: return _OPS[type(n.op)](ev(n.left), ev(n.right))
        raise ValueError("not a number in gamedefs.h: %r" % text)
    return ev(ast.parse(text, mode="eval").body)
root = sys.argv[1] if len(sys.argv) > 1 else "."
entries = []
for name in os.listdir(os.path.join(root, "changelogs")):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\.md", name)   # (x.y.z, or x.y.z.n: a hotfix)
    if m:
        entries.append((tuple(int(g) for g in m.groups() if g is not None), open(os.path.join(root, "changelogs", name), encoding="utf-8").read()))
entries.sort(reverse=True)
json.dump([{"version": ".".join(map(str, v)), "notes": t} for v, t in entries],
          open(os.path.join(root, "site", "changelog.json"), "w", encoding="utf-8"), indent=1)

# weapons: {"NAME", refire, damage, range, projectile, speed, splash, spread, scoped, zoom, beam, pellets, ...}
src = open(os.path.join(root, "src", "game", "gamedefs.h"), encoding="utf-8").read()
weapons = []
for m in re.finditer(r'^\s*\{"([A-Z]+)",([^}]*)\}', src, re.M):
    f = [re.sub(r"(?<=[0-9.])f\b", "", x.strip()) for x in m.group(2).split(",")]   # (C float suffixes, anywhere: 0.75f*2.0f/9.9f)
    if len(f) < 11 or not re.match(r"^-?[0-9.]", f[0]): continue   # other {"NAME", ...} tables (duel rulesets)
    if m.group(1) in ("SYRINGE", "LOCATOR"): continue                # (it heals / does no damage: no time to kill)
    num = lambda x: float(arith(x)) if x not in ("true", "false") else x == "true"
    refire, dmg, splash, beam, pellets = num(f[0]), num(f[1]), num(f[5]), num(f[9]), int(num(f[10]))
    per = dmg * pellets
    shots = int(math.ceil(100 / per - 1e-6))
    weapons.append({"name": m.group(1), "damage": dmg, "pellets": pellets, "refire": refire,
                    "splash": splash > 0, "beam": beam, "projectile": num(f[3]),
                    "shots": shots, "ttk": round((shots - 1) * refire, 3), "scope": num(f[7]) >= 0})
json.dump(weapons, open(os.path.join(root, "site", "weapons.json"), "w", encoding="utf-8"), indent=1)
