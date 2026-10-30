"""DATACENTER: bomb, two sites (q, 2026-10-30: "two on data center"). A hyperscale data centre at night, 4v4.
Attackers (a, T) start in the loading-dock yard at the south; defenders (b, CT) in the security / NOC building across
the courtyard at the north. Site A is the singularity room (an octagon 36 m across and 18 m tall, data-rain walls, a
gallery ring at 8.4 m, the singularity hanging over its plinth); site B is the UPS hall (a 14 m hall of battery and UPS
cabinets with a catwalk on each long side). q's timing rule: T reach a site in about 5 s at 24 m/s (~120 m), CT about
1 s later (~144 m), measured along the walked route.
Levels: the ground floor (0), the cooling roof over the south half and the middle (8.4, outdoors: chillers, the exhaust
duct into A's gallery, the door onto B's south catwalk) and the office floor over the north band (8.4, the CT side:
the bridge door into A's gallery and the balcony over B). Under the server hall, the raised-floor plenum (2.2 m clear)
runs from a hatch in the hall to a hatch inside A.
Routes, T: server hall (the cold aisles) -> A south door; the plenum -> A's hatch; the west stair -> roof -> the duct ->
A's gallery; receiving -> mid -> the ring corridor -> A's south-east door or B's west door; the plant hall -> B's two
south doors; the east stair -> roof -> B's south catwalk.
Routes, CT: the security airlock -> A's north door; the lobby -> the ring corridor -> A's north-east door; the lobby
stairs -> offices -> A's gallery or B's balcony; the courtyard -> the battery room -> B's two north doors; mid -> B west."""
import math
from .prefabs import M, minus, tiles
from .m2kit import B, wedge, prism, wall, door, octagon, clip, clip_rect, poly_wall, light, sphere, MAT

# levels
C, U, UC, RT = 8.0, 8.4, 14.4, 14.8    # ground ceiling underside, upper floor / roof deck top, upper ceiling, upper roof top
# site A: the singularity room
AX, AZ = -58.0, 9.0
A_IN, A_OUT, A_H = 18.0, 19.0, 18.0
# the plenum under the hall: inside x -64..-58, z 18..60, floor -2.6, ceiling -0.4
PX0, PX1, PZ0, PZ1, PY = -64.0, -58.0, 18.0, 60.0, -2.6
H1 = (-64.0, 56.0, -61.0, 59.0)        # the hall hatch
H2 = (-64.0, 19.0, -61.0, 22.0)        # the hatch in A
# site B: the UPS hall, inside x 37..84, z -10..27, 14.4 tall
BX0, BZ0, BX1, BZ1 = 37.0, -10.0, 84.0, 27.0


