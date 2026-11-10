"""PHARAOH: the pharaoh (box design: characters_outlined_extra.pharaoh)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, plate, profile, hexring, material, rotz, shift, offset, ccw

DESC = ("the pharaoh: a team-and-gold striped headdress with lappets and a cobra, a gold false beard, a broad "
        "team-and-gold collar, a white kilt, bare tan skin")


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
    ch = Char("PHARAOH", DESC, Rig(hip=900, hipx=130, knee=470, ankle=40))
    b = body(ch, legs="tan", shins="tan", feet="gold", pelvis="white", torso="tan", head="tan", calf=False, thigh_mid=False,
             foot=(62, 170, 80), thigh=(84, 80, 68, 70), shin=(68, 70, 68, 67),
             pelvis_w=(175, 205), pelvis_d=(118, 130), waist=(190, 118), chest=(222, 132), shoulder=(205, 118), chest_y=1220,
             head_w=148, head_d=146, head_top=1772, jaw=0.80, crown=0.74)
    # the legs: the white kilt flared over the thighs; the sandals' team straps laced up the shins
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(445, 0, "tan"), (628, 0, "white"), (642, 18, "white"), (940, 18, None)], [(445, 0), (642, 18), (940, 18)])
        paint(ch, b, "shin" + s, [(30, 0, "team"), (285, 0, "tan"), (495, 0, None)], [(30, 0), (495, 0)])
    # the kilt's team apron on the white hips
    plate(ch, "pelvis", "team", [(-62, 640), (62, 640), (74, 965), (-74, 965)], -146, -132)
    # the torso: a gold belt, bare tan chest, the broad team collar with a gold hem and a gold row
    paint(ch, b, "torso", [(920, 4, "gold"), (990, 4, "tan"), (1180, 0, "team"), (1192, 14, "gold"), (1215, 14, "team"),
                           (1330, 14, "gold"), (1352, 14, "team"), (1408, 12, None)],
          [(920, 4), (1192, 14), (1408, 12)])
    shoulder(ch, "tan", x=282, y0=1215, w=78, d=88, tilt=0.14)
    # the nemes headdress: team from the brow up with a gold stripe round it, over the tan face
    paint(ch, b, "head", [(1430, 0, "tan"), (1550, 0, "tan"), (1604, 0, "team"), (1612, 12, "team"), (1670, 12, "gold"), (1690, 12, "team"), (1762, 8, None)],
          [(1430, 0), (1612, 12), (1762, 8)])

    def side(sd, s):
        # its wings flaring down behind the ears, and the striped lappets hanging in front of the shoulders
        # (one side profile: the lappet's front edge down to the collar, the wing's back edge flaring out)
        profile(ch, "head", "team", [(-118, 1395), (-86, 1452), (-86, 1600), (-30, 1665), (40, 1665), (64, 1440), (-48, 1440), (-62, 1395)], sd * 134, sd * 158)
    ch.sym(side)
    # the queue down the back of the neck
    ch.loft("head", "team", [(-66, 110), (66, 110), (60, 160), (-60, 160)], 1660, [(-56, 118), (56, 118), (50, 160), (-50, 160)], 1420, "y")
    # the cobra rearing on the brow
    profile(ch, "head", "gold", [(-154, 1668), (-150, 1716), (-168, 1760), (-182, 1748), (-168, 1706)], -12, 12, shell=False)
    # the face: kohl eyes, a gold false beard
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 24, 1562), (sd * 70, 1562), (sd * 74, 1580), (sd * 28, 1584)], -158, -148))
    ch.loft("head", "gold", [(-24, -128), (24, -128), (20, -96), (-20, -96)], 1490, [(-18, -146), (18, -146), (16, -116), (-16, -116)], 1400, "y", shell=False)
    return ch
