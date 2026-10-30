"""MUSEUM (1.19.6): hide and seek (tag, free for all) in a natural history museum at night, 110 x 80 m.
Four levels: the basement (-6), the ground floor (0), the galleries (8) and the roof (16.4) round the Great Hall's
glass-topped hip dome (26.4).
  Ground: the entrance hall (north: the spawns, the grand stair up to the mezzanine, a cloakroom), the Great Hall
  (centre, 40 x 24 m, open to the dome: the dinosaur on its plinth, climbable by its tail to the skull at 9 m, the
  whale hung under the tie beams), Mammals (west: dioramas along the west wall with a crawlway behind their
  backdrops from the shop to the fossil lab), Minerals (east: low cases, the crystal cave walk-through with side
  niches and a crawl on top), the shop (north-west), the temporary exhibition (north-east: a maze of thin drywall you
  can dash through), the fossil lab (south-west: stair down), the service hall (south: the service stair up, a stair
  down, the goods-lift cage with its ladder from the basement to the galleries) and the Egyptian hall (south-east: a
  mastaba with a narrow passage to the sarcophagus chamber).
  Galleries: the ring round the Great Hall (4 m, railed), the mezzanine over the entrance, Birds (west: tall cases,
  a cliff diorama with ledges, the roof ladder), Insects (south-west), the fossil gallery (south), the reading room
  (south-east) and Ocean (east: dark, blue, the giant squid, three deep-sea alcoves): a loop round the hall.
  Basement (under the south strip): the archive (compact shelving, 1.4 m aisles, a frosted-glass office), the
  workshop and the boiler room.
  Roof: a U round the dome (the one ladder up, from Birds), plant and skylights; the lower roofs are clipped off.
Hide: hiding spots on every level, each slow to reach (a dead end, a crawl, a ladder or three corners); clear glass
cases never hide anyone (frosted glass does). Every spot is under 30 s of walking from the entrance hall.
(1.19.6 fix round, all review notes YES: the dome is stucco cut flat at 23 with a two-tier axis-aligned glass lantern
(no glass cut brushes); the roof is the west and south arms only (the east arm clipped), the plant room by the ladder
(15 s walk, was 24.5); every room has a pressed-tin ceiling skin, beams or coffers, skirting, cornices and stone door
surrounds; exhibits: wall cases, crystal points and geode arches in Minerals, a meteorite and a giant geode as
sightline breaks, a fossil wall in the lab, cut-box birds on perches, a stepped seabird cliff with nests, a pylon
gateway and four seated statues in Egypt, hieroglyph bands, a squid of cut-box arms and kelp tanks in Ocean, a curved
ribcage, a jawed skull and feet on the dinosaur; brighter ground-floor halls and a strong dome light; crawl hatches
1.4 m; the Egyptian floor and mastaba top no longer a breakable material; an ffa-any spawn set over every floor.)"""
from .prefabs import M
from .m6kit import Kit, wall_x, wall_z, slab, yaw_to, minus, tiles, num

HX, HZ = 55, 40
B, G, U, R = -6.0, 0.0, 8.0, 16.4      # basement, ground, gallery, roof floors
CAP = 23.0                              # the dome's flat top: the glass lantern stands on it
LID = 72.0                              # the clip lid (45 m over the dome's top)
EXT = "limestoneashlar"


def face(k, mat, box, top, t, extra=()):
    """a plate t thick under the plane y <= d - a x - c z (top = (a, 1, c, d)), cut to box and extra planes"""
    a, _, c, d = top
    n = (a * a + 1 + c * c) ** 0.5
    k.cut(mat, box, [top, (-a, -1, -c, -(d - t * n))] + list(extra))


def hip_dome(k, X0, Z0, X1, Z1, ye, s, cap, mat, t=0.3, rafters=()):
    """a hipped roof over the rect (its ridge along x) from the eave ye at slope s, cut off flat at cap (the opening
    there takes the lantern); rafters: x positions of castiron rafters under the north and south faces"""
    w = (Z1 - Z0) / 2; zm = Z0 + w
    y0 = ye - 0.6
    flat = (0, 1, 0, cap)
    faces = [   # (top plane, region planes)
        ("n", (0, 1, -s, ye - s * Z0), [(-1, 0, 1, Z0 - X0), (1, 0, 1, X1 + Z0)]),
        ("s", (0, 1, s, ye + s * Z1), [(-1, 0, -1, -X0 - Z1), (1, 0, -1, X1 - Z1)]),
        ("w", (-s, 1, 0, ye - s * X0), [(1, 0, -1, X0 - Z0), (1, 0, 1, X0 + Z1)]),
        ("e", (s, 1, 0, ye + s * X1), [(-1, 0, -1, -X1 - Z0), (-1, 0, 1, Z1 - X1)]),
    ]
    for (side, top, reg) in faces:
        rect = {"n": (X0, Z0, X1, zm), "s": (X0, zm, X1, Z1), "w": (X0, Z0, X0 + w, Z1), "e": (X1 - w, Z0, X1, Z1)}[side]
        face(k, mat, (rect[0], y0, rect[1], rect[2], cap, rect[3]), top, t, reg)
        if side in "ns":
            for x in rafters:   # under the plate: castiron rafters (the hooks inside the dome)
                a, _, c, dd = top
                n = (1 + c * c) ** 0.5
                face(k, "castiron", (x - 0.15, y0, (Z0 if side == "n" else zm), x + 0.15, cap, (zm if side == "n" else Z1)),
                     (a, 1, c, dd - t * n), 0.35, reg)


def lantern(k, x0, z0, x1, z1, y, tiers=((0.0, 1.8), (6.0, 1.0))):
    """the dome's skylight: axis-aligned glass in tiers (inset, height) over the flat top's opening, castiron frames"""
    hole = None
    for (i, (ins, h)) in enumerate(tiers):
        a, b, c, d = x0 + ins, z0 + ins * (z1 - z0) / (x1 - x0), x1 - ins, z1 - ins * (z1 - z0) / (x1 - x0)
        for (p0, p1, q, ax) in ((a, c, b, "x"), (a, c, d, "x"), (b, d, a, "z"), (b, d, c, "z")):
            if ax == "x": k.box("glass", p0, y, q - 0.075, p1, y + h, q + 0.075)
            else: k.box("glass", q - 0.075, y, p0, q + 0.075, y + h, p1)
        n = max(2, int(round((c - a) / 3.0)))
        for j in range(n + 1):            # mullions (north and south) and the corner posts
            x = a + (c - a) * j / n
            for q in (b, d): k.box("castiron", x - 0.1, y, q - 0.12, x + 0.1, y + h, q + 0.12)
        for q in (a, c): k.box("castiron", q - 0.12, y, (b + d) / 2 - 0.1, q + 0.12, y + h, (b + d) / 2 + 0.1)
        top = y + h
        nxt = tiers[i + 1][0] if i + 1 < len(tiers) else None
        roof = (a, b, c, d)
        holes = []
        if nxt is not None:
            holes = [(x0 + nxt, z0 + nxt * (z1 - z0) / (x1 - x0), x1 - nxt, z1 - nxt * (z1 - z0) / (x1 - x0))]
        for (p, q, r_, t_) in minus(roof, holes):
            k.box("glass", p, top, q, r_, top + 0.15, t_)
        for (p0, p1, q) in ((a, c, b), (a, c, d)): k.box("castiron", p0 - 0.12, top, q - 0.12, p1 + 0.12, top + 0.25, q + 0.12)
        for q in (a, c): k.box("castiron", q - 0.12, top, b, q + 0.12, top + 0.25, d)
        for j in range(1, n):             # glazing bars over the pane roof
            x = a + (c - a) * j / n
            k.box("castiron", x - 0.06, top + 0.15, b, x + 0.06, top + 0.25, d)
        y = top + 0.15


