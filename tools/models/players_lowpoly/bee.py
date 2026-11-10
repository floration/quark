"""BEE: the bee mascot (box design: characters_outlined_extra2.bee)."""
import math

from players_lowpoly.kit import Char, Rig, Segment, body, plate, ring, hexring, ngon, rrect, scale_poly, rotz, roty, matmul

DESC = "the bee mascot: a round honey-and-black striped body, glassy wings on the back, a stinger, antennae, big dark eyes, team shorts, sleeves and boots"


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


# ---- the bee ----------------------------------------------------------------------------------------------------------

def build():
    ch = Char("BEE", DESC, Rig(hip=900, hipx=130, knee=450, ankle=90))
    # team shorts (the hips and thighs) down to the knee, thin dark legs (the box design's black, at the darkest big-area tone), tall team boots
    b = body(ch, legs="team", shins="dark", feet="team", pelvis="team", torso=None, head=None,
             thigh=(80, 80, 68, 70), shin=(64, 70, 70, 66), foot=(70, 172, 96), calf=False, thigh_mid=False,
             pelvis_w=(176, 206), pelvis_d=(128, 150))
    # the stinger off the back of the abdomen
    ch.loft("pelvis", "hairblk", ngon(0, 880, 36, 5, phase=math.pi / 2), 140, ngon(0, 878, 6, 5, phase=math.pi / 2), 262, "z")
    # the round honey body, two dark stripes round it
    o = ch.outline
    tor = Segment(ch, "torso", [(ring(196, 140), 920), (ring(248, 174), 1150), (ring(192, 128, c=80), 1418 - o)])
    ch.sweep("torso", "honey", tor.rings)
    for y0, y1 in ((1010, 1080), (1190, 1262)):
        tor.wrap(y0, y1, "dark", grow=5, shell=False)
    # the glassy wings on the back: spread out past the body and up, white veins
    for sd in (1, -1):
        with ch.placed("torso", matmul(roty(-sd * 0.3), rotz(sd * 0.22)), (sd * 30, 1250, 170)):
            plate(ch, "torso", "glass", [(sd * 20, 1235), (sd * 140, 1190), (sd * 270, 1230), (sd * 300, 1310), (sd * 220, 1365), (sd * 60, 1360)], 172, 182, shell=True)
            plate(ch, "torso", "white", [(sd * 50, 1290), (sd * 250, 1255), (sd * 260, 1282)], 183, 186)
    # the team sleeves
    for sd in (1, -1):
        with ch.placed("torso", rotz(-sd * 0.2), (sd * 286, 1232, 0)):
            ch.loft("torso", "team", hexring(82, 94, sd * 286), 1232, hexring(58, 68, sd * 270), 1406, "y")
    # the white fluff collar: a ragged ring
    star = [((184 if i % 2 == 0 else 150) * math.cos(math.pi * i / 5), (130 if i % 2 == 0 else 108) * math.sin(math.pi * i / 5)) for i in range(10)]
    ch.loft("torso", "white", star, 1352, scale_poly(star, 0.82), 1428 - o, "y")
    # the head: a round honey head with a dark crown stripe
    hs = Segment(ch, "head", [(ring(120, 116), 1436), (ring(160, 156), 1575), (ring(112, 110), 1758)])
    ch.sweep("head", "honey", hs.rings)
    hs.wrap(1672, 1758, "dark", grow=6, shell=False)
    # the big dark eyes bulging off the face, their shine, a smile
    for sd in (1, -1):
        with ch.placed("head", roty(-sd * 0.5), (sd * 64, 1590, -140)):
            ch.loft("head", "eye", hexring(50, 66, sd * 64, 1590), -142, hexring(36, 48, sd * 64, 1592), -166, "z", shell=False)
            plate(ch, "head", "white", [(sd * 52, 1606), (sd * 74, 1606), (sd * 58, 1626)], -170, -166)
    plate(ch, "head", "eye", [(-40, 1508), (0, 1490), (40, 1508), (0, 1500)], -160, -140)
    # the antennae: dark stalks bent forward and out, team balls
    for sd in (1, -1):
        tube(ch, "head", "hairblk", [(sd * 40, 1740, -40), (sd * 70, 1800, -60), (sd * 108, 1792, -92)], [12, 10, 9], k=3)
        ch.solid("head", "team", ngon(sd * 114, -98, 28, 4), "y", 1772, 1822, shell=False)
    return ch
