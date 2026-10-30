"""QK1 RING (1.19.6): ranked VANILLA 1v1, box and accent only. A square donut: a ring corridor 10 m wide on two
storeys (0 and 8 m) round an open middle, a ledge at the ground floor's level round a void pit 40 m across, and an
island in the pit (a deck at 8 m, its top at 12 m). Four bridges at 8 m reach the island from the upper storey: the
north and south ones whole, the east and west ones broken by a 12 m gap (dash or hook). Each ledge has a stair up
beside its bridge, so the ground floor reaches the middle. The ring's inner walls have openings a storey into the
middle; two corner balconies at 8 m hang over the pit's corners; two grapple blocks hang over the pit at 20 m. The
stairs between the storeys stand in the ring's corners, and each arm has one rise in its floor (a 2 m hump) that
cuts its long line. Point symmetric, the sun overhead.
(1.19.6 fix round 2: 10 m corridors, stairs in the corners, humps, ledge stairs, grapple blocks, void rims,
spawns two a side upstairs, the corner notches filled, 4.1 m upper windows.)"""
from .prefabs import *
from .mkit import wedge

R0, R1 = 37, 49       # the inner wall (37..38) and the outer wall (48..49): the corridor is 38..48, 10 m
WT = 1.0              # wall thickness
UP = 8.0              # the upper storey's floor
TOP = 16.0            # the ring's roof (clip over it: not a floor)
PIT = 20              # the pit: -20..20
ISL, ISL_TOP = 9, 4   # the island's deck (+-9 at 8 m) and its top block (+-4 at 12 m)
LID = 30.0
DOOR_H = 4.5          # (a duel giant is 3.6 m)
WIN_H = 5.0           # the upper windows: sill 0.9 to 5.0 (4.1 m clear)
OPEN = 10             # the openings in the inner walls (ground doors, upper windows) at +-10 along each side
HUMP = 2.0            # each arm's rise