def rod(k, mat, p0, p1, r):
    """a square rod from p0 to p1 (x, y, z), r its half width: a cut box (tentacles, ribs, crystals, perches)"""
    import math
    d = [p1[i] - p0[i] for i in range(3)]
    L = math.sqrt(sum(c * c for c in d))
    d = [c / L for c in d]
    h = (-d[2], 0.0, d[0])
    hl = math.hypot(h[0], h[2])
    h = (1.0, 0.0, 0.0) if hl < 1e-6 else (h[0] / hl, 0.0, h[2] / hl)
    v = (d[1] * h[2] - d[2] * h[1], d[2] * h[0] - d[0] * h[2], d[0] * h[1] - d[1] * h[0])
    planes = []
    for n in (h, v):
        c = sum(n[i] * p0[i] for i in range(3))
        planes += [(n[0], n[1], n[2], c + r), (-n[0], -n[1], -n[2], -c + r)]
    box = tuple(min(p0[i], p1[i]) - r for i in range(3)) + tuple(max(p0[i], p1[i]) + r for i in range(3))
    k.cut(mat, (box[0], box[1], box[2], box[3], box[4], box[5]), planes)


def chain(k, mat, pts, r):
    for (a, b) in zip(pts, pts[1:]): rod(k, mat, a, b, r)


# every room wall goes through wx / wz: they record it, so trims() can dress both its faces afterwards
WALLS = []
CEIL = {B: G - 0.5, G: U - 0.5, U: R - 0.5}   # each storey's ceiling (under its skin)


def wx(k, mat, x0, x1, z, y0, y1, holes=(), t=0.4, glass=None, trim=True):
    wall_x(k, mat, x0, x1, z, y0, y1, holes, t=t, glass=glass)
    if trim: WALLS.append(("x", min(x0, x1), max(x0, x1), z, t, y0, y1, list(holes)))


def wz(k, mat, z0, z1, x, y0, y1, holes=(), t=0.4, glass=None, trim=True):
    wall_z(k, mat, z0, z1, x, y0, y1, holes, t=t, glass=glass)
    if trim: WALLS.append(("z", min(z0, z1), max(z0, z1), x, t, y0, y1, list(holes)))


def trims(k):
    """skirting on both faces of every recorded wall, a cornice where it meets the ceiling, a stone surround on each
    doorway (both faces, out of the opening): all within 0.25 m of the wall"""
    from .m6kit import _spans
    for (ax, a0, a1, c, t, y0, y1, holes) in WALLS:
        def put(mat, p0, p1, q0, q1, ya, yb):
            if p1 - p0 < 0.05: return
            if ax == "x": k.box(mat, p0, ya, q0, p1, yb, q1)
            else: k.box(mat, q0, ya, p0, q1, yb, p1)
        cy = CEIL.get(y0, 99.0)
        ceil = y1 >= cy
        for sd in (-1, 1):
            f = c + sd * t / 2
            q0, q1 = sorted((f, f + sd * 0.06))
            c0, c1 = sorted((f, f + sd * 0.22))
            for sp in _spans(a0, a1, holes):
                if sp[0] == "solid":
                    put("polishedgranite", sp[1], sp[2], q0, q1, y0, y0 + 0.22)
                else:
                    lo, hi = sp[1], sp[2]
                    h = sp[3] if len(sp) > 3 else 3.2
                    sill = sp[4] if len(sp) > 4 else 0.0
                    if sill > 0 or hi - lo < 2.0 or h > y1 - y0 - 0.5: continue
                    e0, e1 = sorted((f, f + sd * 0.1))
                    put("marble", lo - 0.3, lo, e0, e1, y0, y0 + h + 0.3)
                    put("marble", hi, hi + 0.3, e0, e1, y0, y0 + h + 0.3)
                    put("marble", lo - 0.3, hi + 0.3, e0, e1, y0 + h, y0 + h + 0.3)
            if ceil:
                put("stucco", a0, a1, c0, c1, cy - 0.3, cy)


def decorate(k):
    """sparingly, on walls, ceilings and edges (the lanes stay clear: momentum is huge)"""
    # exit signs (green light blocks) over the main doorways
    for (x, y, z, ax) in [(0, 5.4, -14, "x"), (0, 5.4, 10, "x"), (-20, 5.4, -2, "z"), (20, 5.4, -2, "z"), (-20, 4.0, -32, "z"),
                          (20, 4.0, -32, "z"), (0, U + 3.6, 10, "x"), (-20, U + 3.6, -2, "z"), (20, U + 3.6, -2, "z"),
                          (-40, 4.0, -24, "x"), (40, 4.0, -24, "x"), (-40, 4.0, 10, "x"), (39.3, 4.0, 10, "x")]:
        if ax == "x": k.box("glow", x - 0.6, y, z - 0.3, x + 0.6, y + 0.35, z + 0.3)
        else: k.box("glow", x - 0.3, y, z - 0.6, x + 0.3, y + 0.35, z + 0.6)
    # the entrance: pilasters down its side walls, the bronze front doors (shut), benches against the walls
    for z in (-38, -27, -23):
        for x in (-19.8, 19.2):
            k.box("marble", x, 0, z - 0.5, x + 0.6, 12.4, z + 0.5)
    k.box("bronze", -3, 0, -40, 3, 6, -39.8)
    k.box("bronze", -0.1, 0, -40.1, 0.1, 6, -39.7)
    for x in (-16, 12):
        k.box("wideplanks", x, 0, -39.6, x + 4, 0.5, -39)
    # the Great Hall: pilasters between the doors, plaques
    for x in (-14, -8, 8, 14):
        for (z0, z1) in ((-13.6, -13.2), (9.2, 9.6)):
            k.box("polishedgranite", x - 0.4, 0, z0, x + 0.4, R, z1)
    for (x, z) in ((10, 5), (-10, -8)):
        k.box("brass", x - 0.4, 0, z - 0.3, x + 0.4, 1.1, z + 0.3)
    # gallery rooms: framed pictures on the walls (thin, flush), bench seats against walls
    for (x0, z0, x1, z1) in [(-36, -23.6, -32, -23.5), (-28, -23.6, -24, -23.5), (24, -23.6, 28, -23.5), (-36, 9.5, -32, 9.6),
                             (-12, 25.5, -8, 25.6), (4, 25.5, 8, 25.6), (30, 25.5, 34, 25.6), (-52, 25.5, -48, 25.6)]:
        k.box("wainscotpanelling", x0, U + 2.0, z0, x1, U + 4.6, z1)
    # the lift's gallery hole: rails on its open sides
    for (x0, z0, x1, z1) in [(-5.15, 22.2, -5, 23.75), (-3, 22.2, -2.85, 23.75), (-5, 23.6, -3, 23.75)]:
        k.box("galvanizedsteel", x0, U, z0, x1, U + 1.1, z1)
    # the roof (the west and south arms; the east arm is clipped off): a plant room near the ladder (x -50..-40,
    # z 14..22: a door north, a dead end), AC units along the parapets, closed skylights over Birds and Insects
    wall_x(k, "trapezoidcladding", -50, -40, 14, R, R + 3.2, [(-45, 1.6, 2.6)], t=0.3)
    wall_x(k, "trapezoidcladding", -50, -40, 22, R, R + 3.2, t=0.3)
    wall_z(k, "trapezoidcladding", 14, 22, -50, R, R + 3.2, t=0.3)
    wall_z(k, "trapezoidcladding", 14, 22, -40, R, R + 3.2, t=0.3)
    k.box("leadsheet", -50.3, R + 3.2, 13.7, -39.7, R + 3.5, 22.3)
    k.box("ventilationduct", -49.5, R, 17, -46, R + 2.0, 21.6)
    for (x, z) in ((-52, -20), (-52, 24), (-30, 24), (-8, 24), (10, 24)):
        k.box("radiatorfins", x - 1.2, R, z - 0.9, x + 1.2, R + 1.4, z + 0.9)
    for (x, z) in ((-32, -8), (-32, 18)):
        k.box("wroughtiron", x - 3, R, z - 3, x + 3, R + 0.6, z + 3); k.box("dustyglass", x - 2.8, R + 0.6, z - 2.8, x + 2.8, R + 0.7, z + 2.8)
    k.box("ventilationduct", -20, R, 24.2, 6, R + 0.9, 25.4)                      # a duct run along the south parapet
    k.light(-45, R + 2.6, 18, 6, 0.4)
    # the basement: a pipe run under the ceiling down the archive's main aisle
    k.box("greasestainedmetal", -55, -1.2, 11.2, -10, -0.8, 11.6)
    k.box("greasestainedmetal", -10, -1.2, 24.2, 20, -0.8, 24.6)
    # the shop: café tables by its east wall
    for (x, z) in ((-24, -28), (-28, -28)):
        k.disc("wideplanks", x, z, 0.6, 0, 0.75, n=8)
    # the Egyptian hall: an obelisk in the north-east corner
    k.prism("sandstonestrata", [(52, 12.5), (53.5, 12.5), (53.5, 14), (52, 14)], 0, 6.0)
    k.cut("gold", (52, 6.0, 12.5, 53.5, 7.0, 14), [(1, 0.75, 0, 53.5 + 4.5), (-1, 0.75, 0, -52 + 4.5), (0, 0.75, 1, 14 + 4.5), (0, 0.75, -1, -12.5 + 4.5)])


