"""KING: the king (box design: characters_outlined_extra.king)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, tone, plate, profile, hexring, material, rotz, shift, offset, ccw

# the box set's plum (sRGB ~0.29 luminance) is too dark for a cape that fills the back view (visibility rule: big
# non-team areas at least 0.35), so the cape and the crown's velvet are a lighter royal plum (~0.41)
ROYAL = tone("plum_royal", (0.60, 0.29, 0.50))

DESC = ("the king: a gold crown with team jewels, a brown beard, a plum cape with a spotted white ermine mantle, a team "
        "tunic with a gold chain")


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


def shoulder(ch, t, x=265, y0=1225, y1=1418, w=76, d=90, tilt=0.15):
    """a cheap shoulder (a two-ring loft, tipped out); the game draws the arms from here"""
    y1 -= ch.outline

    def one(sd, _):
        cx = sd * x
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            ch.loft("torso", t, hexring(w, d, cx), y0, shift(hexring(w * 0.7, d * 0.75, cx), -sd * w * 0.2), y1, "y")
    Char.sym(one)


# ---------------------------------------------------------------------------------------------------------------

def build():
    ch = Char("KING", DESC, Rig(hip=900, hipx=130, knee=470, ankle=100))
    b = body(ch, legs="white", shins="team", feet="leather", pelvis="team", torso="team", head="skin", calf=False, thigh_mid=False,
             thigh=(80, 76, 66, 70), shin=(66, 70, 66, 68), foot=(64, 175, 85),
             pelvis_w=(170, 205), pelvis_d=(118, 128), waist=(198, 124), chest=(226, 136), shoulder=(206, 120), chest_y=1220,
             head_w=148, head_d=148, head_top=1765, jaw=0.84, crown=0.70)
    # white hose under the team tunic's skirt, flared over the thighs; team stockings
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(445, 0, "white"), (662, 0, "team"), (676, 16, "team"), (940, 16, None)], [(445, 0), (676, 16), (940, 16)])
    # the tunic: a gold belt, the team body, the white ermine mantle round the shoulders
    paint(ch, b, "torso", [(920, 0, "gold"), (985, 0, "team"), (1318, 0, "white"), (1328, 18, "white"), (1408, 14, None)],
          [(920, 0), (1328, 18), (1408, 14)])
    ch.sym(lambda sd, s: plate(ch, "torso", "eye", [(sd * 92, 1362), (sd * 112, 1362), (sd * 112, 1388), (sd * 92, 1388)], -166, -150))  # ermine spots
    shoulder(ch, "white", x=286, y0=1230, w=80, d=100, tilt=0.16)
    # the gold chain and its medallion
    plate(ch, "torso", "gold", [(-112, 1300), (-88, 1300), (0, 1215), (88, 1300), (112, 1300), (0, 1188)], -166, -152)
    plate(ch, "torso", "gold", [(-30, 1110), (30, 1110), (36, 1150), (0, 1196), (-36, 1150)], -168, -150)
    # the plum cape from the mantle down the back to the knees, flaring out, a broad team border and a white hem; it wraps round the sides
    def arc(rx, rz, z0, t):
        out = [(-rx, z0 - 40), (-rx * 0.8, z0 + rz * 0.6), (0, z0 + rz), (rx * 0.8, z0 + rz * 0.6), (rx, z0 - 40)]
        inn = [(x * (rx - t) / rx, (z - z0) * (rz - t) / rz + z0) for x, z in reversed(out)]
        return out + inn
    cr = [(arc(252, 110, 110, 18), 560), (arc(251, 109, 109, 18), 600), (arc(246, 100, 104, 18), 880), (arc(238, 82, 88, 18), 1340)]
    bands(ch, "torso", cr, ["white", "team", ROYAL])
    ink(ch, "torso", [(offset(ccw(arc(252, 110, 110, 18)), ch.outline), 560), (offset(ccw(arc(238, 82, 88, 18)), ch.outline), 1340)])
    # the head: hair under the crown, the crown's gold band and plum velvet cap
    paint(ch, b, "head", [(1430, 0, "skin"), (1550, 0, "skin"), (1618, 0, "hair"), (1646, 0, "gold"), (1652, 12, "gold"), (1706, 12, ROYAL), (1755, 4, None)],
          [(1430, 0), (1652, 12), (1755, 4)])
    # the crown's points (front and sides) and its team jewel
    profile(ch, "head", "gold", [(-178, 1690), (-146, 1690), (-166, 1782)], -20, 20)
    ch.sym(lambda sd, s: ch.solid("head", "gold", [(sd * 146, 1690), (sd * 178, 1690), (sd * 166, 1776)], "z", -18, 18))
    plate(ch, "head", "team", [(-22, 1660), (22, 1660), (22, 1692), (-22, 1692)], -174, -162)
    # the beard round the jaw, the eyes
    ch.loft("head", "hair", [(-122, -132), (122, -132), (100, -60), (-100, -60)], 1560, [(-64, -150), (64, -150), (50, -100), (-50, -100)], 1410, "y")
    plate(ch, "head", "eye", [(-62, 1588), (62, 1588), (62, 1606), (-62, 1606)], -158, -148)
    return ch
