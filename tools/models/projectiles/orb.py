"""ORB: the model of every projectile type with no model of its own (the game's fallback: MODEL_PROJ_ORB, models.h).

No shipped weapon fires one: rockets, stickies, bolts, bubbles and thrown knives / syringes have their own models, and
the rifle, sniper, laser, shotgun (ricochets too) and locator are hitscan. The orb shows for a projectile type id the
client has no model for (a newer server's or a mod's projectile), in the shooter's team colour.

A faceted low-poly energy orb (a team-coloured gem shaded toon-style: hot team facets on top with white glints on
every other facet of the upper cap, plain team below the middle, dark team on the lower cap) inside a tilted flat
ring: team_dark with four glowing segments. Every piece has its ink
hull. The game draws it upright, never turned with its flight (game.cpp: my = mp = 0).
"""
import math

import lowpoly
from gen_weapons_lowpoly import Model

NAME = "ORB"


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _tri(mesh, mat, a, b, c, out):
    """a triangle facing `out` (glTF fronts are counter-clockwise)"""
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    if _dot(n, out) < 0:
        b, c = c, b
    mesh.add(mat, a, b, c)


def _xf(R, p):
    return tuple(sum(R[i][k] * p[k] for k in range(3)) for i in range(3))


def sphere(mesh, matf, r, seg=8, bands=4, shell=True):
    """a faceted sphere (seg around, bands from pole to pole), matf(i, j, centre) -> material per facet, with an
    inward ink hull grown by lowpoly.OUTLINE"""
    def grid(rad):
        rings = []
        for j in range(1, bands):
            th = math.pi * j / bands
            ph0 = (math.pi / seg) * (j % 2)   # (alternate rings turned half a facet: gem-like triangles)
            rings.append([(rad * math.sin(th) * math.cos(ph0 + 2 * math.pi * i / seg), rad * math.cos(th),
                           rad * math.sin(th) * math.sin(ph0 + 2 * math.pi * i / seg)) for i in range(seg)])
        return (0, rad, 0), rings, (0, -rad, 0)

    def faces(rad):
        top, rings, bot = grid(rad)
        f = []
        for i in range(seg):
            f.append((0, i, (top, rings[0][i], rings[0][(i + 1) % seg])))
            f.append((bands - 1, i, (bot, rings[-1][(i + 1) % seg], rings[-1][i])))
        for j in range(len(rings) - 1):
            A, B = rings[j], rings[j + 1]
            odd = j % 2 == 1   # ring j+1 is turned half a facet from ring j
            for i in range(seg):
                if odd:   # B[i] sits between A[i] and A[i+1]
                    f.append((j + 1, 2 * i, (A[i], A[(i + 1) % seg], B[i])))
                    f.append((j + 1, 2 * i + 1, (A[(i + 1) % seg], B[(i + 1) % seg], B[i])))
                else:     # A[i+1] sits between B[i] and B[i+1]
                    f.append((j + 1, 2 * i, (A[i], B[(i + 1) % seg], B[i])))
                    f.append((j + 1, 2 * i + 1, (A[i], A[(i + 1) % seg], B[(i + 1) % seg])))
        return f

    for j, i, (a, b, c) in faces(r):
        ctr = tuple((a[k] + b[k] + c[k]) / 3 for k in range(3))
        _tri(mesh, matf(j, i, ctr), a, b, c, ctr)
    if shell:
        for _, _, (a, b, c) in faces(r + lowpoly.OUTLINE):
            ctr = tuple((a[k] + b[k] + c[k]) / 3 for k in range(3))
            _tri(mesh, "ink", a, b, c, tuple(-v for v in ctr))


def ring(mesh, matf, r0, r1, h, k=16, R=None, shell=True):
    """a flat k-sided ring (inner r0, outer r1, h thick), matf(i) -> material per segment, turned by R, with its hull"""
    R = R or ((1, 0, 0), (0, 1, 0), (0, 0, 1))

    def build(a0, a1, hh, mat_of, inv):
        s = -1 if inv else 1
        for i in range(k):
            t0, t1 = 2 * math.pi * i / k, 2 * math.pi * (i + 1) / k
            c0, s0, c1, s1 = math.cos(t0), math.sin(t0), math.cos(t1), math.sin(t1)
            P = lambda rad, c, sn, y: _xf(R, (rad * c, y, rad * sn))
            i0t, i1t, o0t, o1t = P(a0, c0, s0, hh), P(a0, c1, s1, hh), P(a1, c0, s0, hh), P(a1, c1, s1, hh)
            i0b, i1b, o0b, o1b = P(a0, c0, s0, -hh), P(a0, c1, s1, -hh), P(a1, c0, s0, -hh), P(a1, c1, s1, -hh)
            mid = (math.cos((t0 + t1) / 2), math.sin((t0 + t1) / 2))
            up, rad = _xf(R, (0, 1, 0)), _xf(R, (mid[0], 0, mid[1]))
            neg = lambda v: tuple(-x for x in v)
            mat = mat_of(i)
            for q, n in (((i0t, o0t, o1t, i1t), up), ((i0b, o0b, o1b, i1b), neg(up)),
                         ((o0b, o1b, o1t, o0t), rad), ((i0b, i1b, i1t, i0t), neg(rad))):
                n = tuple(s * x for x in n)
                _tri(mesh, mat, q[0], q[1], q[2], n)
                _tri(mesh, mat, q[0], q[2], q[3], n)

    build(r0, r1, h / 2, matf, False)
    if shell:
        o = lowpoly.OUTLINE
        build(r0 - o, r1 + o, h / 2 + o, lambda i: "ink", True)


def build():
    m = Model(NAME, "energy orb: a faceted team-coloured gem, hot on top with white glints, dark underneath, inside a "
                    "tilted flat team ring with four glowing segments (the fallback for any projectile with no model)")
    B = m.body
    def shell_mat(j, i, ctr):
        if j == 0:   # the upper cap catches the light: a white glint on every other facet
            return "white" if i % 2 == 0 else "team_hot"
        if j == 1:
            return "team_hot"
        if j == 2:
            return "team_wood"
        return "team_dark"   # the lower cap in shadow

    sphere(B, shell_mat, 96, seg=8, bands=4)
    # the ring, rolled 18 degrees and pitched 16 so it reads as a ring at eye height, not a line
    R = lowpoly.matmul(lowpoly.rotz(math.radians(18)), lowpoly.rotx(math.radians(16)))
    ring(B, lambda i: "glow_hot" if i % 4 == 1 else "team_dark", 120, 140, 11, k=16, R=R)
    return m
