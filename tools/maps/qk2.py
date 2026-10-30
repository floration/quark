"""QK2 STACK (1.19.6): ranked VANILLA 1v1, box and accent only. A tower of five open floors 9 m apart (0, 9, 18, 27
and the roof at 36), 64 x 64 m. Each floor has two big holes in opposite quadrants, turned 90 degrees from the floor
below, so you drop or hook diagonally through the building; stairs up the outer walls, at opposite ends on
alternate floors. Floor 18 has two balconies cantilevered over its holes. The roof is open sky inside a parapet
(the clip lid at 45 m). Point symmetric, the sun overhead."""
from .prefabs import *

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
    m.raw("palette base 0.7 0.64 0.55 accent 0.12 0.55 0.58 metal 0.3 0.3 0.32 glow 0.5 1 0.95 sky 0.6 0.7 0.82")
    layout(m)
    decorate(m)
    m.raw("setting a stacked tower of open floors, box and accent")
    m.save("qk2", dress=False)


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
    # ---- the stairs (accent steps up the outer walls)
    for lv in range(4):
        (x0, z0, x1, z1, ax, r) = flight(lv)[0]
        m.stairs(x0, z0, x1, z1, LEVELS[lv + 1], ax, r, mirror=True, base=LEVELS[lv], solid=False)
    # ---- the walls: to the roof's parapet (37.2), clip from there to the lid, the lid
    # (windows on every enclosed floor, three a side, 6 x 3 m from a 1.5 m sill, a clip pane in each: daylight in,
    # nobody out)
    top = LEVELS[4] + 1.2
    win = [(-19, -13), (-3, 3), (13, 19)]
    def wall(a0, a1, f0, f1, along):
        holes = [(c0, c1, LEVELS[lv] + 1.5, LEVELS[lv] + 4.5) for lv in range(4) for (c0, c1) in win]
        for (c0, c1, h0, h1) in holes:
            m.box(*((c0, h0, f0, c1, h1, f1) if along == "x" else (f0, h0, c0, f1, h1, c1)), mat="clip")
        # the wall round them: full-height pieces between the windows' columns, and the bands above / below each row
        cols = [a0] + [v for w in win for v in w] + [a1]
        for k in range(0, len(cols) - 1):
            c0, c1 = cols[k], cols[k + 1]
            if k % 2 == 0: m.box(*((c0, -1, f0, c1, top, f1) if along == "x" else (f0, -1, c0, f1, top, c1)))
            else:
                ys = [-1] + [v for lv in range(4) for v in (LEVELS[lv] + 1.5, LEVELS[lv] + 4.5)] + [top]
                for j in range(0, len(ys) - 1, 2):
                    m.box(*((c0, ys[j], f0, c1, ys[j + 1], f1) if along == "x" else (f0, ys[j], c0, f1, ys[j + 1], c1)))
    wall(-H, H, -H, -i, "x"); wall(-H, H, i, H, "x")
    wall(-i, i, -H, -i, "z"); wall(-i, i, i, H, "z")
    m.box(-H, top, -H, H, LID, -i, mat="clip", mirror=True)
    m.box(-H, top, -i, -i, LID, i, mat="clip", mirror=True)
    m.box(-H, LID, -H, H, LID + 1, H, mat="clip")
    # ---- the core: a 12 x 12 m block up the middle through every floor (no line crosses a floor's centre), accent
    # bands at each floor; and a partition wall in each floor's two solid quadrants (5 m: over it only by hook)
    m.box(-6, 0, -6, 6, LEVELS[4], 6)
    for y in LEVELS[1:4]:
        m.box(-6.1, y, -6.1, 6.1, y + 0.3, 6.1, accent=True)
    for lv in range(4):
        y = LEVELS[lv]
        if lv % 2 == 1: m.box(-16.5, y, 6, -15.5, y + 5, 22, mirror=True)        # (NW / SE solid on odd floors)
        else: m.box(6, y, 15.5, 22, y + 5, 16.5, mirror=True)                     # (NE / SW on even ones)
    # ---- floor 18's two balconies, cantilevered 4 m out over its holes (overlooking the holes below too)
    y = LEVELS[2]
    m.box(8, y - 0.5, -24, 12, y, -8, accent=True, mirror=True)
    m.box(11.7, y, -24, 12, y + 1.0, -14, accent=True, mirror=True)
    # ---- cover on the floors: low blocks (crouch cover, 1.2 m) in the solid quadrants, the same on each storey pair
    for lv in range(0, 4):
        y = LEVELS[lv]
        if lv % 2 == 0:
            spots = [(-18, 14, 3, 1.2), (20, 24, 3, 1.2)]       # (a spot that falls in a hole or a stair is skipped)
        else:
            spots = [(14, 14, 3, 1.2), (18, 20, 1.2, 3)]
        for (cx, cz, hw, hd) in spots:
            bx0, bz0, bx1, bz1 = cx - hw, cz - hd, cx + hw, cz + hd
            if any(a < bx1 and c > bx0 and b < bz1 and d > bz0 for (a, b, c, d) in holes_of(lv) + [f[:4] for f in flight(lv)] + [f[:4] for f in (flight(lv - 1) if lv else [])]): continue
            m.box(bx0, y, bz0, bx1, y + 1.2, bz1, mirror=True)
            m.box(bx0 - 0.05, y + 1.2, bz0 - 0.05, bx1 + 0.05, y + 1.3, bz1 + 0.05, accent=True, mirror=True)
    # ---- spawns: each side two on the ground floor and two on floor 27, facing into the floor
    for (x, y, z, yaw) in ((-26, 0, 20, -90), (-14, 0, 27, -90), (-26, 27, -2, -90), (-2, 27, -27, 180)):
        m.spawn("a", x, y, z, yaw, "b")
    for (x, y, z, yaw) in ((0, 9, -14, 0), (-24, 18, 3, -90), (24, 36, 0, 90), (-27, 9, 4, -90)):
        m.spawn("any", x, y, z, yaw, "any")
    # ---- light: a grid on every enclosed floor (the roof has the sun)
    for lv in range(4):
        y = LEVELS[lv] + 6.5
        for (lx, lz) in ((-20, -20), (0, -20), (20, -20), (-20, 0)):
            m.light(lx, y, lz, 20, 1.2, fixture=False, mirror=True)
        for (lx, lz) in ((-10, -26), (26, -10)):
            m.light(lx, y, lz, 14, 0.9, fixture=False, mirror=True)
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
