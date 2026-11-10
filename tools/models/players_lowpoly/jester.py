"""JESTER: the jester (box design: characters_outlined_extra2.jester)."""
import math

from players_lowpoly.kit import Char, Rig, body, plate, profile, ring, hexring, ngon, offset, rotz, rotx

DESC = "the jester: a two-pointed drooping hat with gold bells, a harlequin suit split team and gold, a scalloped collar, curled shoes"


# ---- helpers of this module (the kit has no bent tube, no half-and-half piece, no one-sided pauldron) ----------------

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


def half(poly, sd):
    """the side of an (x, z) polygon where sd * x >= 0, cut at x = 0"""
    out, n = [], len(poly)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        fp, fq = sd * p[0], sd * q[0]
        if fp >= 0:
            out.append(p)
        if (fp > 0 > fq) or (fp < 0 < fq):
            s = fp / (fp - fq)
            out.append((0.0, p[1] + (q[1] - p[1]) * s))
    return out


def recolor(ch, bone, frm, to):
    """move a bone's triangles of tone frm to tone to (one leg of the suit gold, the other team)"""
    m, mat = ch._m(bone, to)
    src = m.tris.pop(ch._m(bone, frm)[1])
    m.tris.setdefault(mat, []).extend(src)


def shoulder(ch, sd, t, x=285, y0=1225, y1=1418, w=78, d=96, tilt=0.18, band=None):
    """kit.pauldron for one side (sd +1 the right)"""
    y1 -= ch.outline
    cx = sd * x
    poly = lambda s: hexring(w * s, d * s, cx)
    with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
        ch.sweep("torso", t, [(poly(1.0), y0), (poly(1.08), y0 + (y1 - y0) * 0.55), ([(u - sd * w * 0.25, v) for u, v in poly(0.62)], y1)])
        if band:
            ch.loft("torso", band, poly(1.06), y0 - 28, poly(1.1), y0 + 6, "y", shell=False)


def diamond(cx, cy, w, h):
    return [(cx, cy - h), (cx + w, cy), (cx, cy + h), (cx - w, cy)]


# ---- the jester -------------------------------------------------------------------------------------------------------

def build():
    ch = Char("JESTER", DESC, Rig(hip=900, hipx=130, knee=450, ankle=60))
    b = body(ch, legs="team", shins="team", feet="team", pelvis="dark", torso=None, head=None,
             foot=(66, 175, 82), thigh=(80, 78, 66, 70), calf=False, thigh_mid=False)
    # the harlequin legs: the right thigh and the left shin gold, the others team
    recolor(ch, "thigh_r", "team", "gold")
    recolor(ch, "shin_l", "team", "gold")
    # the curled gold toes rising off the front of each shoe
    ch.sym(lambda sd, s: profile(ch, "foot" + s, "gold", [(-150, 26), (-226, 52), (-214, 122), (-168, 76)], sd * 130 - 28, sd * 130 + 28))
    # the suit: the torso in two halves, the model's right team and its left gold
    o = ch.outline
    rings_ = [(ring(190, 120), 920), (ring(228, 140, front=152), 1250), (ring(200, 118, c=118 * 0.6), 1418 - o)]
    for sd, t in ((1, "team"), (-1, "gold")):
        ch.sweep("torso", t, [(half(p, sd), w) for p, w in rings_])
    # the diamonds on the front, counter-changed
    for sd, t in ((1, "gold"), (-1, "team")):
        plate(ch, "torso", t, diamond(sd * 95, 1120, 52, 80), -152, -128)
    # the shoulders, counter-changed too
    for sd, t in ((1, "gold"), (-1, "team")):
        with ch.placed("torso", rotz(-sd * 0.18), (sd * 284, 1236, 0)):
            ch.loft("torso", t, hexring(80, 94, sd * 284), 1236, hexring(56, 68, sd * 268), 1408, "y")
    # the scalloped white collar: a ruff of 6 points round the neck
    star = lambda r0, r1, sz, ph: [((r0 if i % 2 == 0 else r1) * math.cos(math.pi * i / 6 + ph), sz * (r0 if i % 2 == 0 else r1) * math.sin(math.pi * i / 6 + ph)) for i in range(12)]
    ch.loft("torso", "white", star(268, 212, 0.74, math.pi / 2), 1338, star(218, 186, 0.78, math.pi / 2), 1428 - o, "y")
    # the head: a skin face (eyes, a grin turned up at the ends) under a split cap
    head = [(ring(110, 108), 1432), (ring(134, 148), 1550), (ring(132, 146), 1628)]
    ch.sweep("head", "skin", head)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", diamond(sd * 48, 1592, 20, 15), -156, -136))
    plate(ch, "head", "eye", [(-56, 1548), (0, 1520), (56, 1548)], -156, -136)
    # the hat: its right half team and left gold, a drooping point each side ending in a white bell
    cap = [(offset(ring(132, 146), 10), 1618), (ring(104, 116), 1712)]
    for sd, t in ((1, "team"), (-1, "gold")):
        ch.sweep("head", t, [(half(p, sd), w) for p, w in cap])
        tube(ch, "head", t, [(sd * 45, 1700, 0), (sd * 100, 1795, 0), (sd * 160, 1772, 0), (sd * 172, 1692, 0)], [56, 34, 24, 15], k=4, phase=math.pi / 4)
        ch.rod("head", "white", (sd * 172, 1698, 0), (sd * 172, 1636, 0), 21, k=4, shell=False)
    return ch
