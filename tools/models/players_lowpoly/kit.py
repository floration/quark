"""The shared kit for the LOW-POLY player set (gen_players_lowpoly.py; the contract: README.md here).

A character is a Char: one lowpoly.Mesh a walking bone (pelvis torso head thigh_l shin_l thigh_r shin_r foot_l
foot_r; _l = the model's left, -X), every piece a closed faceted solid with its inverted-hull "ink" outline
(lowpoly.py: solid / loft / sweep / rod), written as glTF nodes anim_<bone> whose translation is the joint's pivot.

Units: millimetres, feet on the floor at the origin, facing -Z, +X the model's right, y up. A polygon for a sweep
along "y" is (x, z): its first corners are the front (-z) when made with ring() / rrect().

    from players_lowpoly.kit import *
    def build():
        ch = Char("STANDARD", DESC, Rig(hip=910, knee=470))
        b = body(ch, legs="team", shins="light", torso="team", head="light")
        b.wrap("pelvis", 860, 930, "dark")          # a belt round the hips
        pauldron(ch, "light")
        visor(ch, b, "visor", 1570, 1630)
        return ch

Tones are the box set's (gen_players_outlined.TONES: dark mid_dk mid light bore team team_dk visor eye skin ...),
so a remade character keeps its exact colours; tone(name, srgb) adds a fixed colour of a character's own.
"""
import contextlib
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import lowpoly as LP  # noqa: E402
from lowpoly import Mesh, ngon, rrect, offset, ccw, rotx, roty, rotz, matmul  # noqa: E402,F401
import gen_players_outlined as O  # noqa: E402

OUTLINE = 10.0     # mm the player hull grows (the weapons' 2.6 mm is for 30 cm; players are seen at 5-40 m)
BONES = ("pelvis", "torso", "head", "thigh_l", "shin_l", "thigh_r", "shin_r", "foot_l", "foot_r")
NECK = 1440.0      # the hitbox's neck line (gamedefs.h: kPlayerHeight x (1 - kHeadFraction))

_EXTRA = {}        # tone -> (linear colour, exempt)


def tone(name, srgb, exempt=False):
    """a fixed colour of a character's own (sRGB 0-1); exempt: a small dark detail (eyes, slits) the floor skips.
    The name must not start with team / glow / metal / base / accent / body (the game's tint slots)."""
    lin = tuple(c ** 2.2 for c in srgb)
    if name in O.TONES or (name in _EXTRA and _EXTRA[name][0] != lin):
        raise ValueError("tone %s already defined" % name)
    _EXTRA[name] = (lin, exempt)
    return name


def material(t):
    """a tone's material name in the file: slot tones carry the slot's prefix (team*, metal_*, base_*)"""
    if t == "ink":
        return "ink"
    if t in _EXTRA:
        return t
    return O.material_name(t)


def tone_color(t):
    if t in _EXTRA:
        return _EXTRA[t][0]
    return tuple(float(v) for v in O.tone_color(t))


def exempt(t):
    if t == "ink":
        return True
    if t in _EXTRA:
        return _EXTRA[t][1]
    return O.TONES[t][2]


# ---- the character ------------------------------------------------------------------------------------------------

class Rig:
    """the joints (mm): the game reads them back from the anim_ nodes' translations (gltf.cpp playerMeshRig)"""

    def __init__(self, hip=910, hipx=120, knee=470, ankle=120, neck=NECK, waist=None):
        self.hip, self.hipx, self.knee, self.ankle, self.neck = float(hip), float(hipx), float(knee), float(ankle), float(neck)
        self.waist = self.hip if waist is None else float(waist)

    def pivot(self, bone):
        x = -self.hipx if bone.endswith("_l") else self.hipx
        return {"pelvis": (0, self.hip, 0), "torso": (0, self.waist, 0), "head": (0, self.neck, 0),
                "thigh_l": (x, self.hip, 0), "thigh_r": (x, self.hip, 0), "shin_l": (x, self.knee, 0), "shin_r": (x, self.knee, 0),
                "foot_l": (x, self.ankle, 0), "foot_r": (x, self.ankle, 0)}[bone]


