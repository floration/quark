"""QK4 TERRACES (1.19.6): ranked VANILLA and INSTAGIB 1v1 (a tourney map), box and accent only. Two stepped
amphitheatres face each other across a sunken court (-4 m): six terraces a side every 4 m up to 20 m, 6 m deep
(terrace 2 is a 9 m landing), low walls (1.2 m, crouch cover) along their fronts with gaps, two stairs up each riser
(zig-zagging) and a straight ramp up the whole stand (the fast line). Three monoliths stand in the court (the middle
one 24 m tall: it splits the bowl), and two tall blocks on each stand break its lines. Side tunnels run under each
slope from the court to the top row (a ladder in two stages up a shaft at the back, a halfway exit up to terrace
1); the top row's middle has a roofed box with two exits at its front edge, looking down the slope into the court:
the power spot. 110 x 80 m, point symmetric, the sun overhead.
(1.19.6 fix round 2: the box at the front edge, ramps, LOS blocks, the tall monolith, the landing terrace, the
tunnels' exits and two-stage ladders, 0.15 m caps, wider court lanes, rails, instagib.)"""
from .prefabs import *
from .mkit import wedge

HX, HZ = 55, 40
COURT = 12           # the court: x -12..12, at -4
DEPTHS = [6, 6, 9, 6, 6]   # terraces 0..4 (terrace 2 a deeper landing); the top row runs to the back wall
RISE = 4.0
N = 6                # terraces a side: 0..5 at 0, 4 .. 20
TOP = RISE * (N - 1)
TUN = (33, 37)       # the tunnels' strips (z, both signs)
TF, TC = -6.0, -1.0  # the tunnels' floor and roof
LID = 40.0
RAMP = (13, 17)      # the ramp's strip (z) on the west stand (the east one's is its mirror)


def terrace_x(k):
    """terrace k's span in x on the west slope (k = 5, the top row, runs to the back wall)"""
    x1 = -COURT - sum(DEPTHS[:k])
    return (-HX if k == N - 1 else x1 - DEPTHS[k]), x1


def flights(k):
    """the stairs up riser k (court -> terrace 0 for k = 0): z centres, zig-zagging"""
    return (-24, 8) if k % 2 == 0 else (-8, 24)


