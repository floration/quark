"""WEREWOLF: the werewolf (box design: characters_outlined_extra2.werewolf)."""
import math

from players_lowpoly.kit import Char, Rig, body, plate, profile, ring, hexring, rotz

DESC = "the werewolf: a grey wolf head with a long snout, pointed ears and yellow eyes, a hunched furry back, a torn team shirt and shorts, claws"


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


# ---- the werewolf -----------------------------------------------------------------------------------------------------

def build():
    ch = Char("WEREWOLF", DESC, Rig(hip=880, hipx=130, knee=440, ankle=70))
    # team shorts torn off at the knee over furry grey legs, big paws with bone claws
    b = body(ch, legs="team", shins="wolf", feet="wolf", pelvis="team", torso="team", head=None,
             thigh=(84, 82, 70, 72), shin=(72, 74, 70, 66), foot=(74, 190, 90), calf=False, thigh_mid=False,
             waist=(190, 120), chest=(232, 142), shoulder=(206, 122), chest_y=1220)
    ch.sym(lambda sd, s: [plate(ch, "foot" + s, "bone", [(sd * 130 + x - 18, 12), (sd * 130 + x + 18, 12), (sd * 130 + x, 42)], -206, -186) for x in (-30, 30)])
    # the shorts' torn hem: rags hanging at the knee
    for sd in (1, -1):
        x = sd * 130
        plate(ch, "thigh" + ("_r" if sd > 0 else "_l"), "team", [(x - 76, 520), (x + 76, 520), (x + 66, 446), (x - 4, 490), (x - 60, 440)], -88, 88)
    # the bushy tail, its pale tip
    tube(ch, "pelvis", "wolf", [(0, 840, 110), (0, 790, 220), (0, 690, 292)], [40, 56, 44], k=5)
    tube(ch, "pelvis", "wolf_lt", [(0, 700, 286), (0, 610, 320)], [40, 16], k=5)
    # the shirt's rip, the fur showing through
    plate(ch, "torso", "wolf_lt", [(-90, 1000), (-30, 1050), (40, 1010), (60, 1120), (0, 1180), (-70, 1140)], -168, -146)
    # the hunched furry back: a hump of fur over the shoulder blades, hackles bristling up to the neck
    hump = lambda k, dz: [(-190 * k, 60), (190 * k, 60), (222 * k, 120 + dz * 0.6), (150 * k, 190 + dz), (-150 * k, 190 + dz), (-222 * k, 120 + dz * 0.6)]
    ch.sweep("torso", "wolf", [(hump(0.86, -20), 1215), (hump(1.0, 30), 1330), (hump(0.84, 0), 1416)])
    profile(ch, "torso", "wolf_lt", [(196, 1250), (236, 1262), (232, 1300), (262, 1318), (240, 1350), (250, 1384), (196, 1404)], -36, 36, shell=False)
    # the furry shoulders and the claws below them
    for sd in (1, -1):
        with ch.placed("torso", rotz(-sd * 0.18), (sd * 286, 1236, 0)):
            ch.loft("torso", "wolf", hexring(84, 96, sd * 286), 1236, hexring(60, 70, sd * 272), 1408, "y")
        plate(ch, "torso", "wolf_lt", [(sd * 250, 1236), (sd * 350, 1236), (sd * 338, 1140), (sd * 300, 1196), (sd * 266, 1140)], -40, 40)
    # the wolf's head: a broad-cheeked skull set back on the neck, a long pale snout, a black nose, fangs, slanted
    # glowing eyes
    ch.sweep("head", "wolf", [(ring(116, 108, cz=16), 1436), (ring(158, 126, cz=14), 1545), (ring(112, 108, cz=22), 1712)])
    snout = lambda w, h, y: [(-w, y - h), (w, y - h), (w * 1.05, y + h * 0.4), (w * 0.6, y + h), (-w * 0.6, y + h), (-w * 1.05, y + h * 0.4)]
    ch.loft("head", "wolf_lt", snout(74, 64, 1530), -90, snout(38, 34, 1512), -176, "z")
    ch.loft("head", "eye", snout(22, 13, 1538), -180, snout(16, 9, 1538), -168, "z", shell=False)
    plate(ch, "head", "white", [(-36, 1478), (36, 1478), (24, 1460), (0, 1472), (-24, 1460)], -170, -130)
    ch.sym(lambda sd, s: plate(ch, "head", "zeye", [(sd * 30, 1598), (sd * 90, 1610), (sd * 80, 1630)], -118, -100))
    # the pointed ears, tipped out
    for sd in (1, -1):
        with ch.placed("head", rotz(-sd * 0.35), (sd * 80, 1660, 10)):
            ch.loft("head", "wolf", [(sd * 40 + 70 * sd, -30), (sd * 80 + 4 * sd, 40), (sd * 80 - 52 * sd, -30)], 1650, [(sd * 92, -4), (sd * 88, 4), (sd * 84, -4)], 1820, "y")
    return ch