class Char:
    def __init__(self, name, desc, rig=None, outline=None):
        self.name, self.desc, self.rig = name, desc, rig or Rig()
        self.outline = float(OUTLINE if outline is None else outline)
        self.mesh = {b: Mesh() for b in BONES}
        self.tones = set()
        self.limbs = []      # (label, Segment): checked at least 120 mm thick

    def _m(self, bone, t):
        if bone not in self.mesh:
            raise ValueError("unknown bone %s" % bone)
        tone_color(t)        # (an unknown tone fails here)
        self.tones.add(t)
        return self.mesh[bone], material(t)

    def sweep(self, bone, t, rings, axis="y", shell=True):
        """a closed solid through rings [(polygon, w)] (same corner count; see lowpoly.sweep)"""
        m, mat = self._m(bone, t)
        LP.sweep(m, mat, rings, axis, shell, self.outline)

    def solid(self, bone, t, poly, axis, lo, hi, shell=True):
        m, mat = self._m(bone, t)
        LP.solid(m, mat, poly, axis, lo, hi, shell, self.outline)

    def loft(self, bone, t, pa, wa, pb, wb, axis="y", shell=True):
        m, mat = self._m(bone, t)
        LP.loft(m, mat, pa, wa, pb, wb, axis, shell, self.outline)

    def rod(self, bone, t, p0, p1, r, k=6, shell=True):
        m, mat = self._m(bone, t)
        LP.rod(m, mat, p0, p1, r, k, shell, self.outline)

    @contextlib.contextmanager
    def placed(self, bone, R=None, about=(0, 0, 0), t=(0, 0, 0)):
        """the pieces made inside are turned by R about `about`, then moved by t (not for rod)"""
        m = self.mesh[bone]
        m.place(R, about, t)
        try:
            yield
        finally:
            m.unplace()

    @staticmethod
    def sym(fn):
        """fn(sd, sfx) for both sides: sd +1 / "_r" the model's right (+X), -1 / "_l" its left"""
        for sd, sfx in ((1, "_r"), (-1, "_l")):
            fn(sd, sfx)

    def tris(self):
        return sum(m.count() for m in self.mesh.values())

    def nodes(self):
        """lowpoly.write_gltf parts: an empty static body, then a node a bone"""
        return [(self.name, Mesh(), None)] + [("anim_" + b, self.mesh[b], self.rig.pivot(b)) for b in BONES]


# ---- polygons -----------------------------------------------------------------------------------------------------

def ring(w, d, c=None, cx=0.0, cz=0.0, front=None):
    """an octagon (x, z): half width w, half depth d (front: its own half depth toward -z), corners cut by c"""
    f = d if front is None else front
    c = min(w, d, f) * 0.42 if c is None else c
    return rrect(cx - w, cz - f, cx + w, cz + d, c)


def hexring(w, d, cx=0.0, cz=0.0):
    """a hexagon (x, z) with points at the sides (x = +-w): a faceted limb that reads round"""
    return [(cx + w * math.cos(a), cz + d * math.sin(a)) for a in (math.pi * k / 3.0 for k in range(6))]


def scale_poly(poly, s, cx=0.0, cy=0.0):
    sx, sy = (s, s) if not isinstance(s, tuple) else s
    return [(cx + (u - cx) * sx, cy + (v - cy) * sy) for u, v in poly]


def shift(poly, du=0.0, dv=0.0):
    return [(u + du, v + dv) for u, v in poly]


class Segment:
    """a swept piece's rings, kept so props can wrap it (bands, belts, cuffs)"""

    def __init__(self, ch, bone, rings, axis="y"):
        self.ch, self.bone, self.axis = ch, bone, axis
        self.rings = sorted(((ccw(p), float(w)) for p, w in rings), key=lambda r: r[1])

    def at(self, w):
        R = self.rings
        if w <= R[0][1]:
            return list(R[0][0])
        for (pa, wa), (pb, wb) in zip(R, R[1:]):
            if w <= wb:
                t = (w - wa) / (wb - wa)
                return [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for a, b in zip(pa, pb)]
        return list(R[-1][0])

    def wrap(self, w0, w1, t, grow=6.0, shell=True, bone=None):
        """a band round this piece from w0 to w1, `grow` mm proud of it (a belt, a cuff, a helm band)"""
        ws = [w0] + [w for _, w in self.rings if w0 < w < w1] + [w1]
        self.ch.sweep(bone or self.bone, t, [(offset(self.at(w), grow), w) for w in ws], self.axis, shell)

    def width(self):
        """the thinnest full width (x and z) over the rings"""
        out = 1e9
        for p, _ in self.rings:
            out = min(out, max(u for u, _ in p) - min(u for u, _ in p), max(v for _, v in p) - min(v for _, v in p))
        return out


