"""LIGHTHOUSE (1.19.6): team vs (and power, free for all) on a rocky headland at night in a storm, 120 x 100 m.
North is -z. The land rises from the sea (south) to a granite plateau 12 m up across the north; a spur of it runs
south into the middle with the lighthouse on it, and two coves sit either side of the spur's tip.
  Team A: the keepers' cottages and their walled yard on the west plateau (12 m).
  Team B: the stone boathouse on the east low ground (1 m), its slipway running into the east cove.
  The lighthouse (on the spur, 12 m): a 12-sided whitewashed tower, a square stair inside (11 flights of four 0.5 m
  steps, 1.5 m wide, round an open shaft you can drop down), doors west and east at its foot, the lamp gallery at
  34.5 m behind a 1.1 m rail, the lamp room in the middle.
  B's covered climb: the cave stair, a tunnel from B's yard into the spur and two flights up inside the rock, coming
  out of a sunken stairwell on the spur top east of the tower. B's open climb: the east cliff path (a ramp up the
  spur's east face). A's way down: the west cliff path (a ramp down the west edge to the beach).
  The wreck: a ship on its side across the south beach, 40 m long; its hull is a 30 degree ramp up to 9 m, under it
  a lane along the ship, its deck (now vertical, facing the sea) shoot-through planks above head height. Two masts
  lean out over the open sea (grapple anchors).
  The sea cave: a 6 m wide, 4 m tall tunnel under the spur's tip joining the two coves.
  The coves are floaty deep ocean, 0.4 m deep (not lethal); the open sea past the beach has no bottom: you sink
  into it and die at the void line (-6 m).
Two groynes cross the beach (crouch cover); granite outcrops, drystone field walls and the oil store break up the
plateau."""
import math
from .prefabs import M
from .m5kit import Kit, fill, slab, walls, beam, ramp_slab, rock, yaw_to

HX, HZ = 60, 50
TOP, LOW = 12.0, 1.0          # the plateau and the low ground (beach, B's yard, cave floor)
COVE_F, COVE_S = 0.5, 0.9     # the coves' floor and water surface
LX, LZ = 6.0, -26.0           # the lighthouse's centre
SQ, FW = 3.5, 1.5             # the stairwell's half size and its flights' width
GAL = 34.5                    # the lamp gallery (9 flights of 2.5 m up from the plateau)


