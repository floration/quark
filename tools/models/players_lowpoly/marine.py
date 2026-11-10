"""MARINE: the marine (box design: characters_outlined_extra2.marine)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, band, plate, backpack, hexring, material, rotz, shift, offset, ccw, FRONT

DESC = ("the marine: grim green-grey power armour, a broad helmet with a lit ember visor band and a jaw guard, huge "
        "squared team pauldrons, a chest plate with vents")


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

def big_pauldron(ch, x=290, y0=1150, y1=1408, w=100, d=122, tilt=0.06):
    """the huge squared pauldrons: a marine_dk rim round the bottom, team above; one ink hull"""
    y1 -= ch.outline

    def one(sd, _):
        cx = sd * x
        rings = [(hexring(w, d, cx), y0), (hexring(w * 1.04, d * 1.04, cx), y0 + 44), (shift(hexring(w * 0.8, d * 0.86, cx), -sd * w * 0.16), y1)]
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            bands(ch, "torso", rings, ["marine_dk", "team"])
            ink(ch, "torso", [(offset(ccw(p), ch.outline), w_) for p, w_ in rings])
    Char.sym(one)


def build():
    ch = Char("MARINE", DESC, Rig(hip=890, hipx=140, knee=470, ankle=140))
    b = body(ch, legs="marine", feet="marine", pelvis="marine", torso="marine", head="marine", calf=False, thigh_mid=False,
             thigh=(90, 88, 76, 78), shin=(76, 78, 74, 72), foot=(78, 200, 110),
             pelvis_w=(190, 225), pelvis_d=(128, 140), waist=(215, 136), chest=(248, 152), shoulder=(222, 132), chest_y=1240,
             head_w=150, head_d=150, head_top=1772, jaw=0.88, crown=0.76)
    # the legs: team thigh plates, team knee guards over the armoured shins
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(445, 0, "marine"), (560, 4, "team"), (860, 4, "marine"), (930, 0, None)], [(445, 0), (560, 4), (860, 4), (930, 0)])
        paint(ch, b, "shin" + s, [(130, 0, "marine"), (370, 6, "team"), (495, 6, None)], [(130, 0), (370, 6), (495, 6)])
    plate(ch, "pelvis", "team_dk", [(-60, 790), (60, 790), (70, 910), (-70, 910)], -158, -140)       # the codpiece
    # the torso: the team_dk abdomen, a dark chest plate with two vents
    paint(ch, b, "torso", [(910, 6, "team_dk"), (1090, 6, "marine"), (1100, 0, "marine"), (1408, 0, None)],
          [(910, 6), (1240, 0), (1408, 0)])
    band(ch, b, "torso", "marine_dk", 1120, 1345, FRONT, grow=8, depth=10)
    ch.sym(lambda sd, s: plate(ch, "torso", "bore", [(sd * 40, 1200), (sd * 150, 1200), (sd * 150, 1290), (sd * 40, 1290)], -186, -178))
    # the back unit (team_dk: the back view needs team) with its two ember exhausts
    backpack(ch, "team_dk", 160, 1000, 1340, 140, 70)
    ch.sym(lambda sd, s: plate(ch, "torso", "ember", [(sd * 82, 1070), (sd * 122, 1070), (sd * 122, 1290), (sd * 82, 1290)], 208, 216))
    big_pauldron(ch)
    # the helmet: a dark visor band at the eyes with the ember slit, the jaw guard, the ear guards
    paint(ch, b, "head", [(1430, 4, "marine"), (1560, 4, "bore"), (1612, 4, "marine"), (1772, 0, None)],
          [(1430, 4), (1612, 4), (1772, 0)])
    plate(ch, "head", "ember", [(-104, 1572), (104, 1572), (104, 1598), (-104, 1598)], -166, -156)
    ch.loft("head", "marine_dk", [(-70, -166), (70, -166), (96, -110), (-96, -110)], 1450, [(-62, -168), (62, -168), (90, -120), (-90, -120)], 1545, "y")
    ch.sym(lambda sd, s: ch.loft("head", "marine", [(sd * 150, -64), (sd * 172, -64), (sd * 172, 64), (sd * 150, 64)], 1490,
                                 [(sd * 150, -50), (sd * 168, -50), (sd * 168, 50), (sd * 150, 50)], 1650, "y"))
    return ch