class Body:
    def __init__(self, ch):
        self.ch = ch
        self.seg = {}

    def wrap(self, name, y0, y1, t, grow=6.0, shell=True):
        """a band round body piece `name` (pelvis torso head thigh_l thigh_r shin_l shin_r; "thigh_" both sides)"""
        names = [name + "l", name + "r"] if name.endswith("_") else [name]
        for n in names:
            self.seg[n].wrap(y0, y1, t, grow, shell)


def body(ch, legs="mid", shins=None, feet="dark", pelvis="dark", torso="team", head="light", *,
         bulk=1.0, hipx=None, thigh=(80, 80, 66, 70), shin=(66, 70, 72, 66), foot=(64, 180, 92), knee_pad=None,
         calf=True, thigh_mid=True, pelvis_w=(160, 200), pelvis_d=(112, 126), waist=(190, 120), chest=(228, 140), shoulder=(200, 118),
         chest_y=1250, torso_top=1418,
         head_w=156, head_d=156, head_top=1772, jaw=0.78, crown=0.70, head_k=None, neck_w=62):
    """the parametric body. tones: legs (thighs), shins (default legs), feet, pelvis, torso, head (None: no head:
    the character builds its own). Knobs (mm, half widths): thigh (top w, top d, knee w, knee d), shin (knee w, knee d,
    calf w, ankle w; calf False: a straight taper, 24 triangles less a leg; thigh_mid False the same for the thigh), foot (half width, toe reach forward, heel back), pelvis_w / pelvis_d (crotch, hip top), waist /
    chest / shoulder (w, d) at the waist, at chest_y and at the torso's top (torso_top, under the neck line), the head's half width / depth and
    its top (y, before the outline), jaw / crown (the chin's and the crown's size as a fraction of the head), bulk (all
    the body's widths and depths). Returns a Body: .seg[name] is each piece's Segment, for wraps."""
    r = ch.rig
    if hipx is not None:
        r.hipx = float(hipx)
    B = Body(ch)
    k = bulk
    o = ch.outline
    shins = shins or legs
    # legs: a hexagonal thigh (hip -> knee), a shin with a calf (knee -> ankle), a wedge foot
    tw, td, kw, kd = (v * k for v in thigh)
    sw, sd, cw, aw = (v * k for v in shin)

    def leg(sd_, sfx):
        x = sd_ * r.hipx
        th = Segment(ch, "thigh" + sfx, [(hexring(tw, td, x), r.hip + 40)] + ([(hexring((tw + kw) / 2 + 4, (td + kd) / 2 + 4, x), (r.hip + r.knee) / 2 + 30)] if thigh_mid else []) +
                     [(hexring(kw, kd, x), r.knee - 25)])
        ch.sweep("thigh" + sfx, legs, th.rings)
        cz = 6
        sh = Segment(ch, "shin" + sfx, [(hexring(sw, sd, x), r.knee + 25)] + ([(hexring(cw, cw * 1.04, x, cz), r.knee - (r.knee - r.ankle) * 0.35)] if calf else []) +
                     [(hexring(aw, aw * 1.05, x, 4), r.ankle - 10)])
        ch.sweep("shin" + sfx, shins, sh.rings)
        B.seg["thigh" + sfx], B.seg["shin" + sfx] = th, sh
        ch.limbs += [("thigh" + sfx, th), ("shin" + sfx, sh)]
        if knee_pad:   # (a flat plate on the knee's front, no outline of its own)
            z = -sd * 0.866
            ch.loft("shin" + sfx, knee_pad, [(x - sw * 0.55, z - 4), (x + sw * 0.55, z - 4), (x + sw * 0.4, z - 24), (x - sw * 0.4, z - 24)], r.knee - 60,
                    [(x - sw * 0.45, z - 2), (x + sw * 0.45, z - 2), (x + sw * 0.3, z - 16), (x - sw * 0.3, z - 16)], r.knee + 30, "y", shell=False)
        fw, toe, heel = foot
        fw *= k
        y0, y1 = o, r.ankle + 22
        prof = [(heel, y0), (-toe + 20, y0), (-toe, y0 + 34), (-toe * 0.3, y1 - 14), (-10, y1), (heel, y1)]
        ch.solid("foot" + sfx, feet, prof, "x", x - fw, x + fw)

    Char.sym(leg)
    # pelvis: crotch to the hip line
    pw0, pw1 = (v * k for v in pelvis_w)
    pd0, pd1 = (v * k for v in pelvis_d)
    pel = Segment(ch, "pelvis", [(ring(pw0, pd0), r.hip - 120), (ring(pw1, pd1), r.hip + 70)])
    ch.sweep("pelvis", pelvis, pel.rings)
    B.seg["pelvis"] = pel
    # torso: waist, chest (its front pushed out), shoulders, the neck's slope
    if torso:
        w0, d0 = (v * k for v in waist)
        w1, d1 = (v * k for v in chest)
        w2, d2 = (v * k for v in shoulder)
        ty = torso_top - o
        tor = Segment(ch, "torso", [(ring(w0, d0), r.waist + 20), (ring(w1, d1, front=d1 + 12), chest_y), (ring(w2, d2, c=min(w2, d2) * 0.6), ty)])
        ch.sweep("torso", torso, tor.rings)
        B.seg["torso"] = tor
    # head: a neck, the jaw, cheeks, brow and a crown in one faceted solid
    if head:
        hw, hd = head_w, head_d
        n = r.neck
        rings_ = [(ring(hw * jaw, hd * jaw), n - 10), (ring(hw, hd), n + 110), (ring(hw, hd), head_top - 95), (ring(hw * crown, hd * crown), head_top - o)]
        hs = Segment(ch, "head", rings_)
        ch.sweep("head", head, hs.rings)
        B.seg["head"] = hs
    return B


