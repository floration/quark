"""LIGHTHOUSE (1.19.6): team vs (and power, free for all) on a rocky headland at night in a storm, 120 x 100 m.
North is -z. The land rises from the sea (south) to a granite plateau 12 m up across the north; a spur of it runs
south into the middle with the lighthouse on it, and two coves sit either side of the spur's tip.
  Team A: the keepers' cottages and their walled yard on the west plateau (12 m), the spawns under a slate cart shed
  (out of the lamp gallery's sight, as B's are inside the boathouse).
  Team B: the stone boathouse on the east low ground (1 m), its slipway running into the east cove.
  The lighthouse (on the spur, 12 m): a 12-sided whitewashed tower, a square stair inside (11 flights of four 0.5 m
  steps, 1.5 m wide, round an open shaft you can drop down), doors west and east at its foot, the lamp gallery at
  34.5 m behind a 1.1 m rail, the lamp room in the middle. The shell can't be hooked between 14 and 32 m.
  B's covered climb: the cave stair, a tunnel from B's yard into the spur and two flights up inside the rock, coming
  out of a sunken stairwell on the spur top east of the tower. B's open climb: the east cliff path (a ramp up the
  spur's east face). A's way down: the west cliff path (a ramp down the west edge to the beach).
  The wreck: a ship on its side across the south beach, 40 m long (46 m with her raked bow, x 20-26); its hull is a
  30 degree ramp up to 9 m, under it a lane along the ship cut into bays by two bulkheads with offset doorways, its
  deck (now vertical, facing the sea) shoot-through planks above head height, a square transom stove in at the
  stern. Two masts lean out over the open sea (grapple anchors, not over the water: their outer 6 m won't hold).
  The sea cave: a 6 m wide, 4 m tall tunnel under the spur's tip joining the two coves.
  The coves are floaty deep ocean, 0.4 m deep (not lethal); the open sea past the beach has no bottom: you sink
  into it and die at the void line (-6 m).
Two groynes cross the beach (1 m, each with a 1.8-2.1 m stretch) and two rowing boats lie on their sides: no clear
line runs the beach end to end. Granite outcrops, drystone field walls, the oil store and, on the spur, the
fog-signal house break up the plateau. Night: a dark sky, a low moon from the sea, bare rock and scree with turf only
round the cottages, rain pools, warm lamps at the doors."""
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
    m = M("Lighthouse", "vs", sun=(0.15, 0.3, 0.6))   # (a low moon from the sea: the cliffs' and the cottages' north sides
    # fall into shadow and the lamps carry the scene; the engine has no sun strength, so a high sun lit it like noon)
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
    # the plateau's top: bare wind-scoured rock and scree (a night headland, not a lawn), turf kept only round the
    # cottages and their yard; rain pools and wet rock in a dozen patches (each cut out of the slab, never laid on it)
    pools_top = [("rainwetpaving", -35, -37.5, -31, -31), ("rainwetpaving", -46.2, -41, -44.2, -35),
                 ("wetcaverock", -22, -26, -17, -21), ("wetcaverock", -6, -45, -1, -41), ("wetcaverock", -15, -20, -10, -16),
                 ("rainwetpaving", 13, -22, 18, -17), ("wetcaverock", -43, -9.5, -40.6, -6),
                 ("wetcaverock", 0, -2, 5, 3), ("wetcaverock", 12, 2, 17, 6)]
    ground(k, "dewygrass", (-60, -50, -30, -30), TOP - 0.5, TOP, pools_top, tile=24)     # (the yard, the cottages' gardens)
    ground(k, "dewygrass", (-46, -30, -30, -16), TOP - 0.5, TOP, pools_top, tile=24)
    ground(k, "scree", (-60, -30, -46, -16), TOP - 0.5, TOP, pools_top, tile=24)
    ground(k, "scree", (-60, -16, -30, -6), TOP - 0.5, TOP, pools_top, tile=24)
    ground(k, "graniterock", (-30, -50, -6, -36), TOP - 0.5, TOP, pools_top, tile=24)    # (bare rock round the outcrops)
    ground(k, "scree", (-30, -36, -6, -6), TOP - 0.5, TOP, pools_top, tile=24)
    ground(k, "scree", (-6, -50, 20, -6), TOP - 0.5, TOP, pools_top, holes=[(10, -40, 17, -36)], tile=24)
    ground(k, "scree", (-20, -6, 20, 8), TOP - 0.5, TOP, pools_top, tile=24)
    # B's yard, the slipway, the beach and the coves' floors
    pools_low = [("rainwetpaving", 26, -26, 31, -20), ("rainwetpaving", 52, -24, 57, -18), ("rainwetpaving", 50, -12, 53, -7), ("rainwetpaving", 20, -50, 23, -47),
                 ("wetcaverock", -18, 8, -12, 11), ("wetcaverock", 8, 8.5, 14, 11.5)]
    ground(k, "gravel", (20, -50, 60, -6), -1, LOW, pools_low, holes=[(38, -28, 50, -6)], tile=20)
    k.box("rainwetpaving", 38, -1, -28, 50, LOW, -6)
    slab(k, "shellbeach", -60, 12, 60, 36, -1, LOW, tile=30)
    ground(k, "shellbeach", (-20, 8, 20, 12), -1, LOW, pools_low)
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

    cliffs(k)
    outdoor_detail(k)
    fine_detail(k)
    night_lights(k)
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
    m.raw("palette base 0.5 0.52 0.55 accent 0.62 0.16 0.14 metal 0.22 0.23 0.25 glow 1 0.9 0.65 sky 0.025 0.03 0.06")
    m.save("lighthouse", dress=False)


