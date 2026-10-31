"""QK6 CROSSROADS: ranked 1v1 (vanilla) and ranked instagib free for all, box and accent only, point-symmetric.
A plus-shaped plan 84 x 84 m: four arms 10 m wide, each two corridors high (0 and 8 m), meet in a central atrium
26 x 26 m and 26 m tall round a 10 x 10 m column (floor to 15 m: no line down an arm through the atrium, on either
level) with a cluster of blocks hanging at 16-22 m (grapple anchors and cover in the air; its top block, 22 m, is the
power spot: it sees everything and everything sees it). Each arm is broken once on each level by 2.2 m cover;
the W/E arms carry a low accent band, the N/S arms a high one, and the team's own corner rooms (NW, SE) a floor band and
the shared ones (NE, SW) a ceiling band, so you know where you are. Every corner room has a dash-bounce corner (45
degree walls) on both floors. Stairs at each arm's end join its two corridors, a hole near the atrium drops from the
upper to the lower; a corner room between each pair of arms joins them on both levels, so there are loops that never
touch the atrium. A ring balcony at 8 m runs round the atrium. Spawns: two at each arm's end.
(1.19.6 round 3, q: "qk6 is too big for a 1v1 map": 128 x 128 m cut to 84 x 84 m, the arms 46 m to 26 m long and
12 m to 10 m wide, the atrium 36 m to 26 m, the storeys 10 m to 8 m apart, the corner rooms 18 m to 14 m; walkable
floor about 9,100 to about 4,300 m2, the nearest enemy spawn about 4.6 s to about 2.9 s away.)"""
from .prefabs import M, minus
from .m2kit import B, light, mirror_all, Rooms, prism

AR, AH, AW = 13.0, 26.0, 5.0      # atrium half size (interior), height; arm half width (and the column's half size)
UF, CH = 8.0, 15.0                # upper floor top, arm ceiling
E = 40.0                          # arm end
RS = 14.0                         # corner room size
RN, RF = -20.0 - RS, -20.0        # NW room's near/far edges (x and z: -34..-20)


