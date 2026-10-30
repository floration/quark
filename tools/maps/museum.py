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
cases never hide anyone (frosted glass does). Every spot is under 30 s of walking from the entrance hall."""
from .prefabs import M
from .m6kit import Kit, wall_x, wall_z, slab, yaw_to, minus, tiles, num

HX, HZ = 55, 40
B, G, U, R = -6.0, 0.0, 8.0, 16.4      # basement, ground, gallery, roof floors
LID = 72.0                              # the clip lid (45 m over the dome's top)
EXT = "limestoneashlar"


def face(k, mat, box, top, t, extra=()):
    """a plate t thick under the plane y <= d - a x - c z (top = (a, 1, c, d)), cut to box and extra planes"""
    a, _, c, d = top
    n = (a * a + 1 + c * c) ** 0.5
    k.cut(mat, box, [top, (-a, -1, -c, -(d - t * n))] + list(extra))


def hip_dome(k, X0, Z0, X1, Z1, ye, s, ysplit, lo, hi, t=0.3, rafters=()):
    """a hipped roof over the rect (its ridge along x) from the eave ye at slope s: below ysplit `lo`, above it `hi`;
    rafters: x positions of castiron rafters under the north and south faces"""
    w = (Z1 - Z0) / 2; zm = Z0 + w; d = (ysplit - ye) / s; yr = ye + s * w
    y0 = ye - 0.6
    faces = [   # (box rect, top plane, region planes, split: lower rect, upper rect)
        ("n", (0, 1, -s, ye - s * Z0), [(-1, 0, 1, Z0 - X0), (1, 0, 1, X1 + Z0)], (X0, Z0, X1, Z0 + d), (X0, Z0 + d, X1, zm)),
        ("s", (0, 1, s, ye + s * Z1), [(-1, 0, -1, -X0 - Z1), (1, 0, -1, X1 - Z1)], (X0, Z1 - d, X1, Z1), (X0, zm, X1, Z1 - d)),
        ("w", (-s, 1, 0, ye - s * X0), [(1, 0, -1, X0 - Z0), (1, 0, 1, X0 + Z1)], (X0, Z0, X0 + d, Z1), (X0 + d, Z0, X0 + w, Z1)),
        ("e", (s, 1, 0, ye + s * X1), [(-1, 0, -1, -X1 - Z0), (-1, 0, 1, Z1 - X1)], (X1 - d, Z0, X1, Z1), (X1 - w, Z0, X1 - d, Z1)),
    ]
    for (side, top, reg, rl, ru) in faces:
        face(k, lo, (rl[0], y0, rl[1], rl[2], ysplit + 0.5, rl[3]), top, t, reg)
        face(k, hi, (ru[0], ysplit - 0.5, ru[1], ru[2], yr + 0.1, ru[3]), top, t, reg)
        if side in "ns":
            for x in rafters:   # under the plate: castiron rafters (the hooks inside the dome)
                a, _, c, dd = top
                n = (1 + c * c) ** 0.5
                face(k, "castiron", (x - 0.15, y0, (Z0 if side == "n" else zm), x + 0.15, yr, (zm if side == "n" else Z1)), (a, 1, c, dd - t * n), 0.35, reg)
    k.box("castiron", X0 + w, yr - 0.6, zm - 0.2, X1 - w, yr - 0.2, zm + 0.2)   # the ridge beam


def decorate(k):
    """sparingly, on walls, ceilings and edges (the lanes stay clear: momentum is huge)"""
    # exit signs (green light blocks) over the main doorways
    for (x, y, z, ax) in [(0, 5.4, -14, "x"), (0, 5.4, 10, "x"), (-20, 5.4, -2, "z"), (20, 5.4, -2, "z"), (-20, 4.0, -32, "z"),
                          (20, 4.0, -32, "z"), (0, U + 3.6, 10, "x"), (-20, U + 3.6, -2, "z"), (20, U + 3.6, -2, "z"),
                          (-40, 4.0, -24, "x"), (40, 4.0, -24, "x"), (-40, 4.0, 10, "x"), (38, 4.0, 10, "x")]:
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
    # the roof: a plant room (x 40..50, z 14..22: a door west, a dead end), duct runs and AC units along the parapets,
    # closed skylights over Birds and Ocean
    wall_x(k, "trapezoidcladding", 40, 50, 14, R, R + 3.2, t=0.3)
    wall_x(k, "trapezoidcladding", 40, 50, 22, R, R + 3.2, t=0.3)
    wall_z(k, "trapezoidcladding", 14, 22, 40, R, R + 3.2, [(18, 1.6, 2.6)], t=0.3)
    wall_z(k, "trapezoidcladding", 14, 22, 50, R, R + 3.2, t=0.3)
    k.box("leadsheet", 39.7, R + 3.2, 13.7, 50.3, R + 3.5, 22.3)
    k.box("ventilationduct", 46, R, 15, 49.5, R + 2.0, 21)
    for (x, z) in ((-52, -20), (-52, 20), (52, -20), (-30, 24), (24, 24)):
        k.box("radiatorfins", x - 1.2, R, z - 0.9, x + 1.2, R + 1.4, z + 0.9)
    for (x, z) in ((-32, -8), (32, -8)):
        k.box("wroughtiron", x - 3, R, z - 3, x + 3, R + 0.6, z + 3); k.box("dustyglass", x - 2.8, R + 0.6, z - 2.8, x + 2.8, R + 0.7, z + 2.8)
    k.light(45, R + 2.6, 18, 6, 0.4)
    # the basement: a pipe run under the ceiling down the archive's main aisle, caged bulbs
    k.box("greasestainedmetal", -55, -1.2, 11.2, -10, -0.8, 11.6)
    k.box("greasestainedmetal", -10, -1.2, 24.2, 20, -0.8, 24.6)
    # the shop: café tables by its east wall
    for (x, z) in ((-24, -28), (-28, -28)):
        k.disc("wideplanks", x, z, 0.6, 0, 0.75, n=8)
    # the Egyptian hall: statues either side of the mastaba's mouth, an obelisk
    for x in (36.4, 42.2):
        k.box("sandstoneashlar", x - 0.6, 0, 13.6, x + 0.6, 0.6, 14.8); k.box("gold", x - 0.4, 0.6, 13.8, x + 0.4, 3.4, 14.6)
    k.prism("sandstoneslab", [(52, 12.5), (53.5, 12.5), (53.5, 14), (52, 14)], 0, 6.0)
    k.cut("gold", (52, 6.0, 12.5, 53.5, 7.0, 14), [(1, 0.75, 0, 53.5 + 4.5), (-1, 0.75, 0, -52 + 4.5), (0, 0.75, 1, 14 + 4.5), (0, 0.75, -1, -12.5 + 4.5)])


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
            ((-HX, 10, -20, HZ), "linoleum"), ((-20, 10, 20, HZ), "polishedconcrete"), ((20, 10, HX, HZ), "sandstoneslab")]
    for (r, mat) in gmat:
        slab(k, mat, r, G, holes=[STAIR_A, STAIR_B, LIFT])
    # the galleries (8): Birds, Insects, the fossil gallery, the reading room, Ocean, the ring and the mezzanine;
    # the single-storey parts' roofs (north corners, the south strip past z 26) at the same height
    SERVICE_UP = (14, 14, 18, 21)            # the service stair's well
    ROOF_HATCH = (-HX, -1, -53.5, 1)        # the roof ladder's hatch (Birds)
    for (r, mat) in [((-HX, -24, -20, 26), "basketweaveparquet"), ((20, -24, HX, 26), "basketweaveparquet"),
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
    for (x0, z0, x1, z1, y) in [(-HX, -HZ, -20, -24, U), (20, -HZ, HX, -24, U), (-20, -HZ, 20, -14, 12.4), (-HX, 26, HX, HZ, U)]:
        k.box("clip", x0, y, z0, x1, LID, z1)
    # the roof's parapets on its inner edges (over the lower roofs)
    for (x0, z0, x1, z1) in [(-HX, -24.3, -20, -24), (20, -24.3, HX, -24), (-HX, 26, HX, 26.3), (-20.3, -24, -20, -14), (20, -24, 20.3, -14)]:
        k.box(EXT, x0, R, z0, x1, TOP, z1)

    # ================================================================== ground-floor walls (0..8 unless said)
    D = 3.6                       # door height
    # north line z -24: shop | Mammals, temporary exhibition | Minerals
    wall_x(k, "wainscotpanelling", -HX, -20, -24, G, U - 0.4, [(-40, 3.0, D), (-54.3, 1.2, 1.3)])   # (the crawl hatch)
    wall_x(k, "painteddrywall", 20, HX, -24, G, U - 0.4, [(40, 3.0, D)])
    # x -20 / 20: entrance | shop, entrance | Mammals, hall | Mammals (and the east side the same)
    for sx in (-1, 1):
        x = 20 * sx
        wall_z(k, "limestoneashlar", -HZ, -14, x, G, U - 0.4, [(-32, 4.0, D), (-19, 3.0, D)])
        wall_z(k, "limestoneashlar", -HZ, -24, x, U - 0.4, 12.4)              # (entrance | the shop's roof)
        wall_z(k, "limestoneashlar", -14, 10, x, G, U - 0.4, [(-2, 6.0, 5.0)])
        wall_z(k, "painteddrywall", 10, HZ, x, G, U - 0.4, [(34, 3.0, D)])
    # z -14 (entrance | hall): the ground door, the mezzanine's opening onto the ring
    wall_x(k, "limestoneashlar", -20, 20, -14, G, U - 0.4, [(0, 6.0, 5.0)])
    wall_x(k, "limestoneashlar", -20, 20, -14, U, R, [(0, 16.0, 3.6)])
    # z 10: hall | service hall; Mammals | fossil lab; Minerals | Egyptian
    wall_x(k, "limestoneashlar", -20, 20, 10, G, U - 0.4, [(0, 6.0, 5.0)])
    wall_x(k, "wainscotpanelling", -HX, -20, 10, G, U - 0.4, [(-40, 3.0, D), (-54.3, 1.2, 1.3)])
    wall_x(k, "sandstoneashlar", 20, HX, 10, G, U - 0.4, [(38, 3.0, D)])

    # ================================================================== gallery walls (8..16.4)
    for sx in (-1, 1):
        x = 20 * sx
        wall_z(k, "stripedwallpaper", -24, -14, x, U, R, [(-17, 3.0, 3.4)])        # Birds / Ocean | mezzanine
        wall_z(k, "limestoneashlar", -14, 10, x, U, R, [(-2, 4.0, 3.4)])            # Birds / Ocean | ring
        wall_z(k, "stripedwallpaper", 10, 26, x, U, R, [(18, 3.0, 3.4)])            # Insects / reading room | fossil gallery
        wall_x(k, "stripedwallpaper", 20 * sx, 55 * sx, 10, U, R, [(40 * sx, 3.0, 3.4)])   # Birds | Insects, Ocean | reading
    for (x0, x1) in ((-HX, -20), (20, HX)):
        k.box(EXT, x0, U, -24.2, x1, R, -23.8)           # (over the lower roofs: outer walls)
    k.box(EXT, -HX, U, 25.8, HX, R, 26.2)
    wall_x(k, "limestoneashlar", -20, 20, 10, U, R, [(0, 4.0, 3.4)])                # ring | fossil gallery
    # (the Great Hall's walls stop at the roof; a kerb round the dome's eave)
    for (x0, z0, x1, z1) in [(-20.4, -14, -20, 10), (20, -14, 20.4, 10), (-20, 9.8, 20, 10.4)]:
        k.box("limestoneashlar", x0, R, z0, x1, R + 0.4, z1)

    # ================================================================== the dome: a hipped roof, plaster below 21,
    # glass above (no way out: it's solid), castiron rafters and tie beams inside (the hooks), the whale under them
    hip_dome(k, -20.4, -14.4, 20.4, 10.4, R, 10.0 / 12.4, 21.4, "stucco", "glass", rafters=(-12, -6, 0, 6, 12))
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
    wall_x(k, "wainscotpanelling", -20, -12, -34, G, 4.0, [(-14, 1.6, 3.0)])
    wall_z(k, "wainscotpanelling", -40, -34, -12, G, 4.0)
    k.box("wainscotpanelling", -20, 4.0, -40, -12, 4.3, -34)
    # -- the Great Hall: the dinosaur. Plinth (x -11..5, z -4..2, 0.8, a step round it), spine walk at 6 from the
    # tail ramp (x 15 -> 5) to the neck (up to the skull at 9 over x -11..-14); legs, a ribcage over the plinth
    k.box("polishedgranite", -11.5, 0, -4.5, 5.5, 0.4, 2.5); k.box("polishedgranite", -11, 0.4, -4, 5, 0.8, 2)
    k.wedge("bleachedbones", 1, 5, 0.8, -1.6, 15, 6.0, -0.4)                    # the tail: up -x from the floor
    k.box("bleachedbones", -6, 5.4, -1.6, 5, 6.0, -0.4)                         # the spine
    k.wedge("bleachedbones", 1, -11, 6.0, -1.6, -6, 8.4, -0.4)                  # the neck (-x, up to the skull)
    k.box("bleachedbones", -11, 5.4, -1.6, -6, 6.0, -0.4)
    k.box("bone", -14.5, 7.6, -2.4, -11, 9.0, 0.4)                              # the skull (its top at 9)
    for x in (-5, 3):
        for z in (-3.4, 1.4):
            k.box("bone", x - 0.3, 0.8, z - 0.3, x + 0.3, 5.4, z + 0.3)        # legs
    for x in (-4, -2.5, -1, 0.5, 2):
        for (z0, z1) in ((-3.0, -2.8), (0.8, 1.0)):
            k.box("bone", x - 0.1, 2.4, z0, x + 0.1, 5.4, z1)                   # ribs (a cage over the plinth)
        k.box("bone", x - 0.1, 2.2, -3.0, x + 0.1, 2.4, 1.0)
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
    wall_x(k, "wainscotpanelling", -42, -30, -9.8, 0, 4.5, [(-36, 10.0, 3.6, 0.9)], glass="glass")
    wall_x(k, "wainscotpanelling", -42, -30, -2.2, 0, 4.5, [(-36, 10.0, 3.6, 0.9)], glass="glass")
    wall_z(k, "wainscotpanelling", -10, -2, -41.8, 0, 4.5)
    wall_z(k, "wainscotpanelling", -10, -2, -30.2, 0, 4.5, [(-6, 1.2, 2.4)])
    for z in (-7.2, -4.8):
        wall_x(k, "painteddrywall", -41.6, -30.4, z, 0, 4.5, [] if z < -6 else [], t=0.2)
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
    wall_x(k, "whitesquaretile", -30, -20, -35, 0, U - 0.4, [(-27, 1.6, 2.8)])
    wall_z(k, "whitesquaretile", -40, -35, -30, 0, U - 0.4)
    k.box("chrome", -29.6, 0, -39.6, -21, 1.0, -38.6)
    # -- the fossil lab's storeroom (x -32..-20, z 30..40: shelves, one door)
    wall_x(k, "painteddrywall", -32, -20, 30, 0, U - 0.4, [(-29, 2.0, 3.0)])
    wall_z(k, "painteddrywall", 30, HZ, -32, 0, U - 0.4)
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
    wall_x(k, "painteddrywall", -20, -10, 32, 0, U - 0.4, [(-17, 1.6, 2.8), (-13, 4.0, 2.4, 1.0)], glass="frostedglass")
    wall_z(k, "painteddrywall", 32, HZ, -10, 0, U - 0.4)
    k.box("controlpanel", -19.6, 0, 38.4, -12, 1.1, 39.6)
    # -- the Egyptian hall (south-east): the mastaba (x 30..50, z 16..36, 5 m) with a passage in from the north
    # (1.4 m wide, three turns) to the sarcophagus chamber; columns along its west side
    mast = (30, 16, 50, 36)
    path = [(38.6, 16, 40, 22), (34, 20.6, 40, 22), (34, 20.6, 35.4, 30), (35.4, 28.6, 44, 30), (40, 30, 46, 34)]
    for (x0, z0, x1, z1) in minus(mast, path):
        k.box("sandstoneashlar", x0, 0, z0, x1, 5, z1)
    k.box("sandstoneslab", mast[0], 5, mast[1], mast[2], 5.4, mast[3])
    k.box("gold", 41.5, 0, 31, 44.5, 1.0, 33)                                  # the sarcophagus
    for z in (14, 22, 30, 37):
        k.disc("sandstoneslab", 25, z, 0.7, 0, U - 0.4, n=8)

    # ================================================================== rooms: the galleries
    # -- Birds (west): tall cases (5 m, solid tops: hide on top, a ladder behind one), the cliff diorama (ledges up
    # the west wall), the roof ladder in the north-west of its middle (x -55, z 0)
    wall_z(k, "stripedwallpaper", -24, 10, -38, U, R, [(-14, 4.0, 3.4), (3, 4.0, 3.4)])     # halves: two doorways
    for (x, z) in ((-31, -16), (-25, -5), (-31, 5), (-46, 4)):
        k.box("wainscotpanelling", x - 2, U, z - 2, x + 2, U + 0.8, z + 2)
        k.box("glass", x - 1.9, U + 0.8, z - 1.9, x + 1.9, U + 4.6, z + 1.9)
        k.box("feathers", x - 0.6, U + 0.8, z - 0.6, x + 0.6, U + 2.6, z + 0.6)
        k.box("wainscotpanelling", x - 2, U + 4.6, z - 2, x + 2, U + 5.0, z + 2)
    k.ladder(-31, -18.12, U, U + 5.0, "s", mat="brass")      # (behind the north case)
    for (i, (x1, y)) in enumerate([(-50, 1.2), (-52, 2.4), (-53.5, 3.6)]):
        k.box("seabirdcliff", -HX, U, -22 + 0.0, x1, U + y, -8)
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
        wall_z(k, "waterstainedconcrete", z0, z1, 32.5, U, U + 6, [((z0 + z1) / 2, 4.0, 4.2, 1.2)], t=1.0, glass="bluetintedglass")
    for z in (-20, -8, 4):
        k.box("wetcaverock", 50, U, z - 3, HX, U + 7.6, z - 2.6); k.box("wetcaverock", 50, U, z + 2.6, HX, U + 7.6, z + 3)
        k.box("wetcaverock", 50, U + 4.4, z - 2.6, HX, U + 7.6, z + 2.6)
    k.box("coral", 40, U + 7.0, -8, 44, U + 7.6, -4)
    for (z0, z1) in ((-24, -16), (-4, 6)):
        wall_z(k, "waterstainedconcrete", z0, z1, 42.5, U, U + 6, [((z0 + z1) / 2, 3.0, 3.8, 1.4)], t=1.0, glass="bluetintedglass")
    for (dx, dz) in ((-1, -1), (1, -1), (-1, 1), (1, 1), (0, -1.5), (0, 1.5)):
        k.box("snakeskin", 42 + dx - 0.1, U + 3.6, -6 + dz - 0.1, 42 + dx + 0.1, U + 7.0, -6 + dz + 0.1)

    # ================================================================== the basement
    # -- the archive (x -55..-10): compact shelving rows 1 m deep, 1.4 m aisles, in two blocks; the frosted office
    for x in [round(-46 + 2.4 * i, 2) for i in range(10)]:
        for (z0, z1) in ((13, 23), (26, 36)):
            k.box("steelgridwall", x, B, z0, x + 1.0, B + 3.0, z1)
    wall_z(k, "painteddrywall", 30, HZ, -21.9, B, G - 0.4, [(35, 6.0, 3.2, 0.9)], t=0.2, glass="frostedglass")
    wall_x(k, "painteddrywall", -22, -10.2, 30.1, B, G - 0.4, [(-19, 4.0, 3.2, 0.9), (-13.2, 1.6, 3.0)], t=0.2, glass="frostedglass")
    wall_z(k, "cinderblock", 10, HZ, -10, B, G - 0.4, [(18, 3.0, 3.2), (24.5, 2.6, 3.2)])
    # -- the taxidermy room (x 6..20, z 30..40: one door)
    wall_x(k, "cinderblock", 6, 20, 30, B, G - 0.4, [(9, 2.0, 3.0)])
    wall_z(k, "cinderblock", 30, HZ, 6, B, G - 0.4)
    k.box("quiltedstainless", 10, B, 36, 18, B + 1.0, 38); k.box("fur", 12, B + 1.0, 36.4, 14, B + 1.8, 37.4)
    # -- the workshop: benches and crates
    for (x, z) in ((2, 14), (8, 34), (14, 14)):
        k.box("cratewood", x - 1.5, B, z - 1, x + 1.5, B + 1.0, z + 1)
    wall_z(k, "cinderblock", 10, HZ, 20, B, G - 0.4, [(25, 3.0, 3.2)])
    # -- the boiler room: the fuel tank (x 44..52, z 16..40, 4 m): a dead-end slot behind it from the north
    k.box("boilerplate", 44, B, 16, 52, B + 4.0, HZ)
    # -- the boiler room: two boilers, a pipe run
    for z in (18, 32):
        k.disc("boilerplate", 34, z, 2.4, B, B + 4.2, n=8)
    k.box("castiron", 24, B + 4.4, 12, 54, B + 4.9, 12.6)

    # ================================================================== lights (night: dim, coloured)
    for (x, y, z, r, i) in [(0, 10, -30, 16, 0.7), (0, 22, -2, 26, 0.6), (-12, 4, -2, 12, 0.5), (12, 4, -2, 12, 0.5),
                            (-38, 6, -6, 16, 0.55), (-50, 3.5, -10, 8, 0.6), (38, 6, -6, 16, 0.55), (44, 3, -11, 8, 0.5),
                            (-38, 6, -32, 12, 0.5), (38, 3.5, -32, 12, 0.45), (-38, 6, 26, 14, 0.5), (0, 6, 26, 14, 0.5),
                            (38, 6, 25, 14, 0.5), (40, 3.5, 32, 6, 0.6), (37, 3.5, 21, 6, 0.4),
                            (-38, 14, -6, 16, 0.5), (-38, 14, 18, 14, 0.45), (0, 14, 18, 14, 0.5), (38, 14, 18, 14, 0.45),
                            (0, 11, -17, 12, 0.5), (-32, -2, 24, 14, 0.45), (-32, -2, 18, 10, 0.3), (5, -2, 25, 14, 0.5),
                            (37, -2, 25, 14, 0.5), (-16, -3, 35, 6, 0.4), (-38, 18, 0, 20, 0.35), (38, 18, 0, 20, 0.35), (0, 18, 18, 14, 0.35)]:
        k.light(x, y, z, r, i)
    for (x, z) in ((42, -14), (52, -20), (52, -8), (52, 4)):   # Ocean: blue (the tint comes from the glow palette)
        k.light(x, U + 6, z, 9, 0.35)

    decorate(k)

    # ================================================================== spawns: the entrance hall (8, any)
    for (x, z) in ((-16, -28), (-9, -27), (9, -23), (17, -21), (-16, -22), (-9, -33), (9, -36), (17, -36)):
        k.spawn("any", x, G, z, yaw_to(x, z, 0, -14))
    m.save("museum", dress=False)
