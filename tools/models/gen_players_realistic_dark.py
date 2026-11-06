#!/usr/bin/env python3
"""The 11 player models remade in the "D2 realistic dark" look (the weapon viewmodels' approved style), as mesh
overrides that walk: models/player/<NAME>.gltf (docs/ASSETS.md "Models", "Player mesh parts").

    python3 tools/models/gen_players_realistic_dark.py [--out DIR] [--scratch DIR] [--export DIR] [--no-preview]
                                                       [--only STANDARD,HEAVY]

Writes, under DIR (default /mnt/project-files/player-models):
  D2-realistic-dark/models/player/<NAME>.gltf   the 11 models (copy them to an overrides folder to use them)
  lineup-D2-players.png                          all 11, front 3/4 and back 3/4
  closeup-D2-players.png                         all 11 at twice the size (where the baked polish reads)
  visibility-D2-players.png                      all 11 on a dark map (sRGB 0.05, light x0.6) and on a mid one
  old-vs-new-D2-players.png                      the built-in box models over the new ones
  walk-D2-players.png                            every model mid-stride (run and walk), plus a STANDARD gait strip
Previews are rendered into --scratch first and copied over (DIR may be a network mount). --export: a
`quark_tests exportassets` folder (its models/player/*.gltf box files) for the old models in old-vs-new; without it
that sheet is skipped.

Every model is built from scratch out of shaped meshes (tapered superellipse lofts, rounded / chamfered boxes, curved
plates and shells, tubes), each part hung from the joint it moves with:
- a glTF node named `anim_<bone>` (pelvis torso head thigh_l shin_l thigh_r shin_r foot_l foot_r; `_l` = the model's
  left, -X) with a `translation` = the joint's pivot in model space and its vertices relative to that pivot; the rest
  (the static body: a robe's hem, SIMPLE) in the node named after the model. The pivots are the joints the game's
  playerRig measures from box models: thigh = the hip (x = the leg's offset), shin = the knee, foot = the ankle,
  head = the neck, torso = the waist, pelvis = the hips' centre. An older game draws every node where it stands.
- materials: `team*` parts follow the team colour (COLOR_0 white = the colour exactly: the baked shading is grey);
  `glow*` 30% brighter than the box models' glows; everything else is a fixed colour (gunmetal never follows a map).
- the D2 polish baked per vertex into COLOR_0, view-independent (players are seen from every side): a dim cool sky /
  dark horizon / warm ground reflection by the normal, a top-left key light rim on upward bevels, a warm bounce from
  below, worn bright steel on narrow bevel / chamfer faces (cloth and leather wear lighter instead), a height
  gradient on side faces, and voxel-march ambient occlusion across the whole model (gaps, joints, under belts and
  plates darker).
- every vertex (and edge point) of the standing pose inside the shared hitbox (gamedefs.h kPlayerHalfWidth,
  kPlayerHeight, kHeadFraction, kHeadWidthFraction; pmove.h hitShape: a body cylinder r 0.4 m from the floor to
  1.44 m, a head cylinder r 0.18 m from there to 1.8 m), with a 4 mm margin: the script fails otherwise.
- at most 1000 triangles a model (owner, 2026-11-05), so stylized: chunky faceted shapes, few sides, broad colour
  blocks, the baked shading doing the detail.
Deterministic (no randomness): numpy, plus Pillow for the previews.
"""
import argparse
import base64
import json
import math
import os
import re
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
STYLE = "D2-realistic-dark"
NAMES = ["STANDARD", "HEAVY", "SIMPLE", "ROBOT", "KNIGHT", "WIZARD", "ZOMBIE", "FIGHTER", "PIRATE", "NINJA", "BANDIT"]
BONES = ["root", "pelvis", "torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r", "foot_l", "foot_r"]
MAX_TRIS = 1000        # a model at most (owner, 2026-11-05: 32 players on screen)
WIDEN = (1.0, 1.0)     # (the bodies as designed; widen() still eases anything past r 0.30 m to inside the body cylinder)


# ---------------------------------------------------------------------------------------------------------------
# the hitbox (read from the game's sources so the check follows them)

def _const(name, default):
    try:
        with open(os.path.join(REPO, "src", "game", "gamedefs.h")) as f:
            m = re.search(r"constexpr float %s = ([0-9.]+)f;" % name, f.read())
        return float(m.group(1)) if m else default
    except OSError:
        return default


HALF_W = _const("kPlayerHalfWidth", 0.4)
HEIGHT = _const("kPlayerHeight", 1.8)
HEAD_FR = _const("kHeadFraction", 0.2)
HEAD_WFR = _const("kHeadWidthFraction", 0.45)
R_BODY, R_HEAD = HALF_W, HALF_W * HEAD_WFR
Y_NECK = HEIGHT * (1 - HEAD_FR)          # the body cylinder's top, the head cylinder's bottom
MARGIN = 0.004


def hitbox_misses(P):
    """the points of P (n, 3) outside the hitbox (less the margin): a bool mask"""
    r = np.hypot(P[:, 0], P[:, 2])
    y = P[:, 1]
    body = y < Y_NECK
    ok = np.where(body, r <= R_BODY - MARGIN, (r <= R_HEAD - MARGIN) & (y <= HEIGHT - MARGIN))
    return ~(ok & (y >= -0.0005))


# ---------------------------------------------------------------------------------------------------------------
# colours (linear)

def lin3(r, g, b):
    return np.array((r ** 2.2, g ** 2.2, b ** 2.2))


