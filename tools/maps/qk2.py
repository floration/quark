"""QK2 STACK (1.19.6): ranked VANILLA 1v1, box and accent only. A tower of three floors 9 m apart, 46 x 46 m: the
ground (0), an enclosed middle floor (9) and the open roof (18). Each floor above the ground has two big holes in
opposite quadrants, the roof's turned 90 degrees from the middle floor's, so you drop or hook diagonally through the
building and a hole on the roof drops you onto the middle floor's solid part, in the open under the sky; stairs up
the outer walls (the south wall to 9, the east wall to the roof, each with its point twin) and a ladder up a post at
the edge of every hole (the second way up). The middle floor has two perches 3.5 m over it, hung off the core over
its holes (a short stair up, a lip). The roof is open sky inside a parapet (the clip lid at 27 m) with a roofed hut
on the core (two doors) and two blocks. Each floor reads differently: as many accent bands on the core as the
floor's number, its own cover. Point symmetric, the sun overhead.
(1.19.6 fix round 4, q: "qk2 doesnt need that many layers": five floors cut to three; the walkable floor drops from
8,810 to about 5,300 m2, the perches move to the middle floor, the roof sits at 18.)
(1.19.6 fix round 3: shrunk from 64 x 64 to 46 x 46 m, an 8 m core, 13 m holes, the stairs re-run along the walls.)
(1.19.6 fix round 2: perches, the roof hut, ladders, per-floor reads, a 4 x 4 light grid, solid stair undersides,
no fog windows, partitions out from under the holes.)"""
from .prefabs import *
import math

H = 23               # the walls' inner faces at +-22 (outer +-23): 46 x 46 m, fix round 3 (was 64)
WT = 1.0
STEP = 9.0
LEVELS = [0.0, 9.0, 18.0]   # fix round 4: three floors (was five, to 36)
TOP = len(LEVELS) - 1
HOLE = (6, 19)       # a hole spans 6..19 on both axes in its quadrant
CORE = 4.0           # the core: 8 x 8 m up the middle
LID = 27.0


def holes_of(level):
    """the two holes of a floor (level 1..2): NE + SW on the middle floor, NW + SE on the roof"""
    a, b = HOLE
    if level % 2 == 1: return [(a, a, b, b), (-b, -b, -a, -a)]
    return [(a, -b, b, -a), (-b, a, -a, b)]


def flight(level):
    """the stairs from floor `level` up to the next: (x0, z0, x1, z1, axis, rising) and its point twin. Each runs
    along an outer wall beside the next floor's hole (its opening joins that hole) and tops out past the hole's
    end, on that floor's solid part: 0 the south wall going east, 1 the east wall going north"""
    i = H - WT
    r = [(-18, -i, -4, -i + 3, "x", 1), (i - 3, -18, i, -4, "z", 1)][level]
    x0, z0, x1, z1, ax, ri = r
    return [r, (-x1, -z1, -x0, -z0, ax, -ri)]
