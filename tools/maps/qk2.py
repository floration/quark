"""QK2 STACK (1.19.6): ranked VANILLA 1v1, box and accent only. A tower of five open floors 9 m apart (0, 9, 18, 27
and the roof at 36), 64 x 64 m. Each floor has two big holes in opposite quadrants, turned 90 degrees from the floor
below, so you drop or hook diagonally through the building; stairs up the outer walls, at opposite ends on
alternate floors, and a ladder up a post at the edge of every hole (the second way up). Floor 18 has two perches
3.5 m over it, hung off the core over its holes (a short stair up, a lip). The roof is open sky inside a parapet
(the clip lid at 45 m) with a roofed hut on the core (two doors) and two blocks. Each floor reads differently: as
many accent bands on the core as the floor's number, its own cover. Point symmetric, the sun overhead.
(1.19.6 fix round 2: perches, the roof hut, ladders, per-floor reads, a 4 x 4 light grid, solid stair undersides,
no fog windows, partitions out from under the holes.)"""
from .prefabs import *
import math

H = 32               # the walls' inner faces at +-31 (outer +-32)
WT = 1.0
STEP = 9.0
LEVELS = [0.0, 9.0, 18.0, 27.0, 36.0]
HOLE = (8, 24)       # a hole spans 8..24 on both axes in its quadrant
LID = 45.0


def holes_of(level):
    """the two holes of a floor (level 1..4): NE + SW on odd floors, NW + SE on even ones"""
    a, b = HOLE
    if level % 2 == 1: return [(a, a, b, b), (-b, -b, -a, -a)]
    return [(a, -b, b, -a), (-b, a, -a, b)]


def flight(level):
    """the stairs from floor `level` up to the next: (x0, z0, x1, z1, axis, rising) and its point twin"""
    i = H - WT
    if level in (0, 2):
        r = (-26, -i, -8, -i + 3, "x", 1) if level == 0 else (8, -i, 26, -i + 3, "x", 1)
    else:
        r = (-i, -26, -i + 3, -8, "z", 1) if level == 1 else (-i, 8, -i + 3, 26, "z", 1)
    x0, z0, x1, z1, ax, ri = r
    return [r, (-x1, -z1, -x0, -z0, ax, -ri)]


