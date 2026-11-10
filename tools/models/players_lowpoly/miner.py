"""MINER: the miner (box design: characters_outlined_extra.miner)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, band, plate, profile, hexring, material, rotz, shift, offset, ccw, FRONT

DESC = ("the miner: a yellow hard hat with a lit lamp, olive coveralls with team straps and knee pads, a tool belt and a "
        "pickaxe across the back")


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
    ch = Char("MINER", DESC, Rig(hip=910, hipx=130, knee=480, ankle=120))
    b = body(ch, legs="olive", feet="dark", pelvis="olive", torso="olive", head="skin", calf=False, thigh_mid=False,
             thigh=(82, 78, 68, 70), shin=(68, 70, 68, 68), foot=(66, 185, 92),
             pelvis_w=(172, 205), pelvis_d=(118, 128), waist=(200, 126), chest=(228, 138), shoulder=(208, 120), chest_y=1230,
             head_w=136, head_d=140, head_top=1772, jaw=0.86, crown=0.72)
    # the coveralls' legs: big team knee pads over the knees, team boot cuffs
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(455, 0, "team"), (690, 0, "olive"), (950, 0, None)], [(455, 0), (950, 0)])
        paint(ch, b, "shin" + s, [(110, 0, "team"), (190, 0, "olive"), (270, 0, "team"), (505, 0, None)], [(110, 0), (505, 0)])
    # the tool belt, a pouch on it
    paint(ch, b, "torso", [(930, 9, "leather"), (995, 9, "olive"), (1004, 0, "olive"), (1230, 0, "olive"), (1408, 0, None)],
          [(930, 9), (1230, 0), (1408, 0)])
    ch.loft("torso", "leather", [(120, -150), (200, -132), (200, -100), (120, -112)], 850, [(126, -152), (196, -136), (196, -104), (126, -116)], 975, "y")
    # the team bib, the straps over the shoulders and crossed down the back
    band(ch, b, "torso", "team", 1110, 1300, FRONT, grow=4, depth=8)
    ch.sym(lambda sd, s: plate(ch, "torso", "team", [(sd * 62, 1000), (sd * 136, 1000), (sd * 158, 1408), (sd * 84, 1408)], -160, -140))
    plate(ch, "torso", "team", [(-170, 1000), (-90, 1000), (170, 1408), (90, 1408)], 134, 148)
    plate(ch, "torso", "team", [(90, 1000), (170, 1000), (-90, 1408), (-170, 1408)], 148, 158)
    shoulder(ch, "team", x=282, y0=1225, w=80, d=94, tilt=0.15)
    # the pickaxe slung across the back: its handle up the spine, the head across the shoulder blades
    ch.rod("torso", "leather", (0, 940, 174), (0, 1395, 174), 20, k=4)
    def sq(z0, y0, z1, y1):
        return [(z0, y0), (z1, y0), (z1, y1), (z0, y1)]
    pick = [(sq(152, 1330, 184, 1350), -322), (sq(150, 1360, 196, 1408), -70), (sq(150, 1360, 196, 1408), 70), (sq(152, 1330, 184, 1350), 322)]
    bands(ch, "torso", pick, ["light", "mid", "light"], axis="x")
    ink(ch, "torso", [(offset(ccw(p), ch.outline), w) for p, w in pick], axis="x")
    # the hard hat: its brim, a team band, the dome, a khaki ridge, the lamp
    paint(ch, b, "head", [(1430, 0, "skin"), (1550, 0, "skin"), (1632, 0, "hivis"), (1638, 24, "hivis"), (1652, 24, "hivis"), (1660, 12, "team"), (1690, 12, "hivis"), (1772, 6, None)],
          [(1430, 0), (1638, 24), (1652, 24), (1772, 6)])
    profile(ch, "head", "khaki", [(-120, 1740), (110, 1740), (100, 1772), (-100, 1782)], -16, 16, shell=False)
    ch.loft("head", "dark", [(-36, -170), (36, -170), (36, -148), (-36, -148)], 1665, [(-30, -180), (30, -180), (30, -148), (-30, -148)], 1715, "y")
    plate(ch, "head", "lamp", [(-24, 1674), (24, 1674), (24, 1706), (-24, 1706)], -181, -172)
    # the face: a short beard, eyes
    plate(ch, "head", "hair", [(-70, 1480), (70, 1480), (62, 1530), (-62, 1530)], -150, -138)
    plate(ch, "head", "eye", [(-60, 1584), (60, 1584), (60, 1602), (-60, 1602)], -150, -138)
    return ch