def ground(k, mat, rect, y0, y1, patches, holes=(), tile=None):
    """a floor slab over rect (x0, z0, x1, z1) with the patches [(mat, x0, z0, x1, z1)] that lie inside it cut out and
    laid in their own material at the same height (no coat over the floor: nothing overlaps, nothing z-fights)"""
    x0, z0, x1, z1 = rect
    mine = [p for p in patches if p[1] >= x0 and p[3] <= x1 and p[2] >= z0 and p[4] <= z1]
    slab(k, mat, x0, z0, x1, z1, y0, y1, holes=list(holes) + [p[1:] for p in mine], tile=tile)
    for (pm, a, b, c, d) in mine: k.box(pm, a, y0, b, c, y1, d)


def rowboat(k, x, z0, z1, bow):
    """a 4-5 m rowing boat on its side (keel toward -x), long axis z0..z1, its bow toward bow (+1: +z), 1.9 m tall"""
    yc, h, lb = LOW + 0.95, 0.95, 1.6
    tip = z1 if bow > 0 else z0
    sg = 1 if bow > 0 else -1
    q = h / lb
    planes = [(0, 1, sg * q, yc + sg * q * tip), (0, -1, sg * q, -yc + sg * q * tip)]   # (the pointed bow, top and foot)
    k.cut("rottingwood", (x - 0.55, LOW - 0.1, z0, x + 0.55, LOW + 2 * h, z1), planes)
    sa, sb = (z0 + 0.5, z1 - 1.4) if bow > 0 else (z0 + 1.4, z1 - 0.5)
    k.box("wideplanks", x + 0.55, LOW + 0.35, sa, x + 0.62, LOW + 2 * h - 0.3, sb)   # (the gunwale's boards, open side)


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
    # two groynes across the beach (1 m, jumped at a run), each with a 6-8 m stretch of piled-up planks 1.8-2.1 m high
    # (a broken top) at the end the other one leaves open: the west one at the cliff foot, the east one at the surf, so
    # no line runs the beach's 112 m from cove mouth to cove mouth over them (the wreck's bulkheads close the lane
    # under the hull)
    for (x, runs) in ((-36, ((12, 14, 1.9), (14, 16, 2.1), (16, 18, 1.8), (18, 35, 1.0))),
                      (34, ((13, 28, 1.0), (28, 30, 2.0), (30, 32, 1.8), (32, 34, 2.1), (34, 36, 1.9)))):
        for (z0, z1, h) in runs:
            k.box("rottingwood", x - 0.4, LOW, z0, x + 0.4, LOW + h, z1)
        for z in range(14, 35, 4):
            if any(z0 < z + 0.25 and z - 0.25 < z1 and h >= 1.5 for (z0, z1, h) in runs): continue
            k.box("rottingwood", x - 0.25, LOW, z - 0.25, x + 0.25, LOW + 1.5, z + 0.25)
    # two rowing boats beached on their sides, one each side of the wreck and staggered (the west one on the sea side
    # of her, the east one on the land side by the bow): standing cover on the open sand that breaks the 66 m lines
    # between the groynes past the deck and over the keel
    rowboat(k, -28.0, 29.0, 34.5, +1)
    rowboat(k, 29.0, 12.4, 17.4, -1)


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
    # the hook can't hold on the shell between 14 and 32 m (an invisible clip skin 6 cm proud of it: the hook hits clip
    # and lets go, as on nograpple, and the whitewash stays white), so the gallery is reached by the stair, or by a
    # grapple onto the red band under the rail, the rail or the cap: not a 1 s pull from anywhere within 45 m
    k.ring("clip", cx, cz, 6.2, 6.26, TOP + 2, 32, n=12, a0=-15)
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
    for (x0, z0, x1, z1) in ((-59.6, -30.6, -54, -30), (-50, -30.6, -46.4, -30),
                             (-59.6, -48, -59, -30.6), (-47, -48, -46.4, -42), (-47, -36, -46.4, -30.6)):
        k.box("drystonewall", x0, y, z0, x1, y + 1.3, z1)
    # the cart shed over the yard's north half (the spawns): a slate lean-to on the north wall (raised to carry it) and
    # three posts. The lamp gallery saw 3 of A's 4 spawns over the 1.3 m wall from 55 m (B's are inside the
    # boathouse); under the roof none is in its sight
    k.box("drystonewall", -59.6, y, -48.6, -46.4, y + 3.9, -48)
    ramp_slab(k, "slatecladding", -59.6, -48, -46.4, -35.2, y + 3.15, y + 3.9, "-z", thick=0.25)
    for px in (-59.2, -53.0, -46.8):
        k.box("rottingwood", px - 0.2, y, -35.95, px + 0.2, y + 3.25, -35.55)
    k.box("rottingwood", -59.6, y + 2.95, -36.0, -46.4, y + 3.2, -35.5)   # (the eaves beam on the posts)
    k.box("lightblock", -53.3, y + 3.2, -42.3, -52.7, y + 3.4, -41.7)
    k.light(-53, y + 2.7, -42, 8, 0.7)
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
    sl = (top - LOW) / (zb - za)
    # the hull: one sloped slab per 8 m along the ship (0.5 m thick), the keel at the bottom
    for i in range(5):
        a, b = x0 + 8 * i, x0 + 8 * (i + 1)
        ramp_slab(k, "rottingwood", a, za, b, zb, LOW, top, "+z", thick=0.5)
    # the hull's plane, n . p = d on its top face (the underside is 0.5 m in)
    L = math.hypot(1, sl)
    hn, hd = (0, 1 / L, -sl / L), (LOW - sl * za) / L
    def under(bx0, bx1, bz0, bz1, by0, by1=top):   # a box below the hull's underside
        k.cut("rottingwood", (bx0, by0, bz0, bx1, by1, bz1), [hn + (hd - 0.5,)])
    # the bow (x 20..26): the hull carried on past the keel's end to a raked stem (the deck edge 6 m further out than
    # the keel), dropping from 9 m to 6.5 m at the tip; hollow like the hull, so the lane comes out under it
    bs = (top - 2.5 - top) / 6.0                    # (the fall toward the tip, per metre of x)
    bn = (-bs, 1, -sl); bl = math.sqrt(sum(v * v for v in bn)); bn = tuple(v / bl for v in bn)
    bd = bn[0] * 20 + bn[1] * LOW + bn[2] * za       # (through the keel's end, x 20 z 14 at y 1)
    sn = (1, 0, -6.0 / 14); sln = math.hypot(1, 6.0 / 14); stem = (sn[0] / sln, 0, sn[2] / sln, (20 - 6.0) / sln)
    k.cut("rottingwood", (20, LOW - 0.6, za, 26, top, zb), [bn + (bd,), tuple(-v for v in bn) + (-bd + 0.5,), stem])
    k.cut("wideplanks", (20.25, LOW + 3.4, zb - 0.3, 25.6, top, zb - 0.2), [bn + (bd,)])   # (the bow's deck planks)
    # the stern: a square transom under the hull from 4.2 m up, its lower part stove in (the lane's west mouth)
    k.cut("rottingwood", (x0 - 0.5, LOW + 3.2, za, x0, top, zb), [hn + (hd,)])
    # the frames inside the hull (its ribs, seen from the lane under it)
    for fx in (-15, -10, 0, 15):
        k.cut("rottingwood", (fx - 0.2, LOW, za, fx + 0.2, top, zb - 0.5), [hn + (hd - 0.5,), tuple(-v for v in hn) + (-(hd - 0.85),)])
    # two bulkheads close the lane under the hull (a 112 m clear line ran under it, cove mouth to cove mouth): solid
    # to the hull, 0.5 m thick, each with a 2 m doorway, the two offset across the lane (z 20.5-22.5 at x -6,
    # z 24.5-26.5 at x 8) so the lane is cover from bay to bay, not a tube
    for (bx, dz0, dz1, dh) in ((-6, 20.5, 22.5, 3.6), (8, 24.5, 26.5, 4.0)):
        under(bx - 0.25, bx + 0.25, za, dz0, LOW)
        under(bx - 0.25, bx + 0.25, dz1, zb - 0.3, LOW)
        under(bx - 0.25, bx + 0.25, dz0, dz1, LOW + dh)
    # the deck (vertical, facing the sea): ribs every 5 m, thin planks from 4.4 m up between them (shoot-through)
    for x in range(-20, 21, 5):
        k.box("saltcrustedsteel", x - 0.25, LOW, zb - 0.5, x + 0.25, top, zb)
    for x in range(-20, 20, 5):
        k.box("wideplanks", x + 0.25, LOW + 3.4, zb - 0.3, x + 4.75, top, zb - 0.2)
    # a broken rail along the deck edge (the hull's crest): posts and runs of top rail with gaps
    for (r0, r1) in ((-19, -13), (-8.5, -1.5), (5.5, 12), (15, 19.5)):
        n = max(1, round((r1 - r0) / 2.2))
        for q in range(n + 1):
            px = r0 + (r1 - r0) * q / n
            k.box("saltcrustedsteel", px - 0.07, top + 0.2, zb + 0.06, px + 0.07, top + 1.05, zb + 0.2)
        k.box("saltcrustedsteel", r0 - 0.07, top + 1.05, zb + 0.04, r1 + 0.07, top + 1.17, zb + 0.22)
    # the masts: lean out over the sea at 30 degrees from the deck; their outer 6 m have a clip skin (the hook lets
    # go there), so a grapple on a mast can't leave you hanging over the bottomless sea
    for mx in (-9, 9):
        a = (LOW + 5, zb); b = (LOW + 5 + 18 * math.tan(math.radians(30)), zb + 18)
        beam(k, "rottingwood", mx - 0.3, mx + 0.3, a, b, 0.6)
        ln = math.hypot(b[0] - a[0], b[1] - a[1]); u = ((b[0] - a[0]) / ln, (b[1] - a[1]) / ln)
        c0 = (b[0] - 6 * u[0], b[1] - 6 * u[1]); c1 = (b[0] + 0.4 * u[0], b[1] + 0.4 * u[1])
        beam(k, "clip", mx - 0.36, mx + 0.36, c0, c1, 0.72)
    # in the sand by her: the fallen main yard along the deck face, the capstan off the bow
    k.tube_x("rottingwood", 10.5, 19.5, LOW + 0.27, zb + 0.75, 0.3)
    k.disc("rottingwood", 23.6, zb + 1.3, 0.55, LOW, LOW + 1.0, n=8)
    k.disc("castiron", 23.6, zb + 1.3, 0.75, LOW + 1.0, LOW + 1.25, n=8)


