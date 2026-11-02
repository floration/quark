"""STADIUM (1.19.6): ball (and free for all) in a football stadium under floodlights, a clear evening, 172 x 128 m.
Owner, 2026-11-02: "ball needs a soccer field map obv; also you should make a block that only stops the ball so it
cant fall off the maps (no ceiling tho)".
  the pitch  120 x 76 m of astroturf (x -60..60, z -38..38), white lines flush with it (touchlines, goal lines, the
             halfway line, the centre circle, the penalty boxes); the ball starts on the centre spot.
  the goals  one in the middle of each goal line, a 14 m mouth (z -7..7) 5 m tall, 4.5 m deep under a net roof, a
             cargo net back and sides (players and the ball stop at it, shots go through). The goal (the site) is the
             inside of the frame: the ball's centre in it scores. Team A defends the west goal, team B the east.
  the walls  ball walls (the invisible block only the ball bounces off) round the pitch 4 m outside its lines, 40 m
             tall and open to the sky: the ball stays in play, players walk through them to the stands.
  the stands four tiers of seating round the pitch (0.5 m steps: walkable), the side stands under a cantilever roof,
             the end stands open; a dugout either side of the halfway line (cover); floodlight masts in the corners.
Kickoff: the teams line up in their own halves facing each other, so nobody can fire for the first 3 s (holdfire 3).
Mirrored west to east (x -> -x): both halves the same, only the team colours differ."""
from .prefabs import M
from .m2kit import B, light
from .bigkit import yaw_to

HX, HZ = 60.0, 38.0       # the pitch's lines
GZ, GH, GD = 7.0, 5.0, 4.5   # the goal mouth's half width, its height, its depth
WX, WZ = 64.0 + 2.0, 42.0    # the ball walls (x past the goals' backs)
SX, SZ = 70.0, 46.0       # the stands start here
OX, OZ = 86.0, 64.0       # the outer wall
LID = 40.0
LINE = "snow"             # (white, matte: the painted lines)


def mx(m, mat, x0, y0, z0, x1, y1, z1):
    """a box and its west-east mirror (one box if it straddles x = 0)"""
    B(m, mat, x0, y0, z0, x1, y1, z1)
    if not (abs(x0 + x1) < 1e-6): B(m, mat, -x1, y0, z0, -x0, y1, z1)


def build():
    m = M("Stadium", "ball", sun=(-0.4, 0.5, 0.3))
    m.raw("holdfire 3")   # (a kickoff: both teams line up facing each other across the halfway line, nobody fires for 3 s)
    m.raw("palette base 0.62 0.62 0.64 accent 0.18 0.32 0.62 metal 0.55 0.56 0.6 glow 1 0.96 0.86 sky 0.32 0.4 0.62")
    # ---- the ground: the pitch, the apron round it (out to the stands), under the stands
    B(m, "astroturf", -HX - 4, -1, -HZ - 4, HX + 4, 0, HZ + 4)
    for (x0, z0, x1, z1) in ((-OX, -OZ, OX, -HZ - 4), (-OX, HZ + 4, OX, OZ), (-OX, -HZ - 4, -HX - 4, HZ + 4), (HX + 4, -HZ - 4, OX, HZ + 4)):
        B(m, "concrete", x0, -1, z0, x1, 0, z1)
    lines(m)
    for sx in (-1, 1): goal(m, sx)
    # ---- the ball walls: 4 m outside the touchlines and 2 m behind the goals' backs, up to the lid, no roof
    for sz in (-1, 1):
        za, zb = sorted((sz * WZ, sz * (WZ + 0.5)))
        B(m, "ballwall", -WX - 0.5, 0, za, WX + 0.5, LID, zb)
    mx(m, "ballwall", -WX - 0.5, 0, -WZ, -WX, LID, WZ)
    stands(m)
    dugouts(m)
    floodlights(m)
    # ---- the bounds: the outer wall (the stadium's back) and a clip lid
    for (x0, z0, x1, z1) in ((-OX - 1, -OZ - 1, OX + 1, -OZ), (-OX - 1, OZ, OX + 1, OZ + 1), (-OX - 1, -OZ, -OX, OZ), (OX, -OZ, OX + 1, OZ)):
        B(m, "concrete", x0, 0, z0, x1, 24, z1)
        B(m, "clip", x0, 24, z0, x1, LID, z1)
    B(m, "clip", -OX - 1, LID, -OZ - 1, OX + 1, LID + 1, OZ + 1)
    B(m, "far", -400, -61, -400, 400, -60, 400)
    # ---- spawns: each team in its own half by its goal; free for all round the pitch and in the stands
    for (x, z) in ((-50, -9), (-50, -3), (-50, 3), (-50, 9)):
        m.spawn("a", x, 0, z, yaw_to(x, z, 0, z))
        m.spawn("b", -x, 0, z, yaw_to(-x, z, 0, z))
    for (x, z) in ((-30, -24), (30, 24), (-30, 24), (30, -24), (-12, 0), (12, 0), (0, -30), (0, 30)):
        m.spawn("any", x, 0, z, yaw_to(x, z, 0, 0) if (x, z) not in ((0, -30), (0, 30)) else yaw_to(x, z, 0, 0))
    m.raw("setting a football stadium under floodlights")
    m.save("stadium", dress=False)