def build():
    m = M("QK2 Stack", "1v1", sun=(0, 1, 0))
    m.also = ["duel", "vs", "tourney"]
    m.raw("ranked vanilla vampire instagib")   # (q, 2026-10-31: every qk map on all three ladders)
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
    C = CORE
    a, b = HOLE
    # ---- the ground floor and the two floors above it, each with its holes and the stair openings through it
    m.box(-H, -1, -H, H, 0, H)
    for lv in range(1, TOP + 1):
        y = LEVELS[lv]
        cut = holes_of(lv) + [(f[0], f[1], f[2], f[3]) for f in flight(lv - 1)]
        for (x0, z0, x1, z1) in minus((-i, -i, i, i), cut):
            m.box(x0, y - 0.5, z0, x1, y, z1)
        for (x0, z0, x1, z1) in holes_of(lv):   # the holes' rims (accent, flush lips round each, not on the stair side)
            m.box(x0 - 0.3, y, z0 - 0.3, x1 + 0.3, y + 0.04, z0, accent=True)
            m.box(x0 - 0.3, y, z1, x1 + 0.3, y + 0.04, z1 + 0.3, accent=True)
            m.box(x0 - 0.3, y, z0, x0, y + 0.04, z1, accent=True)
            m.box(x1, y, z0, x1 + 0.3, y + 0.04, z1, accent=True)
    # ---- the stairs up the outer walls, closed underneath (no head-bump pockets along the wall)
    for lv in range(TOP):
        (x0, z0, x1, z1, ax, r) = flight(lv)[0]
        solid_flight(m, x0, z0, x1, z1, LEVELS[lv], LEVELS[lv + 1], ax, r)
    # ---- the second way up: a ladder up a post at the inner edge of each hole, from the floor below
    mid = (a + b) / 2
    for lv in range(TOP):
        y0, y1 = LEVELS[lv], LEVELS[lv + 1]
        (x0, z0, x1, z1) = holes_of(lv + 1)[0]     # (its twin by the mirror)
        if x0 > 0 and z0 > 0:      # NE hole: the post on its south edge (its west edge holds the perch)
            m.box(mid - 0.5, y0, a - 1, mid + 0.5, y1 - 0.5, a, mirror=True)
            m.ladder(mid, a + 0.12, y0, y1, "n", rail="accent"); m.ladder(-mid, -a - 0.12, y0, y1, "s", rail="accent")
        else:                      # SE hole: the post on its north edge
            m.box(mid - 0.5, y0, -a, mid + 0.5, y1 - 0.5, -a + 1, mirror=True)
            m.ladder(mid, -a - 0.12, y0, y1, "s", rail="accent"); m.ladder(-mid, a + 0.12, y0, y1, "n", rail="accent")
    # ---- the walls: solid to the roof's parapet (19.2), clip from there to the lid, the lid
    top = LEVELS[TOP] + 1.2
    m.box(-H, -1, -H, H, top, -i, mirror=True)
    m.box(-H, -1, -i, -i, top, i, mirror=True)
    m.box(-H, top, -H, H, LID, -i, mat="clip", mirror=True)
    m.box(-H, top, -i, -i, LID, i, mat="clip", mirror=True)
    m.box(-H, LID, -H, H, LID + 1, H, mat="clip")
    # ---- the core: an 8 x 8 m block up the middle through every floor. On each floor as many accent bands round it
    # as the floor's number (ground 1, middle 2), so you know where you are
    m.box(-C, 0, -C, C, LEVELS[TOP], C)
    for lv in range(TOP):
        for k in range(lv + 1):
            y = LEVELS[lv] + 2.0 + 0.8 * k
            m.box(-C - 0.05, y, -C - 0.05, C + 0.05, y + 0.4, C + 0.05, accent=True)
    # pillars on the axes between the holes, floor to floor (cover in the long open lines round the core)
    for lv in range(TOP):
        for (px, pz) in ((-13, 0), (0, -13)):
            m.box(px - 1, LEVELS[lv], pz - 1, px + 1, LEVELS[lv + 1] - 0.5, pz + 1, mirror=True)
    # ---- the middle floor's two perches, 3.5 m over it, hung off the core over its holes (NE and SW): a short stair
    # up from the floor south of the hole, a waist-high lip on the hole side and the far end (5 m under the roof)
    y, py = LEVELS[1], LEVELS[1] + 3.5
    m.box(C, y, a, a, py, b, mirror=True)                            # (its inner part stands on the floor)
    m.box(a, py - 0.5, a, a + 2.5, py, b, accent=True, mirror=True)
    m.box(a + 2.2, py, a, a + 2.5, py + 1.0, b, accent=True, mirror=True)
    m.box(C, py, b, a + 2.5, py + 1.0, b + 0.3, accent=True, mirror=True)
    solid_flight(m, C, -1, a + 2.5, a, y, py, "z", 1)
    # ---- the roof: a hut on the core (walls 5 m, a roof, doors north and south) and two blocks
    R = LEVELS[TOP]
    for (x0, x1) in ((-C, -2), (2, C)):
        m.box(x0, R, -C, x1, R + 5, -C + 0.4, mirror=True)
    m.box(-2, R + 4.2, -C, 2, R + 5, -C + 0.4, mirror=True)
    m.box(-C, R, -C + 0.4, -C + 0.4, R + 5, C - 0.4, mirror=True)
    m.box(-C - 0.4, R + 5, -C - 0.4, C + 0.4, R + 5.5, C + 0.4, accent=True)
    m.box(10, R, 11, 14, R + 2.5, 14, mirror=True)
    m.box(9.95, R + 2.5, 10.95, 14.05, R + 2.6, 14.05, accent=True, mirror=True)
    # ---- cover on the floors: low blocks (crouch cover, 1.2 m) on the ground; the middle floor a taller pair (2.2 m)
    # in its solid quadrants, under the roof's holes (cover from a shot down through them)
    for lv in range(0, TOP):
        y = LEVELS[lv]
        if lv == 0:
            spots = [(-13, 13, 2.5, 1, 1.2), (14, 13, 1, 2.5, 1.2)]       # (a spot in a hole or on a stair is skipped)
        else:
            spots = [(-17, 10, 1.5, 1.5, 2.2), (-9, 15, 2, 1, 2.2)]
        for (cx, cz, hw, hd, hh) in spots:
            bx0, bz0, bx1, bz1 = cx - hw, cz - hd, cx + hw, cz + hd
            if any(a_ < bx1 and c_ > bx0 and b_ < bz1 and d_ > bz0 for (a_, b_, c_, d_) in (holes_of(lv) if lv else []) + [f[:4] for f in flight(lv)] + [f[:4] for f in (flight(lv - 1) if lv else [])]): continue
            m.box(bx0, y, bz0, bx1, y + hh, bz1, mirror=True)
            m.box(bx0 - 0.05, y + hh, bz0 - 0.05, bx1 + 0.05, y + hh + 0.1, bz1 + 0.05, accent=True, mirror=True)
    # ---- spawns: each side one on each floor (the roof too), in a corner outside the holes above, its enemy twin across the core
    for (x, y, z, yaw) in SPAWNS_A:
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in SPAWNS_ANY:
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: a 4 x 4 grid on every enclosed floor, the hut (the roof has the sun)
    for lv in range(TOP):
        y = LEVELS[lv] + 6.8
        for (lx, lz) in ((-16, -16), (-6, -16), (6, -16), (16, -16), (-16, -6), (-6, -6), (6, -6), (16, -6)):
            m.light(lx, y, lz, 13, 1.3, fixture=False, mirror=True)
    m.light(0, R + 3.5, 0, 7, 0.9, fixture=False)


