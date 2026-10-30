"""QK6 CROSSROADS: ranked 1v1 (vanilla) and ranked instagib free for all, box and accent only, point-symmetric.
A plus-shaped plan 128 x 128 m: four arms 12 m wide, each two corridors high (0 and 10 m), meet in a central atrium
36 x 36 m and 32 m tall with a cluster of blocks hanging at 16-24 m (grapple anchors and cover in the air; its top block,
24 m, is the power spot: it sees everything and everything sees it). Stairs at each arm's end join its two corridors,
a hole near the atrium drops from the upper to the lower; a corner room between each pair of arms joins them on both
levels, so there are loops that never touch the atrium. A ring balcony at 10 m runs round the atrium. Spawns: two at
each arm's end."""
from .prefabs import M, minus
from .m2kit import B, light, mirror_all, Rooms

AR, AH, AW = 18.0, 32.0, 6.0      # atrium half size, height; arm half width
UF, CH = 10.0, 18.0               # upper floor top, arm ceiling


def build():
    m = M("QK6 Crossroads", "1v1", sun=(0, 1, 0))
    m.raw("ranked vanilla instagib")
    m.also = ["duel", "vs", "tourney"]
    start = len(m.lines)
    R = Rooms(m)
    E = 64.0                                          # arm end
    # ---- doors (both levels): arms into the atrium, corner passages
    for lv in (0.0, UF):
        h = 8.0 if lv == 0 else 7.0
        R.door(-AR - 1.5, lv, -AW, -AR + 0.5, lv + h, AW)                       # W arm -> atrium
        R.door(-AW, lv, -AR - 1.5, AW, lv + h, -AR + 0.5)                       # N arm -> atrium
        R.door(-36, lv, -AW - 1.5, -30, lv + 6, -AW + 0.5)                      # W arm north wall -> NW passage
        R.door(-36, lv, AW - 0.5, -30, lv + 6, AW + 1.5)                        # W arm south wall -> SW passage (the mirror of NE's)
        R.door(-36, lv, -24.5, -30, lv + 6, -22.5)                              # NW passage -> NW room
        R.door(-AW - 1.5, lv, -36, -AW + 0.5, lv + 6, -30)                      # N arm west wall -> NW passage 2
        R.door(-24.5, lv, -36, -22.5, lv + 6, -30)                              # NW passage 2 -> NW room
        R.door(AW - 0.5, lv, -36, AW + 1.5, lv + 6, -30)                        # N arm east wall -> NE passage
        R.door(22.5, lv, -36, 24.5, lv + 6, -30)                                # NE passage -> NE room
        R.door(30, lv, -24.5, 36, lv + 6, -22.5)                                # NE room -> NE passage 2 (down to the E arm)
        R.door(30, lv, -AW - 1.5, 36, lv + 6, -AW + 0.5)                        # NE passage 2 -> E arm north wall
    # ---- the atrium (its W and N walls; E and S are their mirror), its roof, the ring balcony
    R.slab_wall("box", -AR - 1, -1, -AR - 1, -AR, AH + 1, AR + 1)
    R.slab_wall("box", -AR, -1, -AR - 1, AR, AH + 1, -AR)
    B(m, "box", -AR, -1, -AR, AR, 0, AR); B(m, "box", -AR - 1, AH, -AR - 1, AR + 1, AH + 1, AR + 1)
    B(m, "accent", -AR, UF - 0.6, -AR, -AR + 3, UF, AR); B(m, "accent", -AR + 3, UF - 0.6, -AR, AR - 3, UF, -AR + 3)
    # the block in the middle of its floor (no line down an arm through the atrium), the hanging cluster
    B(m, "box", -4, 0, -4, 4, 6, 4)
    B(m, "accent", -3, 20, -3, 3, 24, 3); B(m, "accent", -0.25, 24, -0.25, 0.25, AH, 0.25)
    for (x, z, y0, y1, r) in ((-9, 0, 16, 18, 1.5), (0, -9, 17, 19.5, 1.25), (-7, -7, 21, 22.5, 1.0)):
        B(m, "accent", x - r, y0, z - r, x + r, y1, z + r); B(m, "accent", x - 0.15, y1, z - 0.15, x + 0.15, AH, z + 0.15)
    # ---- the W arm and the N arm (E and S are their mirror): two corridors, stairs at the end, a hole near the atrium
    R.room(-E, -AW, -AR - 1, AW, 0, CH, walls="nsw")
    R.room(-AW, -E, AW, -AR - 1, 0, CH, walls="new")
    for (rect, hole, st) in (((-E, -AW, -AR - 1, AW), (-28, -3, -22, 3), (-E, -AW, -E + 20, -AW + 4, "x")),
                             ((-AW, -E, AW, -AR - 1), (-3, -28, 3, -22), (-AW, -E, -AW + 4, -E + 20, "z"))):
        for r in minus(rect, [hole, st[:4]]): B(m, "accent", r[0], UF - 1, r[1], r[2], UF, r[3])
        m.stairs(st[0], st[1], st[2], st[3], UF, st[4], 1)
    # ---- the NW and NE corner rooms (their mirrors are SE and SW), two floors each, and their passages
    for (x0, z0) in ((-42, -42), (24, -42)):
        x1, z1 = x0 + 18, z0 + 18
        R.room(x0, z0, x1, z1, 0, CH)
        sx0 = x0 if x0 < 0 else x1 - 4
        stair = (x0 + 3, z0, x0 + 16, z0 + 4) if x0 < 0 else (x1 - 16, z0, x1 - 3, z0 + 4)   # (clear of the end wall: a run-up)
        mid = (x0 + 6, z0 + 7, x0 + 12, z0 + 13)
        for r in minus((x0, z0, x1, z1), [stair, mid]): B(m, "accent", r[0], UF - 1, r[1], r[2], UF, r[3])
        m.stairs(stair[0], stair[1], stair[2], stair[3], UF, "x", 1 if x0 < 0 else -1)
        B(m, "box", x0 + 13, 0, z0 + 13, x0 + 15, 1.4, z0 + 15) if x0 < 0 else B(m, "box", x0 + 3, 0, z0 + 13, x0 + 5, 1.4, z0 + 15)
    for (x0, z0, x1, z1) in ((-36, -23, -30, -AW - 1), (-23, -36, -AW - 1, -30), (AW + 1, -36, 23, -30), (30, -23, 36, -AW - 1)):
        R.room(x0, z0, x1, z1, 0, CH, walls="nsew")
        for r in [(x0, z0, x1, z1)]: B(m, "accent", r[0], UF - 1, r[1], r[2], UF, r[3])
    R.build()
    # cover in the arms
    B(m, "box", -48, 0, 2, -46, 1.4, 5); B(m, "box", -2, UF, -50, 1, UF + 1.2, -48)
    # ---- spawns: two at the W arm's end and two at the N arm's end (team a; b is the mirror), free-for-all pairs
    for z in (1.0, 4.5): m.spawn("a", -59, 0, z, -90)
    for x in (1.0, 4.5): m.spawn("a", x, 0, -59, 180)
    for (x, y, z, yaw) in ((-30, 0, -36, 135), (-38, UF, -28, 180), (33, 0, -33, -135), (-40, UF, 3, -90)):
        m.spawn("any", x, y, z, yaw)
    # ---- lights
    for x in (-56, -40, -26):
        light(m, x, 6.5, 0, 12, 0.8); light(m, x, UF + 6.5, 0, 12, 0.7)
    for z in (-56, -40, -26):
        light(m, 0, 6.5, z, 12, 0.8); light(m, 0, UF + 6.5, z, 12, 0.7)
    for (x, z) in ((-33, -33), (33, -33)): light(m, x, 6.5, z, 14, 0.8); light(m, x, UF + 6.5, z, 14, 0.7)
    light(m, -10, 8, -10, 18, 0.8); light(m, 10, 28, -10, 20, 0.8)
    mirror_all(m, start)
    m.raw("palette base 0.66 0.66 0.7 accent 0.6 0.3 0.12 metal 0.3 0.3 0.32 glow 1 0.6 0.3 sky 0.5 0.55 0.62")
    m.raw("setting crossroads of halls")
    m.save("qk6", dress=False)