def lines(m):
    """the painted lines, 0.15 m wide and 2 cm proud of the turf (well under a step)"""
    w, y0, y1 = 0.075, 0.0, 0.02
    B(m, LINE, -HX - w, y0, -HZ - w, HX + w, y1, -HZ + w)        # the touchlines
    B(m, LINE, -HX - w, y0, HZ - w, HX + w, y1, HZ + w)
    mx(m, LINE, -HX - w, y0, -HZ, -HX + w, y1, HZ)               # the goal lines
    B(m, LINE, -w, y0, -HZ, w, y1, HZ)                           # the halfway line
    # the centre circle (9 m): short straight pieces round it, and the centre spot
    import math
    n = 32
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        cx, cz = 9.15 * math.cos(a), 9.15 * math.sin(a)
        h = 9.15 * math.pi / n + 0.05
        if abs(math.cos(a)) > abs(math.sin(a)): B(m, LINE, cx - w, y0, cz - h, cx + w, y1, cz + h)
        else: B(m, LINE, cx - h, y0, cz - w, cx + h, y1, cz + w)
    B(m, LINE, -0.3, y0, -0.3, 0.3, y1, 0.3)
    # the penalty boxes (16.5 m out, 40 m wide) and the six-yard boxes (5.5 m out, 18 m wide), the penalty spots
    for (d, hw) in ((16.5, 20.0), (5.5, 9.0)):
        mx(m, LINE, -HX, y0, -hw - w, -HX + d, y1, -hw + w)
        mx(m, LINE, -HX, y0, hw - w, -HX + d, y1, hw + w)
        mx(m, LINE, -HX + d - w, y0, -hw, -HX + d + w, y1, hw)
    mx(m, LINE, -HX + 11 - 0.3, y0, -0.3, -HX + 11 + 0.3, y1, 0.3)


def goal(m, sx):
    """one goal: the frame (posts and crossbar, white steel), the net (cargo net: back, sides, roof), the site inside"""
    x0, x1 = sorted((sx * HX, sx * (HX + GD)))
    back = sx * (HX + GD)
    p = 0.25
    # posts and crossbar on the goal line, the back frame
    for z in (-GZ - p, GZ):
        B(m, "galvanizedsteel", sx * HX - p / 2, 0, z, sx * HX + p / 2, GH, z + p)
        B(m, "galvanizedsteel", back - p / 2, 0, z, back + p / 2, GH * 0.7, z + p)
    B(m, "galvanizedsteel", sx * HX - p / 2, GH, -GZ - p, sx * HX + p / 2, GH + p, GZ + p)
    # the net: back, sides and roof (the ball and players stop at it, shots pass)
    B(m, "cargonet", back - 0.05 * sx, 0, -GZ - p, back + 0.05 * sx, GH, GZ + p)
    for z in (-GZ - p, GZ + p - 0.1):
        B(m, "cargonet", x0, 0, z, x1, GH, z + 0.1)
    B(m, "cargonet", x0, GH, -GZ - p, x1, GH + 0.1, GZ + p)
    # the goal: the inside of the frame (the ball's centre past the goal line scores)
    xa, xb = sorted((sx * (HX + 0.5), sx * (HX + GD - 0.2)))
    m.raw(f"site {xa:g} 0 {-GZ:g} {xb:g} {GH:g} {GZ:g}")