def _norm(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def rows(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-20)


# the weapons' D2 palette (gen_weapons_realistic_dark.py)
NITRIDE = lin3(0.19, 0.205, 0.235)       # (the weapons' 0.105 / 0.118 / 0.140 lifted: players must read on dark maps)
STEEL_SPEC = np.array((0.78, 0.84, 0.95))
WORN = lin3(0.42, 0.45, 0.50)
RIM = np.array((0.45, 0.50, 0.60))
RECESS = lin3(0.035, 0.038, 0.045)
ALLOY = lin3(0.17, 0.18, 0.205)
POLY = lin3(0.25, 0.24, 0.228)
RUBBER = lin3(0.19, 0.188, 0.184)
LENS = lin3(0.05, 0.11, 0.17)
GLOW_GAIN = 1.3
SKY = np.array((0.060, 0.078, 0.112))
HOR = np.array((0.006, 0.007, 0.009))
GND = np.array((0.032, 0.029, 0.025))
BOUNCE = np.array((0.42, 0.36, 0.30))
LK = _norm((-0.40, 0.80, -0.45))          # key light: top-left-front (model space)
LB = _norm((0.35, -0.55, 0.30))           # warm bounce from below
# the characters' own tones
SKIN = lin3(0.80, 0.60, 0.47)
SKIN_ZOMBIE = lin3(0.50, 0.60, 0.42)
ROT = lin3(0.34, 0.40, 0.24)
LEATHER = lin3(0.36, 0.24, 0.15)
LEATHER_DK = lin3(0.30, 0.21, 0.15)
CREAM = lin3(0.80, 0.77, 0.68)
WHITE = lin3(0.86, 0.86, 0.88)
HAIR_DK = lin3(0.24, 0.17, 0.12)
NINJA_CLOTH = lin3(0.21, 0.215, 0.25)
GOLD = lin3(0.80, 0.62, 0.26)
RED = lin3(0.62, 0.13, 0.10)
BLOOD = lin3(0.38, 0.06, 0.05)
TAPE = lin3(0.88, 0.87, 0.82)
EYE_DK = lin3(0.05, 0.045, 0.045)
# visibility (owner, 2026-11-05: dark models were hard to see on dark maps): no vertex darker than this (sRGB 0.18,
# linear, by luminance, hue kept) but in the small dark details (FLOOR_EXEMPT: recesses, eyes, lenses, slits)
FLOOR = 0.18 ** 2.2
FLOOR_EXEMPT = ("lens", "poly_eyes", "nitride_slit", "nitride_eyerecess", "nitride_grille", "cloth_mouth", "leather_patch", "glow", "team")


class Look:
    """how a class of material is shaded (baked per vertex)"""

    def __init__(self, diffuse, spec=(1, 1, 1), f0=0.04, env=1.0, key=1.0, wear=0.0, rim=0.0, grad=0.25, ao=0.85, kp=36,
                 worn=None, glow=False):
        self.diffuse, self.spec = np.asarray(diffuse, float), np.asarray(spec, float)
        self.f0, self.env, self.key, self.wear, self.rim, self.grad, self.ao, self.kp = f0, env, key, wear, rim, grad, ao, kp
        self.worn = WORN if worn is None else np.asarray(worn, float)
        self.glow = glow


def gunmetal(k=1.0):
    return Look(NITRIDE * k, STEEL_SPEC, 0.5, env=1.4, key=1.5, wear=0.7, rim=1.3, grad=0.35, kp=24)


def alloy():
    return Look(ALLOY, (0.80, 0.84, 0.92), 0.5, env=1.6, key=1.4, wear=0.55, rim=1.0, grad=0.3, kp=24)


def recess():
    return Look(RECESS, STEEL_SPEC, 0.25, env=0.5, key=0.25, grad=0.0, ao=0.6)


def polymer(c=POLY):
    return Look(c, (1, 1, 1), 0.045, env=1.2, key=0.45, wear=0.3, rim=0.3, grad=0.25, kp=12, worn=np.asarray(c) * 2.2)


def rubber():
    return Look(RUBBER, (1, 1, 1), 0.03, env=0.6, key=0.18, grad=0.15, kp=8, worn=RUBBER * 1.6)


def lens():
    return Look(LENS, (0.80, 0.90, 1.0), 0.14, env=4.5, key=2.6, grad=0.0, ao=0.3, kp=60)


def cloth(c, wear=0.18):
    c = np.asarray(c, float)
    return Look(c, (1, 1, 1), 0.02, env=0.6, key=0.16, wear=wear + 0.12, rim=0.25, grad=0.22, kp=6, worn=c * 1.7)


def skin(c=SKIN):
    return Look(c, (1, 0.95, 0.9), 0.03, env=0.6, key=0.25, grad=0.12, kp=10, ao=0.75)


def leather(c=LEATHER):
    c = np.asarray(c, float)
    return Look(c, (1, 0.95, 0.9), 0.05, env=1.0, key=0.5, wear=0.45, rim=0.3, grad=0.25, kp=14, worn=c * 1.9)


def brass(c=GOLD):
    c = np.asarray(c, float)
    return Look(c * 0.45, c / c.max() * np.array((1.0, 0.95, 0.9)), 0.6, env=3.0, key=1.2, rim=0.4, grad=0.25)


def team(k, sheen=0.12, wear=0.15):
    """a team-coloured part: grey k (the tint multiplies it), cloth-like (sheen low) or painted (sheen higher)"""
    d = np.array((k, k, k), float)
    return Look(d, (1, 1, 1), 0.03 + sheen * 0.2, env=0.4 + sheen * 3, key=sheen * 2.5, wear=wear, rim=sheen, grad=0.2,
                kp=8 + sheen * 80, worn=d * 1.5)


def glow(c):
    return Look(np.asarray(c, float) * GLOW_GAIN, glow=True)


# ---------------------------------------------------------------------------------------------------------------
# mesh building (after gen_rifles.py: finish / ear clipping / sweep)

def finish(P, F, flat=False):
    P = np.asarray(P, float).reshape(-1, 3)
    F = np.asarray(F, np.int64).reshape(-1, 3)
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    cr = np.cross(b - a, c - a)
    keep = np.linalg.norm(cr, axis=1) > 1e-13
    F, cr = F[keep], cr[keep]
    vol = np.einsum("ij,ij->i", P[F[:, 0]], np.cross(P[F[:, 1]], P[F[:, 2]])).sum()
    if vol < 0:
        F = F[:, [0, 2, 1]]
        cr = -cr
    if flat:
        P = P[F.reshape(-1)]
        F = np.arange(len(P)).reshape(-1, 3)
        N = np.repeat(cr / np.linalg.norm(cr, axis=1)[:, None], 3, axis=0)
    else:
        N = np.zeros_like(P)
        for k in range(3):
            np.add.at(N, F[:, k], cr)
        used = np.unique(F.reshape(-1))
        remap = -np.ones(len(P), np.int64)
        remap[used] = np.arange(len(used))
        P, N, F = P[used], N[used], remap[F]
        N = N / np.maximum(np.linalg.norm(N, axis=1)[:, None], 1e-20)
    return P, N, F


def _earclip(ring):
    K = len(ring)
    n = np.zeros(3)
    for i in range(K):
        p, q = ring[i], ring[(i + 1) % K]
        n += np.array([(p[1] - q[1]) * (p[2] + q[2]), (p[2] - q[2]) * (p[0] + q[0]), (p[0] - q[0]) * (p[1] + q[1])])
    n = _norm(n)
    ref = np.array([1.0, 0, 0]) if abs(n[0]) < 0.9 else np.array([0, 1.0, 0])
    u = _norm(np.cross(ref, n))
    v = np.cross(n, u)
    pts = [(float(np.dot(p, u)), float(np.dot(p, v))) for p in ring]
    idx = list(range(K))
    tris = []

    def cross2(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def inside(p, a, b, c):
        return cross2(a, b, p) >= -1e-15 and cross2(b, c, p) >= -1e-15 and cross2(c, a, p) >= -1e-15

    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        m = len(idx)
        done = False
        for i in range(m):
            ia, ib, ic = idx[i - 1], idx[i], idx[(i + 1) % m]
            A, B, C = pts[ia], pts[ib], pts[ic]
            if cross2(A, B, C) <= 1e-16:
                continue
            if any(inside(pts[j], A, B, C) for j in idx if j not in (ia, ib, ic) and pts[j] not in (A, B, C)):
                continue
            tris.append((ia, ib, ic))
            idx.pop(i)
            done = True
            break
        if not done:
            tris.append((idx[-1], idx[0], idx[1]))
            idx.pop(0)
    tris.append(tuple(idx))
    return np.array(tris, np.int64)


def sweep(rings, mode="smooth", cap0=True, cap1=True):
    """a closed surface through rings (each K points, or 1 point = a pole); ends capped flat.
    mode: smooth (shared vertices), around (smooth around each ring, hard between rings), flat"""
    rings = [np.asarray(r, float).reshape(-1, 3) for r in rings]
    P, F = [], []
    cnt = [0]

    def add(pts):
        P.append(pts)
        i0 = cnt[0]
        cnt[0] += len(pts)
        return np.arange(i0, i0 + len(pts))

    shared = [add(r) for r in rings] if mode != "around" else None
    for i in range(len(rings) - 1):
        if mode == "around":
            ia, ib = add(rings[i]), add(rings[i + 1])
        else:
            ia, ib = shared[i], shared[i + 1]
        Ka, Kb = len(ia), len(ib)
        if Ka == Kb and Ka > 1:
            j = np.arange(Ka)
            j2 = (j + 1) % Ka
            F.append(np.stack([ia[j], ia[j2], ib[j2]], 1))
            F.append(np.stack([ia[j], ib[j2], ib[j]], 1))
        elif Kb == 1 and Ka > 1:
            j = np.arange(Ka)
            F.append(np.stack([ia[j], ia[(j + 1) % Ka], np.full(Ka, ib[0])], 1))
        elif Ka == 1 and Kb > 1:
            j = np.arange(Kb)
            F.append(np.stack([np.full(Kb, ia[0]), ib[(j + 1) % Kb], ib[j]], 1))
        else:
            raise ValueError("rings must match (or be a pole)")
    if cap0 and len(rings[0]) > 2:
        idx = add(rings[0])
        F.append(idx[_earclip(rings[0])][:, [0, 2, 1]])
    if cap1 and len(rings[-1]) > 2:
        idx = add(rings[-1])
        F.append(idx[_earclip(rings[-1])])
    return finish(np.concatenate(P), np.concatenate(F), flat=(mode == "flat"))


def poly_offset(poly, d):
    """a 2D polygon moved inward by d"""
    poly = np.asarray(poly, float)
    K = len(poly)
    area = 0.5 * sum(poly[i][0] * poly[(i + 1) % K][1] - poly[(i + 1) % K][0] * poly[i][1] for i in range(K))
    s = 1.0 if area > 0 else -1.0
    out = []
    for i in range(K):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % K]
        e1, e2 = _norm(p1 - p0), _norm(p2 - p1)
        n1, n2 = s * np.array([-e1[1], e1[0]]), s * np.array([-e2[1], e2[0]])
        out.append(p1 + d * (n1 + n2) / max(1 + np.dot(n1, n2), 0.2))
    return np.array(out)


def rot(axis, a):
    x, y, z = _norm(axis)
    c, s = math.cos(a), math.sin(a)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def xf(mesh, R=None, t=(0, 0, 0), s=None):
    """a mesh scaled by s (per axis), turned by R, then moved by t"""
    P, N, F = mesh
    if s is not None:
        s = np.asarray(s, float)
        P = P * s
        N = rows(N / s)
        if np.prod(s) < 0:
            F = F[:, [0, 2, 1]]
    R = np.eye(3) if R is None else np.asarray(R, float)
    P2 = P @ R.T + np.asarray(t, float)
    N2 = N @ R.T
    if np.linalg.det(R) < 0:
        F = F[:, [0, 2, 1]]
    return P2, N2, F


def mirror_x(mesh):
    return xf(mesh, s=(-1, 1, 1))


def merge(*meshes):
    Ps, Ns, Fs, base = [], [], [], 0
    for P, N, F in meshes:
        Ps.append(P)
        Ns.append(N)
        Fs.append(F + base)
        base += len(P)
    return np.concatenate(Ps), np.concatenate(Ns), np.concatenate(Fs)


def ring_se(a, b, n=2.0, k=16, cx=0.0, cz=0.0, y=0.0, folds=0, famp=0.0, fphase=0.0, ywave=None, phase=0.0):
    """a superellipse ring in the xz plane (half widths a along x, b along z); folds: radial ripples (cloth)"""
    th = phase + 2 * math.pi * np.arange(k) / k
    c, s = np.cos(th), np.sin(th)
    e = 2.0 / n
    x = a * np.sign(c) * np.abs(c) ** e
    z = b * np.sign(s) * np.abs(s) ** e
    if folds:
        f = 1 + famp * np.cos(folds * th + fphase)
        x, z = x * f, z * f
    yy = np.full(k, float(y)) if ywave is None else y + ywave(th)
    return np.stack([cx + x, yy, cz + z], 1)


def loft_y(st, k=16, n=2.4, mode="smooth", cap0=True, cap1=True, **kw):
    """stations (y, a, b[, cx, cz]) or dicts: a ring each (a <= 0: a pole), lofted up y"""
    rings = []
    for s in st:
        if isinstance(s, dict):
            d = dict(kw)
            d.update(s)
            y, a, b = d.pop("y"), d.pop("a"), d.pop("b")
            cx, cz = d.pop("cx", 0.0), d.pop("cz", 0.0)
            nn = d.pop("n", n)
        else:
            y, a, b = s[0], s[1], s[2]
            cx = s[3] if len(s) > 3 else 0.0
            cz = s[4] if len(s) > 4 else 0.0
            nn, d = n, dict(kw)
        if a <= 0:
            rings.append(np.array([[cx, y, cz]]))
        else:
            rings.append(ring_se(a, b, nn, k, cx, cz, y, **d))
    return sweep(rings, mode, cap0, cap1)


def pbox(c, h, bev=0.01, n=7.0, k=12, axis="y"):
    """a box with rounded edges along `axis` and chamfered ends: c centre, h half sizes (x, y, z)"""
    c, h = np.asarray(c, float), np.asarray(h, float)
    perm = {"y": (0, 1, 2), "x": (1, 0, 2), "z": (0, 2, 1)}[axis]
    hl = h[list(perm)]   # (across a, along, across b)
    bev = min(bev, hl[1] * 0.45, hl[0] * 0.45, hl[2] * 0.45)
    ia, ib = hl[0] - bev, hl[2] - bev
    m = loft_y([(-hl[1], ia, ib), (-hl[1] + bev, hl[0], hl[2]), (hl[1] - bev, hl[0], hl[2]), (hl[1], ia, ib)], k=k, n=n, mode="around")
    if axis == "x":
        m = xf(m, np.array([[0, 1, 0], [1, 0, 0], [0, 0, 1]], float))
    elif axis == "z":
        m = xf(m, np.array([[1, 0, 0], [0, 0, 1], [0, 1, 0]], float))
    return xf(m, t=c)


def tube(p0, p1, prof, k=12, n=2.0, mode="smooth", twist=0.0):
    """a loft along p0 -> p1: prof = [(t 0..1, ra, rb)], ra across the axis toward +X (or +Z for an x-axis tube)"""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    d = p1 - p0
    L = np.linalg.norm(d)
    m = loft_y([(t * L, ra, rb) for t, ra, rb in prof], k=k, n=n, mode=mode, phase=twist)
    dn = d / L
    ax = np.cross([0, 1.0, 0], dn)
    if np.linalg.norm(ax) < 1e-9:
        R = np.eye(3) if dn[1] > 0 else rot((1, 0, 0), math.pi)
    else:
        R = rot(ax, math.acos(max(-1.0, min(1.0, dn[1]))))
    return xf(m, R, p0)


def ellipsoid(c, r, k=14, nv=8):
    prof = []
    for i in range(nv + 1):
        a = -math.pi / 2 + math.pi * i / nv
        prof.append((math.sin(a), math.cos(a) if 0 < i < nv else 0.0))
    return loft_y([(c[1] + t * r[1], s * r[0], s * r[2], c[0], c[2]) for t, s in prof], k=k, n=2.0)


def arc_poly(a, b, th0, th1, thick, k=10, n=2.4):
    """an arc slab's outline in xz: the superellipse (a, b) from angle th0 to th1 (front -Z is -pi/2), thick inward"""
    th = np.linspace(th0, th1, k)
    e = 2.0 / n

    def pt(aa, bb):
        c, s = np.cos(th), np.sin(th)
        return np.stack([aa * np.sign(c) * np.abs(c) ** e, bb * np.sign(s) * np.abs(s) ** e], 1)

    outer = pt(a, b)
    inner = pt(max(a - thick, 0.001), max(b - thick, 0.001))[::-1]
    return np.concatenate([outer, inner])


def shell(st, th0, th1, thick=0.012, k=10, n=2.4, mode="smooth", cx=0.0, cz=0.0):
    """a curved plate wrapping the body between angles th0..th1: stations (y, a, b[, cz]) of its outer surface"""
    rings = []
    for s in st:
        y, a, b = s[0], s[1], s[2]
        oz = s[3] if len(s) > 3 else cz
        p = arc_poly(a, b, th0, th1, thick, k, n)
        rings.append(np.stack([cx + p[:, 0], np.full(len(p), y), oz + p[:, 1]], 1))
    return sweep(rings, mode)


def extrude_y(poly_xz, y0, y1, bevel=0.0):
    poly = np.asarray(poly_xz, float)

    def ring(y, pp):
        return np.stack([pp[:, 0], np.full(len(pp), y), pp[:, 1]], 1)

    if bevel > 0:
        ins = poly_offset(poly, bevel)
        rings = [ring(y0, ins), ring(y0 + bevel, poly), ring(y1 - bevel, poly), ring(y1, ins)]
    else:
        rings = [ring(y0, poly), ring(y1, poly)]
    return sweep(rings, "around")


def front_plate(poly_xy, z, depth, bevel=0.004, bend=0.0):
    """a flat outline (x, y points) as a plate facing -Z at z (its front face), depth thick, its sides bent back
    (bend: z += bend * x^2)"""
    m = extrude_y(np.asarray(poly_xy, float), 0.0, depth, bevel)   # (outline in x-z, thickness along y)
    P, N, F = xf(m, np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float))   # y -> -z: the plate's front at z = -depth
    P = P + np.array([0, 0, z + depth])
    if bend:
        P = P.copy()
        P[:, 2] += bend * P[:, 0] ** 2
    return P, N, F


