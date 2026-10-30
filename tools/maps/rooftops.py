"""ROOFTOPS (1.19.6): tag (and hide, free for all) over a city block at night, in the rain, 96 x 96 m. Five buildings
of different heights stand round a centre block, joined roof to roof in a ring (plank stairs and ramps over the
streets) with spokes into the centre: the chase runs round the ring, cuts across the middle, or drops off a roof to
the streets and climbs back up a ladder:
  west: the office tower (roof 20 m): a plant room with louvred steel walls (a door on every side), water tanks on
  stilts, AC units;
  east: the hotel (14 m): a neon sign on a frame with a perch on top (22 m, a ladder up the lift motor room), a raised
  pool, a cabana;
  north: the warehouse (10 m): sawtooth skylights of breakable glass over a top-floor loft (open light wells at both
  ends, a ladder up out of each);
  south: the tenement (12 m): a fire escape (three switchback flights) down its north face, chimneys, a shed;
  centre: the billboard block (16 m): the billboard on posts (run under it), its catwalk at 22 m up a ladder.
The streets at 0 are safe ground (a fall costs time, not a life): every building has a ladder up from them, and a
service alley at 4 m (the back sheds' roofs) runs round the whole block behind the buildings, a stair up to it in
every corner yard and a ladder from it up every building's back."""
import math
from .prefabs import M
from .m6kit import Kit, wall_x, wall_z, slab, shell, rails_x, rails_z, yaw_to, minus, tiles, num

H = 48.0                                   # the block: x and z -48..48 (the alley ring 44..48)
LID = 70.0
# the buildings: (x0, z0, x1, z1, roof)
W = (-44, -12, -16, 12, 20.0)
E = (16, -13, 44, 13, 14.0)
N = (-22, -44, 22, -27, 10.0)
S = (-22, 26, 22, 44, 12.0)
C = (-9, -9, 9, 9, 16.0)


def facade(k, b, wall, band, roofmat, base="londonstock"):
    """a solid building: a plinth, storeys of wall with a window band in each, the roof slab (top at the roof)"""
    x0, z0, x1, z1, y = b
    k.box(base, x0, 0, z0, x1, 1.0, z1)
    h = 1.0
    while h < y - 0.3 - 1e-6:
        top = min(h + 4.0, y - 0.3)
        k.box(wall, x0, h, z0, x1, min(h + 2.4, top), z1)
        if h + 2.4 < top: k.box(band, x0, h + 2.4, z0, x1, top, z1)
        h = top
    k.box(roofmat, x0, y - 0.3, z0, x1, y, z1)


def parapet(k, b, gaps, mat="londonstock", h=1.0, t=0.3, cap="leadsheet"):
    """a parapet (h, vaultable) round a roof's edge, inside it, less gaps {edge: [(lo, hi)]} (n s w e)"""
    x0, z0, x1, z1, y = b
    for edge in "nswe":
        if edge in "ns":
            z = z0 if edge == "n" else z1 - t
            runs = minus((x0, 0, x1, 1), [(lo, -1, hi, 2) for (lo, hi) in gaps.get(edge, ())])
            for (a, _, c, _) in runs: k.box(mat, a, y, z, c, y + h, z + t); k.box(cap, a, y + h, z, c, y + h + 0.06, z + t)
        else:
            x = x0 if edge == "w" else x1 - t
            runs = minus((0, z0 + t, 1, z1 - t), [(-1, lo, 2, hi) for (lo, hi) in gaps.get(edge, ())])
            for (_, a, _, c) in runs: k.box(mat, x, y, a, x + t, y + h, c); k.box(cap, x, y + h, a, x + t, y + h + 0.06, c)


