"""QK5 ISLANDS: ranked 1v1 (vanilla), box and accent only, point-symmetric. Eleven floating platforms from 0 to 30 m
over a deep void, 130 x 130 m: the most Aerowalk-like of the qk maps. The platforms are chained by walkable ramps
(two at most per platform: corner spawn platform -> mid -> the sheltered centre -> mid -> the other corner, and a branch
up from each corner to its high platform); every other crossing is a jump, a dash (gaps of 6-12 m) or the grapple (anchor
blocks under the lid at 37-40 m). Knockback kills. Power spots: the two high platforms (30 m), each with one walkable
link, and the sheltered centre (the lowest)."""
from .prefabs import M
from .m2kit import B, island, light, mirror_all


def build():
    m = M("QK5 Islands", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead)
    m.raw("ranked vanilla")
    m.also = ["duel", "vs", "tourney"]
    m.raw("void -20")
    start = len(m.lines)
    # ---- half the map (the rest is its point mirror): the centre is its own twin
    island(m, 0, 0, 20, 20, 0, depth=10)                       # C: the centre, the lowest
    island(m, -34, 8, 12, 12, 5)                               # P4: mid
    island(m, -46, 40, 18, 18, 10, depth=12)                   # P1: the corner spawn platform
    island(m, -20, 49, 10, 10, 18)                             # P5
    island(m, 20, 48, 14, 14, 24)                              # P2
    island(m, 46, 40, 12, 12, 30, depth=14)                    # P3: the high platform (one walkable link)
    # walkable links (ramps): C - P4 - P1 - P5 - P2 - P3
    # (flights of slabs in the air, steps of 0.5 m: the walk checks and the bots walk boxes, not slopes)
    m.stairs(-28, 3, -10, 7, 5, "x", -1, solid=False)
    m.stairs(-40, 14, -37, 31, 10, "z", 1, base=5, solid=False)
    m.stairs(-37, 45, -25, 49, 18, "x", 1, base=10, solid=False)
    m.stairs(-15, 46, 13, 50, 24, "x", 1, base=18, solid=False)
    m.stairs(27, 42, 40, 46, 30, "x", 1, base=24, solid=False)
    # the centre's shelter: four pillars and a roof (a perch you hook onto), cover on its floor
    B(m, "accent", 7, 0, 7, 8.4, 5, 8.4); B(m, "accent", 7, 0, -8.4, 8.4, 5, -7)
    B(m, "box", -8.5, 5, -8.5, 8.5, 5.6, 8.5); B(m, "accent", -7.5, 5.6, -7.5, 7.5, 12.5, 7.5); B(m, "box", -8, 12.5, -8, 8, 13, 8)   # (the canopy block: no line from corner to corner)
    B(m, "box", 2.5, 0, -5, 5.5, 1.2, -3.8)
    # cover: a low wall on each spawn platform, a block on the mid platform, a parapet on the high one
    B(m, "accent", -42, 10, 33, -36.5, 11.3, 34.2); B(m, "box", -36, 5, 4, -34, 6.6, 6)
    B(m, "accent", 41, 30, 35, 43, 31.2, 39); B(m, "box", 18, 24, 50, 22, 25.1, 51.2)
    # grapple anchors: blocks hung under the lid (tops against it: nobody stands on them)
    for (x, z) in ((-40, 0), (-20, 22), (0, 30), (22, 12), (40, 20), (-60, 28), (8, 62), (-6, 46), (30, 30), (56, 4)):
        B(m, "accent", x - 1, 37, z - 1, x + 1, 40, z + 1)
    # spawns: four on the corner platform (team a), the free-for-all ones in pairs
    for (x, z) in ((-50, 44), (-42, 44), (-50, 36), (-42, 37.5)): m.spawn("a", x, 10, z, -49)
    for (x, y, z, yaw) in ((-20, 18, 50, -90), (20, 24, 45, 90), (-3, 0, -3, 135), (48, 30, 42, 120)):
        m.spawn("any", x, y, z, yaw)
    # light over each platform
    for (x, y, z, r) in ((0, 9, 0, 16), (-34, 11, 8, 12), (-46, 16, 40, 16), (-20, 23, 49, 10), (20, 29, 48, 12), (46, 35, 40, 11)):
        light(m, x, y, z, r, 0.8)
    mirror_all(m, start)
    # bounds: a lid at 40 m and walls all round (players only: shots and hooks pass)
    B(m, "clip", -68, 40, -68, 68, 41, 68)
    for (a, b, c, d) in ((-68, -68, 68, -66), (-68, 66, 68, 68), (-68, -66, -66, 66), (66, -66, 68, 66)):
        B(m, "clip", a, -25, b, c, 40, d)
    m.raw("palette base 0.7 0.72 0.76 accent 0.16 0.42 0.7 metal 0.3 0.32 0.36 glow 0.5 0.85 1 sky 0.12 0.16 0.26")
    m.raw("setting floating islands over the void")
    m.save("qk5", dress=False)