def build():
    m = M("Lighthouse", "vs", sun=(0.25, 0.9, 0.35))
    m.also = ["power"]
    k = Kit(m)

    # ---- the ground: the plateau and the spur (granite with the sea cave and the cave stair cut in it), the low
    # ground (B's yard, the beach, the coves), the open sea
    air = [(-20, LOW, -3, 20, 5.0, 3)]                                    # the sea cave
    air += [(17, LOW, -44, 20, 5.0, -40)]                                  # the cave stair's mouth (B's yard)
    air += [(7, LOW, -44, 17, 9.5, -40)]                                   # flight 1 (rising west)
    air += [(3, 6.5, -44, 7, 10.0, -36)]                                   # the landing
    air += [(7, 6.5, -40, 10, 11.5, -36), (10, 6.5, -40, 17, 13, -36)]     # flight 2 (rising east; open to the sky)
    fill(k, "graniterock", (-60, -1, -50, 20, TOP - 0.5, -6), air)
    fill(k, "graniterock", (-20, -1, -6, 20, TOP - 0.5, 8), air)
    k.stairs("wetcaverock", 7, -44, 17, -40, 6.5, "x", -1, base=LOW, floor=LOW)
    k.box("wetcaverock", 3, LOW, -44, 7, 6.5, -36)
    k.stairs("wetcaverock", 7, -40, 17, -36, TOP, "x", 1, base=6.5, floor=6.5)
    k.box("wetcaverock", -20, LOW - 0.5, -3, 20, LOW, 3)                   # (the sea cave's floor over the rock)
    # the plateau's turf (A's west, the spur), the stairwell's hole open
    slab(k, "dewygrass", -60, -50, 20, -6, TOP - 0.5, TOP, holes=[(10, -40, 17, -36)], tile=24)
    slab(k, "scree", -20, -6, 20, 8, TOP - 0.5, TOP, tile=24)
    # B's yard, the slipway, the beach and the coves' floors
    slab(k, "gravel", 20, -50, 60, -6, -1, LOW, holes=[(38, -28, 50, -6)], tile=20)
    k.box("rainwetpaving", 38, -1, -28, 50, LOW, -6)
    slab(k, "shellbeach", -60, 12, 60, 36, -1, LOW, tile=30)
    k.box("shellbeach", -20, -1, 8, 20, LOW, 12)
    k.box("blacksand", -60, -1, -6, -53, LOW, 12)                           # (under the west cliff path)
    k.box("blacksand", 53, -1, -6, 60, LOW, 12)                             # the east shore (B's way to the beach)
    for (x0, x1) in ((-53, -20), (20, 53)):
        k.box("blacksand", x0, -1, -6, x1, COVE_F, 12)
        k.box("deepocean", x0, COVE_F, -6, x1, COVE_S, 12)
    k.box("deepocean", -60, -10, 36, 60, COVE_S, 50)                        # the open sea: no bottom
    m.raw("void -6")

    # ---- the cliff paths: A's down the west edge to the beach, B's up the spur's east face
    k.ramp("graniterock", -60, -6, -53, 16, LOW, TOP, "-z")
    k.ramp("graniterock", 20, -30, 25, -8, LOW, TOP, "+z")
    k.box("graniterock", 20, -1, -8, 25, TOP, -6)                            # the path's top step onto the spur
    # a drystone field wall along the spur's east edge (A can't stand over B's yard and see in) with a gap at the path
    k.box("drystonewall", 18.8, TOP, -50, 20, TOP + 1.4, -12)

    # ---- the outer walls (rock to the north and the sides, clip over the sea), clip lid 85 m (gallery cap 38 + 45)
    k.box("graniterock", -62, -10, -52, 62, 18, -50)
    k.box("graniterock", -62, -10, -50, -60, 16, 50)
    k.box("graniterock", 60, -10, -50, 62, 16, 50)
    for (x0, z0, x1, z1, y) in ((-62, -52, 62, -50, 18), (-62, -50, -60, 50, 16), (60, -50, 62, 50, 16)):
        k.box("nograpple", x0, y, z0, x1, y + 0.3, z1)
    k.box("clip", -62, -10, 50, 62, 85, 52)
    for (x0, z0, x1, z1, y) in ((-62, -52, 62, -50, 18.3), (-62, -50, -60, 50, 16.3), (60, -50, 62, 50, 16.3)):
        k.box("clip", x0, y, z0, x1, 85, z1)
    k.box("clip", -60, 85, -50, 60, 86, 50)

    plateau_cover(k)
    lighthouse(k)
    cottages(k)
    boathouse(k)
    wreck(k)

    decorate(k)

    # ---- spawns: A in the cottages' yard, B on the boathouse floor, eight for free for all round the map
    for (x, z) in ((-56, -44), (-52, -44), (-56, -40), (-52, -40)):
        k.spawn("a", x, TOP, z, yaw_to(x, z, LX, LZ))
    for (x, z) in ((48, -42), (52, -42), (48, -37), (52, -37)):
        k.spawn("b", x, LOW, z, yaw_to(x, z, 30, -20))
    for (x, y, z) in ((-39, TOP, -32), (-30, TOP, -14), (45, LOW, -18), (56, LOW, -2), (-10, LOW, 24), (14, LOW, 32),
                      (0, LOW, 0), (LX - 9, TOP, LZ + 4)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))

    m.raw("setting a rocky headland with a lighthouse, at night in a storm")
    m.raw("palette base 0.86 0.86 0.83 accent 0.62 0.16 0.14 metal 0.22 0.23 0.25 glow 1 0.9 0.65 sky 0.09 0.11 0.16")
    m.save("lighthouse", dress=False)