def decorate(k):
    """kept off the lanes: shellfish low on the cove faces, mooring rings (hooks) on the cliffs, the surf line, the
    boat in the boathouse, chimneys, lamps in the caves, the lamp's two fixed beams, boulders in the corners"""
    # (the mussels, barnacles and bird-limed bands on the cliffs: cliffs())
    # mooring rings: iron loops on the cliffs every 15 m, 6-8 m up (grapple anchors)
    # (cleanup: the one at x 30, z -6 hung in the air 5.5 m over the low east shore, no cliff behind it: dropped)
    for (x, z, f) in ((-45, -6, "z"), (-30, -6, "z"), (-12, 8, "z"), (3, 8, "z"), (14, 8, "z"), (20, -20, "x"), (-20, 0, "x")):
        if f == "z": k.box("castiron", x - 0.3, 6.5, z, x + 0.3, 7.3, z + 0.25)   # (both faces look +z: the plateau's at
            # z -6 over the west cove, the spur's at z 8 over the beach; the ones at z -6 were sunk in the rock)
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
    # (from the rail out, over the head of anyone on the gallery: eye 36.1, head 36.3, beams 36.5-37.1)
    k.box("scannerbeam", LX - 0.3, GAL + 2.0, LZ + 7.8, LX + 0.3, GAL + 2.6, 49)
    k.box("scannerbeam", LX + 7.8, GAL + 2.0, LZ - 0.3, 59, GAL + 2.6, LZ + 0.3)
    # the wreck: the hull's strake along the deck edge
    k.box("saltcrustedsteel", -20, LOW + 7.6, 27.9, 20, LOW + 8.2, 28.4)