def ceilings(k, STAIR_A, STAIR_B, LIFT, SERVICE_UP, ROOF_HATCH):
    """a 0.1 m pressed-tin skin under every floor above (the slab's own top material never shows as a ceiling), and
    plaster coffers / beams in the big rooms"""
    for (rect, y, holes) in [((-HX, 10, HX, HZ), G, [STAIR_A, STAIR_B, LIFT]),                          # the basement
                             ((-HX, -HZ, HX, HZ), U, [(-20, -HZ, 20, -20), (-16, -10, 16, 6), SERVICE_UP, LIFT]),
                             ((-20, -HZ, 20, -14), 12.4, []),                                          # the entrance hall
                             ((-HX, -24, -20, 26), R, [ROOF_HATCH]), ((20, -24, HX, 26), R, []), ((-20, 10, 20, 26), R, [])]:
        for (a, b, c, d) in tiles(minus(rect, holes), 20):
            k.box("pressedtinceiling", a, y - 0.5, b, c, y - 0.4, d)
    # coffers in the entrance hall (a 4 m grid of plaster beams)
    for x in range(-16, 17, 4):
        k.box("stucco", x - 0.2, 11.4, -HZ, x + 0.2, 11.9, -14)
    for z in (-36, -32, -28, -24):
        k.box("stucco", -20, 11.4, z - 0.2, 20, 11.9, z + 0.2)
    # beams across the ground-floor halls and the gallery rooms (every 6 m, 0.5 m deep)
    for (x0, x1, zs, y) in [(-HX, -20, (-18, -12, -6, 0, 6), U - 0.5), (20, HX, (-18, -12, -6, 0, 6), U - 0.5),
                            (-HX, -20, (16, 22, 32), U - 0.5), (-38, -20, (-18, -12, -6, 0, 6), R - 0.5),
                            (20, HX, (-18, -6, 6), R - 0.5), (-20, 20, (14, 18, 22), R - 0.5)]:
        for z in zs:
            k.box("stucco", x0, y - 0.5, z - 0.25, x1, y, z + 0.25)


def statue(k, x, z, face):
    """a seated Egyptian statue on a plinth, facing +z (face 1) or -z (-1): 3.6 m"""
    f = face
    k.box("sandstoneashlar", x - 0.7, 0, z - 0.8, x + 0.7, 0.6, z + 0.8)                          # plinth
    k.box("sandstonestrata", x - 0.55, 0.6, z - 0.7, x + 0.55, 1.5, z + 0.7)                      # throne and lap
    k.box("sandstonestrata", x - 0.45, 1.5, z - 0.7 * f - 0.3, x + 0.45, 2.9, z - 0.7 * f + 0.3)  # torso
    k.box("sandstonestrata", x - 0.6, 2.4, z - 0.7 * f - 0.25, x + 0.6, 2.9, z - 0.7 * f + 0.25)  # shoulders
    k.box("sandstonestrata", x - 0.25, 2.9, z - 0.7 * f - 0.25, x + 0.25, 3.3, z - 0.7 * f + 0.25)  # head
    k.wedge("gold", 2 if f > 0 else 3, x - 0.35, 3.0, z - 0.7 * f - 0.35, x + 0.35, 3.6, z - 0.7 * f + 0.35)  # headdress
    for sx in (-1, 1):
        k.box("sandstonestrata", x + sx * 0.4 - 0.12, 1.5, z - 0.7 * f, x + sx * 0.4 + 0.12, 1.65, z + 0.5 * f)  # forearms


def bird(k, x, y, z, ax, mat="feathers", size=1.0):
    """a bird of cut boxes perched at (x, y, z), its body along ax ('x' or 'z'), beak toward +"""
    s = size
    def P(u, v, w):   # (along, up, across)
        return (x + u, y + v, z + w) if ax == "x" else (x + w, y + v, z + u)
    rod(k, mat, P(-0.3 * s, 0.25 * s, 0), P(0.25 * s, 0.38 * s, 0), 0.17 * s)            # body
    rod(k, mat, P(0.3 * s, 0.45 * s, 0), P(0.42 * s, 0.6 * s, 0), 0.11 * s)              # head
    rod(k, "gold", P(0.5 * s, 0.56 * s, 0), P(0.68 * s, 0.52 * s, 0), 0.035 * s)         # beak
    rod(k, mat, P(-0.3 * s, 0.25 * s, 0), P(-0.62 * s, 0.12 * s, 0), 0.06 * s)           # tail
    for w in (-1, 1):
        rod(k, mat, P(-0.15 * s, 0.4 * s, 0.05 * w * s), P(0.1 * s, 0.6 * s, 0.4 * w * s), 0.05 * s)   # wings up
    rod(k, "treebark", P(0, 0.08 * s, 0), P(0, 0.2 * s, 0), 0.03 * s)                    # legs


