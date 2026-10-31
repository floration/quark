"""QK7 SPIRAL: ranked 1v1 (vanilla), box and accent only, point-symmetric. 84 x 84 m. Two interleaved spiral walkways
(a double helix, each 1.5 turns, 5 m a side) climb round a central drum 24 m across from the ground to 30 m: each
player has their own climb, and the two pass 10 m apart on every side. The drum is hollow, with windows, floors at 10
and 20 m (doors off the helices) and its top floor at 30 m (the power spot, open in the middle: grapple across it),
reached where each helix ends. The helices are ramps (12 degrees: bhop up them), the first one solid to the ground (no
pocket under it), and every landing has a 45 degree wall across its outer corner to carve or dash-bounce the turn on.
The court round them is broken into thirds on each side by 2.6 m walls from the outer wall, with a tower in every
corner. Team spawns in the corners at the foot of each helix (about 100 m apart); the free-for-all ones on the court and
one on a helix landing (15 m). The drum's windows are slits (1 m: lines, not doors)."""
from .prefabs import M, minus
from .m2kit import B, light, mirror_all, ramp, wedge, prism

D, RW, TOP = 12.0, 8.0, 30.0      # drum half size, ring width, top floor


def build():
    m = M("QK7 Spiral", "1v1", sun=(0, 1, 0))
    m.raw("ranked vanilla vampire instagib")   # (q, 2026-10-31: every qk map on all three ladders)
    m.also = ["duel", "vs", "tourney"]
    start = len(m.lines)
    O = D + RW
    # ---- the ground (its own twin), the outer walls (W and N; their mirror E and S)
    B(m, "box", -42, -1, -42, 42, 0, 42)
    B(m, "box", -43, 0, -43, -42, 10, 43); B(m, "box", -42, 0, -43, 43, 10, -42)
    # ---- helix A (helix B is its mirror): flights along the drum's sides, landings at the corners
    flights = [((-D, D, D, O), 0, "x", 1), ((D, -D, O, D), 5, "z", -1), ((-D, -O, D, -D), 10, "x", -1),
               ((-O, -D, -D, D), 15, "z", 1), ((-D, D, D, O), 20, "x", 1), ((D, -D, O, D), 25, "z", -1)]
    for k, ((x0, z0, x1, z1), base, ax, ris) in enumerate(flights):
        ya, yb = (base, base + 5) if ris > 0 else (base + 5, base)
        if k == 0: wedge(m, "box", ("+" if ris > 0 else "-") + ax, x0, 0, z0, x1, base + 5, z1)   # (solid to the ground)
        else: ramp(m, "box", x0, z0, x1, z1, ax, ya, yb)
    for ((x0, z0, x1, z1), y) in (((D, D, O, O), 5), ((D, -O, O, -D), 10), ((-O, -O, -D, -D), 15), ((-O, D, -D, O), 20), ((D, D, O, O), 25), ((D, -O, O, -D), 30)):
        B(m, "accent", x0, y - 1, z0, x1, y, z1)
        # a 45 degree wall across the outer corner (carve the turn, or dash-bounce off it)
        cx, cz = (x0 if x0 < 0 else x1), (z0 if z0 < 0 else z1)
        sx, sz = (1 if cx < 0 else -1), (1 if cz < 0 else -1)
        if y < TOP: prism(m, "accent", [(cx, cz), (cx + sx * 4, cz), (cx, cz + sz * 4)], y, y + 3)
    # ---- the drum: walls (doors off the helices at 10 and 20, ground doors, windows), floors at 10 and 20, the top at 30
    from .m2kit import wall
    wall(m, "box", -D, -D, D, -D + 1, 0, TOP, [(7, 11, 10, 14), (7, 11, 20, 24), (-6, -2, 5, 6), (2, 6, 15, 16), (-6, -2, 25, 26)])   # N
    wall(m, "box", D - 1, -D + 1, D, D - 1, 0, TOP, [(-3, 3, 0, 4), (5, 9, 15, 16), (-9, -5, 25, 26), (5, 9, 5, 6)])                     # E
    for y in (10, 20):
        for r in minus((-D + 1, -D + 1, D - 1, D - 1), [(-4, -4, 4, 4)]): B(m, "accent", r[0], y - 0.6, r[1], r[2], y, r[3])
    for r in minus((-D, -D, D, D), [(-5, -5, 5, 5)]): B(m, "box", r[0], TOP - 1, r[1], r[2], TOP, r[3])
    B(m, "accent", -5, TOP, -5.3, 5, TOP + 1, -5)                                   # (a low wall on the top floor's lip)
    B(m, "box", -3, 0, -8, 3, 1.4, -6)                                              # cover on the drum's ground floor
    # ---- the outer band: a tower in each corner (grapple anchors, perches), walls from the outer wall splitting each
    # side into thirds (2.6 m, 12 m long: a 10 m lane stays open beside the helix), cover blocks a player fits behind
    B(m, "accent", -38, 0, -38, -33, 7, -33); B(m, "accent", 33, 0, -38, 38, 7, -33)
    for x in (-14, 14): B(m, "box", x - 0.6, 0, -42, x + 0.6, 2.6, -30)
    for z in (-14, 14): B(m, "box", -42, 0, z - 0.6, -30, 2.6, z + 0.6)
    for (x0, z0, x1, z1, h) in ((-36, -6, -33, 2, 2.4), (24, -34, 30, -31, 2.2), (-6, -32, -2, -28, 2.6)):
        B(m, "box", x0, 0, z0, x1, h, z1)
    # ---- spawns: the foot of helix A (team a; b at the foot of helix B), free-for-all pairs
    for (x, z) in ((-39, 27), (-39, 20), (-29, 39), (-21, 38)): m.spawn("a", x, 0, z, -45)
    for (x, y, z, yaw) in ((-26, 0, -26, 180), (36, 0, 8, 90), (-14, 15, -14, 180), (24, 0, -38, 0)):
        m.spawn("any", x, y, z, yaw)
    # ---- lights
    for (x, y, z, r) in ((0, 6, 0, 12), (0, 16, 0, 12), (0, 26, 0, 12), (16, 9, 16, 12), (16, 19, -16, 12), (-16, 29, -16, 12), (-30, 7, 30, 16), (30, 7, 30, 16)):
        light(m, x, y, z, r, 0.8)
    mirror_all(m, start)
    # ---- bounds: clip over the outer walls and a lid
    B(m, "clip", -44, 10, -44, 44, 40, -42); B(m, "clip", -44, 10, 42, 44, 40, 44)
    B(m, "clip", -44, 10, -42, -42, 40, 42); B(m, "clip", 42, 10, -42, 44, 40, 42)
    B(m, "clip", -44, 40, -44, 44, 41, 44)
    m.raw("palette base 0.78 0.76 0.7 accent 0.2 0.5 0.42 metal 0.3 0.3 0.3 glow 0.5 1 0.8 sky 0.55 0.66 0.78")
    m.raw("setting a spiral tower court")
    m.save("qk7", dress=False)
