"""WAR (1.19.6): multi point on a 420 x 160 m strip of an active warzone, attacked east to west through five points
about 80 m apart:
  A  the crater field (x 150): no-man's-land, shell craters (stepped pits, spoil slopes on their lips, mud only in
     the deepest), barbed wire, the burnt tank on the point's east edge and a broken sandbag ring on it;
  B  the trench line (x 70): a zig-zag trench 2.2 m deep with duckboards, steps out every 8 m, dugouts, 1.2 and 1.8 m
     sandbag parapets and MG nests, a wire belt in front of it; the point is the trench itself (its zone stops at
     ground level);
  C  the village crossroads (x -10): ruined houses (two with an upper floor, partial roofs), a church whose broken
     tower is a 14 m perch (stairs inside, a parapet notched on every side);
  D  the bridge (x -90): a damaged girder bridge over a swamp-water river (x 0.7 speed), a sunken barge to the north
     and a rubble ford to the south as the two flanks; the shell hole in its deck drops onto rubble, not into a trap;
  E  the HQ bunker (x -165): a concrete command bunker, three ways in (front, side, rear), an AA gun on the roof
     (stairs up inside, a ladder at the back); its outer faces and the roof's outer band can't be hooked.
The strip's long lines are broken every 40-80 m by a few big pieces (momentum flows round them): facades fallen
inward off the edges, spoil ridges, ruined walls across the strip, wrecked lorries and sandbag barricades across one
half of the road, smoke plumes over the three wrecks and smoke banks in the hollows (sight only). No ground line runs
over about 250 m (418 m before), and of the ground pairs 50-100 m apart 21% see each other (38%). The facades along
the edges are clipped from 8 m up (no sniper ledges).
Each stage's attackers spawn east of their point (south of the middle line) and its defenders west of it (north of
it), each group in a shelter (a sandbagged dugout, a ruined wall corner or a supply dump) closed toward its point and
its outer side and open toward the middle: the spawns face the opening and come out moving. The attackers' shelter
stands 25-45 m past the point they just took (for pace; the plan had them behind it). Arrival, from the game's own
metric (`quark_maptool metrics`, the bots' nav routes, attackers minus defenders): A -0.4 s, B +0.3 s, C -0.2 s,
D +0.1 s, E +0.4 s; on the walk grid (slowest spawn, 9.5 m/s) every stage is within 1.0 s.
Everything but the spoil slopes and the fallen facades' faces is axis-aligned boxes (the walk grid sees a brush as
its box). No fall damage: you only die below the void line.
Bomb (also offered): two sites in the trench line, each round one of its dugouts (south: z -50..-30, north: z 30..50;
x 58..80, glow posts at the corners). Their own spawn set (bomb-a / bomb-b, five a side; the staged points spawns
are untouched): team A at the crater field's east end behind a sandbag barricade, team B on the road west of the
village beside one. Both are about 5 s from either site at 24 m/s (nav: A 111-114 m, B 117 m; diff -0.3 / -0.7 s at
the metric's 9.5 m/s): equal on purpose, not the plan's "CT 1 s later", because a team keeps its spawns when the
sides switch at halftime (attackers come from bomb-b in the second half), so a CT head start would favour team A in
both halves (with A at 115 m and B at 144 m, 8 expert bots won 7 matches of 8 for team A). Equal: 24 seeds, rounds
attackers 143 / defenders 108, team A 134 / team B 117, matches 13-11, plants south 105 / north 99."""
import random
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 210, 80
G0 = -3.0                         # the ground's underside (trenches and craters are dug into it)
TRENCH_Y = -2.2                   # the trench floor
RIVER = (-102, -HZ, -78, HZ)      # the river (along z)
BED, SURF = -2.6, -1.0            # its bed and its surface
ROAD = (-148, -5, 60, 5)          # the road (along x) from the bunker's door through the village
STREET = (-14, -HZ, -6, HZ)       # the village street (along z)
SQUARE = (-22, -10, 2, 10)        # the crossroads square
DOOR_H = 3.2

# craters (cx, cz, w, d, depth): 0.5 m steps down to a mud bottom
CRATERS = [(186, 36, 12, 10, 2.0), (181, -42, 10, 12, 2.0), (168, 60, 8, 8, 1.5), (176, 14, 8, 8, 1.5),
           (160, -36, 12, 10, 2.5), (150, 32, 14, 12, 3.0), (145, 66, 10, 8, 2.0), (136, -58, 10, 10, 2.0),
           (132, 22, 12, 10, 2.5), (126, -26, 10, 8, 2.0), (110, 52, 12, 10, 2.5), (104, -46, 8, 10, 1.5),
           (96, 16, 10, 10, 2.0), (92, -28, 8, 8, 1.5),
           (-158, -34, 8, 8, 1.5), (-152, 44, 8, 8, 1.5), (-192, -48, 10, 8, 2.0)]   # (and shell holes round the bunker)
# the trench: 16 m legs along z, alternately at x 65..68.5 and 71.5..75, joined by cross legs
LEGS = [(-80 + 16 * i, -64 + 16 * i, (65, 68.5) if i % 2 == 0 else (71.5, 75)) for i in range(10)]

POINTS = {"A": (142, -8, 158, 8, 0), "B": (62, -8, 78, 8, TRENCH_Y), "C": (-18, -8, -2, 8, 0),
          "D": (-100, -5, -80, 5, 0), "E": (-170, -6, -156, 6, 0)}
# staged spawns: (x of the shelter wall, which side the spawns stand: +1 east (attackers), -1 west (defenders), z of
# the shelter's middle, its dressing). Off the road and the centre: attackers south of the middle line, defenders
# north of it; each shelter is closed toward its point and on its outer side, open toward the middle of the strip
SHELTERS = {"att0": (194, 1, 20, "dugout"), "def0": (104, -1, -20, "dump"), "att1": (116, 1, 20, "dugout"),
            "def1": (40, -1, -30, "dump"), "att2": (30, 1, 20, "ruin"), "def2": (-58, -1, -20, "ruin"),
            "att3": (-58, 1, 18, "dugout"), "def3": (-127, -1, -22, "dugout"), "att4": (-126, 1, 20, "dump"),
            "def4": (-200, -1, -20, "ruin")}


def crater_rect(c):
    cx, cz, w, d, _ = c
    return (cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2)


def trench_rects():
    """the trench's floor plan: the legs, the cross legs at each bend, the step-out bays and the dugouts"""
    legs = [(x0, z0, x1, z1) for (z0, z1, (x0, x1)) in LEGS]
    cross = [(65, z1 - 1.75, 75, z1 + 1.75) for (z0, z1, _) in LEGS[:-1]]
    return legs + cross


def bays():
    """step-out bays: two per leg (one each side), each a 3 m notch with a flight up to the ground"""
    out = []
    for i, (z0, z1, (x0, x1)) in enumerate(LEGS):
        za, zb = z0 + 3.5, z1 - 6.5
        if i == 0: za = z0 + 5
        out.append(("w", x0 - 3.5, za, x0, za + 3))
        out.append(("e", x1, zb, x1 + 3.5, zb + 3))
    return out


DUGOUTS = [(LEGS[i][2][0] - 6, (LEGS[i][0] + LEGS[i][1]) / 2 + 0.5, LEGS[i][2][0], (LEGS[i][0] + LEGS[i][1]) / 2 + 5) for i in (2, 7)]
HQ = (-182, -13, -148, 13)
BOMB_SITES = [(-50, -30), (30, 50)]                                  # (z ranges; x 58..80 over the trench)
BOMB_A = [(177, 4.5), (177, 7), (180, 4.5), (180, 7), (174, 5.75)]   # (the crater field, behind a sandbag barricade east of point A, facing west)
BOMB_B = [(-38, -2), (-38, -4), (-41, -2), (-41, -4), (-44, -3)]   # (on the road west of the village, beside a sandbag barricade, facing east)