def chamfer_rect(w, h, c):
    return [(w, -h + c), (w, h - c), (w - c, h), (-w + c, h), (-w, h - c), (-w, -h + c), (-w + c, -h), (w - c, -h)]


def strap_path(pts, w, t, k=4):
    """a flat strap (w wide, t thick) through 3D points (Catmull-Rom), its width along each point's outward normal x up"""
    pts = [np.asarray(p, float) for p in pts]
    n = max(6, 3 * len(pts))

    def cat(u):
        m = len(pts) - 1
        x = min(max(u, 0.0), 1.0) * m
        i = min(int(x), m - 1)
        f = x - i
        p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, m)]
        return 0.5 * ((2 * p1) + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f + (-p0 + 3 * p1 - 3 * p2 + p3) * f ** 3)

    path = [cat(u) for u in np.linspace(0, 1, n)]
    rings = []
    for i, p in enumerate(path):
        T = _norm(path[min(i + 1, n - 1)] - path[max(i - 1, 0)])
        out = np.array([p[0], 0.0, p[2]])
        out = _norm(out) if np.linalg.norm(out) > 1e-6 else np.array([0, 0, -1.0])
        U = _norm(out - T * np.dot(out, T))   # thickness: outward
        V = np.cross(T, U)                     # width
        rings.append(np.array([p + U * t + V * w, p + U * t - V * w, p - V * w, p + V * w]))
    return sweep(rings, "around")


