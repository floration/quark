"""DINO: the dinosaur costume (box design: characters_outlined_extra2.dino)."""
import math

from players_lowpoly.kit import Char, Rig, body, band, plate, profile, ring, hexring, rotz, FRONT, FRONT_WRAP

DESC = "the dinosaur costume: a green hood with an open toothy jaw round the face, team spikes from the crown down the back to a long tail, a team belly"


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


# ---- the dinosaur -----------------------------------------------------------------------------------------------------

def build():
    ch = Char("DINO", DESC, Rig(hip=900, hipx=130, knee=450, ankle=80))
    # a green costume with stout legs, team slippers with a cuff up the ankle and a white toe claw
    b = body(ch, legs="forest", shins="forest", feet="team", pelvis="forest", torso="forest", head=None,
             thigh=(88, 86, 74, 76), shin=(76, 78, 74, 70), foot=(76, 190, 92), calf=False, thigh_mid=False,
             pelvis_w=(176, 214), pelvis_d=(122, 140), waist=(206, 132), chest=(236, 150), shoulder=(206, 124), chest_y=1200)
    b.wrap("shin_", 80, 240, "team", grow=9, shell=False)
    ch.sym(lambda sd, s: plate(ch, "foot" + s, "white", [(sd * 130 - 24, 12), (sd * 130 + 24, 12), (sd * 130, 50)], -210, -190))
    # the team belly, from the hips up the chest
    band(ch, b, "pelvis", "team", 790, 940, FRONT, grow=5, depth=14)
    band(ch, b, "torso", "team", 930, 1380, FRONT_WRAP, grow=6, depth=14)
    # the long tail, sweeping down behind, team spikes along its top
    tube(ch, "pelvis", "forest", [(0, 830, 110), (0, 650, 270), (0, 410, 350)], [84, 56, 18], k=5)
    profile(ch, "pelvis", "team", [(150, 870), (176, 922), (214, 846), (250, 838), (286, 736), (332, 600), (280, 700)], -12, 12, shell=False)
    # team spikes down the back
    profile(ch, "torso", "team", [(140, 1020), (236, 1070), (168, 1130), (256, 1190), (168, 1250), (240, 1312), (126, 1400)], -16, 16, shell=False)
    # the shoulders with team costume paws
    for sd in (1, -1):
        with ch.placed("torso", rotz(-sd * 0.18), (sd * 286, 1236, 0)):
            ch.loft("torso", "forest", hexring(84, 96, sd * 286), 1236, hexring(60, 70, sd * 272), 1408, "y")
            ch.loft("torso", "team", hexring(92, 104, sd * 286), 1150, hexring(88, 100, sd * 286), 1290, "y", shell=False)
    # the hood: a green head, the face looking out of the open jaw, the costume's eyes above the snout
    ch.sweep("head", "forest", [(ring(130, 128), 1436), (ring(156, 150), 1560), (ring(120, 118, cz=10), 1712)])
    plate(ch, "head", "skin", [(-92, 1490), (92, 1490), (92, 1600), (-92, 1600)], -160, -140)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 28, 1558), (sd * 60, 1558), (sd * 44, 1580)], -164, -156))
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 40, 1690), (sd * 84, 1684), (sd * 70, 1712)], -136, -112))
    # the upper jaw pushed out over the face and its teeth; the lower jaw under the face, lined with team
    jaw = lambda w, y0, y1: [(-w, y0), (w, y0), (w, y1 - 26), (w * 0.6, y1), (-w * 0.6, y1), (-w, y1 - 26)]
    ch.loft("head", "forest", jaw(140, 1596, 1682), -100, jaw(64, 1600, 1660), -168, "z")
    for x in (-36, 36):
        plate(ch, "head", "white", [(x - 16, 1604), (x + 16, 1604), (x, 1566)], -164, -148)
    ch.loft("head", "forest", jaw(134, 1438, 1494), -100, jaw(62, 1446, 1488), -164, "z")
    plate(ch, "head", "team", [(-100, 1494), (100, 1494), (0, 1504)], -160, -120)
    # team spikes over the crown and down the nape
    profile(ch, "head", "team", [(-60, 1700), (-20, 1800), (30, 1712), (100, 1750), (110, 1660), (178, 1640), (140, 1560)], -14, 14)
    return ch