def plateau_cover(k):
    """the plateau's cover (A's approach is open ground otherwise): drystone field walls, granite outcrops, the oil
    store west of the tower; the groynes across the beach"""
    y = TOP
    for (x0, z0, x1, z1) in ((-31, -50, -30.2, -40), (-24, -14.8, -10, -14), (-20, -34, -19.2, -22), (-6, -48, 4, -47.2)):
        k.box("drystonewall", x0, y, z0, x1, y + 1.3, z1)
    for (i, (cx, cz, r, h)) in enumerate(((-12, -40, 3.2, 4.0), (-37, -11, 2.6, 3.0), (14, -13, 2.4, 3.2), (-26, -28, 2.0, 2.6))):
        rock(k, "graniterock", cx, cz, r, y - 0.5, h + 0.5, seed=i)
    walls(k, "riverstonewall", -12, -33, -5, -28, y, y + 3.4, [("e", -30.5, 2.4, 2.6)])
    k.box("slatecladding", -12.3, y + 3.4, -33.3, -4.7, y + 3.8, -27.7)
    # two groynes across the beach (1 m: crouch cover, jumped at a run), cutting the beach's long east-west line
    for x in (-36, 34):
        k.box("rottingwood", x - 0.4, LOW, 13, x + 0.4, LOW + 1.0, 35)
        for z in range(14, 35, 4):
            k.box("rottingwood", x - 0.25, LOW, z - 0.25, x + 0.25, LOW + 1.5, z + 0.25)


def lighthouse(k):
    """the tower on the spur: shell, doors, the square stair round the shaft, the gallery, the lamp room, the cap"""
    cx, cz = LX, LZ
    # the shell: 12 sides (a flat facing each way: segment 0 +x, 3 +z, 6 -x, 9 -z), doors west and east at its foot,
    # east and south out onto the gallery
    k.ring("whitewashedbrick", cx, cz, 5.2, 6.2, TOP, TOP + 3.2, n=12, a0=-15, skip=(0, 6))
    k.ring("whitewashedbrick", cx, cz, 5.2, 6.2, TOP + 3.2, TOP + 4, n=12, a0=-15)
    k.ring("paintedredbrick", cx, cz, 5.2, 6.2, TOP + 4, TOP + 5, n=12, a0=-15)
    k.ring("whitewashedbrick", cx, cz, 5.2, 6.2, TOP + 5, GAL - 2, n=12, a0=-15)
    k.ring("paintedredbrick", cx, cz, 5.2, 6.2, GAL - 2, GAL, n=12, a0=-15)
    k.ring("whitewashedbrick", cx, cz, 5.2, 6.2, GAL, GAL + 3.4, n=12, a0=-15, skip=(0, 3))
    k.ring("whitewashedbrick", cx, cz, 5.2, 6.2, GAL + 2.6, GAL + 3.4, n=12, a0=-15, only=(0, 3))
    # the stairwell's walls inside the shell (a square, 7 m across), the doorways through them
    H = GAL + 3.4
    fill(k, "whitewashedbrick", (cx + SQ, TOP, cz - 3.3, cx + 5.0, H, cz + 3.3),
         [(cx + SQ, TOP, cz - 0.4, cx + 5.0, TOP + 3.2, cz + 1.6), (cx + SQ, GAL, cz - 1.6, cx + 5.0, GAL + 2.6, cz + 1.6)])
    fill(k, "whitewashedbrick", (cx - 5.0, TOP, cz - 3.3, cx - SQ, H, cz + 3.3), [(cx - 5.0, TOP, cz - 1.6, cx - SQ, TOP + 3.2, cz + 1.6)])
    fill(k, "whitewashedbrick", (cx - 3.3, TOP, cz + SQ, cx + 3.3, H, cz + 5.0), [(cx - 1.6, GAL, cz + SQ, cx + 1.6, GAL + 2.6, cz + 5.0)])
    k.box("whitewashedbrick", cx - 3.3, TOP, cz - 5.0, cx + 3.3, H, cz - SQ)
    k.box("wideplanks", cx - SQ, TOP - 0.1, cz - SQ, cx + SQ, TOP, cz + SQ)
    # the stair: 9 flights of five 0.5 m steps along the walls (N, E, S, W, ...), a landing in each corner
    I = SQ - FW
    for f in range(9):
        y0 = TOP + 2.5 * f; y1 = y0 + 2.5
        side = f % 4
        if side == 0: k.stairs("castiron", cx - I, cz - SQ, cx + I, cz - I, y1, "x", 1, base=y0, solid=False)
        elif side == 1: k.stairs("castiron", cx + I, cz - I, cx + SQ, cz + I, y1, "z", 1, base=y0, solid=False)
        elif side == 2: k.stairs("castiron", cx - I, cz + I, cx + I, cz + SQ, y1, "x", -1, base=y0, solid=False)
        else: k.stairs("castiron", cx - SQ, cz - I, cx - I, cz + I, y1, "z", -1, base=y0, solid=False)
        corner = [(cx + I, cz - SQ), (cx + I, cz + I), (cx - SQ, cz + I), (cx - SQ, cz - SQ)][side]
        k.box("castiron", corner[0], y1 - 0.5, corner[1], corner[0] + FW, y1, corner[1] + FW)
    # the top floor (34.5 m): the middle over the shaft and strips along the east and south walls, out to the two
    # doors (the north and west strips stay open over the last flights)
    k.box("castiron", cx - I, GAL - 0.5, cz - I, cx + I, GAL, cz + I)
    k.box("castiron", cx + I, GAL - 0.5, cz - SQ, cx + SQ, GAL, cz + SQ)
    k.box("castiron", cx - SQ, GAL - 0.5, cz + I, cx + I, GAL, cz + SQ)
    k.box("castiron", cx + SQ, GAL - 0.5, cz - 1.6, cx + 5.2, GAL, cz + 1.6)    # (the door sills)
    k.box("castiron", cx - 1.6, GAL - 0.5, cz + SQ, cx + 1.6, GAL, cz + 5.2)
    # the gallery: a ring walk 1.6 m wide behind a 1.1 m rail
    k.ring("castiron", cx, cz, 6.0, 7.8, GAL - 0.4, GAL, n=12, a0=-15)
    k.ring("castiron", cx, cz, 7.55, 7.8, GAL, GAL + 1.1, n=12, a0=-15)
    # the lamp room: glass round the lamp on the middle floor, the lens
    k.ring("glass", cx, cz, 1.55, 1.8, GAL, GAL + 3.4, n=8)
    k.disc("glow", cx, cz, 0.9, GAL + 0.6, GAL + 2.4, n=8)
    k.light(cx, GAL + 1.5, cz, 30, 1.0)
    # the cap: a red roof over the shell and a ventilator ball on top (hookable)
    k.disc("paintedredbrick", cx, cz, 6.6, GAL + 3.4, GAL + 3.9, n=12)
    k.disc("paintedredbrick", cx, cz, 4.5, GAL + 3.9, GAL + 4.6, n=12)
    k.disc("castiron", cx, cz, 0.6, GAL + 4.6, GAL + 5.6, n=8)
    # lights on the stair (it's dark in there)
    for y in (TOP + 3, TOP + 12, TOP + 21):
        k.light(cx, y, cz, 9, 0.7)