# ---- the cliffs: batter buttresses, bands of bird lime, barnacles and mussels, a waterfall and a drain outfall
# (faces: (axis, sign, c): the face is at axis = c and looks toward sign)

def _fbox(k, mat, face, a0, a1, y0, y1, d0, d1):
    ax, sg, c = face
    p0, p1 = sorted((c + sg * d0, c + sg * d1))
    if ax == "z": k.box(mat, a0, y0, p0, a1, y1, p1)
    else: k.box(mat, p0, y0, a0, p1, y1, a1)


def _batter(k, mat, face, a0, a1, y0, y1, o0, o1):
    """a block on the face from y0 to y1 standing o0 out at its foot and o1 at its top (a sloped front)"""
    ax, sg, c = face
    g = (o1 - o0) / (y1 - y0)
    n = (sg, -g, 0) if ax == "x" else (0, -g, sg)
    d = sg * c + o0 - g * y0
    p0, p1 = sorted((c, c + sg * max(o0, o1)))
    box = (p0, y0, a0, p1, y1, a1) if ax == "x" else (a0, y0, p0, a1, y1, p1)
    k.cut(mat, box, [n + (d,)])


def buttress(k, mat, face, a0, a1, y0, y1, out):
    """a stepped rock buttress: a battered foot to 55% of the height, a narrower battered top"""
    ym = y0 + (y1 - y0) * 0.55
    _batter(k, mat, face, a0, a1, y0, ym, out, out * 0.6)
    _batter(k, mat, face, a0 + 0.3, a1 - 0.3, ym, y1, out * 0.45, 0.05)


