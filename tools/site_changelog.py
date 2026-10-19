#!/usr/bin/env python3
"""Bundle changelogs/*.md into site/changelog.json (newest first) so the site works without the GitHub API,
and the weapon table from src/game/gamedefs.h into site/weapons.json (so the site always shows current stats)."""
import json, os, re, sys
root = sys.argv[1] if len(sys.argv) > 1 else "."
entries = []
for name in os.listdir(os.path.join(root, "changelogs")):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\.md", name)
    if m:
        entries.append((tuple(map(int, m.groups())), open(os.path.join(root, "changelogs", name), encoding="utf-8").read()))
entries.sort(reverse=True)
json.dump([{"version": ".".join(map(str, v)), "notes": t} for v, t in entries],
          open(os.path.join(root, "site", "changelog.json"), "w", encoding="utf-8"), indent=1)

# weapons: {"NAME", refire, damage, range, projectile, speed, splash, spread, scoped, zoom, beam, pellets, ...}
src = open(os.path.join(root, "src", "game", "gamedefs.h"), encoding="utf-8").read()
weapons = []
for m in re.finditer(r'^\s*\{"([A-Z]+)",([^}]*)\}', src, re.M):
    f = [re.sub(r"(?<=[0-9.])f$", "", x.strip()).replace("f/", "/") for x in m.group(2).split(",")]
    if len(f) < 11 or not re.match(r"^-?[0-9.]", f[0]): continue   # other {"NAME", ...} tables (duel rulesets)
    num = lambda x: float(eval(x)) if x not in ("true", "false") else x == "true"
    refire, dmg, splash, beam, pellets = num(f[0]), num(f[1]), num(f[5]), num(f[9]), int(num(f[10]))
    per = dmg * pellets
    shots = int(__import__("math").ceil(100 / per - 1e-6))
    weapons.append({"name": m.group(1), "damage": dmg, "pellets": pellets, "refire": refire,
                    "splash": splash > 0, "beam": beam, "projectile": num(f[3]),
                    "shots": shots, "ttk": round((shots - 1) * refire, 3), "scope": num(f[7]) >= 0})
json.dump(weapons, open(os.path.join(root, "site", "weapons.json"), "w", encoding="utf-8"), indent=1)
