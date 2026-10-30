"""GREENHOUSE (1.19.6): 1v1 (and duel, team vs, tourney, small FFA) in a Victorian glasshouse, 53 x 44 m, all under
roof:
  palm house  a 12-sided hall 24 m across (a 1 m brick plinth, iron-framed glass 0.25 m thick to 13 m,
              a glass crown at 16), a shallow 5 x 5 m basin (clear water,
              0.5 m) round a giant palm at its centre, two planting beds (1.2 m: crouch cover) on the wing-to-wing
              line, a high walkway ring at 9 m (1.6 m wide, 4 m clear over it) with two bridges across under the
              fronds; two straight cast-iron stairs up to it, point symmetric (north one rises west, south one east).
              Breakable glass partitions (1/8 m: shoot-through, a dash through costs 10 hp) face each wing; its doors
              are 6 m off the axis (4 m tall), so there is no straight line wing to wing.
  orchid      (west, team A) 17.5 x 18 m at floor level, a mezzanine at 4.5 m in the north-west corner up a stair
              along the north wall, benches;
  fern        (east, team B) 17.5 x 18 m sunk 2 m (a wide stair down by the palm house), a rock grotto with a small
              pool (plain water: no healing) in its north-east corner, a fern bank along the south wall;
  the wings' long walls are glass above a 2 m brick plinth, their roofs lead with a glass lantern down the middle;
  boiler      (north) a brick corridor 3 m wide and 4.5 m tall joining both wings behind the palm house (the flank loop,
              stairs down into the fern house), the boiler room in its middle.
Every main route is at least 4 m tall and 2.5 m wide (DOUBLE SIZE makes players 3.6 m tall). Outside the glass: a
lawn, a hedge, trees and a garden wall (out of reach); a clip lid at 20 m over everything.

Fix round (1.19.6 review, q's answers): glass and iron walls instead of concrete (palm house and wings), real
planting (moss beds, a pebble path, palms in the wing corners), the hot spring replaced by a small plain pool (no
healing in one-life rounds), the wings trimmed 4 m each (x +-26: 2,011 m2 of floor, was 2,792), the any spawns
facing walls and doors (the palm floor's two behind the beds), the stairs closed under their low half (no head
snags), the wing roofs solid but for a lantern (was 827 m2 of glass), the clip lid, a smaller basin. Walk times
(slowest spawn, 9.5 m/s): basin A 3.6 / B 3.4 s, boiler room A 5.2 / B 5.1 s, bridges A 6.4 / B 6.1 s, spawn to
spawn 5.5 s."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

R_IN, R_OUT = 12.0, 12.5          # the palm house's wall
WALL_H = 13.0
WALK_Y = 9.0                      # the walkway ring r 10.4..12
WEST = (-26.0, -9.0, -8.5, 9.0)   # the orchid house (its floor 0)
EAST = (8.5, -9.0, 26.0, 9.0)     # the fern house (sunk to -2 east of x 16.5)
FERN_Y = -2.0
ROOF = 9.5                        # the wings' ceiling
CORR = (-24.0, -18.0, 24.0, -15.0)  # the boiler corridor
DOOR_H = 4.0
ANY = [(-9.0, 0, 0.0, -13, 0), (9.0, 0, 0.0, 13, 0), (-12.0, 0, -16.5, -24, -16.5), (12.0, 0, -16.5, 24, -16.5),
       (-3.0, WALK_Y, 11.2, -8, 8.5), (3.0, WALK_Y, -11.2, 8, -8.5), (-17, 0, -2, -26, -2), (18, FERN_Y, 2, 26, 2)]
BASIN = 2.5                       # the basin's half width (5 x 5 m: a smaller slow patch on the cross lines)


def seg_chord(i, r):
    a0, a1 = math.radians(-15 + 30 * i), math.radians(15 + 30 * i)
    return (r * math.cos(a0), r * math.sin(a0)), (r * math.cos(a1), r * math.sin(a1))


def glass_wall(k, axis, c0, c1, a0, a1, floor, doors):
    """a wing's long wall along x (z c0..c1, x a0..a1): a brick plinth to 2 m above the floor, glass 0.25 m thick in
    wrought-iron frames above it to the roof (a mullion every 3 m, a transom at 5.5), doorways (x ranges) 4 m tall"""
    lo = floor - 1
    pos = a0
    for (p, q) in sorted(doors) + [(a1, a1)]:
        if p - pos > 1e-3: k.box("londonstock", pos, lo, c0, p, floor + 2.0, c1)
        if q - p > 1e-3:
            k.box("wroughtiron", p, floor + DOOR_H, c0, q, floor + DOOR_H + 0.2, c1)                      # (the lintel)
            k.box("glass", p, floor + DOOR_H + 0.2, c0 + 0.125, q, ROOF - 0.2, c0 + 0.375)
        pos = q
    g0 = c0 + 0.125
    spans = [(a0, a1)]
    for (p, q) in sorted(doors):
        spans = [s for (x, y) in spans for s in ((x, min(y, p)), (max(x, q), y)) if s[1] - s[0] > 1e-3]
    for (x, y) in spans:
        k.box("londonstock", x, floor + 2.0, c0, y, floor + 2.2, c1)
        k.box("glass", x, floor + 2.2, g0, y, ROOF - 0.2, g0 + 0.25)
    k.box("wroughtiron", a0, ROOF - 0.2, c0, a1, ROOF, c1)
    k.box("wroughtiron", a0, floor + 5.5, c0, a1, floor + 5.65, c1)
    n = max(1, round((a1 - a0) / 3.0))
    for i in range(n + 1):
        x = a0 + (a1 - a0) * i / n
        k.box("wroughtiron", max(a0, x - 0.08), floor + 2.2, c0 - 0.05, min(a1, x + 0.08), ROOF - 0.2, c1 + 0.05)


def roof(k, x0, x1, z0, z1):
    """a wing's roof: lead-sheet slopes either side of a glass lantern 2.4 m wide down the middle (glass only there).
    It stops at the palm house's wall: the box part ends at |x| 12, the rest is lead cut along the wall's circle (r
    12.25: inside the wall's thickness), so nothing pokes in over the walkway and no gap opens to the sky"""
    sx = 1 if x0 > 0 else -1
    zs = [z0, -8.0, -6.0, -3.5, 0.0, 3.5, 6.0, 8.0, z1]
    for i in range(len(zs) - 1):
        za, zb = zs[i], zs[i + 1]
        xa, xb = sx * math.sqrt(12.25 ** 2 - za * za), sx * math.sqrt(12.25 ** 2 - zb * zb)
        k.prism("leadsheet", [(sx * 12.0, za), (xa, za), (xb, zb), (sx * 12.0, zb)][::(1 if sx < 0 else -1)], ROOF, ROOF + 0.25)
    x0, x1 = (x0, -12.0) if sx < 0 else (12.0, x1)
    k.box("leadsheet", x0, ROOF, z0, x1, ROOF + 0.25, -1.2)
    k.box("leadsheet", x0, ROOF, 1.2, x1, ROOF + 0.25, z1)
    k.box("glass", x0, ROOF + 0.1, -1.2, x1, ROOF + 0.25, 1.2)
    n = max(1, round((x1 - x0) / 3.0))
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        k.box("wroughtiron", max(x0, x - 0.08), ROOF - 0.25, -1.3, min(x1, x + 0.08), ROOF + 0.1, 1.3)


def planting(k):
    """real planting, kept off the door-to-door lines: moss and fern beds along the palm house's solid segments,
    a pebble path round the basin, palms in the wings' outer corners, a bed along each wing's south wall"""
    k.ring("riverpebbles", 0, 0, BASIN + 0.2, BASIN + 1.4, 0, 0.02, n=12, a0=-15)
    for i in (2, 4, 8, 10):   # (beds 0.45 m tall against the wall: under the step height, so a runner just steps up)
        k.ring("mosscarpet", 0, 0, 10.4, R_IN, 0, 0.45, n=12, a0=-15, only=[i])
    for (x, z) in ((-24.8, 8.2), (24.8, 6.4)):
        y = 0 if x < 0 else FERN_Y
        k.disc("palmtrunk", x, z, 0.3, y, y + 6.5, n=6)
        for (ex, ez) in ((1.8, 0), (-1.2, 1.2), (-1.2, -1.2), (0.6, -1.6)):
            k.bar("jungleleaves", (x, z), (x + ex, z + ez), 0.9, y + 6.2, y + 6.5)
    k.box("terracotta", -19.0, 0, 8.6, -13, 0.45, 9.0)                        # (raised bed under the hedge)
    k.box("mosscarpet", 17.0, FERN_Y, -9.0, 20.5, FERN_Y + 0.45, -8.4)         # (moss bed on the fern house's north wall)


def outside(k):
    """what the glass looks out on: a lawn with gravel walks, a hedge and trees round the house, a garden wall; a clip
    lid at 20 m over the whole footprint (nobody gets onto the wing roofs or the crown with moon gravity)"""
    holes = [(-12.5, -12.5, 12.5, 12.5), (8.5, -9.5, 26.5, 9.5), (20.5, -15.5, 24.5, -9.0)]
    for t in minus((-34.0, -30.0, 34.0, 22.0), holes):
        k.box("grass", t[0], -1.2, t[1], t[2], -0.1, t[3])
    k.box("gravel", -34, -0.1, 14.0, 34, -0.08, 16.0)
    k.box("hedge", -34, -0.1, 18.5, 34, 1.6, 19.5)
    k.box("londonstock", -34.5, -1.2, 22.0, 34.5, 3.0, 22.5); k.box("londonstock", -34.5, -1.2, -30.5, 34.5, 3.0, -30.0)
    k.box("londonstock", -34.5, -1.2, -30.0, -34.0, 3.0, 22.0); k.box("londonstock", 34.0, -1.2, -30.0, 34.5, 3.0, 22.0)
    for (x, z) in ((-20, 15.0), (0, 17.0), (20, 15.0), (-30, -4), (30, 4), (-14, -12.5), (14, -12.5)):
        k.disc("treebark", x, z, 0.35, -0.1, 7.0, n=6)
        k.disc("jungleleaves" if abs(x) < 25 else "pineboughs", x, z, 2.2, 5.0, 8.0, n=8)
    k.box("clip", -35, 20.0, -31, 35, 21.0, 23)


def build():
    m = M("Greenhouse", "1v1", sun=(0.2, 1.0, 0.15))
    m.also = ["duel", "vs", "tourney"]
    k = Kit(m)
    m.raw("palette base 0.80 0.80 0.76 accent 0.24 0.36 0.26 metal 0.18 0.20 0.20 glow 1.00 0.90 0.70 sky 0.82 0.88 0.92")

    # ---- floors
    B = BASIN
    k.box("encaustictile", -12.5, -1, -12.5, 12.5, 0, -B); k.box("encaustictile", -12.5, -1, B, 12.5, 0, 12.5)
    k.box("encaustictile", -12.5, -1, -B, -B, 0, B); k.box("encaustictile", B, -1, -B, 12.5, 0, B)
    k.box("victorianborder", -B, -1.5, -B, B, -0.5, B)                                                 # the basin
    k.box("clearlagoon", -B, -0.5, -B, B, -0.08, B)
    x0, z0, x1, z1 = WEST
    k.box("quarrytile", x0 - 0.5, -1, z0, -12.5, 0, z1)
    for (a, b, c, d) in minus((16.5, z0, EAST[2] + 0.5, z1), [(24.5, -9, 26.5, -6.6)]):
        k.box("riverpebbles", a, FERN_Y - 1, b, c, FERN_Y, d)
    k.stairs("quarrytile", 12.5, z0, 16.5, z1, 0, "x", -1, base=FERN_Y)
    k.box("londonstock", CORR[0], -1, CORR[1], CORR[2], 0, CORR[3])
    k.box("londonstock", -5, -1, -24, 5, 0, CORR[1])

    # ---- the palm house: the 12-sided wall (segment i is centred on 30i degrees from +x toward +z)
    doors = (1, 5, 7, 11)
    panes = (0, 6)
    solid = [i for i in range(12) if i not in doors + panes]
    # an iron and glass wall: a 1 m brick plinth, glass panes 0.25 m thick (shots stop) to 13 m in iron frames; the
    # lower ring (to 4.2) a lattice of small panes (a mullion mid-segment, a transom at 2.6), full panes above
    k.ring("londonstock", 0, 0, R_IN, R_OUT, -1, 1.0, n=12, a0=-15, only=solid)
    k.ring("glass", 0, 0, R_IN + 0.125, R_IN + 0.375, 1.0, DOOR_H, n=12, a0=-15, only=solid)
    k.ring("glass", 0, 0, R_IN + 0.125, R_IN + 0.375, DOOR_H + 0.2, WALL_H - 0.3, n=12, a0=-15)
    k.ring("wroughtiron", 0, 0, R_IN, R_OUT, DOOR_H, DOOR_H + 0.2, n=12, a0=-15)
    k.ring("wroughtiron", 0, 0, R_IN, R_OUT, 2.5, 2.7, n=12, a0=-15, only=solid)
    k.ring("wroughtiron", 0, 0, R_IN, R_OUT, WALL_H - 0.3, WALL_H, n=12, a0=-15)
    for i in solid:
        t = math.radians(30 * i)
        x, z = 12.25 * math.cos(t), 12.25 * math.sin(t)
        k.bar("wroughtiron", (x - 0.25 * math.cos(t), z - 0.25 * math.sin(t)), (x + 0.25 * math.cos(t), z + 0.25 * math.sin(t)), 0.12, 1.0, DOOR_H)
    for i in panes:   # the breakable partitions: one axis-thin pane (1/8 m) across each wing's face
        (ax, az), (bx, bz) = seg_chord(i, R_IN)
        x = (ax + bx) / 2
        k.box("breakableglass", x - 0.0625, 0, min(az, bz), x + 0.0625, DOOR_H, max(az, bz))
        k.box("wroughtiron", x - 0.15, -1, min(az, bz), x + 0.15, 0.25, max(az, bz))                    # (its sill)
    # the crown: two iron tiers and a glass cap
    k.ring("wroughtiron", 0, 0, 9.0, R_OUT, WALL_H, 14.5, n=12, a0=-15)
    k.ring("wroughtiron", 0, 0, 5.5, 9.5, 14.5, 15.5, n=12, a0=-15)
    k.disc("glass", 0, 0, 6.0, 15.5, 15.75, n=8)
    k.disc("wroughtiron", 0, 0, 6.2, 15.8, 16.3, n=8)

    # the walkway ring at 9 m, its rail, two bridges across, the two stairs up
    k.ring("castiron", 0, 0, 10.4, R_IN, WALK_Y - 0.3, WALK_Y, n=12, a0=-15)
    k.ring("castiron", 0, 0, 10.4, 10.55, WALK_Y, WALK_Y + 1.0, n=12, a0=-15, skip=[2, 8])
    for sz in (-1, 1):
        za, zb = sorted((sz * 3.7, sz * 5.3))
        k.box("castiron", -10.2, WALK_Y - 0.3, za, 10.2, WALK_Y, zb)
        for zz in (za, zb - 0.1): k.box("castiron", -9.8, WALK_Y, zz, 9.8, WALK_Y + 1.0, zz + 0.1)
    k.stairs("castiron", -6.5, 6.5, 6.5, 9.0, WALK_Y, "x", 1, solid=False)          # south: rises east (A's)
    # (under each stair's low half, to 4.5 m, closed: nobody runs in under a 2-4 m step and clips a head)
    n = 18
    for i in range(9):
        h = WALK_Y * (i + 1) / n - 0.5
        if h <= 0: continue
        a = -6.5 + 13.0 * i / n; b = -6.5 + 13.0 * (i + 1) / n
        k.box("castiron", a, 0, 6.5, b, h, 9.0)                  # (south: low at the west end)
        k.box("castiron", -b, 0, -9.0, -a, h, -6.5)              # (north: low at the east end)
    k.box("castiron", 6.5, WALK_Y - 0.3, 6.5, 9.6, WALK_Y, 9.0)
    k.stairs("castiron", -6.5, -9.0, 6.5, -6.5, WALK_Y, "x", -1, solid=False)       # north: rises west (B's)
    k.box("castiron", -9.6, WALK_Y - 0.3, -9.0, -6.5, WALK_Y, -6.5)

    # the palm (trunk to 12, fronds 11.5-12.5 between the bridges), the beds
    k.disc("palmtrunk", 0, 0, 0.6, -0.5, 12.2, n=8)
    for (ex, ez) in ((6.5, 0), (-6.5, 0), (5, 2.8), (-5, -2.8), (5, -2.8), (-5, 2.8)):
        k.bar("jungleleaves", (ex * 0.08, ez * 0.08), (ex, ez), 1.4, 11.6, 12.1)
    for sx in (-1, 1):
        a, b = sorted((sx * 5.5, sx * 7.5))
        k.box("terracotta", a, 0, -1.5, b, 0.9, 1.5); k.box("flowermeadow", a + 0.1, 0.9, -1.4, b - 0.1, 1.2, 1.4)

    # ---- the orchid house (west): walls, the mezzanine at 4.5 on the north side, its stair, benches
    x0, z0, x1, z1 = WEST
    k.box("londonstock", x0 - 0.5, -1, z0 - 0.5, x0, ROOF, z1 + 0.5)
    glass_wall(k, "x", z0 - 0.5, z0, x0, x1, 0.0, [(-24, -21)])
    glass_wall(k, "x", z1, z1 + 0.5, x0, x1, 0.0, [])
    roof(k, x0 - 0.5, x1, z0 - 0.5, z1 + 0.5)
    k.box("castiron", x0, 4.2, z0, -22, 4.5, -2.0)                                    # the mezzanine (x -26..-22)
    k.box("castiron", x0, 4.5, -2.1, -22, 5.5, -2.0)
    k.box("castiron", -22.1, 4.5, -6.5, -22, 5.5, -2.0)
    k.stairs("castiron", -22, z0, -13, -6.5, 4.5, "x", -1, solid=False)
    for (a, b) in ((-18, -15),):
        for (c, d) in ((2.0, 3.5), (5.5, 7.0)):
            k.box("wideplanks", a, 0, c, b, 1.0, d)                                    # (the orchid benches)

    # ---- the fern house (east): walls, the grotto and hot spring, the fern bank
    x0, z0, x1, z1 = EAST
    k.box("londonstock", x1, FERN_Y - 1, z0 - 0.5, x1 + 0.5, ROOF, z1 + 0.5)
    glass_wall(k, "x", z0 - 0.5, z0, x0, x1, FERN_Y, [(21, 24)])
    glass_wall(k, "x", z1, z1 + 0.5, x0, x1, FERN_Y, [])
    roof(k, x0, x1 + 0.5, z0 - 0.5, z1 + 0.5)
    # the grotto: rocks round a small pool (plain water, no healing: one-life rounds) in the north-east corner
    k.box("graniterock", 24.1, FERN_Y - 1, -9, 24.5, FERN_Y + 2.2, -6.6)
    k.box("graniterock", 24.1, FERN_Y + 3.0, -9, 26, FERN_Y + 3.6, -7.6)
    k.box("graniterock", 24.5, FERN_Y - 1.0, -9, 26, FERN_Y - 0.5, -6.6)
    k.box("clearlagoon", 24.5, FERN_Y - 0.5, -9, 26, FERN_Y - 0.08, -6.6)
    k.wedge("ferns", 2, 18, FERN_Y, 7.0, 25.5, FERN_Y + 1.4, 9.0)                    # the fern bank (south wall)

    # ---- the boiler house: the corridor, spurs into both wings, the boiler room
    cx0, cz0, cx1, cz1 = CORR
    ch = 4.5
    k.box("londonstock", cx0 - 0.5, -1, cz0 - 0.5, cx1 + 0.5, ch, cz0)                  # (north wall, the room's door cut below)
    k.box("londonstock", -24.5, -1, cz0, cx0, ch, cz1 + 0.5)
    k.box("londonstock", cx1, -1, cz0, 24.5, ch, cz1 + 0.5)
    k.box("londonstock", -21, -1, cz1, 21, ch, cz1 + 0.5)                                # (south wall)
    k.box("londonstock", cx0 - 0.5, ch, cz0 - 0.5, cx1 + 0.5, ch + 0.4, cz1 + 0.5)
    for (sx, floor) in ((-1, 0.0), (1, FERN_Y)):
        a, b = sorted((sx * 21, sx * 24))
        wa, wb = sorted((sx * 20.5, sx * 21)); wc, wd = sorted((sx * 24, sx * 24.5))
        k.box("londonstock", wa, FERN_Y - 1, cz1, wb, ch, -9.5)
        k.box("londonstock", wc, FERN_Y - 1, cz1, wd, ch, -9.5)
        k.box("londonstock", a, ch, cz1, b, ch + 0.4, -9.5)
        if floor == 0: k.box("londonstock", a, -1, cz1, b, 0, -9.5)
        else:
            k.box("londonstock", a, -1, cz1, b, 0, -13)
            k.stairs("londonstock", a, -13, b, -9.5, 0, "z", -1, base=FERN_Y)
    # the boiler room in the middle of the corridor (north), the boiler, its chimney through the roof
    k.box("londonstock", -5.5, -1, -24.5, 5.5, 6, -24); k.box("londonstock", -5.5, -1, -24, -5, 6, cz0 - 0.5); k.box("londonstock", 5, -1, -24, 5.5, 6, cz0 - 0.5)
    k.box("londonstock", -5.5, 6, -24.5, 5.5, 6.4, cz0)
    # (cut the corridor's north wall open into the room)
    m.lines = [l for l in m.lines if not l.startswith(f"londonstock {num(cx0 - 0.5)} -1 {num(cz0 - 0.5)} ")]
    k.box("londonstock", cx0 - 0.5, -1, cz0 - 0.5, -5, ch, cz0); k.box("londonstock", 5, -1, cz0 - 0.5, cx1 + 0.5, ch, cz0)
    k.box("londonstock", -5, 4.5, cz0 - 0.5, 5, 6, cz0)
    k.disc("castiron", 0, -21.5, 1.5, 0, 3.0, n=8)
    k.disc("londonstock", 0, -21.5, 0.6, 3.0, 12, n=8)

    # ---- spawns: A in the orchid house (west end), B in the fern house (east end), behind frosted screens
    for (x, z) in ((-24.5, -1), (-24.5, 2.5), (-24.5, 6), (-22.5, 4)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((23.5, -2.5), (23.5, 1), (23.5, 4.5), (21.5, 1)):
        k.spawn("b", x, FERN_Y, z, yaw_to(x, z, 0, 0))
    k.box("frostedglass", -19.625, 0, 0, -19.5, 2.6, 8)
    k.box("frostedglass", 19.5, FERN_Y, -3, 19.625, FERN_Y + 2.6, 4)
    # free for all / warm-up: facing walls or doors, the palm floor's two behind the beds
    for (x, y, z, tx, tz) in ANY:
        k.spawn("any", x, y, z, yaw_to(x, z, tx, tz))

    # ---- decoration (sparing, against the walls): iron ribs up the palm house's wall, lamp strips under the walkway,
    # terracotta pots and hedges along the wings' walls, ivy on the boiler house brick, the basin's rim tiles
    for i in range(12):
        t = math.radians(30 * i - 15)
        x, z = 11.85 * math.cos(t), 11.85 * math.sin(t)
        k.box("castiron", x - 0.15, 0, z - 0.15, x + 0.15, WALL_H, z + 0.15)
    k.ring("lightblock", 0, 0, 10.45, 10.6, WALK_Y - 0.45, WALK_Y - 0.3, n=12, a0=-15)
    for x in (-12.5, 14):
        y = 0 if x < 0 else FERN_Y
        for zz in (8.0, -8.0):
            k.box("terracotta", x - 0.5, y, zz - 0.5, x + 0.5, y + 0.8, zz + 0.5)
            k.box("ferns" if x > 0 else "flowermeadow", x - 0.45, y + 0.8, zz - 0.45, x + 0.45, y + 1.0, zz + 0.45)
    k.box("hedge", -19, 0, 8.3, -13, 1.2, 9.0)
    for (x0, x1) in ((-20.5, -6), (6, 20.5)):
        k.box("ivyonbrick", x0, 0.5, -15.05, x1, 3.8, -15.0)
    e = BASIN + 0.2
    k.box("victorianborder", -e, 0, -e, e, 0.02, -BASIN); k.box("victorianborder", -e, 0, BASIN, e, 0.02, e)
    k.box("victorianborder", -e, 0, -BASIN, -BASIN, 0.02, BASIN); k.box("victorianborder", BASIN, 0, -BASIN, e, 0.02, BASIN)
    planting(k)
    k.box("cobwebs", -5, 4.6, -23.9, -2, 5.9, -23.8)

    # ---- light: soft daylight through the glass, lamps in the wings, warm in the boiler house
    k.light(0, 12, 0, 22, 0.9); k.light(0, 4, 0, 14, 0.6)
    for x in (-23, -15): k.light(x, 7.5, 0, 14, 0.8)
    for x in (15, 23): k.light(x, 6.5, 0, 14, 0.8)
    for x in (-18, 0, 18): k.light(x, 3.6, -16.5, 9, 0.7)
    outside(k)
    m.raw("setting victorian glasshouse")
    m.save("greenhouse", dress=False)
