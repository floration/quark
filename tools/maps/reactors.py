"""TWIN REACTORS (1.19.6): power (and team vs, FFA) in a nuclear plant, 150 x 92 m inside the perimeter
(the dome and the pool hall stand out through it), two reactor halls of different
designs either side of a shared middle:
  A (west)  a pressurised-water reactor: a 12-sided containment dome 30 m tall (r 15), the vessel at its centre, four
            steam generators round it, the polar crane's ring girder and bridge at 22 m (a perch that only sees the
            dome), a refuelling pool of coolant (x1.3 speed) at the back. Three ways out: the equipment hatch (east,
            7 m), and doors north-east and south-east. Team A spawns inside the hatch.
  B (east)  a pool-type reactor: a low wide hall (flat roof at 16 m), an open Cherenkov pool (clear water over blue
            tile, lit from its walls, x0.9) with a cross of walkways over it at 4 m, the bridge crane at 12 m. Three
            ways out: the airlock door (west, 7 m), and doors in the north and south walls. Team B spawns inside the
            airlock door.
  middle    the turbine hall (64 x 40 m, roof trusses at 14 m to hook): two turbine trains, 4 m tall with walkways on
            top, east-west (the long walls to soak behind, the same casings for both teams), broken in the middle;
            a gallery at 8 m along the north wall, a stair up to each end of it from the hall floor. North of it the control building (two floors, a corridor loop round
            the offices, the control room upstairs behind 1/8 m glass: shoot-through, no bunker), south of it the
            cooling-water intake yard (pumps, a pipe rack at 6 m, a canal of clear water: not lethal).
  shield    each team has one 20 m regroup corridor from its reactor's side door to the turbine hall, a shield wall at
            both ends (stops shots, rockets and grapples; players and sight pass): A's north, B's south.
Power is about soaking: nobody dies of damage, so nothing here is lethal (no pits, no lava, no void inside the plant:
a fall would be a free 2 s trip home). The middle is point symmetric round (0, 0) except the control building (north)
and the intake (south), which both teams reach in the same time (x mirror). An invisible lid at 22 m covers the
plant outside the dome (a jump off the pipe rack tops out at 20.2), with clip from every roof and the perimeter's top
up to it; inside the dome the roof is at 30 m.
  yards     each a set piece, point symmetric: a transformer bank (B north, A south) on B's line from its north door
            to the control building, a cask yard (A north, B south), a cooling tower's louvred base and a gantry crane
            on rails in the corners behind the halls; pilasters, pipe runs, louvres and sign bands on the walls.
  team vs   its own spawns at the turbine hall's ends (vs-a / vs-b: 6.9 s apart, out of each other's sight).

Fix round (1.19.6 review, q's answers): the perimeter pulled in to x +-75 behind the halls (the dead corners gone,
routes unchanged), the yards given set pieces as cover on the door-to-door lines, facades broken up, the control
floor furnished (a server room, an office, desks, partitions); stairs from the hall floor to both gallery ends (walk
to the gallery 9.0 s, was 14.9; control room 10.5, was 14.6); A's shield corridor reaches back to 2 m off its dome
door and B's side doors moved to the pool hall's north and south walls (B's south door no longer opens onto its
corridor's mouth); B's spawns 1.5 m nearer its airlock (the hall's face is 3 m further out than the dome's); B's pool
is clear water over blue tile, lit from its walls and over it; the lid at 22 m; the polar crane lowered for 3 m of
headroom; the hatch-side cover block 7 m from both doors; 14 FFA spawns (13% of pairs see each other); team vs gets
its own spawns. Walk times, A / B (slowest spawn, 9.5 m/s, shield walls left open): hall 7.7 / 7.7, gallery 8.6 /
8.7, control room 10.2 / 10.3, intake rack 9.9 / 10.0, own shield corridor's middle 3.3 / 3.4 s."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 75, 46                               # (the perimeter; the dome and the pool hall stand out through it to |x| 81 / 84)
LID = 22.0
WALL_TOP = 17.5
DOME_C, DOME_R = (-66.0, 0.0), 14.0           # the dome's centre, its inner radius (the wall r 14..15)
BHALL = (54, -16, 84, 16)                     # the pool hall
TURB = (-32, -20, 32, 20)                     # the turbine hall
CTRL = (-28, -46, 28, -20)                    # the control building
CANAL = (-44, 36, 44, 42)                     # the intake canal (along x)
POOL_A = (-78, -5, -72, 5)                    # A's refuelling pool
POOL_B = (64, -10, 80, 10)                    # B's reactor pool
ROOF = 16.0                                   # the halls' roof slab 16..16.5
T = 0.6                                       # wall thickness


def wall(k, mat, axis, c0, c1, a0, a1, y0, y1, doors=()):
    """a wall along `axis` ('x': spans x a0..a1 at z c0..c1; 'z': spans z a0..a1 at x c0..c1) from y0 to y1, with
    doorways doors = [(centre, width, y_bottom, y_top)]; a lintel over each up to y1"""
    cuts = sorted((c - w / 2, c + w / 2, yb, yt) for (c, w, yb, yt) in doors)
    def b(p, q, ya, yb):
        if q - p < 1e-3 or yb - ya < 1e-3: return
        if axis == "x": k.box(mat, p, ya, c0, q, yb, c1)
        else: k.box(mat, c0, ya, p, c1, yb, q)
    pos = a0
    for (p, q, yb, yt) in cuts:
        b(pos, p, y0, y1)
        b(p, q, y0, yb)           # (a sill, for an upper door)
        b(p, q, yt, y1)           # (the lintel)
        pos = q
    b(pos, a1, y0, y1)


def layout(m, k):
    # ---- the ground: pavers outside, each building its own floor; the pools and the canal dug in
    holes = [TURB, CTRL, BHALL, CANAL, (-81, -15, -51, 15)]
    for t in tiles([(-HX, -HZ, HX, HZ)], 30):
        for (x0, z0, x1, z1) in minus(t, holes): k.box("concretepavers", x0, -1, z0, x1, 0, z1)
    for (x0, z0, x1, z1) in minus((-81, -15, -51, 15), [POOL_A]): k.box("boardformedconcrete", x0, -1, z0, x1, 0, z1)
    for t in tiles([TURB], 32): k.box("epoxyfloor", t[0], -1, t[1], t[2], 0, t[3])
    for t in tiles([CTRL], 28): k.box("steelcheckerfloor", t[0], -1, t[1], t[2], 0, t[3])
    for (x0, z0, x1, z1) in minus(BHALL, [POOL_B]): k.box("epoxyfloor", x0, -1, z0, x1, 0, z1)
    # the pools: a ledge half a metre down round the edge (step out), the bottom at -1
    # (B's pool: clear water over pale blue tile, lit from its walls: the Cherenkov blue reads; A's coolant, the canal)
    for (pool, liq, edge, bottom) in ((POOL_A, "coolant", "rivetedplates", "rivetedplates"), (POOL_B, "clearlagoon", "bluepooltile", "bluepooltile"),
                                      (CANAL, "clearlagoon", "boardformedconcrete", "driedriverbed")):
        x0, z0, x1, z1 = pool
        inner = (x0 + 1, z0 + 1, x1 - 1, z1 - 1)
        for (a, b, c, d) in minus(pool, [inner]): k.box(edge, a, -1.5, b, c, -0.5, d)
        k.box(bottom, inner[0], -2, inner[1], inner[2], -1, inner[3])
        k.box(liq, x0, -1.0, z0, x1, -0.2, z1)

    # ---- the perimeter (x +-75, behind the halls: the dome's back and the pool hall's stand out through it), the
    # lid at 22 m over the yards (a jump off the pipe rack tops out at 20.2), clip from every roof and wall top up
    # to the lid, so the roofs stay out of reach; round the dome the lid is a ring, so nothing climbs its outside
    walls = [(-HX - 2, -HZ - 2, -HX, -11), (-HX - 2, 11, -HX, HZ + 2), (HX, -HZ - 2, HX + 2, -16), (HX, 16, HX + 2, HZ + 2),
             (-HX, -HZ - 2, HX, -HZ), (-HX, HZ, HX, HZ + 2)]
    for (x0, z0, x1, z1) in walls:
        k.box("precastconcrete", x0, -1, z0, x1, WALL_TOP, z1)
        k.box("clip", x0, WALL_TOP, z0, x1, LID + 1, z1)
    for (x0, z0, x1, z1) in minus((-HX, -HZ, HX, HZ), [(-81, -15, -51, 15)]): k.box("clip", x0, LID, z0, x1, LID + 1, z1)
    k.ring("clip", DOME_C[0], DOME_C[1], 15.0, 22.0, LID, LID + 1, n=12, a0=-15)
    for (x0, z0, x1, z1) in (BHALL, (TURB[0] - T, TURB[1] - T, TURB[2] + T, TURB[3] + T), (CTRL[0] - T, CTRL[1], CTRL[2] + T, CTRL[3] - T)):
        k.box("clip", x0, ROOF + 0.5, z0, x1, LID, z1)

    # ---- reactor A: the containment dome (segment i spans -15 + 30i .. +30 degrees from +x toward +z)
    cx, cz = DOME_C
    doors = {0: 7.0, 2: 5.0, 10: 5.0}             # the hatch (east), the south-east and north-east doors
    k.ring("castinplacewalls", cx, cz, DOME_R, DOME_R + 1, 0, 18, n=12, a0=-15, skip=list(doors))
    for i, h in doors.items(): k.ring("castinplacewalls", cx, cz, DOME_R, DOME_R + 1, h, 18, n=12, a0=-15, only=[i])
    for (y0, y1, rin) in ((18, 21, 13.5), (21, 24, 11.5), (24, 27, 8.5), (27, 30, 5.0)):
        k.ring("castinplacewalls", cx, cz, rin, DOME_R + 1, y0, y1, n=12, a0=-15)
    k.disc("castinplacewalls", cx, cz, 5.5, 29, 30.5, n=8)
    # inside: the vessel, the steam generators, the polar crane
    k.disc("electroplatednickel", cx, cz, 3.2, 0, 11, n=8)
    k.disc("rivetedplates", cx, cz, 3.8, 0, 1.2, n=8)
    for a in (45, 135, 225, 315):
        gx, gz = cx + 8.5 * math.cos(math.radians(a)), cz + 8.5 * math.sin(math.radians(a))
        k.disc("powdercoatgreen", gx, gz, 1.6, 0, 13, n=8)
    # (the polar crane: low enough for 3 m of headroom under the roof's 24 m tier, a real perch end to end)
    k.ring("powdercoatyellow", cx, cz, 10.4, 11.5, 20.2, 21.0, n=12, a0=-15)
    k.box("powdercoatyellow", cx - 10.6, 20.4, cz - 1.2, cx + 10.6, 21.2, cz + 1.2)

    # ---- reactor B: the pool hall
    x0, z0, x1, z1 = BHALL
    # west: the airlock door (7 m) and a door each side (the south one opens onto B's shield corridor)
    # (the side doors are in the north and south walls at x 59, facing out like the dome's angled ones: B walks about
    # as far from its south door to its shield corridor as A does from its north-east door)
    wall(k, "precastconcrete", "z", x0, x0 + T, z0, z1, 0, ROOF, [(0, 7, 0, 7)])
    wall(k, "precastconcrete", "z", x1 - T, x1, z0, z1, 0, ROOF)
    wall(k, "precastconcrete", "x", z0, z0 + T, x0 + T, x1 - T, 0, ROOF, [(59, 5, 0, 5)])
    wall(k, "precastconcrete", "x", z1 - T, z1, x0 + T, x1 - T, 0, ROOF, [(59, 5, 0, 5)])
    k.box("precastconcrete", x0, ROOF, z0, x1, ROOF + 0.5, z1)
    # the walkways at 4 m: one along z over the pool's middle (stairs at both ends), one along x into it
    k.box("meshcatwalk", 70.5, 3.7, -13, 73.5, 4, 13)
    k.box("meshcatwalk", 65, 3.7, -1.5, 70.5, 4, 1.5); k.box("meshcatwalk", 73.5, 3.7, -1.5, 79, 4, 1.5)
    k.stairs("steelcheckerfloor", 73.5, -13, 79.5, -10, 4, "x", -1)
    k.stairs("steelcheckerfloor", 73.5, 10, 79.5, 13, 4, "x", -1)
    # the bridge crane at 12 m (rails along the north and south walls, the bridge across)
    for zz in (z0 + T, z1 - T - 0.6):
        k.box("powdercoatyellow", x0 + T, 12, zz, x1 - T, 12.6, zz + 0.6)
    k.box("powdercoatyellow", 77, 12.6, z0 + T, 79, 13.4, z1 - T)

    # ---- the turbine hall
    x0, z0, x1, z1 = TURB
    wall(k, "boardformedconcrete", "z", x0 - T, x0, z0, z1, 0, ROOF, [(0, 7, 0, 7), (-13.5, 5, 0, 5), (12, 5, 0, 5)])
    wall(k, "boardformedconcrete", "z", x1, x1 + T, z0, z1, 0, ROOF, [(0, 7, 0, 7), (13.5, 5, 0, 5), (-12, 5, 0, 5)])
    wall(k, "boardformedconcrete", "x", z0 - T, z0, x0 - T, x1 + T, 0, ROOF, [(-16, 5, 0, 5), (16, 5, 0, 5), (-8, 4, 8, 12.5), (8, 4, 8, 12.5)])
    wall(k, "boardformedconcrete", "x", z1, z1 + T, x0 - T, x1 + T, 0, ROOF, [(-20, 5, 0, 5), (0, 6, 0, 6), (20, 5, 0, 5)])
    for t in tiles([(x0 - T, z0 - T, x1 + T, z1 + T)], 34): k.box("precastconcrete", t[0], ROOF, t[1], t[2], ROOF + 0.5, t[3])
    for xx in range(-24, 25, 8): k.box("galvanizedsteel", xx - 0.3, 13.6, z0, xx + 0.3, 14.2, z1)        # the roof trusses
    # the north gallery at 8 m (from the control room's upper floor)
    k.box("meshcatwalk", x0, 7.6, z0, x1, 8, z0 + 4)
    # (its rail, open at both ends onto a landing, and a stair from the hall floor up to each end: the overlook is
    # a short climb from the hall doors, not a detour through the control building)
    k.box("galvanizedsteel", x0 + 2.5, 8, z0 + 3.9, x1 - 2.5, 9.1, z0 + 4)
    for sx in (-1, 1):
        a, b = sorted((sx * 32, sx * 29.5))
        k.box("meshcatwalk", a, 7.6, z0 + 4, b, 8, z0 + 6.5)                                        # (the landing)
        sa, sb = sorted((sx * 29.5, sx * 19.9))
        k.stairs("steelcheckerfloor", sa, z0 + 4, sb, z0 + 6.5, 8, "x", -sx)

    # the turbine trains: 4 m casings, walkways on top, broken in the middle; stairs up at the outer ends
    for zc in (-7.5, 7.5):
        for (a, b) in ((-24, -2), (2, 24)):
            k.box("powdercoatgreen", a, 0, zc - 2.5, b, 4, zc + 2.5)
        k.stairs("steelcheckerfloor", -28, zc - 1.5, -24, zc + 1.5, 4, "x", 1)
        k.stairs("steelcheckerfloor", 24, zc - 1.5, 28, zc + 1.5, 4, "x", -1)
    # the exciter house at the crossing: breaks the line down the centre lane (door to door)
    k.box("powdercoatgreen", -3.5, 0, -3, 3.5, 4.5, 3)
    # the reactor approaches: four cover blocks each (transformers, a cask, a valve pit housing), point symmetric
    # (the hatch-side block 7 m out from each door: A's hatch face is at x -51, B's airlock at 54)
    for (x, z, w, d, h) in ((-44, 0, 3, 8, 3.0), (-38, 9, 4, 3, 2.2)):
        for sx in (1, -1):
            xx = sx * x + (3 if (sx > 0 and z == 0) else 0)
            k.box("electroplatednickel", xx - w / 2, 0, sx * z - d / 2, xx + w / 2, h, sx * z + d / 2)
    yards(m, k)

    # ---- the shield corridors: A's north-west (from the dome's north-east door), B's south-east
    for (xa, xb, za, zb, entry) in ((-52, -32, -16, -11, -52), (32, 52, 11, 16, 52)):
        k.box("castinplacewalls", xa, 0, za - 0.5, xb, 5.5, za); k.box("castinplacewalls", xa, 0, zb, xb, 5.5, zb + 0.5)
        k.box("castinplacewalls", xa, 5, za - 0.5, xb, 5.5, zb + 0.5)
        k.box("shieldwall", entry - 0.1 if entry < 0 else entry, 0, za, entry + 0.1 if entry < 0 else entry + 0.2, 5, zb)
        tx = -32 if entry < 0 else 32
        k.box("shieldwall", tx - 0.2 if entry > 0 else tx, 0, za, tx if entry > 0 else tx + 0.2, 5, zb)
    # A's corridor reaches back to 2 m off its dome door (the door faces north-east): an angled, roofed mouth, so A
    # is out of the yard as soon as B is (B's door is 2 m from its corridor's mouth)
    cx, cz = DOME_C
    nx, nz = math.cos(math.radians(300)), math.sin(math.radians(300))
    p1 = (cx + 15 * math.cos(math.radians(285)) + 2 * nx, cz + 15 * math.sin(math.radians(285)) + 2 * nz)
    p2 = (cx + 15 * math.cos(math.radians(315)) + 2 * nx, cz + 15 * math.sin(math.radians(315)) + 2 * nz)
    k.bar("castinplacewalls", p1, (-52, -16.25), 0.5, 0, 5.5)
    k.bar("castinplacewalls", p2, (-52, -10.75), 0.5, 0, 5.5)
    k.prism("castinplacewalls", [p1, (-52, -16.5), (-52, -10.5), p2], 5, 5.5)

    # ---- the intake yard: pumps (cover by the hall's south doors' sides), a pipe rack at 6 m with stairs at both ends
    for x in (-30, -14, 14, 30):
        k.box("powdercoatgreen", x - 1.5, 0, 25, x + 1.5, 2.4, 28)
        k.disc("electroplatednickel", x, 26.5, 0.9, 2.4, 3.4, n=8)
    k.box("meshcatwalk", -24, 5.7, 30, 24, 6, 32.5)
    k.box("galvanizedsteel", -24, 6, 32.4, 24, 7.0, 32.5)
    for x in range(-24, 25, 8):
        for zz in (30, 32.1): k.box("galvanizedsteel", x - 0.2, 0, zz, x + 0.2, 5.7, zz + 0.4)
    k.stairs("steelcheckerfloor", -32, 30, -24, 32.5, 6, "x", 1)
    k.stairs("steelcheckerfloor", 24, 30, 32, 32.5, 6, "x", -1)
    k.tube_x("oxidizedsteel", -40, 40, 6.8, 33.4, 0.7)                                                  # (the intake mains)
    k.tube_x("oxidizedsteel", -40, 40, 8.4, 33.4, 0.7)
    for x in (-38, 38): k.box("galvanizedsteel", x - 0.3, 0, 32.8, x + 0.3, 9.2, 34)

    # ---- the control building (two floors; the corridor loop round the offices; stairs at both ends)
    x0, z0, x1, z1 = CTRL
    wall(k, "precastconcrete", "z", x0 - T, x0, z0, z1 - T, 0, ROOF, [(-33, 5, 0, 5)])
    wall(k, "precastconcrete", "z", x1, x1 + T, z0, z1 - T, 0, ROOF, [(-33, 5, 0, 5)])
    for t in tiles([(x0 - T, z0, x1 + T, z1 - T)], 30): k.box("precastconcrete", t[0], ROOF, t[1], t[2], ROOF + 0.5, t[3])
    wells = [(-27.4, -42, -23.4, -24), (23.4, -42, 27.4, -24)]
    for t in minus((x0, z0, x1, z1 - T), wells): k.box("epoxyfloor", t[0], 7.6, t[1], t[2], 8, t[3])
    k.stairs("steelcheckerfloor", -27.4, -42, -23.4, -26, 8, "z", -1)
    k.stairs("steelcheckerfloor", 23.4, -42, 27.4, -26, 8, "z", -1)
    for (a, b) in ((-23.4, -23.2), (23.2, 23.4)):
        k.box("galvanizedsteel", a, 8, -42, b, 9.1, -24)                                                # (rails by the wells)
    # ground floor: the office block in the middle (doors on all four sides)
    wall(k, "stucco", "x", -38.3, -38, -14, 14, 0, 7.6, [(0, 4, 0, 3), (-9, 3, 0, 3)])
    wall(k, "stucco", "x", -28, -27.7, -14, 14, 0, 7.6, [(0, 4, 0, 3), (9, 3, 0, 3)])
    wall(k, "stucco", "z", -14.3, -14, -38, -28, 0, 7.6, [(-33, 3, 0, 3)])
    wall(k, "stucco", "z", 14, 14.3, -38, -28, 0, 7.6, [(-33, 3, 0, 3)])
    # upper floor: the control room, behind 1/8 m glass (shoot-through)
    for (x0g, z0g, x1g, z1g) in ((-14, -38, 14, -37.875), (-14, -28.125, -2, -28), (2, -28.125, 14, -28), (-14, -38, -13.875, -28), (13.875, -38, 14, -28)):
        k.box("glass", x0g, 8, z0g, x1g, 11, z1g)
    k.box("glass", -2, 10, -28.125, 2, 11, -28)


def yards(m, k):
    """the yards' set pieces, each a few big blocks of cover on a door-to-door line, point symmetric round (0, 0):
    a transformer bank (B's north yard, A's south), a cask yard (A's north, B's south), a cooling tower's base and a
    gantry crane on its rails in the corners behind the halls"""
    for sx in (1, -1):
        # transformers (3.4 m, radiator fins on both sides, bushings on top) on B's line NW door -> control east door
        for (x, z) in ((45, -21), (39.5, -27), (47, -32)):
            X, Z = sx * x, sx * z
            k.box("powdercoatgreen", X - 2, 0, Z - 1.5, X + 2, 3.4, Z + 1.5)
            for zz in (Z - 1.5, Z + 1.5):
                a, b = sorted((zz, zz - 0.4 if zz < Z else zz + 0.4))
                k.box("radiatorfins", X - 1.6, 0.4, a, X + 1.6, 3.0, b)
            for dx in (-1.1, 0, 1.1): k.disc("electroplatednickel", X + dx, Z, 0.22, 3.4, 4.4, n=6)
        # the cask yard: six spent-fuel casks (2.6 m) on a pad, 3.2 m apart
        for (x, z) in ((-36.5, -30.5), (-40, -30.5), (-43.5, -30.5), (-36.5, -34), (-40, -34), (-43.5, -34)):
            X, Z = sx * x, sx * z
            k.disc("electroplatednickel", X, Z, 1.15, 0, 2.6, n=8)
            k.disc("anodizedblack", X, Z, 1.2, 2.2, 2.4, n=8)
        a, b = sorted((sx * -34.5, sx * -45.5)); c, d = sorted((sx * -28.5, sx * -36))
        k.box("hazard", a, 0, c, b, 0.02, d)
        # the cooling tower's base (B north-east corner, A south-west): a ring 9 m round, 7 m tall, louvred
        X, Z = sx * 64, sx * -35
        k.disc("ribbedconcrete", X, Z, 8.5, 0, 7.0, n=12)
        k.ring("weatheredconcrete", X, Z, 7.6, 9.0, 7.0, 8.0, n=12)
        for i in range(12):
            t = math.radians(30 * i)
            r = 8.5 * math.cos(math.radians(15)) + 0.06
            k.bar("ventilationduct", (X + r * math.cos(t) - 1.2 * math.sin(t), Z + r * math.sin(t) + 1.2 * math.cos(t)),
                  (X + r * math.cos(t) + 1.2 * math.sin(t), Z + r * math.sin(t) - 1.2 * math.cos(t)), 0.12, 1.0, 5.5)
        # the gantry crane (A north-west corner, B south-east): rails along x, two legs, a beam at 9 m, a hook block
        X0, X1 = sorted((sx * -71, sx * -54))
        for z in (-27, -43):
            Z = sx * z
            k.box("railtrack", X0, 0, Z - 0.6, X1, 0.02, Z + 0.6)
            L = sx * -62
            k.box("powdercoatyellow", L - 0.7, 0, Z - 0.7, L + 0.7, 9, Z + 0.7)
        Za, Zb = sorted((sx * -27.7, sx * -42.3))
        k.box("powdercoatyellow", sx * -62 - 0.8, 9, Za, sx * -62 + 0.8, 10.2, Zb)
        k.box("anodizedblack", sx * -62 - 0.6, 6.0, sx * -36 - 0.6, sx * -62 + 0.6, 9.0, sx * -36 + 0.6)


def spawns(m, k):
    cx, cz = DOME_C
    for (x, z) in ((-56, -3), (-56, 3), (-59, -1.5), (-59, 1.5)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, z))
        k.spawn("b", 0.5 - x, 0, -z, yaw_to(0.5 - x, -z, 0, -z))     # (B 1.5 m nearer its airlock: the hall face is 3 m further out than the dome's)
    # team vs: its own sets at the turbine hall's ends (the whole plant is too big for a vs round's walk)
    for z in (-2.4, -0.8, 0.8, 2.4):
        k.spawn("vs-a", -29.5, 0, z, yaw_to(-29.5, z, 0, z * 3))
        k.spawn("vs-b", 29.5, 0, -z, yaw_to(29.5, -z, 0, -z * 3))
    # free for all (14): through the middle, both buildings, both reactors, the yards, the control room's floor
    for (x, y, z) in ((-20, 0, 0), (20, 0, 0), (-20, 0, -42), (20, 0, -24), (-38, 0, 22), (22, 0, 23), (-48, 0, -38),
                      (48, 0, 38), (-74, 0, -9), (70, 0, 8), (-8.5, 8, -41.5), (18, 8, -24), (-62, 0, -22), (62, 0, 22)):
        k.spawn("any", x, y, z, yaw_to(x, z, 0, 0))


def lights(m, k):
    cx, cz = DOME_C
    k.light(cx, 16, cz, 26, 0.9); k.light(cx + 9, 6, cz, 14, 0.6)
    for (x, z) in ((62, -8), (62, 8), (78, -8), (78, 8)): k.light(x, 10, z, 16, 0.7)
    for x in (-24, -8, 8, 24):
        for z in (-12, 12): k.light(x, 12, z, 16, 0.7)
    for x in (-20, 0, 20):
        k.light(x, 6, -24, 12, 0.6); k.light(x, 6, -42, 12, 0.6); k.light(x, 13, -33, 14, 0.7)
    for x in (-42, 42):
        k.light(x, 4, -13.5 if x < 0 else 13.5, 10, 0.6)


def decorate(m, k):
    """sparing, and off the lanes: signs and panels flush on walls, light strips high up, desks in the control room,
    the pools lit from below, lamp posts along the perimeter"""
    cx, cz = DOME_C
    # the dome: a light ring under the roof step, hazard bands round the vessel's base and the pool's edge
    k.ring("lightblock", cx, cz, 13.2, 13.45, 17.4, 17.7, n=12, a0=-15, skip=[0, 2, 10])
    k.ring("hazard", cx, cz, 3.8, 4.6, 0, 0.02, n=8)
    for a in (90, 180, 270):   # (radiation signs flush on the wall between the doors)
        t = math.radians(a); x, z = cx + 13.45 * math.cos(t), cz + 13.45 * math.sin(t)
        k.box("radiationwarning", x - 0.9 if a != 180 else x - 0.05, 2.4, z - 0.05 if a != 180 else z - 0.9, x + 0.9 if a != 180 else x + 0.05, 4.2, z + 0.05 if a != 180 else z + 0.9)
    for a in (45, 135, 225, 315):   # (pipes from the generators up into the roof)
        gx, gz = cx + 8.5 * math.cos(math.radians(a)), cz + 8.5 * math.sin(math.radians(a))
        k.disc("electroplatednickel", gx, gz, 0.6, 13, 21, n=8)
    # the pool hall: the pool lit from under the glow, a light strip along the walls up high, signs by the doors
    x0, z0, x1, z1 = POOL_B
    for (a, b, c, d) in minus((x0 + 0.95, z0 + 0.95, x1 - 0.95, z1 - 0.95), [(x0 + 1, z0 + 1, x1 - 1, z1 - 1)]):
        k.box("lightblock", a, -0.95, b, c, -0.6, d)                       # (strips round the pool's inner wall, under the water)
    for (x, z) in ((67, -6), (67, 6), (77, -6), (77, 6)): k.light(x, 1.0, z, 9, 0.9)
    for zz in (-15.35, 15.25): k.box("lightblock", 55, 10.6, zz, 83, 10.9, zz + 0.1)
    for zz in (-13, 13): k.box("radiationwarning", 54.6, 5.6, zz - 1, 54.65, 7.2, zz + 1)
    k.box("controlpanel", 81.8, 0, -4, 83.4, 1.1, 4)                                                       # (the reactor desk)
    # the turbine hall: hazard lines along the trains' feet, gauges flush on the casings, light strips on the trusses
    for zc in (-7.5, 7.5):
        for (a, b) in ((-24, -2), (2, 24)):
            for zz in (zc - 2.5, zc + 2.5): k.box("hazard", a, 0, zz - 0.3 if zz < zc else zz, b, 0.02, zz if zz < zc else zz + 0.3)
            for x in (a + 5, b - 5):
                for zz in (zc - 2.5, zc + 2.5): k.box("gaugecluster", x - 1, 1.6, zz - 0.04 if zz < zc else zz, x + 1, 2.6, zz if zz < zc else zz + 0.04)
    for xx in range(-24, 25, 8): k.box("lightblock", xx - 0.15, 13.45, -19, xx + 0.15, 13.6, 19)
    k.box("controlpanel", -3.5, 4.5, -3, 3.5, 4.6, 3)
    # the control room: desks along its north glass (crouch cover, 1 m), an amber light
    for x in (-10, -3, 4):
        k.box("controlpanel", x, 8, -37.6, x + 5, 9, -36.6)
    k.light(0, 10.5, -33, 12, 0.8)
    control_floor(m, k)
    facades(m, k)
    # the perimeter: radiation signs and lamp posts (against the wall, out of the yards' lanes)
    for (x, z) in ((-70, -45.9), (0, 45.9), (70, -45.9), (-70, 45.9), (70, 45.9)):
        k.box("radiationwarning", x - 1.5, 3, z - 0.05, x + 1.5, 5, z + 0.05)
    for x in (-60, -40, 40, 60):
        for zz in (-45.4, 45.0):
            k.box("galvanizedsteel", x - 0.2, 0, zz, x + 0.2, 7, zz + 0.4); k.box("lightblock", x - 0.5, 7, zz - 0.3, x + 0.5, 7.3, zz + 0.7)
            k.light(x, 6, zz + (1 if zz < 0 else -1), 14, 0.6)


def control_floor(m, k):
    """the control building's upper floor round the control room: a server room (east) and an engineers' office
    (west) on the north side, rows of desks between them, chest-high partitions by the gallery doors"""
    for sx in (1, -1):
        a, b = sorted((sx * 15.5, sx * 22.5))
        # the room: walls 0.3 m (solid, not wallbang), a 3 m door toward the middle, a ceiling at 11.5
        k.box("drywall", a, 8, -45.7, b, 11.5, -45.4)
        k.box("drywall", a, 8, -39.3, b, 11.5, -39.0)
        wa, wb = sorted((sx * 15.5, sx * 15.8))
        k.box("drywall", wa, 8, -45.4, wb, 11.5, -44.0); k.box("drywall", wa, 8, -41.0, wb, 11.5, -39.3)
        k.box("drywall", wa, 11.0, -44.0, wb, 11.5, -41.0)
        ea, eb = sorted((sx * 22.2, sx * 22.5))
        k.box("drywall", ea, 8, -45.4, eb, 11.5, -39.3)
        k.box("pressedtinceiling", a, 11.5, -45.7, b, 11.8, -39.0)
        if sx > 0:   # (servers in two rows)
            for zz in (-44.6, -40.2):
                k.box("serverrack", 17.0, 8, zz - 0.4, 21.5, 10.2, zz + 0.4)
            k.light(19, 10.8, -42.4, 7, 0.6)
        else:        # (an office: two desks, a filing wall)
            for x in (-17.5, -20.5):
                k.box("chipboard", x - 0.8, 8.7, -43.5, x + 0.8, 8.75, -42.3); k.box("chipboard", x - 0.75, 8, -43.45, x + 0.75, 8.7, -43.35)
            k.box("lockerdoors", -22.15, 8, -45.3, -21.6, 10.0, -40.0)
            k.light(-19, 10.8, -42.4, 7, 0.6)
    # desks in the north strip (x -12..12): two rows, 0.75 m (crouch cover), facing the control room
    for x in (-11, -6, 6, 11):
        k.box("chipboard", x - 1.6, 8.7, -44.4, x + 1.6, 8.75, -43.4)
        k.box("chipboard", x - 1.55, 8, -44.4, x + 1.55, 8.7, -44.3)
        k.box("controlpanel", x - 1.0, 8.75, -44.3, x + 1.0, 9.2, -44.0)
    # partitions (1.5 m, 0.2 m: solid) beside the gallery doors, off the door-to-door line
    for sx in (1, -1):
        a, b = sorted((sx * 12, sx * 17))
        k.box("officecarpettiles", a, 8, -24.6, b, 9.5, -24.4)


def facades(m, k):
    """the perimeter and the halls' outsides: pilasters every 12 m and a pipe run at 4 m along the perimeter,
    louvres and a sign band on the turbine hall, a band and louvres on the pool hall, a loading dock on the control
    building's west side"""
    for sz in (-1, 1):
        zw = sz * HZ
        for x in range(-72, 73, 12):
            a, b = sorted((zw, zw - sz * 0.6))
            k.box("precastconcrete", x - 0.6, 0, a, x + 0.6, WALL_TOP, b)
        a, b = sorted((zw - sz * 0.9, zw - sz * 0.6))
        k.tube_x("oxidizedsteel", -HX, HX, 4.2, zw - sz * 1.0, 0.35)
        k.tube_x("galvanizedsteel", -HX, HX, 5.2, zw - sz * 1.0, 0.25)
    for sx in (-1, 1):
        xw = sx * HX
        for (za, zb) in ((-HZ, -16), (16, HZ)):
            if sx < 0: za, zb = (-HZ, -13) if za < 0 else (13, HZ)
            for z in range(int(za) + 6, int(zb) - 2, 10):
                a, b = sorted((xw, xw - sx * 0.6))
                k.box("precastconcrete", a, 0, z - 0.6, b, WALL_TOP, z + 0.6)
    # the turbine hall: a sign band at 12 m and louvres high on its long faces
    x0, z0, x1, z1 = TURB
    for zz in (z0 - T, z1 + T):
        a, b = sorted((zz, zz - 0.06 if zz < 0 else zz + 0.06))
        k.box("sandblastedsign", -26, 11.5, a, 26, 12.7, b)
        for x in (-26, -14, 6, 18):
            if zz < 0 and x in (-14, 6): continue
            k.box("ventilationduct", x, 13.2, a, x + 8, 15.2, b)
    # the pool hall: a hazard band at 2 m and louvres along its north and south faces (outside)
    bx0, bz0, bx1, bz1 = BHALL
    for zz in (bz0, bz1):
        a, b = sorted((zz, zz - 0.06 if zz < 0 else zz + 0.06))
        k.box("hazard", bx0, 2.0, a, bx1, 2.3, b)
        for x in (58, 66, 74): k.box("ventilationduct", x, 10, a, x + 5, 12, b)
    # the loading dock: a 1.2 m platform on the control building's west face with a shutter flush on the wall
    k.box("concrete", -31.5, 0, -45, -28.6, 1.2, -38.5)
    k.box("rollershutter", -28.66, 0, -44, -28.6, 4.4, -39.5)
    k.box("hazard", -31.5, 1.2, -45, -31.3, 1.22, -38.5)


def build():
    m = M("Twin Reactors", "power", sun=(0.45, 1.0, 0.3))
    m.also = ["vs"]
    k = Kit(m)
    m.raw("palette base 0.62 0.62 0.60 accent 0.30 0.42 0.30 metal 0.30 0.31 0.33 glow 0.70 0.90 1.00 sky 0.62 0.70 0.80")
    layout(m, k)
    spawns(m, k)
    lights(m, k)
    decorate(m, k)
    m.raw("setting nuclear power plant")
    m.save("reactors", dress=False)