def build():
    m = M("QK1 Ring", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla")
    m.raw("void -14")
    m.raw("palette base 0.6 0.62 0.66 accent 0.95 0.45 0.12 metal 0.3 0.3 0.33 glow 1 0.62 0.25 sky 0.34 0.38 0.48")
    layout(m)
    m.raw("setting a ring of corridors round a pit, box and accent")
    m.save("qk1", dress=False)


def side_wall(m, a0, a1, fixed0, fixed1, y0, y1, holes, axis):
    """a wall along `axis` from a0 to a1 (its thickness fixed0..fixed1), with holes [(c0, c1, h0, h1)] cut in it"""
    pos = a0
    for (c0, c1, h0, h1) in sorted(holes):
        if c0 > pos: m.box(*((pos, y0, fixed0, c0, y1, fixed1) if axis == "x" else (fixed0, y0, pos, fixed1, y1, c0)))
        if h0 > y0: m.box(*((c0, y0, fixed0, c1, h0, fixed1) if axis == "x" else (fixed0, y0, c0, fixed1, h0, c1)))
        if h1 < y1: m.box(*((c0, h1, fixed0, c1, y1, fixed1) if axis == "x" else (fixed0, h1, c0, fixed1, y1, c1)))
        pos = c1
    if pos < a1: m.box(*((pos, y0, fixed0, a1, y1, fixed1) if axis == "x" else (fixed0, y0, pos, fixed1, y1, a1)))


def hump(m, axis, a0, a1, w0, w1, y, ramp=5.0):
    """a rise of HUMP across a corridor: a ramp up, a flat top, a ramp down (along `axis`, from a0 to a1; w0..w1 across)
    (built for one side and point-mirrored)"""
    if axis == "x":
        wedge(m, "box", 0, (a0, y, w0, a0 + ramp, y + HUMP, w1), mirror=True)
        m.box(a0 + ramp, y, w0, a1 - ramp, y + HUMP, w1, mirror=True)
        wedge(m, "box", 1, (a1 - ramp, y, w0, a1, y + HUMP, w1), mirror=True)
        m.box(a0 + ramp, y + HUMP - 0.04, w0, a1 - ramp, y + HUMP + 0.02, w0 + 0.4, accent=True, mirror=True)
        m.box(a0 + ramp, y + HUMP - 0.04, w1 - 0.4, a1 - ramp, y + HUMP + 0.02, w1, accent=True, mirror=True)
    else:
        wedge(m, "box", 2, (w0, y, a0, w1, y + HUMP, a0 + ramp), mirror=True)
        m.box(w0, y, a0 + ramp, w1, y + HUMP, a1 - ramp, mirror=True)
        wedge(m, "box", 3, (w0, y, a1 - ramp, w1, y + HUMP, a1), mirror=True)
        m.box(w0, y + HUMP - 0.04, a0 + ramp, w0 + 0.4, y + HUMP + 0.02, a1 - ramp, accent=True, mirror=True)
        m.box(w1 - 0.4, y + HUMP - 0.04, a0 + ramp, w1, y + HUMP + 0.02, a1 - ramp, accent=True, mirror=True)


def layout(m):
    C0, C1 = R0 + WT, R1 - WT     # the corridor: 38..48
    # ---- floors: the ring's ground floor, the ledge (minus the pit and its void corners), the upper storey
    for (x0, z0, x1, z1) in minus((-R1, -R1, R1, R1), [(-R0, -R0, R0, R0)]):
        m.box(x0, -1, z0, x1, 0, z1)
    voids = [(-PIT, -PIT, PIT, PIT), (PIT, PIT, R0, R0), (-R0, PIT, -PIT, R0), (-R0, -R0, -PIT, -PIT), (PIT, -R0, R0, -PIT)]
    ledge = minus((-R0, -R0, R0, R0), voids)
    for (x0, z0, x1, z1) in ledge:
        m.box(x0, -1, z0, x1, 0, z1)
    # every void edge gets an accent rim (the pit and the corner voids), so the edge reads at speed
    for (x0, z0, x1, z1) in ledge:
        if abs(x0) < R0: m.box(x0, 0, z0, x0 + 0.4, 0.03, z1, accent=True)
        if abs(x1) < R0: m.box(x1 - 0.4, 0, z0, x1, 0.03, z1, accent=True)
        if abs(z0) < R0: m.box(x0, 0, z0, x1, 0.03, z0 + 0.4, accent=True)
        if abs(z1) < R0: m.box(x0, 0, z1 - 0.4, x1, 0.03, z1, accent=True)
    # ---- stairs between the storeys: one in each corner of the ring, along the outer wall (16 steps, 0.6 m), so
    # the arms stay clear; a hole over each
    flights = [(-C1 + 0.4, -C1, -C0, -C1 + 3, "x", -1), (C1 - 3, -C1 + 0.4, C1, -C0, "z", -1)]   # (low end on the arm)
    holes = []
    for (x0, z0, x1, z1, ax, r) in flights:
        m.stairs(x0, z0, x1, z1, UP, ax, r, mirror=True)
        holes += [(x0, z0, x1, z1), (-x1, -z1, -x0, -z0)]
    for (x0, z0, x1, z1) in minus((-R1, -R1, R1, R1), [(-R0, -R0, R0, R0)] + holes):
        m.box(x0, UP - 0.5, z0, x1, UP, z1)
    # the roof (and the clip on it, up to the lid: the ring's top is no floor)
    for (x0, z0, x1, z1) in minus((-R1, -R1, R1, R1), [(-R0, -R0, R0, R0)]):
        m.box(x0, TOP, z0, x1, TOP + 0.5, z1)
        m.box(x0, TOP + 0.5, z0, x1, LID, z1, mat="clip")
    m.box(-R1, LID, -R1, R1, LID + 1, R1, mat="clip")
    # ---- the outer walls (plain, to the roof)
    m.box(-R1, -1, -R1, R1, TOP, -R1 + WT, mirror=True)
    m.box(-R1, -1, -R1 + WT, -R1 + WT, TOP, R1 - WT, mirror=True)
    # ---- the humps: one per arm and storey. Ground: in the arm's middle; upper: between the windows and the corner
    # (south arm toward +x, west arm toward +z; their mirrors)
    hump(m, "x", -7.5, 7.5, -C1, -C0, 0.0)
    hump(m, "z", -7.5, 7.5, -C1, -C0, 0.0)
    hump(m, "x", 17, 31, -C1, -C0, UP)
    hump(m, "z", 17, 31, -C1, -C0, UP)
    # ---- the inner walls (they run corner to corner: no notches): per side, ground openings onto the ledge at
    # +-10, upper windows at +-10 (sill 0.9) and the bridge door at 0, and the balcony openings at the north-east
    # and south-west corners
    def openings(storey, balcony):
        y0 = 0.0 if storey == 0 else UP
        hs = [(c - 3, c + 3, y0 + (0 if storey == 0 else 0.9), y0 + (DOOR_H if storey == 0 else WIN_H)) for c in (-OPEN, OPEN)]
        if storey == 1:
            hs.append((-2.5, 2.5, y0, y0 + DOOR_H))
            hs.append((balcony - 3, balcony + 3, y0, y0 + DOOR_H))
        return hs
    for storey in (0, 1):
        y0, y1 = (-1, UP - 0.5) if storey == 0 else (UP - 0.5, TOP)
        hs = openings(storey, -32)
        side_wall(m, -C0, C0, -C0, -R0, y0, y1, hs, "x")
        side_wall(m, -C0, C0, R0, C0, y0, y1, [(-c1, -c0, h0, h1) for (c0, c1, h0, h1) in hs], "x")
        side_wall(m, -R0, R0, -C0, -R0, y0, y1, hs, "z")
        side_wall(m, -R0, R0, R0, C0, y0, y1, [(-c1, -c0, h0, h1) for (c0, c1, h0, h1) in hs], "z")
    # ---- the balconies (south-west and north-east corners, 8 m, over the void corners), accent edges
    m.box(-R0, UP - 0.5, -R0, -28, UP, -28, mirror=True)
    m.box(-28.3, UP - 0.5, -R0, -28, UP + 1.0, -28, accent=True, mirror=True)
    m.box(-R0, UP - 0.5, -28.3, -28.3, UP + 1.0, -28, accent=True, mirror=True)
    # ---- the island: a pillar out of the void, a deck at 8 m, a top block at 12 m with steps up two sides
    m.box(-ISL, -14, -ISL, ISL, UP, ISL, accent=True)
    m.box(-ISL_TOP, UP, -ISL_TOP, ISL_TOP, UP + 4, ISL_TOP)
    m.stairs(-ISL_TOP, -ISL, ISL_TOP, -ISL_TOP, UP + 4, "z", 1, mirror=True, base=UP)
    # ---- two grapple blocks hanging over the pit at 20 m (a third height; nothing walks there)
    m.box(-15, 20, -15, -10, 21.5, -10, accent=True, mirror=True)
    # ---- the bridges at 8 m: north / south whole, east / west broken (a 12 m gap over the pit, x 12..24); each ledge
    # has a stair up beside its bridge's outer part (no rail on that side there)
    BW = 2.5
    m.box(-BW, UP - 0.6, -R0, BW, UP, -ISL, mirror=True)
    m.box(-BW - 0.3, UP - 0.6, -R0, -BW, UP + 0.3, -ISL, accent=True, mirror=True)
    m.box(BW, UP - 0.6, -PIT, BW + 0.3, UP + 0.3, -ISL, accent=True, mirror=True)
    m.stairs(BW, -R0, BW + 3, -PIT - 1, UP, "z", -1, mirror=True)
    m.box(-R0, UP - 0.6, -BW, -24, UP, BW, mirror=True)
    m.box(-12, UP - 0.6, -BW, -ISL, UP, BW, mirror=True)
    m.box(-R0, UP - 0.6, -BW - 0.3, -24, UP + 0.3, -BW, accent=True, mirror=True)
    m.box(-12, UP - 0.6, -BW - 0.3, -ISL, UP + 0.3, -BW, accent=True, mirror=True)
    m.box(-12, UP - 0.6, BW, -ISL, UP + 0.3, BW + 0.3, accent=True, mirror=True)
    m.stairs(-R0, BW, -PIT - 1, BW + 3, UP, "x", -1, mirror=True)
    # ---- spawns: four a side in their quadrant, two on the ground floor and two upstairs near the bridges, facing
    # along the corridor
    for (x, y, z, yaw) in ((-43, 0, -30, 180), (-30, 0, -43, -90), (-43, UP, -12, 180), (-12, UP, -43, -90)):
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in ((-10, 0, -28, 0), (-43, 0, 22, 180), (-43, UP, 8, 0), (35, UP, -43, 90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: the corridors (both storeys), the ledge and the island (the open middle has the sun)
    for t in (-36, -18, 0, 18, 36):
        for y in (5.5, UP + 5.5):
            m.light(t, y, -43, 16, 1.2, fixture=False, mirror=True)
            m.light(-43, y, t, 16, 1.2, fixture=False, mirror=True)
    m.light(0, 14, 0, 16, 0.6, fixture=False)
    decorate(m)


def decorate(m):
    """accent lines only, flush or 5 cm proud: skirting on the corridor walls, frames round the openings, bands on the
    island (nothing in the lanes: corridors stay clear for dashing and wall bounces). The arms differ: the south /
    north ones (whole bridges) have one wide band on the outer wall, the west / east ones (broken bridges) two thin
    ones; the ground storey's band is low, the upper storey's high."""
    C0, C1 = R0 + WT, R1 - WT
    for y in (0.0, UP):
        m.box(-C1, y, -C1, C1, y + 0.2, -C1 + 0.05, accent=True, mirror=True)        # outer walls' skirting
        m.box(-C1, y, -C1 + 0.05, -C1 + 0.05, y + 0.2, C1 - 0.05, accent=True, mirror=True)
    for (y0, y1) in ((2.2, 2.8), (UP + 5.2, UP + 5.8)):     # south / north arms: one wide band
        m.box(-C0, y0, -C1, C0, y1, -C1 + 0.05, accent=True, mirror=True)
    for (y0, y1) in ((2.2, 2.4), (2.6, 2.8), (UP + 5.2, UP + 5.4), (UP + 5.6, UP + 5.8)):   # west / east: two thin
        m.box(-C1, y0, -C0, -C1 + 0.05, y1, C0, accent=True, mirror=True)
    # frames round the inner walls' openings, on the corridor side
    for (c, y0, top) in ((-OPEN, 0.0, DOOR_H), (OPEN, 0.0, DOOR_H), (-OPEN, UP + 0.9, UP + WIN_H), (OPEN, UP + 0.9, UP + WIN_H), (0, UP, UP + DOOR_H)):
        hw = 2.5 if c == 0 else 3
        for (a0, a1) in ((c - hw - 0.25, c - hw), (c + hw, c + hw + 0.25)):
            m.box(a0, y0, -C0 - 0.05, a1, top, -C0, accent=True, mirror=True)
            m.box(-C0 - 0.05, y0, a0, -C0, top, a1, accent=True, mirror=True)
        m.box(c - hw - 0.25, top, -C0 - 0.05, c + hw + 0.25, top + 0.25, -C0, accent=True, mirror=True)
        m.box(-C0 - 0.05, top, c - hw - 0.25, -C0, top + 0.25, c + hw + 0.25, accent=True, mirror=True)
    # the island: base bands round the pillar (box over accent) down to the void, the top block's edge
    for y in (-12, -6, 0, 4):
        m.box(-ISL - 0.05, y, -ISL - 0.05, ISL + 0.05, y + 0.6, -ISL, mirror=True)
        m.box(-ISL - 0.05, y, -ISL, -ISL, y + 0.6, ISL, mirror=True)
    m.box(-ISL_TOP - 0.05, UP + 3.75, -ISL_TOP - 0.05, ISL_TOP + 0.05, UP + 4, -ISL_TOP, accent=True, mirror=True)
    m.box(-ISL_TOP - 0.05, UP + 3.75, -ISL_TOP, -ISL_TOP, UP + 4, ISL_TOP, accent=True, mirror=True)
