"""BUBBLE: the bubble gun's soap bubble, a faceted low-poly sphere.

A see-through team-coloured shell (an icosphere split once, 80 facets), its facets baked in three glass tones lit from
above so it still reads as faceted (the game draws see-through faces flat, unlit, both sides), a smaller opaque hot
team core with its ink outline inside, and the white cartoon highlights: a curved streak over the top and a dot low on
the other side. The shell has no ink hull: the hull's inner faces would show through the glass as a black ball. The
game wobbles it (x and z stretched by 1 +- 0.18) and never turns it, so the baked light stays on top.
"""
import math

import lowpoly
from gen_weapons_lowpoly import Model

NAME = "BUBBLE"

# the shell's glass tones (team slot by their names; drawn see-through like team_glass)
for _n, _f in (("team_glass_hi", 1.45), ("team_glass_lo", 0.62)):
    lowpoly.TONES.setdefault(_n, ("team", _f))
    lowpoly.ALPHA.setdefault(_n, 0.35)

R_SHELL = 170.0   # the box bubble's radius
R_CORE = 72.0


def _norm(v):
    L = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / L for c in v)


def _icosphere(subdiv=1):
    """unit icosphere: (vertices, faces)"""
    t = (1 + math.sqrt(5)) / 2
    V = [_norm(p) for p in ((-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
                            (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1))]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdiv):
        mid, F2 = {}, []

        def m(a, b):
            k = (min(a, b), max(a, b))
            if k not in mid:
                V.append(_norm(tuple((V[a][i] + V[b][i]) / 2 for i in range(3))))
                mid[k] = len(V) - 1
            return mid[k]
        for a, b, c in F:
            ab, bc, ca = m(a, b), m(b, c), m(c, a)
            F2 += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        F = F2
    return V, F


def _tri(mesh, mat, a, b, c, inward=False, ref=(0, 0, 0)):
    """one triangle, wound to face away from ref (toward it with inward)"""
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    out = sum(n[i] * ((a[i] + b[i] + c[i]) / 3 - ref[i]) for i in range(3)) > 0
    if out == inward:
        b, c = c, b
    mesh.add(mat, a, b, c)


def _sphere(mesh, mat, center, r, subdiv=1, hull=True, tone=None):
    V, F = _icosphere(subdiv)
    for f in F:
        if tone:
            nrm = _norm(tuple(sum(V[k][i] for k in f) for i in range(3)))
            fm = tone(nrm)
        else:
            fm = mat
        p = [tuple(center[i] + V[k][i] * r for i in range(3)) for k in f]
        _tri(mesh, fm, *p, ref=center)
        if hull:
            q = [tuple(center[i] + V[k][i] * (r + lowpoly.OUTLINE) for i in range(3)) for k in f]
            _tri(mesh, "ink", *q, inward=True, ref=center)


def _sph(r, el, az):
    """a point on the sphere: elevation and azimuth in degrees (azimuth 0 = +x, 90 = -z)"""
    e, a = math.radians(el), math.radians(az)
    return (r * math.cos(e) * math.cos(a), r * math.sin(e), -r * math.cos(e) * math.sin(a))


def _streak(mesh, mat, el, az0, az1, w, seg=5):
    """a curved highlight lying on the shell: a thin slab along a latitude, tapered at both ends"""
    r0, r1 = R_SHELL + 1.0, R_SHELL + 5.0
    rings = []
    for k in range(seg + 1):
        u = k / seg
        az = az0 + (az1 - az0) * u
        half = w * (0.25 + 0.75 * math.sin(math.pi * u))
        rings.append([_sph(r1, el + half, az), _sph(r1, el - half, az), _sph(r0, el - half, az), _sph(r0, el + half, az)])
    for k in range(seg):
        A, B = rings[k], rings[k + 1]
        for i in range(4):
            j = (i + 1) % 4
            # each side quad, wound away from the slab's own centre line at that spot
            mid = tuple((A[0][t] + A[2][t] + B[0][t] + B[2][t]) / 4 for t in range(3))
            _tri(mesh, mat, A[i], A[j], B[j], ref=mid)
            _tri(mesh, mat, A[i], B[j], B[i], ref=mid)
    for E, nxt in ((rings[0], rings[1]), (rings[-1], rings[-2])):   # end caps, wound away from the next ring
        ref = tuple(sum(p[t] for p in nxt) / 4 for t in range(3))
        _tri(mesh, mat, E[0], E[1], E[2], ref=ref)
        _tri(mesh, mat, E[0], E[2], E[3], ref=ref)


def _dot(mesh, mat, p, r):
    """a little faceted gem (an octahedron) on the shell"""
    c = _norm(p)
    c = tuple(v * (R_SHELL + r * 0.6) for v in c)
    pts = [(c[0] + r, c[1], c[2]), (c[0] - r, c[1], c[2]), (c[0], c[1] + r, c[2]), (c[0], c[1] - r, c[2]),
           (c[0], c[1], c[2] + r), (c[0], c[1], c[2] - r)]
    for a in (0, 1):
        for b in (2, 3):
            for d in (4, 5):
                _tri(mesh, mat, pts[a], pts[b], pts[d], ref=c)


def build():
    m = Model(NAME, "soap bubble: a faceted see-through team shell lit from above, a hot team core with an ink outline "
                    "inside, a white curved highlight over the top and a small one low on the other side")
    B = m.body
    L = _norm((0.35, 1.0, 0.25))

    def tone(n):
        d = sum(n[i] * L[i] for i in range(3))
        return "team_glass_hi" if d > 0.55 else ("team_glass" if d > -0.2 else "team_glass_lo")
    _sphere(B, None, (0, 0, 0), R_SHELL, 1, hull=False, tone=tone)
    _sphere(B, "team_hot", (0, 0, 0), R_CORE, 1, hull=True)
    _streak(B, "white", 40, -30, 150, 9, seg=8)
    _dot(B, "white", (-0.6, -0.35, 0.45), 16)
    return m