def cottages(k):
    """A's side: the walled yard (the spawns), two whitewashed cottages east of it with a lane between them"""
    y = TOP
    # the yard: a drystone wall 1.3 m high round x -59..-47, z -48..-30, gates east and south
    for (x0, z0, x1, z1) in ((-59.6, -48.6, -46.4, -48), (-59.6, -30.6, -54, -30), (-50, -30.6, -46.4, -30),
                             (-59.6, -48, -59, -30.6), (-47, -48, -46.4, -42), (-47, -36, -46.4, -30.6)):
        k.box("drystonewall", x0, y, z0, x1, y + 1.3, z1)
    # the cottages: stucco walls 4.5 m, slate roofs, doors front and back, windows with thin shutters
    for (x0, z0, x1, z1) in ((-44, -48, -32, -38), (-44, -28, -32, -18)):
        walls(k, "stucco", x0, z0, x1, z1, y, y + 4.5,
              [("w", (z0 + z1) / 2, 2.4, 3.0), ("e", (z0 + z1) / 2, 2.4, 3.0), ("n", (x0 + x1) / 2, 2.0, 2.4, 1.0), ("s", (x0 + x1) / 2, 2.0, 2.4, 1.0)])
        k.box("wideplanks", x0 + 0.4, y - 0.05, z0 + 0.4, x1 - 0.4, y, z1 - 0.4)
        k.wedge("slatecladding", 2, x0 - 0.4, y + 4.5, z0 - 0.4, x1 + 0.4, y + 7, (z0 + z1) / 2)
        k.wedge("slatecladding", 3, x0 - 0.4, y + 4.5, (z0 + z1) / 2, x1 + 0.4, y + 7, z1 + 0.4)
        for wz in (z0 + 0.2, z1 - 0.2):   # (thin shutters in the windows: a dash breaks them, shots go through)
            k.box("plywoodboardup", (x0 + x1) / 2 - 1.0, y + 1.0, wz - 0.06, (x0 + x1) / 2 + 1.0, y + 2.4, wz + 0.06)
        k.box("lightblock", (x0 + x1) / 2 - 0.3, y + 3.8, (z0 + z1) / 2 - 0.3, (x0 + x1) / 2 + 0.3, y + 4.1, (z0 + z1) / 2 + 0.3)
        k.light((x0 + x1) / 2, y + 3.2, (z0 + z1) / 2, 10, 0.9)