def torus_y(c, R, r, a=None, b=None, k=16, kv=6):
    """a ring around the y axis (a belt roll, a hat band): radii a / b of the centre line (default R), tube r"""
    a = R if a is None else a
    b = R if b is None else b
    G = np.zeros((k, kv, 3))
    for i in range(k):
        t = 2 * math.pi * i / k
        d = np.array([a * math.cos(t), 0, b * math.sin(t)])
        dn = _norm(d)
        for j in range(kv):
            s = 2 * math.pi * j / kv
            G[i, j] = np.asarray(c, float) + d + (math.cos(s) * dn + math.sin(s) * np.array([0, 1.0, 0])) * r
    P = G.reshape(-1, 3)
    F = []
    for i in range(k):
        for j in range(kv):
            p, q = i * kv + j, i * kv + (j + 1) % kv
            rr, w = ((i + 1) % k) * kv + (j + 1) % kv, ((i + 1) % k) * kv + j
            F += [(p, q, rr), (p, rr, w)]
    return finish(P, F)


# ---------------------------------------------------------------------------------------------------------------
# a character: parts (mesh, material, look, bone) and its joints

class Rig:
    def __init__(self, hipY=0.90, hipX=0.12, kneeY=0.48, ankleY=0.11, neckY=1.45, waistY=None, animated=True):
        self.hipY, self.hipX, self.kneeY, self.ankleY, self.neckY = hipY, hipX, kneeY, ankleY, neckY
        self.waistY = hipY if waistY is None else waistY
        self.animated = animated

    def pivot(self, bone):
        return {
            "root": (0, 0, 0), "pelvis": (0, self.hipY, 0), "torso": (0, self.waistY, 0), "head": (0, self.neckY, 0),
            "thigh_l": (-self.hipX, self.hipY, 0), "thigh_r": (self.hipX, self.hipY, 0),
            "shin_l": (-self.hipX, self.kneeY, 0), "shin_r": (self.hipX, self.kneeY, 0),
            "foot_l": (-self.hipX, self.ankleY, 0), "foot_r": (self.hipX, self.ankleY, 0)}[bone]


class Char:
    def __init__(self, name, desc, rig):
        self.name, self.desc, self.rig = name, desc, rig
        self.parts = []   # [mesh, material, look, bone]

    def add(self, mesh, mat, look, bone="torso"):
        assert bone in BONES, bone
        self.parts.append([mesh, mat, look, bone])
        return self

    def sym(self, fn):
        """fn(sd) for both sides: sd -1 the model's left (-X) bone suffix _l, +1 the right"""
        for sd in (-1, 1):
            fn(sd, "_l" if sd < 0 else "_r")

    def tris(self):
        return sum(len(p[0][2]) for p in self.parts)

    def widen(self, sx, sz):
        """every part below the head (and the hips' spacing) wider by sx, deeper by sz: the bodies fill the hitbox
        about as the box models did, the heads keep their size"""
        knee, lim = 0.30, R_BODY - MARGIN - 0.002
        for p in self.parts:
            if p[3] == "head":
                continue
            P, N, F = xf(p[0], s=(sx, 1.0, sz))
            r = np.hypot(P[:, 0], P[:, 2])
            # past `knee` the radius eases toward the hitbox's edge (tanh): wide parts (shoulders, a hem) fill the
            # body cylinder to its edge without leaving it
            rr = np.where(r > knee, knee + (lim - knee) * np.tanh((r - knee) / (lim - knee)), r)
            k = np.where(r > 1e-9, rr / np.maximum(r, 1e-9), 1.0)
            P = P.copy()
            P[:, 0] *= k
            P[:, 2] *= k
            p[0] = (P, N, F)
        self.rig.hipX *= sx
        return self


# ---------------------------------------------------------------------------------------------------------------
# baked shading

def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _propagate(n, a, b):
    lab = np.arange(n)
    if len(a) == 0:
        return lab
    for _ in range(400):
        old = lab.copy()
        m = np.minimum(lab[a], lab[b])
        np.minimum.at(lab, a, m)
        np.minimum.at(lab, b, m)
        lab = lab[lab]
        lab = lab[lab]
        if (lab == old).all():
            break
    return lab


def facets(P, N, F):
    """(gen_weapons_realistic_dark.py) per triangle a facet (coplanar / connected faces): its width, length, extent"""
    T = len(F)
    q = np.round(P / 1e-6).astype(np.int64)
    _, pid = np.unique(q, axis=0, return_inverse=True)
    pid = pid.reshape(-1)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    area = 0.5 * np.linalg.norm(fn, axis=1)
    fn = rows(fn)
    vt = np.repeat(np.arange(T), 3)
    vi = F.reshape(-1)
    order = np.argsort(vi, kind="stable")
    vs, ts = vi[order], vt[order]
    same = vs[1:] == vs[:-1]
    la, lb = [ts[1:][same]], [ts[:-1][same]]
    E = np.stack([pid[F[:, [0, 1]]], pid[F[:, [1, 2]]], pid[F[:, [2, 0]]]], 1)
    E = np.sort(E, 2).reshape(-1, 2)
    et = np.repeat(np.arange(T), 3)
    key = E[:, 0] * (pid.max() + 1) + E[:, 1]
    o = np.argsort(key, kind="stable")
    ks, tt = key[o], et[o]
    s = ks[1:] == ks[:-1]
    a, b = tt[1:][s], tt[:-1][s]
    cop = np.einsum("ij,ij->i", fn[a], fn[b]) > 0.9995
    la.append(a[cop])
    lb.append(b[cop])
    lab = _propagate(T, np.concatenate(la), np.concatenate(lb))
    _, lab = np.unique(lab, return_inverse=True)
    nf = lab.max() + 1
    A = np.bincount(lab, area, nf)
    fkey = lab[et] * (key.max() + 1) + key
    uk, inv, cnt = np.unique(fkey, return_inverse=True, return_counts=True)
    elen = np.linalg.norm(P[F[:, [1, 2, 0]].reshape(-1)] - P[F.reshape(-1)], axis=1)
    bnd = cnt[inv] == 1
    Per = np.bincount(lab[et][bnd], elen[bnd], nf)
    disc = (Per / 2) ** 2 - 4 * A
    width = np.where(disc > 0, (Per / 2 - np.sqrt(np.maximum(disc, 0))) / 2, np.sqrt(A))
    width = np.where(Per <= 0, np.sqrt(A) * 4, width)
    length = np.where(width > 0, A / np.maximum(width, 1e-9), 0)
    vl = np.full(len(P), -1)
    vl[F.reshape(-1)] = np.repeat(lab, 3)
    ymin = np.full(nf, 1e9)
    ymax = np.full(nf, -1e9)
    u = vl >= 0
    np.minimum.at(ymin, vl[u], P[u, 1])
    np.maximum.at(ymax, vl[u], P[u, 1])
    nv = np.bincount(vl[u], minlength=nf)
    return dict(vlab=vl, area=A, width=width, length=length, ymin=ymin, ymax=ymax, nv=nv)


