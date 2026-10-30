"""QK4 TERRACES (1.19.6): ranked VANILLA 1v1 (a tourney map), box and accent only. Two stepped amphitheatres face
each other across a sunken court (-4 m): six terraces a side, 6 m deep, every 4 m up to 20 m, low walls (1.2 m,
crouch cover) along their fronts with gaps, two stairs up each riser (zig-zagging). Three monoliths stand in the
court. Side tunnels run under each slope from the court to the top row (a ladder up a shaft at the back); the top
row's middle has a roofed box with two exits, the power spot. 110 x 80 m, point symmetric, the sun overhead."""
from .prefabs import *

HX, HZ = 55, 40
COURT = 12           # the court: x -12..12, at -4
DEEP = 6             # a terrace's depth
RISE = 4.0
N = 6                # terraces a side: 0..5 at 0, 4 .. 20
TOP = RISE * (N - 1)
TUN = (33, 37)       # the tunnels' strips (z, both signs)
TF, TC = -6.0, -1.0  # the tunnels' floor and roof
LID = 40.0


def terrace_x(k):
    """terrace k's span in x on the west slope (k = 5, the top row, runs to the back wall)"""
    x1 = -COURT - DEEP * k
    return (-HX if k == N - 1 else x1 - DEEP), x1


def flights(k):
    """the stairs up riser k (court -> terrace 0 for k = 0): z centres, zig-zagging"""
    return (-24, 8) if k % 2 == 0 else (-8, 24)