# ---- props --------------------------------------------------------------------------------------------------------

def pauldron(ch, t, x=285, y0=1225, y1=1418, w=78, d=96, tilt=0.18, band=None, k=6):
    """a shoulder pad each side (the game draws the arms from the shoulders): centre x, from y0 to y1 (keep its top
    and outline under the neck line, 1440), half width w / depth d, tipped out by tilt (radians); band: a tone for a
    rim round its bottom edge"""
    y1 -= ch.outline

    def one(sd, _):
        cx = sd * x
        R = rotz(-sd * tilt)
        poly = (lambda s, dz=0: ring(w * s, d * s, None, cx, dz)) if k == 8 else (lambda s, dz=0: hexring(w * s, d * s, cx, dz))
        with ch.placed("torso", R, (cx, y0, 0)):
            ch.sweep("torso", t, [(poly(1.0), y0), (poly(1.08), y0 + (y1 - y0) * 0.55), (shift(poly(0.62), -sd * w * 0.25), y1)])
            if band:
                ch.loft("torso", band, poly(1.06), y0 - 28, poly(1.1), y0 + 6, "y", shell=False)

    Char.sym(one)


FRONT, FRONT_WRAP, BACK, BACK_WRAP = [0, 1], [7, 0, 1, 2], [4, 5], [3, 4, 5, 6]   # a ring()'s corners on each side


def band(ch, b, name, t, y0, y1, side=FRONT_WRAP, grow=5.0, depth=14.0, shell=False, bone=None):
    """a panel that follows one side of an octagonal body piece (made with ring(): pelvis torso head) from y0 to y1,
    grow mm proud of it: a tabard, a chest panel, a visor, a face strip. Past the piece's ends it hangs straight
    (a tabard below the torso). side: FRONT (the front face), FRONT_WRAP (and its two corner facets), BACK, BACK_WRAP"""
    sg = b.seg[name]
    ws = [y0] + [w for _, w in sg.rings if y0 < w < y1] + [y1]
    rings_ = []
    for w in ws:
        rg = sg.at(w)
        out, inn = offset(rg, grow), offset(rg, -depth)
        rings_.append(([out[i] for i in side] + [inn[i] for i in reversed(side)], w))
    ch.sweep(bone or sg.bone, t, rings_, "y", shell)