def band(k, mat, face, a0, a1, y0, y1, gaps=(), t=0.05):
    """a coat flush on the face along a0..a1, left out over the gaps [(a, b)] (buttresses, falls)"""
    pos = a0
    for (g0, g1) in sorted(gaps) + [(a1, a1)]:
        if g0 > pos: _fbox(k, mat, face, pos, min(g0, a1), y0, y1, 0, t)
        pos = max(pos, g1)


def cliffs(k):
    """the coves' and the yards' rock faces were sheer flat granite 11.5 m tall and 14-44 m long: buttresses every
    10-15 m (in the water, or a hand's breadth out where the face edges a lane), bird lime in bands above head
    height, barnacles and mussels at the tide line, a stream falling into the west cove, a drain in B's yard"""
    rockm = "graniterock"
    # the west cove's north face (the plateau's edge over the water), z -6
    F = ("z", 1, -6.0)
    bs = [(-51.5, -49.5), (-35.5, -33.5), (-26, -24)]
    fall = (-42.6, -40.6)
    for (a, b) in bs: buttress(k, rockm, F, a, b, COVE_F, TOP - 0.2, 1.6)
    band(k, "musselbed", F, -53, -20, COVE_F, 1.4, bs)
    band(k, "barnacles", F, -53, -20, 1.4, 2.3, bs + [fall])
    band(k, "seabirdcliff", F, -53, -20, 7.6, 10.6, bs + [fall])
    _fbox(k, "waterfall", F, fall[0], fall[1], COVE_S, TOP, 0, 0.08)      # (the stream off the plateau's rain pool)
    _fbox(k, "musselbed", F, fall[0], fall[1], COVE_F, COVE_S, 0, 0.05)
    # the spur's west and east faces over the coves (the sea cave's mouths, z -3..3, 4 m tall)
    for (F, bz) in ((("x", -1, -20.0), (5.4, 7.4)), (("x", 1, 20.0), (4.6, 6.6))):
        buttress(k, rockm, F, bz[0], bz[1], COVE_F, TOP - 0.2, 1.4)
        band(k, "musselbed", F, -6, 8, COVE_F, 2.2, [(-3, 3), bz])
        band(k, "seabirdcliff", F, -6, 8, 7.6, 10.4, [bz])
    # the spur's south face over the beach strip (z 8): shallow pilasters (0.6 m at the foot), the tide line, bird lime
    F = ("z", 1, 8.0)
    ps = [(-16.6, -15.2), (-4.2, -2.8), (8.8, 10.2)]
    for (a, b) in ps: buttress(k, rockm, F, a, b, LOW, TOP - 0.2, 0.6)
    band(k, "barnacles", F, -20, 20, LOW, 2.0, ps)
    band(k, "seabirdcliff", F, -20, 20, 4.6, 6.2, ps)
    band(k, "mossystone", F, -20, 20, 8.8, 10.8, ps)
    # B's yard's west face (the plateau's edge, x 20, z -50..-30; the cave stair's mouth at z -44..-40) and the face
    # over the east cliff path (the bird lime runs on above the path's slope)
    F = ("x", 1, 20.0)
    buttress(k, rockm, F, -38.2, -36.2, LOW, TOP - 0.2, 1.4)
    band(k, "seabirdcliff", F, -50, -16.8, 7.6, 10.4, [(-38.2, -36.2)])
    band(k, "mossystone", F, -50, -30, 1.0, 2.6, [(-44, -40), (-38.2, -36.2), (-48.95, -48.05)])
    #   the drain outfall in the corner: an iron pipe end, its grate, the rust and wet below it
    k.tube_x("castiron", 20.0, 20.9, 4.0, -48.5, 0.5)
    k.box("stormdraingrate", 20.9, 3.6, -48.85, 20.94, 4.4, -48.15)
    k.box("ruststreaks", 20.0, LOW, -48.95, 20.04, 3.55, -48.05)
    # B's yard's north wall west of the boathouse: fishing nets drying on frames
    for px in (22.0, 27.0, 32.0):
        k.box("rottingwood", px - 0.1, LOW, -49.85, px + 0.1, LOW + 3.4, -49.6)
    k.box("rottingwood", 21.9, LOW + 3.2, -49.85, 32.1, LOW + 3.4, -49.6)
    for (a, b) in ((22.1, 26.9), (27.1, 31.9)):
        k.box("cargonet", a, LOW + 0.6, -49.78, b, LOW + 3.2, -49.72)


