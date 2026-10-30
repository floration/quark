"""QK1 RING (1.19.6): ranked VANILLA 1v1, box and accent only. A square donut: a ring corridor 10 m wide on two
storeys (0 and 8 m) round an open middle, a ledge at the ground floor's level round a void pit 40 m across, and an
island in the pit (a deck at 8 m, its top at 12 m). Four bridges at 8 m reach the island from the upper storey: the
north and south ones whole, the east and west ones broken by a 12 m gap (dash or hook). The ring's inner walls
have eight openings a storey into the middle; two corner balconies at 8 m hang over the pit's corners.
Point symmetric, the sun overhead."""
from .prefabs import *

R0, R1 = 38, 48       # the ring corridor: from its inner wall (38) to its outer wall (48)
WT = 1.0              # wall thickness
UP = 8.0              # the upper storey's floor
TOP = 16.0            # the ring's roof (clip over it: not a floor)
PIT = 20              # the pit: -20..20
CORNER = 18           # the ledge's corners are void too (x, z both past 20)
ISL, ISL_TOP = 9, 4   # the island's deck (+-9 at 8 m) and its top block (+-4 at 12 m)
LID = 30.0
DOOR_H = 4.5          # (a duel giant is 3.6 m)


def build():
    m = M("QK1 Ring", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla")
    m.raw("void -14")
    m.raw("palette base 0.6 0.62 0.66 accent 0.95 0.45 0.12 metal 0.3 0.3 0.33 glow 1 0.62 0.25 sky 0.56 0.62 0.74")
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


def layout(m):
    # ---- floors: the ring's ground floor, the ledge (minus the pit and its void corners), the upper storey
    for (x0, z0, x1, z1) in minus((-R1, -R1, R1, R1), [(-R0, -R0, R0, R0)]):
        m.box(x0, -1, z0, x1, 0, z1)
    ledge = minus((-R0, -R0, R0, R0), [(-PIT, -PIT, PIT, PIT), (PIT, PIT, R0, R0), (-R0, PIT, -PIT, R0), (-R0, -R0, -PIT, -PIT), (PIT, -R0, R0, -PIT)])
    for (x0, z0, x1, z1) in ledge:
        m.box(x0, -1, z0, x1, 0, z1)
    # the pit's rim (accent), so the edge reads at speed
    m.box(-PIT, 0, -PIT - 0.4, PIT, 0.03, -PIT, accent=True, mirror=True)
    m.box(-PIT - 0.4, 0, -PIT, -PIT, 0.03, PIT, accent=True, mirror=True)
    # ---- stairs between the storeys: one along the outer wall of each side (mirrored pairs), a hole over each
    flights = [(10, -R1 + WT, 26, -R1 + WT + 3, "x", 1), (R1 - WT - 3, 10, R1 - WT, 26, "z", 1)]
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
    # ---- the inner walls: per side, ground-floor openings onto the ledge at +-14, upper openings at +-14 (sill 0.9)
    # and the bridge doors at 0 (south / north sides lead to the whole bridges, east / west to the broken ones), and the
    # balcony openings at the north-east and south-west corners
    def openings(storey, side_has_balcony_at):
        y0 = 0.0 if storey == 0 else UP
        hs = []
        for c in (-14, 14):
            hs.append((c - 3, c + 3, y0 + (0 if storey == 0 else 0.9), y0 + DOOR_H))
        if storey == 1:
            hs.append((-2.5, 2.5, y0, y0 + DOOR_H))
            if side_has_balcony_at is not None:
                c = side_has_balcony_at
                hs.append((c - 3, c + 3, y0, y0 + DOOR_H))
        return hs
    # south side (z = -38): its balcony opening is at the south-west corner (x = -33); north mirrors it
    for storey in (0, 1):
        y0, y1 = (-1, UP - 0.5) if storey == 0 else (UP - 0.5, TOP)
        hs = openings(storey, -33)
        sw = [(c0, c1, h0, h1) for (c0, c1, h0, h1) in hs]
        side_wall(m, -R0, R0, -R0 - WT, -R0, y0, y1, sw, "x")
        side_wall(m, -R0, R0, R0, R0 + WT, y0, y1, [(-c1, -c0, h0, h1) for (c0, c1, h0, h1) in sw], "x")
        # west side (x = -38): balcony opening at z = -33 (the south-west corner); east mirrors it
        hw = openings(storey, -33)
        side_wall(m, -R0, R0, -R0 - WT, -R0, y0, y1, hw, "z")
        side_wall(m, -R0, R0, R0, R0 + WT, y0, y1, [(-c1, -c0, h0, h1) for (c0, c1, h0, h1) in hw], "z")
    # ---- the balconies (south-west and north-east corners, 8 m, over the void corners), accent edges
    m.box(-R0, UP - 0.5, -R0, -28, UP, -28, mirror=True)
    m.box(-28.3, UP - 0.5, -R0, -28, UP + 1.0, -28, accent=True, mirror=True)
    m.box(-R0, UP - 0.5, -28.3, -28.3, UP + 1.0, -28, accent=True, mirror=True)
    # ---- the island: a pillar out of the void, a deck at 8 m, a top block at 12 m with steps up two sides
    m.box(-ISL, -14, -ISL, ISL, UP, ISL, accent=True)
    m.box(-ISL_TOP, UP, -ISL_TOP, ISL_TOP, UP + 4, ISL_TOP)
    m.stairs(-ISL_TOP, -ISL, ISL_TOP, -ISL_TOP, UP + 4, "z", 1, mirror=True, base=UP)
    # ---- the bridges at 8 m: north / south whole, east / west broken (a 12 m gap over the pit, x 12..24)
    BW = 2.5
    m.box(-BW, UP - 0.6, -R0, BW, UP, -ISL, mirror=True)
    m.box(-BW - 0.3, UP - 0.6, -R0, -BW, UP + 0.3, -ISL, accent=True, mirror=True)
    m.box(BW, UP - 0.6, -R0, BW + 0.3, UP + 0.3, -ISL, accent=True, mirror=True)
    m.box(-R0, UP - 0.6, -BW, -24, UP, BW, mirror=True)
    m.box(-12, UP - 0.6, -BW, -ISL, UP, BW, mirror=True)
    for (x0, x1) in ((-R0, -24), (-12, -ISL)):
        m.box(x0, UP - 0.6, -BW - 0.3, x1, UP + 0.3, -BW, accent=True, mirror=True)
        m.box(x0, UP - 0.6, BW, x1, UP + 0.3, BW + 0.3, accent=True, mirror=True)
    # ---- spawns: four a side on the ground floor of their quadrant, facing along the corridor
    for (x, z, yaw) in ((-43, -16, 180), (-43, -30, 180), (-30, -43, -90), (-16, -43, -90)):
        m.spawn("a", x, 0, z, yaw, "b")
    for (x, y, z, yaw) in ((0, 0, -30, -90), (-30, 0, 10, 180), (-43, UP, 22, 0), (24, UP, -43, 90)):
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
    island (nothing in the lanes: corridors stay clear for dashing and wall bounces)"""
    for y in (0.0, UP):
        m.box(-R1 + WT, y, -R1 + WT, R1 - WT, y + 0.2, -R1 + WT + 0.05, accent=True, mirror=True)        # outer walls
        m.box(-R1 + WT, y, -R1 + WT + 0.05, -R1 + WT + 0.05, y + 0.2, R1 - WT - 0.05, accent=True, mirror=True)
        m.box(-R0, y + 7.2 if y == 0 else y + 7.7, -R1 + WT, R0, (y + 7.5) if y == 0 else y + 7.95, -R1 + WT + 0.05, accent=True, mirror=True)
    # frames round the inner walls' openings, on the corridor side
    for (c, y0, h) in ((-14, 0.0, DOOR_H), (14, 0.0, DOOR_H), (-14, UP + 0.9, DOOR_H - 0.9), (14, UP + 0.9, DOOR_H - 0.9), (0, UP, DOOR_H)):
        hw = 2.5 if c == 0 else 3
        top = (UP if y0 > UP - 1 and c != 0 else y0) + (DOOR_H if c != 0 or y0 >= UP else DOOR_H)
        top = (UP + DOOR_H) if y0 >= UP else DOOR_H
        for (a0, a1) in ((c - hw - 0.25, c - hw), (c + hw, c + hw + 0.25)):
            m.box(a0, y0, -R0 - WT - 0.05, a1, top, -R0 - WT, accent=True, mirror=True)
            m.box(-R0 - WT - 0.05, y0, a0, -R0 - WT, top, a1, accent=True, mirror=True)
        m.box(c - hw - 0.25, top, -R0 - WT - 0.05, c + hw + 0.25, top + 0.25, -R0 - WT, accent=True, mirror=True)
        m.box(-R0 - WT - 0.05, top, c - hw - 0.25, -R0 - WT, top + 0.25, c + hw + 0.25, accent=True, mirror=True)
    # the island: base bands round the pillar (box over accent), the top block's edge
    for y in (-6, 0, 4):
        m.box(-ISL - 0.05, y, -ISL - 0.05, ISL + 0.05, y + 0.6, -ISL, mirror=True)
        m.box(-ISL - 0.05, y, -ISL, -ISL, y + 0.6, ISL, mirror=True)
    m.box(-ISL_TOP - 0.05, UP + 3.75, -ISL_TOP - 0.05, ISL_TOP + 0.05, UP + 4, -ISL_TOP, accent=True, mirror=True)
    m.box(-ISL_TOP - 0.05, UP + 3.75, -ISL_TOP, -ISL_TOP, UP + 4, ISL_TOP, accent=True, mirror=True)