def build():
    m = M("QK4 Terraces", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla")
    m.raw("palette base 0.68 0.64 0.6 accent 0.46 0.26 0.62 metal 0.3 0.3 0.32 glow 0.85 0.6 1 sky 0.62 0.7 0.84")
    layout(m)
    decorate(m)
    m.raw("setting terraces round a sunken court, box and accent")
    m.save("qk4", dress=False)


def layout(m):
    a, b = TUN
    strips = [(-b, -a), (a, b)]
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
                if c1 > c0: m.box(c0, TC, z0, c1, top, z1, mirror=True)       # its roof (the shaft at x -52..-49 open)
    for (z0, z1) in strips:
        m.box(-HX, TF, z0, -52, TOP, z1, mirror=True)                         # the shaft's back wall
        m.stairs(-COURT - 4, z0, -COURT, z1, -4, "x", 1, mirror=True, base=TF)   # steps down from the court
    m.ladder(-51.88, 35, TF, TOP, "w"); m.ladder(51.88, -35, TF, TOP, "e")
    m.ladder(-51.88, -35, TF, TOP, "w"); m.ladder(51.88, 35, TF, TOP, "e")
    # ---- the stairs up the risers: two a riser, 8 steps of 0.5 m over 4.8 m on the level below
    for k in range(N):
        base = -4.0 if k == 0 else RISE * (k - 1)
        xr = -COURT if k == 0 else -COURT - DEEP * k
        for c in flights(k):
            m.stairs(xr, c - 1.5, xr + 4.8, c + 1.5, RISE * k, "x", -1, mirror=True, base=base)
    # ---- the low walls on every terrace's front edge (1.2 m), gaps where the stairs arrive and every few metres
    runs = [(-38, -29), (-21, -12), (-4, 4), (12, 21), (29, 38)]
    for k in range(N):
        x1 = terrace_x(k)[1]
        y = RISE * k
        busy = [(c - 2.5, c + 2.5) for c in flights(k)]
        for (z0, z1) in runs:
            pieces = [(z0, z1)]
            for (q0, q1) in busy:
                pieces = [p for (p0, p1) in pieces for p in ((p0, min(p1, q0)), (max(p0, q1), p1)) if p[1] - p[0] > 1.5]
            for (p0, p1) in pieces:
                m.box(x1 - 0.5, y, p0, x1, y + 1.2, p1, mirror=True)
                m.box(x1 - 0.55, y + 1.2, p0, x1 + 0.05, y + 1.3, p1, accent=True, mirror=True)
    # ---- the court's monoliths (11 m: cover in the court, and no line from one spawn row to the other)
    m.box(-3, -4, -4, 3, 7, 4, accent=True)
    m.box(-3, -4, 17, 3, 7, 29, accent=True, mirror=True)
    # ---- the power box on the top row: walls 4.2 m, a roof, exits north and south, a slot over the court
    y = TOP
    m.box(-54, y, -4.5, -53.5, y + 4.2, 4.5, mirror=True)                               # back
    m.box(-47, y, -4.5, -46.5, y + 1.0, 4.5, mirror=True)                               # front: a sill...
    m.box(-47, y + 2.4, -4.5, -46.5, y + 4.2, 4.5, mirror=True)                         # ...and over the slot
    m.box(-53.5, y, 4, -51, y + 4.2, 4.5, mirror=True); m.box(-48.5, y, 4, -47, y + 4.2, 4.5, mirror=True)
    m.box(-53.5, y, -4.5, -51, y + 4.2, -4, mirror=True); m.box(-48.5, y, -4.5, -47, y + 4.2, -4, mirror=True)
    m.box(-51, y + 4.0, -4.5, -48.5, y + 4.2, -4, mirror=True); m.box(-51, y + 4.0, 4, -48.5, y + 4.2, 4.5, mirror=True)
    m.box(-54.3, y + 4.2, -4.8, -46.2, y + 4.7, 4.8, accent=True, mirror=True)          # its roof
    # ---- the outer walls (to 26 m), clip from there to the lid, the lid
    W = TOP + 6
    m.box(-HX - 1, -7, -HZ - 1, HX + 1, W, -HZ, mirror=True)
    m.box(-HX - 1, -7, -HZ, -HX, W, HZ, mirror=True)
    m.box(-HX - 1, W, -HZ - 1, HX + 1, LID, -HZ, mat="clip", mirror=True)
    m.box(-HX - 1, W, -HZ, -HX, LID, HZ, mat="clip", mirror=True)
    m.box(-HX - 1, LID, -HZ - 1, HX + 1, LID + 1, HZ + 1, mat="clip")
    m.box(-54.3, y + 4.7, -4.8, -46.2, LID, 4.8, mat="clip", mirror=True)              # (the box's roof is no perch)
    # ---- spawns: each side's four on its low terraces near the ends, facing the court
    for (x, y, z) in ((-15, 0, -20), (-15, 0, 20), (-21, 4, -19), (-21, 4, 19)):
        m.spawn("a", x, y, z, -90, "b")
    for (x, y, z, yaw) in ((-27, 8, 0, -90), (-50, TOP, 14, -90), (-6, -4, 10, 0), (-30, TF, 35, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: the tunnels and the shafts (the terraces and the court have the sun)
    for (z0, z1) in strips:
        for x in (-16, -26, -36, -46):
            m.light(x, TC - 1.2, (z0 + z1) / 2, 10, 0.9, fixture=False, mirror=True)
        m.light(-50.5, 8, (z0 + z1) / 2, 10, 0.8, fixture=False, mirror=True)
    m.light(-50, TOP + 3.2, 0, 7, 0.7, fixture=False, mirror=True)


def decorate(m):
    """accent only, flush: a band along each riser's top, the court's border, pilasters on the back walls"""
    for k in range(N):
        x1 = terrace_x(k)[1]
        y = RISE * k
        for (z0, z1) in ((-HZ, -37), (-33, 33), (37, HZ)):
            m.box(x1, y - 0.3, z0, x1 + 0.05, y - 0.05, z1, accent=True, mirror=True)
    m.box(-COURT, -4, -HZ, COURT, -3.97, -HZ + 0.6, accent=True, mirror=True)
    for z in (-28, -14, 14, 28):
        m.box(-HX, TOP, z - 0.6, -HX + 0.05, TOP + 6, z + 0.6, accent=True, mirror=True)