def union(rects):
    """overlapping rects as non-overlapping pieces"""
    x0 = min(r[0] for r in rects); z0 = min(r[1] for r in rects); x1 = max(r[2] for r in rects); z1 = max(r[3] for r in rects)
    bb = (x0, z0, x1, z1)
    return minus(bb, minus(bb, rects))


def inter(a, b):
    r = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return r if r[2] > r[0] and r[3] > r[1] else None


def ruin(k, mat, x0, z0, x1, z1, h, ops=(), t=0.4, base=0.0):
    """a ruined building's four walls (outer faces x0..x1, z0..z1, 0..h); ops = [(side, centre, width, y0, y1)] cut out
    of a side (n = z0, s = z1, w = x0, e = x1): a door (0..3.2), a window (1.1..2.4), a breach (1.2..h)"""
    for side in "nswe":
        o = [(c - w / 2, c + w / 2, ya, yb) for (sd, c, w, ya, yb) in ops if sd == side]
        if side in "ns": a, b = x0, x1
        else: a, b = z0 + t, z1 - t
        cuts = sorted({a, b} | {v for (p, q, _, _) in o for v in (p, q) if a < v < b})
        for (p, q) in zip(cuts, cuts[1:]):
            mid = (p + q) / 2
            gone = sorted((ya, yb) for (pa, pb, ya, yb) in o if pa < mid < pb)
            y = 0.0
            spans = []
            for (ya, yb) in gone:
                if ya > y: spans.append((y, ya))
                y = max(y, yb)
            if y < h: spans.append((y, h))
            for (ya, yb) in spans:
                ya += base; yb += base
                if side == "n": k.box(mat, p, ya, z0, q, yb, z0 + t)
                elif side == "s": k.box(mat, p, ya, z1 - t, q, yb, z1)
                elif side == "w": k.box(mat, x0, ya, p, x0 + t, yb, q)
                else: k.box(mat, x1 - t, ya, p, x1, yb, q)


def tank(k, x, z, along, burning=True):
    """a burnt-out tank (7 x 3.4 m): tracks 1.2 m, the hull 1.7 m, the turret 2.7 m (each a jump up), a barrel 4 m out"""
    def b(mat, a0, y0, c0, a1, y1, c1):   # (a along the hull, c across it)
        if along == "x": k.box(mat, x + a0, y0, z + c0, x + a1, y1, z + c1)
        else: k.box(mat, x + c0, y0, z + a0, x + c1, y1, z + a1)
    b("charredtimber", -3.5, 0, -1.7, 3.5, 1.2, -1.0); b("charredtimber", -3.5, 0, 1.0, 3.5, 1.2, 1.7)   # tracks
    b("rustedsheet", -3.2, 0.4, -1.0, 3.2, 1.2, 1.0)
    b("tankarmour", -3.3, 1.2, -1.6, 3.3, 1.45, 1.6)                                               # (its last paint: a band)
    b("sootysteel", -3.3, 1.45, -1.6, 3.3, 1.7, 1.6)                                               # the hull deck
    b("sootysteel", -1.8, 1.7, -1.3, 0.8, 2.7, -0.3); b("sootysteel", -1.8, 1.7, 0.3, 0.8, 2.7, 1.3)   # the turret, its
    b("sootysteel", -1.8, 1.7, -0.3, -1.0, 2.7, 0.3); b("sootysteel", 0.0, 1.7, -0.3, 0.8, 2.7, 0.3)   # hatch blown open
    b("gunmetal", 0.8, 2.0, -0.15, 4.8, 2.3, 0.15)                                                 # the barrel (a hook)
    if burning:   # (the fire inside the open hatch, an orange light over it)
        b("lightblock", -1.0, 1.7, -0.3, 0.0, 2.2, 0.3)
        k.light(x, 3.4, z, 11, 1.0)


def shelter(k, xw, side, zc, style):
    """a spawn shelter: a 3.5 m wall across the strip toward the point (16 m along z) and a wing on its outer side
    (6 m back), open toward the middle of the strip, so players come out moving; dressed as a sandbagged dugout (an
    earth bank topped with sandbags), a ruined wall corner or a supply dump (stacked crates under a tarp)"""
    zo = 1 if zc > 0 else -1                                   # the outer side
    xa, xb = sorted((xw, xw + side * 6))
    za, zb = zc - 8, zc + 8
    wall = (xw - 0.5, za, xw + 0.5, zb)
    wing = (xa, zb - 0.6, xb, zb) if zo > 0 else (xa, za, xb, za + 0.6)
    if style == "dugout":
        for (x0, z0, x1, z1) in (wall, wing):
            k.box("dirt", x0 - 0.3, 0, z0, x1 + 0.3, 2.3, z1)
            k.box("sandbagwall", x0 - 0.1, 2.3, z0, x1 + 0.1, 3.5, z1)
        k.box("charredtimber", xa, 2.6, zb - 3 if zo > 0 else za, xb, 2.9, zb if zo > 0 else za + 3)   # (its roof logs)
        k.light(xw + side * 3, 2.4, zc, 8, 0.6)
    elif style == "ruin":
        for (x0, z0, x1, z1) in (wall, wing):
            k.box("burntoutruinwall", x0, 0, z0, x1, 3.5, z1)
        k.box("burntoutruinwall", wall[0], 3.5, zc + zo * 2, wall[2], 4.6, zc + zo * 8)                # (its broken top)
    else:   # dump
        for (x0, z0, x1, z1) in (wall, wing):
            k.box("woodencrate", x0 - 0.5, 0, z0, x1 + 0.5, 2.4, z1)
            k.box("woodencrate", x0 - 0.2, 2.4, z0 + 0.4, x1 + 0.2, 3.5, z1 - 0.4)
        k.box("corrugated", wall[0] - 0.6, 3.5, za, wall[2] + 0.6, 3.7, zb)                            # (the tarp)


