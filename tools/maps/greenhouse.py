"""GREENHOUSE (1.19.6): 1v1 (and duel, team vs, tourney, small FFA) in a Victorian glasshouse, 62 x 44 m, all under
roof:
  palm house  a 12-sided hall 24 m across (the walls to 13 m, a glass crown at 16), a shallow basin (clear water,
              0.5 m) round a giant palm at its centre, two planting beds (1.2 m: crouch cover) on the wing-to-wing
              line, a high walkway ring at 9 m (1.6 m wide, 4 m clear over it) with two bridges across under the
              fronds; two straight cast-iron stairs up to it, point symmetric (north one rises west, south one east).
              Breakable glass partitions (1/8 m: shoot-through, a dash through costs 10 hp) face each wing; its doors
              are 6 m off the axis (4 m tall), so there is no straight line wing to wing.
  orchid      (west, team A) 21 x 18 m at floor level, a mezzanine at 4.5 m along the north wall up a stair, benches;
  fern        (east, team B) 21 x 18 m sunk 2 m (a wide stair down by the palm house), a rock grotto with a hot
              spring (heals 5%/s) in its north-east corner, a fern bank along the south wall: B's answer to A's height;
  boiler      (north) a brick corridor 3 m wide and 4.5 m tall joining both wings behind the palm house (the flank loop,
              stairs down into the fern house), the boiler room in its middle.
Every main route is at least 4 m tall and 2.5 m wide (DOUBLE SIZE makes players 3.6 m tall)."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

R_IN, R_OUT = 12.0, 12.5          # the palm house's wall
WALL_H = 13.0
WALK_Y = 9.0                      # the walkway ring r 10.4..12
WEST = (-30.0, -9.0, -8.5, 9.0)   # the orchid house (its floor 0)
EAST = (8.5, -9.0, 30.0, 9.0)     # the fern house (sunk to -2 east of x 16.5)
FERN_Y = -2.0
ROOF = 9.5                        # the wings' ceiling
CORR = (-24.0, -18.0, 24.0, -15.0)  # the boiler corridor
DOOR_H = 4.0


def seg_chord(i, r):
    a0, a1 = math.radians(-15 + 30 * i), math.radians(15 + 30 * i)
    return (r * math.cos(a0), r * math.sin(a0)), (r * math.cos(a1), r * math.sin(a1))


def build():
    m = M("Greenhouse", "1v1", sun=(0.2, 1.0, 0.15))
    m.also = ["duel", "vs", "tourney"]
    k = Kit(m)
    m.raw("palette base 0.80 0.80 0.76 accent 0.24 0.36 0.26 metal 0.18 0.20 0.20 glow 1.00 0.90 0.70 sky 0.82 0.88 0.92")

    # ---- floors
    k.box("encaustictile", -12.5, -1, -12.5, 12.5, 0, -3.5); k.box("encaustictile", -12.5, -1, 3.5, 12.5, 0, 12.5)
    k.box("encaustictile", -12.5, -1, -3.5, -3.5, 0, 3.5); k.box("encaustictile", 3.5, -1, -3.5, 12.5, 0, 3.5)
    k.box("victorianborder", -3.5, -1.5, -3.5, 3.5, -0.5, 3.5)                                         # the basin
    k.box("clearlagoon", -3.5, -0.5, -3.5, 3.5, -0.08, 3.5)
    x0, z0, x1, z1 = WEST
    k.box("quarrytile", x0 - 0.5, -1, z0, -12.5, 0, z1)
    for (a, b, c, d) in minus((16.5, z0, EAST[2] + 0.5, z1), [(25.5, -9, 30.5, -4.5)]):
        k.box("riverpebbles", a, FERN_Y - 1, b, c, FERN_Y, d)
    k.stairs("quarrytile", 12.5, z0, 16.5, z1, 0, "x", -1, base=FERN_Y)
    k.box("londonstock", CORR[0], -1, CORR[1], CORR[2], 0, CORR[3])
    k.box("londonstock", -5, -1, -24, 5, 0, CORR[1])

    # ---- the palm house: the 12-sided wall (segment i is centred on 30i degrees from +x toward +z)
    doors = (1, 5, 7, 11)
    panes = (0, 6)
    solid = [i for i in range(12) if i not in doors + panes]
    k.ring("whiteconcrete", 0, 0, R_IN, R_OUT, -1, WALL_H, n=12, a0=-15, only=solid)
    k.ring("whiteconcrete", 0, 0, R_IN, R_OUT, DOOR_H, WALL_H, n=12, a0=-15, only=list(doors + panes))
    for i in panes:   # the breakable partitions: one axis-thin pane (1/8 m) across each wing's face
        (ax, az), (bx, bz) = seg_chord(i, R_IN)
        x = (ax + bx) / 2
        k.box("breakableglass", x - 0.0625, 0, min(az, bz), x + 0.0625, DOOR_H, max(az, bz))
        k.box("wroughtiron", x - 0.15, -1, min(az, bz), x + 0.15, 0.25, max(az, bz))                    # (its sill)
    # the crown: two iron tiers and a glass cap
    k.ring("wroughtiron", 0, 0, 9.0, R_OUT, WALL_H, 14.5, n=12, a0=-15)
    k.ring("wroughtiron", 0, 0, 5.5, 9.5, 14.5, 15.5, n=12, a0=-15)
    k.disc("glass", 0, 0, 6.0, 15.5, 15.8, n=8)
    k.disc("wroughtiron", 0, 0, 6.2, 15.8, 16.3, n=8)

    # the walkway ring at 9 m, its rail, two bridges across, the two stairs up
    k.ring("castiron", 0, 0, 10.4, R_IN, WALK_Y - 0.3, WALK_Y, n=12, a0=-15)
    k.ring("castiron", 0, 0, 10.4, 10.55, WALK_Y, WALK_Y + 1.0, n=12, a0=-15, skip=[2, 8])
    for sz in (-1, 1):
        za, zb = sorted((sz * 3.7, sz * 5.3))
        k.box("castiron", -10.2, WALK_Y - 0.3, za, 10.2, WALK_Y, zb)
        for zz in (za, zb - 0.1): k.box("castiron", -9.8, WALK_Y, zz, 9.8, WALK_Y + 1.0, zz + 0.1)
    k.stairs("castiron", -6.5, 6.5, 6.5, 9.0, WALK_Y, "x", 1, solid=False)          # south: rises east (A's)
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
    k.box("whiteconcrete", x0 - 0.5, -1, z0 - 0.5, x0, ROOF, z1 + 0.5)
    k.box("whiteconcrete", x0, -1, z0 - 0.5, -24, ROOF, z0); k.box("whiteconcrete", -21, -1, z0 - 0.5, x1, ROOF, z0)
    k.box("whiteconcrete", -24, DOOR_H + 0.5, z0 - 0.5, -21, ROOF, z0)
    k.box("whiteconcrete", x0, -1, z1, x1, ROOF, z1 + 0.5)
    k.box("glass", x0 - 0.5, ROOF, z0 - 0.5, x1, ROOF + 0.25, z1 + 0.5)
    k.box("castiron", x0, 4.2, z0, -22, 4.5, -5.5)                                    # the mezzanine
    k.box("castiron", -22, 4.5, -5.6, x0, 5.5, -5.5)
    k.stairs("castiron", -22, z0, -13, -6.5, 4.5, "x", -1, solid=False)
    for (a, b) in ((-19, -16),):
        for (c, d) in ((2.0, 3.5), (5.5, 7.0)):
            k.box("wideplanks", a, 0, c, b, 1.0, d)                                    # (the orchid benches)

    # ---- the fern house (east): walls, the grotto and hot spring, the fern bank
    x0, z0, x1, z1 = EAST
    k.box("whiteconcrete", x1, FERN_Y - 1, z0 - 0.5, x1 + 0.5, ROOF, z1 + 0.5)
    k.box("whiteconcrete", x0, FERN_Y - 1, z0 - 0.5, 21, ROOF, z0); k.box("whiteconcrete", 24, FERN_Y - 1, z0 - 0.5, x1, ROOF, z0)
    k.box("whiteconcrete", 21, DOOR_H + FERN_Y + 0.5, z0 - 0.5, 24, ROOF, z0)
    k.box("whiteconcrete", x0, FERN_Y - 1, z1, x1, ROOF, z1 + 0.5)
    k.box("glass", x0, ROOF, z0 - 0.5, x1 + 0.5, ROOF + 0.25, z1 + 0.5)
    # the grotto: rocks round a 3 x 3 hot spring in the north-east corner
    k.box("graniterock", 24.5, FERN_Y - 1, -9, 25.5, FERN_Y + 3.0, -4.5)
    k.box("graniterock", 28.2, FERN_Y - 1, -4.5, 30, FERN_Y + 2.2, -3.5)
    k.box("graniterock", 25.5, FERN_Y + 3.6, -9, 30, FERN_Y + 4.4, -6.5)
    k.box("graniterock", 25.5, FERN_Y - 1.0, -9, 30.5, FERN_Y - 0.5, -4.5)
    k.box("hotspring", 25.5, FERN_Y - 0.5, -9, 30, FERN_Y - 0.08, -4.5)
    k.wedge("ferns", 2, 18, FERN_Y, 7.0, 29.5, FERN_Y + 1.4, 9.0)                    # the fern bank (south wall)

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
    for (x, z) in ((-27, -3), (-27, 1), (-27, 5), (-24.5, 3)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, 0))
    for (x, z) in ((27, -1.5), (27, 2.5), (27, 6), (24.5, 0)):
        k.spawn("b", x, FERN_Y, z, yaw_to(x, z, 0, 0))
    k.box("frostedglass", -21.5, 0, 0, -21.375, 2.6, 7)
    k.box("frostedglass", 21.375, FERN_Y, -3, 21.5, FERN_Y + 2.6, 4)
    for (x, y, z) in ((-8.5, 0, -3.5), (8.5, 0, 3.5), (-6, 0, 4.5), (6, 0, -4.5), (0, WALK_Y, 11.0), (0, WALK_Y, -11.0),
                      (-17, 0, -2), (18, FERN_Y, 2)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))

    # ---- decoration (sparing, against the walls): iron ribs up the palm house's wall, lamp strips under the walkway,
    # terracotta pots and hedges along the wings' walls, ivy on the boiler house brick, the basin's rim tiles
    for i in range(12):
        t = math.radians(30 * i - 15)
        x, z = 11.85 * math.cos(t), 11.85 * math.sin(t)
        k.box("castiron", x - 0.15, 0, z - 0.15, x + 0.15, WALL_H, z + 0.15)
    k.ring("lightblock", 0, 0, 10.45, 10.6, WALK_Y - 0.45, WALK_Y - 0.3, n=12, a0=-15)
    for x in (-28, -12.5, 14, 28.5):
        y = 0 if x < 0 else FERN_Y
        for zz in (8.0, -8.0):
            if x == 28.5 and zz < 0: continue
            k.box("terracotta", x - 0.5, y, zz - 0.5, x + 0.5, y + 0.8, zz + 0.5)
            k.box("ferns" if x > 0 else "flowermeadow", x - 0.45, y + 0.8, zz - 0.45, x + 0.45, y + 1.0, zz + 0.45)
    k.box("hedge", -21, 0, 8.3, -13, 1.2, 9.0)
    for (x0, x1) in ((-20.5, -6), (6, 20.5)):
        k.box("ivyonbrick", x0, 0.5, -15.05, x1, 3.8, -15.0)
    k.box("victorianborder", -3.7, 0, -3.7, 3.7, 0.02, -3.5); k.box("victorianborder", -3.7, 0, 3.5, 3.7, 0.02, 3.7)
    k.box("victorianborder", -3.7, 0, -3.5, -3.5, 0.02, 3.5); k.box("victorianborder", 3.5, 0, -3.5, 3.7, 0.02, 3.5)
    k.box("castiron", -22, 4.5, -9, -21.9, 5.5, -6.5)                                  # (the mezzanine's rail end)
    k.box("cobwebs", -5, 4.6, -23.9, -2, 5.9, -23.8)

    # ---- light: soft daylight through the glass, lamps in the wings, warm in the boiler house
    k.light(0, 12, 0, 22, 0.9); k.light(0, 4, 0, 14, 0.6)
    for x in (-25, -15): k.light(x, 7.5, 0, 14, 0.8)
    for x in (15, 25): k.light(x, 6.5, 0, 14, 0.8)
    for x in (-18, 0, 18): k.light(x, 3.6, -16.5, 9, 0.7)
    m.raw("setting victorian glasshouse")
    m.save("greenhouse", dress=False)