def build():
    m = M("Rooftops", "tag", sun=(0.2, 1.0, 0.4))
    m.also = ["hide"]
    k = Kit(m)
    m.raw("palette base 0.55 0.5 0.48 accent 0.85 0.25 0.55 metal 0.4 0.42 0.46 glow 0.45 0.85 1 sky 0.02 0.03 0.08")
    m.raw("setting city rooftops at night, in the rain")

    # ================================================================== streets, the alley ring, the city, bounds
    for (x0, z0, x1, z1) in tiles([(-44, -44, 44, 44)], 22):
        k.box("asphalt", x0, -0.4, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in [(-48, -48, -44, 48), (44, -48, 48, 48), (-44, -48, 44, -44), (-44, 44, 44, 48)]:
        k.box("corrugated", x0, 0, z0, x1, 3.7, z1)                        # the back sheds
        k.box("rainwetpaving", x0, 3.7, z0, x1, 4.0, z1)                   # (their roofs: the alley)
    # a stair up to the alley in every corner yard
    for sx in (-1, 1):
        for sz in (-1, 1):
            k.stairs("steeldeckplate", sx * 36, sz * 33, sx * 44, sz * 36, 4.0, "x", sx, base=0.0)
    # the city round the block: facades behind the alley, far towers with lit windows
    HS = [9, 13, 7, 11, 15, 8, 12, 10]                                     # (low: the skyline shows over them)
    for side in range(4):
        for j in range(4):
            a0, a1 = -54 + 27 * j, -54 + 27 * (j + 1)
            h = HS[(side * 3 + j) % len(HS)]
            box = [(-54, a0, -48, a1), (48, a0, 54, a1), (a0, -54, a1, -48), (a0, 48, a1, 54)][side]
            k.box("graffitibuffedwall", box[0], 0, box[1], box[2], 6, box[3])
            k.box("londonstock" if (side + j) % 2 else "creambrick", box[0], 6, box[1], box[2], h, box[3])
    for i, (x0, z0, x1, z1, h) in enumerate([(-140, -150, -100, -120, 70), (-60, -170, -20, -140, 95), (10, -160, 50, -130, 60),
                                             (90, -150, 130, -110, 85), (120, -60, 150, -20, 55), (125, 20, 160, 60, 75),
                                             (90, 110, 130, 150, 50), (0, 120, 40, 160, 65), (-80, 115, -40, 150, 90),
                                             (-150, 60, -120, 100, 60), (-160, -20, -125, 20, 45), (-150, -100, -115, -70, 80)]):
        k.box("far", x0, -1, z0, x1, h, z1)
        for y in range(8, int(h) - 4, 9):
            k.box("glow", x0 + 3, y, z0 - 0.2, x1 - 3, y + 1.2, z0)
            k.box("glow", x0 + 3, y, z1, x1 - 3, y + 1.2, z1 + 0.2)
    m.clip_walls(-H, -H, H, H, 0, up=LID)
    k.box("clip", -H - 2, LID, -H - 2, H + 2, LID + 1, H + 2)

    # ================================================================== the buildings
    facade(k, W, "concrete", "rainwindow", "concreterooftiles")
    facade(k, E, "creambrick", "rainwindow", "rainwetpaving")
    facade(k, S, "londonstock", "rainwindow", "birdspottedroof")
    facade(k, C, "graffitibuffedwall", "londonstock", "puddledground")
    # the warehouse: solid to 6, the loft (x -16..16, z -43..-31, 6..10) walled round, the walkway along its south
    x0, z0, x1, z1, y = N
    k.box("londonstock", x0, 0, z0, x1, 1.0, z1)
    k.box("corrugated", x0, 1.0, z0, x1, 5.7, z1)
    k.box("concrete", -16, 5.7, -43, 16, 6.0, -31)                       # (the loft's floor)
    for (a, b, c, d) in [(x0, -31, x1, z1), (x0, z0, x1, -43), (x0, -43, -16, -31), (16, -43, x1, -31)]:
        k.box("corrugated", a, 5.7, b, c, y - 0.3, d)
        k.box("standingseamroof", a, y - 0.3, b, c, y, d)
    # the sawtooth: three teeth (4 m, rising north 10 -> 11.2) over x -14..14, north-facing breakable glass
    for i in range(3):
        za = -43 + 4 * i
        shell(k, "standingseamroof", (-14, y, za, 14, y + 1.2, za + 4), "-z", t=0.25)
        k.box("breakableglass", -14, y, za - 0.0625, 14, y + 1.2, za + 0.0625)
        k.box("galvanizedsteel", -14.2, y, za - 0.1, -14, y + 1.25, za + 0.1)
        k.box("galvanizedsteel", 14, y, za - 0.1, 14.2, y + 1.25, za + 0.1)
    # the light wells (x -16..-14, 14..16) open; a ladder up out of each onto the roof, a rail round each well
    k.ladder(-15.88, -37, 6.0, y, "w")
    k.ladder(15.88, -37, 6.0, y, "e")
    for sx in (-1, 1):
        a, b = sorted((sx * 14, sx * 16))
        rails_x(k, "galvanizedsteel", a, b, -31.06, y, h=1.0)
    # the loft: pallets of crates (cover, out of the middle lane)
    for (a, b) in ((-12, -42.5), (6, -42.5), (-4, -33)):
        k.box("cratewood", a, 6.0, b, a + 3, 7.2, b + 1.6)

    # ================================================================== the links: the ring and the spokes
    # office -> centre: scaffold-board stairs 20 -> 16 over the street (z -1.5..1.5); centre -> hotel 16 -> 14
    k.stairs("largescaffoldboards", -16, -1.5, -9, 1.5, 20.0, "x", -1, base=16.0, solid=False)
    k.stairs("largescaffoldboards", 9, -1.5, 16, 1.5, 16.0, "x", -1, base=14.0, solid=False)
    # office -> warehouse and office -> tenement: long stairs (x -21..-17)
    k.stairs("steeldeckplate", -21, -27, -17, -12, 20.0, "z", 1, base=10.0, solid=False)
    k.stairs("steeldeckplate", -21, 12, -17, 26, 20.0, "z", -1, base=12.0, solid=False)
    # centre -> warehouse, centre -> tenement: plank ramps (x 2..5); warehouse -> hotel, hotel -> tenement (x 17..21)
    shell(k, "scaffoldboards", (2, 10.0, -27, 5, 16.0, -9), "+z", t=0.25)
    shell(k, "scaffoldboards", (2, 12.0, 9, 5, 16.0, 26), "-z", t=0.25)
    shell(k, "scaffoldboards", (17, 10.0, -27, 21, 14.0, -13), "+z", t=0.25)
    shell(k, "scaffoldboards", (17, 12.0, 13, 21, 14.0, 26), "-z", t=0.25)
    # scaffold poles under the long spans (to the street; out of the street's middle)
    for (x, z, top) in [(-21.2, -26.8, 10), (-16.8, -26.8, 10),
                        (-21.2, 25.8, 12), (-16.8, 25.8, 12)]:
        k.box("galvanizedsteel", x - 0.06, 0, z - 0.06, x + 0.06, top - 0.5, z + 0.06)
    # the tenement's fire escape on its north face (x -14..-6): three flights in two lanes, landings at the ends
    IN, OUT = (24.4, 26.0), (22.8, 24.4)
    k.stairs("galvanizedgrating", -14, IN[0], -6, IN[1], 4.0, "x", 1, base=0.0)
    k.stairs("galvanizedgrating", -14, OUT[0], -6, OUT[1], 8.0, "x", -1, base=4.0, solid=False)
    k.stairs("galvanizedgrating", -14, IN[0], -6, IN[1], 12.0, "x", 1, base=8.0, solid=False)
    k.box("galvanizedgrating", -6, 3.8, 22.8, -4.4, 4.0, 26)
    k.box("galvanizedgrating", -15.6, 7.8, 22.8, -14, 8.0, 26)

    # ================================================================== ladders (from the streets, from the alley)
    LAD = [(-15.88, -7, 0, W[4], "w"), (-44.12, 6, 4, W[4], "e"),          # office: street (east face), alley (west)
           (15.88, 8, 0, E[4], "e"), (44.12, -10, 4, E[4], "w"),            # hotel
           (-10, -26.88, 0, N[4], "n"), (-19, -44.12, 4, N[4], "s"),          # warehouse
           (10, 25.88, 0, S[4], "s"), (0, 44.12, 4, S[4], "n"),            # tenement
           (9.12, -5, 0, C[4], "w"), (-9.12, 5, 0, C[4], "e")]              # centre
    for (x, z, y0, y1, f) in LAD:
        k.ladder(x, z, y0, y1, f)

    # ================================================================== parapets (gaps at every link and ladder top)
    parapet(k, W, {"e": [(-1.5, 1.5), (-8, -6)], "w": [(5, 7)], "n": [(-21, -17)], "s": [(-21, -17)]})
    parapet(k, E, {"w": [(-1.5, 1.5), (7, 9)], "e": [(-11, -9)], "n": [(17, 21)], "s": [(17, 21)]})
    parapet(k, (N[0], N[1], N[2], N[3], N[4]), {"s": [(-21, -17), (2, 5), (17, 21), (-11, -9)], "n": [(-20, -18)],
                                               "w": [], "e": []})
    parapet(k, S, {"n": [(-21, -17), (2, 5), (17, 21), (-8, -5.5), (9, 11)], "s": [(-1, 1)]})
    parapet(k, C, {"w": [(-1.5, 1.5), (4, 6)], "e": [(-1.5, 1.5), (-6, -4)], "n": [(2, 5)], "s": [(2, 5)]})

    # ================================================================== the office roof (20): plant room, tanks, AC
    y = W[4]
    for zz in (-4, 4):
        wall_x(k, "galvanizedsteel", -38, -30, zz, y, y + 3.2, holes=[(-34, 1.8, 2.4)], t=0.12)
    for xx in (-38, -30):
        wall_z(k, "galvanizedsteel", -4, 4, xx, y, y + 3.2, holes=[(0, 1.8, 2.4)], t=0.12)
    k.box("leadsheet", -38.2, y + 3.2, -4.2, -29.8, y + 3.5, 4.2)
    k.box("radiatorfins", -37, y, -1, -35, y + 1.6, 1)                     # (the plant inside)
    k.box("ventilationduct", -33, y, -3.5, -31, y + 2.2, -1.5)
    for zc in (-7, 7):                                                      # water tanks on stilts (3 m: run under)
        for (dx, dz) in ((-1.3, -1.3), (1.3, -1.3), (-1.3, 1.3), (1.3, 1.3)):
            k.box("galvanizedsteel", -26 + dx - 0.1, y, zc + dz - 0.1, -26 + dx + 0.1, y + 3.0, zc + dz + 0.1)
        k.box("galvanizedgrating", -27.6, y + 3.0, zc - 1.6, -24.4, y + 3.2, zc + 1.6)
        k.ring("cratewood", -26, zc, 0, 1.5, y + 3.2, y + 5.6, n=8)
    for (a, b) in ((-37, 8.5), (-37, -10.5), (-42, -2)):                    # AC units (solid cover)
        k.box("radiatorfins", a, y, b, a + 2, y + 1.4, b + 1.6)

    # ================================================================== the hotel roof (14): the neon sign, the perch,
    # the motor room (a ladder up its west face to the perch), the pool, the cabana
    y = E[4]
    k.box("chippedpaint", 40, y, 6, 44, 22.0, 10)                          # the lift motor room
    k.ladder(39.88, 8, y, 22.0, "e")
    k.box("galvanizedgrating", 40, 21.8, -6, 44, 22.0, 6)                  # the perch
    rails_z(k, "galvanizedsteel", -6, 10, 43.94, 22.0)
    rails_x(k, "galvanizedsteel", 40, 44, -5.94, 22.0)
    k.box("neonmaze", 40.0, 16.0, -6, 40.12, 21.8, 6)                      # the sign (facing west), its frame below
    for zz in (-6, -2, 2, 6):
        k.box("galvanizedsteel", 40.12, y, zz - 0.1, 40.32, 21.8, zz + 0.1)
    for (a, b, c, d) in [(24, 3, 34, 3.3), (24, 8.7, 34, 9), (24, 3.3, 24.3, 8.7), (33.7, 3.3, 34, 8.7)]:
        k.box("bluepooltile", a, y, b, c, y + 1.0, d)                      # the raised pool (vault its rim)
    k.box("bluepooltile", 24.3, y, 3.3, 33.7, y + 0.1, 8.7)
    k.box("clearlagoon", 24.3, y + 0.1, 3.3, 33.7, y + 0.85, 8.7)
    for (a, b) in ((24, -10), (30, -10), (24, -5), (30, -5)):              # the cabana
        k.box("wideplanks", a - 0.1, y, b - 0.1, a + 0.1, y + 2.8, b + 0.1)
    k.box("leantocanvas", 23.6, y + 2.8, -10.4, 30.4, y + 3.0, -4.6)
    k.box("wideplanks", 25, y, -9, 29, y + 0.5, -8)

    # ================================================================== the tenement roof (12): chimneys, the shed
    y = S[4]
    for (a, b) in ((-12, 30), (10, 40), (12, 30.5), (-4, 40)):
        k.box("londonstock", a, y, b, a + 1.2, y + 2.2, b + 0.9)
    k.box("corrugated", -18, y, 36, -14, y + 2.6, 36.2)
    k.box("corrugated", -18, y, 39.8, -14, y + 2.6, 40)
    k.box("corrugated", -18.2, y, 36, -18, y + 2.6, 40)
    k.box("corrugated", -18.2, y + 2.6, 35.8, -13.8, y + 2.8, 40.2)       # the shed (open to the east)
    k.box("galvanizedsteel", -10, y, 37.95, -9.9, y + 2.4, 38.05)          # a washing line (2.4: over your head)
    k.box("galvanizedsteel", 2, y, 37.95, 2.1, y + 2.4, 38.05)
    k.box("galvanizedsteel", -9.9, y + 2.3, 37.98, 2, y + 2.34, 38.02)

    # ================================================================== the centre (16): the billboard on posts (2.5 m
    # under it), its catwalk at 22 (x 0.1..1.6), the electrics cabinet with a ladder up its south face
    y = C[4]
    for zz in (-6, 6):
        k.box("galvanizedsteel", -0.15, y, zz - 0.15, 0.15, 24.5, zz + 0.15)
    k.box("lightblock", -0.1, 18.5, -6, 0.1, 24.5, 6)
    k.box("galvanizedgrating", 0.1, 21.8, -6, 1.6, 22.0, 6)
    rails_z(k, "galvanizedsteel", -6, 6, 1.54, 22.0)
    k.box("chippedpaint", 0.1, y, 6, 1.6, 22.0, 7.5)
    k.ladder(0.85, 7.62, y, 22.0, "n")

    # aerials (grapple anchors, over everything: nothing in a lane)
    for (x, z, y0, y1) in ((-36, -2, 23.5, 31), (-32, 2, 23.5, 28), (42, 8, 22, 29), (-12, 42, 12, 18)):
        k.box("galvanizedsteel", x - 0.08, y0, z - 0.08, x + 0.08, y1, z + 0.08)
        k.box("galvanizedsteel", x - 1.2, y1 - 1.5, z - 0.05, x + 1.2, y1 - 1.4, z + 0.05)

    # ================================================================== the streets and the alley: few big pieces
    for (a, b, c, d, mat) in [(-40, -24, -35, -21.6, "powdercoatyellow"), (30, 20, 35, 22.4, "powdercoatgreen"),
                              (-32, 18, -27.5, 20.4, "powdercoatgreen"), (28, -24, 32.5, -21.6, "powdercoatyellow")]:
        k.box(mat, a, 0.4, b, c, 2.6, d)                                   # parked vans
        k.box("rubber", a + 0.4, 0, b + 0.1, a + 1.2, 0.6, d - 0.1); k.box("rubber", c - 1.2, 0, b + 0.1, c - 0.4, 0.6, d - 0.1)
    for (a, b) in ((-47.4, -20), (-47.4, 26), (46.4, -26), (46.4, 20), (-30, -47.4), (24, 46.4)):
        k.box("powdercoatgreen", a, 4.0, b, a + 1.0, 5.2, b + 1.8)         # bins on the alley (its outer edge)

    # ================================================================== lights (sodium streets, neon, windows)
    L = [(-30, 6, -30, 16, 0.6), (30, 6, -30, 16, 0.6), (-30, 6, 30, 16, 0.6), (30, 6, 30, 16, 0.6), (0, 5, -18, 12, 0.6),
         (0, 5, 18, 12, 0.6), (-12.5, 5, 0, 10, 0.5), (12.5, 5, 0, 10, 0.5), (-46, 6, 0, 14, 0.5), (46, 6, 0, 14, 0.5),
         (0, 6, -46, 14, 0.5), (0, 6, 46, 14, 0.5), (-30, 23, 0, 16, 0.6), (-34, 22.5, 0, 6, 0.5), (30, 18, 0, 16, 0.6),
         (38.5, 19, 0, 10, 0.8), (0, 13, -35, 16, 0.5), (0, 8.5, -37, 14, 0.6), (0, 15.5, 35, 16, 0.6), (-3, 20, 0, 12, 0.6),
         (-10, 9, 22, 8, 0.4), (-18, 16, -19, 10, 0.4), (-18, 16, 19, 10, 0.4), (19, 13, -20, 10, 0.4), (19, 13, 20, 10, 0.4)]
    for (x, y, z, r, i) in L:
        k.light(x, y, z, r, i)

    # ================================================================== spawns (8, any): over the five roofs
    for (x, y, z, tx, tz) in [(-22, 20, -4, 0, 0), (-41, 20, 6, -16, 0), (36, 14, -2, 16, 0), (21, 14, 8, 0, 0),
                              (-8, 10, -29, 10, -29), (-10, 12, 34, 10, 34), (12, 12, 36, -10, 34), (-5, 16, -4, 9, 0)]:
        k.spawn("any", x, y, z, yaw_to(x, z, tx, tz))
    m.save("rooftops", dress=False)
