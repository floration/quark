"""TWIN REACTORS (1.19.6): power (and team vs, FFA) in a nuclear plant, 168 x 92 m, two reactor halls of different
designs either side of a shared middle:
  A (west)  a pressurised-water reactor: a 12-sided containment dome 30 m tall (r 15), the vessel at its centre, four
            steam generators round it, the polar crane's ring girder and bridge at 22 m (a perch that only sees the
            dome), a refuelling pool of coolant (x1.3 speed) at the back. Three ways out: the equipment hatch (east,
            7 m), and doors north-east and south-east. Team A spawns inside the hatch.
  B (east)  a pool-type reactor: a low wide hall (flat roof at 16 m), an open Cherenkov pool (glowing tide, x0.9) with
            a cross of walkways over it at 4 m, the bridge crane at 12 m. Three ways out: the airlock door (west, 7 m),
            and doors north-west and south-west. Team B spawns inside the airlock door.
  middle    the turbine hall (64 x 40 m, roof trusses at 14 m to hook): two turbine trains, 4 m tall with walkways on
            top, east-west (the long walls to soak behind, the same casings for both teams), broken in the middle;
            a gallery at 8 m along the north wall. North of it the control building (two floors, a corridor loop round
            the offices, the control room upstairs behind 1/8 m glass: shoot-through, no bunker), south of it the
            cooling-water intake yard (pumps, a pipe rack at 6 m, a canal of clear water: not lethal).
  shield    each team has one 20 m regroup corridor from its reactor's side door to the turbine hall, a shield wall at
            both ends (stops shots, rockets and grapples; players and sight pass): A's north, B's south.
Power is about soaking: nobody dies of damage, so nothing here is lethal (no pits, no lava, no void inside the plant:
a fall would be a free 2 s trip home). The middle is point symmetric round (0, 0) except the control building (north)
and the intake (south), which both teams reach in the same time (x mirror). An invisible lid at 17.5 m covers the
plant outside the dome; inside the dome the roof is at 30 m."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, yaw_to

HX, HZ = 84, 46
LID = 17.5
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
    for (pool, liq) in ((POOL_A, "coolant"), (POOL_B, "glowingtide"), (CANAL, "clearlagoon")):
        x0, z0, x1, z1 = pool
        inner = (x0 + 1, z0 + 1, x1 - 1, z1 - 1)
        for (a, b, c, d) in minus(pool, [inner]): k.box("rivetedplates" if liq != "clearlagoon" else "boardformedconcrete", a, -1.5, b, c, -0.5, d)
        k.box("rivetedplates" if liq != "clearlagoon" else "driedriverbed", inner[0], -2, inner[1], inner[2], -1, inner[3])
        k.box(liq, x0, -1.0 if liq != "clearlagoon" else -1.0, z0, x1, -0.2, z1)

    # ---- the perimeter (up to the lid) and the lid; round the dome the lid is a ring, so nothing climbs its outside
    for (x0, z0, x1, z1) in ((-HX - 2, -HZ - 2, -HX, HZ + 2), (HX, -HZ - 2, HX + 2, HZ + 2), (-HX, -HZ - 2, HX, -HZ), (-HX, HZ, HX, HZ + 2)):
        k.box("precastconcrete", x0, -1, z0, x1, LID, z1)
    for (x0, z0, x1, z1) in minus((-HX, -HZ, HX, HZ), [(-81, -15, -51, 15)]): k.box("clip", x0, LID, z0, x1, LID + 1, z1)
    k.ring("clip", DOME_C[0], DOME_C[1], 15.0, 22.0, LID, LID + 1, n=12, a0=-15)

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
    k.ring("powdercoatyellow", cx, cz, 10.4, 11.5, 21.4, 22.2, n=12, a0=-15)
    k.box("powdercoatyellow", cx - 10.6, 21.6, cz - 1.2, cx + 10.6, 22.4, cz + 1.2)

    # ---- reactor B: the pool hall
    x0, z0, x1, z1 = BHALL
    # west: the airlock door (7 m) and a door each side (the south one opens onto B's shield corridor)
    wall(k, "precastconcrete", "z", x0, x0 + T, z0, z1, 0, ROOF, [(0, 7, 0, 7), (-13, 5, 0, 5), (13, 5, 0, 5)])
    wall(k, "precastconcrete", "z", x1 - T, x1, z0, z1, 0, ROOF)
    wall(k, "precastconcrete", "x", z0, z0 + T, x0 + T, x1 - T, 0, ROOF)
    wall(k, "precastconcrete", "x", z1 - T, z1, x0 + T, x1 - T, 0, ROOF)
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
    k.box("galvanizedsteel", x0, 8, z0 + 3.9, x1, 9.1, z0 + 4)
    # the turbine trains: 4 m casings, walkways on top, broken in the middle; stairs up at the outer ends
    for zc in (-7.5, 7.5):
        for (a, b) in ((-24, -2), (2, 24)):
            k.box("powdercoatgreen", a, 0, zc - 2.5, b, 4, zc + 2.5)
        k.stairs("steelcheckerfloor", -28, zc - 1.5, -24, zc + 1.5, 4, "x", 1)
        k.stairs("steelcheckerfloor", 24, zc - 1.5, 28, zc + 1.5, 4, "x", -1)
    # the exciter house at the crossing: breaks the line down the centre lane (door to door)
    k.box("powdercoatgreen", -3.5, 0, -3, 3.5, 4.5, 3)
    # the reactor approaches: four cover blocks each (transformers, a cask, a valve pit housing), point symmetric
    for (x, z, w, d, h) in ((-44, 0, 3, 8, 3.0), (-38, 9, 4, 3, 2.2), (-46, 28, 4, 4, 2.6), (-40, -32, 3, 5, 2.6)):
        for sx in (1, -1):
            k.box("electroplatednickel", sx * x - w / 2, 0, sx * z - d / 2, sx * x + w / 2, h, sx * z + d / 2)

    # ---- the shield corridors: A's north-west (from the dome's north-east door), B's south-east
    for (xa, xb, za, zb, entry) in ((-52, -32, -16, -11, -52), (32, 52, 11, 16, 52)):
        k.box("castinplacewalls", xa, 0, za - 0.5, xb, 5.5, za); k.box("castinplacewalls", xa, 0, zb, xb, 5.5, zb + 0.5)
        k.box("castinplacewalls", xa, 5, za - 0.5, xb, 5.5, zb + 0.5)
        k.box("shieldwall", entry - 0.1 if entry < 0 else entry, 0, za, entry + 0.1 if entry < 0 else entry + 0.2, 5, zb)
        tx = -32 if entry < 0 else 32
        k.box("shieldwall", tx - 0.2 if entry > 0 else tx, 0, za, tx if entry > 0 else tx + 0.2, 5, zb)

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


def spawns(m, k):
    cx, cz = DOME_C
    for (x, z) in ((-56, -3), (-56, 3), (-59, -1.5), (-59, 1.5)):
        k.spawn("a", x, 0, z, yaw_to(x, z, 0, z))
        k.spawn("b", 2 - x, 0, -z, yaw_to(2 - x, -z, 0, -z))
    # free for all: through the middle, both buildings and both yards
    for (x, z) in ((-20, 0), (20, 0), (-20, -42), (20, -24), (-38, 22), (22, 23), (-44, -30), (44, 30)):
        k.spawn("any", x, 0, z, yaw_to(x, z, 0, 0))


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
    k.box("lightblock", x0 + 2, -2.05, z0 + 2, x1 - 2, -2.0, z1 - 2)
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
    # the perimeter: radiation signs and lamp posts (against the wall, out of the yards' lanes)
    for (x, z) in ((-70, -45.9), (0, 45.9), (70, -45.9), (-70, 45.9), (70, 45.9)):
        k.box("radiationwarning", x - 1.5, 3, z - 0.05, x + 1.5, 5, z + 0.05)
    for x in (-60, -40, 40, 60):
        for zz in (-45.4, 45.0):
            k.box("galvanizedsteel", x - 0.2, 0, zz, x + 0.2, 7, zz + 0.4); k.box("lightblock", x - 0.5, 7, zz - 0.3, x + 0.5, 7.3, zz + 0.7)
            k.light(x, 6, zz + (1 if zz < 0 else -1), 14, 0.6)


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