def outdoor_detail(k):
    """things people live with, on the edges of the play space: the keepers' vegetable garden, the washing line, the
    flagstaff, the bell and the lobster pots at the oil store, the fog-signal house on the spur, the boathouse's
    davit and barrels"""
    y = TOP
    # the vegetable garden in the north corner past the cottages (a 0.8 m drystone wall, a gate south), its rows
    for (x0, z0, x1, z1) in ((-29, -45, -26.2, -44.5), (-24.6, -45, -21, -44.5), (-29, -50, -28.5, -45), (-21.5, -50, -21, -45)):
        k.box("drystonewall", x0, y, z0, x1, y + 0.8, z1)
    for z in (-49.1, -47.8, -46.5):
        k.box("mosscarpet", -28.0, y, z - 0.3, -22.0, y + 0.3, z + 0.3)
    # the washing line in the alley behind the north cottage, sheets over it (above the hull's shoulders)
    for px in (-43.2, -33.0):
        k.box("castiron", px - 0.06, y, -49.16, px + 0.06, y + 2.6, -49.04)
    k.box("castiron", -43.2, y + 2.47, -49.12, -33.0, y + 2.51, -49.08)
    for (a, b) in ((-41.6, -40.4), (-38.6, -37.2), (-35.6, -34.6)):
        k.box("leantocanvas", a, y + 1.55, -49.13, b, y + 2.47, -49.07)
    # the flagstaff at the oil store's north-east corner, the bell on its south wall, lobster pots stacked on its west
    k.box("castiron", -4.62, y, -33.72, -4.46, y + 9, -33.56)
    k.box("leantocanvas", -4.46, y + 7.5, -33.66, -2.4, y + 8.8, -33.62)
    k.box("castiron", -10.08, y + 3.0, -28, -9.92, y + 3.12, -27.3)       # (the bracket)
    k.disc("bronze", -10, -27.55, 0.28, y + 2.45, y + 3.0, n=8)
    for (x0, z0, yy) in ((-12.85, -32.6, 0), (-12.85, -31.7, 0), (-13.7, -32.6, 0), (-12.85, -32.15, 0.8)):
        k.box("cratewood", x0, y + yy, z0, x0 + 0.8, y + yy + 0.8, z0 + 0.8)
    # the fog-signal house on the spur (the spur top was bare scree, open to both coves and the beach: a small stone
    # house to fight round), doors west and east, a window to the sea, the horn through its south wall
    walls(k, "riverstonewall", -16, 1, -9.5, 6.5, y, y + 3.2, [("w", 3.75, 1.6, 2.4), ("e", 3.75, 1.6, 2.4), ("s", -12.75, 1.6, 2.2, 1.0)])
    k.wedge("slatecladding", 2, -16.3, y + 3.2, 0.7, -9.2, y + 4.6, 3.75)
    k.wedge("slatecladding", 3, -16.3, y + 3.2, 3.75, -9.2, y + 4.6, 6.8)
    k.tube_z("brass", 6.5, 7.3, -12.75, y + 2.7, 0.3)
    k.box("lightblock", -13.05, y + 2.9, 3.45, -12.45, y + 3.1, 4.05)
    k.light(-12.75, y + 2.5, 3.75, 7, 0.7)
    # the boathouse yard: a davit at the slipway's west edge, barrels stacked by the slip doors
    k.box("castiron", 37.0, LOW, -14.2, 37.4, LOW + 6.8, -13.8)
    k.box("castiron", 37.0, LOW + 6.5, -14.12, 41.6, LOW + 6.8, -13.88)
    k.box("castiron", 41.22, LOW + 4.2, -14.03, 41.28, LOW + 6.5, -13.97)
    k.box("castiron", 41.05, LOW + 3.8, -14.2, 41.45, LOW + 4.2, -13.8)
    for (bx, bz, by) in ((51.0, -27.45, 0), (52.0, -27.45, 0), (53.0, -27.45, 0), (51.5, -26.55, 0), (51.5, -27.45, 1.0)):
        k.disc("rottingwood", bx, bz, 0.45, LOW + by, LOW + by + 1.0, n=8)


def night_lights(k):
    """warm lamps over the cottages' doors and the boathouse's: at night these, the windows and the lighthouse are
    what you see by"""
    y = TOP
    for zc in (-43.0, -23.0):
        for (x0, x1, lx) in ((-44.25, -44.0, -44.7), (-32.0, -31.75, -31.3)):
            k.box("lightblock", x0, y + 3.25, zc - 0.2, x1, y + 3.5, zc + 0.2)
            k.light(lx, y + 3.0, zc, 8, 0.8)
    k.box("lightblock", 43.7, LOW + 6.0, -28.0, 44.3, LOW + 6.25, -27.75)
    k.light(44, LOW + 5.4, -27.0, 10, 0.8)
    k.box("lightblock", 33.75, LOW + 3.95, -33.2, 34.0, LOW + 4.2, -32.8)
    k.light(33.3, LOW + 3.4, -33, 8, 0.7)


