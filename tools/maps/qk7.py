"""QK7 SPIRAL: ranked 1v1 (vanilla), box and accent only, point-symmetric. 84 x 84 m. Two interleaved spiral walkways
(a double helix, each 1.5 turns, 5 m a side) climb round a central drum 24 m across from the ground to 30 m: each
player has their own climb, and the two pass 10 m apart on every side. The drum is hollow, with windows, floors at 10
and 20 m (doors off the helices) and its top floor at 30 m (the power spot, open in the middle: grapple across it),
reached where each helix ends. Spawns at the foot of each helix; the free-for-all ones on the landings and in the drum.
(Square turns, so the climbs are flights of steps the walk checks and the bots can use.)"""
from .prefabs import M, minus
from .m2kit import B, light, mirror_all

D, RW, TOP = 12.0, 8.0, 30.0      # drum half size, ring width, top floor


def build():
    m = M("QK7 Spiral", "1v1", sun=(0, 1, 0))
    m.raw("ranked vanilla")
    m.also = ["duel", "vs", "tourney"]
    start = len(m.lines)
    O = D + RW
    # ---- the ground (its own twin), the outer walls (W and N; their mirror E and S)
    B(m, "box", -42, -1, -42, 42, 0, 42)
    B(m, "box", -43, 0, -43, -42, 10, 43); B(m, "box", -42, 0, -43, 43, 10, -42)
    # ---- helix A (helix B is its mirror): flights along the drum's sides, landings at the corners
    flights = [((-D, D, D, O), 0, "x", 1), ((D, -D, O, D), 5, "z", -1), ((-D, -O, D, -D), 10, "x", -1),
               ((-O, -D, -D, D), 15, "z", 1), ((-D, D, D, O), 20, "x", 1), ((D, -D, O, D), 25, "z", -1)]
    for ((x0, z0, x1, z1), base, ax, ris) in flights:
        m.stairs(x0, z0, x1, z1, base + 5, ax, ris, base=base, solid=False)
    for ((x0, z0, x1, z1), y) in (((D, D, O, O), 5), ((D, -O, O, -D), 10), ((-O, -O, -D, -D), 15), ((-O, D, -D, O), 20), ((D, D, O, O), 25), ((D, -O, O, -D), 30)):
        B(m, "accent", x0, y - 1, z0, x1, y, z1)
    # ---- the drum: walls (doors off the helices at 10 and 20, ground doors, windows), floors at 10 and 20, the top at 30
    from .m2kit import wall
    wall(m, "box", -D, -D, D, -D + 1, 0, TOP, [(7, 11, 10, 14), (7, 11, 20, 24), (-6, -2, 4, 6.5), (2, 6, 14, 16.5), (-6, -2, 24, 26.5)])   # N
    wall(m, "box", D - 1, -D + 1, D, D - 1, 0, TOP, [(-3, 3, 0, 4), (5, 9, 13, 16), (-9, -5, 23, 26), (5, 9, 4, 7)])                           # E
    for y in (10, 20):
        for r in minus((-D + 1, -D + 1, D - 1, D - 1), [(-4, -4, 4, 4)]): B(m, "accent", r[0], y - 0.6, r[1], r[2], y, r[3])
    for r in minus((-D, -D, D, D), [(-5, -5, 5, 5)]): B(m, "box", r[0], TOP - 1, r[1], r[2], TOP, r[3])
    B(m, "accent", -5, TOP, -5.3, 5, TOP + 1, -5)                                   # (a low wall on the top floor's lip)
    B(m, "box", -3, 0, -8, 3, 1.4, -6)                                              # cover on the drum's ground floor
    # ---- the outer band: cover blocks and corner towers (grapple anchors, perches)
    B(m, "accent", -38, 0, -38, -33, 7, -33)
    for (x0, z0, x1, z1, h) in ((-32, 24, -28, 27, 1.4), (-36, -6, -33, 2, 2.2), (24, -34, 30, -31, 1.2), (-10, -32, -6, -28, 2.6)):
        B(m, "box", x0, 0, z0, x1, h, z1)
    # ---- spawns: the foot of helix A (team a; b at the foot of helix B), free-for-all pairs
    for (x, z) in ((-24, 30), (-17, 33), (-31, 36), (-10, 37)): m.spawn("a", x, 0, z, -90)
    for (x, y, z, yaw) in ((16, 5, 16, 0), (-16, 15, -16, 180), (0, 10, 7, 90), (30, 0, -20, 90)):
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