def build():
    m = M("War", "points", sun=(-1.0, 0.32, 0.35))
    m.also = ["bomb"]
    k = Kit(m)
    R = random.Random("war")
    m.raw("palette base 0.52 0.48 0.44 accent 0.40 0.30 0.22 metal 0.26 0.25 0.24 glow 1.00 0.55 0.20 sky 0.50 0.42 0.36")
    m.raw("void -10")

    # ---- the ground, dug out where the craters, the trench and the river are
    trench = union(trench_rects())
    bay_rects = [(a, b, c, d) for (_, a, b, c, d) in bays()]
    sunk = [crater_rect(c) for c in CRATERS] + trench + bay_rects + DUGOUTS + [RIVER]
    for t in tiles([(-HX, -HZ, HX, HZ)], 30):
        for (x0, z0, x1, z1) in minus(t, sunk + [ROAD, STREET, SQUARE, HQ]): k.box("dirt", x0, G0, z0, x1, 0, z1)
        for (r, cut) in ((ROAD, [SQUARE]), (STREET, [SQUARE, ROAD])):
            r = inter(t, r)
            if r:
                for (x0, z0, x1, z1) in minus(r, sunk + cut): k.box("rutteddirtroad", x0, G0, z0, x1, 0, z1)
    k.box("flagstonepaving", SQUARE[0], G0, SQUARE[1], SQUARE[2], 0, SQUARE[3])
    k.box("polishedconcrete", HQ[0], G0, HQ[1], HQ[2], 0, HQ[3])

    # craters: rings 0.5 m down each, a mud bottom (x 0.6 speed: being knocked in costs you)
    for c in CRATERS:
        x0, z0, x1, z1 = crater_rect(c); n = int(round(c[4] / 0.5)); s = min(x1 - x0, z1 - z0) / 2 / (n + 0.6)
        for j in range(n):
            o = (x0 + j * s, z0 + j * s, x1 - j * s, z1 - j * s)
            if j == n - 1:   # (mud only in the deepest few: "stop dead" everywhere would kill the field's momentum)
                k.box("mud" if c[4] >= 2.5 else "smoulderingash", o[0], G0, o[1], o[2], -0.5 * n, o[3]); break
            i = (o[0] + s, o[1] + s, o[2] - s, o[3] - s)
            for (a, b, cc, d) in minus(o, [i]): k.box("crackedearth" if j == 0 else "dirt", a, G0, b, cc, -0.5 * (j + 1), d)

    # ---- the trench line (B): duckboards 2.2 m down, the step-out flights, dugouts, sandbags, MG nests
    for (x0, z0, x1, z1) in trench: k.box("scaffoldboards", x0, G0, z0, x1, TRENCH_Y, z1)
    for (side, x0, z0, x1, z1) in bays():
        k.stairs("crackedearth", x0, z0, x1, z1, 0, "x", 1 if side == "e" else -1, base=TRENCH_Y, rise=0.45)
    for (x0, z0, x1, z1) in DUGOUTS:
        k.box("scaffoldboards", x0, G0, z0, x1, TRENCH_Y, z1)
        k.box("charredtimber", x0 - 0.3, 0, z0 - 0.3, x1, 0.5, z1 + 0.3)            # the roof: logs and earth
        k.box("sandbagwall", x0 - 0.3, 0.5, z0 - 0.3, x1 - 1.5, 0.9, z1 + 0.3)
        k.box("cratewood", x0 + 0.3, TRENCH_Y, z0 + 0.4, x0 + 1.5, TRENCH_Y + 0.9, z1 - 0.4)   # (a bench)
        k.light((x0 + x1) / 2, -0.6, (z0 + z1) / 2, 7, 0.8)
        k.box("lightblock", (x0 + x1) / 2 - 0.2, -0.25, (z0 + z1) / 2 - 0.2, (x0 + x1) / 2 + 0.2, 0, (z0 + z1) / 2 + 0.2)
    # sandbag parapets on the east lip (1.2 m: crouch cover, 1.8 m: standing cover; gaps at every bay and bend)
    for i, (z0, z1, (x0, x1)) in enumerate(LEGS):
        bz = [b for b in bays() if b[0] == "e" and z0 <= b[2] < z1][0]
        for (a, b) in ((z0 + 2, bz[2] - 1), (bz[4] + 1, z1 - 2)):
            if b - a > 2: k.box("sandbagwall", x1 + 0.2, 0, a, x1 + 1.2, 1.2 if (i + (a > z0 + 3)) % 2 else 1.8, b)
    # MG nests: sandbag horseshoes on the west lip, open to the west
    for (x, z) in ((58, -46), (58, 24), (58, -8.5), (57, 56)):
        k.box("sandbagwall", x + 1.4, 0, z - 2, x + 2.2, 1.2, z + 2)
        k.box("sandbagwall", x - 1.6, 0, z - 2, x + 1.4, 1.2, z - 1.2); k.box("sandbagwall", x - 1.6, 0, z + 1.2, x + 1.4, 1.2, z + 2)
        k.box("gunmetal", x + 0.6, 1.0, z - 0.12, x + 2.8, 1.25, z + 0.12)
    # the wire belt in front of the trench (x 86) and a second line in the crater field (x 138): mesh coils that stop
    # players, not shots, 6 m gaps; spike strips under the worst tangles
    def wire(x, z0, z1):
        k.box("chainlinkmesh", x - 0.25, 0, z0, x + 0.25, 1.0, z1)
        z = z0
        while z <= z1 + 0.01:
            k.box("charredtimber", x - 0.08, 0, z - 0.08, x + 0.08, 1.3, z + 0.08); z += 4.5
    for (z0, z1) in ((-78, -52), (-46, -24), (-18, -4.5), (4.5, 18), (24, 46), (52, 78)): wire(86, z0, z1)
    for (z0, z1) in ((-50, -40), (-24, -10), (40, 60)): wire(138, z0, z1)
    for (z0, z1) in ((-46, -36), (30, 42)): k.box("spikefloor", 86.6, 0, z0, 88.6, 0.05, z1)

    # ---- the village (C): ruined houses round the crossroads, the church and its tower
    W = "burntoutruinwall"
    # house 1 (north-east, two floors: stairs up the west wall to a floor over its north half)
    ruin(k, W, 0, 10, 12, 20, 6.4, [("n", 6, 3, 0, DOOR_H), ("e", 13, 2, 1.1, 2.4), ("s", 4, 2, 4.2, 5.4), ("s", 9, 2.5, 1.1, 2.4),
                                    ("e", 17, 2, 4.2, 5.4), ("w", 17, 2, 4.2, 5.4), ("n", 10, 3, 3.8, 6.4)])
    k.stairs("charredtimber", 0.4, 12, 2.0, 15.6, 3.3, "z", 1)
    k.box("charredtimber", 0.4, 3.0, 15.6, 11.6, 3.3, 19.6)
    # house 2 (north-east, one floor, its roof fallen in)
    ruin(k, W, 16, 12, 28, 22, 4.6, [("n", 22, 3, 0, DOOR_H), ("w", 17, 2.5, 1.1, 2.4), ("s", 20, 4, 1.2, 4.6), ("e", 15, 3, 0, DOOR_H)])
    k.box("collapsedbrick", 18, 0, 17, 22, 0.5, 21.5); k.box("collapsedbrick", 18.8, 0.5, 18.5, 21, 1.0, 21.5)
    # house 3 (north-east, back row)
    ruin(k, "bulletpockedwall", 2, 28, 14, 38, 5.2, [("n", 8, 3, 0, DOOR_H), ("e", 33, 2.5, 1.1, 2.4), ("w", 31, 3, 1.2, 5.2), ("s", 6, 2, 1.1, 2.4)])
    # house 4 and 5 (south-east), the barn
    ruin(k, W, 0, -22, 14, -10, 5.0, [("s", 7, 3, 0, DOOR_H), ("e", -18, 3, 0, DOOR_H), ("n", 4, 2.5, 1.1, 2.4), ("w", -16, 4, 1.2, 5.0)])
    ruin(k, W, 18, -20, 28, -10, 4.4, [("s", 22, 3, 0, DOOR_H), ("w", -15, 2.5, 1.1, 2.4), ("n", 24, 3, 1.2, 4.4)])
    ruin(k, "charredtimber", 4, -42, 20, -30, 5.5, [("n", 12, 5, 0, 4.0), ("s", 12, 5, 0, 4.0), ("e", -36, 2.5, 1.1, 2.4)], t=0.3)
    k.box("charredtimber", 4, 5.0, -42, 9, 5.3, -30)                    # (what's left of its loft)
    # south-west: house 6 (two floors: stairs up its east wall), house 7
    ruin(k, W, -30, -24, -18, -12, 6.4, [("s", -24, 3, 0, DOOR_H), ("w", -18, 2.5, 1.1, 2.4), ("n", -26, 2, 4.2, 5.4), ("n", -21, 2.5, 1.1, 2.4),
                                        ("e", -21, 2, 4.2, 5.4), ("w", -21, 2, 4.2, 5.4), ("s", -20, 3, 3.8, 6.4)])
    k.stairs("charredtimber", -20.0, -22.0, -18.4, -18.4, 3.3, "z", -1)
    k.box("charredtimber", -29.6, 3.0, -23.6, -18.4, 3.3, -22.0)
    k.box("charredtimber", -29.6, 3.0, -22.0, -20.0, 3.3, -19.4)
    ruin(k, "bulletpockedwall", -46, -38, -34, -28, 4.8, [("s", -40, 3, 0, DOOR_H), ("e", -33, 2.5, 1.1, 2.4), ("n", -38, 3, 1.2, 4.8)])
    # north-west: house 8
    ruin(k, "collapsedbrick", -44, 30, -34, 40, 5.0, [("n", -39, 3, 0, DOOR_H), ("e", 35, 2.5, 1.1, 2.4), ("s", -41, 3, 1.2, 5.0)])
    # what's left of the roofs: a sagging sheet over half of house 3, beams over house 8, a scorched floor in house 4,
    # a rubble ramp against house 7's back wall, a charred upper storey stub on house 5
    k.box("corrugated", 1.7, 5.2, 27.7, 9, 5.45, 38.3)
    for z in (32, 36):
        k.box("charredtimber", -44, 4.8, z - 0.15, -34, 5.1, z + 0.15)
    k.box("scorchedtiles", 0.4, 0, -21.6, 13.6, 0.02, -10.4)
    k.wedge("rubblepile", 3, -45.6, 0, -37.6, -40, 1.6, -34)
    k.box("charredtimber", 18, 4.4, -20, 28, 6.2, -19.7); k.box("collapsedbrick", 18, 4.4, -20, 18.4, 5.6, -14)
    # the church: the nave (roof gone, beams left), the tower at its east end over the square
    ruin(k, "rubblewall", -50, 11, -28, 21, 8, [("w", 16, 4, 0, 4.2), ("n", -38.5, 3, 0, 3.6), ("s", -41, 6, 1.0, 8),
                                                ("n", -45, 1.4, 3, 6.2), ("n", -32, 1.4, 3, 6.2), ("s", -33, 1.4, 3, 6.2)], t=0.6)
    for x in (-46, -40, -34):
        k.box("charredtimber", x - 0.2, 7.6, 11, x + 0.2, 8.0, 21)
    k.box("collapsedbrick", -44, 0, 18, -38, 0.5, 20.4); k.box("rubblepile", -42.5, 0.5, 19, -39, 1.0, 20.4)
    k.box("charredtimber", -47, 0, 13, -45, 0.9, 19)                    # (the altar)
    T0, T1, U0, U1 = -28, -20, 12, 20                                    # the tower: outer walls, 0.5 thick
    ruin(k, "rubblewall", T0, U0, T1, U1, 14, [("e", 14.5, 3, 0, DOOR_H), ("n", -24, 1.2, 8, 10), ("s", -24, 1.2, 8, 10)], t=0.5)
    ix0, ix1, iz0, iz1 = T0 + 0.5, T1 - 0.5, U0 + 0.5, U1 - 0.5          # inside: 7 x 7, four flights round it
    k.stairs("rubblewall", ix0, iz0 + 1.6, ix0 + 1.6, iz1, 3.5, "z", 1)
    k.stairs("rubblewall", ix0 + 1.6, iz1 - 1.6, ix1, iz1, 7, "x", 1, base=3.5, solid=False)
    k.stairs("rubblewall", ix1 - 1.6, iz0, ix1, iz1 - 1.6, 10.5, "z", -1, base=7, solid=False)
    k.stairs("rubblewall", ix0, iz0, ix1 - 1.6, iz0 + 1.6, 14, "x", -1, base=10.5, solid=False)
    for (a, b, c, d) in minus((ix0, iz0, ix1, iz1), [(ix0, iz0, ix1 - 1.6, iz0 + 1.6)]): k.box("charredtimber", a, 13.6, b, c, 14, d)
    # the perch: a parapet round all four sides (the same cover toward both teams), each side notched down to 0.5 m
    # in one place, stumps of the spire
    for (a, b, c, d, n0, n1) in ((T0, U0, T0 + 0.5, U1, U0 + 3, U0 + 5), (T1 - 0.5, U0, T1, U1, U1 - 5, U1 - 3)):
        k.box("rubblewall", a, 14, b, c, 15.1, n0); k.box("rubblewall", a, 14, n1, c, 15.1, d); k.box("rubblewall", a, 14, n0, c, 14.5, n1)
    for (a, b, c, d, n0, n1) in ((T0 + 0.5, U0, T1 - 0.5, U0 + 0.5, T0 + 4, T0 + 5.5), (T0 + 0.5, U1 - 0.5, T1 - 0.5, U1, T1 - 5.5, T1 - 4)):
        k.box("rubblewall", a, 14, b, n0, 15.1, d); k.box("rubblewall", n1, 14, b, c, 15.1, d); k.box("rubblewall", n0, 14, b, n1, 14.5, d)
    k.box("rubblewall", T0, 15.1, U1 - 1, T0 + 1, 17.5, U1); k.box("rubblewall", T1 - 1, 15.1, U1 - 1, T1, 16.4, U1)
    k.light(-24, 6, 16, 9, 0.7); k.light(-24, 12.5, 16, 8, 0.6)
    # the square: a broken fountain, a cart, sandbags round the crossroads
    k.disc("limestoneashlar", -10, 0, 2.6, 0, 0.7, n=10); k.disc("limestoneashlar", -10, 0, 0.6, 0.7, 2.2, n=8)
    k.box("cratewood", 2.5, 0, -4.4, 5.5, 1.4, -2.6); k.box("charredtimber", 2.2, 0, -4.8, 2.5, 1.0, -2.2)
    k.box("sandbagwall", -20, 0, 6.5, -16, 1.2, 7.3); k.box("sandbagwall", -4, 0, -7.3, 0, 1.2, -6.5)
    k.box("sandbagwall", -21.2, 0, -10, -20.4, 1.2, -6); k.box("sandbagwall", 0.4, 0, 6, 1.2, 1.2, 10)

    # ---- the river (D): swamp water over a mud bed, banks of steps, the damaged bridge, the sunken barge, the ford
    x0, z0, x1, z1 = RIVER
    k.box("mud", x0, BED - 1, z0, x1, BED, z1)
    k.box("swampwater", x0, BED, z0, x1, SURF, z1)
    for z in (-62, -26, 20, 56):
        k.stairs("precastconcrete", x0, z, x0 + 3.5, z + 3, 0, "x", -1, base=BED)
        k.stairs("precastconcrete", x1 - 3.5, z + 4, x1, z + 7, 0, "x", 1, base=BED)
    # the bridge (z -5..5): the deck flush with the road, a shell hole in its north half, girder trusses either side
    # (rails 1 m: cover), posts and a top chord at 5 m (hooks), cross beams overhead
    for (a, b, c, d) in minus((x0, -5, x1, 5), [(-92, 0.5, -88, 5)]): k.box("steeldeckplate", a, -0.6, b, c, 0, d)
    k.box("rubblepile", -92, BED, 0.5, -88, -0.5, 5)                    # (the deck's rubble under the hole: a dip, not a trap)
    for x in (-94, -86):
        k.box("precastconcrete", x - 1, BED - 1, -4, x + 1, -0.6, 4)
    for (zs, broken) in ((-5.0, False), (4.4, True)):
        spans = [(x0, x1)] if not broken else [(x0, -93), (-87, x1)]
        for (a, b) in spans:
            k.box("bridgegirder", a, 0, zs, b, 1.0, zs + 0.6)
            k.box("bridgegirder", a, 4.6, zs, b, 5.2, zs + 0.6)
        x = x0
        while x <= x1 + 0.01:
            if not (broken and -93 < x < -87): k.box("bridgegirder", x - 0.25, 0, zs, x + 0.25, 5.2, zs + 0.6)
            x += 4
    for x in (-98, -90, -82):
        if x != -90: k.box("bridgegirder", x - 0.25, 5.2, -5.0, x + 0.25, 5.6, 5.0)
    k.box("bridgegirder", -92.6, -1.8, 4.5, -92.0, 0, 5.0)              # (the torn rail, hanging into the water)
    # the sunken barge to the north (z 34..44): its deck steps down to the water at the bow, a wheelhouse aft
    k.box("ruststreaks", x0, BED, 34, -90, -0.5, 44)
    k.box("ruststreaks", -90, BED, 34, -81, -0.9, 44)
    k.box("ruststreaks", -81, BED, 34, x1, -0.45, 44)
    for (a, b, y) in ((x0, -94, -0.5), (-86, x1, -0.9)):
        k.box("heavyscalerust", a, y, 34, b, 0.1, 34.4); k.box("heavyscalerust", a, y, 43.6, b, 0.1, 44)
    ruin(k, "heavyscalerust", -100, 36, -95, 42, 2.6, [("e", 39, 2.4, 0, 2.2)], t=0.25, base=-0.5)
    k.box("heavyscalerust", -100.2, 2.1, 35.8, -94.8, 2.35, 42.2)
    # the rubble ford to the south (z -42..-36): stepping stones 1.5 m apart
    for (a, b) in ((-102, -99), (-97.5, -94.5), (-93, -90), (-88.5, -85.5), (-84, -81), (-79.5, -78)):
        k.box("rubblepile", a, BED, -42 + R.uniform(-0.4, 0.4), b, -0.5, -36 + R.uniform(-0.4, 0.4))
    k.box("bridgegirder", -97.5, -0.9, -38.5, -79, -0.5, -37.9)        # (a fallen girder across it)

    # ---- the HQ bunker (E): x -182..-148, z -13..13; walls 1 m, the roof 5..6 m; the command room in the middle;
    # doors: the front (east, z 6..10), the side (north, x -168..-164), the rear (west, z -10..-6)
    B0, B1, C0, C1 = -182, -148, -13, 13
    CW = "castinplacewalls"
    def outer(side, a, b, ya, yb):   # (the outer 0.3 m can't be hooked: nobody swings round the bunker)
        if side == "w": k.box("nograpple", B0, ya, a, B0 + 0.3, yb, b); k.box(CW, B0 + 0.3, ya, a, B0 + 1, yb, b)
        elif side == "e": k.box(CW, B1 - 1, ya, a, B1 - 0.3, yb, b); k.box("nograpple", B1 - 0.3, ya, a, B1, yb, b)
        elif side == "n": k.box(CW, a, ya, C1 - 1, b, yb, C1 - 0.3); k.box("nograpple", a, ya, C1 - 0.3, b, yb, C1)
        else: k.box("nograpple", a, ya, C0, b, yb, C0 + 0.3); k.box(CW, a, ya, C0 + 0.3, b, yb, C0 + 1)
    doors = {"e": [(6, 10)], "n": [(-168, -164)], "w": [(-10, -6)], "s": []}
    for side in "nswe":
        a, b = (C0 + 1, C1 - 1) if side in "we" else (B0, B1)
        cuts = [a] + [v for d in doors[side] for v in d] + [b]
        for i in range(0, len(cuts) - 1):
            p, q = cuts[i], cuts[i + 1]
            if i % 2 == 0:
                outer(side, p, q, 0, 5)
            else:
                outer(side, p, q, DOOR_H, 5)
    # the roof (a hatch over the stairs), its parapet (gap at the ladder), the AA gun in its sandbag ring
    HATCH = (-177, -12, -168, -9)
    for (a, b, c, d) in minus((B0, C0, B1, C1), [HATCH]): k.box(CW, a, 5, b, c, 6, d)
    for (a, b, c, d) in ((B0, C0, B1, C0 + 0.6), (B0, C1 - 0.6, B1, C1), (B1 - 0.6, C0 + 0.6, B1, C1 - 0.6),
                         (B0, C0 + 0.6, B0 + 0.6, 8.5), (B0, 11.5, B0 + 0.6, C1 - 0.6)):
        k.box("nograpple", a, 6, b, c, 6.9, d)
    k.ladder(B0 - 0.12, 10, 0, 6, "e")   # (rails flush on the wall's outer face, so they show)
    k.box("sandbagwall", -168, 6, -4.5, -160, 7.1, -3.7); k.box("sandbagwall", -168, 6, 3.7, -160, 7.1, 4.5)
    k.box("sandbagwall", -160.8, 6, -3.7, -160, 7.1, -1.2); k.box("sandbagwall", -160.8, 6, 1.2, -160, 7.1, 3.7)
    k.disc("gunmetal", -164, 0, 1.6, 6, 6.8, n=8)
    k.box("gunmetal", -165, 6.8, -1.0, -163, 8.0, 1.0)
    k.box("gunmetal", -163, 7.6, -0.5, -158.5, 7.8, -0.3); k.box("gunmetal", -163, 7.6, 0.3, -158.5, 7.8, 0.5)
    # inside: the command room (x -171..-155, z -6.5..6.5; ways in from the east hall, the west hall and the north
    # corridor), the stairs up to the hatch in the south corridor
    ruin(k, CW, -171, -6.5, -155, 6.5, 5, [("e", 0, 3, 0, DOOR_H), ("w", 0, 3, 0, DOOR_H), ("n", -161, 3, 0, DOOR_H)], t=0.5)
    k.stairs(CW, HATCH[0], HATCH[1], HATCH[2], HATCH[3], 6, "x", -1)
    k.box(CW, -152, 0, -2, -151.4, 3.2, 4.5)                             # (a baffle in the east hall)
    k.box("cratewood", -165, 0, -2.0, -161, 0.95, 2.0)                    # (the map table)
    k.box("cratewood", -178.5, 0, 2, -176.5, 1.4, 5)
    for (x, z) in ((-163, 0), (-176, 0), (-152, 8), (-160, 9.5), (-172, -9.5)):
        k.light(x, 4.2, z, 8, 0.8)
    for (x, z) in ((-163, -4.5), (-163, 4.5)):
        k.box("lightblock", x - 0.6, 4.8, z - 0.2, x + 0.6, 5, z + 0.2)
    # tank traps on the approach (off the road), sandbags at the front door
    for (x, z) in ((-145, -18), (-128, -38), (-142, -34), (-138, 16), (-133, 30), (-143, 40), (-140, -50), (-136, 50)):
        k.box("tanktrapfield", x - 0.8, 0, z - 0.8, x + 0.8, 1.3, z + 0.8)
    k.box("sandbagwall", -146.5, 0, 4.5, -145.7, 1.2, 11.5)
    # its outbuildings: a half-sunk ammunition store to the south, the radio hut and its mast to the north
    ruin(k, CW, -178, -60, -164, -50, 3.0, [("n", -171, 3, 0, 2.6), ("e", -55, 2.5, 1.4, 2.2)], t=0.6)
    k.box(CW, -178.3, 3.0, -60.3, -163.7, 3.6, -49.7)
    ruin(k, "plywoodboardup", -178, 44, -168, 52, 3.2, [("s", -173, 2.6, 0, 2.6), ("e", 48, 2, 1.2, 2.2)], t=0.3)
    k.box("corrugated", -178.3, 3.2, 43.7, -167.7, 3.45, 52.3)
    k.box("galvanizedsteel", -166.2, 0, 52.8, -165.8, 4, 53.2); k.box("nograpple", -166.2, 4, 52.8, -165.8, 18, 53.2)   # (no perch)

    # crater rims: spoil thrown up on two sides of each crater (a 0.5 m step, then a 1 m berm at the lip: crouch cover)
    keep = [ROAD, SQUARE, HQ] + [(p[0] - 1, p[1] - 1, p[2] + 1, p[3] + 1) for p in POINTS.values()] + \
           [(min(xw, xw + sd * 6) - 1, zc - 9, max(xw, xw + sd * 6) + 1, zc + 9) for (xw, sd, zc, _) in SHELTERS.values()]
    holes = [crater_rect(c) for c in CRATERS]
    def free(r, extra=()):
        return not any(inter(r, o) for o in keep + list(extra))
    for c in CRATERS:
        x0, z0, x1, z1 = crater_rect(c)
        for side in R.sample("nswe", 2):
            if side == "e": a, b = (x1, z0, x1 + 1.0, z1), (x1 + 1.0, z0 + 1, x1 + 2.4, z1 - 1)
            elif side == "w": a, b = (x0 - 1.0, z0, x0, z1), (x0 - 2.4, z0 + 1, x0 - 1.0, z1 - 1)
            elif side == "n": a, b = (x0, z0 - 1.0, x1, z0), (x0 + 1, z0 - 2.4, x1 - 1, z0 - 1.0)
            else: a, b = (x0, z1, x1, z1 + 1.0), (x0 + 1, z1 + 1.0, x1 - 1, z1 + 2.4)
            if not free(a, [h for h in holes if h != (x0, z0, x1, z1)]) or not free(b, holes): continue
            k.box("crackedearth", a[0], 0, a[1], a[2], 1.0, a[3])
            k.wedge("dirt", {"e": 1, "w": 0, "n": 2, "s": 3}[side], b[0], 0, b[1], b[2], 1.0, b[3])   # (spoil sloping off)
            if R.random() < 0.6:   # (shell debris on the lip)
                rx, rz = R.uniform(a[0], a[2] - 1.2), R.uniform(a[1], a[3] - 1.2)
                k.box("rubblepile", rx, 1.0, rz, rx + 1.2, 1.3, rz + 1.2)
        if free((x0 - 3.5, z0 - 3.5, x0 - 0.5, z0 - 0.5), holes):   # (a scorch mark by its corner)
            k.box("scorchedtiles", x0 - 3.5, 0, z0 - 3.5, x0 - 0.5, 0.02, z0 - 0.5)

    # ---- cover between the points (purposeful pieces, spaced for momentum: broken walls, field walls, wrecks)
    def stub(x, z, along, n, h, mat="burntoutruinwall"):
        """a broken wall: 0.6 m thick, n m long, its far end knocked down to 60%"""
        if along == "x": k.box(mat, x - n / 2, 0, z - 0.3, x + n * 0.1, h, z + 0.3); k.box(mat, x + n * 0.1, 0, z - 0.3, x + n / 2, h * 0.6, z + 0.3)
        else: k.box(mat, x - 0.3, 0, z - n / 2, x + 0.3, h, z + n * 0.1); k.box(mat, x - 0.3, 0, z + n * 0.1, x + 0.3, h * 0.6, z + n / 2)
    for (x, z, al, n, h) in ((172, -26, "z", 8, 3.4), (156, 50, "x", 10, 3.0), (188, 9, "z", 6, 2.8), (140, -30, "x", 8, 2.8),
                             (121, 40, "z", 9, 3.2), (127, 1, "z", 6, 2.6), (100, -62, "x", 10, 3.0), (98, 64, "x", 8, 2.6),
                             (196, -24, "z", 8, 3.0), (180, 66, "x", 8, 2.8)):
        stub(x, z, al, n, h)
    k.box("sandbagwall", 146, 0, 9.4, 152, 1.2, 10.2); k.box("sandbagwall", 138.4, 0, -6, 139.2, 1.2, -1)
    def wall(x0, z0, x1, z1, h=1.1): k.box("drystonewall", x0, 0, z0, x1, h, z1)
    def truck(x, z, along):
        """a wrecked lorry: the cab 2.6 m, the bed 1.4 m, 7 m long, 2.4 m wide"""
        if along == "x":
            k.box("sootysteel", x - 3.5, 0, z - 1.2, x + 1.5, 1.4, z + 1.2); k.box("dentedsteel", x + 1.5, 0, z - 1.2, x + 3.5, 2.6, z + 1.2)
        else:
            k.box("sootysteel", x - 1.2, 0, z - 3.5, x + 1.2, 1.4, z + 1.5); k.box("dentedsteel", x - 1.2, 0, z + 1.5, x + 1.2, 2.6, z + 3.5)
    # between the trench and the village: a supply dump, field walls, a lorry
    k.box("woodencrate", 43, 0, 20, 47, 2.4, 23); k.box("woodencrate", 47.6, 0, 21, 49.6, 1.2, 23); k.box("woodencrate", 43.5, 2.4, 20.5, 45.5, 3.6, 22.5)
    k.box("woodencrate", 46, 0, -27, 48, 1.2, -25); k.box("woodencrate", 48.5, 0, -28, 51.5, 1.6, -25.5)
    wall(44, -34.4, 56, -33.8)
    for (a, b) in ((30, 38), (44, 56)): wall(a, 33.8, b, 34.4)
    truck(52, -13, "x"); truck(48, 46, "z")
    stub(36, 58, "x", 10, 3.2, "collapsedbrick"); stub(56, -54, "z", 10, 3.0, "collapsedbrick")
    # between the village and the river: a dead orchard, field walls
    for (x, z) in ((-70, 14), (-68, 34), (-62, 40), (-70, 50), (-60, 60), (-64, -22), (-70, -32), (-60, -42), (-68, -52), (-62, -64)):
        k.box("charredtimber", x - 0.3, 0, z - 0.3, x + 0.3, 4.5 + (x * 7 + z) % 3 * 0.6, z + 0.3)
    for (a, b) in ((12, 22), (28, 44), (-22, -12), (-44, -28)): wall(-74.6, a, -74, b)
    # between the river and the bunker: sandbag posts, a lorry, the old forward trench's sandbags
    k.box("sandbagwall", -112, 0, 12, -111.2, 1.2, 19); k.box("sandbagwall", -112, 0, -19, -111.2, 1.2, -12)
    k.box("sandbagwall", -142, 0, 22, -136, 1.2, 22.8); k.box("sandbagwall", -142, 0, -22.8, -136, 1.2, -22)
    truck(-130, -40, "x"); stub(-118, -48, "x", 10, 3.2, "collapsedbrick"); stub(-124, 50, "x", 10, 3.0, "collapsedbrick")
    # the motor pool's burnt sheds either side of the bunker's approach (cover off the road)
    ruin(k, "charredtimber", -142, -66, -126, -56, 4.6, [("n", -134, 5, 0, 3.8), ("s", -134, 5, 0, 3.8), ("w", -61, 3, 1.1, 2.4)], t=0.3)
    ruin(k, "charredtimber", -142, 56, -126, 66, 4.2, [("n", -134, 5, 0, 3.8), ("s", -134, 5, 0, 3.8), ("e", 61, 3, 1.1, 2.4)], t=0.3)
    truck(-134, -61, "x"); k.box("corrugated", -142.2, 4.2, 55.8, -134, 4.45, 66.2)
    # more of the village up and down the street
    ruin(k, W, -4, 46, 8, 56, 4.6, [("n", 2, 3, 0, DOOR_H), ("w", 51, 2.5, 1.1, 2.4), ("e", 49, 3, 1.2, 4.6)])
    ruin(k, W, -28, 48, -18, 58, 5.0, [("e", 52, 3, 0, DOOR_H), ("n", -24, 2.5, 1.1, 2.4), ("s", -21, 3, 1.2, 5.0)])
    ruin(k, W, -4, -58, 8, -48, 4.8, [("s", 2, 3, 0, DOOR_H), ("w", -53, 2.5, 1.1, 2.4), ("n", 4, 4, 1.2, 4.8)])
    ruin(k, W, -28, -56, -18, -46, 4.4, [("e", -52, 3, 0, DOOR_H), ("s", -24, 2.5, 1.1, 2.4), ("w", -50, 3, 1.2, 4.4)])

    # ---- the big pieces that break the strip's long lines (a few, large, purposeful: momentum flows round them)
    def fallen(x0, x1, zs, d=12, h=4.0):
        """a facade fallen inward off the strip's edge: a rubble foot, the slab lying on it (a steep face toward the
        field, a flat top at h), rebar stubs out of its broken end"""
        ze = HZ * zs - zs * 3.5                               # (it leans on the standing facades' line)
        zi = ze - zs * d
        a, b = sorted((ze, zi + zs * 3))
        k.box("collapsedbrick", x0, 0, a, x1, h, b)
        k.wedge("bulletpockedwall", 3 if zs > 0 else 2, x0, 0, min(zi, zi + zs * 3), x1, h, max(zi, zi + zs * 3))
        k.box("rubblepile", x0 - 1.5, 0, min(zi, zi + zs * 6), x0, 0.6, max(zi, zi + zs * 6))
        for x in (x0 + 2, x0 + 4.5, x1 - 3):
            k.box("exposedrebar", x - 0.05, h, ze - zs * 1.5 - 0.05, x + 0.05, h + 1.2, ze - zs * 1.5 + 0.05)
    for (x0, x1, zs) in ((182, 194, -1), (160, 172, 1), (120, 132, 1), (84, 96, -1), (44, 54, 1), (50, 58, -1),
                         (-40, -28, -1), (10, 22, 1), (-124, -112, -1), (-124, -112, 1), (-200, -188, -1), (-196, -184, 1), (106, 118, -1)):
        fallen(x0, x1, zs)
    def ridge(x0, z0, x1, z1, h=2.6):
        """a spoil ridge thrown up by the shelling: earth slopes either side of a flat crest (along its long side)"""
        if x1 - x0 < z1 - z0:
            d = (x1 - x0) / 3
            k.wedge("crackedearth", 0, x0, 0, z0, x0 + d, h, z1); k.box("dirt", x0 + d, 0, z0, x1 - d, h, z1)
            k.wedge("crackedearth", 1, x1 - d, 0, z0, x1, h, z1)
        else:
            d = (z1 - z0) / 3
            k.wedge("crackedearth", 2, x0, 0, z0, x1, h, z0 + d); k.box("dirt", x0, 0, z0 + d, x1, h, z1 - d)
            k.wedge("crackedearth", 3, x0, 0, z1 - d, x1, h, z1)
    for r in ((166, 24, 173, 44), (116, -50, 123, -34), (40, -61, 50, -53), (-116, -42, -109, -26), (-116, 30, -109, 46),
              (79, 52, 85, 68), (78, -68, 84, -52), (186, 46, 193, 62)):
        ridge(*r)
    # ruined walls across the strip (a broken field wall line, a shot-up barn's gable)
    for (x, z0, z1, h) in ((150, -76, -58, 3.6), (40, 36, 52, 3.4), (-66, -72, -52, 3.4), (-66, 44, 64, 3.2)):
        k.box("bulletpockedwall", x - 0.4, 0, z0, x + 0.4, h, z0 + (z1 - z0) * 0.65)
        k.box("bulletpockedwall", x - 0.4, 0, z0 + (z1 - z0) * 0.65, x + 0.4, h * 0.55, z1)
        k.box("rubblepile", x + 0.4, 0, z0 + (z1 - z0) * 0.7, x + 1.6, 0.5, z1 - 1)
    # the road and the strip's middle: wrecked lorries and sandbag barricades, each across one half (round them, not
    # over them: the middle line is broken every 20-40 m, the other half stays a clean lane)
    for (x, z) in ((112, -2.5), (44, 1.5), (-66, -2.5), (-118, 2.5)):
        truck(x, z, "z")
    for (x, z0, z1) in ((96, 2, 9), (14, -6, 0), (-40, 0, 6), (-140, -6, 0), (176, -4, 3)):
        k.box("sandbagwall", x - 0.5, 0, z0, x + 0.5, 1.8, z1); k.box("sandbagwall", x - 1.0, 0, z0 + 0.5, x - 0.5, 1.2, z1 - 0.5)
    # smoke: drifting from the burning wrecks, and banks lying in the hollows on the long lines (sight only)
    for (x0, z0, x1, z1, y0, y1) in ((158, -7, 164, 1, 2.0, 9.0), (106, 26, 112, 34, 2.0, 9.0), (-122, 18, -116, 26, 1.5, 7.0),
                                     (126, 40, 134, 48, 0, 4.5), (16, -48, 24, -40, 0, 4.5), (-136, 40, -128, 48, 0, 4.5),
                                     (96, -74, 104, -66, 0, 4.5), (-60, 66, -52, 74, 0, 4.5)):
        if y0 > 0:   # a plume off a wreck: narrow at the bottom, spreading as it rises and drifts
            cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
            k.box("smokewall", cx - 1.5, y0, cz - 1.5, cx + 1.5, y0 + 2.5, cz + 1.5)
            k.box("smokewall", cx - 2.5, y0 + 2.5, cz - 2.0, cx + 3.0, y1 - 2.0, cz + 2.5)
            k.box("smokewall", cx - 2.0, y1 - 2.0, cz - 3.5, cx + 4.5, y1 + 1.0, cz + 3.0)
            k.light(cx, 2.0, cz, 12, 0.7)                                    # (the wreck's fire glows in its smoke)
        else:        # a bank lying low in a hollow: wide, 2.4 m deep, a lower skirt round it
            k.box("smokewall", x0, 0, z0, x1, 2.4, z1)
            k.box("smokewall", x0 + 1.5, 2.4, z0 + 1.0, x1 - 2.0, 3.4, z1 - 1.5)

    # ---- tanks: one burning in the crater field, one in front of the wire, one dead outside the bunker
    tank(k, 156, -3, "z"); tank(k, 104, 32, "z"); tank(k, -116, 24, "x", burning=False)
    # point A's cover: the burnt tank on its east edge, a broken sandbag ring (1.2 and 1.8 m) round its middle
    for (a, b, c, d, h) in ((145, -3.2, 149.5, -2.4, 1.8), (142.6, -0.5, 143.4, 4.0, 1.2), (146.5, 5.6, 151, 6.4, 1.8), (151.6, 0.5, 152.4, 3.5, 1.2)):
        k.box("sandbagwall", a, 0, b, c, h, d)

    # ---- the strip's edges: tall ruined facades along the long sides (clip above them), walls at the ends
    for zs in (-1, 1):
        zw = HZ * zs
        k.box("bulletpockedwall", -HX - 1, G0, min(zw, zw + zs), HX + 1, 10, max(zw, zw + zs))
        x = -HX
        while x < HX - 8:
            w = R.choice((14, 18, 22)); h = R.choice((10, 11.5, 13)); d = R.choice((3, 4))
            if x + w > HX: break
            if any(x < b and x + w > a for (a, b) in ((-104, -76), (55, 80))): x += w; continue
            a, b = sorted((zw, zw - zs * d))
            mat = R.choice(("collapsedbrick", "burntoutruinwall", "bulletpockedwall"))
            # piers between window recesses (0.8 m deep) on the two upper floors, the ground floor pushed out a step
            k.box(mat, x, 0, a, x + w, 4.0, b)
            fa, fb = sorted((zw - zs * (d + 0.6), zw - zs * d))
            k.box(mat, x, 0, fa, x + w, 3.4, fb)
            ra, rb = sorted((zw - zs * (d - 0.8), zw))
            xs = [x] + [x + 2 + 4 * i for i in range(int((w - 2) // 4))] + [x + w]
            for (y0, y1, win) in ((4.0, 5.2, False), (5.2, 6.8, True), (6.8, 8.0, False), (8.0, h - 2, True)):
                if y1 <= y0: continue
                if not win: k.box(mat, x, y0, a, x + w, y1, b); continue
                k.box(mat, x, y0, ra, x + w, y1, rb)                             # (the wall behind the windows)
                for i in range(len(xs) - 1):
                    p0, p1 = xs[i], xs[i + 1]
                    k.box(mat, p0, y0, a, min(p1, p0 + 1.4) if i else p0 + 2, y1, b)
            k.box(mat, x, h - 2, a, x + w * 0.6, h, b)                           # (a broken top)
            k.box("exposedrebar", x + w * 0.6, h - 2, (a + b) / 2 - 0.05, x + w * 0.6 + 0.1, h - 0.8, (a + b) / 2 + 0.05)
            x += w + R.choice((12, 16, 22))
    for xs in (-1, 1):
        xw = HX * xs
        k.box("bulletpockedwall", min(xw, xw + xs), G0, -HZ, max(xw, xw + xs), 10, HZ)
    m.clip_walls(-HX, -HZ, HX, HZ, 10)
    for zs in (-1, 1):   # (the facades' tops are no perches: clip over them from 8 m up)
        k.box("clip", -HX, 8, min(HZ * zs, HZ * zs - zs * 4.6), HX, 40, max(HZ * zs, HZ * zs - zs * 4.6))
    k.box("clip", -HX, 40, -HZ, HX, 41, HZ)

    # ---- the points (attack order A to E): a flush glow border, the zone 4 m tall
    for key in "ABCDE":
        x0, z0, x1, z1, y = POINTS[key]
        if key not in "BD":
            for (a, b, c, d) in ((x0, z0, x1, z0 + 0.3), (x0, z1 - 0.3, x1, z1), (x0, z0 + 0.3, x0 + 0.3, z1 - 0.3), (x1 - 0.3, z0 + 0.3, x1, z1 - 0.3)):
                k.box("glow", a, y, b, c, y + 0.04, d)
        top = 0 if key == "B" else y + 4.5   # (B: only the trench counts, not the open ground over it)
        m.raw(f"point {num(x0)} {num(y)} {num(z0)} {num(x1)} {num(top)} {num(z1)}")
    for (a, b) in ((-8, -6), (6, 8)):                                     # (B: glow posts on the trench's lips)
        for x in (62, 78): k.box("glow", x - 0.2, 0, a, x + 0.2, 1.2, b)
    for x in (-100, -80): k.box("glow", x - 0.1, 0, -4.9, x + 0.1, 0.04, 4.9)

    # ---- staged spawns: six a side a stage, behind a shelter, facing their point
    for key, (xw, side, zc, style) in SHELTERS.items():
        shelter(k, xw, side, zc, style)
        zo = 1 if zc > 0 else -1
        for i in range(2):
            for j in (-2, 0, 2):
                x = xw + side * (2.6 + 1.6 * i)
                k.spawn(key, x, 0, zc + zo * 4 + j, 0 if zo > 0 else 180)   # (facing the open side: yaw 0 faces -z)
    for (x, z) in ((182, 62), (128, -66), (82, 44), (40, -50), (-24, 54), (-62, -52), (-120, -58), (-196, 44)):
        k.spawn("any", x, 0, z, yaw_to(x, z, x * 0.6, 0))

    # ---- bomb: two sites in the trench line, each round a dugout (south: the leg at z -48..-32, north: z 32..48);
    # the attackers (bomb-a) come from the crater field's east end, the defenders (bomb-b) from the village's west
    # edge, both on the road (their own spawn set: the staged points spawns stay as they are)
    for (z0, z1) in BOMB_SITES:
        m.raw(f"site 58 {num(TRENCH_Y)} {num(z0)} 80 3 {num(z1)}")
        for x in (58, 79.6):   # (glow posts at its corners, on the ground beside the trench)
            for z in (z0, z1 - 0.4): k.box("glow", x, 0, z, x + 0.4, 1.6, z + 0.4)
    for (x, z) in BOMB_A:
        k.spawn("bomb-a", x, 0, z, 90)
    for (x, z) in BOMB_B:
        k.spawn("bomb-b", x, 0, z, 270)

    decorate(k, R)
    m.raw("setting active warzone")
    m.save("war", dress=False)


def decorate(k, R):
    """sparing: fires at the wrecks and in the ruins, flares over the trench, rubble against walls, ash round the
    tanks; nothing in the lanes (momentum is huge)"""
    # flares hanging over the trench (glow), lamps for them
    for z in (-56, -24, 8, 40):
        k.box("glow", 69.6, 17, z - 0.3, 70.4, 17.6, z + 0.3); k.light(70, 15, z, 18, 0.5)
    # fires in the ruins
    for (x, z) in ((25, -14), (-36, 33), (10, 35), (-42, -33)):
        k.box("emberbed", x - 1, 0, z - 1, x + 1, 0.15, z + 1); k.box("lightblock", x - 0.3, 0.15, z - 0.3, x + 0.3, 0.6, z + 0.3)
        k.light(x, 1.6, z, 9, 0.9)
    # rubble against the houses' walls and the strip's edges
    for (x0, z0, x1, z1) in ((12.2, 11, 13.6, 14), (-30.2, -15, -31.4, -12), (-34.2, 36, -32.8, 39.6), (28.2, -18, 29.6, -14),
                             (-50.2, 11.5, -51.6, 13.5), (14.2, 29, 15.6, 31)):
        a, c = sorted((x0, x1)); k.box("rubblepile", a, 0, z0, c, 0.5, z1)
    for x in range(-190, 200, 47):
        if -106 < x < -76: continue
        for zs in (-1, 1):
            z = zs * (HZ - 5.6)
            k.box("rubblepile", x, 0, min(z, z + zs * 1.2), x + 4, 0.45, max(z, z + zs * 1.2))
    # ash round the burning tanks
    for (x, z) in ((156, -3), (104, 32)):
        k.box("scorchedtiles", x - 4, 0, z - 4, x + 4, 0.03, z + 4)
    # shell-shocked trees: stumps on the field's edges
    for (x, z) in ((196, -70), (172, -66), (120, 72), (40, 70), (-60, 72), (-130, -70), (-70, -66), (30, -72)):
        k.box("charredtimber", x - 0.35, 0, z - 0.35, x + 0.35, R.uniform(2.5, 6), z + 0.35)
    # crates and fuel drums against the bunker and the barge's wheelhouse
    for (x, z) in ((-184.5, -2), (-184.5, 2.4), (-147, -10.5)):
        k.disc("rustedsheet", x, z, 0.5, 0, 1.0, n=8)
