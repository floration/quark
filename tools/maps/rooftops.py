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
every corner yard and a ladder from it up every building's back.
(1.19.6 fix round, all review notes YES: the facades have recessed window bands with steel mullions, sills, string
courses, a cornice at every roof line (open at the links and ladder tops), downpipes, a fifth of the panes lit, shop
fronts with awnings on four street faces, and the sheds and the city round the block are dressed (bare walls 84% ->
25%); stair cores in the office (north-west) and the tenement (south-west): switchback flights from a street door, a
door onto the alley at 4 m, a roof hut at the top, so street -> roof -> bridge -> next building runs as a loop (the
ladders stay as the second way up); the warehouse's ground floor is a racked hall with a roller-door opening and a stair
up into the loft; the billboard is a solid board with neon on both faces lit by two light-block lamps; the sawtooth's
gables are closed; puddles on three roofs and the alley's corners; the plant room's walls 0.2 m (not wallbang).)"""
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


def facade(k, b, wall, band, roofmat, base="londonstock", holes=(), gaps=None, seed=0):
    """a building: a plinth, storeys of wall with a window band in each (recessed 0.2 m, steel mullions about every 3 m,
    about a fifth of the panes lit), the roof slab (top at the roof), a cornice 0.4 m proud under the roof line (not
    at gaps {edge: [(lo, hi)]}: links and ladder tops), downpipes at the corners; holes: rects left out (stair cores)"""
    x0, z0, x1, z1, y = b
    R = (x0, z0, x1, z1)
    def put(mat, rect, ya, yb):
        for (a, c, bb, d) in minus(rect, list(holes)): k.box(mat, a, ya, c, bb, yb, d)
    def in_hole(edge, u):
        for (hx0, hz0, hx1, hz1) in holes:
            if edge in "ns" and hx0 - 0.3 <= u <= hx1 + 0.3 and ((edge == "n" and hz0 <= z0 + 0.01) or (edge == "s" and hz1 >= z1 - 0.01)): return True
            if edge in "we" and hz0 - 0.3 <= u <= hz1 + 0.3 and ((edge == "w" and hx0 <= x0 + 0.01) or (edge == "e" and hx1 >= x1 - 0.01)): return True
        return False
    gaps = gaps or {}
    def ring(mat, ya, yb, d):
        """a strip d proud round the building from ya to yb, cut at the gaps (and the cores)"""
        for edge in "nswe":
            a0, a1 = (x0 - d, x1 + d) if edge in "ns" else (z0, z1)
            runs = [(a0, a1)]
            cutl = list(gaps.get(edge, ()))
            for (hx0, hz0, hx1, hz1) in holes:
                if edge == "n" and hz0 <= z0 + 0.01: cutl.append((hx0 + 0.5, hx1 - 0.5))
                if edge == "s" and hz1 >= z1 - 0.01: cutl.append((hx0 + 0.5, hx1 - 0.5))
                if edge == "w" and hx0 <= x0 + 0.01: cutl.append((hz0 + 0.5, hz1 - 0.5))
                if edge == "e" and hx1 >= x1 - 0.01: cutl.append((hz0 + 0.5, hz1 - 0.5))
            for (lo, hi) in cutl:
                runs = [r for (p, q) in runs for r in ((p, min(q, lo - 0.5)), (max(p, hi + 0.5), q)) if r[1] - r[0] > 0.2]
            for (p, q) in runs:
                if edge == "n": k.box(mat, p, ya, z0 - d, q, yb, z0)
                elif edge == "s": k.box(mat, p, ya, z1, q, yb, z1 + d)
                elif edge == "w": k.box(mat, x0 - d, ya, p, x0, yb, q)
                else: k.box(mat, x1, ya, p, x1 + d, yb, q)
    put(base, R, 0, 1.0)
    h, fl = 1.0, 0
    while h < y - 0.3 - 1e-6:
        top = min(h + 4.0, y - 0.3)
        put(wall, R, h, min(h + 2.4, top))
        ring("precastconcrete", h, h + 0.12, 0.06)                                # the floor line's string course
        if h + 2.4 < top:
            put(band, (x0 + 0.2, z0 + 0.2, x1 - 0.2, z1 - 0.2), h + 2.4, top)
            ring("precastconcrete", h + 2.26, h + 2.4, 0.1)                           # the window sills
            if top - h - 2.4 > 0.8:
                for (ei, edge) in enumerate("nswe"):
                    a0, a1 = (x0, x1) if edge in "ns" else (z0, z1)
                    n = int((a1 - a0) / 3.0)
                    st = (a1 - a0) / n
                    for i in range(n + 1):
                        u = a0 + st * i
                        if in_hole(edge, u): continue
                        f = {"n": z0, "s": z1, "w": x0, "e": x1}[edge]
                        sg = 1 if edge in "nw" else -1        # (into the building)
                        d0, d1 = sorted((f + sg * 0.02, f + sg * 0.2))
                        if 0 < i < n:
                            if edge in "ns": k.box("galvanizedsteel", u - 0.05, h + 2.4, d0, u + 0.05, top, d1)
                            else: k.box("galvanizedsteel", d0, h + 2.4, u - 0.05, d1, top, u + 0.05)
                        if i < n and (i * 7 + fl * 3 + ei + seed) % 5 == 0 and not in_hole(edge, u + st):
                            p0, p1 = sorted((f + sg * 0.18, f + sg * 0.2))
                            if edge in "ns": k.box("glow", u + 0.1, h + 2.5, p0, u + st - 0.1, top - 0.1, p1)
                            else: k.box("glow", p0, h + 2.5, u + 0.1, p1, top - 0.1, u + st - 0.1)
        h = top; fl += 1
    put(roofmat, R, y - 0.3, y)
    # the cornice (0.4 m proud, 0.4 m deep under the roof line), cut at the gaps
    ring("precastconcrete", y - 0.7, y - 0.3, 0.4)
    # downpipes down the corners
    for (cx, cz, dx, dz) in ((x0, z0, -1, 1), (x1, z0, 1, 1), (x0, z1, -1, -1), (x1, z1, 1, -1)):
        px = cx + dx * 0.12; pz = cz + dz * 0.5
        if any(hx0 - 0.5 <= px <= hx1 + 0.5 and hz0 - 0.5 <= pz <= hz1 + 0.5 for (hx0, hz0, hx1, hz1) in holes): continue
        k.box("castiron", px - 0.08, 0, pz - 0.08, px + 0.08, y - 0.7, pz + 0.08)


def core(k, rect, roof, start, mat, street, alley, hut_door):
    """a stair core (fix round): rect (x0, z0, x1, z1), 4 m across x and 6 m along z, walls 0.5 m; switchback flights
    of 2 m (lanes split across x, landings 1.2 m at the z ends) from the street (0) up to the roof, starting at the
    `start` end ('n' or 's'); doors: street (face, centre) at 0, alley (face, centre) at 4, a roof hut over it (2.8 m)
    with its door (face, centre) at the roof"""
    x0, z0, x1, z1 = rect
    ix0, iz0, ix1, iz1 = x0 + 0.5, z0 + 0.5, x1 - 0.5, z1 - 0.5
    mid = (ix0 + ix1) / 2
    land = {"n": (iz0, iz0 + 1.2), "s": (iz1 - 1.2, iz1)}
    top = roof + 2.8
    doors = {"n": [], "s": [], "w": [], "e": []}
    for (face, c, y0, w) in (street + (0.0, 1.2), alley + (4.0, 1.2), hut_door + (roof, 1.2)):
        doors[face].append((c, w, y0))
    for face in "nswe":
        if face in "ns":
            zc = z0 + 0.25 if face == "n" else z1 - 0.25
            spans = [(x0, x1)]
        else:
            zc = x0 + 0.25 if face == "w" else x1 - 0.25
        holes = doors[face]
        # (a wall from 0 to the hut's top with door holes at their heights: built as runs between holes per band)
        cuts = sorted(set([0.0, top] + [y for (_, _, y) in holes] + [y + 2.4 for (_, _, y) in holes]))
        for (ya, yb) in zip(cuts, cuts[1:]):
            hs = [(c, w, yb - ya) for (c, w, y) in holes if y <= ya + 1e-6 and y + 2.4 >= yb - 1e-6]
            if face in "ns": wall_x(k, mat, x0, x1, zc, ya, yb, hs, t=0.5)
            else: wall_z(k, mat, z0 + 0.5, z1 - 0.5, zc, ya, yb, hs, t=0.5)
    k.box(mat, x0 - 0.1, top, z0 - 0.1, x1 + 0.1, top + 0.25, z1 + 0.1)          # the hut's roof
    y, end = 0.0, start
    f = 0
    while y < roof - 1e-6:
        dest = "s" if end == "n" else "n"
        lx0, lx1 = (ix0, mid) if f % 2 == 0 else (mid, ix1)
        rising = 1 if dest == "s" else -1
        k.stairs("concrete", lx0, iz0 + 1.2, lx1, iz1 - 1.2, y + 2.0, "z", rising, base=y, solid=(f < 2))
        y += 2.0
        la, lb = land[dest]
        k.box("concrete", ix0, (y - 0.3) if f >= 1 else 0.0, la, ix1, y, lb)
        end = dest; f += 1
    # the top landing's rail over the well (the lane the last flight didn't use)
    la, lb = land[end]
    rz = lb if end == "n" else la
    lx0, lx1 = (ix0, mid) if (f - 1) % 2 == 1 else (mid, ix1)
    k.box("galvanizedsteel", lx0, roof, min(rz, rz + (0.1 if end == "n" else -0.1)), lx1, roof + 1.0, max(rz, rz + (0.1 if end == "n" else -0.1)))


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
    # the roofs' edge gaps (links and ladder tops): the parapets and the cornices leave them open
    GW = {"e": [(-1.5, 1.5), (-8, -6)], "w": [(5, 7)], "n": [(-21, -17)], "s": [(-21, -17)]}
    GE = {"w": [(-1.5, 1.5), (7, 9)], "e": [(-11, -9)], "n": [(17, 21)], "s": [(17, 21)]}
    GN = {"s": [(-21, -17), (2, 5), (17, 21), (-11, -9)], "n": [(-20, -18)], "w": [], "e": []}
    GS = {"n": [(-21, -17), (2, 5), (17, 21), (-8, -5.5), (9, 11)], "s": [(-1, 1)]}
    GC = {"w": [(-1.5, 1.5), (4, 6)], "e": [(-1.5, 1.5), (-6, -4)], "n": [(2, 5)], "s": [(2, 5)]}
    CORE_W, CORE_S = (-44, -12, -40, -6), (-22, 38, -18, 44)

    # ================================================================== streets, the alley ring, the city, bounds
    for (x0, z0, x1, z1) in tiles([(-44, -44, 44, 44)], 22):
        k.box("asphalt", x0, -0.4, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in [(-48, -48, -44, 48), (44, -48, 48, 48), (-44, -48, 44, -44), (-44, 44, 44, 48)]:
        k.box("corrugated", x0, 0, z0, x1, 3.7, z1)                        # the back sheds
        k.box("rainwetpaving", x0, 3.7, z0, x1, 4.0, z1)                   # (their roofs: the alley)
    # the sheds' street faces: roller shutters and a gutter (fix round: no bare 88 m walls along the streets)
    for (axis, f, sg) in (("x", -44, 1), ("x", 44, -1), ("z", -44, 1), ("z", 44, -1)):
        o0, o1 = sorted((f, f + sg * 0.06))
        g0, g1 = sorted((f, f + sg * 0.18))
        if axis == "x": k.box("castiron", g0, 3.5, -44, g1, 3.66, 44)
        else: k.box("castiron", -44, 3.5, g0, 44, 3.66, g1)
        for u in range(-42, 40, 6):
            if axis == "x" and any(a <= u + 1.5 and u + 1.5 <= b for (a, b) in ((-36.5, -32.5), (32.5, 36.5))): continue
            if axis == "x": k.box("rollershutter", o0, 0, u, o1, 2.8, u + 3.0)
            else: k.box("rollershutter", u, 0, o0, u + 3.0, 2.8, o1)
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
            # (fix round) their inner faces dressed: back doors and AC boxes on the alley, window rows above (a few lit)
            f = [-48, 48, -48, 48][side]; sg = [1, -1, 1, -1][side]    # (the inner face, and out of it toward the block)
            o0, o1 = sorted((f, f + sg * 0.1))
            q0, q1 = sorted((f, f + sg * 0.5))
            for (i, u) in enumerate(range(a0 + 2, a1 - 2, 4)):
                rows = [y_ for y_ in (6.6, 9.6, 12.6) if y_ + 1.6 < h]
                for (r_, yw) in enumerate(rows):
                    mat = "glow" if (i * 3 + r_ + side + j) % 5 == 0 else "rainwindow"
                    if side < 2: k.box(mat, o0, yw, u, o1, yw + 1.5, u + 2.0)
                    else: k.box(mat, u, yw, o0, u + 2.0, yw + 1.5, o1)
                if i % 2 == 0:   # a back door onto the alley, or an AC box on brackets
                    if side < 2: k.box("steeldoorskin", o0, 4.0, u + 0.4, o1, 6.0, u + 1.4)
                    else: k.box("steeldoorskin", u + 0.4, 4.0, o0, u + 1.4, 6.0, o1)
                elif side < 2: k.box("radiatorfins", q0, 5.0, u + 0.4, q1, 5.8, u + 1.6)
                else: k.box("radiatorfins", u + 0.4, 5.0, q0, u + 1.6, 5.8, q1)
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
    facade(k, W, "concrete", "rainwindow", "concreterooftiles", holes=[CORE_W], gaps=GW, seed=0)
    facade(k, E, "creambrick", "rainwindow", "rainwetpaving", gaps=GE, seed=1)
    facade(k, S, "londonstock", "rainwindow", "birdspottedroof", holes=[CORE_S], gaps=GS, seed=2)
    facade(k, C, "graffitibuffedwall", "londonstock", "puddledground", gaps=GC, seed=3)
    # the stair cores (fix round): the office's in its north-west corner, the tenement's in its south-west corner: in
    # from the street, a door onto the alley at 4 m, out through a roof hut: street -> roof -> bridge -> next building
    core(k, CORE_W, W[4], "n", "concrete", ("n", -42.0), ("w", -11.0), ("e", -11.0))
    core(k, CORE_S, S[4], "s", "londonstock", ("w", 43.0), ("s", -20.0), ("e", 43.0))
    # shop fronts on the street faces: a lit window, a door, a fascia, an awning at 3.4-3.9 (over your head)
    for (face, c0, c1, f, mat) in [("x", 3.0, 10.0, -16.0, "powdercoatgreen"), ("x", -10.0, -3.0, 16.0, "paintedredbrick"),
                                   ("z", -7.0, 0.0, -27.0, "powdercoatyellow"), ("z", -4.0, 6.0, 26.0, "pinkbrick")]:
        sg = 1 if f in (-16.0, 26.0) else -1          # (out of the face, toward the street)
        o0, o1 = sorted((f, f + sg * 0.08))
        a0, a1 = sorted((f + sg * 0.0, f + sg * 1.4))
        if face == "x":
            k.box("glow", o0, 0.8, c0 + 0.4, o1, 2.6, c1 - 2.0); k.box("chippedpaint", o0, 0, c1 - 1.6, o1, 2.4, c1 - 0.4)
            k.box("neonmaze", o0, 2.9, c0, o1, 3.3, c1)
            k.cut(mat, (a0, 3.4, c0, a1, 3.9, c1), [(-sg * 0.36, 1, 0, 3.9 - sg * 0.36 * f), (sg * 0.36, -1, 0, -(3.9 - sg * 0.36 * f) + 0.12)])
        else:
            k.box("glow", c0 + 0.4, 0.8, o0, c1 - 2.0, 2.6, o1); k.box("chippedpaint", c1 - 1.6, 0, o0, c1 - 0.4, 2.4, o1)
            k.box("neonmaze", c0, 2.9, o0, c1, 3.3, o1)
            k.cut(mat, (c0, 3.4, a0, c1, 3.9, a1), [(0, 1, -sg * 0.36, 3.9 - sg * 0.36 * f), (0, -1, sg * 0.36, -(3.9 - sg * 0.36 * f) + 0.12)])
    # the warehouse: solid to 6, the loft (x -16..16, z -43..-31, 6..10) walled round, the walkway along its south
    x0, z0, x1, z1, y = N
    # its ground floor (fix round): a hall with racks in aisles, a roller-door opening on the street (south), a stair up
    # through the loft's floor (north-west): the loft has a way down inside
    wall_x(k, "corrugated", x0, x1, z1 - 0.2, 0, 5.7, [(9.5, 3.0, 3.2)], t=0.4)
    wall_x(k, "corrugated", x0, x1, z0 + 0.2, 0, 5.7, t=0.4)
    wall_z(k, "corrugated", z0 + 0.4, z1 - 0.4, x0 + 0.2, 0, 5.7, t=0.4)
    wall_z(k, "corrugated", z0 + 0.4, z1 - 0.4, x1 - 0.2, 0, 5.7, t=0.4)
    for (a, b, c, d) in [(x0, z0, x1, z0 + 0.4), (x0, z1 - 0.4, x1, z1)]:
        k.box("londonstock", a, 0, b, c, 1.0, d)
    k.box("rollershutter", 8.0, 3.2, z1 - 0.1, 11.0, 4.0, z1)                     # (the rolled-up door's drum)
    for (a, b) in ((-20.4, -16), (2, 20.4)):
        for zr in (-37.5, -33.0):
            k.box("steelgridwall", a, 0, zr, b, 3.2, zr + 1.0)                      # racks (aisles between)
    for (a, b) in ((-20, -35.5), (12, -30.5), (-6, -30.5)):
        k.box("palletstack", a, 0, b, a + 1.6, 1.6, b + 1.6)
    k.stairs("steeldeckplate", -14, -42.6, -6, -40.6, 6.0, "x", 1, base=0.0)
    for (a, c, b, d) in minus((-16, -43, 16, -31), [(-14, -42.6, -6, -40.6)]):
        k.box("concrete", a, 5.7, c, b, 6.0, d)                                      # (the loft's floor)
    k.box("galvanizedsteel", -14, 6.0, -40.6, -6, 7.0, -40.48); k.box("galvanizedsteel", -14.12, 6.0, -42.6, -14, 7.0, -40.48)
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
        for (ga, gb) in ((-14.2, -14.0), (14.0, 14.2)):                             # the gables, closed
            k.cut("standingseamroof", (ga, y, za, gb, y + 1.2, za + 4), [(0, 1, 0.3, y + 1.2 + 0.3 * za)])
    # the light wells (x -16..-14, 14..16) open; a ladder up out of each onto the roof, a rail round each well
    k.ladder(-15.88, -37, 6.0, y, "w")
    k.ladder(15.88, -37, 6.0, y, "e")
    for sx in (-1, 1):
        a, b = sorted((sx * 14, sx * 16))
        rails_x(k, "galvanizedsteel", a, b, -31.06, y, h=1.0)
    # the loft: pallets of crates (cover, out of the middle lane)
    for (a, b) in ((-3, -42.5), (6, -42.5), (-4, -33)):
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
    parapet(k, W, GW)
    parapet(k, E, GE)
    parapet(k, (N[0], N[1], N[2], N[3], N[4]), GN)
    parapet(k, S, GS)
    parapet(k, C, GC)

    # ================================================================== the office roof (20): plant room, tanks, AC
    y = W[4]
    for zz in (-4, 4):
        wall_x(k, "galvanizedsteel", -38, -30, zz, y, y + 3.2, holes=[(-34, 1.8, 2.4)], t=0.2)
    for xx in (-38, -30):
        wall_z(k, "galvanizedsteel", -4, 4, xx, y, y + 3.2, holes=[(0, 1.8, 2.4)], t=0.2)
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
    k.box("corrugated", -16, y, 30, -12.5, y + 2.6, 30.2)
    k.box("corrugated", -16, y, 33.8, -12.5, y + 2.6, 34)
    k.box("corrugated", -16.2, y, 30, -16, y + 2.6, 34)
    k.box("corrugated", -16.2, y + 2.6, 29.8, -12.3, y + 2.8, 34.2)       # the shed (open to the east)
    k.box("galvanizedsteel", -10, y, 37.95, -9.9, y + 2.4, 38.05)          # a washing line (2.4: over your head)
    k.box("galvanizedsteel", 2, y, 37.95, 2.1, y + 2.4, 38.05)
    k.box("galvanizedsteel", -9.9, y + 2.3, 37.98, 2, y + 2.34, 38.02)

    # ================================================================== the centre (16): the billboard on posts (2.5 m
    # under it), its catwalk at 22 (x 0.1..1.6), the electrics cabinet with a ladder up its south face
    y = C[4]
    for zz in (-6, 6):
        k.box("galvanizedsteel", -0.15, y, zz - 0.15, 0.15, 24.5, zz + 0.15)
    # (fix round: a solid board with a neon face each side, lit by light-block lamps, not one big light block)
    k.box("chippedpaint", -0.15, 18.5, -6, 0.15, 24.5, 6)
    for (a, b) in ((-0.21, -0.15), (0.15, 0.21)):
        k.box("neonmaze", a, 18.8, -5.7, b, 24.2, 5.7)
    k.box("galvanizedgrating", 0.21, 21.8, -6, 1.7, 22.0, 6)
    rails_z(k, "galvanizedsteel", -6, 6, 1.64, 22.0)
    k.box("lightblock", 1.7, 21.7, -6, 2.0, 22.0, 6)                                 # the lamps: one on the catwalk's edge,
    k.box("lightblock", -2.0, 18.2, -6, -1.7, 18.5, 6)                               # one on a bracket the other side
    for zz in (-5, 5):
        k.box("galvanizedsteel", -1.7, 18.3, zz - 0.05, -0.21, 18.4, zz + 0.05)
    k.box("chippedpaint", 0.21, y, 6, 1.7, 22.0, 7.5)
    k.ladder(0.95, 7.62, y, 22.0, "n")

    # puddles (the rain's look, consistent): on the office, tenement and hotel roofs and the alley's corners
    for (x0_, z0_, x1_, z1_, yy) in [(-28, 6, -23, 9, W[4]), (-36, -9, -32, -6.5, W[4]), (4, 32, 9, 35, S[4]), (-8, 30, -4, 33, S[4]),
                                     (20, -11, 23, -8, E[4]), (36, -2, 40, 1, E[4]),
                                     (-47.6, -47.6, -44.6, -45, 4.0), (44.6, -47.6, 47.6, -45, 4.0), (-47.6, 45, -44.6, 47.6, 4.0), (44.6, 45, 47.6, 47.6, 4.0)]:
        k.box("puddledground", x0_, yy, z0_, x1_, yy + 0.02, z1_)
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
    L += [(-42, 3, -9, 7, 0.5), (-42, 9, -9, 7, 0.5), (-42, 15, -9, 7, 0.5), (-20, 3, 41, 7, 0.5), (-20, 9, 41, 7, 0.5),
          (0, 3.5, -36, 14, 0.45)]   # (the stair cores; the warehouse's ground floor)
    for (x, y, z, r, i) in L:
        k.light(x, y, z, r, i)

    # ================================================================== spawns (8, any): over the five roofs
    for (x, y, z, tx, tz) in [(-22, 20, -4, 0, 0), (-41, 20, 6, -16, 0), (36, 14, -2, 16, 0), (21, 14, 8, 0, 0),
                              (-8, 10, -29, 10, -29), (-10, 12, 34, 10, 34), (12, 12, 36, -10, 34), (-5, 16, -4, 9, 0)]:
        k.spawn("any", x, y, z, yaw_to(x, z, tx, tz))
    m.save("rooftops", dress=False)
