"""RAILYARD (1.19.6): neutral payload in a freight yard, 140 x 260 m. The main line runs the yard's length (along z)
with an S-bend near each end; the cart starts in the middle and each team pushes it about 107 m into the other's
building: the roundhouse at the south end (team A's home: the track's start) and the goods shed at the north end (team
B's home: its end). Three sidings each side carry parked trains (boxcars, tank cars, flatcars with containers): lanes
along the yard with gaps every 15-25 m to cross, wagon roofs at 4 m the mid level (end ladders). Two gantry cranes span
every track (deck at 15 m: the high ground, leg ladders), container stacks 1-3 high outside the roads are a maze layer
with stairs up, and a signal box at each quarter mark looks down the yard.
Built point-symmetric through the yard's centre (the two ends dressed differently), then shifted 1 m along x so the
tracks sit on the rail-track look's 2 m grid (its rails are drawn on odd x)."""
import math
from .prefabs import M, minus, tiles
from .bigkit import Kit, num, MAT

HX, HZ = 62, 130          # the yard (local coordinates, before the 1 m shift)
WALL_H = 7.0
SIDINGS = (6, 12, 18)     # x of the sidings on the +x side (mirrored to -x)


class Sym:
    """draws each piece and its point-mirrored twin ((x, z) -> (-x, -z)); once=True draws only the piece"""
    def __init__(s, k): s.k = k
    def box(s, mat, x0, y0, z0, x1, y1, z1, once=False):
        s.k.box(mat, x0, y0, z0, x1, y1, z1)
        if not once: s.k.box(mat, -x1, y0, -z1, -x0, y1, -z0)
    def prism(s, mat, poly, y0, y1, once=False):
        s.k.prism(mat, poly, y0, y1)
        if not once: s.k.prism(mat, [(-x, -z) for (x, z) in poly], y0, y1)
    def bar(s, mat, a, b, w, y0, y1, once=False):
        s.k.bar(mat, a, b, w, y0, y1)
        if not once: s.k.bar(mat, (-a[0], -a[1]), (-b[0], -b[1]), w, y0, y1)
    def tube_z(s, mat, z0, z1, cx, cy, r, once=False):
        s.k.tube_z(mat, z0, z1, cx, cy, r)
        if not once: s.k.tube_z(mat, -z1, -z0, -cx, cy, r)
    def ladder(s, x, z, y0, y1, face, width=0.9, once=False):
        s.k.ladder(x, z, y0, y1, face, width)
        if not once: s.k.ladder(-x, -z, y0, y1, {"n": "s", "s": "n", "e": "w", "w": "e"}[face], width)
    def stairs(s, mat, x0, z0, x1, z1, top, axis, rising, base=0.0, solid=True, once=False):
        s.k.stairs(mat, x0, z0, x1, z1, top, axis, rising, base, solid)
        if not once: s.k.stairs(mat, -x1, -z1, -x0, -z0, top, axis, -rising, base, solid)
    def light(s, x, y, z, r=12, i=0.8, once=False):
        s.k.light(x, y, z, r, i)
        if not once: s.k.light(-x, y, -z, r, i)
    def spawn(s, team, x, y, z, yaw, once=False):
        s.k.spawn(team, x, y, z, yaw)
        if not once: s.k.spawn({"a": "b", "b": "a"}.get(team, team), -x, y, -z, (yaw + 360) % 360 - 180)


def curve(a, b, n=8):
    """an S-bend from a to b along z (x eased with smoothstep): n + 1 points"""
    (ax, az), (bx, bz) = a, b
    out = []
    for i in range(n + 1):
        t = i / n; e = t * t * (3 - 2 * t)
        out.append((round(ax + (bx - ax) * e, 3), round(az + (bz - az) * t, 3)))
    return out


def offset(pts, d):
    """a polyline moved sideways by d"""
    out = []
    for i, (x, z) in enumerate(pts):
        (ax, az) = pts[max(0, i - 1)]; (bx, bz) = pts[min(len(pts) - 1, i + 1)]
        tx, tz = bx - ax, bz - az; L = math.hypot(tx, tz)
        out.append((x - tz / L * d, z + tx / L * d))
    return out


