"""MEDIC: the field medic (box design: characters_outlined_extra2.medic)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, plate, backpack, hexring, rrect, material, rotz, shift, offset, ccw

DESC = ("the field medic: a pale grey field top, team trousers, a team armband and cap with white crosses, a big white "
        "med pack on the back, a pouch belt")


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

def cross(ch, bone, t, cx, cy, arm, w, z0, z1):
    """a flat cross (two plates) on the front (z < 0) or back"""
    plate(ch, bone, t, [(cx - w, cy - arm), (cx + w, cy - arm), (cx + w, cy + arm), (cx - w, cy + arm)], z0, z1)
    plate(ch, bone, t, [(cx - arm, cy - w), (cx + arm, cy - w), (cx + arm, cy + w), (cx - arm, cy + w)], z0, z1)


def build():
    ch = Char("MEDIC", DESC, Rig(hip=900, hipx=130, knee=470, ankle=110))
    b = body(ch, legs="team", feet="dark", pelvis="team", torso="pale", head="skin", thigh_mid=False,
             thigh=(80, 78, 66, 70), shin=(66, 70, 70, 66), foot=(64, 180, 90),
             pelvis_w=(168, 202), pelvis_d=(116, 126), waist=(196, 124), chest=(224, 136), shoulder=(204, 118), chest_y=1240,
             head_w=148, head_d=136, head_top=1770, jaw=0.82, crown=0.72)
    # the field top: a leather pouch belt, the pack's straps down the front
    paint(ch, b, "torso", [(920, 8, "leather"), (985, 8, "pale"), (994, 0, "pale"), (1240, 0, "pale"), (1408, 0, None)],
          [(920, 8), (1240, 0), (1408, 0)])
    for x in (-160, 90):
        ch.solid("torso", "leather", rrect(x, 940, x + 70, 1030, 8), "z", -158, -128, shell=False)
    ch.sym(lambda sd, s: plate(ch, "torso", "leather", [(sd * 64, 1000), (sd * 106, 1000), (sd * 116, 1408), (sd * 74, 1408)], -154, -140))
    # the big white med pack with a team cross and a team strap
    backpack(ch, "white", 125, 980, 1340, 128, 80)
    cross(ch, "torso", "team", 0, 1215, 80, 26, 204, 214)
    plate(ch, "torso", "team", [(-92, 1020), (92, 1020), (92, 1080), (-92, 1080)], 204, 214)
    # the shoulders; the left one carries the team armband with a white cross
    with ch.placed("torso", rotz(-0.15), (282, 1215, 0)):
        ch.loft("torso", "pale", hexring(78, 92, 282), 1215, shift(hexring(55, 69, 282), -16), 1408, "y")
    rings = [(hexring(80, 94, -282), 1110), (hexring(82, 96, -282), 1230), (hexring(78, 92, -282), 1236), (shift(hexring(55, 69, -282), 16), 1408)]
    with ch.placed("torso", rotz(0.15), (-282, 1215, 0)):
        bands(ch, "torso", rings, ["team", "pale", "pale"])
        ink(ch, "torso", [(offset(ccw(p), ch.outline), w) for p, w in (rings[0], rings[1], rings[3])])
        ch.solid("torso", "white", [(-24, 1150), (24, 1150), (24, 1190), (-24, 1190)], "x", -372, -364, shell=False)
        ch.solid("torso", "white", [(-8, 1132), (8, 1132), (8, 1208), (-8, 1208)], "x", -372, -364, shell=False)
    # the head: the team cap with its bill and a white cross
    paint(ch, b, "head", [(1430, 0, "skin"), (1550, 0, "skin"), (1636, 0, "team"), (1644, 8, "team"), (1770, 6, None)],
          [(1430, 0), (1644, 8), (1770, 6)])
    ch.loft("head", "team", [(-104, -140), (104, -140), (104, -60), (-104, -60)], 1636, [(-60, -170), (60, -170), (104, -60), (-104, -60)], 1652, "y")
    cross(ch, "head", "white", 0, 1695, 30, 10, -150, -140)
    plate(ch, "head", "eye", [(-60, 1584), (60, 1584), (60, 1602), (-60, 1602)], -142, -132)
    return ch
