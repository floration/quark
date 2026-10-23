"""HAMLET: hide and seek / tag. A village in a valley: streets around a green with a well, a church with a
bell tower you climb by ladder, a dozen houses (flat roofs reached by ladders, or pitched), a barn with a
hayloft, a windmill on a terraced hill, a water tower, a pond, a creek with footbridges, an orchard,
hedges, haystacks and carts. Every building has a way in; plenty of places to hide and to be found."""
from .prefabs import *


def build():
    m = M("Hamlet", "hide", sun=(0.45, 1.0, 0.3))
    m.raw("modes hide tag ffa")
    m.room(56, 56, 16, pits=[(52.5, -42, 55.5, -28, "water")], sunken=[(-46, 26, -30, 42, 1.0, "accent"), (-56, -6, -20, -2, 1.3, "metal")], style="yard", skyline="mountains")
    # the green and the well (centrepiece of the village)
    m.box(-12, -0.05, -12, 12, 0, 12, accent=True)
    for (a, b, c, d) in ((-1.6, -1.6, 1.6, -1.2), (-1.6, 1.2, 1.6, 1.6), (-1.6, -1.2, -1.2, 1.2), (1.2, -1.2, 1.6, 1.2)): m.box(a, 0, b, c, 1.0, d)
    m.glow(-1.2, 0.2, -1.2, 1.2, 0.3, 1.2)
    for (x, z) in ((-1.4, -1.4), (1.4, 1.4)): m.metal(x - 0.1, 1.0, z - 0.1, x + 0.1, 3.0, z + 0.1)
    m.box(-2.0, 3.0, -2.0, 2.0, 3.3, 2.0, accent=True); m.glow(-0.15, 2.4, -0.15, 0.15, 2.9, 0.15)
    for (x, z) in ((-8, -8), (8, -8), (-8, 8), (8, 8)): m.tree(x, z, 1.1)
    m.box(-6, 0, 5, -3, 0.6, 5.6); m.box(3, 0, -5.6, 6, 0.6, -5)                  # benches
    # the church (north of the green) with its bell tower: ladder inside up to the belfry
    m.building(-10, -34, 6, -20, 7, [("s", -2, 3.0), ("w", -27, 2.4)], windows=True, notch=(-6.75, -34.45, -5.25, -33.95))
    m.ladder(-6, -34.12, 0, 7.35, "s")                                                 # up onto the church roof (walk the tiers to the ridge)
    for k in range(1, 5): m.box(-10.3 + k * 0.9, 7 + 0.4 * k, -34.3 + k * 0.9, 6.3 - k * 0.9, 7.4 + 0.4 * k, -19.7 - k * 0.9, accent=True)   # stepped roof (walk up it)
    m.doorwall(6, -26, 12, -25.6, 16, [(9, 2.4, 2.6)], axis="x"); m.doorwall(6, -20.4, 12, -20, 16, [], axis="x")
    m.doorwall(6, -26, 6.4, -20, 16, [], axis="z"); m.doorwall(11.6, -26, 12, -20, 16, [], axis="z")
    for (a, b, c, d) in minus((6.4, -25.6, 11.6, -20.4), [(10.65, -23.7, 11.6, -22.7)]):   # belfry floor (a hole just over the ladder)
        m.box(a, 11.6, b, c, 12, d)
    m.ladder(11.48, -23.2, 0, 12, "e")
    for (a, b, c, d) in ((6, -26, 12, -25.6), (6, -20.4, 12, -20)): m.box(a, 12, b, c, 12.5, d)               # belfry: open arches above 12.5
    m.box(5.7, 15.4, -26.3, 12.3, 15.8, -19.7, accent=True); m.box(6.6, 15.8, -25.4, 11.4, 16.6, -20.6, accent=True)
    m.box(7.6, 16.6, -24.4, 10.4, 18.2, -21.6, accent=True); m.glow(8.8, 18.2, -23.2, 9.2, 19.6, -22.8)    # spire + light
    m.metal(8.3, 13.6, -23.7, 9.7, 15.0, -22.3)                                                          # the bell
    m.light(9, 14, -23, 6, 0.6, fixture=False)
    for z in (-31, -28, -24): m.box(-8, 0, z, -0.5, 0.5, z + 0.7); m.box(0.5, 0, z, 4, 0.5, z + 0.7)     # pews
    m.box(-3, 0, -33.5, 1, 1.2, -32.5, accent=True)                                                      # altar
    # houses: flat roofs with ladders (rooftop hiding) and pitched ones
    houses = [((-34, -36, -24, -28), 3.6, [("s", -29, 1.6)], "flat", (-23.88, -32, "w")),
              ((-48, -24, -38, -14), 3.6, [("e", -19, 1.6)], "pitched", None),
              ((-30, -16, -20, -8), 3.8, [("n", -25, 1.6), ("s", -25, 1.6)], "flat", (-30.12, -12, "e")),
              ((8, -38, 18, -30), 3.6, [("s", 13, 1.6)], "pitched", None),     # (clear of the windmill hill)
              ((-46, 8, -36, 18), 3.6, [("e", 13, 1.6)], "flat", (-46.12, 13, "e")),

              ((-14, 22, -4, 32), 3.6, [("n", -9, 1.6)], "flat", (-9, 32.12, "n")),
              ((4, 24, 14, 32), 3.6, [("n", 9, 1.6), ("w", 28, 1.6)], "pitched", None),
              ((18, 6, 28, 16), 3.8, [("w", 11, 1.6)], "flat", (28.12, 11, "w")),
              ((32, -16, 44, -6), 3.6, [("w", -11, 1.6), ("s", 38, 1.6)], "pitched", None),
              ((-12, 40, 0, 50), 3.6, [("n", -6, 1.6)], "flat", (0.12, 45, "w")),
              ((18, 44, 30, 52), 3.6, [("n", 24, 1.6)], "pitched", None)]
    for (r, h, doors, roof, lad) in houses:
        m.house(r[0], r[1], r[2], r[3], h, doors, roof=roof, ladder=lad)
        m.box(r[0] + 1, 0, r[1] + 1, r[0] + 2.2, 0.8, r[1] + 2.2)                     # a table / chest inside
    # a two-storey house: upper floor up a ladder inside, windows on both floors
    m.building(-26, 10, -16, 20, 7.0, [("n", -21, 1.8), ("e", 15, 1.8)], windows=True)
    for (a, b, c, d) in minus((-25.6, 10.4, -16.4, 19.6), [(-25.3, 18.7, -24.3, 19.6)]):
        m.box(a, 3.3, b, c, 3.6, d)                                                    # upper floor (hole over the ladder)
    m.ladder(-24.8, 19.48, 0, 3.6, "s")
    for k in range(1, 4): m.box(-26.3 + k * 1.4, 7 + 0.35 * k, 9.7, -15.7 - k * 1.4, 7.35 + 0.35 * k, 20.3, accent=True)
    m.light(-21, 6.2, 15, 7, 0.7)
    m.box(-19, 3.6, 12, -17.5, 4.4, 13.5); m.box(-25, 3.6, 11, -22, 4.1, 12)         # furniture upstairs
    # rooftop plank bridges between neighbouring flat roofs (sitting on the parapets)
    m.metal(-28.6, 4.2, -28.3, -27.2, 4.5, -22); m.metal(-28.6, 4.4, -22, -27.2, 4.7, -15.7)
    # lookout tower at the orchard's edge: posts, a platform 7 m up with rails, a ladder
    for (x, z) in ((46, 8), (50, 8), (46, 12), (50, 12)): m.metal(x - 0.2, 0, z - 0.2, x + 0.2, 7, z + 0.2)
    m.box(45.6, 6.7, 7.6, 50.4, 7.0, 12.4, accent=True)
    for (a, b, c, d) in ((45.6, 7.6, 50.4, 7.75), (45.6, 12.25, 50.4, 12.4), (50.25, 7.75, 50.4, 12.25)): m.box(a, 7.0, b, c, 8.0, d, accent=True)
    m.box(45.6, 7.0, 7.75, 45.75, 8.0, 9.2, accent=True); m.box(45.6, 7.0, 10.8, 45.75, 8.0, 12.25, accent=True)
    m.box(45.4, 9.6, 7.4, 50.6, 9.9, 12.6, accent=True)                               # little roof
    for (x, z) in ((45.6, 7.6), (50.2, 7.6), (45.6, 12.2), (50.2, 12.2)): m.metal(x, 7.0, z, x + 0.2, 9.6, z + 0.2)
    m.box(45.6, 0, 9.2, 45.8, 6.7, 10.8)                                              # board behind the ladder
    m.ladder(45.48, 10, 0, 7.0, "e")
    m.light(48, 8.8, 10, 5, 0.5, fixture=False)
    # the barn (south-east) with a hayloft up a ladder
    m.building(30, 22, 48, 42, 8, [("w", 32, 4.0), ("n", 39, 2.4)], windows=False)
    for k in range(1, 5): m.box(29.7 + k * 1.1, 8 + 0.45 * k, 21.7, 48.3 - k * 1.1, 8.45 + 0.45 * k, 42.3, accent=True)
    m.box(38, 3.7, 22.4, 47.6, 4.0, 41.6)                                              # hayloft floor
    m.box(38, 0, 31.8, 38.3, 3.7, 34.2)                                                 # wall behind the loft ladder
    m.ladder(37.88, 33, 0, 4.0, "e")
    m.box(38, 4.0, 22.4, 38.1, 5.0, 32, accent=True); m.box(38, 4.0, 34, 38.1, 5.0, 41.6, accent=True)   # loft rail (gap at the ladder)
    for (x, z) in ((40, 25), (44, 26), (42, 38), (45, 35)): m.box(x - 1, 4.0, z - 0.7, x + 1, 5.2, z + 0.7, accent=True)   # bales up top
    for (x, z) in ((33, 26), (34, 38), (44, 30)): m.box(x - 1, 0, z - 0.7, x + 1, 1.2, z + 0.7, accent=True)                # and below
    m.light(39, 6.5, 32, 12, 0.8)
    # terraced hill with the windmill (north-east)
    for k, (a, b) in enumerate(((22, 52), (26, 48), (30, 44), (33, 41))):
        m.box(a, 0, -b, b, 1.5 * (k + 1), -a)
    m.stairs(36, -22, 38, -20, 1.5, "z", -1)
    m.stairs(20, -38, 22, -34, 1.5, "x", 1)
    m.stairs(36, -26, 38, -22, 3.0, "z", -1, base=1.5); m.stairs(22, -38, 26, -36, 3.0, "x", 1, base=1.5)
    m.stairs(36, -30, 38, -26, 4.5, "z", -1, base=3.0)
    m.stairs(36, -33, 38, -30, 6.0, "z", -1, base=4.5)
    m.box(35, 6, -39, 39, 14, -35, accent=True)                                         # the windmill tower
    m.box(34.5, 14, -39.5, 39.5, 15, -34.5); m.box(35.5, 15, -38.5, 38.5, 16.2, -35.5)
    m.metal(36.7, 13.6, -35, 37.3, 14.2, -33.8)                                          # hub, sails
    m.box(36.85, 9, -33.8, 37.15, 18.8, -33.5, mat="metal"); m.box(32.2, 13.75, -33.8, 41.8, 14.05, -33.5, mat="metal")
    for (y0, y1) in ((14.4, 18.6), (9.2, 13.4)): m.box(36.2, y0, -33.7, 36.85, y1, -33.6, accent=True)
    for (x0, x1) in ((32.4, 36.6), (37.4, 41.6)): m.box(x0, 14.05, -33.7, x1, 14.7, -33.6, accent=True)
    # water tower (north-west) with a ladder to the walkable tank top
    for (x, z) in ((-50, -50), (-44, -50), (-50, -44), (-44, -44)): m.metal(x - 0.3, 0, z - 0.3, x + 0.3, 8, z + 0.3)
    m.box(-51, 8, -51, -43, 11, -43, accent=True); m.box(-51.3, 11, -51.3, -42.7, 11.3, -43)
    m.ladder(-47, -42.88, 0, 11.3, "n")
    m.metal(-51, 3.8, -50.1, -43, 4.1, -49.9); m.metal(-51, 3.8, -44.1, -43, 4.1, -43.9)
    # pond (south-west) with reeds and a jetty
    for (x, z) in ((-44, 28), (-32, 40), (-45, 39), (-31, 27)): m.metal(x - 0.06, -1, z - 0.06, x + 0.06, 0.8, z + 0.06)
    m.box(-38, -1.0, 34, -36, 0, 42)
    # creek (west) with two footbridges
    for x in (-48, -30): m.box(x - 1.2, -0.25, -6.2, x + 1.2, 0, -1.8, accent=True); m.box(x - 1.3, 0, -6.2, x - 1.2, 0.9, -1.8, accent=True); m.box(x + 1.2, 0, -6.2, x + 1.3, 0.9, -1.8, accent=True)
    # orchard (south-east of the green), hedges, haystacks, carts, fences
    for (x, z) in ((36, 4), (42, 8), (48, 4), (36, 14), (48, 14), (42, -2), (50, -18)): m.tree(x, z, 0.9)
    for (x, z) in ((-20, -44), (-8, -46), (0, -44), (-40, -2), (-26, 34), (40, 50), (48, 48), (-52, 50), (8, 52)): m.tree(x, z, 1.0)
    for (a, b, c, d) in ((-18, 4, -14, 5), (-18, -5, -14, -4), (14, -18, 15, -14), (24, 0, 34, 1), (-52, 20, -48, 21), (0, 36, 10, 37)):
        m.box(a, 0, b, c, 1.6, d, accent=True)                                         # hedges
    for (x, z) in ((-22, 26), (12, 18), (-38, -40), (46, -48), (30, 0)): m.box(x - 1.2, 0, z - 1.2, x + 1.2, 2.2, z + 1.2, accent=True); m.box(x - 0.8, 2.2, z - 0.8, x + 0.8, 3.0, z + 0.8, accent=True)   # haystacks
    for (x, z, ax) in ((-14, -18, 1), (24, 20, 0), (-34, 4, 1)):                         # hay carts
        w, l = (1.0, 1.8) if ax else (1.8, 1.0)
        m.box(x - w, 0.5, z - l, x + w, 1.4, z + l, accent=True)
        m.metal(x - 0.5 if not ax else x - w - 0.15, 0, z - 0.5 if ax else z - l - 0.15, x + 0.5 if not ax else x - w, 1.0, z + 0.5 if ax else z - l)
    for z in range(-54, 56, 4):                                                       # fence along the east wall (not over the gully)
        if not -43 < z < -27: m.box(54.2, 0, z, 54.4, 1.1, z + 0.2, accent=True)
    for (x, z, yaw) in ((0, 14, 180), (0, -14, 0), (14, 0, 90), (-14, 0, -90), (-34, -22, 90), (-40, 0, 0), (-12, -40, 0), (20, -20, 180),
                        (28, -44, 0), (-50, 32, 90), (-22, 42, 180), (10, 40, 180), (34, 46, 180), (50, 26, -90), (52, -12, -90), (-6, 52, 180),
                        (24, 22, 0), (-24, 2, 90), (-48, -32, 0), (8, -46, 0)):
        m.spawn("any", x, 3.0 if (x, z) == (28, -44) else 0, z, yaw)
    # the pond's water (shallow: you wade through it)
    m.box(-46, -0.3, 26, -30, -0.15, 42, mat="water")
    # garden sheds and woodpiles beside the houses and the open streets: plank walls, so a seeker can shoot
    # through where a hider is crouched (and something to duck behind out on the green and the lanes)
    for (x0, z0, x1, z1, h) in ((-22, -40, -19, -37, 2.2), (-38, -27, -35, -24, 2.0), (28, -26, 31, -23, 2.2), (-30, 22, -27, 25, 2.0),
                                (14, 34, 17, 37, 2.0), (-8, 16, -5, 18, 1.4), (5, -18, 8, -16, 1.4), (16, -6, 18, -3, 1.4), (-18, 6, -16, 9, 1.4),
                                (-40, -12, -37, -10, 1.4), (24, -2, 27, 0, 1.4), (2, 18, 4, 21, 1.4), (-4, -18, -2, -16, 1.4)):
        m.wallbang(x0, 0, z0, x1, h, z1)
    # street lamps round the green and along the lanes (light for the night, and you can see who's coming)
    for (x, z) in ((-12, -12), (12, -12), (-12, 12), (12, 12), (0, -18), (0, 18), (-18, 0), (18, 0), (-30, -24), (30, 24),
                   (-36, 4), (36, -4), (-6, 36), (6, -36), (24, -14), (-24, 14)):
        m.metal(x - 0.08, 0, z - 0.08, x + 0.08, 3.4, z + 0.08); m.box(x - 0.25, 3.4, z - 0.25, x + 0.25, 3.7, z + 0.25, accent=True)
        m.light(x, 3.2, z, 9, 0.7, fixture=False)
    # dry-stone field walls and log piles on every open patch of ground (placed only where nothing stands within 1.3 m
    # and away from spawns): the valley floor was a lot of open grass with nowhere to break line of sight
    m.clutter(-54, -54, 54, 54, 5, [(3.0, 0.6, 1.2), (0.6, 3.0, 1.2), (1.4, 1.4, 1.8), (2.0, 1.2, 1.5)], seed=11)
    m.raw("setting valley village")
    m.save("hamlet")