def build():
    m = M("Railyard", "npayload", sun=(0.55, 0.42, -0.45))
    m.also = ["payload"]
    k = Kit(m); S = Sym(k)

    # ---- the main line: start in the roundhouse (south, A's home), S-bends, the long straight, the goods shed (north)
    bend = curve((-12, -84), (0, -62))
    path = [(-12, -104)] + bend + [(-x, -z) for (x, z) in reversed(bend)] + [(12, 104)]
    for (x, z) in path: m.raw(f"path {num(x)} 0 {num(z)}")

    # ---- floor: rail-track strips (2 m, flush), ballast at the bends, the turntable pits, asphalt roads, concrete
    # building floors, dirt over the rest of the yard
    strips = [(-1, -62, 1, 62), (-13, -106, -11, -84), (11, 84, 13, 106)]
    for sx in SIDINGS:   # the sidings: +x ones run from z -90 to 56, the -x ones mirrored
        strips += [(sx - 1, -90, sx + 1, 56), (-sx - 1, -56, -sx + 1, 90)]
    strips += [(-1, -127.6, 1, -100.4), (-25, -127.6, -23, -100.4)]   # (the roundhouse's two other stalls)
    bends = [(-14, -84, 2, -62), (-2, 62, 14, 84)]
    pits = [(-20, -99, -4, -83), (4, 83, 20, 99)]
    roads = [(22, -100, 30, 100), (-30, -100, -22, 100)]
    halls = [(-34, -128, 14, -100), (-14, 100, 34, 128)]
    yard = minus((-HX, -HZ, HX, HZ), strips + bends + pits + roads + halls)
    for (x0, z0, x1, z1) in strips:
        for (a, b, c, d) in minus((x0, z0, x1, z1), pits + halls): k.box("railtrack", a, -1, b, c, 0, d)
    for (x0, z0, x1, z1) in tiles(yard, 32):
        inner = max(abs(x0), abs(x1)) <= 22
        k.box("dirt" if inner else "concrete", x0, -1, z0, x1, 0, z1)
    for r in roads:
        for t in tiles([r], 40): k.box("asphalt", t[0], -1, t[1], t[2], 0, t[3])
    for h in halls:   # (the track runs on into each hall: its strip cut out of the hall's floor)
        for (x0, z0, x1, z1) in minus(h, strips): k.box("polishedconcrete", x0, -1, z0, x1, 0, z1)
        for (x0, z0, x1, z1) in strips:
            for (a, b, c, d) in minus(h, [(-1e3, -1e3, x0, 1e3), (x1, -1e3, 1e3, 1e3), (-1e3, -1e3, 1e3, z0), (-1e3, z1, 1e3, 1e3)]):
                k.box("railtrack", a, -1, b, c, 0, d)
    # the bends: ballast 5 cm down, the rails on it flush with the yard (the cart rides on them, as on the strips)
    for (x0, z0, x1, z1) in bends: k.box("gravel", x0, -1, z0, x1, -0.05, z1)
    for side in (-0.72, 0.72):
        rail = offset(bend, side)
        for a, b in zip(rail, rail[1:]): S.bar("steel", a, b, 0.09, -0.05, 0)
    # the turntable pits (in front of each end building): an octagonal pit 0.5 m deep, the track across it on a bridge
    def pit(cx, cz, r=8.0):
        c = r * math.tan(math.pi / 8)
        k.box("concrete", cx - r, -1.5, cz - r, cx + r, -0.5, cz + r)                    # the pit floor
        for (sx, sz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):                           # the corners outside the octagon
            k.prism("concrete", [(cx + sx * r, cz + sz * c), (cx + sx * r, cz + sz * r), (cx + sx * c, cz + sz * r)], -1, 0)
        k.box("bridgegirder", cx - 1.6, -0.5, cz - r, cx - 1.0, 0, cz + r)               # the bridge: girders and the track
        k.box("bridgegirder", cx + 1.0, -0.5, cz - r, cx + 1.6, 0, cz + r)
        k.box("railtrack", cx - 1.0, -0.5, cz - r, cx + 1.0, 0, cz + r)
    pit(-12, -91); pit(12, 91)

    # ---- perimeter: brick walls, clip above them
    k.box("brick", -HX - 2, -1, -HZ - 2, -HX, WALL_H, HZ + 2); k.box("brick", HX, -1, -HZ - 2, HX + 2, WALL_H, HZ + 2)
    k.box("brick", -HX, -1, -HZ - 2, HX, WALL_H, -HZ); k.box("brick", -HX, -1, HZ, HX, WALL_H, HZ + 2)
    m.clip_walls(-HX, -HZ, HX, HZ, WALL_H)

    # ---- the parked trains on the sidings (the +x side; Sym mirrors them onto the -x sidings)
    def boxcar(x, z0, z1, ladder_end=-1):
        S.box("greasestainedmetal", x - 1.2, 0, z0 + 0.6, x + 1.2, 1.0, z1 - 0.6)     # frame and bogies
        S.box("corrugated", x - 1.5, 1.0, z0, x + 1.5, 4.0, z1)                         # the body; its roof is the mid level
        if ladder_end < 0: S.ladder(x, z0, 0, 4.0, "s", width=0.6)
        elif ladder_end > 0: S.ladder(x, z1, 0, 4.0, "n", width=0.6)
    def tankcar(x, z0, z1, ladder_end=-1):
        S.box("greasestainedmetal", x - 1.3, 0, z0, x + 1.3, 1.1, z1)
        S.tube_z("chrome", z0 + 0.4, z1 - 0.4, x, 2.55, 1.45)                          # the tank (top at 4 m)
    def flatcar(x, z0, z1, load=True):
        S.box("rivetedplates", x - 1.4, 0, z0, x + 1.4, 1.1, z1)                        # the deck: one jump up
        if load: S.box("shippingcontainer", x - 1.25, 1.1, z0 + 0.6, x + 1.25, 3.7, z1 - 0.6)
    def train(x, z, cars):
        for (kind, length, gap) in cars:
            if kind == "box": boxcar(x, z, z + length)
            elif kind == "boxn": boxcar(x, z, z + length, 1)
            elif kind == "tank": tankcar(x, z, z + length)
            elif kind == "flat": flatcar(x, z, z + length)
            elif kind == "empty": flatcar(x, z, z + length, load=False)
            z += length + gap
    train(6, -86, [("box", 14, 3), ("tank", 12, 3), ("boxn", 14, 0)])
    train(6, -10, [("flat", 14, 3), ("box", 14, 3), ("tank", 12, 0)])
    train(12, -60, [("tank", 12, 3), ("tank", 12, 3), ("empty", 12, 0)])
    train(12, 14, [("box", 14, 3), ("flat", 14, 0)])
    train(18, -90, [("flat", 14, 3), ("boxn", 14, 0)])
    train(18, -20, [("box", 14, 4), ("box", 14, 3), ("empty", 12, 0)])
    for sx in SIDINGS:   # buffer stops at the sidings' ends
        S.box("powdercoatyellow", sx - 1.2, 0, 55.4, sx + 1.2, 1.2, 56.2)
        S.box("powdercoatyellow", sx - 1.2, 0, -90.2, sx + 1.2, 1.2, -89.4)

    # ---- the gantry cranes (z -40 and +40): legs outside the tracks, the deck at 15 m, a trolley with a hanging container
    def crane(cz, tx):
        for lx in (-27, 27):
            for dz in (-4.0, 3.0):
                k.box("powdercoatyellow", lx - 0.5, 0, cz + dz, lx + 0.5, 14.4, cz + dz + 1.0)
            k.box("powdercoatyellow", lx - 0.5, 12.6, cz - 3.0, lx + 0.5, 14.4, cz + 3.0)    # the leg frame's head
        k.box("bridgegirder", -27.5, 14.4, cz - 4.5, 27.5, 15.0, cz + 4.5)                    # the deck
        k.box("powdercoatyellow", tx - 3, 15.0, cz - 2.5, tx + 3, 17.4, cz + 2.5)             # the trolley
        for dx in (-1.0, 1.0):
            k.box("steel", tx + dx - 0.07, 11.0, cz - 0.07, tx + dx + 0.07, 14.4, cz + 0.07)   # cables
        k.box("shippingcontainer", tx - 6.1, 8.4, cz - 1.25, tx + 6.1, 11.0, cz + 1.25)       # (a perch)
        # leg ladders: up the outer face of a leg each side
        k.ladder(-27.5, cz - 3.5, 0, 15.0, "e", width=0.8)
        k.ladder(27.5, cz + 3.5, 0, 15.0, "w", width=0.8)
    crane(-40, -9); crane(40, 9)

    # ---- container terminal blocks (x 34..57, z -6..38, and the mirror): stacks 1-3 high, alleys 4 m; stairs up
    cols = [(33, 38), (42, 47), (51, 56)]
    rows = [(-6, 6.2), (10.2, 22.4), (26.4, 38.6)]
    tiers = [[1, 2, 1], [2, 3, 2], [3, 1, 3]]   # [column][row]
    looks = ["shippingcontainer", "rust", "shippingcontainer"]
    for ci, (x0, x1) in enumerate(cols):
        for ri, (z0, z1) in enumerate(rows):
            for t in range(tiers[ci][ri]):
                for (a, b) in ((x0, x0 + 2.5), (x0 + 2.5, x1)):
                    S.box(looks[(ci + ri + t) % 3], a, 2.6 * t, z0, b, 2.6 * (t + 1), z1)
    S.stairs("galvanizedgrating", 33, -8.2, 38, -6, 2.6, "x", 1)                           # ground -> the first stack (2.6)
    S.stairs("galvanizedgrating", 38, -1.5, 42, 1.0, 5.2, "x", 1, base=2.6, solid=False)   # over the alley -> 5.2
    S.stairs("galvanizedgrating", 43, 6.2, 45.5, 10.2, 7.8, "z", 1, base=5.2, solid=False)  # over the alley -> 7.8

    # ---- the outer belts (x 31..62 and the mirror), south to north: the fuel depot, the repair shed, the containers
    # (above), the signal box (the mirrored one), the transit shed
    # fuel depot: two tanks with a catwalk between their tops (a perch: ladder up one), the pump island on the road
    # with the oil spilt round it (no grip: fast through, no stopping)
    for tx in (42, 54):
        for (cx, cz) in ((tx, -90), (-tx, 90)):
            k.disc("whitewashedbrick", cx, cz, 4.2, 0, 1.0, n=8)
            k.disc("galvanizedsteel", cx, cz, 3.6, 1.0, 10.0, n=8)
    S.box("galvanizedgrating", 45.2, 9.7, -91, 50.8, 10.0, -89)
    S.box("galvanizedsteel", 37.6, 0, -91, 38.6, 10.0, -89)                                # the ladder's post
    S.ladder(37.6, -90, 0, 10.0, "e", width=0.8)
    S.box("galvanizedgrating", 37.6, 9.7, -91, 38.4, 10.0, -89)
    S.box("powdercoatgreen", 33.5, 0, -90, 35.5, 1.4, -86)                                  # the pump island
    S.box("oilslick", 31, 0, -92, 39, 0.05, -84)
    # the repair shed (x 36..58, z -70..-40): doors on the road and at both ends, a wagon on blocks inside, a
    # mezzanine along the back wall (stairs up) with a door onto the roof of the next block
    W2 = "trapezoidcladding"
    for (z0, z1) in ((-70, -60), (-50, -40)):
        S.box(W2, 36, 0, z0, 36.4, 8, z1)                                                    # road side, a 10 m door
    S.box(W2, 36, 5, -60, 36.4, 8, -50)
    S.box(W2, 57.6, 0, -70, 58, 8, -40)
    for zz in (-70.4, -40):
        S.box(W2, 36, 0, zz, 41, 8, zz + 0.4); S.box(W2, 49, 0, zz, 58, 8, zz + 0.4); S.box(W2, 41, 5, zz, 49, 8, zz + 0.4)
    S.box("corrugated", 35.6, 8, -70.8, 58.4, 8.4, -39.6)
    S.box("greasestainedmetal", 44, 0, -63, 47, 1.0, -47)                                  # the wagon on its blocks
    S.box("rust", 43.7, 1.0, -63.5, 47.3, 4.0, -46.5)
    S.box("steeldeckplate", 53, 3.7, -70, 57.6, 4.0, -40)                                  # the mezzanine
    S.stairs("steeldeckplate", 49.4, -48, 53, -44, 4.0, "x", 1)
    for (lx, lz) in ((46, -64), (46, -46)): S.light(lx, 7, lz, 12, 0.9)
    # the transit shed (x 42..60, z 68..96): a roof on posts over pallet stacks, open on every side
    for px in (42, 59.4):
        for pz in (68, 77, 86, 95.4):
            S.box("galvanizedsteel", px, 0, pz, px + 0.6, 6, pz + 0.6)
    S.box("corrugated", 41.5, 6, 67.5, 60.5, 6.4, 96.5)
    for (a, b, c, d, h) in ((45, 71, 49, 75, 1.6), (52, 72, 56, 74, 2.6), (46, 80, 48, 86, 2.6), (52, 82, 57, 86, 1.6), (45, 90, 50, 93, 2.6)):
        S.box("palletstack", a, 0, b, c, h, d)

    # ---- signal boxes at the quarter marks (x -40..-32, z -62..-54 and the mirror): two storeys, the upper one open
    # all round over the yard, stairs inside
    def signalbox(x0, z0):
        x1, z1 = x0 + 8, z0 + 8
        W = "redengineeringbrick"
        S.box(W, x0, 0, z0, x1, 4.0, z0 + 0.4); S.box(W, x0, 0, z1 - 0.4, x1, 4.0, z1)
        S.box(W, x0, 0, z0 + 0.4, x0 + 0.4, 4.0, z1 - 0.4)
        S.box(W, x1 - 0.4, 0, z0 + 0.4, x1, 4.0, z0 + 2.4)                  # the doorway on the yard side (z0+2.4..+5)
        S.box(W, x1 - 0.4, 2.6, z0 + 2.4, x1, 4.0, z0 + 5.0)
        S.box(W, x1 - 0.4, 0, z0 + 5.0, x1, 4.0, z1 - 0.4)
        # the upper floor (4.0), the stairwell open over the flight along the back wall
        S.box("wideplanks", x0 + 0.4, 3.7, z0 + 0.4, x1 - 0.4, 4.0, z1 - 2.0)
        S.box("wideplanks", x0 + 0.4, 3.7, z1 - 2.0, x0 + 1.6, 4.0, z1 - 0.4)
        S.box("wideplanks", x1 - 1.8, 3.7, z1 - 2.0, x1 - 0.4, 4.0, z1 - 0.4)        # (over the foot of the flight)
        S.stairs("wideplanks", x0 + 1.6, z1 - 2.0, x1 - 1.8, z1 - 0.4, 4.0, "x", -1)
        # upstairs: a waist-high wall all round, posts at the corners, the roof at 7
        S.box(W, x0, 4.0, z0, x1, 5.0, z0 + 0.4); S.box(W, x0, 4.0, z1 - 0.4, x1, 5.0, z1)
        S.box(W, x0, 4.0, z0 + 0.4, x0 + 0.4, 5.0, z1 - 0.4); S.box(W, x1 - 0.4, 4.0, z0 + 0.4, x1, 5.0, z1 - 0.4)
        for (px, pz) in ((x0, z0), (x1 - 0.4, z0), (x0, z1 - 0.4), (x1 - 0.4, z1 - 0.4)):
            S.box("charredtimber", px, 5.0, pz, px + 0.4, 7.0, pz + 0.4)
        S.box("roofshingles", x0 - 0.5, 7.0, z0 - 0.5, x1 + 0.5, 7.4, z1 + 0.5)
        S.light((x0 + x1) / 2, 3.0, (z0 + z1) / 2, 7, 0.8)
    signalbox(-40, -62)

    # ---- the end buildings: the roundhouse (south, x -34..14, z -128..-100) and the goods shed (north, mirrored):
    # the same shell, so both teams' homes play alike: three doors in the front, one each side, the spawn room at the
    # back behind a wall with three openings (no sightline down the track to the spawns)
    def hall(wallmat, roofmat, sgn):
        x0, x1, H = -34, 14, 10.0
        def B(mat, a, y0, b, c, y1, d):
            if sgn > 0: S.box(mat, a, y0, b, c, y1, d, once=True)
            else: S.box(mat, -c, y0, -d, -a, y1, -b, once=True)
        xs = x0
        for (a, b, h) in ((-30, -25, 4.0), (-17, -7, 7.0), (3, 8, 4.0)):   # the front: two 5 m doors, the 10 m track door
            B(wallmat, xs, 0, -100.4, a, H, -100); B(wallmat, a, h, -100.4, b, H, -100); xs = b
        B(wallmat, xs, 0, -100.4, x1, H, -100)
        B(wallmat, x0, 0, -128, x1, H, -127.6)
        for (wx0, wx1) in ((x0, x0 + 0.4), (x1 - 0.4, x1)):                 # side walls: a 5 m door each
            B(wallmat, wx0, 0, -127.6, wx1, H, -116); B(wallmat, wx0, 4.0, -116, wx1, H, -111); B(wallmat, wx0, 0, -111, wx1, H, -100.4)
        B(roofmat, x0 - 0.5, H, -128.5, x1 + 0.5, H + 0.5, -99.5)
        xs = x0 + 0.4
        for (a, b) in ((-31, -27), (-14, -10), (8, 12)):                    # the spawn room's wall
            B(wallmat, xs, 0, -120.4, a, H, -120); B(wallmat, a, 3.0, -120.4, b, H, -120); xs = b
        B(wallmat, xs, 0, -120.4, x1 - 0.4, H, -120)
        for lx in (-24, -4, 8):
            for lz in (-124, -110):
                S.light(lx * sgn, H - 1.5, lz * sgn, 14, 0.9, once=True)
    hall("londonstock", "standingseamroof", 1)
    hall("trapezoidcladding", "corrugated", -1)
    # inside the roundhouse: a locomotive on each of the two other stalls (big cover either side of the main line)
    def loco(x, z0, z1, mat):
        k.box("sootysteel", x - 1.3, 0, z0, x + 1.3, 1.2, z1)
        k.tube_z(mat, z0 + 4, z1 - 0.5, x, 2.6, 1.4)
        k.box(mat, x - 1.5, 1.2, z0, x + 1.5, 4.4, z0 + 4)     # the cab
    loco(0, -118, -104, "boilerplate"); loco(-24, -118, -104, "powdercoatgreen")
    # inside the goods shed: a loading platform beside the track and a stack of crates (the same cover, other shapes)
    k.box("precastconcrete", -4, 0, 104, 3, 1.1, 118)
    k.stairs("precastconcrete", -4, 102, 3, 104, 1.1, "z", 1)
    k.box("precastconcrete", 21.6, 0, 104, 26.4, 4.4, 118)
    k.box("cratewood", -2.5, 1.1, 108, 0.5, 3.6, 112)

    # ---- decoration (off the lanes: on the wagons' sides and roofs, the walls, above head height, past the walls)
    decorate(k, S, bend)

    # ---- spawns: four a side in the spawn rooms; free for all over the yard
    for x in (-28, -20, 2, 6):
        S.spawn("a", x, 0, -124, 180)
    for (x, z) in ((-9, -30), (27, 10), (-26, 60), (45, -20)):
        S.spawn("any", x, 0, z, round(math.degrees(math.atan2(x, z))))   # (toward the middle)

    m.raw("setting freight rail yard")
    m.raw("cartcheck 0.33"); m.raw("cartcheck 0.66")   # (one-way payload: checkpoints; neutral payload ignores them)
    shift_x(m, 1.0)
    m.save("railyard", dress=False)