SPAWNS_A = [(-17, 0, 17, -45), (-17, 9, -2, -90), (-20, 9, 20, -45), (-20, 18, -20, -135)]
SPAWNS_ANY = [(0, 9, -10, 0), (12, 0, -16, 0), (16, 18, 17, 90)]


def decorate(m):
    """accent only, flush or 5 cm proud and on walls: a band round the facade at each floor, the parapet's cap,
    skirting inside, the stair stringers' edge"""
    i = H - WT
    for y in LEVELS[1:]:
        m.box(-H - 0.05, y - 0.5, -H - 0.05, H + 0.05, y, -H, accent=True, mirror=True)
        m.box(-H - 0.05, y - 0.5, -H, -H, y, H, accent=True, mirror=True)
    top = LEVELS[TOP] + 1.2
    m.box(-H - 0.05, top, -H - 0.05, H + 0.05, top + 0.15, -i, accent=True, mirror=True)
    m.box(-H - 0.05, top, -i, -i, top + 0.15, i, accent=True, mirror=True)
    for y in LEVELS[:TOP]:
        m.box(-i, y, -i, i, y + 0.2, -i + 0.05, accent=True, mirror=True)
        m.box(-i, y, -i + 0.05, -i + 0.05, y + 0.2, i - 0.05, accent=True, mirror=True)
