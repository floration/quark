"""SUMO: the sumo (box design: characters_outlined_extra.sumo)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, Segment, body, plate, profile, hexring, ring, material, rotz, shift, offset, ccw

DESC = "the sumo: a huge round skin body and thick legs, a wide team belt with hanging cords, a black topknot"


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
    ch = Char("SUMO", DESC, Rig(hip=880, hipx=140, knee=440, ankle=70))
    b = body(ch, legs="skin", feet="skin", pelvis="team", torso=None, head="skin", calf=False, thigh_mid=False,
             thigh=(118, 112, 96, 94), shin=(96, 94, 86, 80), foot=(80, 185, 95),
             pelvis_w=(225, 262), pelvis_d=(150, 172), head_w=160, head_d=158, head_top=1768, jaw=0.86, crown=0.72)
    # the huge round belly: a torso of its own, its front pushed out low
    b.seg["torso"] = Segment(ch, "torso", [(ring(258, 172), 900), (ring(286, 196, front=222), 1050), (ring(272, 184, front=196), 1190),
                                           (ring(250, 160), 1300), (ring(212, 132, c=70), 1408)])
    # the knee supporters and ankle tape, in team
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(415, 0, "team"), (650, 0, "skin"), (920, 0, None)], [(415, 0), (920, 0)])
        paint(ch, b, "shin" + s, [(60, 0, "team"), (140, 0, "skin"), (255, 0, "team"), (465, 0, None)], [(60, 0), (465, 0)])
    # the mawashi: the team belt wrapped high round the belly; the skin above it
    paint(ch, b, "torso", [(900, 12, "team"), (1015, 12, "skin"), (1025, 0, "skin"), (1050, 0, "skin"), (1190, 0, "skin"), (1300, 0, "skin"), (1408, 0, None)],
          [(900, 12), (1015, 12), (1050, 0), (1190, 0), (1408, 0)])
    plate(ch, "torso", "skin_dk", [(-12, 1080), (12, 1080), (12, 1100), (-12, 1100)], -226, -218)       # the navel
    # the mawashi's apron of hanging cords (sagari) in front, its knot behind
    plate(ch, "pelvis", "team", [(-150, 760), (150, 760), (150, 900), (-150, 900)], -192, -176)
    for k, x in enumerate((-112, -56, 0, 56, 112)):
        plate(ch, "pelvis", "team_dk", [(x - 16, 600), (x + 16, 600), (x + 18, 770), (x - 18, 770)], -186 - k % 2 * 4, -172)
    ch.loft("pelvis", "team", [(-90, 170), (90, 170), (70, 228), (-70, 228)], 830, [(-70, 170), (70, 170), (54, 214), (-54, 214)], 990, "y")
    # the thick shoulders
    shoulder(ch, "skin", x=300, y0=1180, w=92, d=104, tilt=0.12)
    # the head: black hair slicked back over the crown, the topknot folded forward on top, tied in team
    paint(ch, b, "head", [(1430, 0, "skin"), (1550, 0, "skin"), (1660, 0, "hairblk"), (1700, 4, "hairblk"), (1760, 4, None)],
          [(1430, 0), (1550, 0), (1760, 4)])
    ch.loft("head", "hairblk", [(-32, -70), (32, -70), (32, 60), (-32, 60)], 1752, [(-26, -96), (26, -96), (26, 40), (-26, 40)], 1798, "y")
    plate(ch, "head", "team", [(-38, 1758), (38, 1758), (36, 1790), (-36, 1790)], 60, 74)
    # the face: brows, eyes, a mouth; small ears
    ch.sym(lambda sd, s: plate(ch, "head", "hairblk", [(sd * 22, 1606), (sd * 82, 1612), (sd * 80, 1628), (sd * 24, 1624)], -164, -154))
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 30, 1572), (sd * 66, 1572), (sd * 66, 1590), (sd * 30, 1590)], -164, -154))
    plate(ch, "head", "skin_dk", [(-32, 1500), (32, 1500), (32, 1512), (-32, 1512)], -164, -154)
    ch.sym(lambda sd, s: profile(ch, "head", "skin", [(-26, 1540), (26, 1550), (20, 1610), (-22, 1604)], sd * 156, sd * 176, shell=False))
    return ch