def shift_x(m, dx):
    """everything moved dx along x (cut planes are box fractions: unchanged)"""
    boxlike = set(MAT) | {"site", "hill", "point", "checkpoint", "ladder"}
    out = []
    for l in m.lines:
        w = l.split()
        if not w or w[0].startswith("#"): out.append(l); continue
        kw, idx = w[0], []
        if kw in boxlike and len(w) >= 7: idx = [1, 4]
        elif kw in ("wedge", "corner", "tetra"): idx = [3, 6]
        elif kw == "cut": n = int(w[2]); idx = [3 + 4 * n, 6 + 4 * n]
        elif kw == "spawn": idx = [2]
        elif kw in ("light", "path"): idx = [1]
        elif kw == "stairs": idx = [1, 3]
        for i in idx: w[i] = num(float(w[i]) + dx)
        out.append(" ".join(w) if idx else l)
    m.lines = out


def decorate(k, S, bend):
    # wagons: wheels on the frames' sides, a plank walkway on each boxcar roof, the sliding doors, the tank cars' domes
    def wheels(x, z0, z1, w=1.2):
        for zc in (z0 + 1.6, z0 + 3.2, z1 - 3.2, z1 - 1.6):
            for sx in (-1, 1):
                a = x + w if sx > 0 else x - w - 0.1
                S.box("sootysteel", a, 0, zc - 0.45, a + 0.1, 0.9, zc + 0.45)
    cars = []   # (kind, x, z0, z1): the same trains as build() (kept in step by hand)
    def train(x, z, spec):
        for (kind, length, gap) in spec:
            cars.append((kind, x, z, z + length)); z += length + gap
    train(6, -86, [("box", 14, 3), ("tank", 12, 3), ("box", 14, 0)])
    train(6, -10, [("flat", 14, 3), ("box", 14, 3), ("tank", 12, 0)])
    train(12, -60, [("tank", 12, 3), ("tank", 12, 3), ("empty", 12, 0)])
    train(12, 14, [("box", 14, 3), ("flat", 14, 0)])
    train(18, -90, [("flat", 14, 3), ("box", 14, 0)])
    train(18, -20, [("box", 14, 4), ("box", 14, 3), ("empty", 12, 0)])
    for (kind, x, z0, z1) in cars:
        if kind == "box":
            wheels(x, z0, z1)
            S.box("scaffoldboards", x - 0.4, 4.0, z0 + 0.3, x + 0.4, 4.06, z1 - 0.3)                  # roof walk
            zm = (z0 + z1) / 2
            for sx in (-1, 1):                                                                         # sliding doors
                a = x + 1.5 if sx > 0 else x - 1.56
                S.box("rustedsheetfence", a, 1.2, zm - 1.6, a + 0.06, 3.7, zm + 1.6)
        elif kind == "tank":
            wheels(x, z0, z1, 1.3)
            zm = (z0 + z1) / 2
            S.k.disc("chrome", x, zm, 0.7, 3.9, 4.5, n=8); S.k.disc("chrome", -x, -zm, 0.7, 3.9, 4.5, n=8)
            for zz in (z0 + 0.4, z1 - 0.8):   # the end platforms' handrails (above the frame, off the gaps)
                S.box("powdercoatyellow", x - 1.3, 1.1, zz, x - 1.2, 2.1, zz + 0.4); S.box("powdercoatyellow", x + 1.2, 1.1, zz, x + 1.3, 2.1, zz + 0.4)
        elif kind in ("flat", "empty"):
            if kind == "empty":   # stakes along the deck's edges
                for zz in range(int(z0) + 1, int(z1), 3):
                    for sx in (-1, 1): S.box("charredtimber", x + sx * 1.3 - 0.06, 1.1, zz - 0.06, x + sx * 1.3 + 0.06, 1.7, zz + 0.06)
    # oil spilt beside the tank cars (no grip: fast through, no stopping) in the alleys
    S.box("oilslick", 13.7, 0, -58, 16.3, 0.05, -51)
    S.box("oilslick", 7.7, 0, -66, 10.3, 0.05, -60)
    # sleepers under the rails at the bends
    from math import hypot
    for i in range(len(bend) - 1):
        (ax, az), (bx, bz) = bend[i], bend[i + 1]
        L = hypot(bx - ax, bz - az); n = max(1, int(L / 1.0))
        tx, tz = (bx - ax) / L, (bz - az) / L
        for j in range(n):
            f = (j + 0.5) / n; cx, cz = ax + (bx - ax) * f, az + (bz - az) * f
            S.bar("railwaysleepers", (cx - tz * 1.25, cz + tx * 1.25), (cx + tz * 1.25, cz - tx * 1.25), 0.26, -0.05, -0.02)
    # floodlight masts (sodium lamps over the yard; their heads are grapple anchors): at the roads' outer edges
    for (mx, mz) in ((31.5, -70), (31.5, 20), (-31.5, -20)):
        S.box("galvanizedsteel", mx - 0.35, 0, mz - 0.35, mx + 0.35, 22, mz + 0.35)
        S.box("galvanizedsteel", mx - 0.5, 0, mz - 0.5, mx + 0.5, 0.6, mz + 0.5)
        S.box("sootysteel", mx - 2.4, 22, mz - 0.5, mx + 2.4, 23.2, mz + 0.5)                       # the lamp bar
        S.box("glow", mx - 2.2, 21.9, mz - 0.45, mx + 2.2, 22.0, mz + 0.45)
        S.light(mx - (3 if mx > 0 else -3), 20, mz, 30, 0.7)
    # the cranes: diagonal bracing in the leg frames (outside the tracks), the operator's cab under the deck by a leg
    for cz in (-40, 40):
        for lx in (-27, 27):
            k.bar("powdercoatyellow", (lx, cz - 3.5), (lx, cz + 3.5), 0.5, 6.0, 6.6)
        cx = 22 if cz < 0 else -22
        k.box("insulatedpanel", cx - 2, 11.4, cz - 2, cx + 2, 14.4, cz + 2)
        k.box("bluetintedglass", cx - 2.05, 12.6, cz - 1.6, cx + 2.05, 13.8, cz + 1.6)
        k.light(cx, 13, cz, 6, 0.6)
    # the halls: roof lights, a hazard band over the track door
    for sgn in (1, -1):
        for xs in (-26, -12, 2):
            a, b = sorted((xs * sgn - 2, xs * sgn + 2)); c, d = sorted((-124 * sgn, -104 * sgn))
            k.box("glow", a, 9.88, c, b, 10.0, d)
    S.box("hazard", -17, 7.0, -100.0, -7, 7.4, -99.8)                                                # the track door's lintel band
    # beyond the walls: the town round the yard (unlit far blocks: no light cost, no collision with play)
    import random
    R = random.Random("railyard-skyline")
    for side in range(4):
        for i in range(12):
            w, d, h = R.uniform(14, 30), R.uniform(10, 22), R.uniform(10, 34)
            if side < 2:
                z = -120 + i * 22 + R.uniform(-3, 3); x = (66 + d / 2 + R.uniform(4, 20))
                xa, xb, za, zb = x - d / 2, x + d / 2, z - w / 2, z + w / 2
                if side == 1: xa, xb = -xb, -xa
            else:
                x = -55 + i * 10 + R.uniform(-2, 2); z = 134 + d / 2 + R.uniform(4, 20)
                xa, xb, za, zb = x - w / 2, x + w / 2, z - d / 2, z + d / 2
                if side == 3: za, zb = -zb, -za
            k.box("far", xa, -1, za, xb, h, zb)