def stands(m):
    """four tiers round the pitch, 0.5 m steps 1.2 m deep (walkable), aisles every 16 m; the side stands roofed"""
    rise, deep, n = 0.5, 1.2, 10
    for sz in (-1, 1):   # the side stands (z), the full length
        for k in range(n):
            za, zb = sorted((sz * (SZ + k * deep), sz * (SZ + n * deep + 2)))
            B(m, "concrete" if k % 2 else "paintedconcretestripes", -SX, 0, za, SX, (k + 1) * rise, zb)
        # the seats: a plastic row on every second tier (0.45 m: crouch cover, under a step), aisles left open
        for k in range(1, n, 2):
            z = sz * (SZ + k * deep + 0.6)
            za, zb = sorted((z - 0.3 * sz, z + 0.3 * sz))
            x = -SX + 2
            while x < SX - 2:
                B(m, "plastic", x, (k + 1) * rise, za, min(x + 13, SX - 2), (k + 1) * rise + 0.45, zb)
                x += 16
        # the roof: a cantilever over the side stand's back two thirds, on posts at its back
        rz0, rz1 = sorted((sz * (SZ + 4), sz * (SZ + n * deep + 4)))
        B(m, "galvanizedsteel", -SX, 14, rz0, SX, 14.4, rz1)
        for x in range(-64, 65, 16):
            zp = sz * (SZ + n * deep + 2.5)
            B(m, "galvanizedsteel", x - 0.3, n * rise, zp - 0.3, x + 0.3, 14, zp + 0.3)
    for sx in (-1, 1):   # the end stands (x), open to the sky, between the side stands
        for k in range(n):
            xa, xb = sorted((sx * (SX + k * deep), sx * (SX + n * deep + 2)))
            B(m, "concrete" if k % 2 else "paintedconcretestripes", xa, 0, -SZ, xb, (k + 1) * rise, SZ)
        for k in range(1, n, 2):
            x = sx * (SX + k * deep + 0.6)
            xa, xb = sorted((x - 0.3 * sx, x + 0.3 * sx))
            z = -SZ + 2
            while z < SZ - 2:
                B(m, "plastic", xa, (k + 1) * rise, z, xb, (k + 1) * rise + 0.45, min(z + 13, SZ - 2))
                z += 16
    # the corners: plain concrete blocks up to the top tier, so the stands join round the pitch
    for sx in (-1, 1):
        for sz in (-1, 1):
            xa, xb = sorted((sx * SX, sx * (SX + n * deep + 2)))
            za, zb = sorted((sz * SZ, sz * (SZ + n * deep + 2)))
            for k in range(n):
                xk = sorted((sx * (SX + k * deep), sx * (SX + n * deep + 2)))
                zk = sorted((sz * (SZ + k * deep), sz * (SZ + n * deep + 2)))
                B(m, "concrete", xk[0], 0, zk[0], xk[1], (k + 1) * rise, zk[1])


def dugouts(m):
    """a dugout each side of the halfway line on the south touchline: a bench under a plexiglass shelter (cover)"""
    for x0 in (-14, 4):
        B(m, "galvanizedsteel", x0, 0, -HZ - 3.6, x0 + 10, 2.4, -HZ - 3.4)                   # the back
        B(m, "scratchedplexiglass", x0, 2.4, -HZ - 3.6, x0 + 10, 2.5, -HZ - 1.6)            # the roof
        for xe in (x0, x0 + 9.9):
            B(m, "scratchedplexiglass", xe, 0, -HZ - 3.4, xe + 0.1, 2.4, -HZ - 1.6)        # the ends
        B(m, "plastic", x0 + 0.5, 0, -HZ - 3.4, x0 + 9.5, 0.45, -HZ - 2.8)                 # the bench
    light(m, -9, 2.2, -HZ - 2.5, 6, 0.5)
    light(m, 9, 2.2, -HZ - 2.5, 6, 0.5)


def floodlights(m):
    """a mast in each corner behind the stands, a bank of lamps on top facing the pitch"""
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * 80, sz * 58
            B(m, "galvanizedsteel", x - 0.6, 0, z - 0.6, x + 0.6, 30, z + 0.6)
            B(m, "galvanizedsteel", x - 3, 30, z - 0.4, x + 3, 33, z + 0.4)
            B(m, "glow", x - 2.8, 30.2, z - 0.5 * sz - 0.05, x + 2.8, 32.8, z - 0.5 * sz + 0.05)
    for (x, z) in ((-40, -20), (-40, 20), (0, -20), (0, 20), (40, -20), (40, 20), (-55, 0), (55, 0), (0, 0)):
        light(m, x, 22, z, 34, 0.9)
    for (x, z) in ((-45, 52), (0, 52), (45, 52), (-45, -52), (0, -52), (45, -52), (76, 0), (-76, 0)):
        light(m, x, 12, z, 22, 0.6)