def exhibits(k):
    """the exhibits that make the rooms (all against walls, in cases or over the lanes' heads)"""
    # ---- Minerals: wall cases on the east wall, crystal points round the cave's mouths, geode arches over them, a
    # meteorite and a giant geode as sightline breaks on the open floor north of the cave
    stones = ["geode", "malachite", "crystal", "jasper", "opal", "obsidian", "jade", "quartzcrystals"]
    for (i, (z0, z1)) in enumerate(((-2, 1.6), (2, 5.6), (6, 9.6))):
        k.box("wainscotpanelling", 53.6, 0, z0, HX, 0.9, z1)
        k.box("glass", 53.6, 0.9, z0, 53.7, 2.6, z1)
        k.box("wainscotpanelling", 53.6, 2.6, z0, HX, 2.85, z1)
        for j in range(3):
            zz = z0 + 0.4 + j * 1.1
            k.box(stones[(i * 3 + j) % len(stones)], 54.3, 0.9, zz, 54.5, 1.6 + 0.25 * ((i + j) % 3), zz + 0.7)
    for (x, z, h) in ((35.6, -18.0, 1.2), (35.6, -14.2, 0.8), (35.6, -8.8, 1.0), (35.6, -5.2, 0.7), (39.3, -20.6, 0.9)):
        for (dx, dz, dh) in ((-0.3, 0, 1.0), (-0.1, 0.35, 0.7), (-0.15, -0.3, 0.6)):
            rod(k, "crystalcluster", (x + 0.15, 0, z), (x + dx, h * dh, z + dz), 0.13)
    for (z0, z1) in ((-17, -15), (-8, -6)):
        k.box("geode", 35.6, 0, z0 - 0.5, 36.0, 4.4, z0); k.box("geode", 35.6, 0, z1, 36.0, 4.4, z1 + 0.5)
        k.box("geode", 35.6, 3.8, z0 - 0.5, 36.0, 4.6, z1 + 0.5)
    k.disc("polishedgranite", 45, 3, 1.8, 0, 0.5, n=8)
    k.prism("bandediron", [(43.6, 2.0), (45.4, 1.5), (46.5, 2.9), (45.4, 4.5), (43.4, 3.6)], 0.5, 2.6,
            top=(0.25, 1, -0.2, 2.6 + 0.25 * 45 - 0.2 * 3))
    k.box("polishedgranite", 37, 0, 2.6, 40.4, 0.5, 4.4)
    k.box("geode", 37.2, 0.5, 3.1, 40.2, 3.4, 3.9)
    k.box("crystal", 37.6, 0.9, 3.9, 39.8, 3.0, 4.0); k.box("crystal", 37.6, 0.9, 3.0, 39.8, 3.0, 3.1)
    # ---- the fossil lab: a freestanding fossil wall (a sightline break) and a rack of casts
    k.box("crossbeddedsandstone", -44, 0, 25.6, -36, 3.4, 26.4)
    for (x, y, w) in ((-42.5, 1.2, 1.6), (-40, 2.2, 2.2), (-38, 1.0, 1.2)):
        for sd in (25.55, 26.45):
            k.box("bone", x - w / 2, y, sd - 0.05, x + w / 2, y + 0.25, sd + 0.05)
    k.box("steelgridwall", -21.4, 0, 12, -20.2, 2.8, 16)
    for y in (0.9, 1.9):
        k.box("bone", -21.6, y, 12.4, -21.4, y + 0.5, 15.6)
    # ---- Birds: perches and birds in the cases; the stepped cliff with nests
    for (i, (x, z)) in enumerate(((-31, -16), (-25, -5), (-31, 5), (-46, 4))):
        ax = "x" if i % 2 else "z"
        k.box("treebark", x - 0.08, U + 0.8, z - 0.08, x + 0.08, U + 2.3, z + 0.08)
        if ax == "x": k.box("treebark", x - 0.8, U + 2.2, z - 0.06, x + 0.8, U + 2.32, z + 0.06)
        else: k.box("treebark", x - 0.06, U + 2.2, z - 0.8, x + 0.06, U + 2.32, z + 0.8)
        bird(k, x + (0.4 if ax == "x" else 0), U + 2.32, z + (0.4 if ax == "z" else 0), ax, size=1.3)
        k.box("dewygrass" if i % 2 else "forestfloor", x - 1.85, U + 0.8, z - 1.85, x + 1.85, U + 0.9, z + 1.85)
    cliff = [(-49.2, 1.0, -21.6, -8.6), (-50.6, 2.0, -22.0, -8.2), (-51.8, 3.0, -21.2, -9.6), (-53.0, 4.0, -22.0, -11.0),
             (-54.0, 5.0, -20.4, -13.0)]
    for (j, (x1, y, z0, z1)) in enumerate(cliff):
        lean = 0.6 if j % 2 else -0.5
        k.prism("seabirdcliff", [(-HX, z0), (x1 + lean, z0), (x1 + 0.4, (z0 + z1) / 2), (x1 - lean, z1), (-HX, z1)], U, U + y)
        nx, nz = x1 - 0.7, (z0 + 1.2 if j % 2 else z1 - 1.2)
        k.disc("hay", nx, nz, 0.35, U + y, U + y + 0.18, n=6)
        if j in (1, 3): bird(k, nx, U + y + 0.1, nz + 0.9, "z", mat="whitewashedbrick", size=0.8)
    # ---- Egypt: the pylon gateway (two battered towers, 7 m) before the mastaba's mouth, four seated statues before
    # it, hieroglyph bands round the mastaba
    for (x0, x1) in ((33.8, 37.8), (40.8, 44.8)):
        k.cut("sandstoneashlar", (x0, 0, 12.8, x1, 7.0, 15.8),
              [(1, 0.08, 0, x1 + 0.08 * 0), (-1, 0.08, 0, -x0), (0, 0.08, 1, 15.8), (0, 0.08, -1, -12.8)])
        k.cut("stampedstonepattern", (x0 + 0.6, 4.6, 12.8, x1 - 0.6, 5.6, 13.8), [(0, 0.08, -1, -12.75)])
        k.box("gold", x0 + 0.3, 6.6, 13.1, x1 - 0.3, 7.0, 15.5)
    k.box("sandstoneashlar", 37.0, 5.6, 13.4, 41.6, 6.6, 15.2)                    # the lintel between them
    for (x, z) in ((34.6, 11.2), (36.6, 11.2), (42.0, 11.2), (44.0, 11.2)):
        statue(k, x, z, -1)
    for (x0, z0, x1, z1) in [(30, 15.94, 38.6, 16.0), (40, 15.94, 50, 16.0), (29.94, 16, 30, 36), (50, 16, 50.06, 36), (30, 36, 50, 36.06)]:
        for (ya, yb) in ((1.8, 2.5), (3.4, 4.1)):
            k.box("stampedstonepattern", x0, ya, z0, x1, yb, z1)
    # ---- Ocean: the giant squid (a mantle hung from the ceiling, eight cut-box arms, two long feeding tentacles), a
    # dark tank along the east wall with kelp between the alcoves, a blue floor
    cx, cz, yb = 37.5, -11.0, 13.2
    k.disc("coral", cx, cz, 1.1, yb, yb + 1.6, n=8)
    k.disc("coral", cx, cz, 0.7, yb + 1.6, yb + 2.4, n=8)
    k.box("galvanizedsteel", cx - 0.04, yb + 2.4, cz - 0.04, cx + 0.04, R - 0.5, cz + 0.04)
    import math
    for i in range(8):
        a = math.radians(22.5 + 45 * i)
        ca, sa = math.cos(a), math.sin(a)
        reach = 3.0 if abs(ca) > 0.6 else 3.6
        p0 = (cx + 0.7 * ca, yb + 0.1, cz + 0.7 * sa)
        p1 = (cx + 1.8 * ca, yb - 0.9, cz + 1.8 * sa)
        p2 = (cx + reach * ca, yb - 1.2 + 0.3 * (i % 2), cz + reach * sa)
        rod(k, "coral", p0, p1, 0.17); rod(k, "coral", p1, p2, 0.11)
    for sz in (-1, 1):
        chain(k, "coral", [(cx, yb, cz + 0.4 * sz), (cx + 0.3, yb - 1.4, cz + 2.5 * sz), (cx - 0.2, yb - 1.7, cz + 5.0 * sz),
                           (cx + 0.4, yb - 1.4, cz + 6.4 * sz)], 0.08)
    for (z0, z1) in ((-17, -11), (-5, 1)):
        k.box("darkconcrete", 49.8, U, z0, HX, U + 0.6, z1)
        k.box("bluetintedglass", 49.85, U + 0.6, z0, 50.05, U + 6.0, z1)
        k.box("waterstainedconcrete", 49.8, U + 6.0, z0, HX, U + 7.6, z1)
        k.box("bluepooltile", 54.7, U + 0.6, z0, HX, U + 6.0, z1)
        for j in range(4):
            zz = z0 + 0.8 + j * 1.4
            k.box("kelp", 51.0 + (j % 2) * 1.6, U + 0.6, zz, 51.3 + (j % 2) * 1.6, U + 3.6 + (j * 7 % 3), zz + 0.25)
    # ---- the Great Hall's dinosaur, rebuilt: a curved ribcage off the spine, a 3 m skull with an open jaw, legs with
    # feet (the tail ramp, the spine walk at 6 and the skull's top at 9 stay)
    for x in (-4.6, -3.2, -1.8, -0.4, 1.0, 2.4):
        for sz in (-1, 1):
            chain(k, "bone", [(x, 5.45, -1.0 + 0.5 * sz), (x, 5.0, -1.0 + 1.4 * sz), (x, 3.9, -1.0 + 1.75 * sz),
                              (x, 2.8, -1.0 + 1.3 * sz)], 0.1)
    k.box("bone", -14.2, 8.0, -2.0, -11.0, 9.0, 0.0)                               # the cranium (its top at 9)
    k.wedge("bone", 0, -15.4, 7.6, -1.8, -14.2, 8.8, -0.2)                        # the snout
    k.box("bone", -14.0, 7.6, -1.9, -11.0, 8.0, -0.1)
    k.box("bone", -15.0, 6.8, -1.7, -11.6, 7.2, -0.3)                             # the lower jaw (the mouth open)
    for xx in (-14.6, -14.0, -13.4, -12.8):
        for zz in (-1.6, -0.4):
            k.box("bone", xx - 0.06, 7.2, zz - 0.06, xx + 0.06, 7.55, zz + 0.06)    # teeth
    k.box("bone", -11.8, 6.0, -1.6, -11.0, 8.0, -0.4)                             # the jaw's hinge onto the neck
    for (x, z) in ((-5, -3.4), (-5, 1.4), (3, -3.4), (3, 1.4)):
        sz = -1 if z < 0 else 1
        rod(k, "bone", (x, 5.4, z - 0.6 * sz), (x + 0.6, 3.2, z), 0.28)           # thigh
        rod(k, "bone", (x + 0.6, 3.2, z), (x - 0.2, 1.05, z), 0.2)                # shin
        k.box("bone", x - 0.6, 0.8, z - 0.35, x + 0.6, 1.05, z + 0.35)            # foot
        for dz in (-0.25, 0, 0.25):
            k.wedge("bone", 0, x - 1.0, 0.8, z + dz - 0.08, x - 0.6, 1.0, z + dz + 0.08)   # toes


