"""BASTION: every mode (built for vs). A castle: a thick curtain wall you can walk along the top of, square
towers jutting out at every corner and halfway along the flanks, a gatehouse at each end (both teams start
inside one, the portcullis shut behind them), an inner ward wall with gates, and the keep in the middle on
an island in a moat with four drawbridges. Outside: a water moat and fields under the mountains."""
from .prefabs import *


def build():
    m = M("Bastion", "vs")
    moat = [(-10, -10, -1.5, -5, "water"), (1.5, -10, 10, -5, "water"), (-10, 5, -1.5, 10, "water"), (1.5, 5, 10, 10, "water"),
            (-10, -5, -5, -1.5, "water"), (-10, 1.5, -5, 5, "water"), (5, -5, 10, -1.5, "water"), (5, 1.5, 10, 5, "water")]
    m.room(30, 30, 7, pits=moat, style="castle", skyline="mountains", walls=False)
    W, T, WH = 30, 3.0, 7.0   # curtain wall: inner face at +-30, 3 m thick, wall-walk on top at 7 m
    gates = (-3, 3)            # gate passages through the north / south walls
    for side in (-1, 1):
        # north / south walls, with the gate passage cut through (portcullis at the outer end)
        for (x0, x1) in ((-W - T, gates[0]), (gates[1], W + T)):
            m.box(x0, -1, side * W if side > 0 else -W - T, x1, WH, side * (W + T) if side > 0 else -W, mirror=False)
        zi, zo = (W, W + T) if side > 0 else (-W - T, -W)
        m.box(gates[0], 4.6, zi, gates[1], WH, zo)                               # over the gate
        for i in range(7): m.metal(gates[0] + 0.4 + i * 0.85, 0, zo - 0.3 if side > 0 else zo, gates[0] + 0.55 + i * 0.85, 4.6, zo if side > 0 else zo + 0.3)   # portcullis bars
        m.metal(gates[0], 2.2, zo - 0.3 if side > 0 else zo, gates[1], 2.35, zo if side > 0 else zo + 0.3)
        m.light(0, 3.6, side * (W + 1.5), 6, 0.6, fixture=False)
        # east / west walls
        xi, xo = (W, W + T) if side > 0 else (-W - T, -W)
        m.box(xi, -1, -W, xo, WH, W)
    for side in (-1, 1):   # merlons on the outer edge of the wall-walk, a low lip on the inner edge
        for i in range(-W - 2, W + 3, 2):
            m.box(i - 0.5, WH, side * (W + T) - (0.6 if side > 0 else 0), i + 0.5, WH + 1.1, side * (W + T) + (0 if side > 0 else 0.6), accent=True)
            m.box(side * (W + T) - (0.6 if side > 0 else 0), WH, i - 0.5, side * (W + T) + (0 if side > 0 else 0.6), WH + 1.1, i + 0.5, accent=True)
        m.box(-W, WH, W if side > 0 else -W - 0.25, W, WH + 0.35, W + 0.25 if side > 0 else -W)      # inner lip, on top of the wall
        m.box(W if side > 0 else -W - 0.25, WH, -W, W + 0.25 if side > 0 else -W, WH + 0.35, W)
    def tower(cx, cz, half=5.0, top=WH, wall=1.0, rise=3.2, doors=()):
        """a square tower: solid up to the wall-walk, then a walled fighting platform with doorways (doors: sides
        n/s/w/e) and merlons on top, plus a banner and torches on the courtyard side"""
        x0, z0, x1, z1 = cx - half, cz - half, cx + half, cz + half
        m.box(x0, -1, z0, x1, top, z1)
        m.box(x0 - 0.3, top - 0.4, z0 - 0.3, x1 + 0.3, top, z1 + 0.3, accent=True)               # string course
        walls = {"n": (x0, z0, x1, z0 + wall, "x"), "s": (x0, z1 - wall, x1, z1, "x"), "w": (x0, z0, x0 + wall, z1, "z"), "e": (x1 - wall, z0, x1, z1, "z")}
        for k, (a, b, c, d, ax) in walls.items():
            m.doorwall(a, b, c, d, rise, [((a + c) / 2 if ax == "x" else (b + d) / 2, 2.4, 2.4)] if k in doors else [], axis=ax, base=top)
        for i in range(int(x0) + 1, int(x1), 2):
            m.box(i - 0.4, top + rise, z0, i + 0.4, top + rise + 0.9, z0 + wall, accent=True); m.box(i - 0.4, top + rise, z1 - wall, i + 0.4, top + rise + 0.9, z1, accent=True)
        for i in range(int(z0) + 1, int(z1), 2):
            m.box(x0, top + rise, i - 0.4, x0 + wall, top + rise + 0.9, i + 0.4, accent=True); m.box(x1 - wall, top + rise, i - 0.4, x1, top + rise + 0.9, i + 0.4, accent=True)
        m.light(cx, top + 2.4, cz, 7, 0.5, fixture=False)
    for (sx, sz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):                     # corner towers jut out past the walls
        tower(sx * (W + 3), sz * (W + 3), rise=5.0, doors=("w" if sx > 0 else "e", "n" if sz > 0 else "s"))
        # a roof on each corner tower, 12 m up, reached by a ladder inside against the outer wall: the castle's
        # highest perch, overlooking the walls and the fields
        cx, cz = sx * (W + 3), sz * (W + 3); zf = cz + (4.0 if sz > 0 else -4.0)   # inner face of the outer wall
        notch = (cx - 0.6, min(zf, zf - sz * 0.9), cx + 0.6, max(zf, zf - sz * 0.9))
        for (a0, b0, a1, b1) in minus((cx - 4, cz - 4, cx + 4, cz + 4), [notch]): m.box(a0, 11.7, b0, a1, 12.0, b1)
        m.ladder(cx, zf, WH, 12.0, "s" if sz > 0 else "n", width=0.8)
        # a tall stone on the outer edge of the wall top over the ladder: at 6.75 m/s you come out over the 1 m wall
        # top with a hop that would carry you off the castle; this stops you on it (1.17; clear of the climb, so you
        # stand, not climb on)
        zo = cz + 5.0 * sz
        m.box(cx - 0.6, 12.0, min(zo, zo - sz * 0.2), cx + 0.6, 14.6, max(zo, zo - sz * 0.2), accent=True)
    for sx in (-1, 1):                                                         # flank towers halfway along east / west
        tower(sx * (W + 2.5), 0, half=4.0, doors=("n", "s"))
    for sz in (-1, 1):                                                         # gatehouse towers either side of each gate
        for gx in (-6.5, 6.5): tower(gx, sz * (W + 3.5), half=3.5, rise=4.4, doors=("e" if gx < 0 else "w",))
    # the hoarding: a timber gallery along the inside of the curtain walls, level with the wall-walk (it widens it,
    # and roofs a covered ring of courtyard below), on posts
    for (a0, b0, a1, b1) in minus((-W, -W, W, W), [(-W + 3, -W + 3, W - 3, W - 3), (gates[0] - 0.5, -W, gates[1] + 0.5, -W + 3), (gates[0] - 0.5, W - 3, gates[1] + 0.5, W),
                                                   (-26.5, -W, -11.5, -W + 3), (11.5, W - 3, 26.5, W), (W - 3, 11.5, W, 26.5), (-W, -26.5, -W + 3, -11.5)]):   # (open over the wall stairs)
        m.box(a0, WH - 0.3, b0, a1, WH, b1, accent=True)
    for c in range(-24, 25, 8):
        for side in (-1, 1):
            if abs(c) > 4: m.metal(c - 0.2, 0, side * (W - 3) - 0.2, c + 0.2, WH - 0.3, side * (W - 3) + 0.2)
            if abs(c) > 4: m.metal(side * (W - 3) - 0.2, 0, c - 0.2, side * (W - 3) + 0.2, WH - 0.3, c + 0.2)   # (not in front of the bastion stairs)
    # a timber breastwork along the gallery's inner edge, in 4 m panels between the posts: archers' cover over the
    # courtyard. Planks, so a rifle or sniper shot still goes through (half damage); the gaps let you drop down.
    for c in range(-20, 21, 8):
        for side in (-1, 1):
            if abs(c) > 4: m.wallbang(c, WH, side * (W - 3) - 0.1, c + 4, WH + 1.2, side * (W - 3) + 0.1)
            m.wallbang(side * (W - 3) - 0.1, WH, c, side * (W - 3) + 0.1, WH + 1.2, c + 4)
    # stairs up to the wall-walk (one flight along each wall, point symmetric)
    m.stairs(-26, -29.9, -12, -28, WH, "x", -1, mirror=True)                   # north wall, rising west toward the corner
    m.stairs(28, 14, 29.9, 22, WH, "z", 1, base=3.0, mirror=True)              # east wall: on from the raised bastion, rising south
    # banners and torches along the inner faces of the curtain walls
    for side in (-1, 1):
        for x in (-20, -10, 10, 20):
            zf = side * W - (0.06 if side > 0 else -0.06)
            m.box(x - 1.0, 2.6, min(zf, side * W), x + 1.0, 6.2, max(zf, side * W), accent=True)                # the cloth
            m.glow(x - 1.05, 6.2, min(zf, side * W) - 0.02, x + 1.05, 6.35, max(zf, side * W) + 0.02)           # its pole
            m.glow(x - 0.15, 3.4, min(zf, side * W) - 0.04, x + 0.15, 5.4, max(zf, side * W) + 0.04)            # emblem
        for z in (-20, -10, 10, 20):
            m.glow(side * W - (0.2 if side > 0 else -0.2) - 0.1, 3.0, z - 0.1, side * W - (0.2 if side > 0 else -0.2) + 0.1, 3.4, z + 0.1)   # torch
            m.light(side * (W - 1), 3.2, z, 6, 0.55, fixture=False)
    # outside: the water moat round the walls and fields beyond (scenery, out of reach)
    for (x0, z0, x1, z1) in ((-60, -60, 60, -38), (-60, 38, 60, 60), (-60, -38, -38, 38), (38, -38, 60, 38)):
        m.box(x0, -1.6, z0, x1, -1.4, z1, mat="far")
    for (x0, z0, x1, z1) in ((-38, -38, 38, -33), (-38, 33, 38, 38), (-38, -33, -33, 33), (33, -33, 38, 33)):
        m.box(x0, -3.0, z0, x1, -2.8, z1, mat="water")
    # the keep: a tall tower on the island, with a gate and a top you can reach from the inner ward walls
    m.box(-2.4, 0, -2.4, 2.4, 6.5, 2.4, accent=True)
    for (dx, dz) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        x0, x1 = (-2.4, -1.4) if dx < 0 else (1.4, 2.4); z0, z1 = (-2.4, -1.4) if dz < 0 else (1.4, 2.4)
        m.box(x0, 6.5, z0, x1, 8.4, z1)
        m.box(x0 - 0.1, 8.4, z0 - 0.1, x1 + 0.1, 8.7, z1 + 0.1, accent=True)
    for (a0, b0, a1, b1) in minus((-2.6, -2.6, 2.6, 2.6), [(-0.6, 2.2, 0.6, 2.6)]): m.box(a0, 6.3, b0, a1, 6.5, b1)   # (notched over the ladder)
    m.metal(-0.1, 6.5, -0.1, 0.1, 12, 0.1)
    m.glow(0.1, 10.4, -0.05, 2.4, 11.8, 0.05)                                  # banner on the keep
    m.box(-2.5, 1.5, -2.5, 2.5, 1.7, 2.5); m.glow(-2.45, 2.0, -2.45, 2.45, 2.1, 2.45)
    m.raw("hill -5 0 -5 5 3 5")
    m.ladder(0, 2.6, 0, 6.5, "n", width=0.8)                                   # up the keep: its top is a perch over the hill
    for (x0, z0, x1, z1, ns) in ((-1.5, -10, 1.5, -5, True), (-1.5, 5, 1.5, 10, True), (-10, -1.5, -5, 1.5, False), (5, -1.5, 10, 1.5, False)):   # drawbridges
        m.metal(x0, -0.1, z0, x1, 0, z1)
        if ns: m.box(x0 - 0.15, 0, z0, x0, 0.9, z1, accent=True); m.box(x1, 0, z0, x1 + 0.15, 0.9, z1, accent=True)
        else: m.box(x0, 0, z0 - 0.15, x1, 0.9, z0, accent=True); m.box(x0, 0, z1, x1, 0.9, z1 + 0.15, accent=True)
    # raised bastions on the flanks (west one south of centre, east one north: point symmetric)
    m.box(-30, 0, -14, -22, 3, -4, mirror=True); m.stairs(-29, -4, -24, -1, 3, "z", -1, mirror=True)
    for i in range(5): m.box(-22 - 0.4, 3, -14 + i * 2.2, -22, 3.9, -14 + i * 2.2 + 1.0, accent=True, mirror=True)
    m.metal(-26.1, 3, -9.1, -25.9, 9, -8.9, mirror=True); m.glow(-25.9, 7.6, -9.05, -24, 8.8, -8.95, mirror=True)
    # inner ward walls with gates (north and south), walk the top via the stairs
    m.doorwall(-14, 15.75, 14, 16.25, 3.2, [(-9, 2.6, 2.8), (9, 2.6, 2.8)], axis="x", mirror=True)
    for i in range(-13, 14, 2): m.box(i - 0.4, 3.2, 15.75, i + 0.4, 3.9, 16.25, accent=True, mirror=True)
    m.stairs(-14, 16.25, -12, 20, 3.2, "z", -1, mirror=True)
    m.box(-20, 0, -2, -12, 3, 2, mirror=True)                                  # mid lane walls
    m.box(-14, 0, 10, -11, 1.5, 13, mirror=True); m.box(11, 0, 10, 14, 1.5, 13, mirror=True)
    m.box(-8, 0, 22, -5, 1.2, 25, mirror=True)
    # the courtyard well and a cart of hay (cover)
    m.box(-21, 0, 21, -18, 1.0, 24, accent=True, mirror=True); m.box(-20.6, 1.0, 21.4, -18.4, 1.1, 23.6, mirror=True)
    m.metal(-21, 1.0, 21, -20.8, 2.6, 21.2, mirror=True); m.metal(-18.2, 1.0, 23.8, -18, 2.6, 24, mirror=True); m.box(-21.2, 2.6, 21, -17.8, 2.8, 24, accent=True, mirror=True)
    m.box(-24, -0.05, -28, -16, 0, -20, accent=True); m.box(16, -0.05, -28, 24, 0, -20, accent=True)
    m.raw("site -24 0 -28 -16 3 -20"); m.raw("site 16 0 -28 24 3 -20")
    for p in ((9, 26), (9, 8), (9, -8), (9, -18), (0, -27)): m.raw(f"path {p[0]:g} 0 {p[1]:g}")
    for x in (-12, -6, 0, 6, 12): m.spawn("a", x, 0, 27 if x % 12 else 26, 0, "b")
    for (x, z, yaw) in ((-26, 26, 45), (26, 26, -45), (-20, 8, 90)): m.spawn("any", x, 0, z, yaw, "any")
    m.raw("checkpoint 25 0 -8 29 4 -4"); m.raw("checkpoint -4 0 -29 4 4 -25"); m.raw("checkpoint -29 0 4 -25 4 8"); m.raw("checkpoint -4 0 25 4 4 29")
    for x in (16, 20, 24): m.spawn("race", x, 0, 26, -90)
    m.light(-20, 2.2, 0, 6, 0.4, mirror=True)
    # the torches light the courtyard (each banner's torch is a lamp), and lanterns under the hoarding
    for side in (-1, 1):
        for x in (-20, -10, 10, 20): m.light(x, 3.2, side * (W - 0.8), 7, 0.7, fixture=False); m.light(side * (W - 0.8), 3.2, x, 7, 0.7, fixture=False)
    m.raw("setting castle")
    m.save("bastion")
