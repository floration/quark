"""MARKSMAN: the marksman (box design: characters_outlined_extra2.marksman)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, band, plate, profile, hexring, material, rotz, shift, offset, ccw, FRONT

DESC = ("the marksman: a deep hood and ghillie cloak of ragged green strips, a rangefinder over the right eye with a red "
        "lens, team trousers and gloves strap")


# ---- painted pieces (one sweep in coloured bands under one ink hull: a stripe costs 12-16 triangles, not a wrap) ----

def bands(ch, bone, rings, tones, axis="y"):
    """one closed sweep through rings [(polygon, w)] (w rising), the band above ring i in tones[i]; no outline"""
    rings = [(ccw(p), w) for p, w in rings]
    flip = LP._MIRROR[axis]
    m = ch.mesh[bone]

    def add(t, a, b_, c):
        ch._m(bone, t)
        m.add(material(t), *((a, c, b_) if flip else (a, b_, c)))
    P = LP._P
    (p0, w0), (p1, w1) = rings[0], rings[-1]
    for i, j, k in LP.triangulate(p1):
        add(tones[-1], P(axis, *p1[i], w1), P(axis, *p1[j], w1), P(axis, *p1[k], w1))
    for i, j, k in LP.triangulate(p0):
        add(tones[0], P(axis, *p0[i], w0), P(axis, *p0[k], w0), P(axis, *p0[j], w0))
    n = len(p0)
    for t, (pa, wa), (pb, wb) in zip(tones, rings, rings[1:]):
        for i in range(n):
            a0, a1, b1, b0 = P(axis, *pa[i], wa), P(axis, *pa[(i + 1) % n], wa), P(axis, *pb[(i + 1) % n], wb), P(axis, *pb[i], wb)
            add(t, a0, a1, b1)
            add(t, a0, b1, b0)


def ink(ch, bone, rings, axis="y"):
    """the inverted-hull outline round rings [(polygon, w)] (already grown by the outline; ends pushed out by it)"""
    rings = sorted(((ccw(p), w) for p, w in rings), key=lambda r: r[1])
    o = ch.outline
    rings[0], rings[-1] = (rings[0][0], rings[0][1] - o), (rings[-1][0], rings[-1][1] + o)
    LP._sweep(ch.mesh[bone], "ink", rings, axis, True)


def paint(ch, b, name, cuts, hull):
    """rebuild body piece `name` (call it before anything else goes on its bone) as bands: cuts bottom up
    [(y, grow, tone of the band above)], each ring the piece's section at y grown by grow; hull [(y, grow)]"""
    sg = b.seg[name]
    ch.mesh[sg.bone].tris.clear()
    bands(ch, sg.bone, [(offset(sg.at(y), g), y) for y, g, _ in cuts], [t for _, _, t in cuts[:-1]])
    ink(ch, sg.bone, [(offset(sg.at(y), g + ch.outline), y) for y, g in hull])


# ---------------------------------------------------------------------------------------------------------------

def ragged(x0, x1, y_top, ends):
    """a plate outline (x, y) from y_top down to a ragged hem: ends = the strips' bottoms, left to right, with a notch
    between strips"""
    n = len(ends)
    w = (x1 - x0) / n
    pts = [(x1, y_top), (x0, y_top)]
    for i, yb in enumerate(ends):
        xa, xb = x0 + w * i, x0 + w * (i + 1)
        pts += [(xa + 4, yb + 30), (xa + w * 0.3, yb), (xb - w * 0.25, yb + 12)]
    return pts


def build():
    ch = Char("MARKSMAN", DESC, Rig(hip=900, hipx=130, knee=470, ankle=110))
    b = body(ch, legs="team", feet="hat_dk", pelvis="team", torso="ghillie_dk", head="ghillie", thigh_mid=False,
             thigh=(80, 76, 66, 70), shin=(66, 70, 70, 66), foot=(64, 180, 90),
             pelvis_w=(166, 198), pelvis_d=(114, 124), waist=(192, 120), chest=(220, 132), shoulder=(205, 118), chest_y=1230,
             head_w=158, head_d=140, head_top=1770, jaw=0.86, crown=0.74)
    # the body under the cloak; the cloak round the shoulders with a moss band
    paint(ch, b, "torso", [(910, 0, "ghillie_dk"), (1168, 0, "ghillie"), (1180, 24, "ghillie"), (1255, 24, "moss"), (1290, 24, "ghillie"), (1408, 16, None)],
          [(910, 0), (1180, 24), (1408, 16)])
    plate(ch, "torso", "team", [(-120, 940), (120, 940), (128, 1130), (-128, 1130)], -138, -126)   # the vest
    # the ghillie strips hanging from the cloak: a ragged hem down the back, two long strips in front
    plate(ch, "torso", "ghillie", ragged(-232, 232, 1190, [1040, 960, 1060, 940, 1010]), 146, 156)
    ch.sym(lambda sd, s: plate(ch, "torso", "ghillie_dk", [(sd * 150, 1190), (sd * 230, 1190), (sd * 226, 1090), (sd * 196, 1070), (sd * 170, 1104)], -170, -158))
    # the cloak over the shoulders, ragged strips under them
    for sd in (1, -1):
        cx = sd * 282
        with ch.placed("torso", rotz(-sd * 0.15), (cx, 1215, 0)):
            ch.loft("torso", "ghillie", hexring(82, 96, cx), 1150, shift(hexring(58, 70, cx), -sd * 16), 1408, "y")
            ch.loft("torso", "ghillie_dk", hexring(60, 70, cx), 1060, hexring(80, 94, cx), 1152, "y", shell=False)
    # the deep hood over the head: the face shadowed inside it, wrapped below the eyes; its rim; a tuft on top
    plate(ch, "head", "skin", [(-92, 1500), (92, 1500), (96, 1640), (-96, 1640)], -148, -138)
    plate(ch, "head", "ghillie_dk", [(-96, 1490), (96, 1490), (96, 1556), (-96, 1556)], -152, -140)
    plate(ch, "head", "eye", [(-64, 1584), (64, 1584), (64, 1600), (-64, 1600)], -151, -146)
    band(ch, b, "head", "ghillie", 1640, 1712, FRONT, grow=8, depth=14, shell=True)
    profile(ch, "head", "ghillie_dk", [(-60, 1755), (90, 1740), (70, 1795), (-30, 1790)], -60, 60)
    # the rangefinder over the right eye, its red lens
    ch.loft("head", "dark", [(16, -166), (70, -166), (70, -136), (16, -136)], 1552, [(20, -166), (66, -166), (66, -136), (20, -136)], 1618, "y")
    plate(ch, "head", "redeye", [(28, 1566), (58, 1566), (58, 1604), (28, 1604)], -171, -165)
    return ch