def fine_detail(k):
    """the small stuff, all of it on walls, roofs and the sand's edges (none in a lane): the tower's corbels, window
    slits and lamp-room bars, the cottages' sills, lintels, quoins, gutters and ridge caps, the boathouse's piers,
    the wreck's hatch coamings and anchor, kelp along the tide line, flowstone in the sea cave"""
    y = TOP
    cx, cz = LX, LZ
    ap = 6.2 * math.cos(math.radians(15))          # (the shell's flats: segment i faces 30 i degrees)
    # the tower: corbels under the gallery, slit windows up the four flats (west, east, north, south), lamp-room bars
    for i in range(12):
        t = math.radians(30 * i + 15)
        a = (cx + 6.2 * math.cos(t), cz + 6.2 * math.sin(t)); b = (cx + 7.5 * math.cos(t), cz + 7.5 * math.sin(t))
        k.bar("castiron", a, b, 0.3, GAL - 1.3, GAL - 0.4)
    for h in (8.0, 14.5, 21.0, 27.0):
        for (ux, uz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if h == 8.0 and uz == 0: continue      # (the doors' flats: the first slit would sit over the door)
            px, pz = cx + ux * ap, cz + uz * ap
            if ux: k.box("castiron", min(px, px + ux * 0.04), y + h, pz - 0.3, max(px, px + ux * 0.04), y + h + 1.5, pz + 0.3)
            else: k.box("castiron", px - 0.3, y + h, min(pz, pz + uz * 0.04), px + 0.3, y + h + 1.5, max(pz, pz + uz * 0.04))
            if ux: k.box("whitewashedbrick", min(px, px + ux * 0.18), y + h - 0.15, pz - 0.45, max(px, px + ux * 0.18), y + h, pz + 0.45)
            else: k.box("whitewashedbrick", px - 0.45, y + h - 0.15, min(pz, pz + uz * 0.18), px + 0.45, y + h, max(pz, pz + uz * 0.18))
    for i in range(8):
        t = 2 * math.pi * i / 8
        k.box("castiron", cx + 1.83 * math.cos(t) - 0.04, GAL, cz + 1.83 * math.sin(t) - 0.04,
              cx + 1.83 * math.cos(t) + 0.04, GAL + 3.4, cz + 1.83 * math.sin(t) + 0.04)
    # the cottages
    for (x0, z0, x1, z1) in ((-44, -48, -32, -38), (-44, -28, -32, -18)):
        xc = (x0 + x1) / 2
        for (za, zb) in ((z0 - 0.15, z0), (z1, z1 + 0.15)):
            k.box("limestoneashlar", xc - 1.15, y + 0.85, za, xc + 1.15, y + 1.0, zb)      # (window sills)
            k.box("limestoneashlar", xc - 1.2, y + 2.4, za, xc + 1.2, y + 2.65, zb)       # (lintels)
        for (xa, xb) in ((x0 - 0.12, x0), (x1, x1 + 0.12)):
            k.box("limestoneashlar", xa, y + 3.0, (z0 + z1) / 2 - 1.4, xb, y + 3.25, (z0 + z1) / 2 + 1.4)   # (door lintels)
        for (qx, sx) in ((x0, -1), (x1, 1)):
            for (qz, sz) in ((z0, -1), (z1, 1)):
                for j in range(4):   # (quoins: alternate long and short stones up each corner, on the gable side)
                    l = 0.7 if j % 2 == 0 else 0.4
                    k.box("limestoneashlar", min(qx, qx + sx * 0.05), y + 0.2 + j * 1.05, min(qz, qz - sz * l),
                          max(qx, qx + sx * 0.05), y + 0.2 + j * 1.05 + 0.5, max(qz, qz - sz * l))
        for (za, zb) in ((z0 - 0.5, z0 - 0.3), (z1 + 0.3, z1 + 0.5)):
            k.box("castiron", x0 - 0.4, y + 4.3, za, x1 + 0.4, y + 4.5, zb)               # (gutters)
            k.box("castiron", x1 - 0.35, y, za + 0.04, x1 - 0.23, y + 4.3, za + 0.16)      # (downpipes)
        k.box("slatecladding", x0 - 0.4, y + 6.9, (z0 + z1) / 2 - 0.18, x1 + 0.4, y + 7.15, (z0 + z1) / 2 + 0.18)   # (ridge)
    # the boathouse: piers on its long walls, a ridge cap, lintels over the doors
    for (px, sx, zs) in ((56, 1, (-45.5, -30.5)), (34, -1, (-46.5, -37.5, -29.5))):
        for pz in zs:
            k.box("riverstonewall", min(px, px + sx * 0.5), LOW, pz - 0.4, max(px, px + sx * 0.5), LOW + 8.6, pz + 0.4)
    k.box("slatecladding", 33.6, LOW + 11.9, -38.18, 56.4, LOW + 12.15, -37.82)
    for (xa, xb, dz) in ((33.85, 34, -42), (33.85, 34, -33), (56, 56.15, -38)):
        k.box("limestoneashlar", xa, LOW + 3.6, dz - 1.9, xb, LOW + 3.85, dz + 1.9)
    k.box("limestoneashlar", 39.6, LOW + 5.5, -28.0, 48.4, LOW + 5.85, -27.85)          # (the slip doors' lintel)
    #   inside: oars racked on the north wall, coils of rope by the loft stair
    for oz in (-46.6, -45.7, -44.8, -43.9):   # (on the east wall's inside, north of its door)
        k.box("rottingwood", 55.5, LOW + 0.4, oz - 0.05, 55.6, LOW + 4.2, oz + 0.05)
    for (rx, rz) in ((54.6, -30.2), (53.4, -30.2)):
        k.disc("hay", rx, rz, 0.45, LOW, LOW + 0.3, n=8)
    # the wreck: hatch coamings on her deck (facing the sea), the anchor in the sand by the bow
    for (h0, h1) in ((-14.5, -10.5), (0.5, 4.5), (10.5, 14.5)):   # (inside the ribs' bays)
        for (a, b, c, d) in ((h0, LOW + 5.0, h1, LOW + 5.25), (h0, LOW + 7.3, h1, LOW + 7.55), (h0, LOW + 5.25, h0 + 0.25, LOW + 7.3), (h1 - 0.25, LOW + 5.25, h1, LOW + 7.3)):
            k.box("rottingwood", a, b, 27.8, c, d, 27.98)
    k.box("castiron", 26.6, LOW, 23.0, 26.9, LOW + 0.3, 27.4)       # (the shank)
    k.box("castiron", 25.6, LOW, 22.7, 27.9, LOW + 0.3, 23.0)       # (the arms)
    k.box("castiron", 26.45, LOW, 27.4, 27.05, LOW + 0.35, 27.7)    # (the ring)
    # kelp thrown up along the tide line, outside the lanes' middle (the last 2 m of sand before the surf)
    for (i, x) in enumerate(range(-52, 53, 8)):
        if -21 < x < 27 or abs(x + 36) < 2 or abs(x - 34) < 2 or abs(x + 28) < 2: continue   # (the wreck, groynes, boat)
        z = 33.6 + 0.6 * ((i * 37) % 5) / 4
        k.box("kelp", x - 1.2 + (i % 3) * 0.4, LOW, z, x + 1.0, LOW + 0.04, z + 0.9)
    # the sea cave: flowstone down its walls, barnacles where the tide reaches
    for (za, zb) in ((-3.0, -2.95), (2.95, 3.0)):
        for (xa, xb) in ((-17, -9), (-4, 4), (9, 17)):
            k.box("flowstone", xa, LOW + 1.4, za, xb, LOW + 3.8, zb)
        k.box("barnacles", -20, LOW, za, 20, LOW + 0.9, zb)
    # the beach's outer walls: bird lime and lichen on the rock above the corner boulders
    for (xa, xb) in ((-60.0, -59.95), (59.95, 60.0)):
        k.box("seabirdcliff", xa, 6.5, 12, xb, 9.5, 36)
        k.box("mossystone", xa, 3.0, 12, xb, 4.8, 36)
    # the spawn shed's rafters under its slates
    for rx in (-57.0, -54.5, -51.5, -49.0):
        ramp_slab(k, "rottingwood", rx - 0.08, -48, rx + 0.08, -35.5, y + 3.15 - 0.27, y + 3.9 - 0.27, "-z", thick=0.15)
    # the tower's doorways: dressed-stone jambs and a red lintel set in the shell's reveal (flush with its face, under
    # the hook-proof skin)
    ai = 5.2 * math.cos(math.radians(15))
    for sx in (-1, 1):
        p0, p1 = sorted((cx + sx * ai, cx + sx * ap))
        for jz in (cz - 1.6, cz + 1.35):
            k.box("limestoneashlar", p0, y, jz, p1, y + 2.9, jz + 0.25)
        k.box("paintedredbrick", p0, y + 2.9, cz - 1.6, p1, y + 3.2, cz + 1.6)
    # the outer walls: buttresses on the north wall over the plateau (clear of the alley behind the cottages) and on
    # the east wall over B's yard and the east shore, bird lime and lichen between them
    F = ("z", 1, -50.0)
    nb = [(-16, -14), (-3, -1), (13, 15)]
    for (a, b) in nb: buttress(k, "graniterock", F, a, b, y, 17.8, 1.0)
    band(k, "mossystone", F, -60, 20, y + 1.0, y + 2.6, nb + [(-29, -21)])
    band(k, "seabirdcliff", F, -60, 20, y + 3.6, y + 5.4, nb)
    F = ("x", -1, 60.0)
    eb = [(-23, -21), (3, 5)]
    for (a, b) in eb: buttress(k, "graniterock", F, a, b, LOW, 15.8, 1.0)
    band(k, "mossystone", F, -28, 12, LOW + 1.5, LOW + 3.2, eb)
    band(k, "seabirdcliff", F, -28, 12, 7.0, 10.0, eb)
    # bollards along the slipway's edges (mooring posts for the boats hauled up it)
    for (bx, bz) in ((37.55, -20), (37.55, -10), (50.45, -20), (50.45, -10)):
        k.disc("castiron", bx, bz, 0.3, LOW, LOW + 0.6, n=8)