def visor(ch, b, t, y0, y1, grow=4.0, depth=14.0, shell=False, wrap=True):
    """a band over the front of the head (a visor, an eye slit, a face strip) from y0 to y1: the front face and,
    wrap, its two corner facets"""
    band(ch, b, "head", t, y0, y1, FRONT_WRAP if wrap else FRONT, grow, depth, shell)


def helmet(ch, b, t, y0, grow=8.0, top=12.0, shell=True):
    """a shell over the head from y0 up (a helmet, a cap, a hood): the head's rings grown by `grow`, the top raised"""
    hs = b.seg["head"]
    ws = [y0] + [w for _, w in hs.rings if w > y0]
    rings_ = [(offset(hs.at(w), grow), w) for w in ws]
    rings_[-1] = (rings_[-1][0], rings_[-1][1] + top)
    ch.sweep("head", t, rings_, "y", shell)


def brim(ch, t, y, r_in, r_out, thick=12.0, up=0.0, k=10, cz=0.0, sx=1.0, bone="head"):
    """a hat brim: a flat k-gon ring from r_in to r_out (up: the rim raised that much: a turned-up brim). Keep r_out
    plus the outline inside the head's limit (198 mm)"""
    inner = ngon(0, cz, r_in, k, sx=sx)
    outer = ngon(0, cz, r_out, k, sx=sx)
    if up:
        ch.sweep(bone, t, [(inner, y), (scale_poly(outer, 0.98, 0, cz), y + thick), (outer, y + thick + up)])
    else:
        ch.loft(bone, t, outer, y, scale_poly(inner, 1.0, 0, cz), y + thick)


def plate(ch, bone, t, poly_xy, z0, z1, shell=False):
    """a flat plate (x, y) from z0 to z1 (a chest plate, a badge, a cross: on the front z < 0)"""
    ch.solid(bone, t, poly_xy, "z", min(z0, z1), max(z0, z1), shell)


def profile(ch, bone, t, poly_zy, x0, x1, shell=True):
    """a side profile (z, y) extruded across x0..x1 (a crest, a plume, a fin, a blade)"""
    ch.solid(bone, t, poly_zy, "x", min(x0, x1), max(x0, x1), shell)


def backpack(ch, t, w, y0, y1, z0, depth, c=24.0, bone="torso", taper=0.9):
    """a pack on the back: half width w, y0..y1, from z0 (the back's surface) out by depth; its back face shrunk by
    taper"""
    a = rrect(-w, y0, w, y1, c)
    bq = scale_poly(a, (taper, 1 - (1 - taper) * 0.5), 0, (y0 + y1) / 2)
    ch.loft(bone, t, a, z0, bq, z0 + depth, "z")


def belt(b, name, y0, y1, t, grow=7.0):
    b.wrap(name, y0, y1, t, grow)


def cape(ch, t, y_top, y_bot, w_top, w_bot, z, thick=16.0, flare=40.0, bone="torso"):
    """a cape down the back: w_top wide at y_top, w_bot (and flare mm further back) at y_bot"""
    top = [(-w_top, z), (w_top, z), (w_top * 0.9, z + thick), (-w_top * 0.9, z + thick)]
    bot = [(-w_bot, z + flare), (w_bot, z + flare), (w_bot * 0.9, z + flare + thick), (-w_bot * 0.9, z + flare + thick)]
    ch.loft(bone, t, bot, y_bot, top, y_top, "y")


def crest(ch, t, z0, z1, y0, h, x=8.0, bone="head", sweep=0.4):
    """a ridge along the top of the head from z0 (front) to z1 (back), y0 its foot, h tall, 2x wide; sweep leans
    it back"""
    L = z1 - z0
    poly = [(z0, y0), (z1, y0), (z1 + L * 0.05, y0 + h * 0.55), (z1 - L * sweep * 0.3, y0 + h), (z0 + L * sweep, y0 + h), (z0 - L * 0.04, y0 + h * 0.4)]
    profile(ch, bone, t, poly, -x, x)
