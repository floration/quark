#!/usr/bin/env python3
"""The projectiles as low-poly outlined models, in the style of the shipped weapons (owner, 2026-11-10: "now that the
weapons have been remodeled, lets do the projectiles, keep the style of the weapons").

Mesh overrides built from lowpoly.py like gen_weapons_lowpoly.py: angled and curved low-poly solids, flat-shaded, an
inverted-hull outline on every solid, colours that follow the team and the map's glow. One module per projectile in
tools/models/projectiles/ (NAME and build() -> gen_weapons_lowpoly.Model). Projectile space (docs/ASSETS.md): origin at
the projectile, flying toward -Z, millimetres here, metres in the file. The game keeps each one's life on the mesh:
it spins the sticky (about Y) and the thrown knife (end over end about X), blinks the sticky's team colour, wobbles
the bubble and draws the rocket's flickering flame behind z +270 (rocketFlameParts, models.cpp).

  python3 tools/models/gen_projectiles_lowpoly.py [--out DIR] [--only NAME,...]

Writes <out>/models/projectile/<NAME>.gltf (default out: assets/overrides). Deterministic.
"""
import argparse
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lowpoly  # noqa: E402
from lowpoly import write_gltf  # noqa: E402

# Projectiles are seen metres away, not at arm's length like a viewmodel: a thicker hull keeps the outline visible.
PROJ_OUTLINE = 5.0
# A projectile can be on screen many times at once (a sticky spam, a rocket volley): keep each one cheap.
MAX_TRIS = 700

# extra materials (team_glass: the bubble's see-through shell; white: highlights, drawn as authored)
lowpoly.TONES.setdefault("team_glass", ("team", 1.0))
lowpoly.TONES.setdefault("team_hot", ("team", 1.4))
lowpoly.TONES.setdefault("white", (1.0, 1.0, 1.0))
lowpoly.ALPHA.setdefault("team_glass", 0.35)

PROJECTILES = ["ROCKET", "STICKY", "KNIFE", "SYRINGE", "CROSSBOW", "BUBBLE", "ORB"]


def bounds(m):
    pts = [p for mesh in [m.body] + [x[1] for x in m.parts] for tris in mesh.tris.values() for t in tris for p in t]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "assets", "overrides"))
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = [s.strip().upper() for s in a.only.split(",") if s.strip()]
    ok = True
    for name in PROJECTILES:
        if only and name not in only:
            continue
        lowpoly.OUTLINE = PROJ_OUTLINE
        mod = importlib.import_module("projectiles." + name.lower())
        m = mod.build()
        lo, hi = bounds(m)
        tris = write_gltf(os.path.join(a.out, "models", "projectile", name + ".gltf"), name, m.nodes(),
                          {"style": "low-poly outlined", "model": name, "description": m.desc})
        note = ""
        if tris > MAX_TRIS:
            note = "  ERROR: over %d triangles" % MAX_TRIS
            ok = False
        print("%-9s %5d triangles, x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f%s" % (name, tris, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], note))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
