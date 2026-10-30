"""DOJO (1.19.6): ranked LASER VAMPIRE 1v1. A martial arts hall in a walled garden. The centre mat (18 x 18 m of
tatami) is flat and empty: the two fighters start on its edges facing each other and bow (no firing for 5 s).
Round the mat: eight pillars 3 m clear of it, a raised shrine dais at each end with a gallery over it, and a ring
of rafters at 7 m (grapple up, or the gallery ladder at each end). The hall's walls are paper screens that tear on
touch: the first fight opens the hall onto the veranda round it and the garden beyond (a koi pond, a tea house and
a bamboo grove on each side). Point symmetric, the sun overhead."""
from .prefabs import *
from .mkit import wedge, peak

F = 0.5          # the building's floor (a stone plinth half a metre over the garden: a step anyone walks up)
HX, HZ = 18, 14  # the hall's half sizes (36 x 28 m), to the wall line
VX, VZ = 22, 18  # the veranda's outer edge (4 m round the hall)
GX, GZ = 28, 28  # the garden wall (56 x 56 m)
CEIL = F + 11    # the hall's ceiling (the rafters' tops at F + 7: a duel giant, 3.6 m, still stands on them)
RAFT = F + 7


def build():
    m = M("Dojo", "1v1", sun=(0, 1, 0))   # (ranked: the sun overhead, so both halves are lit alike, MAP-17)
    m.also = ["duel", "vs"]               # (q: a map that plays 1v1 also offers duel and vs)
    m.raw("ranked vampire")
    m.raw("holdfire 5")
    layout(m)
    decorate(m)
    m.raw("setting a martial arts dojo in a walled garden")
    m.save("dojo", dress=False)