def build():
    m = M("QK6 Crossroads", "1v1", sun=(0, 1, 0))
    m.raw("ranked vanilla instagib")
    m.also = ["duel", "vs", "tourney"]
    start = len(m.lines)
    R = Rooms(m)
    p0, p1 = -30.0, -25.0                             # the passages' 5 m width (between the arm walls and the rooms)
    # ---- doors (both levels): arms into the atrium, corner passages
    for lv in (0.0, UF):
        h = 6.5 if lv == 0 else 6.0
        R.door(-AR - 1.5, lv, -AW, -AR + 0.5, lv + h, AW)                       # W arm -> atrium
        R.door(-AW, lv, -AR - 1.5, AW, lv + h, -AR + 0.5)                       # N arm -> atrium
        R.door(p0, lv, -AW - 1.5, p1, lv + 6, -AW + 0.5)                        # W arm north wall -> NW passage
        R.door(p0, lv, AW - 0.5, p1, lv + 6, AW + 1.5)                          # W arm south wall -> SW passage (the mirror of NE's)
        R.door(p0, lv, RF - 0.5, p1, lv + 6, RF + 1.5)                          # NW passage -> NW room
        R.door(-AW - 1.5, lv, p0, -AW + 0.5, lv + 6, p1)                        # N arm west wall -> NW passage 2
        R.door(RF - 0.5, lv, p0, RF + 1.5, lv + 6, p1)                          # NW passage 2 -> NW room
        R.door(AW - 0.5, lv, p0, AW + 1.5, lv + 6, p1)                          # N arm east wall -> NE passage
        R.door(-RF - 1.5, lv, p0, -RF + 0.5, lv + 6, p1)                        # NE passage -> NE room
        R.door(-p1, lv, RF - 0.5, -p0, lv + 6, RF + 1.5)                        # NE room -> NE passage 2 (down to the E arm)
        R.door(-p1, lv, -AW - 1.5, -p0, lv + 6, -AW + 0.5)                      # NE passage 2 -> E arm north wall
    # ---- the atrium (its W and N walls; E and S are their mirror), its roof, the ring balcony
    R.slab_wall("box", -AR - 1, -1, -AR - 1, -AR, AH + 1, AR + 1)
    R.slab_wall("box", -AR, -1, -AR - 1, AR, AH + 1, -AR)
    B(m, "box", -AR, -1, -AR, AR, 0, AR); B(m, "box", -AR - 1, AH, -AR - 1, AR + 1, AH + 1, AR + 1)
    B(m, "accent", -AR, UF - 0.6, -AR, -AR + 3, UF, AR); B(m, "accent", -AR + 3, UF - 0.6, -AR, AR - 3, UF, -AR + 3)
    # the column in the middle (the full door width, through the balcony level: no line down an arm through the atrium),
    # the hanging cluster
    B(m, "box", -AW, 0, -AW, AW, 15, AW); B(m, "accent", -AW - 0.3, 14.6, -AW - 0.3, AW + 0.3, 15, AW + 0.3)
    B(m, "accent", -3, 19, -3, 3, 22, 3); B(m, "accent", -0.25, 22, -0.25, 0.25, AH, 0.25)
    for (x, z, y0, y1, r) in ((-8, 0, 16.5, 18, 1.3), (0, -8, 17, 19, 1.1), (-6.5, -6.5, 20, 21.2, 0.9)):
        B(m, "accent", x - r, y0, z - r, x + r, y1, z + r); B(m, "accent", x - 0.15, y1, z - 0.15, x + 0.15, AH, z + 0.15)
    # ---- the W arm and the N arm (E and S are their mirror): two corridors, stairs at the end, a hole near the atrium
    R.room(-E, -AW, -AR - 1, AW, 0, CH, walls="nsw")
    R.room(-AW, -E, AW, -AR - 1, 0, CH, walls="new")
    # (the drop hole against the far wall from the stairs: the fast line down the middle stays clear)
    for (rect, hole, st) in (((-E, -AW, -AR - 1, AW), (-22, 1, -17, AW), (-E, -AW, -E + 16, -AW + 3, "x")),
                             ((-AW, -E, AW, -AR - 1), (1, -22, AW, -17), (-AW, -E, -AW + 3, -E + 16, "z"))):
        for r in minus(rect, [hole, st[:4]]): B(m, "accent", r[0], UF - 1, r[1], r[2], UF, r[3])
        m.stairs(st[0], st[1], st[2], st[3], UF, st[4], 1)
    # ---- the NW and NE corner rooms (their mirrors are SE and SW), two floors each, and their passages
    for (x0, z0) in ((RN, RN), (-RF, RN)):
        x1, z1 = x0 + RS, z0 + RS
        R.room(x0, z0, x1, z1, 0, CH)
        stair = (x0 + 1.5, z0, x0 + 12, z0 + 4) if x0 < 0 else (x1 - 12, z0, x1 - 1.5, z0 + 4)   # (clear of the end wall: a run-up)
        mid = (x0 + 5, z0 + 6.5, x0 + 10, z0 + 11.5) if x0 < 0 else (x1 - 10, z0 + 6.5, x1 - 5, z0 + 11.5)
        for r in minus((x0, z0, x1, z1), [stair, mid]): B(m, "accent", r[0], UF - 1, r[1], r[2], UF, r[3])
        m.stairs(stair[0], stair[1], stair[2], stair[3], UF, "x", 1 if x0 < 0 else -1)
        B(m, "box", x1 - 4, 0, z0 + 6, x1 - 2, 1.4, z0 + 8) if x0 < 0 else B(m, "box", x0 + 2, 0, z0 + 6, x0 + 4, 1.4, z0 + 8)
    for (x0, z0, x1, z1) in ((p0, RF + 1, p1, -AW - 1), (RF + 1, p0, -AW - 1, p1), (AW + 1, p0, -RF - 1, p1), (-p1, RF + 1, -p0, -AW - 1)):
        R.room(x0, z0, x1, z1, 0, CH, walls="nsew")
        B(m, "accent", x0, UF - 1, z0, x1, UF, z1)
    R.build()
    # cover in the arms (2.2 m: a standing player fits behind): one piece per level, on opposite sides
    B(m, "box", -21, 0, -AW, -19, 2.2, -2); B(m, "box", -32, UF, 2, -30, UF + 2.2, AW)          # W arm
    B(m, "box", 2, 0, -21, AW, 2.2, -19); B(m, "box", -AW, UF, -32, -2, UF + 2.2, -30)          # N arm
    # landmarks: an accent band low in the W arm, high in the N arm (their twins E and S the same)
    for zz in (-AW, AW - 0.1):
        B(m, "accent", -E, 3, zz, -AR - 1, 3.6, zz + 0.1); B(m, "accent", zz, UF + 3.5, -E, zz + 0.1, UF + 4.1, -AR - 1)
    # the corner rooms: 45 degree walls across their outer corners on both floors (dash-bounce corners); the team's own
    # room (NW) a floor band, the shared one (NE) a ceiling band
    for (cx, cz, sx, sz, own) in ((RN, RF, 1, -1, True), (-RN, RF, -1, -1, False)):   # (the corner away from the stairs)
        tri = [(cx, cz), (cx + sx * 4, cz), (cx, cz + sz * 4)]
        for (y0, y1) in ((0, UF - 1), (UF, CH)): prism(m, "accent" if own else "box", tri, y0, y1)
        x0, x1 = sorted((cx, cx + sx * RS)); z0, z1 = sorted((cz, cz + sz * RS))
        if own: B(m, "accent", x0, 0, z1 - 0.6, x1, 0.05, z1); B(m, "accent", x0, UF, z1 - 0.6, x1, UF + 0.05, z1)
        else: B(m, "accent", x0, UF - 1.6, z1 - 0.6, x1, UF - 1, z1); B(m, "accent", x0, CH - 0.6, z1 - 0.6, x1, CH, z1)
    # ---- spawns: two at the W arm's end and two at the N arm's end (team a; b is the mirror), free-for-all pairs
    for z in (0.5, 3.5): m.spawn("a", -37, 0, z, -90)
    for x in (0.5, 3.5): m.spawn("a", x, 0, -37, 180)
    for (x, y, z, yaw) in ((-25, 0, -27, 135), (-22.5, UF, -24, 180), (29, 0, -27, -135), (-26, UF, 3.5, -90)):
        m.spawn("any", x, y, z, yaw)
    # ---- lights
    for x in (-36, -27, -19):
        light(m, x, 5, 0, 15, 1.0); light(m, x, UF + 4.5, 0, 15, 1.0)
    for z in (-36, -27, -19):
        light(m, 0, 5, z, 15, 1.0); light(m, 0, UF + 4.5, z, 15, 1.0)
    for (x, z) in ((-27, -27), (27, -27)): light(m, x, 5, z, 14, 1.0); light(m, x, UF + 4.5, z, 14, 1.0)
    for (x, z) in ((-27.5, -12.5), (-12.5, -27.5), (12.5, -27.5), (27.5, -12.5)): light(m, x, 4, z, 9, 0.8)   # the passages
    light(m, -9, 6, -9, 16, 1.0); light(m, 9, 6, -9, 16, 1.0); light(m, 8, 23, -8, 18, 0.9)
    mirror_all(m, start)
    m.raw("palette base 0.66 0.66 0.7 accent 0.6 0.3 0.12 metal 0.3 0.3 0.32 glow 1 0.6 0.3 sky 0.5 0.55 0.62")
    m.raw("setting crossroads of halls")
    m.save("qk6", dress=False)