def build():
    m = M("QK4 Terraces", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla instagib")
    m.raw("palette base 0.68 0.64 0.6 accent 0.46 0.26 0.62 metal 0.3 0.3 0.32 glow 0.85 0.6 1 sky 0.62 0.7 0.84")
    layout(m)
    decorate(m)
    m.raw("setting terraces round a sunken court, box and accent")
    m.save("qk4", dress=False)


def layout(m):
    a, b = TUN
    strips = [(-b, -a), (a, b)]
    X5 = terrace_x(N - 1)[1]        # the top row's front edge (-45)
    # ---- the court and the terraces (west slope, mirrored east), the tunnels carved under them
    m.box(-COURT, -5, -HZ, COURT, -4, HZ)
    for k in range(N):
        x0, x1 = terrace_x(k)
        top = RISE * k
        for (z0, z1) in ((-HZ, -b), (-a, a), (b, HZ)):
            m.box(x0, -7, z0, x1, top, z1, mirror=True)
        for (z0, z1) in strips:
            m.box(x0, -7, z0, x1, TF, z1, mirror=True)                       # the tunnel's floor
            if k == 0: continue                                               # (open over the first terrace: a trench into it)
            for (c0, c1) in ([(x0, -52), (-49, x1)] if k == N - 1 else [(x0, x1)]):
                if k == 1:   # (terrace 1: a hatch up from the tunnel, x -23..-20, the tunnel's halfway exit)
                    for (d0, d1) in ((c0, -23), (-20, c1)):
                        m.box(d0, TC, z0, d1, top, z1, mirror=True)
                    m.box(-23, TC, z0, -20, top, z0 + 0.5, mirror=True); m.box(-23, TC, z1 - 0.5, -20, top, z1, mirror=True)
                elif c1 > c0: m.box(c0, TC, z0, c1, top, z1, mirror=True)     # its roof (the shaft at x -52..-49 open)
    # the trench over terrace 0 (the tunnel's open top) has a 1 m rail along both sides
    for (z0, z1) in strips:
        x0, x1 = terrace_x(0)
        m.box(x0, 0, z0 - 0.3, x1, 1.0, z0, accent=True, mirror=True)
        m.box(x0, 0, z1, x1, 1.0, z1 + 0.3, accent=True, mirror=True)
    for (z0, z1) in strips:
        m.box(-HX, TF, z0, -52, TOP, z1, mirror=True)                         # the shaft's back wall
        m.stairs(-COURT - 4, z0, -COURT, z1, -4, "x", 1, mirror=True, base=TF)   # steps down from the court
    # the shaft: a ladder in two stages (a landing block at 7.5 in its court-side half), a lip round its top
    LAND = 7.5
    for (z0, z1) in strips:
        inner = z0 if z0 > 0 else z1          # the strip's edge nearer the middle
        s = 1 if z0 > 0 else -1               # +1: the north strip
        lz0, lz1 = sorted((inner, inner + 2 * s))
        m.box(-52, TF, lz0, -49, LAND, lz1, mirror=True)
        m.box(-49, TOP, z0 - 0.3, -48.7, TOP + 1.0, z1 + 0.3, accent=True, mirror=True)
        m.box(-52, TOP, z0 - 0.3, -49, TOP + 1.0, z0, accent=True, mirror=True)
        m.box(-52, TOP, z1, -49, TOP + 1.0, z1 + 0.3, accent=True, mirror=True)
    for s in (1, -1):
        for (sx, f1, f2) in ((1, "n" if s > 0 else "s", "w"), (-1, "s" if s > 0 else "n", "e")):
            zl = s * (a + 2 + 0.12)                                           # (the landing's open face, +0.12)
            m.ladder(-50.5 * sx, zl * sx, TF, LAND, f1, rail="accent")
            m.ladder((-52 + 0.12) * sx, s * (a + 1) * sx, LAND, TOP, f2, rail="accent")
    # the halfway exit: a ladder up the hatch's west face from the tunnel to terrace 1
    for s in (1, -1):
        m.ladder(-23 + 0.12, s * (a + b) / 2, TF, RISE, "w", rail="accent")
        m.ladder(23 - 0.12, -s * (a + b) / 2, TF, RISE, "e", rail="accent")
    # ---- the stairs up the risers: two a riser, 8 steps of 0.5 m over 4.8 m on the level below
    for k in range(N):
        base = -4.0 if k == 0 else RISE * (k - 1)
        xr = terrace_x(k)[1]
        for c in flights(k):
            m.stairs(xr, c - 1.5, xr + 4.8, c + 1.5, RISE * k, "x", -1, mirror=True, base=base)
    # ---- the ramp: straight up the stand from terrace 0 to the top row (a wedge over each terrace, 34 degrees on the
    # 6 m ones, 24 on the landing): the fast line, in view of the court and the other stand
    for k in range(N - 1):
        x0, x1 = terrace_x(k)
        wedge(m, "box", 1, (x0, RISE * k, RAMP[0], x1, RISE * (k + 1), RAMP[1]), mirror=True)
        m.box(x0, RISE * k, RAMP[0] - 0.3, x1, RISE * (k + 1) + 0.04, RAMP[0], accent=True, mirror=True)
    # ---- the low walls on every terrace's front edge (1.2 m, a 0.15 m cap: not wallbang), gaps where the stairs
    # and the ramp arrive and every few metres
    runs = [(-38, -29), (-21, -12), (-4, 4), (8, 12), (18, 21), (29, 38)]
    for k in range(N):
        x1 = terrace_x(k)[1]
        y = RISE * k
        busy = [(c - 2.5, c + 2.5) for c in flights(k)] + [(RAMP[0] - 1, RAMP[1] + 1)]
        if k == N - 1: busy.append((-5, 5))          # (the power box stands there)
        for (z0, z1) in runs:
            pieces = [(z0, z1)]
            for (q0, q1) in busy:
                pieces = [p for (p0, p1) in pieces for p in ((p0, min(p1, q0)), (max(p0, q1), p1)) if p[1] - p[0] > 1.5]
            for (p0, p1) in pieces:
                m.box(x1 - 0.5, y, p0, x1, y + 1.2, p1, mirror=True)
                m.box(x1 - 0.55, y + 1.2, p0, x1 + 0.05, y + 1.35, p1, accent=True, mirror=True)
    # ---- the court's monoliths: the middle one 24 m (it splits the bowl into halves), the side ones thin slabs (11 m)
    # leaving 6 m lanes by the riser stairs
    m.box(-2, -4, -4, 2, 24, 4, accent=True)
    m.box(-1.2, -4, 17, 1.2, 7, 29, accent=True, mirror=True)
    # ---- two tall blocks on each stand (7 m), breaking its lines: on the landing (terrace 2) and on terrace 4
    for (k, z0, z1) in ((2, 28, 32), (4, -22, -18)):
        x0, x1 = terrace_x(k)
        m.box(x0 + 1.5, RISE * k, z0, x0 + 4.5, RISE * k + 7, z1, mirror=True)
        m.box(x0 + 1.45, RISE * k + 7, z0 - 0.05, x0 + 4.55, RISE * k + 7.15, z1 + 0.05, accent=True, mirror=True)
    # ---- the power box on the top row, at its front edge: walls 4.2 m, a roof, exits north and south, a wide slot
    # (sill 0.6, up to 2.6) looking down the slope into the court
    y = TOP
    BX0, BX1 = X5 - 8.5, X5            # -53.5..-45
    m.box(BX0 - 0.5, y, -4.5, BX0, y + 4.2, 4.5, mirror=True)                             # back
    m.box(BX1 - 0.5, y, -4.5, BX1, y + 0.6, 4.5, mirror=True)                             # front: a sill...
    m.box(BX1 - 0.5, y + 2.6, -4.5, BX1, y + 4.2, 4.5, mirror=True)                       # ...and over the slot
    for zz in (4, -4.5):
        m.box(BX0, y, zz, BX0 + 2.5, y + 4.2, zz + 0.5, mirror=True)
        m.box(BX1 - 3.5, y, zz, BX1 - 0.5, y + 4.2, zz + 0.5, mirror=True)
        m.box(BX0 + 2.5, y + 4.0, zz, BX1 - 3.5, y + 4.2, zz + 0.5, mirror=True)
    m.box(BX0 - 0.8, y + 4.2, -4.8, BX1 + 0.3, y + 4.7, 4.8, accent=True, mirror=True)  # its roof
    # ---- the outer walls (to 26 m), clip from there to the lid, the lid
    W = TOP + 6
    m.box(-HX - 1, -7, -HZ - 1, HX + 1, W, -HZ, mirror=True)
    m.box(-HX - 1, -7, -HZ, -HX, W, HZ, mirror=True)
    m.box(-HX - 1, W, -HZ - 1, HX + 1, LID, -HZ, mat="clip", mirror=True)
    m.box(-HX - 1, W, -HZ, -HX, LID, HZ, mat="clip", mirror=True)
    m.box(-HX - 1, LID, -HZ - 1, HX + 1, LID + 1, HZ + 1, mat="clip")
    m.box(BX0 - 0.8, y + 4.7, -4.8, BX1 + 0.3, LID, 4.8, mat="clip", mirror=True)      # (the box's roof is no perch)
    # ---- spawns: each side's four on its low terraces near the ends, facing the court
    for (x, y, z) in ((-15, 0, -20), (-15, 0, 20), (-21, 4, -19), (-21, 4, 19)):
        m.spawn("a", x, y, z, -90, "b")
    for (x, y, z, yaw) in ((-30, 8, 3, -90), (-50, TOP, 14, -90), (-30, TF, -35, -90), (-30, TF, 35, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: the tunnels and the shafts (the terraces and the court have the sun)
    for (z0, z1) in strips:
        for x in (-16, -26, -36, -46):
            m.light(x, TC - 1.2, (z0 + z1) / 2, 10, 0.9, fixture=False, mirror=True)
        m.light(-50.5, 8, (z0 + z1) / 2, 10, 0.8, fixture=False, mirror=True)
    m.light(-49, TOP + 3.2, 0, 7, 0.7, fixture=False, mirror=True)


def decorate(m):
    """accent only, flush: a band along each riser's top (two on the stand's north half, one on its south half: the
    halves read differently), the court's border, pilasters on the back walls"""
    for k in range(N):
        x1 = terrace_x(k)[1]
        y = RISE * k
        for (z0, z1) in ((-HZ, -37), (-33, 0), (0, 33), (37, HZ)):
            m.box(x1, y - 0.3, z0, x1 + 0.05, y - 0.05, z1, accent=True, mirror=True)
            if z0 >= 0: m.box(x1, y - 0.8, z0, x1 + 0.05, y - 0.55, z1, accent=True, mirror=True)
    m.box(-COURT, -4, -HZ, COURT, -3.97, -HZ + 0.6, accent=True, mirror=True)
    for z in (-28, -14, 14, 28):
        m.box(-HX, TOP, z - 0.6, -HX + 0.05, TOP + 6, z + 0.6, accent=True, mirror=True)