def _hemi_dirs():
    out = []
    for el, n in ((20, 8), (45, 7), (72, 4)):
        e = math.radians(el)
        for i in range(n):
            a = 2 * math.pi * (i + 0.5 * (el == 45)) / n
            out.append((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
    return np.array(out)


HEMI = _hemi_dirs()


def voxel_ao(meshes, vox=0.008, reach=0.14):
    """(gen_weapons_realistic_dark.py, at body scale) per mesh, per vertex: 1 open .. 0 occluded, by rays marched
    through a voxel grid of every mesh's surface"""
    allP = np.concatenate([m[0] for m in meshes])
    lo = allP.min(0) - reach - 0.03
    hi = allP.max(0) + reach + 0.03
    dims = np.ceil((hi - lo) / vox).astype(int) + 1
    occ = np.zeros(dims, bool)
    for P, N, F in meshes:
        a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
        ar = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
        n = np.maximum(3, np.ceil(ar / (vox * vox * 0.25)).astype(int))
        ti = np.repeat(np.arange(len(F)), n)
        j = np.arange(len(ti)) - np.repeat(np.cumsum(n) - n, n)
        u = (0.5 + j * 0.7548776662) % 1.0
        v = (0.5 + j * 0.5698402910) % 1.0
        flip = u + v > 1
        u, v = np.where(flip, 1 - u, u), np.where(flip, 1 - v, v)
        S = a[ti] + (b[ti] - a[ti]) * u[:, None] + (c[ti] - a[ti]) * v[:, None]
        S = np.concatenate([S, a, b, c])
        idx = np.floor((S - lo) / vox).astype(int)
        occ[idx[:, 0], idx[:, 1], idx[:, 2]] = True
    steps = np.arange(1, int(reach / (vox * 1.1)) + 1) * vox * 1.1
    out = []
    for P, N, F in meshes:
        Nn = rows(N)
        t1 = rows(np.cross(Nn, np.where(np.abs(Nn[:, :1]) < 0.9, [[1, 0, 0]], [[0, 1, 0]])))
        t2 = np.cross(Nn, t1)
        occl = np.zeros(len(P))
        wsum = 0.0
        for dx, dy, dz in HEMI:
            d = t1 * dx + t2 * dy + Nn * dz
            hit = np.zeros(len(P))
            for s in steps:
                Q = P + Nn * (1.6 * vox) + d * s
                idx = np.clip(np.floor((Q - lo) / vox).astype(int), 0, dims - 1)
                h = occ[idx[:, 0], idx[:, 1], idx[:, 2]]
                hit = np.maximum(hit, h * (1.0 - s / (reach * 1.15)))
            occl += dz * hit
            wsum += dz
        out.append(1.0 - np.clip(occl / wsum, 0, 1))
    return out


def shade(P, N, F, look, ao):
    """the D2 polish, view-independent, per vertex"""
    if look.glow:
        return np.tile(np.clip(look.diffuse, 0, 16), (len(P), 1))
    fa = facets(P, N, F)
    vl = fa["vlab"]
    used = vl >= 0
    vl = np.where(used, vl, 0)
    Nn = rows(N)
    up_ = _smooth(-0.05, 0.75, Nn[:, 1])[:, None]
    dn_ = _smooth(0.0, -0.6, Nn[:, 1])[:, None]
    envc = (HOR * (1 - up_ - dn_) + SKY * up_ + GND * dn_) * look.env
    # a grazing (Fresnel-like) lift where the surface turns away from the horizontal views most players see it from
    side = 1 - np.abs(Nn[:, 1])
    fres = look.f0 + (1 - look.f0) * 0.25 * (1 - side) ** 3
    kl = np.clip(Nn @ LK, 0, 1)
    keyc = np.array((1.0, 1.0, 1.04))[None, :] * (look.key * (kl ** look.kp + 0.15 * kl ** 4))[:, None]
    kb = np.clip(Nn @ LB, 0, 1)
    keyc = keyc + BOUNCE[None, :] * (look.key * 0.5 * kb ** max(4, look.kp * 0.5))[:, None]
    # worn edges: narrow, long facets (bevels, chamfers, plate edges) where open
    w = fa["width"][vl]
    L = fa["length"][vl]
    strip = _smooth(0.034, 0.009, w) * np.clip((L / np.maximum(w, 1e-9) - 2.0) / 3.0, 0, 1)
    # curvature along the mesh's own edges (1/m: + convex, - a crease): smooth parts' rounded edges wear and catch the
    # rim like the bevels do, their creases darken (cavity)
    ei = np.concatenate([F[:, 0], F[:, 1], F[:, 2], F[:, 1], F[:, 2], F[:, 0]])
    ej = np.concatenate([F[:, 1], F[:, 2], F[:, 0], F[:, 0], F[:, 1], F[:, 2]])
    d = P[ej] - P[ei]
    kk = np.einsum("ij,ij->i", Nn[ej] - Nn[ei], d) / np.maximum(np.einsum("ij,ij->i", d, d), 1e-12)
    curv = np.bincount(ei, kk, len(P)) / np.maximum(np.bincount(ei, None, len(P)), 1)
    cv, cc = _smooth(6.0, 35.0, curv), _smooth(-4.0, -30.0, curv)
    strip = np.maximum(strip, cv * 0.8)
    up = _smooth(0.2, 0.85, Nn[:, 1])
    aom = np.bincount(vl[used], ao[used], len(fa["area"])) / np.maximum(fa["nv"], 1)
    spacing = np.sqrt(fa["area"] / np.maximum(fa["nv"], 1))[vl]
    al = np.clip((spacing - 0.03) / 0.06, 0, 1)
    aov = ao * (1 - al) + aom[vl] * al
    aof = (1 - look.ao * (1 - aov)) ** 1.5 * (1 - 0.45 * look.ao * cc)
    open_ = _smooth(0.55, 0.9, aov)
    wear = look.wear * strip * open_
    ext = fa["ymax"][vl] - fa["ymin"][vl]
    hy = np.where(ext > 0.004, (P[:, 1] - fa["ymin"][vl]) / np.maximum(ext, 1e-9), 1.0)
    gradf = 1 + look.grad * side * (hy - 0.5) * 2
    dif = look.diffuse[None, :] * (1 - wear[:, None]) + look.worn[None, :] * wear[:, None]
    spec = look.spec[None, :] * (fres[:, None] + wear[:, None] * 0.35)
    c = dif * gradf[:, None] + spec * (envc + keyc) * gradf[:, None]
    c = c + RIM[None, :] * (1.7 * look.rim * strip * (0.35 + 0.65 * up) * open_)[:, None]
    c = c * aof[:, None]
    return np.clip(c, 0, 16)


def lift(c, mat):
    """the visibility floor: a vertex darker than FLOOR (luminance) scaled up to it, hue kept"""
    if mat.lower().startswith(FLOOR_EXEMPT):
        return c
    lum = c @ np.array((0.2126, 0.7152, 0.0722))
    k = np.where(lum < FLOOR, FLOOR / np.maximum(lum, 1e-4), 1.0)
    return np.clip(np.where((lum < 1e-4)[:, None], FLOOR, c * k[:, None]), 0, 16)


def bake(ch):
    """per part: its colours (n, 3), with the AO of the whole model standing, lifted to the visibility floor"""
    meshes = [p[0] for p in ch.parts]
    ao = voxel_ao(meshes)
    return [lift(shade(*p[0], p[2], a), p[1]) for p, a in zip(ch.parts, ao)]


def leg_stripes(ch, k=1.15):
    """a team stripe down the outside of each thigh and shin (every walking model alike: a team silhouette line that
    reads at distance), on the outermost surface of that bone's parts"""
    for bone in ("thigh_l", "shin_l", "thigh_r", "shin_r"):
        sd = -1 if bone.endswith("_l") else 1
        P = np.concatenate([p[0][0] for p in ch.parts if p[3] == bone])
        y0, y1 = (ch.rig.kneeY + 0.07, ch.rig.hipY - 0.06) if bone.startswith("thigh") else (ch.rig.ankleY + 0.16, ch.rig.kneeY - 0.06)
        band = P[(P[:, 1] > y0) & (P[:, 1] < y1)]
        if len(band) == 0:
            continue
        x = float((band[:, 0] * sd).max()) * sd + sd * 0.002
        poly = [(x + 0.005, -0.016), (x + 0.005, 0.016), (x - 0.005, 0.016), (x - 0.005, -0.016)]
        ch.add(extrude_y(poly, y0, y1), "team_stripe", team(k, 0.3), bone)   # (a plain box: 12 triangles)


# ---------------------------------------------------------------------------------------------------------------
# glTF out: a node per bone (anim_<bone>, translation = the pivot, vertices relative to it), one primitive per material

def write_gltf(ch, cols, path):
    bin_ = bytearray()
    views, accs, meshes, nodes, mats, mat_index = [], [], [], [], [], {}

    def put(arr, target):
        b = arr.tobytes()
        off = len(bin_)
        bin_.extend(b)
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(b), "target": target})
        return len(views) - 1

    for bone in BONES:
        sel = [(p, c) for p, c in zip(ch.parts, cols) if p[3] == bone]
        if not sel:
            continue
        piv = np.array(ch.rig.pivot(bone), float)
        order = []
        for p, _ in sel:
            if p[1] not in order:
                order.append(p[1])
        prims = []
        for mname in order:
            Ps, Ns, Cs, Fs, base = [], [], [], [], 0
            for (mesh, mat, look, b), c in sel:
                if mat != mname:
                    continue
                P, N, F = mesh
                Ps.append(P - piv)
                Ns.append(N)
                Cs.append(c)
                Fs.append(F + base)
                base += len(P)
            P = np.round(np.concatenate(Ps), 6).astype("<f4")
            N = np.round(rows(np.concatenate(Ns)), 5).astype("<f4")
            C = np.round(np.concatenate(Cs), 5).astype("<f4")
            F = np.concatenate(Fs).astype("<u4").reshape(-1)
            a0 = len(accs)
            accs.append({"bufferView": put(P, 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                         "min": [float(x) for x in P.min(0)], "max": [float(x) for x in P.max(0)]})
            accs.append({"bufferView": put(N, 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
            accs.append({"bufferView": put(C, 34962), "componentType": 5126, "count": len(C), "type": "VEC3"})
            accs.append({"bufferView": put(F, 34963), "componentType": 5125, "count": len(F), "type": "SCALAR"})
            if mname not in mat_index:
                mat_index[mname] = len(mats)
                mats.append({"name": mname, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1}})
            prims.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "COLOR_0": a0 + 2}, "indices": a0 + 3,
                          "material": mat_index[mname], "mode": 4})
        nname = ch.name if bone == "root" else "anim_" + bone
        node = {"name": nname, "mesh": len(meshes)}
        if bone != "root":
            node["translation"] = [round(float(v), 6) for v in piv]
        meshes.append({"name": nname, "primitives": prims})
        nodes.append(node)
    r = ch.rig
    doc = {
        "asset": {"version": "2.0", "generator": "quark tools/models/gen_players_realistic_dark.py"},
        "extras": {"style": "realistic dark", "player": ch.name, "description": ch.desc,
                   "rig": {"hipY": r.hipY, "hipX": r.hipX, "kneeY": r.kneeY, "ankleY": r.ankleY, "neckY": r.neckY, "waistY": r.waistY}},
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": meshes, "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(bin_)).decode("ascii")}],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    txt = json.dumps(doc, separators=(",", ":")) + "\n"
    with open(path, "w") as f:
        f.write(txt)
    return len(txt)