def layout(m):
    # ---- ground: the garden's gravel, the building's plinth and floors
    ponds = [(-13, 20, -3, 26)]
    holes = [(-VX - 0.5, -VZ - 0.5, VX + 0.5, VZ + 0.5)] + ponds + [(-x1, -z1, -x0, -z0) for (x0, z0, x1, z1) in ponds]
    for (x0, z0, x1, z1) in minus((-GX, -GZ, GX, GZ), holes):
        m.box(x0, -1, z0, x1, 0, z1, mat="gravel")
    m.box(-VX - 0.5, -1, -VZ - 0.5, VX + 0.5, F - 0.15, VZ + 0.5, mat="largetemplestone")   # the plinth
    for (x0, z0, x1, z1) in minus((-VX - 0.5, -VZ - 0.5, VX + 0.5, VZ + 0.5), [(-HX, -HZ, HX, HZ)]):
        m.box(x0, F - 0.15, z0, x1, F, z1, mat="hugeplanks")                                 # the veranda's boards
    for (x0, z0, x1, z1) in minus((-HX, -HZ, HX, HZ), [(-9, -9, 9, 9)]):
        m.box(x0, F - 0.15, z0, x1, F, z1, mat="wideplanks")                                 # the hall floor
    m.box(-9, F - 0.15, -9, 9, F, 9, mat="tatami")                                           # the mat, flush
    # the koi ponds (clear water, slows, not lethal), a pool 0.8 m deep: a step down and a hop out
    for (x0, z0, x1, z1) in ponds:
        m.box(x0, -1.8, z0, x1, -0.8, z1, mat="mossystone", mirror=True)
        m.box(x0, -0.8, z0, x1, -0.15, z1, mat="clearlagoon", mirror=True)
    # ---- the garden wall (and the clip over it and over the whole map: nobody hooks out)
    WH = 4.0
    m.box(-GX - 1, -1, -GZ - 1, -GX, WH, GZ + 1, mat="hedge", mirror=True)
    m.box(-GX, -1, GZ, GX, WH, GZ + 1, mat="hedge", mirror=True)
    LID = F + 16
    m.box(-GX - 1, WH, -GZ - 1, -GX, LID, GZ + 1, mat="clip", mirror=True)
    m.box(-GX, WH, GZ, GX, LID, GZ + 1, mat="clip", mirror=True)
    m.box(-GX - 1, LID, -GZ - 1, GX + 1, LID + 0.5, GZ + 1, mat="clip")
    # ---- the hall: posts every 4 m, paper screens between them (they tear on touch), solid shoji above
    SCR = F + 4.4   # the screens' top (a duel giant is 3.6 m: the doorways are 4.4)
    def bay(x0, z0, x1, z1, kind):
        if kind == "screen": m.box(x0, F, z0, x1, SCR, z1, mat="paperscreen")
        elif kind == "wall": m.box(x0, F, z0, x1, SCR, z1, mat="shojiscreen")
        m.box(x0, SCR, z0, x1, SCR + 0.4, z1, mat="logcabin")                  # the lintel beam
        m.box(x0, SCR + 0.4, z0, x1, CEIL - 0.5, z1, mat="shojiscreen")       # the high wall
        m.box(x0, CEIL - 0.5, z0, x1, CEIL, z1, mat="logcabin")
    t = 0.2
    for k in range(9):          # the long walls (z = +-14): the end bays are open doorways, the rest screens
        x0 = -HX + 4 * k
        kind = "open" if k in (0, 8) else "screen"
        bay(x0 + 0.25, HZ - t / 2, x0 + 3.75, HZ + t / 2, kind)
        bay(-x0 - 3.75, -HZ - t / 2, -x0 - 0.25, -HZ + t / 2, kind)
    for k in range(7):          # the end walls (x = +-18): solid behind the shrine, screens either side
        z0 = -HZ + 4 * k
        kind = "wall" if k in (2, 3, 4) else "screen"
        bay(HX - t / 2, z0 + 0.25, HX + t / 2, z0 + 3.75, kind)
        bay(-HX - t / 2, -z0 - 3.75, -HX + t / 2, -z0 - 0.25, kind)
    for k in range(10):         # the posts
        x = -HX + 4 * k
        m.box(x - 0.25, F, HZ - 0.25, x + 0.25, CEIL, HZ + 0.25, mat="logcabin", mirror=True)
    for k in range(1, 7):
        z = -HZ + 4 * k
        m.box(HX - 0.25, F, z - 0.25, HX + 0.25, CEIL, z + 0.25, mat="logcabin", mirror=True)
    m.box(-HX - 0.25, CEIL, -HZ - 0.25, HX + 0.25, CEIL + 0.5, HZ + 0.25, mat="wideplanks")   # the ceiling
    # the roof over it: a gable of shingles (out of play, but a hook can land on it: in bounds, under the clip)
    wedge(m, "roofshingles", 2, (-HX - 1, CEIL + 0.5, -HZ - 1, HX + 1, CEIL + 4, 0), mirror=True)
    # ---- inside: the shrine daises, the galleries over them, the pillars and the rafters
    m.box(14.5, F, -11, HX - 0.1, F + 0.5, 11, mat="largetemplestone", mirror=True)        # the dais (0.5 m)
    G0 = 15.6   # the gallery's front edge (2.2 m deep, along the whole end wall)
    m.box(G0, RAFT - 0.5, -HZ + 0.25, HX - 0.1, RAFT, HZ - 0.25, mat="hugeplanks", mirror=True)
    m.box(G0, F + 0.5, 7.4, G0 + 0.6, RAFT - 0.5, 8.6, mat="logcabin", mirror=True)        # the ladder's column
    m.ladder(G0 - 0.12, 8, F + 0.5, RAFT, "e"); m.ladder(-G0 + 0.12, -8, F + 0.5, RAFT, "w")
    for (x, z) in ((5, 12.5), (-5, 12.5), (12.5, 5), (12.5, -5)):                         # pillars, 3 m clear of the mat
        m.box(x - 0.4, F, z - 0.4, x + 0.4, RAFT - 0.6, z + 0.4, mat="logcabin", mirror=True)
    m.box(-G0, RAFT - 0.6, 11.9, G0, RAFT, 13.1, mat="logcabin", mirror=True)              # the rafter ring: long beams
    m.box(11.9, RAFT - 0.6, -11.9, 13.1, RAFT, 11.9, mat="logcabin", mirror=True)          # ...and the cross beams
    # ---- the veranda: posts along its edge, a lean-to roof of shingles
    VR = F + 5.0
    for k in range(12):
        x = -VX + 4 * k
        m.box(x - 0.2, F, VZ - 0.4, x + 0.2, VR, VZ, mat="logcabin", mirror=True)
    for k in range(1, 9):
        z = -VZ + 4 * k
        m.box(VX - 0.4, F, z - 0.2, VX, VR, z + 0.2, mat="logcabin", mirror=True)
    for (x0, z0, x1, z1) in minus((-VX - 0.8, -VZ - 0.8, VX + 0.8, VZ + 0.8), [(-HX - 0.25, -HZ - 0.25, HX + 0.25, HZ + 0.25)]):
        m.box(x0, VR, z0, x1, VR + 0.35, z1, mat="wideplanks")
    wedge(m, "roofshingles", 2, (-VX - 0.8, VR + 0.35, -VZ - 0.8, VX + 0.8, VR + 1.6, -HZ - 0.25), mirror=True)
    wedge(m, "roofshingles", 0, (-VX - 0.8, VR + 0.35, -HZ - 0.25, -HX - 0.25, VR + 1.6, HZ + 0.25), mirror=True)
    # ---- the tea houses (one each side): raised 1 m on stone, two steps up at each end, open to the front
    TX0, TX1, TZ0, TZ1, TY = 11, 19, 22, 27.5, 1.0
    m.box(TX0, -1, TZ0, TX1, TY, TZ1, mat="largetemplestone", mirror=True)
    m.stairs(TX0 - 2, 23, TX0, 26.5, TY, "x", 1, mirror=True)
    m.stairs(TX1, 23, TX1 + 2, 26.5, TY, "x", -1, mirror=True)
    TR = TY + 4.6
    m.box(TX0, TY, TZ1 - 0.3, TX1, TR, TZ1, mat="shojiscreen", mirror=True)                # the back wall
    for (x0, x1) in ((TX0, TX0 + 0.3), (TX1 - 0.3, TX1)):                                  # the end walls, a door in each
        m.box(x0, TY, TZ0, x1, TR, 23, mat="shojiscreen", mirror=True)
        m.box(x0, TY, 26.5, x1, TR, TZ1 - 0.3, mat="shojiscreen", mirror=True)
        m.box(x0, TY + 4.3, 23, x1, TR, 26.5, mat="logcabin", mirror=True)
    for x in (TX0 + 0.3, 15, TX1 - 0.6):
        m.box(x, TY, TZ0, x + 0.3, TR, TZ0 + 0.3, mat="logcabin", mirror=True)            # the open front's posts
    m.box(TX0 - 0.6, TR, TZ0 - 0.6, TX1 + 0.6, TR + 0.3, TZ1 + 0.3, mat="wideplanks", mirror=True)
    wedge(m, "roofshingles", 3, (TX0 - 0.6, TR + 0.3, TZ0 - 0.6, TX1 + 0.6, TR + 1.8, TZ1 + 0.3), mirror=True)
    # ---- the spawns: each side's four along its edge of the mat, facing across it (14 m: the bow)
    for z in (-3.75, -1.25, 1.25, 3.75):
        m.spawn("a", -7, F, z, -90, "b")
    # free for all (and a tourney's warm-up): eight, in mirrored pairs, on every level
    for (x, y, z, yaw) in ((-12, F, -9, -90), (16.8, RAFT, -10, 90), (-4, F, 16, -90), (-24, 0, -23, 180)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: daylight on the garden (the sun overhead), lamps in the hall, under the galleries and the veranda
    for (x, z) in ((-6, -6), (6, -6)):
        m.light(x, F + 9, z, 16, 0.6, fixture=False, mirror=True)
    m.light(16.8, RAFT - 1.2, 0, 10, 0.6, fixture=False, mirror=True)
    m.light(16.8, F + 9.5, 0, 9, 0.5, fixture=False, mirror=True)
    for x in (-12, 0, 12):
        m.light(x, VR - 0.8, 16, 7, 0.45, fixture=False, mirror=True)
    for z in (-8, 8):
        m.light(20, VR - 0.8, z, 7, 0.45, fixture=False, mirror=True)
    m.light(15, TR - 0.8, 24.8, 6, 0.5, fixture=False, mirror=True)


def decorate(m):
    """sparing, and out of the lanes (movement is fast): on walls, under the ceiling, in the garden's corners"""
    VR = F + 5.0
    # the shrine on each dais, against the solid end wall: an altar, two candles, a scroll panel over it
    m.box(16.9, F + 0.5, -2.2, HX - 0.1, F + 1.5, 2.2, mat="largetemplestone", mirror=True)
    m.box(16.8, F + 1.5, -2.4, HX - 0.1, F + 1.65, 2.4, mat="logcabin", mirror=True)
    for z in (-1.6, 1.6):
        m.glow(17.3, F + 1.65, z - 0.08, 17.46, F + 2.0, z + 0.08, mirror=True)
    m.box(17.75, F + 2.2, -1.4, HX - 0.1, F + 4.0, 1.4, mat="bamboo", mirror=True)          # (flat on the wall)
    m.light(17.0, F + 2.4, 0, 5, 0.5, fixture=False, mirror=True)
    # weapon racks on the gallery's back wall: a rail and staves standing in it (0.3 m deep, the gallery stays 1.9 m)
    for zc in (-5.5, 5.5):
        m.box(17.5, RAFT + 0.9, zc - 1.6, HX - 0.1, RAFT + 1.05, zc + 1.6, mat="logcabin", mirror=True)
        for k in range(-3, 4):
            m.box(17.58, RAFT, zc + k * 0.45 - 0.04, 17.66, RAFT + 2.3, zc + k * 0.45 + 0.04, mat="bamboo", mirror=True)
    # paper lanterns hung between the mat and the daises (over the floor, never over the mat), lit
    for (x, z) in ((10.5, -6), (10.5, 6)):
        m.box(x - 0.03, F + 10.2, z - 0.03, x + 0.03, CEIL, z + 0.03, mat="metal", mirror=True)
        m.glow(x - 0.35, F + 9.4, z - 0.35, x + 0.35, F + 10.2, z + 0.35, mirror=True)
        m.light(x, F + 9.0, z, 9, 0.5, fixture=False, mirror=True)
    # lanterns under the veranda's eaves, by the outer posts (4.1 m clear under them: a duel giant passes)
    for x in (-14, -6, 6, 14):
        m.glow(x - 0.25, VR - 0.7, VZ - 0.45, x + 0.25, VR - 0.1, VZ - 0.05, mirror=True)
    for z in (-10, 10):
        m.glow(VX - 0.45, VR - 0.7, z - 0.25, VX - 0.05, VR - 0.1, z + 0.25, mirror=True)
    # the garden: stone lanterns in the corners and by the ponds, rocks at the ponds' far edge, a bamboo grove on each
    # side strip (breakable stems at the wall side: the lane along the veranda stays clear)
    for (x, z) in ((25.5, 25.5), (-25.5, 25.5), (-1.5, 22), (25, 20.5)):
        m.box(x - 0.5, 0, z - 0.5, x + 0.5, 0.3, z + 0.5, mat="mossystone", mirror=True)
        m.box(x - 0.2, 0.3, z - 0.2, x + 0.2, 1.0, z + 0.2, mat="mossystone", mirror=True)
        m.glow(x - 0.35, 1.0, z - 0.35, x + 0.35, 1.5, z + 0.35, mirror=True)
        m.box(x - 0.55, 1.5, z - 0.55, x + 0.55, 1.75, z + 0.55, mat="mossystone", mirror=True)
        m.light(x, 1.3, z, 5, 0.4, fixture=False, mirror=True)
    for (x0, z0, x1, z1, h) in ((-12.5, 26.2, -10.5, 27.6, 0.9), (-7, 26.4, -5.5, 27.6, 0.6), (-13.8, 23, -13.1, 24.6, 0.5)):
        m.box(x0, 0, z0, x1, h, z1, mat="mossystone", mirror=True)
    for (x, z) in ((26.2, 3), (27.1, 4.4), (25.8, 5.6), (26.9, 7.1), (26.0, 8.8), (27.2, 9.9), (26.4, 11.3), (27.0, 1.6),
                   (27.4, 2.6), (26.6, 6.3), (27.5, 8.1), (25.9, 10.2), (27.3, 11.9), (26.5, 0.8), (27.6, 5.3), (26.1, 12.4)):
        m.box(x - 0.18, 0, z - 0.18, x + 0.18, 6.5 + (x * 7 + z * 3) % 2.0, z + 0.18, mat="bamboofence", mirror=True)
    # out past the wall: far mountains (unlit scenery)
    for (x, z, r, h) in ((150, -60, 70, 60), (120, 90, 55, 42), (30, 170, 80, 70), (-90, 140, 60, 48)):
        peak(m, "far", x, z, r, -10, h, mirror=True)
    m.box(-400, -3, -400, 400, -1.05, 400, mat="far")                                   # the valley floor round it