def build():
    m = M("Museum", "hide", sun=(0.25, 1.0, -0.35))
    m.also = ["tag"]
    k = Kit(m)
    m.raw("palette base 0.78 0.75 0.7 accent 0.42 0.3 0.22 metal 0.3 0.3 0.32 glow 0.55 1 0.6 sky 0.02 0.025 0.05")
    m.raw("setting a natural history museum at night")

    # ================================================================== shell: floors, roofs, outer walls, clip
    # the basement under the south strip (x -55..55, z 10..40)
    slab(k, "dustylinoleum", (-HX, 10, HX, HZ), B)
    # the ground floor: everywhere, less the stairwells down (A in the fossil lab, B in the service hall) and the lift
    STAIR_A, STAIR_B, LIFT = (-52, 14, -48, 26), (-18, 14, -14, 26), (-5, 22.2, -3, 23.6)
    gmat = [((-20, -40, 20, -14), "polishedgranite"), ((-20, -14, 20, 10), "terrazzo"), ((-HX, -24, -20, 10), "basketweaveparquet"),
            ((20, -24, HX, 10), "basketweaveparquet"), ((-HX, -40, -20, -24), "terrazzo"), ((20, -40, HX, -24), "officecarpettiles"),
            ((-HX, 10, -20, HZ), "linoleum"), ((-20, 10, 20, HZ), "polishedconcrete"), ((20, 10, HX, HZ), "largesandstoneashlar")]
    for (r, mat) in gmat:
        slab(k, mat, r, G, holes=[STAIR_A, STAIR_B, LIFT])
    # the galleries (8): Birds, Insects, the fossil gallery, the reading room, Ocean, the ring and the mezzanine;
    # the single-storey parts' roofs (north corners, the south strip past z 26) at the same height
    SERVICE_UP = (14, 14, 18, 21)            # the service stair's well
    ROOF_HATCH = (-HX, -1, -53.5, 1)        # the roof ladder's hatch (Birds)
    for (r, mat) in [((-HX, -24, -20, 26), "basketweaveparquet"), ((20, -24, HX, 10), "slatetile"), ((20, 10, HX, 26), "basketweaveparquet"),
                     ((-20, 10, 20, 26), "basketweaveparquet"), ((-20, -20, 20, -14), "polishedgranite")]:
        slab(k, mat, r, U, holes=[SERVICE_UP, LIFT])
    slab(k, "terrazzo", (-20, -14, 20, 10), U, holes=[(-16, -10, 16, 6)])       # the ring
    for r in [(-HX, -40, -20, -24), (20, -40, HX, -24), (-HX, 26, HX, HZ)]:
        slab(k, "leadsheet", r, U)
    slab(k, "leadsheet", (-20, -40, 20, -14), 12.4)                              # the entrance hall's roof
    slab(k, "concreterooftiles", (-HX, -24, -20, 26), R, holes=[ROOF_HATCH])     # the roofs round the dome
    slab(k, "concreterooftiles", (20, -24, HX, 26), R)
    slab(k, "concreterooftiles", (-20, 10, 20, 26), R)
    # outer walls (0.6 m, outside the footprint) from under the basement to the roof parapet
    TOP = R + 1.2
    k.box(EXT, -HX - 0.6, B - 0.6, -HZ - 0.6, -HX, TOP, HZ + 0.6); k.box(EXT, HX, B - 0.6, -HZ - 0.6, HX + 0.6, TOP, HZ + 0.6)
    k.box(EXT, -HX, B - 0.6, -HZ - 0.6, HX, TOP, -HZ); k.box(EXT, -HX, B - 0.6, HZ, HX, TOP, HZ + 0.6)
    k.box("rubblewall", -HX, B - 0.6, 9.8, HX, G - 0.4, 10.2)                   # the basement's north wall (earth beyond)
    # the clip: up from the parapets to the lid, and filling the space over the lower roofs (nobody drops onto them)
    m.clip_walls(-HX, -HZ, HX, HZ, TOP, up=LID - TOP)
    k.box("clip", -HX, LID, -HZ, HX, LID + 1, HZ)
    for (x0, z0, x1, z1, y) in [(-HX, -HZ, -20, -24, U), (20, -HZ, HX, -24, U), (-20, -HZ, 20, -14, 12.4), (-HX, 26, HX, HZ, U),
                                (20.4, -24, HX, 26, R)]:      # (and the east arm's roof: the roof is the west and south arms)
        k.box("clip", x0, y, z0, x1, LID, z1)
    # the roof's parapets on its inner edges (over the lower roofs)
    for (x0, z0, x1, z1) in [(-HX, -24.3, -20, -24), (-HX, 26, 20.3, 26.3), (-20.3, -24, -20, -14), (20, -24, 20.3, -14), (20, 10, 20.3, 26)]:
        k.box(EXT, x0, R, z0, x1, TOP, z1)

    # ================================================================== ground-floor walls (0..8 unless said)
    D = 3.6                       # door height
    # north line z -24: shop | Mammals, temporary exhibition | Minerals
    wx(k, "wainscotpanelling", -HX, -20, -24, G, U - 0.4, [(-40, 3.0, D), (-54.3, 1.2, 1.4)])   # (the crawl hatch)
    wx(k, "painteddrywall", 20, HX, -24, G, U - 0.4, [(40, 3.0, D)])
    # x -20 / 20: entrance | shop, entrance | Mammals, hall | Mammals (and the east side the same)
    for sx in (-1, 1):
        x = 20 * sx
        wz(k, "limestoneashlar", -HZ, -14, x, G, U - 0.4, [(-32, 4.0, D), (-19, 3.0, D)])
        wz(k, "limestoneashlar", -HZ, -24, x, U - 0.4, 12.4, trim=False)            # (entrance | the shop's roof)
        wz(k, "limestoneashlar", -14, 10, x, G, U - 0.4, [(-2, 6.0, 5.0)])
        wz(k, "painteddrywall", 10, HZ, x, G, U - 0.4, [(34, 3.0, D)])
    # z -14 (entrance | hall): the ground door, the mezzanine's opening onto the ring
    wx(k, "limestoneashlar", -20, 20, -14, G, U - 0.4, [(0, 6.0, 5.0)])
    wx(k, "limestoneashlar", -20, 20, -14, U, R, [(0, 16.0, 3.6)])
    # z 10: hall | service hall; Mammals | fossil lab; Minerals | Egyptian
    wx(k, "limestoneashlar", -20, 20, 10, G, U - 0.4, [(0, 6.0, 5.0)])
    wx(k, "wainscotpanelling", -HX, -20, 10, G, U - 0.4, [(-40, 3.0, D), (-54.3, 1.2, 1.4)])
    wx(k, "sandstoneashlar", 20, HX, 10, G, U - 0.4, [(39.3, 3.0, D)])

    # ================================================================== gallery walls (8..16.4)
    for sx in (-1, 1):
        x = 20 * sx
        wz(k, "stripedwallpaper", -24, -14, x, U, R, [(-17, 3.0, 3.4)])        # Birds / Ocean | mezzanine
        wz(k, "limestoneashlar", -14, 10, x, U, R, [(-2, 4.0, 3.4)])            # Birds / Ocean | ring
        wz(k, "stripedwallpaper", 10, 26, x, U, R, [(18, 3.0, 3.4)])            # Insects / reading room | fossil gallery
        wx(k, "stripedwallpaper", 20 * sx, 55 * sx, 10, U, R, [(40 * sx, 3.0, 3.4)])   # Birds | Insects, Ocean | reading
    for (x0, x1) in ((-HX, -20), (20, HX)):
        k.box(EXT, x0, U, -24.2, x1, R, -23.8)           # (over the lower roofs: outer walls)
    k.box(EXT, -HX, U, 25.8, HX, R, 26.2)
    wx(k, "limestoneashlar", -20, 20, 10, U, R, [(0, 4.0, 3.4)])                # ring | fossil gallery
    # (the Great Hall's walls stop at the roof; a kerb round the dome's eave)
    for (x0, z0, x1, z1) in [(-20.4, -14, -20, 10), (20, -14, 20.4, 10), (-20, 9.8, 20, 10.4)]:
        k.box("limestoneashlar", x0, R, z0, x1, R + 0.4, z1)

    # ================================================================== the dome: a hipped roof, plaster below 21,
    # glass above (no way out: it's solid), castiron rafters and tie beams inside (the hooks), the whale under them
    hip_dome(k, -20.4, -14.4, 20.4, 10.4, R, 10.0 / 12.4, CAP, "stucco", rafters=(-12, -6, 0, 6, 12))
    lantern(k, -12.0, -6.0, 12.0, 2.0, CAP)
    for x in (-12, -4, 4, 12):
        k.box("castiron", x - 0.2, R + 0.3, -14, x + 0.2, R + 0.8, 10)
    # ================================================================== stairs and the lift
    # the grand stair (entrance hall up to the mezzanine) and its rails
    k.stairs("marble", -4, -36, 4, -20, U, "z", 1)
    for x in (-4.3, 4.0):   # its balustrades: a stringer wall rising with the flight
        k.wedge("marble", 2, x, 0, -36, x + 0.3, U + 1.1, -20)
    # the mezzanine's rail (but where the stair arrives) and the ring's rails
    for (a, b) in ((-20, -4), (4, 20)):
        k.box("castiron", a, U, -20.15, b, U + 1.0, -20); k.box("brass", a, U + 1.0, -20.2, b, U + 1.1, -19.95)
    for (x0, z0, x1, z1) in [(-16, -10.1, 16, -10), (-16, 6, 16, 6.1), (-16.1, -10, -16, 6), (16, -10, 16.1, 6)]:
        k.box("castiron", x0, U, z0, x1, U + 1.0, z1); k.box("brass", x0 - 0.05, U + 1.0, z0 - 0.05, x1 + 0.05, U + 1.1, z1 + 0.05)
    # the service stair (service hall up to the fossil gallery): rails round its well
    k.stairs("polishedconcrete", 14, 14, 18, 30, U, "z", -1)
    for (x0, z0, x1, z1) in [(13.85, 14, 14, 21), (18, 14, 18.15, 21), (14, 21, 18, 21.15)]:
        k.box("galvanizedsteel", x0, U, z0, x1, U + 1.1, z1)
    # the stairs down (A in the fossil lab, B in the service hall): rails round the wells, open at the top step
    for (x0, z0, x1, z1) in (STAIR_A, STAIR_B):
        k.stairs("polishedconcrete", x0, z0, x1, z1, G, "z", -1, base=B)
        for (a, b, c, d) in [(x0 - 0.15, z0 + 1, x0, z1), (x1, z0 + 1, x1 + 0.15, z1), (x0 - 0.15, z1, x1 + 0.15, z1 + 0.15)]:
            k.box("galvanizedsteel", a, G, b, c, G + 1.1, d)
    # the goods-lift cage (x -6.4..-1.6, z 22..26): a solid north side with the ladder up it (basement to the
    # galleries: over its top onto the fossil gallery's floor), mesh sides, open to the south on every floor
    k.box("steeldoorskin", -6.4, B, 22, -1.6, U, 22.2)
    for x in (-6.4, -1.72):
        for (y0, y1) in ((B, G - 0.4), (G, U - 0.4)):
            k.box("expandedmetal", x, y0, 22, x + 0.12, y1, 26)
    k.ladder(-4, 22.32, B, U, "n", mat="galvanizedsteel")

    # ================================================================== rooms: the ground floor
    # -- entrance hall: the ticket desk, benches, the cloakroom (a dead end off the north-west corner)
    k.box("polishedgranite", 8, 0, -31, 16, 1.1, -30); k.box("polishedgranite", 15, 0, -30, 16, 1.1, -26)
    wx(k, "wainscotpanelling", -20, -12, -34, G, 4.0, [(-14, 1.6, 3.0)])
    wz(k, "wainscotpanelling", -40, -34, -12, G, 4.0)
    k.box("wainscotpanelling", -20, 4.0, -40, -12, 4.3, -34)
    # -- the Great Hall: the dinosaur. Plinth (x -11..5, z -4..2, 0.8, a step round it), spine walk at 6 from the
    # tail ramp (x 15 -> 5) to the neck (up to the skull at 9 over x -11..-14); legs, a ribcage over the plinth
    k.box("polishedgranite", -11.5, 0, -4.5, 5.5, 0.4, 2.5); k.box("polishedgranite", -11, 0.4, -4, 5, 0.8, 2)
    k.wedge("bleachedbones", 1, 5, 0.8, -1.6, 15, 6.0, -0.4)                    # the tail: up -x from the floor
    k.box("bleachedbones", -6, 5.4, -1.6, 5, 6.0, -0.4)                         # the spine
    k.wedge("bleachedbones", 1, -11, 6.0, -1.6, -6, 8.4, -0.4)                  # the neck (-x, up to the skull)
    k.box("bleachedbones", -11, 5.4, -1.6, -6, 6.0, -0.4)
    # the whale under the tie beams (x -13..13 at z 0, its back at 15), cables up to them
    k.box("bleachedbones", -11, 14.4, -0.3, 11, 14.9, 0.3)
    for x in range(-9, 10, 2):
        k.box("bone", x - 0.1, 13.4, -1.8, x + 0.1, 14.4, -1.6); k.box("bone", x - 0.1, 13.4, 1.6, x + 0.1, 14.4, 1.8)
        k.box("bone", x - 0.1, 13.2, -1.8, x + 0.1, 13.4, 1.8)
    k.box("bone", 11, 13.6, -1.2, 14.6, 15.0, 1.2)                              # the skull
    k.box("bone", -14, 14.5, -2.2, -11, 14.7, 2.2)                              # the flukes
    for x in (-12, -4, 4, 12):
        k.box("galvanizedsteel", x - 0.04, 14.9, -0.04, x + 0.04, R + 0.3, 0.04)
    # -- Mammals (west): four diorama bays on the west wall (x -53.6..-47: raised scenery behind glass, painted
    # backdrop), a crawlway behind the backdrops (x -55..-53.6, 1.3 m high hatches into the shop and the fossil lab),
    # rock outcrops and three cases down the middle
    k.box("painteddrywall", -53.6, 0, -24, -53.2, U - 0.4, 10)                  # the backdrop wall
    k.box("wainscotpanelling", -55, 1.4, -24, -53.6, U - 0.4, 10)              # (the crawlway's ceiling: 1.4 m)
    for i, (z0, z1, scen) in enumerate([(-23, -15, "forestfloor"), (-14, -6, "boulderfield"), (-5, 3, "dewygrass"), (4, 9.6, "snow")]):
        k.box(scen, -53.2, 0, z0, -47.4, 0.9, z1)
        k.box("glass", -47.4, 0.9, z0, -47.2, 4.2, z1)
        k.box("wainscotpanelling", -47.4, 0, z0, -47.0, 0.9, z1)
        k.box("wainscotpanelling", -53.2, 4.2, z0, -47.0, U - 0.4, z1)
        k.box("fur", -51, 0.9, (z0 + z1) / 2 - 1.2, -49, 2.4, (z0 + z1) / 2 + 0.8)
    for z in (-24, -15, -6, 3, 9.6):
        k.box("wainscotpanelling", -53.2, 0, max(z, -24), -47.0, U - 0.4, min(z + 1, 10))   # bay dividers
    k.prism("boulderfield", [(-40, -18), (-34, -20), (-31, -15), (-36, -12)], 0, 3.2)
    k.prism("boulderfield", [(-28, 4), (-24, 2), (-22, 7), (-26, 9)], 0, 2.8)
    # the savanna island (x -42..-30, z -10..-2, 4.5 m): dioramas behind glass on its long sides, a keeper's corridor
    # down its middle (in by a narrow door at its east end: a dead end)
    wx(k, "wainscotpanelling", -42, -30, -9.8, 0, 4.5, [(-36, 10.0, 3.6, 0.9)], glass="glass")
    wx(k, "wainscotpanelling", -42, -30, -2.2, 0, 4.5, [(-36, 10.0, 3.6, 0.9)], glass="glass")
    wz(k, "wainscotpanelling", -10, -2, -41.8, 0, 4.5)
    wz(k, "wainscotpanelling", -10, -2, -30.2, 0, 4.5, [(-6, 1.2, 2.4)])
    for z in (-7.2, -4.8):
        wx(k, "painteddrywall", -41.6, -30.4, z, 0, 4.5, [] if z < -6 else [], t=0.2)
    k.box("dewygrass", -41.6, 0, -9.6, -30.4, 0.9, -7.3); k.box("ochreearth", -41.6, 0, -4.7, -30.4, 0.9, -2.4)
    k.box("fur", -38, 0.9, -9.2, -36, 2.3, -8.0); k.box("fur", -34, 0.9, -3.8, -32.4, 1.9, -2.8)
    k.box("wainscotpanelling", -42, 4.5, -10, -30, 4.8, -2)
    for (x, z) in ((-26, -20), (-26, -10), (-46, 6)):
        k.box("cratewood", x - 1.5, 0, z - 1.5, x + 1.5, 0.8, z + 1.5)
        k.box("glass", x - 1.4, 0.8, z - 1.4, x + 1.4, 3.0, z + 1.4)
        k.box("fur", x - 0.8, 0.8, z - 0.6, x + 0.8, 2.0, z + 0.6)
    # -- Minerals (east): low cases in rows, the crystal cave (x 36..53, z -20..-4: in at its west side, a U through
    # it, out again; three side niches; a crawl on its roof)
    for x in (24, 30):
        for (z0, z1) in ((-21, -13), (-9, -1), (3, 8)):
            k.box("wainscotpanelling", x, 0, z0, x + 1.4, 0.9, z1); k.box("glass", x + 0.1, 0.9, z0 + 0.1, x + 1.3, 1.2, z1 - 0.1)
    cave = (36, -20, 53, -4)
    runs = [(36, -17, 46, -15), (44, -17, 46, -6), (36, -8, 46, -6), (46, -13, 50, -11), (46, -17, 51, -15), (40, -6, 42, -4.6), (40, -20, 42, -17)]
    for (x0, z0, x1, z1) in minus(cave, runs):
        k.box("wetcaverock", x0, 0, z0, x1, 5, z1)
    k.box("geode", cave[0], 5, cave[1], cave[2], 5.6, cave[3])
    for (x0, z0, x1, z1) in [(49.2, -12.8, 49.8, -12.2), (50.2, -16.8, 50.8, -16.2), (40.4, -5.2, 41.6, -4.7)]:
        k.box("quartzcrystal", x0, 0, z0, x1, 1.4, z1)
    # -- the shop (north-west): shelving, the counter, café tables
    k.box("wideplanks", -30, 0, -32, -22, 1.1, -31)
    for z in (-38, -35):
        k.box("cratewood", -50, 0, z, -36, 2.2, z + 1.0)
    # -- the temporary exhibition (north-east): a maze of thin drywall (dash through it), 4 m high
    for (x0, z0, x1, z1) in [(24, -36, 34, -35.88), (30, -32, 46, -31.88), (40, -38, 40.12, -32), (24, -28, 36, -27.88),
                             (46, -36, 52, -35.88), (46, -36, 46.12, -27), (36, -32, 36.12, -24.5), (50, -31, 55, -30.88)]:
        k.box("drywall", x0, 0, z0, x1, 4.0, z1)
    # -- the shop's café kitchen (x -30..-20, z -40..-35: one door, a dead end)
    wx(k, "whitesquaretile", -30, -20, -35, 0, U - 0.4, [(-27, 1.6, 2.8)])
    wz(k, "whitesquaretile", -40, -35, -30, 0, U - 0.4)
    k.box("chrome", -29.6, 0, -39.6, -21, 1.0, -38.6)
    # -- the fossil lab's storeroom (x -32..-20, z 30..40: shelves, one door)
    wx(k, "painteddrywall", -32, -20, 30, 0, U - 0.4, [(-29, 2.0, 3.0)])
    wz(k, "painteddrywall", 30, HZ, -32, 0, U - 0.4)
    for z in (33, 36.5):
        k.box("cratewood", -31, 0, z, -23, 2.4, z + 1.0)
    # -- the fossil lab (south-west): work tables, casts
    for (x, z) in ((-40, 20), (-32, 20), (-40, 32), (-26, 22)):
        k.box("cratewood", x - 2, 0, z - 1, x + 2, 1.0, z + 1)
    # -- the service hall (south): crates by the loading door
    k.box("rollershutter", -10, 0, 39.6, 4, 5.0, 40)
    for (x, z) in ((4, 31), (8, 35.5), (4, 36), (9.5, 30.5)):
        k.box("palletstack", x - 1.2, 0, z - 1.2, x + 1.2, 2.6, z + 1.2)
    # the security office (x -20..-10, z 32..40): a frosted window onto the hall, one door
    wx(k, "painteddrywall", -20, -10, 32, 0, U - 0.4, [(-17, 1.6, 2.8), (-13, 4.0, 2.4, 1.0)], glass="frostedglass")
    wz(k, "painteddrywall", 32, HZ, -10, 0, U - 0.4)
    k.box("controlpanel", -19.6, 0, 38.4, -12, 1.1, 39.6)
    # -- the Egyptian hall (south-east): the mastaba (x 30..50, z 16..36, 5 m) with a passage in from the north
    # (1.4 m wide, three turns) to the sarcophagus chamber; columns along its west side
    mast = (30, 16, 50, 36)
    path = [(38.6, 16, 40, 22), (34, 20.6, 40, 22), (34, 20.6, 35.4, 30), (35.4, 28.6, 44, 30), (40, 30, 46, 34)]
    for (x0, z0, x1, z1) in minus(mast, path):
        k.box("sandstoneashlar", x0, 0, z0, x1, 5, z1)
    k.box("largesandstoneashlar", mast[0], 5, mast[1], mast[2], 5.4, mast[3])
    k.box("gold", 41.5, 0, 31, 44.5, 1.0, 33)                                  # the sarcophagus
    for z in (14, 22, 30, 37):
        k.disc("sandstoneashlar", 25, z, 0.7, 0, U - 0.4, n=8)

    # ================================================================== rooms: the galleries
    # -- Birds (west): tall cases (5 m, solid tops: hide on top, a ladder behind one), the cliff diorama (ledges up
    # the west wall), the roof ladder in the north-west of its middle (x -55, z 0)
    wz(k, "stripedwallpaper", -24, 10, -38, U, R, [(-14, 4.0, 3.4), (3, 4.0, 3.4)])     # halves: two doorways
    for (x, z) in ((-31, -16), (-25, -5), (-31, 5), (-46, 4)):
        k.box("wainscotpanelling", x - 2, U, z - 2, x + 2, U + 0.8, z + 2)
        k.box("glass", x - 1.9, U + 0.8, z - 1.9, x + 1.9, U + 4.6, z + 1.9)
        k.box("wainscotpanelling", x - 2, U + 4.6, z - 2, x + 2, U + 5.0, z + 2)
    k.ladder(-31, -18.12, U, U + 5.0, "s", mat="brass")      # (behind the north case)
    k.ladder(-HX + 0.12, 0, U, R, "w", mat="galvanizedsteel")
    # -- Insects (south-west upper): drawer cabinets (1.4 m) in rows
    for z in (14, 18, 22):
        k.box("wainscotpanelling", -50, U, z, -26, U + 1.4, z + 1.2)
    # -- the fossil gallery (south upper): plinths
    for x in (-12, 0):
        k.box("polishedgranite", x - 3, U, 15, x + 3, U + 0.8, 20)
        k.box("bone", x - 2, U + 0.8, 17, x + 2, U + 2.6, 18)
    # -- the reading room (south-east upper): bookcases 3 m high
    for z in (13.5, 17.5, 21.5):
        k.box("cratewood", 26, U, z, 48, U + 3.0, z + 1.0)
    # -- Ocean (east): tank walls with blue windows, three deep-sea alcoves on the east wall, the giant squid
    for (z0, z1) in ((-22, -14), (-8, 0)):
        wz(k, "waterstainedconcrete", z0, z1, 32.5, U, U + 6, [((z0 + z1) / 2, 4.0, 4.2, 1.2)], t=1.0, glass="bluetintedglass")
    for z in (-20, -8, 4):
        k.box("wetcaverock", 50, U, z - 3, HX, U + 7.6, z - 2.6); k.box("wetcaverock", 50, U, z + 2.6, HX, U + 7.6, z + 3)
        k.box("wetcaverock", 50, U + 4.4, z - 2.6, HX, U + 7.6, z + 2.6)
    for (z0, z1) in ((-24, -16), (-4, 6)):
        wz(k, "waterstainedconcrete", z0, z1, 42.5, U, U + 6, [((z0 + z1) / 2, 3.0, 3.8, 1.4)], t=1.0, glass="bluetintedglass")

    # ================================================================== the basement
    # -- the archive (x -55..-10): compact shelving rows 1 m deep, 1.4 m aisles, in two blocks; the frosted office
    for x in [round(-46 + 2.4 * i, 2) for i in range(10)]:
        for (z0, z1) in ((13, 23), (26, 36)):
            k.box("steelgridwall", x, B, z0, x + 1.0, B + 3.0, z1)
    wz(k, "painteddrywall", 30, HZ, -21.9, B, G - 0.4, [(35, 6.0, 3.2, 0.9)], t=0.2, glass="frostedglass")
    wx(k, "painteddrywall", -22, -10.2, 30.1, B, G - 0.4, [(-19, 4.0, 3.2, 0.9), (-13.2, 1.6, 3.0)], t=0.2, glass="frostedglass")
    wz(k, "cinderblock", 10, HZ, -10, B, G - 0.4, [(18, 3.0, 3.2), (24.5, 2.6, 3.2)])
    # -- the taxidermy room (x 6..20, z 30..40: one door)
    wx(k, "cinderblock", 6, 20, 30, B, G - 0.4, [(9, 2.0, 3.0)])
    wz(k, "cinderblock", 30, HZ, 6, B, G - 0.4)
    k.box("quiltedstainless", 10, B, 36, 18, B + 1.0, 38); k.box("fur", 12, B + 1.0, 36.4, 14, B + 1.8, 37.4)
    # -- the workshop: benches and crates
    for (x, z) in ((2, 14), (8, 34), (14, 14)):
        k.box("cratewood", x - 1.5, B, z - 1, x + 1.5, B + 1.0, z + 1)
    wz(k, "cinderblock", 10, HZ, 20, B, G - 0.4, [(25, 3.0, 3.2)])
    # -- the boiler room: the fuel tank (x 44..52, z 16..40, 4 m): a dead-end slot behind it from the north
    k.box("boilerplate", 44, B, 16, 52, B + 4.0, HZ)
    # -- the boiler room: two boilers, a pipe run
    for z in (18, 32):
        k.disc("boilerplate", 34, z, 2.4, B, B + 4.2, n=8)
    k.box("castiron", 24, B + 4.4, 12, 54, B + 4.9, 12.6)

    # ================================================================== lights (night, but readable: the ground
    # floor's halls at 0.7 with one more each, a strong light under the dome; the dark corners stay for hiders)
    for (x, y, z, r, i) in [(0, 10, -30, 16, 0.75), (0, 21, -2, 30, 1.0), (-12, 4, -2, 12, 0.7), (12, 4, -2, 12, 0.7),
                            (-38, 6, -6, 16, 0.7), (-50, 3.5, -10, 8, 0.6), (38, 6, -6, 16, 0.7), (44, 3, -11, 8, 0.5),
                            (-38, 6, -32, 12, 0.7), (38, 3.5, -32, 12, 0.6), (-38, 6, 26, 14, 0.7), (0, 6, 26, 14, 0.7),
                            (38, 6, 25, 14, 0.7), (40, 3.5, 32, 6, 0.6), (37, 3.5, 21, 6, 0.4),
                            (-38, 14, -6, 16, 0.6), (-38, 14, 18, 14, 0.55), (0, 14, 18, 14, 0.6), (38, 14, 18, 14, 0.55),
                            (0, 11, -17, 12, 0.6), (-32, -2, 24, 14, 0.45), (-32, -2, 18, 10, 0.3), (5, -2, 25, 14, 0.5),
                            (37, -2, 25, 14, 0.5), (-16, -3, 35, 6, 0.4), (-38, 18, 0, 20, 0.35), (0, 18, 18, 14, 0.35),
                            # one more per hall
                            (-28, 6, 4, 14, 0.65), (30, 6, -16, 14, 0.65), (46, 6, 4, 12, 0.6), (48, 6, 14, 12, 0.6),
                            (-46, 6, -32, 10, 0.6), (-44, 6, 34, 12, 0.6), (8, 6, 34, 12, 0.6), (-28, 14, -18, 12, 0.55),
                            (12, 14, 14, 12, 0.5), (0, 6, -36, 12, 0.6), (0, 3, -2, 14, 0.6)]:
        k.light(x, y, z, r, i)
    for (z0, z1) in ((-23, -15), (-14, -6), (-5, 3), (4, 9.6)):            # the Mammals dioramas, lit behind their glass
        k.light(-50.5, 3.6, (z0 + z1) / 2, 6, 0.6)
    # Ocean stays dark (lights carry no colour: its blue is the tanks' glass and tiles and the slate floor)
    for (x, z) in ((42, -14), (52, -20), (52, -8), (52, 4), (52, -14), (52, -2)):
        k.light(x, U + 6, z, 9, 0.35)

    decorate(k)
    exhibits(k)
    ceilings(k, STAIR_A, STAIR_B, LIFT, SERVICE_UP, ROOF_HATCH)
    trims(k)

    # ================================================================== spawns: the entrance hall (8, any); free for
    # all spreads out (ffa-any, format 11): two on each floor and in the big halls
    for (x, z) in ((-16, -28), (-9, -27), (9, -23), (17, -21), (-16, -22), (-9, -33), (9, -36), (17, -36)):
        k.spawn("any", x, G, z, yaw_to(x, z, 0, -14))
    for (x, y, z, tx, tz) in [(12, G, -8, 0, -2), (-14, G, 4, 0, -2), (-26, G, -16, -36, -6), (27.7, G, -4, 40, -4),
                              (-36, G, 22, -30, 30), (12, G, -17, 0, -30), (-44, U, -12, -30, -10), (37, U, 6, 37, -10),
                              (6, U, 22, 0, 14), (-46, B, 24.5, -30, 24.5), (8, B, 20, 0, 24)]:
        k.spawn("ffa-any", x, y, z, yaw_to(x, z, tx, tz))
    m.save("museum", dress=False)
