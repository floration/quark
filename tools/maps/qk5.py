"""QK5 ISLANDS: ranked 1v1 (vanilla), box and accent only, point-symmetric. Fifteen floating platforms from 0 to 20 m
over a deep void, 130 x 130 m: the most Aerowalk-like of the qk maps. A walkable chain (spawn platform -> mid -> the
centre -> the other side, and up from the spawn platform over two more to the top of its arm) with shortcuts off it:
two low stepping islands per half sit 9-11 m from their neighbours (a jump down, a dash up), so each half has a loop
(the arm's top -> a drop onto a stepping island -> a drop onto the centre -> back up the chain). The two high power
platforms (18 m) hang east and west of the centre, each up its own ramp from the centre's floor: both players reach
both the same way, so they're contested, not one nest per player. Every platform has a piece of cover a standing
player fits behind (2.2 m or more). Grapple anchors hang from the lid on a regular grid. Knockback kills."""
from .prefabs import M
from .m2kit import B, island, light, mirror_all, ramp


def build():
    m = M("QK5 Islands", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead)
    m.raw("ranked vanilla")
    m.also = ["duel", "vs", "tourney"]
    m.raw("void -20")
    start = len(m.lines)
    # ---- half the map (the rest is its point mirror): the centre is its own twin
    island(m, 0, 0, 20, 20, 0, depth=10)                       # C: the centre, the lowest
    island(m, -30, 6, 12, 12, 4, depth=7)                      # P4: mid (14 m from C, up a flight)
    island(m, -42, 34, 16, 16, 8, depth=13)                    # P1: the corner spawn platform
    island(m, -16, 24, 6, 6, 4, depth=4)                       # S1: a stepping island (a 10 m jump level with P4; 11 m from C)
    island(m, -20, 44, 10, 10, 14, depth=8)                    # P5
    island(m, 8, 44, 12, 12, 20, depth=11)                     # P2: the top of the arm
    island(m, 10, 24, 6, 6, 10, depth=5)                       # S2: a stepping island (11 m under P2, 11 m over C)
    island(m, 40, 11, 10, 10, 18, depth=15)                    # P3: a power platform, up its ramp from the centre
    # walkable links: C - P4 - P1 - P5 - P2 (flights of slabs in the air: the walk checks and the bots walk them) and
    # C - P3 (a ramp, 36 degrees)
    m.stairs(-24, 4, -10, 8, 4, "x", -1, solid=False)
    m.stairs(-38, 12, -34, 26, 8, "z", 1, base=4, solid=False)
    m.stairs(-34, 39, -25, 42, 14, "x", 1, base=8, solid=False)
    m.stairs(-15, 42, 2, 46, 20, "x", 1, base=14, solid=False)
    ramp(m, "accent", 10, 7, 35, 11, "x", 0, 18)
    # the centre's shelter: four posts and a stepped roof (a perch you hook onto; its top blocks the corner to corner
    # line over the centre)
    B(m, "accent", 7, 0, 7, 8.4, 5, 8.4); B(m, "accent", 7, 0, -8.4, 8.4, 5, -7)
    B(m, "box", -8.5, 5, -8.5, 8.5, 5.6, 8.5)
    B(m, "accent", -7.5, 5.6, -7.5, 7.5, 8, 7.5); B(m, "box", -5.5, 8, -5.5, 5.5, 10.5, 5.5); B(m, "accent", -3.5, 10.5, -3.5, 3.5, 12.5, 3.5)
    # cover a standing player fits behind (2.2 m or more), one piece per platform
    B(m, "box", 2.5, 0, -5, 5.5, 2.4, -3.8)                     # C
    B(m, "box", -32, 4, 4, -29.6, 6.4, 6.4)                     # P4
    B(m, "accent", -50, 8, 26, -38.5, 13.5, 27.2)                 # P1: a tall wall on its side toward P3' (no line from it)
    B(m, "box", -14.4, 4, 25.2, -13.2, 6.4, 26.4)               # S1
    B(m, "box", -22.5, 14, 46, -20, 16.4, 47.4)                 # P5
    B(m, "box", 5, 20, 47, 9, 22.4, 48.2)                       # P2
    B(m, "box", 11.8, 10, 21.6, 13, 12.4, 22.8)                 # S2
    B(m, "accent", 38, 18, 12.5, 40.5, 20.4, 14.5)              # P3
    # spawns: four on the corner platform (team a), the free-for-all ones on the walkable chain (P5, P2, P4, beside the centre's shelter)
    for (x, z) in ((-49, 41), (-43, 41), (-49, 35), (-43, 35)): m.spawn("a", x, 8, z, -49)
    for (x, y, z, yaw) in ((-18, 14, 41, -90), (6, 20, 41, 90), (-27, 4, 9, -90), (9.5, 0, -3, 90)):
        m.spawn("any", x, y, z, yaw)
    # light over each platform
    for (x, y, z, r) in ((0, 9, 0, 16), (-30, 10, 6, 12), (-42, 15, 34, 16), (-16, 9, 24, 8), (-20, 20, 44, 10), (8, 26, 44, 12),
                         (10, 15, 24, 8), (40, 24, 11, 11), (22, 12, 9, 10)):
        light(m, x, y, z, r, 0.8)
    mirror_all(m, start)
    # grapple anchors: a regular grid of blocks hung from the lid on rods (their tops against it: nobody stands on them)
    for x in (-39, -13, 13, 39):
        for z in (-39, -13, 13, 39):
            B(m, "accent", x - 1, 35, z - 1, x + 1, 37, z + 1); B(m, "box", x - 0.2, 37, z - 0.2, x + 0.2, 40, z + 0.2)
    # bounds: a lid at 40 m and walls all round (players only: shots and hooks pass)
    B(m, "clip", -68, 40, -68, 68, 41, 68)
    for (a, b, c, d) in ((-68, -68, 68, -66), (-68, 66, 68, 68), (-68, -66, -66, 66), (66, -66, 68, 66)):
        B(m, "clip", a, -25, b, c, 40, d)
    m.raw("palette base 0.7 0.72 0.76 accent 0.16 0.42 0.7 metal 0.3 0.32 0.36 glow 0.5 0.85 1 sky 0.12 0.16 0.26")
    m.raw("setting floating islands over the void")
    m.save("qk5", dress=False)