def build():
    m = M("QK2 Stack", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla")
    m.raw("palette base 0.8 0.74 0.64 accent 0.12 0.55 0.58 metal 0.3 0.3 0.32 glow 0.5 1 0.95 sky 0.6 0.7 0.82")
    layout(m)
    decorate(m)
    m.raw("setting a stacked tower of open floors, box and accent")
    m.save("qk2", dress=False)


def solid_flight(m, x0, z0, x1, z1, base, top, axis, rising, mirror=True):
    """stairs from one floor to the next, closed underneath down to that floor only (stairs(solid=True) would fill
    down to y 0 through the floors below): the flight's slabs plus a block under each step"""
    m.stairs(x0, z0, x1, z1, top, axis, rising, mirror=mirror, base=base, solid=False)
    n = max(1, math.ceil((top - base) / 0.5 - 1e-6)); length = (x1 - x0) if axis == "x" else (z1 - z0)
    for k in range(n):
        a = length * k / n; b = length * (k + 1) / n
        h = base + (top - base) * ((k + 1) if rising > 0 else (n - k)) / n
        if h - 1 <= base: continue
        if axis == "x": m.box(x0 + a, base, z0, x0 + b, h - 1, z1, mirror=mirror)
        else: m.box(x0, base, z0 + a, x1, h - 1, z0 + b, mirror=mirror)


def layout(m):
    i = H - WT
    # ---- the ground floor and the four floors above it, each with its holes and the stair openings through it
    m.box(-H, -1, -H, H, 0, H)
    for lv in range(1, 5):
        y = LEVELS[lv]
        cut = holes_of(lv) + [(f[0], f[1], f[2], f[3]) for f in flight(lv - 1)]
        for (x0, z0, x1, z1) in minus((-i, -i, i, i), cut):
            m.box(x0, y - 0.5, z0, x1, y, z1)
        for (x0, z0, x1, z1) in holes_of(lv):   # the holes' rims (accent, flush lips round each)
            m.box(x0 - 0.3, y, z0 - 0.3, x1 + 0.3, y + 0.04, z0, accent=True)
            m.box(x0 - 0.3, y, z1, x1 + 0.3, y + 0.04, z1 + 0.3, accent=True)
            m.box(x0 - 0.3, y, z0, x0, y + 0.04, z1, accent=True)
            m.box(x1, y, z0, x1 + 0.3, y + 0.04, z1, accent=True)
    # ---- the stairs up the outer walls, closed underneath (no head-bump pockets along the wall)
    for lv in range(4):
        (x0, z0, x1, z1, ax, r) = flight(lv)[0]
        solid_flight(m, x0, z0, x1, z1, LEVELS[lv], LEVELS[lv + 1], ax, r)
    # ---- the second way up: a ladder up a post at the core-side edge of each hole, from the floor below
    a, b = HOLE
    for lv in range(4):
        y0, y1 = LEVELS[lv], LEVELS[lv + 1]
        for (x0, z0, x1, z1) in holes_of(lv + 1)[:1]:     # (its twin by the mirror)
            if x0 > 0 and z0 > 0:      # NE hole: the post on its west edge (x 7..8), the ladder facing west
                m.box(a - 1, y0, 15.5, a, y1 - 0.5, 16.5, mirror=True)
                m.ladder(a + 0.12, 16, y0, y1, "w", rail="accent"); m.ladder(-a - 0.12, -16, y0, y1, "e", rail="accent")
            else:                      # SE hole (x 8..24, z -24..-8): the post on its north edge (z -8..-7)
                m.box(15.5, y0, -a, 16.5, y1 - 0.5, -a + 1, mirror=True)
                m.ladder(16, -a - 0.12, y0, y1, "s", rail="accent"); m.ladder(-16, a + 0.12, y0, y1, "n", rail="accent")
    # ---- the walls: solid to the roof's parapet (37.2), clip from there to the lid, the lid
    top = LEVELS[4] + 1.2
    m.box(-H, -1, -H, H, top, -i, mirror=True)
    m.box(-H, -1, -i, -i, top, i, mirror=True)
    m.box(-H, top, -H, H, LID, -i, mat="clip", mirror=True)
    m.box(-H, top, -i, -i, LID, i, mat="clip", mirror=True)
    m.box(-H, LID, -H, H, LID + 1, H, mat="clip")
    # ---- the core: a 12 x 12 m block up the middle through every floor (no line crosses a floor's centre). On each
    # floor as many accent bands round it as the floor's number (ground 1 .. 27: 4), so you know where you are
    m.box(-6, 0, -6, 6, LEVELS[4], 6)
    for lv in range(4):
        for k in range(lv + 1):
            y = LEVELS[lv] + 2.0 + 0.8 * k
            m.box(-6.05, y, -6.05, 6.05, y + 0.4, 6.05, accent=True)
    # a partition in each floor's two solid quadrants (5 m: over it only by hook), out past the next floor's hole
    # (a drop or a pull through that hole doesn't catch on it)
    for lv in range(4):
        y = LEVELS[lv]
        if lv % 2 == 1: m.box(-25.5, y, 6, -24.5, y + 5, 22, mirror=True)        # (NW / SE solid on odd floors)
        else: m.box(6, y, 24.5, 22, y + 5, 25.5, mirror=True)                     # (NE / SW on even ones)
    # ---- floor 18's two perches, 3.5 m over it, hung off the core over its holes (SE and NW): a short stair up
    # from the floor north of the hole, a waist-high lip on the hole side and the far end
    y, py = LEVELS[2], LEVELS[2] + 3.5
    m.box(6, y, -22, 8, py, -8, mirror=True)                      # (its inner part stands on the floor by the core)
    m.box(8, py - 0.5, -22, 10.5, py, -8, accent=True, mirror=True)
    m.box(10.2, py, -22, 10.5, py + 1.0, -8, accent=True, mirror=True)
    m.box(6, py, -22.3, 10.5, py + 1.0, -22, accent=True, mirror=True)
    solid_flight(m, 6, -8, 10.5, -1, y, py, "z", -1)
    # ---- the roof: a hut on the core (walls 5 m, a roof, doors north and south) and two blocks
    R = LEVELS[4]
    for (x0, x1) in ((-6, -2), (2, 6)):
        m.box(x0, R, -6, x1, R + 5, -5.6, mirror=True)
    m.box(-2, R + 4.2, -6, 2, R + 5, -5.6, mirror=True)
    m.box(-6, R, -5.6, -5.6, R + 5, 5.6, mirror=True)
    m.box(-6.4, R + 5, -6.4, 6.4, R + 5.5, 6.4, accent=True)
    m.box(14, R, 14, 18, R + 2.5, 17, mirror=True)
    m.box(13.95, R + 2.5, 13.95, 18.05, R + 2.6, 17.05, accent=True, mirror=True)
    # ---- cover on the floors: low blocks (crouch cover, 1.2 m) in the solid quadrants; floor 27 has its own (a
    # taller L of two blocks), so it doesn't copy floor 9
    for lv in range(0, 4):
        y = LEVELS[lv]
        if lv % 2 == 0:
            spots = [(-18, 14, 3, 1.2, 1.2), (20, 24, 3, 1.2, 1.2)]       # (a spot that falls in a hole or a stair is skipped)
        elif lv == 1:
            spots = [(14, 14, 3, 1.2, 1.2), (18, 20, 1.2, 3, 1.2)]
        else:
            spots = [(-19, 6, 1.5, 1.5, 2.2), (-12, 15, 2.5, 1, 2.2)]
        for (cx, cz, hw, hd, hh) in spots:
            bx0, bz0, bx1, bz1 = cx - hw, cz - hd, cx + hw, cz + hd
            if any(a_ < bx1 and c_ > bx0 and b_ < bz1 and d_ > bz0 for (a_, b_, c_, d_) in holes_of(lv) + [f[:4] for f in flight(lv)] + [f[:4] for f in (flight(lv - 1) if lv else [])]): continue
            m.box(bx0, y, bz0, bx1, y + hh, bz1, mirror=True)
            m.box(bx0 - 0.05, y + hh, bz0 - 0.05, bx1 + 0.05, y + hh + 0.1, bz1 + 0.05, accent=True, mirror=True)
    # ---- spawns: each side two on the ground floor and two on floor 27, facing into the floor
    for (x, y, z, yaw) in ((-26, 0, 20, -90), (-14, 0, 27, -90), (-26, 27, -2, -90), (-2, 27, -27, 180)):
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in ((0, 9, -14, 0), (-24, 18, 3, -90), (19, 36, 21, 90), (-27, 9, 4, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: a 4 x 4 grid on every enclosed floor, the hut (the roof has the sun)
    for lv in range(4):
        y = LEVELS[lv] + 6.8
        for (lx, lz) in ((-24, -24), (-8, -24), (8, -24), (24, -24), (-24, -8), (-8, -8), (8, -8), (24, -8)):
            m.light(lx, y, lz, 16, 1.3, fixture=False, mirror=True)
    m.light(0, R + 3.5, 0, 9, 0.9, fixture=False)
    # pillars on the axes of every enclosed floor (between the holes: cover in the long open lines)
    for lv in range(4):
        for (px, pz) in ((-20, 0), (0, -20)):
            m.box(px - 1, LEVELS[lv], pz - 1, px + 1, LEVELS[lv + 1] - 0.5, pz + 1, mirror=True)


def decorate(m):
    """accent only, flush or 5 cm proud and on walls: a band round the facade at each floor, the parapet's cap,
    skirting inside, the stair stringers' edge"""
    i = H - WT
    for y in LEVELS[1:]:
        m.box(-H - 0.05, y - 0.5, -H - 0.05, H + 0.05, y, -H, accent=True, mirror=True)
        m.box(-H - 0.05, y - 0.5, -H, -H, y, H, accent=True, mirror=True)
    top = LEVELS[4] + 1.2
    m.box(-H - 0.05, top, -H - 0.05, H + 0.05, top + 0.15, -i, accent=True, mirror=True)
    m.box(-H - 0.05, top, -i, -i, top + 0.15, i, accent=True, mirror=True)
    for y in LEVELS[:4]:
        m.box(-i, y, -i, i, y + 0.2, -i + 0.05, accent=True, mirror=True)
        m.box(-i, y, -i + 0.05, -i + 0.05, y + 0.2, i - 0.05, accent=True, mirror=True)