def read_back(path):
    """the file's nodes as (name, pivot, [(material, P, N, C, F)]) (a check that what was written reads)"""
    with open(path) as f:
        doc = json.load(f)
    raw = base64.b64decode(doc["buffers"][0]["uri"].split(",", 1)[1])

    def acc(i, dt, w):
        a = doc["accessors"][i]
        v = doc["bufferViews"][a["bufferView"]]
        return np.frombuffer(raw, dt, a["count"] * w, v.get("byteOffset", 0)).reshape(-1, w) if w > 1 else np.frombuffer(raw, dt, a["count"], v.get("byteOffset", 0))

    out = []
    for nd in doc["nodes"]:
        t = np.array(nd.get("translation", [0, 0, 0]), float)
        prims = []
        for pr in doc["meshes"][nd["mesh"]]["primitives"]:
            at = pr["attributes"]
            prims.append((doc["materials"][pr["material"]]["name"], acc(at["POSITION"], "<f4", 3).astype(float),
                          acc(at["NORMAL"], "<f4", 3).astype(float), acc(at["COLOR_0"], "<f4", 3).astype(float),
                          acc(pr["indices"], "<u4", 1).astype(np.int64).reshape(-1, 3)))
        out.append((nd["name"], t, prims))
    return doc, out


# ---------------------------------------------------------------------------------------------------------------
# posing (a port of models.cpp playerPose: the previews walk the way the game does)

ANIM = dict(walkStride=1.7, runStride=3.4, walkSpeed=2.5, runSpeed=7.0, walkSwing=0.38, runSwing=0.52, walkKnee=0.55,
            runKnee=1.25, stanceKnee=0.08, runLean=0.12, airThighL=0.75, airThighR=0.35, airKneeL=1.45, airKneeR=0.95,
            airLean=0.1, headPitch=0.3, torsoPitch=0.1, pitchMax=0.35, legTurnMax=1.05)


def _pivx(y, c, s):
    M = np.eye(4)
    M[1, 1], M[2, 1], M[1, 2], M[2, 2] = c, s, -s, c
    M[1, 3], M[2, 3] = y - c * y, -s * y
    return M


def pose(rig, phase=0.0, speed=0.0, legYaw=0.0, air=0.0, pitch=0.0):
    """bone -> 4x4 (model space standing -> posed), as playerPose; footHalf 0 (the models have feet)"""
    out = {b: np.eye(4) for b in BONES}
    if not rig.animated:
        return out
    k = ANIM
    clamp = lambda v, a, b: max(a, min(b, v))
    lerp = lambda a, b, t: a + (b - a) * t
    sp = max(0.0, speed)
    air = clamp(air, 0, 1)
    gnd = 1 - air
    run = clamp((sp - k["walkSpeed"]) / (k["runSpeed"] - k["walkSpeed"]), 0, 1)
    amt = clamp(sp / k["walkSpeed"], 0, 1)
    ph = phase * 2 * math.pi
    sw = lerp(k["walkSwing"], k["runSwing"], run) * amt
    kn = lerp(k["walkKnee"], k["runKnee"], run) * amt
    sn, cs = math.sin(ph), math.cos(ph)
    thL, thR = sw * sn, -sw * sn
    knL = kn * max(0.0, cs) + k["stanceKnee"] * amt
    knR = kn * max(0.0, -cs) + k["stanceKnee"] * amt
    thL, thR = lerp(thL, k["airThighL"], air), lerp(thR, k["airThighR"], air)
    knL, knR = lerp(knL, k["airKneeL"], air), lerp(knR, k["airKneeR"], air)
    cTL, sTL, cTR, sTR = math.cos(thL), math.sin(thL), math.cos(thR), math.sin(thR)
    cKL, sKL, cKR, sKR = math.cos(knL), math.sin(knL), math.cos(knR), math.sin(knR)
    cSL, sSL = cTL * cKL + sTL * sKL, sTL * cKL - cTL * sKL
    cSR, sSR = cTR * cKR + sTR * sKR, sTR * cKR - cTR * sKR
    lt, ls = rig.hipY - rig.kneeY, rig.kneeY - rig.ankleY
    lowL = rig.hipY - lt * cTL - ls * cSL - rig.ankleY
    lowR = rig.hipY - lt * cTR - ls * cSR - rig.ankleY
    drop = max(min(lowL, lowR), -0.15) * gnd
    lean = k["runLean"] * run * amt * gnd + k["airLean"] * air
    tp = clamp(pitch * k["torsoPitch"], -k["pitchMax"], k["pitchMax"])
    hp = clamp(pitch * k["headPitch"], -k["pitchMax"], k["pitchMax"]) - tp * 0.5
    ly = clamp(legYaw, -k["legTurnMax"], k["legTurnMax"])
    pel = np.eye(4)
    c, s = math.cos(ly), math.sin(ly)
    pel[0, 0], pel[2, 0], pel[0, 2], pel[2, 2] = c, -s, s, c
    pel[1, 3] = -drop
    out["pelvis"] = pel
    out["thigh_l"] = pel @ _pivx(rig.hipY, cTL, sTL)
    out["thigh_r"] = pel @ _pivx(rig.hipY, cTR, sTR)
    out["shin_l"] = out["thigh_l"] @ _pivx(rig.kneeY, cKL, -sKL)
    out["shin_r"] = out["thigh_r"] @ _pivx(rig.kneeY, cKR, -sKR)
    out["foot_l"] = out["shin_l"] @ _pivx(rig.ankleY, cSL, -sSL)
    out["foot_r"] = out["shin_r"] @ _pivx(rig.ankleY, cSR, -sSR)
    t = _pivx(rig.waistY, math.cos(tp - lean), math.sin(tp - lean))
    t[1, 3] -= drop
    out["torso"] = t
    out["head"] = t @ _pivx(rig.neckY, math.cos(hp), math.sin(hp))
    return out


# ---------------------------------------------------------------------------------------------------------------
# previews: a small rasterizer (gen_weapons_realistic_dark.py's, per-vertex colours, the game's model light)

PREVIEW_TEAM = np.array((0.95, 0.30, 0.06))   # a sample team colour (linear)


class Cam:
    def __init__(self, eye_dir, scale, center, W, H):
        f = -_norm(eye_dir)
        self.r = _norm(np.cross(f, [0, 1, 0]))
        self.u = np.cross(self.r, f)
        self.f, self.scale, self.center, self.W, self.H = f, scale, center, W, H

    def project(self, P):
        x, y = P @ self.r, P @ self.u
        cx, cy = self.center
        return np.stack([self.W / 2 + self.scale * (x - cx), self.H / 2 - self.scale * (y - cy)], -1), P @ (-self.f)