def boathouse(k):
    """B's side: the stone boathouse (spawns on its floor), a loft at 5 m up a stair, the slipway doors south"""
    x0, z0, x1, z1 = 34, -48, 56, -28
    walls(k, "riverstonewall", x0, z0, x1, z1, LOW, LOW + 9,
          [("s", 44, 8, 5.5), ("w", -42, 3.2, 3.6), ("w", -33, 3.2, 3.6), ("e", -38, 3.2, 3.6)])
    k.box("wideplanks", x0 + 0.4, LOW - 0.05, z0 + 0.4, x1 - 0.4, LOW, z1 - 0.4)
    # the loft over the west half, up a stair along the north wall
    k.box("wideplanks", x0 + 0.4, LOW + 4.6, z0 + 0.4, 41, LOW + 5, z1 - 0.4)
    k.stairs("wideplanks", 41, z0 + 0.4, 51, z0 + 2.4, LOW + 5, "x", -1, base=LOW)
    k.box("wideplanks", 41, LOW, z0 + 0.4, 41.4, LOW + 5, z0 + 2.4)
    # roof: a slate pitch over it
    k.wedge("roofshingles", 2, x0 - 0.4, LOW + 9, z0 - 0.4, x1 + 0.4, LOW + 12, (z0 + z1) / 2)
    k.wedge("roofshingles", 3, x0 - 0.4, LOW + 9, (z0 + z1) / 2, x1 + 0.4, LOW + 12, z1 + 0.4)
    k.box("lightblock", 47.7, LOW + 8.4, -38.3, 48.3, LOW + 8.7, -37.7)
    k.light(48, LOW + 7.5, -38, 16, 0.8)
    k.light(37, LOW + 3.5, -38, 8, 0.6)


def wreck(k):
    """the ship on its side across the south beach: the hull a ramp up to 9 m (hollow), the lane under it, the
    deck's ribs and shoot-through planks facing the sea, two masts leaning out over the open sea"""
    x0, x1 = -20, 20
    za, zb = 14.0, 28.0          # the hull from the beach (z 14) up to the deck edge (z 28, 9 m)
    top = LOW + 8
    # the hull: one sloped slab per 8 m along the ship (0.5 m thick), the keel at the bottom
    for i in range(5):
        a, b = x0 + 8 * i, x0 + 8 * (i + 1)
        ramp_slab(k, "rottingwood", a, za, b, zb, LOW, top, "+z", thick=0.5)
    # the deck (vertical, facing the sea): ribs every 5 m, thin planks from 4.4 m up between them (shoot-through)
    for x in range(-20, 21, 5):
        k.box("saltcrustedsteel", x - 0.25, LOW, zb - 0.5, x + 0.25, top, zb)
    for x in range(-20, 20, 5):
        k.box("wideplanks", x + 0.25, LOW + 3.4, zb - 0.3, x + 4.75, top, zb - 0.2)
    # the masts: lean out over the sea at 30 degrees from the deck
    for mx in (-9, 9):
        beam(k, "rottingwood", mx - 0.3, mx + 0.3, (LOW + 5, zb), (LOW + 5 + 18 * math.tan(math.radians(30)), zb + 18), 0.6)


