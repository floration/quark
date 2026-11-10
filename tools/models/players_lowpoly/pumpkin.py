"""PUMPKIN: the pumpkin head (box design: characters_outlined_extra2.pumpkin)."""
import math

from players_lowpoly.kit import Char, Rig, body, band, plate, ring, hexring, ngon, rotz, roty, FRONT

DESC = "the pumpkin head: a big ribbed pumpkin with a candle-lit carved face and a green stem, a ragged dark tailcoat over a team waistcoat, team trousers"


# ---- helpers of this module (the kit has no bent tube) ---------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a):
    L = math.sqrt(_dot(a, a)) or 1.0
    return _mul(a, 1.0 / L)


def tube(ch, bone, t, pts, radii, k=6, shell=True, phase=0.0):
    """a bent k-sided tube through 3D points (radii per point): a hat point, a tail, an antenna"""
    m, mat = ch._m(bone, t)
    n = len(pts)
    T = [_norm(_sub(pts[min(i + 1, n - 1)], pts[max(i - 1, 0)])) for i in range(n)]

    def make(mt, grow, inward):
        ref = (0.0, 0.0, 1.0) if abs(T[0][2]) < 0.9 else (1.0, 0.0, 0.0)
        N = _norm(_sub(ref, _mul(T[0], _dot(ref, T[0]))))
        rings = []
        for i in range(n):
            if i:
                N = _norm(_sub(N, _mul(T[i], _dot(N, T[i]))))
            B = _cross(T[i], N)
            p = pts[i]
            if i == 0:
                p = _sub(p, _mul(T[0], grow))
            if i == n - 1:
                p = _add(p, _mul(T[-1], grow))
            r = radii[i] + grow
            rings.append([_add(p, _add(_mul(N, r * math.cos(2 * math.pi * j / k + phase)), _mul(B, r * math.sin(2 * math.pi * j / k + phase)))) for j in range(k)])

        def tri(a, b, c, out):
            nn = _cross(_sub(b, a), _sub(c, a))
            if (_dot(nn, out) < 0) != inward:
                b, c = c, b
            m.add(mt, a, b, c)

        for i in range(n - 1):
            mid = _mul(_add(pts[i], pts[i + 1]), 0.5)
            for j in range(k):
                a, b, c, d = rings[i][j], rings[i][(j + 1) % k], rings[i + 1][(j + 1) % k], rings[i + 1][j]
                out = _sub(_mul(_add(_add(a, b), _add(c, d)), 0.25), mid)
                tri(a, b, c, out)
                tri(a, c, d, out)
        for rg, out in ((rings[0], _mul(T[0], -1)), (rings[-1], T[-1])):
            for j in range(1, k - 1):
                tri(rg[0], rg[j], rg[j + 1], out)

    make(mat, 0.0, False)
    if shell:
        make("ink", ch.outline, True)


# ---- the pumpkin ------------------------------------------------------------------------------------------------------

def ribbed(R, k=10, groove=0.82):
    """a pumpkin's ring (x, z): k corners, ribs (R) and grooves between, a rib at the front"""
    return [((R if i % 2 == 0 else R * groove) * math.sin(2 * math.pi * i / k), -(R if i % 2 == 0 else R * groove) * math.cos(2 * math.pi * i / k)) for i in range(k)]


def facet(R, i, k=10, groove=0.82):
    """facet i (between corners i and i + 1) of ribbed(R): (its normal's angle from -z toward +x, distance, the
    tangential offset of its middle)"""
    p = ribbed(R, k, groove)
    a, b = p[i % k], p[(i + 1) % k]
    tx, tz = b[0] - a[0], b[1] - a[1]
    L = math.hypot(tx, tz)
    tx, tz = tx / L, tz / L
    nx, nz = tz, -tx                       # (the outward side of a CCW-from-above front facet)
    mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    if nx * mx + nz * mz < 0:
        nx, nz = -nx, -nz
    d = nx * mx + nz * mz
    phi = math.atan2(nx, -nz)
    # the facet's middle in the turned frame: rotate it by -phi about y
    c, s = math.cos(phi), math.sin(phi)
    u = c * mx + s * mz
    return phi, d, u


def carve(ch, t, poly_xy, R, i, sink=18.0, proud=3.0):
    """a flat cut-out on facet i of the pumpkin (poly x relative to the facet's middle)"""
    phi, d, u = facet(R, i)
    with ch.placed("head", roty(-phi)):
        plate(ch, "head", t, [(x + u, y) for x, y in poly_xy], -d - proud, -d + sink)


def build():
    ch = Char("PUMPKIN", DESC, Rig(hip=900, hipx=130, knee=450, ankle=80))
    b = body(ch, legs="team", shins="team", feet="dark", pelvis="team", torso="dark", head=None,
             thigh=(78, 76, 66, 70), shin=(66, 70, 70, 66), foot=(64, 178, 88), calf=False, thigh_mid=False,
             waist=(186, 118), chest=(222, 136), shoulder=(198, 116))
    # the team waistcoat down the tailcoat's open front, three gold buttons
    band(ch, b, "torso", "team", 930, 1405, FRONT, grow=5, depth=14)
    for y in (1040, 1140, 1240):
        plate(ch, "torso", "gold", [(0, y - 18), (18, y), (0, y + 18), (-18, y)], -160, -140)
    # the coat's ragged tails hanging behind
    tails = [(-200, 960), (200, 960), (204, 700), (150, 760), (104, 640), (46, 735), (-22, 625), (-84, 730), (-140, 650), (-198, 735)]
    plate(ch, "torso", "dark", tails, 126, 146, shell=True)
    # the dark shoulders
    for sd in (1, -1):
        with ch.placed("torso", rotz(-sd * 0.18), (sd * 284, 1236, 0)):
            ch.loft("torso", "dark", hexring(82, 94, sd * 284), 1236, hexring(56, 66, sd * 270), 1408, "y")
    # the team cravat
    ch.loft("torso", "team", ring(140, 100), 1352, ring(118, 88), 1428 - ch.outline, "y")
    plate(ch, "torso", "team", [(-34, 1380), (34, 1380), (20, 1290), (0, 1270), (-20, 1290)], -142, -122)
    # the pumpkin: five ribs, squat and round, on a green vine neck
    R = 166
    ch.sweep("head", "pumpkin", [(ribbed(108), 1450), (ribbed(160), 1505), (ribbed(R), 1640), (ribbed(112), 1712)])
    # the candle-lit face carved on the two front facets: triangle eyes, a nose on the rib, a jagged grin
    carve(ch, "candle", [(-30, 1600), (30, 1600), (6, 1652)], R, 0)
    carve(ch, "candle", [(-30, 1600), (30, 1600), (-6, 1652)], R, 9)
    plate(ch, "head", "candle", [(-18, 1562), (18, 1562), (0, 1592)], -R - 6, -R + 14)
    carve(ch, "candle", [(-44, 1550), (-26, 1512), (34, 1508), (44, 1524), (10, 1528), (-14, 1540)], R, 0)
    carve(ch, "candle", [(44, 1550), (26, 1512), (-34, 1508), (-44, 1524), (-10, 1528), (14, 1540)], R, 9)
    # the stem, its curl and a leaf
    ch.solid("head", "stem", ngon(0, 0, 26, 5), "y", 1690, 1772)
    tube(ch, "head", "stem", [(0, 1760, 0), (40, 1790, 0), (66, 1770, 0)], [14, 11, 8], k=3)
    plate(ch, "head", "stem", [(-20, 1712), (-90, 1745), (-130, 1722), (-80, 1706)], -30, -18, shell=True)
    return ch