def scene_tris(items):
    """items: (P, N, F, material, colours (n, 3) or (3,)) -> per-triangle arrays"""
    Ps, Ns, Cs, E = [], [], [], []
    for P, N, F, mat, col in items:
        l = mat.lower()
        c = col[F] if np.ndim(col) == 2 else np.tile(np.asarray(col, float), (len(F), 3, 1))
        if l.startswith("team"):
            c = c * PREVIEW_TEAM
        Ps.append(P[F])
        Ns.append(N[F])
        Cs.append(c)
        E.append(np.full(len(F), 1.0 if l.startswith("glow") else 0.0))
    return np.concatenate(Ps), np.concatenate(Ns), np.concatenate(Cs), np.concatenate(E)


def rasterize(items, cam, bg_top=(0.62, 0.64, 0.67), bg_bot=(0.44, 0.46, 0.50), dim=1.0):
    P, N, C, E = scene_tris(items)
    W, H = cam.W, cam.H
    S, D = cam.project(P.reshape(-1, 3))
    S, D = S.reshape(-1, 3, 2), D.reshape(-1, 3)
    fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    vis = fn @ (-cam.f) > 0
    depth = np.full((H, W), -np.inf)
    nb = np.zeros((H, W, 3))
    cb = np.zeros((H, W, 3))
    eb = np.zeros((H, W))
    for t in np.nonzero(vis)[0]:
        (x0, y0), (x1, y1), (x2, y2) = S[t]
        bx0, bx1 = max(int(math.floor(min(x0, x1, x2))), 0), min(int(math.ceil(max(x0, x1, x2))), W - 1)
        by0, by1 = max(int(math.floor(min(y0, y1, y2))), 0), min(int(math.ceil(max(y0, y1, y2))), H - 1)
        if bx0 > bx1 or by0 > by1:
            continue
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-12:
            continue
        px, py = np.meshgrid(np.arange(bx0, bx1 + 1) + 0.5, np.arange(by0, by1 + 1) + 0.5)
        w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
        w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
        w2 = 1 - w0 - w1
        msk = (w0 >= -1e-9) & (w1 >= -1e-9) & (w2 >= -1e-9)
        z = w0 * D[t, 0] + w1 * D[t, 1] + w2 * D[t, 2]
        sub = depth[by0:by1 + 1, bx0:bx1 + 1]
        msk &= z > sub
        if not msk.any():
            continue
        sub[msk] = z[msk]
        a0, a1, a2 = w0[msk][:, None], w1[msk][:, None], w2[msk][:, None]
        nb[by0:by1 + 1, bx0:bx1 + 1][msk] = a0 * N[t, 0] + a1 * N[t, 1] + a2 * N[t, 2]
        cb[by0:by1 + 1, bx0:bx1 + 1][msk] = a0 * C[t, 0] + a1 * C[t, 1] + a2 * C[t, 2]
        eb[by0:by1 + 1, bx0:bx1 + 1][msk] = E[t]
    covered = np.isfinite(depth)
    n = rows(nb)
    sun = _norm((0.45, 0.80, -0.35))
    light = 0.55 * (0.6 + 0.25 * n[..., 1]) + 0.75 * np.clip(n @ sun, 0, None)
    shade_ = ((light * 0.7 + 0.3) * dim)[..., None]
    col = np.where(eb[..., None] > 0, cb * (0.85 + 0.25 * shade_), cb * shade_)
    yy = np.linspace(0, 1, H)[:, None, None]
    bg = (np.array(bg_top) * (1 - yy) + np.array(bg_bot) * yy) ** 2.2
    img = np.where(covered[..., None], col, np.broadcast_to(bg, col.shape))
    e = np.zeros((H, W), bool)
    e[:, :-1] |= covered[:, :-1] != covered[:, 1:]
    e[:-1, :] |= covered[:-1, :] != covered[1:, :]
    e &= covered
    img = np.where(e[..., None], np.array([0.01, 0.01, 0.012]), img)
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    return (img * 255 + 0.5).astype(np.uint8)


def posed_items(ch, cols, pz):
    items = []
    for (mesh, mat, look, bone), c in zip(ch.parts, cols):
        P, N, F = mesh
        M = pz[bone]
        items.append((P @ M[:3, :3].T + M[:3, 3], N @ M[:3, :3].T, F, mat, c))
    return items


def old_items(path):
    """a box model export's boxes (the built-in model) as preview items, and its bones"""
    with open(path) as f:
        doc = json.load(f)
    boxes = doc["meshes"][0]["extras"]["quark"]["boxes"]
    out = []
    for bx in boxes:
        c, h = np.array(bx["c"], float), np.array(bx["h"], float)
        corners = np.array([[i & 1, (i >> 1) & 1, (i >> 2) & 1] for i in range(8)], float) * 2 - 1
        P = c + corners * h
        F = np.array([[0, 2, 3], [0, 3, 1], [4, 5, 7], [4, 7, 6], [0, 1, 5], [0, 5, 4], [2, 6, 7], [2, 7, 3], [0, 4, 6], [0, 6, 2], [1, 3, 7], [1, 7, 5]])
        m = finish(P, F, flat=True)
        slot = bx.get("slot", "body")
        out.append((m, "team" if slot == "team" else "glow" if slot == "glow" else "body", np.array(bx["color"], float), bx.get("bone", "root")))
    return out


def old_rig(path):
    """models.cpp playerRig on a box export (the old model's walk)"""
    with open(path) as f:
        boxes = json.load(f)["meshes"][0]["extras"]["quark"]["boxes"]
    have = {bx.get("bone", "root") for bx in boxes}
    th = [bx for bx in boxes if bx.get("bone") in ("thigh_l", "thigh_r")]
    if not {"thigh_l", "shin_l", "thigh_r", "shin_r"} <= have or not th:
        return Rig(animated=False)
    top = max(b["c"][1] + b["h"][1] for b in th)
    bot = min(b["c"][1] - b["h"][1] for b in th)
    hx = sum(abs(b["c"][0]) for b in th) / len(th)
    heads = [b["c"][1] - b["h"][1] for b in boxes if b.get("bone") == "head"]
    ank = max([b["c"][1] + b["h"][1] for b in boxes if b.get("bone") in ("foot_l", "foot_r")] or [0])
    return Rig(hipY=top, hipX=hx, kneeY=bot, ankleY=min(ank, bot), neckY=min(heads) if heads else 1.44)


# ---------------------------------------------------------------------------------------------------------------
# the characters

def _imports():
    from characters_d2 import BUILDERS   # (tools/models/characters_d2.py: the 11 designs; it imports this file's kit)
    return BUILDERS


def check(ch):
    """errors: hitbox misses (vertices and points along every edge, standing), triangle budget, bones"""
    errs = []
    for mesh, mat, look, bone in ch.parts:
        P, N, F = mesh
        if not np.isfinite(P).all():
            errs.append("%s: non-finite vertex" % mat)
            continue
        E = np.concatenate([P[F[:, [0, 1]]], P[F[:, [1, 2]]], P[F[:, [2, 0]]]])
        pts = np.concatenate([P] + [E[:, 0] * (1 - t) + E[:, 1] * t for t in (0.25, 0.5, 0.75)])
        bad = hitbox_misses(pts)
        if bad.any():
            q = pts[bad]
            r = np.hypot(q[:, 0], q[:, 2])
            errs.append("%s (%s): %d points outside the hitbox, e.g. (%.3f, %.3f, %.3f) r %.3f" % (mat, bone, bad.sum(), q[0, 0], q[0, 1], q[0, 2], r[0]))
        if not mat.replace("_", "").isalnum():
            errs.append("%s: material name" % mat)
    assert MAX_TRIS <= 1000
    if ch.tris() > MAX_TRIS:
        errs.append("%d triangles (at most %d)" % (ch.tris(), MAX_TRIS))
    if ch.rig.animated:
        have = {p[3] for p in ch.parts}
        for b in ("thigh_l", "shin_l", "thigh_r", "shin_r"):
            if b not in have:
                errs.append("animated but nothing on %s" % b)
    return errs


def render_char(items, eye_dir, W, H, scale, ss=2, cy=0.92, bg=None, dim=1.0):
    from PIL import Image
    cam = Cam(eye_dir, scale * ss, (0.0, cy), W * ss, H * ss)
    kw = dict(bg_top=bg, bg_bot=bg) if bg else {}
    return Image.fromarray(rasterize(items, cam, dim=dim, **kw)).resize((W, H), Image.LANCZOS)