def decorate(k):
    """kept off the lanes: shellfish low on the cove faces, mooring rings (hooks) on the cliffs, the surf line, the
    boat in the boathouse, chimneys, lamps in the caves, the lamp's two fixed beams, boulders in the corners"""
    # mussels and barnacles on the cliff faces round the coves (thin coats, flush to the rock)
    for (x0, z0, x1, z1) in ((-20.06, -6, -20, -3), (-20.06, 3, -20, 8), (20, -6, 20.06, -3), (20, 3, 20.06, 8),
                             (-53, -6.06, -20, -6), (25, -6.06, 38, -6)):
        k.box("musselbed", x0, COVE_F, z0, x1, 2.2, z1)
    k.box("barnacles", -20, LOW, 8, 20, 2.0, 8.06)
    # mooring rings: iron loops on the cliffs every 15 m, 6-8 m up (grapple anchors)
    for (x, z, f) in ((-45, -6, "z"), (-30, -6, "z"), (30, -6, "z"), (-12, 8, "z"), (3, 8, "z"), (14, 8, "z"), (20, -20, "x"), (-20, 0, "x")):
        if f == "z": k.box("castiron", x - 0.3, 6.5, z - 0.25 if z < 0 else z, x + 0.3, 7.3, z + 0.25 if z > 0 else z)
        else: k.box("castiron", x if x > 0 else x - 0.25, 6.5, z - 0.3, x + 0.25 if x > 0 else x, 7.3, z + 0.3)
    # the surf line along the beach's sea edge
    k.box("seafoam", -60, LOW - 0.02, 35.2, 60, LOW + 0.01, 36)
    # boulders in the beach's corners, against the walls
    for (i, (x, z, r)) in enumerate(((-57, 28, 2.4), (-55.5, 33, 1.6), (57, 27, 2.2), (56, 33, 1.5))):
        rock(k, "graniterock", x, z, r, LOW - 0.3, 1.8 + r * 0.4, seed=10 + i)
    # the boat on the slip inside the boathouse (cover by B's spawns), lobster pots in the corners
    k.prism("rottingwood", [(41.5, -29.5), (46.5, -29.5), (46.5, -33.5), (44, -35.5), (41.5, -33.5)], LOW, LOW + 1.4)
    k.box("wideplanks", 41.8, LOW + 1.0, -33.2, 46.2, LOW + 1.4, -29.8)
    for (x, z) in ((35.3, -46.4), (35.3, -29.4), (54.4, -46.4)):
        k.box("cratewood", x - 0.6, LOW, z - 0.6, x + 0.6, LOW + 1.0, z + 0.6)
    # chimneys on the cottages
    for (x, z) in ((-35.5, -43), (-35.5, -23)):
        k.box("whitewashedbrick", x - 0.6, TOP + 5, z - 0.6, x + 0.6, TOP + 8, z + 0.6)
    # lamps in the sea cave and the cave stair
    for (x, y, z) in ((-12, 4.2, 0), (0, 4.2, 0), (12, 4.2, 0), (18.5, 4.2, -42), (8, 8.8, -42), (4, 9.5, -40)):
        k.box("lightblock", x - 0.25, y, z - 0.25, x + 0.25, y + 0.3, z + 0.25)
        k.light(x, y - 0.4, z, 9, 0.7)
    # the lamp's two fixed beams (scanner beam: blocks nothing): south over the beach and the sea, east over B's yard
    k.box("scannerbeam", LX - 0.4, GAL + 1.2, LZ + 8, LX + 0.4, GAL + 2.0, 49)
    k.box("scannerbeam", LX + 8, GAL + 1.2, LZ - 0.4, 59, GAL + 2.0, LZ + 0.4)
    # the wreck: a stem post at the bow and a sternpost, the hull's strakes along the deck edge
    for x in (-20.6, 20.0):
        k.box("rottingwood", x, LOW, 13.6, x + 0.6, LOW + 8.6, 14.4)
    k.box("saltcrustedsteel", -20, LOW + 7.6, 27.9, 20, LOW + 8.2, 28.4)