def build():
    m = M("Datacenter", "bomb", sun=(0.25, 1.0, -0.35))
    L = m.lines

    def floor(mat, rect, holes=(), y0=-1.0, y1=0.0, size=16):
        for r in tiles(minus(rect, list(holes)), size): B(m, mat, r[0], y0, r[1], r[2], y1, r[3])

    def ceil(rect, holes=(), mat="insulatedpanel", y0=C, y1=U, size=24, roof=None):
        """a slab; roof: its top 10 cm in that (a roof's covering over a room's ceiling)"""
        for r in tiles(minus(rect, list(holes)), size):
            B(m, mat, r[0], y0, r[1], r[2], y1 - (0.1 if roof else 0), r[3])
            if roof: B(m, roof, r[0], y1 - 0.1, r[1], r[2], y1, r[3])

    OCT_OUT = octagon(AX, AZ, A_OUT)
    OCT_IN = octagon(AX, AZ, A_IN)
    OCT_SKIN = octagon(AX, AZ, A_IN + 0.5)

    # ================= floors (ground) =================
    floor("oilstainedconcrete", (-46, 63, 46, 90))                                        # the dock yard
    floor("loadingdockfloor", (-46, 62, 46, 63))                                          # (under the dock doors)
    floor("raisedaccessfloor", (-96, 27, -19, 62), [(-77, 27, -39, 28), (PX0 - 1, 27, PX1 + 1, PZ1 + 1)])     # server hall (less the plenum)
    floor("loadingdockfloor", (-19, 27, 19, 62))                                          # receiving
    floor("epoxyfloor", (19, 27, 96, 62))                                                 # plant hall
    floor("polishedconcrete", (-96, -10, -77, 27))                                        # west ring corridor
    floor("polishedconcrete", (-39, -10, -27, 27))                                        # east ring corridor
    s19 = A_OUT * math.tan(math.pi / 8)
    tri = {"nw": [(-77, -10), (AX - s19, -10), (-77, AZ - s19)], "sw": [(-77, AZ + s19), (AX - s19, 28), (-77, 28)],
           "ne": [(AX + s19, -10), (-39, -10), (-39, AZ - s19)], "se": [(-39, AZ + s19), (-39, 28), (AX + s19, 28)]}
    for t in tri.values(): prism(m, "polishedconcrete", t, -1, 0)
    # A's floor (the outer octagon, under its walls too) less the plenum's strip
    for part in (clip(OCT_OUT, 1, 0, PX0 - 1), clip(OCT_OUT, -1, 0, -(PX1 + 1)), clip_rect(OCT_OUT, PX0 - 1, -20, PX1 + 1, PZ0 - 1)):
        if part: prism(m, "circuit", part, -1, 0)
    floor("terrazzo", (-27, -10, 36, 27))                                                 # mid
    floor("epoxyfloor", (36, -10, 85, 27))                                                # B
    floor("polishedconcrete", (85, -10, 96, 27))                                          # east corridor
    floor("steelcheckerfloor", (-96, -46, -40, -10))                                      # security
    floor("terrazzo", (-40, -46, 36, -10))                                                # lobby
    floor("epoxyfloor", (36, -46, 96, -10))                                               # battery room
    floor("loadingdockfloor", (-96, -47, 96, -46))                                        # (under the north doors)
    floor("concretepavers", (-96, -64, 96, -47))                                          # courtyard
    floor("officecarpettiles", (-32, -96, 32, -64))                                       # NOC

    # ================= the plenum =================
    B(m, "concrete", PX0, PY - 1, PZ0, PX1, PY, PZ1)                                       # its floor
    for (a, b, c, d) in ((PX0 - 1, PZ0 - 1, PX0, PZ1 + 1), (PX1, PZ0 - 1, PX1 + 1, PZ1 + 1), (PX0, PZ0 - 1, PX1, PZ0), (PX0, PZ1, PX1, PZ1 + 1)):
        B(m, "darkconcrete", a, PY - 1, b, c, -0.4, d)
    for r in minus((PX0 - 1, PZ0 - 1, PX1 + 1, PZ1 + 1), [H1, H2]): B(m, "raisedaccessfloor", r[0], -0.4, r[1], r[2], 0, r[3])
    m.ladder(PX0 + 0.4, (H1[1] + H1[3]) / 2, PY, 0, "w")
    m.ladder(PX0 + 0.4, (H2[1] + H2[3]) / 2, PY, 0, "w")

    # ================= the outer shell =================
    # south facade (dock doors: west into the hall, centre into receiving, east into the plant hall; parapet gaps
    # where the yard's two stairs reach the roof)
    wall(m, "trapezoidcladding", -96, 62, 96, 63, 0, U, [door(-32, 6, 5), door(0, 6, 5), door(32, 6, 5)])
    wall(m, "trapezoidcladding", -96, 62, 96, 63, U, U + 1.2, [(-46, -41, U, U + 2), (41, 46, U, U + 2)])
    wall(m, "trapezoidcladding", -97, 27, -96, 63, 0, U + 1.2); wall(m, "trapezoidcladding", 96, 27, 97, 63, 0, U + 1.2)
    wall(m, "trapezoidcladding", -97, -47, -96, 27, 0, RT + 1.2); wall(m, "trapezoidcladding", 96, -47, 97, 27, 0, RT + 1.2)
    # north facade (the security door, the lobby's front door, the battery room door)
    wall(m, "trapezoidcladding", -96, -47, 96, -46, 0, RT + 1.2, [door(-46, 5), door(0, 6), door(52, 5)])

    # ================= ground floor walls =================
    WALL = "painteddrywall"
    # server hall north wall (the octagon's own south face between): doors to the west ring and the east ring
    wall(m, WALL, -96, 27, AX - s19, 28, 0, C, [door(-89.75)])
    wall(m, WALL, AX + s19, 27, -19, 28, 0, C, [door(-33.5)])
    wall(m, WALL, -20, 28, -19, 62, 0, C, [door(44.75), door(33)])                        # hall | receiving
    wall(m, WALL, 19, 28, 20, 62, 0, C, [door(44.75)])                                    # receiving | plant
    wall(m, WALL, -19, 27, 20, 28, 0, C, [door(0, 6)])                                    # receiving | mid
    wall(m, WALL, 20, 27, 36, 28, 0, C)                                                   # plant | mid
    wall(m, WALL, 85, 27, 96, 28, 0, C, [door(90.5)])                                     # plant | east corridor
    wall(m, WALL, -28, -10, -27, 27, 0, C, [door(0.5), door(18)])                         # east ring | mid
    # the north band's south wall (two storeys): ground doors, then the office facade (windows on the roof, the bridge
    # door into A's gallery, the balcony door over B)
    gdoors = [door(-89.75), door(AX), door(-33.5), door(0, 6), door(46), door(75), door(90.5)]
    wall(m, WALL, -96, -11, 96, -10, 0, C, gdoors)
    win = [(x, x + 4, U + 1.2, U + 3.4) for x in (-30, -20, -10, 2, 12, 22)]
    wall(m, WALL, -96, -11, 96, -10, C, RT, [(AX - 2, AX + 2, U, U + 3.8), (58.5, 62.5, U, U + 3.8)] + win)
    for (x0, x1) in ((-30, -26), (-20, -16), (-10, -6), (2, 6), (12, 16), (22, 26)):
        B(m, "bluetintedglass", x0, U + 1.2, -10.9, x1, U + 3.4, -10.1)
    wall(m, WALL, -40, -46, -39, -11, 0, C, [door(-28)])                                   # security | lobby
    wall(m, WALL, 36, -46, 37, -11, 0, C, [door(-28)])                                     # lobby | battery
    # security's airlock: the outer door (north facade) and the inner wall's two doors, offset (no line through)
    wall(m, "steeldoorskin", -96, -37, -40, -36, 0, C, [door(-55), door(-76), (-50, -46, 1, 4.5)])
    B(m, "bluetintedglass", -50, 1, -36.9, -46, 4.5, -36.1)                                # (the guard's window)

    # ================= site A: the singularity room =================
    def face_holes(k):
        """openings in face k (N, NE, E, SE, S, SW, W, NW): (centre along the face from its middle, width, y0, y1)"""
        return {0: [(0, 4.5, 0, 4), (0, 4, U, U + 3.8)],     # N: the airlock door; the office bridge into the gallery
                1: [(0, 4.5, 0, 4)],                         # NE: the east ring (CT side)
                3: [(0, 4.5, 0, 4)],                         # SE: the east ring (T side)
                4: [(0, 4.5, 0, 4), (0, 4, U, U + 3.8)],     # S: the server hall; the exhaust duct off the roof
                }.get(k, [])
    for k in range(8):
        o0, o1 = OCT_OUT[k], OCT_OUT[(k + 1) % 8]
        L0 = math.hypot(o1[0] - o0[0], o1[1] - o0[1]); u = ((o1[0] - o0[0]) / L0, (o1[1] - o0[1]) / L0)
        cu = u[0] * (o0[0] + o1[0]) / 2 + u[1] * (o0[1] + o1[1]) / 2
        holes = [(cu + c - w / 2, cu + c + w / 2, y0, y1) for (c, w, y0, y1) in face_holes(k)]
        shell = [o0, o1, OCT_SKIN[(k + 1) % 8], OCT_SKIN[k]]
        skin = [OCT_SKIN[k], OCT_SKIN[(k + 1) % 8], OCT_IN[(k + 1) % 8], OCT_IN[k]]
        poly_wall(m, "darkconcrete", shell, u, holes, 0, A_H)
        poly_wall(m, "scifipanels", skin, u, holes, 0, U)                  # (the lower half: dark panels)
        poly_wall(m, "datarain", skin, u, holes, U, A_H)                   # the data-rain walls above the gallery
    prism(m, "darkconcrete", OCT_OUT, A_H, A_H + 0.6)                      # its roof
    # the gallery ring at 8.4 (3 m wide), with the ladders' gaps in its rail
    G_IN = octagon(AX, AZ, A_IN - 3)
    s15, s18 = (A_IN - 3) * math.tan(math.pi / 8), A_IN * math.tan(math.pi / 8)
    for k in range(8):
        piece = [OCT_IN[k], OCT_IN[(k + 1) % 8], G_IN[(k + 1) % 8], G_IN[k]]
        prism(m, "galvanizedgrating", piece, C, U)
        # its rail: 1 m, on the inner edge (thin: hitscan goes through), left open in front of the two ladders
        a, b = G_IN[k], G_IN[(k + 1) % 8]
        L0 = math.hypot(b[0] - a[0], b[1] - a[1]); ux, uz = (b[0] - a[0]) / L0, (b[1] - a[1]) / L0
        nx, nz = -uz, ux
        if (AX - a[0]) * nx + (AZ - a[1]) * nz < 0: nx, nz = -nx, -nz
        rail = [a, b, (b[0] + nx * 0.12, b[1] + nz * 0.12), (a[0] + nx * 0.12, a[1] + nz * 0.12)]
        cu = ux * (a[0] + b[0]) / 2 + uz * (a[1] + b[1]) / 2
        gaps = [(cu - 1.2, cu + 1.2, U, U + 2)] if k in (2, 6) else []
        poly_wall(m, "galvanizedgrating", rail, (ux, uz), gaps, U, U + 1.0)
    # ladders up to the gallery on two steel columns (E and W), for the walk back up
    for sx in (1, -1):
        cx = AX + sx * (A_IN - 3)
        B(m, "steel", cx - (0.8 if sx > 0 else 0), 0, AZ - 0.6, cx + (0 if sx > 0 else 0.8), C, AZ + 0.6)
        m.ladder(cx - sx * 1.2, AZ, 0, U, "e" if sx > 0 else "w")
    # the plinth and the singularity: an octagonal plinth 3 m tall; the core, a glowing ball hanging at 10 m, inside a
    # shield shell (stops shots, rockets and grapples, not players or sight: nobody hangs off it over the site)
    prism(m, "darkconcrete", octagon(AX, AZ, 3.0), 0, 0.6)
    prism(m, "scifipanels", octagon(AX, AZ, 2.4), 0.6, 3.0)
    prism(m, "glow", octagon(AX, AZ, 2.6), 3.0, 3.15)
    sphere(m, "lightblock", AX, 10.0, AZ, 2.2, bands=6, sectors=8)
    prism(m, "shieldwall", octagon(AX, AZ, 3.0), 7.2, 12.8)
    L.append(f"site {AX - 4:g} 0 {AZ - 8:g} {AX + 12:g} 4 {AZ + 8:g}")   # (east of the plinth: its middle on open floor)

    # ================= site B: the UPS hall (two storeys tall, a catwalk on each long side) =================
    wall(m, WALL, 36, -10, 37, 27, 0, RT, [door(9)])                                       # W: mid
    wall(m, WALL, 84, -10, 85, 27, 0, RT)                                                  # E
    wall(m, WALL, 36, 27, 85, 28, 0, RT, [door(46), door(75), (58.5, 62.5, U, U + 3.8)])    # S: the plant hall; the roof door
    for (z0, z1) in ((23, 27), (-10, -6)):   # the south catwalk (T, off the roof) and the north balcony (CT, off the offices)
        B(m, "meshcatwalk", BX0, C, z0, BX1, U, z1)
        zr = z0 if z0 > 0 else z1 - 0.12
        wall(m, "galvanizedgrating", BX0, zr, BX1, zr + 0.12, U, U + 1.0, [(41, 43.4, U, U + 2), (77.6, 80, U, U + 2)])
        for x in (42.2, 78.8):   # columns with ladders, from the hall floor to each walkway
            zc = z0 - 0.6 if z0 > 0 else z1 + 0.6
            B(m, "steel", x - 0.6, 0, min(zc, z0 if z0 > 0 else z1), x + 0.6, C, max(zc, z0 if z0 > 0 else z1))
            m.ladder(x, zc - 0.4 if z0 > 0 else zc + 0.4, 0, U, "s" if z0 > 0 else "n")
    L.append(f"site {60.5 - 8:g} 0 {8.5 - 8:g} {60.5 + 8:g} 4 {8.5 + 8:g}")

    # ================= ceilings and roofs =================
    ROOF = "birdspottedroof"
    ceil((-96, 28, 96, 62), roof=ROOF)                                                     # the cooling roof deck
    ceil((-96, -10, -77, 28), roof=ROOF); ceil((-39, -10, 36, 28), roof=ROOF); ceil((85, -10, 96, 28), roof=ROOF)
    for t in tri.values(): prism(m, "insulatedpanel", t, C, U - 0.1); prism(m, ROOF, t, U - 0.1, U)
    STAIRW, STAIRE = (-24, -42, -20, -22), (16, -42, 20, -22)
    ceil((-96, -46, 96, -10), [(STAIRW[0], -34, STAIRW[2], -22), (STAIRE[0], -34, STAIRE[2], -22)], mat="officecarpettiles")
    ceil((-96, -46, 96, -10), mat="pressedtinceiling", y0=UC, y1=RT)                       # offices' roof
    ceil((36, -10, 85, 28), mat="pressedtinceiling", y0=UC, y1=RT)                         # B's roof
    # the lobby's two stairs up to the offices
    m.stairs(STAIRW[0], STAIRW[1], STAIRW[2], STAIRW[3], U, "z", 1)
    m.stairs(STAIRE[0], STAIRE[1], STAIRE[2], STAIRE[3], U, "z", 1)

    # ================= the roof (8.4): the two yard stairs, the exhaust duct into A =================
    m.stairs(-46, 66, -42, 84, U, "z", -1); B(m, "steeldeckplate", -46, 0, 63, -42, U, 66)
    m.stairs(42, 66, 46, 84, U, "z", -1); B(m, "steeldeckplate", 42, 0, 63, 46, U, 66)
    # the exhaust duct: a 4 m tunnel off the roof into A's south face at gallery height
    B(m, "ventilationduct", AX - 3, U, A_OUT + AZ, AX - 2, U + 4.6, A_OUT + AZ + 8)
    B(m, "ventilationduct", AX + 2, U, A_OUT + AZ, AX + 3, U + 4.6, A_OUT + AZ + 8)
    B(m, "ventilationduct", AX - 3, U + 3.8, A_OUT + AZ, AX + 3, U + 4.6, A_OUT + AZ + 8)
    # roof cover: chiller units in two staggered rows across the south roof, fan stacks over mid, a pipe rack; lanes
    # kept from each stair to the duct and to B's roof door
    for (x0, z0) in ((-92, 44), (-80, 52), (-40, 44), (-28, 52), (-14, 44), (-2, 52), (10, 44), (22, 52), (54, 44), (66, 52), (78, 44), (88, 52)):
        B(m, "radiatorfins", x0, U, z0, x0 + 7, U + 2.6, z0 + 3.2)
    for (x, z) in ((-16, 2), (-16, 18), (2, 2), (2, 18), (22, 10), (-88, 4), (-88, 18), (90, 4), (90, 18)):
        prism(m, "heatvents", octagon(x, z, 2.2), U, U + 1.6); prism(m, "galvanizedgrating", octagon(x, z, 2.2), U + 1.6, U + 1.8)
    for x in range(-34, 31, 8): B(m, "steel", x - 0.25, U, 33.75, x + 0.25, U + 3.4, 34.25)
    B(m, "ventilationduct", -36, U + 3.4, 33, 32, U + 4.4, 35)
    # parapets round the roof's edge toward the tall parts are their walls; the east corridor's roof meets B's wall

    # ================= the yard (T spawn) and the courtyard / NOC (CT spawn) =================
    wall(m, "corrugated", -47, 63, -46, 91, 0, 6); wall(m, "corrugated", 46, 63, 47, 91, 0, 6)
    wall(m, "corrugated", -46, 90, 46, 91, 0, 6)
    wall(m, "precastconcrete", -97, -65, -32, -64, 0, 6); wall(m, "precastconcrete", 32, -65, 97, -64, 0, 6)
    wall(m, "precastconcrete", -97, -64, -96, -47, 0, 6); wall(m, "precastconcrete", 96, -64, 97, -47, 0, 6)
    # the NOC: CT's spawn behind its glass front; doors at both ends of the front
    wall(m, "whiteconcrete", -33, -97, -32, -64, 0, 6.4); wall(m, "whiteconcrete", 32, -97, 33, -64, 0, 6.4)
    wall(m, "whiteconcrete", -32, -97, 32, -96, 0, 6.4)
    wall(m, "whiteconcrete", -32, -65, 32, -64, 0, 6.4, [door(-25), door(25), (-18, 18, 1, 5)])
    B(m, "bluetintedglass", -18, 1, -64.9, 18, 5, -64.1)
    B(m, "whiteconcrete", -33, 6.4, -97, 33, 6.8, -64)

    # ================= cover (layout) =================
    # server hall: four rack rows (E-W), broken by two cross aisles
    for zc in (34.0, 40.4, 46.8, 53.2):
        for (x0, x1) in ((-92, -78), (-72, -50), (-44, -24)):
            B(m, "serverrack", x0, 0, zc - 0.6, x1, 2.6, zc + 0.6)
    # receiving: pallets and a cage
    for (x0, z0, x1, z1) in ((-12, 48, -8, 52), (6, 36, 10, 40), (-4, 34, 0, 36)):
        B(m, "palletstack", x0, 0, z0, x1, 1.4, z1)
    # plant hall: chillers and a generator
    for (x0, z0, x1, z1, h) in ((28, 36, 36, 41, 3.0), (48, 34, 58, 39, 3.0), (66, 44, 76, 49, 3.0), (48, 50, 58, 55, 3.0), (84, 36, 92, 44, 3.4), (28, 50, 36, 55, 2.2)):
        B(m, "radiatorfins", x0, 0, z0, x1, h, z1)
    # mid: the MDF core (solid, long N-S: it splits the receiving -> lobby line), four meet-me cages (chain-link: they
    # stop players, not shots or sight) with cabinet rows inside, a cabinet block in each lane
    B(m, "electricalcabinet", -3, 0, 0, 7, 6, 16)
    for (x0, z0) in ((-21, -6), (-21, 14), (14, -6), (14, 14)):
        x1, z1 = x0 + 10, z0 + 8
        for (a0, b0, a1, b1) in ((x0, z0, x1, z0 + 0.1), (x0, z1 - 0.1, x1, z1), (x0, z0, x0 + 0.1, z1), (x1 - 0.1, z0, x1, z1)):
            B(m, "chainlinkmesh", a0, 0, b0, a1, 3.2, b1)
        B(m, "steel", x0, 3.2, z0, x1, 3.3, z1)
        for zz in (z0 + 2.2, z0 + 5.0): B(m, "serverrack", x0 + 1.5, 0, zz, x1 - 1.5, 2.4, zz + 1.0)
    for xc in (-6.5, 10.5): B(m, "electricalcabinet", xc - 1.5, 0, 7, xc + 1.5, 1.6, 9)
    # B: UPS cabinets round the site, battery strings
    for (x0, z0, x1, z1, h) in ((55, 6, 58, 11, 2.2), (63, 6, 66, 11, 2.2), (44, -2, 47, 4, 2.4), (74, 13, 77, 19, 2.4), (44, 15, 47, 19, 1.6), (74, -2, 77, 2, 1.6)):
        B(m, "electricalcabinet", x0, 0, z0, x1, h, z1)
    # battery room: rows of battery racks
    for xc in (48, 58, 68, 78):
        B(m, "lockerdoors", xc - 0.6, 0, -40, xc + 0.6, 2.4, -30); B(m, "lockerdoors", xc - 0.6, 0, -24, xc + 0.6, 2.4, -16)
    # security: desks
    B(m, "electricalcabinet", -84, 0, -30, -76, 1.1, -27); B(m, "electricalcabinet", -66, 0, -22, -58, 1.1, -19)
    # lobby: the reception desk
    B(m, "marble", -8, 0, -32, 8, 1.1, -29)
    # offices: drywall partitions (a dash breaks them), desks
    for (x0, z0, x1, z1) in ((-80, -46, -79.8, -26), (-50, -46, -49.8, -26), (-10, -46, -9.8, -36), (12, -46, 12.2, -36), (50, -46, 50.2, -26), (70, -46, 70.2, -26)):
        B(m, "drywall", x0, U, z0, x1, UC, z1)
    for (x0, z0) in ((-90, -40), (-70, -40), (-60, -32), (-24, -42), (20, -42), (60, -40), (80, -40), (88, -24)):
        B(m, "scaffoldboards", x0, U, z0, x0 + 3, U + 0.8, z0 + 1.6)
    # yard: two trucks backed up to the dock doors
    for xc in (-18, 18):
        B(m, "shippingcontainer", xc - 1.4, 0.6, 66, xc + 1.4, 4.0, 78); B(m, "steel", xc - 1.3, 0.6, 78, xc + 1.3, 3.2, 81.5)
    # lobby: columns
    for (x, z) in ((-12, -18), (6, -18), (-12, -38), (6, -38), (-30, -18), (28, -18)):
        B(m, "polishedgranite", x - 0.6, 0, z - 0.6, x + 0.6, C, z + 0.6)
    # courtyard: planters
    for (x0, x1) in ((-80, -70), (-36, -26), (26, 36), (70, 80)):
        B(m, "precastconcrete", x0, 0, -58, x1, 1.0, -54)

    # ================= decoration (walls, ceilings, edges: the lanes stay clear) =================
    # server hall: blue LED strips along the rack tops, cable trays over the aisles (grapple lines), light panels
    for zc in (34.0, 40.4, 46.8, 53.2):
        for (x0, x1) in ((-92, -78), (-72, -50), (-44, -24)):
            B(m, "glow", x0, 2.6, zc - 0.62, x1, 2.66, zc - 0.5); B(m, "glow", x0, 2.6, zc + 0.5, x1, 2.66, zc + 0.62)
    for zc in (30.7, 37.2, 43.6, 50.0):
        B(m, "steel", -92, 4.6, zc - 0.3, -24, 4.7, zc + 0.3)
        for x in range(-90, -23, 11): B(m, "steel", x - 0.04, 4.7, zc - 0.04, x + 0.04, C, zc + 0.04)
    for zc in (30.7, 43.6, 57.9):
        for x in range(-88, -23, 14): B(m, "lightblock", x, C - 0.1, zc - 0.4, x + 5, C, zc + 0.4)
    # the plenum: light strips and cable trays along its walls
    for z in range(22, 59, 9): B(m, "lightblock", PX0 + 2.5, -0.5, z, PX1 - 2.5, -0.4, z + 2)
    B(m, "steel", PX0, PY, 24, PX0 + 0.5, PY + 0.4, 54); B(m, "steel", PX1 - 0.5, PY, PZ0, PX1, PY + 0.4, PZ1)   # (not under the ladders)
    # A: glow channels in the floor running in to the plinth, a light line under the gallery's edge, steel brackets
    # under the gallery (grapple anchors)
    for k in range(8):
        ang = math.pi / 8 + k * math.pi / 4
        ux, uz = math.cos(ang), math.sin(ang); nx, nz = -uz * 0.15, ux * 0.15
        a0, a1 = (AX + ux * 3.2, AZ + uz * 3.2), (AX + ux * 17.2, AZ + uz * 17.2)
        prism(m, "glow", [(a0[0] - nx, a0[1] - nz), (a1[0] - nx, a1[1] - nz), (a1[0] + nx, a1[1] + nz), (a0[0] + nx, a0[1] + nz)], 0, 0.03)
    for k in range(8):
        a, b = G_IN[k], G_IN[(k + 1) % 8]
        c, d = [(p[0] + (AX - p[0]) * 0.012, p[1] + (AZ - p[1]) * 0.012) for p in (a, b)]
        prism(m, "glow", [a, b, d, c], C - 0.08, C)
        mx, mz = (OCT_IN[k][0] + OCT_IN[(k + 1) % 8][0]) / 2, (OCT_IN[k][1] + OCT_IN[(k + 1) % 8][1]) / 2
        if k not in (0, 4):
            ox, oz = (AX - mx) / A_IN, (AZ - mz) / A_IN
            prism(m, "steel", [(mx - oz * 0.4, mz + ox * 0.4), (mx + oz * 0.4, mz - ox * 0.4), (mx + oz * 0.4 + ox * 1.2, mz - ox * 0.4 + oz * 1.2), (mx - oz * 0.4 + ox * 1.2, mz + ox * 0.4 + oz * 1.2)], 6.4, C)
    # B: hazard lines round the site, light panels in its roof, the UPS units' status lights
    for (x0, z0, x1, z1) in ((52.5, 0.5, 68.5, 0.8), (52.5, 16.2, 68.5, 16.5), (52.5, 0.5, 52.8, 16.5), (68.2, 0.5, 68.5, 16.5)):
        B(m, "hazard", x0, 0, z0, x1, 0.02, z1)
    for x in (44, 60, 76):
        for z in (2, 15): B(m, "lightblock", x - 2, UC - 0.1, z - 1, x + 2, UC, z + 1)
    for (x0, z0, x1, z1) in ((55, 6, 58, 11), (63, 6, 66, 11)): B(m, "glow", x0 + 0.5, 1.6, z0 - 0.04, x1 - 0.5, 1.75, z0)
    # mid, lobby, offices, security, battery, NOC: light panels in the ceilings
    for x in range(-22, 34, 12):
        for z in (-6, 8, 22): B(m, "lightblock", x, C - 0.1, z - 0.5, x + 4, C, z + 0.5)
    for x in range(-90, 96, 14):
        for z in (-40, -28, -16):
            B(m, "lightblock", x, C - 0.1, z - 0.5, x + 4, C, z + 0.5)
            B(m, "lightblock", x, UC - 0.1, z - 0.5, x + 4, UC, z + 0.5)
    # NOC: the wall of screens, consoles behind the spawns
    B(m, "controlpanel", -28, 1.0, -96, 28, 5.4, -95.9)
    for z in (-93.0, -90.0): B(m, "electricalcabinet", -20, 0, z, 20, 0.8, z + 1)
    for x in range(-24, 25, 12): B(m, "lightblock", x - 2, 6.3, -86, x + 2, 6.4, -84)
    # the office windows over the roof get a frame; the parapets a coping strip and warning lights at the corners
    B(m, "steel", -96, U + 1.2, 61.8, 96, U + 1.3, 63.2)
    for (x, z) in ((-96, 62), (96, 62), (-46, 62), (46, 62)): B(m, "glow", x - 0.3, U + 1.3, z - 0.3, x + 0.3, U + 1.6, z + 0.3)
    # the roof: an antenna mast and a satellite dish over the west ring (out of the lanes)
    B(m, "steel", -92.5, U, -6.5, -91.5, U + 12, -5.5); B(m, "glow", -92.6, U + 12, -6.6, -91.4, U + 12.4, -5.4)
    prism(m, "galvanizedsteel", octagon(-84, -5, 1.6), U, U + 1.2); prism(m, "aluminiumtreadplate", octagon(-84, -5, 2.4), U + 2.4, U + 2.7)
    B(m, "steel", -84.2, U + 1.2, -5.2, -83.8, U + 2.4, -4.8)
    # plant hall: pipes along the walls and over the chillers
    for (x0, z0, x1, z1) in ((20, 60.5, 96, 61.4), (20, 28.6, 36, 29.5), (85, 28.6, 96, 29.5)):
        B(m, "copperandpatina", x0, 5.4, z0, x1, 6.2, z1)
    for x in (32, 53, 71): B(m, "galvanizedsteel", x - 0.4, 3.0, 38, x + 0.4, 6.2, 39) if x != 71 else B(m, "galvanizedsteel", x - 0.4, 3.0, 46, x + 0.4, 6.2, 47)
    # the yard: bay lines on the ground, dock bumpers beside the doors, floodlights on the yard walls
    for x in (-24, -12, 12, 24): B(m, "roadmarkings", x - 0.15, 0, 64, x + 0.15, 0.01, 86)
    for xd in (-32, 0, 32):
        for sx in (-1, 1): B(m, "rubber", xd + sx * 3.4 - 0.3, 0.4, 63, xd + sx * 3.4 + 0.3, 1.4, 63.4)
    for (x, z) in ((-46, 72), (-46, 86), (46, 72), (46, 86)):
        B(m, "glow", x - 0.3 if x < 0 else x - 0.1, 5.2, z - 0.6, x + 0.1 if x < 0 else x + 0.3, 5.6, z + 0.6); light(m, x * 0.95, 5, z, 16, 0.7)
    # the courtyard: lamp posts along its edges
    for x in (-70, -40, -10, 10, 40, 70):
        for z in (-48.5, -62.8):
            B(m, "steel", x - 0.15, 0, z - 0.15, x + 0.15, 4.5, z + 0.15); B(m, "glow", x - 0.4, 4.5, z - 0.4, x + 0.4, 4.8, z + 0.4)
    # the city at night round the site: dark towers beyond the walls (unlit scenery)
    for (x0, z0, x1, z1, h) in ((-150, -160, -120, -130, 60), (-100, -170, -60, -140, 44), (-40, -175, 0, -150, 80), (20, -165, 60, -135, 52),
                                (90, -170, 130, -130, 70), (150, -120, 180, -80, 40), (150, -20, 185, 20, 56), (150, 60, 180, 100, 36),
                                (-185, 40, -150, 80, 48), (-180, -60, -150, -20, 64), (-120, 130, -80, 160, 40), (-20, 140, 30, 175, 58), (80, 130, 120, 165, 46)):
        B(m, "far", x0, -2, z0, x1, h, z1)
    B(m, "far", -400, -7, -400, 400, -6, 400)

    # ================= spawns =================
    for x in (-9, -3, 3, 9): m.spawn("a", x, 0, 84, 0)
    for x in (-9, -3, 3, 9): m.spawn("b", x, 0, -86, 180)
    for (x, z, yaw) in ((-84, 58, -90), (-30, 32, 0), (60, 58, 90), (90, 0, 0), (-90, 0, 180), (-70, -20, 90), (70, -30, -90), (0, -54, 0)):
        m.spawn("any", x, 0, z, yaw)

    # ================= lights (layout pass: enough to see) =================
    for x in range(-88, 96, 16):
        for z in (36, 54): light(m, x, C - 0.6, z, 14, 0.8)
    for z in (0, 18): light(m, -86, C - 0.6, z, 14, 0.8); light(m, -33, C - 0.6, z, 14, 0.8); light(m, 90, C - 0.6, z, 12, 0.8)
    for x in range(-18, 36, 14):
        for z in (0, 18): light(m, x, C - 0.6, z, 14, 0.8)
    for x in range(-88, 96, 16):
        for z in (-36, -20): light(m, x, C - 0.6, z, 14, 0.8); light(m, x, UC - 0.6, z, 14, 0.7)
    light(m, AX, 15, AZ, 30, 0.9); light(m, AX, 4, AZ, 16, 0.7); light(m, AX, 10, AZ, 8, 1.0)
    for x in (45, 60, 76):
        for z in (0, 17): light(m, x, 12, z, 18, 0.8)
    for x in range(-24, 25, 16): light(m, x, 5.4, -80, 14, 0.8)
    for x in range(-40, 41, 20): light(m, x, 6, 76, 16, 0.7)
    for x in range(-90, 91, 20): light(m, x, 6, -56, 14, 0.7)
    for x in range(-88, 96, 16): light(m, x, U + 4, 44, 16, 0.5)

    # ================= bounds =================
    # the map's own outline, clipped all round, a lid at 45 m, and the tall roofs (offices, B, A, the NOC) clipped
    # so nobody stands over the courtyard or the sites' roofs
    for (a, b, c, d) in ((-99, -65, -96, 63), (96, -65, 99, 63), (-99, 90, 99, 93), (-99, 63, -46, 90), (46, 63, 99, 90),
                         (-99, -99, -32, -65), (32, -99, 99, -65), (-32, -99, 32, -96)):
        B(m, "clip", a, -4, b, c, 45, d)
    B(m, "clip", -99, 45, -99, 99, 46, 93)
    B(m, "clip", -96, RT, -46, 96, 45, -10); B(m, "clip", 36, RT, -10, 85, 45, 28)
    B(m, "clip", -77, A_H + 0.6, -10, -39, 45, 28); B(m, "clip", -32, 6.8, -96, 32, 45, -64)

    m.raw("palette base 0.5 0.5 0.52 accent 0.2 0.3 0.5 metal 0.3 0.32 0.36 glow 0.4 0.8 1 sky 0.03 0.04 0.08")
    m.raw("setting hyperscale data centre at night")
    m.save("datacenter", dress=False)