def font(size, bold=False):
    from PIL import ImageFont
    for p in (["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"] if bold else []) + ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


FRONT = (0.62, 0.22, -1.0)    # front 3/4 (from the model's right front, a little above)
BACK = (-0.62, 0.22, 1.0)
SIDE = (1.0, 0.12, -0.35)


def sheet(cells, title, cols, cw, ch_, labels, sub=None):
    from PIL import Image, ImageDraw
    pad, head, top = 10, 26, 56 if sub else 46
    nrow = (len(cells) + cols - 1) // cols
    img = Image.new("RGB", (pad + cols * (cw + pad), top + nrow * (ch_ + head + pad)), (24, 26, 30))
    d = ImageDraw.Draw(img)
    d.text((pad, 10), title, fill=(240, 240, 240), font=font(24, True))
    if sub:
        d.text((pad, 38), sub, fill=(170, 175, 185), font=font(13))
    for i, (c, lab) in enumerate(zip(cells, labels)):
        x = pad + (i % cols) * (cw + pad)
        y = top + (i // cols) * (ch_ + head + pad)
        if lab:
            d.text((x + 2, y + 4), lab, fill=(225, 228, 235), font=font(15, True))
        if c is not None:
            img.paste(c, (x, y + head))
    return img


def make_previews(chars, cols_all, scratch, out, export):
    from PIL import Image
    cw, chh, sc = 240, 470, 230.0   # px / m
    saved = []

    def save(img, name):
        p = os.path.join(scratch, name)
        img.save(p, optimize=True)
        shutil.copyfile(p, os.path.join(out, name))
        saved.append(os.path.join(out, name))

    stand = {c.name: posed_items(c, cols_all[c.name], pose(c.rig)) for c in chars}
    # lineup: front 3/4 row, back 3/4 row
    cells, labels = [], []
    for c in chars:
        cells.append(render_char(stand[c.name], FRONT, cw, chh, sc))
        labels.append("%s  %d tris" % (c.name, c.tris()))
    for c in chars:
        cells.append(render_char(stand[c.name], BACK, cw, chh, sc))
        labels.append(c.name + " (back)")
    save(sheet(cells, "D2 REALISTIC DARK: the 11 player models (front 3/4, back 3/4)", len(chars), cw, chh, labels,
               "team colour shown as orange; mesh overrides with walking parts (anim_<bone> nodes); every vertex inside the hitbox"),
         "lineup-D2-players.png")
    # close-ups: twice the size, front 3/4 (where the polish reads)
    cells, labels = [], []
    for c in chars:
        cells.append(render_char(stand[c.name], FRONT, cw * 2, chh * 2, sc * 2))
        labels.append(c.name)
    save(sheet(cells, "D2 REALISTIC DARK: close-ups (front 3/4)", 6, cw * 2, chh * 2, labels), "closeup-D2-players.png")
    # visibility: each model on a dark (sRGB 0.05) and a mid (0.35) background, the game's model light dimmed for the dark one
    cells, labels = [], []
    for bgn, bg, dim in (("dark", (0.05, 0.05, 0.055), 0.6), ("mid", (0.35, 0.36, 0.38), 1.0)):
        for c in chars:
            cells.append(render_char(stand[c.name], FRONT, cw, chh, sc, bg=bg, dim=dim))
            labels.append("%s on %s" % (c.name, bgn))
    save(sheet(cells, "Visibility: every model on a dark map (sRGB 0.05 background, light x0.6) and a mid one", len(chars), cw, chh, labels,
               "no part below sRGB 0.18 but small recesses, eyes and lenses; team stripes and trims along the silhouette"), "visibility-D2-players.png")
    # old vs new
    if export:
        cells, labels = [], []
        for c in chars:
            p = os.path.join(export, "models", "player", c.name + ".gltf")
            ob = old_items(p) if os.path.exists(p) else []
            items = [(m[0], m[1], m[2], mat, col) for m, mat, col, b in ob]
            cells.append(render_char(items, FRONT, cw, chh, sc) if items else None)
            labels.append(c.name + " old")
        for c in chars:
            cells.append(render_char(stand[c.name], FRONT, cw, chh, sc))
            labels.append(c.name + " new")
        save(sheet(cells, "Player models: built-in boxes (top) vs D2 realistic dark (bottom)", len(chars), cw, chh, labels),
             "old-vs-new-D2-players.png")
    # walking: each model mid-stride (run), then walking, from the side 3/4
    cells, labels = [], []
    for i, c in enumerate(chars):
        pz = pose(c.rig, phase=0.25, speed=7.0)
        cells.append(render_char(posed_items(c, cols_all[c.name], pz), SIDE, cw, chh, sc))
        labels.append(c.name + " run" + ("" if c.rig.animated else " (still)"))
    for i, c in enumerate(chars):
        pz = pose(c.rig, phase=0.75, speed=2.6, legYaw=0.0)
        cells.append(render_char(posed_items(c, cols_all[c.name], pz), FRONT, cw, chh, sc))
        labels.append(c.name + " walk")
    std = [c for c in chars if c.name == "STANDARD"] or chars[:1]
    for k in range(8):
        pz = pose(std[0].rig, phase=k / 8.0, speed=7.0)
        cells.append(render_char(posed_items(std[0], cols_all[std[0].name], pz), SIDE, cw, chh, sc))
        labels.append("%s phase %.3f" % (std[0].name, k / 8.0))
    cells.append(render_char(posed_items(std[0], cols_all[std[0].name], pose(std[0].rig, air=1.0)), SIDE, cw, chh, sc))
    labels.append(std[0].name + " jump")
    save(sheet(cells, "Walking: the game's gait (models.cpp playerPose) on the anim_<bone> nodes", len(chars), cw, chh, labels,
               "row 1: running 7 m/s, phase 0.25 (side 3/4); row 2: walking 2.6 m/s, phase 0.75 (front 3/4); row 3: one run cycle + the jump pose"),
         "walk-D2-players.png")
    return saved


# ---------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="/mnt/project-files/player-models")
    ap.add_argument("--scratch", default="/tmp/quark-players-d2/")
    ap.add_argument("--export", default="", help="a quark_tests exportassets folder (old models for old-vs-new)")
    ap.add_argument("--no-preview", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    sys.path.insert(0, HERE)
    builders = _imports()
    only = [s.strip().upper() for s in args.only.split(",") if s.strip()]
    os.makedirs(args.scratch, exist_ok=True)
    mdir = os.path.join(args.out, STYLE, "models", "player")
    sdir = os.path.join(args.scratch, STYLE, "models", "player")
    os.makedirs(mdir, exist_ok=True)
    os.makedirs(sdir, exist_ok=True)
    chars, cols_all, ok = [], {}, True
    for name in NAMES:
        if only and name not in only:
            continue
        ch = builders[name]()
        if ch.rig.animated:
            if getattr(ch, "stripes", True):
                leg_stripes(ch)
            ch.widen(*WIDEN)
        errs = check(ch)
        cols = bake(ch)
        sp = os.path.join(sdir, name + ".gltf")
        size = write_gltf(ch, cols, sp)
        doc, nodes = read_back(sp)
        ntri = sum(len(p[4]) for _, _, prims in nodes for p in prims)
        if ntri != ch.tris():
            errs.append("read back %d triangles, wrote %d" % (ntri, ch.tris()))
        shutil.copyfile(sp, os.path.join(mdir, name + ".gltf"))
        allP = np.concatenate([p[0][0] for p in ch.parts])
        print("%-8s %5d triangles, %2d nodes, %3d KB, x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f, max r body %.3f head %.3f" % (
            name, ch.tris(), len(nodes), size // 1024, allP[:, 0].min(), allP[:, 0].max(), allP[:, 1].min(), allP[:, 1].max(),
            allP[:, 2].min(), allP[:, 2].max(), np.hypot(allP[:, 0], allP[:, 2])[allP[:, 1] < Y_NECK].max(),
            np.hypot(allP[:, 0], allP[:, 2])[allP[:, 1] >= Y_NECK].max() if (allP[:, 1] >= Y_NECK).any() else 0))
        for e in errs:
            print("   ERROR: " + e)
            ok = False
        chars.append(ch)
        cols_all[name] = cols
    if not args.no_preview:
        for p in make_previews(chars, cols_all, args.scratch, args.out, args.export):
            print("wrote " + p)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
