"""ENGINEER: the engineer (box design: characters_outlined_extra2.engineer)."""
import lowpoly as LP
from players_lowpoly.kit import Char, Rig, body, plate, profile, hexring, material, rotx, rotz, shift, offset, ccw

DESC = ("the engineer: sage coveralls, a welding mask flipped up on the head, a tool harness with wrenches, team knee "
        "and elbow pads, a team toolbox on the back")


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

def build():
    ch = Char("ENGINEER", DESC, Rig(hip=900, hipx=130, knee=470, ankle=120))
    b = body(ch, legs="sage", feet="hat_dk", pelvis="sage", torso="sage", head="skin", calf=False, thigh_mid=False,
             thigh=(82, 80, 68, 70), shin=(68, 70, 68, 68), foot=(66, 185, 92),
             pelvis_w=(172, 205), pelvis_d=(118, 128), waist=(200, 126), chest=(228, 138), shoulder=(208, 120), chest_y=1230,
             head_w=158, head_d=158, head_top=1700, jaw=0.84, crown=0.86)
    # the coveralls' legs, big team knee pads; steel toe caps on the work boots
    for s in ("_l", "_r"):
        paint(ch, b, "thigh" + s, [(445, 0, "team"), (640, 0, "sage"), (940, 0, None)], [(445, 0), (940, 0)])
        paint(ch, b, "shin" + s, [(110, 0, "sage"), (300, 0, "team"), (495, 0, None)], [(110, 0), (495, 0)])
    ch.sym(lambda sd, s: ch.loft("foot" + s, "light", [(sd * 130 - 60, -192), (sd * 130 + 60, -192), (sd * 130 + 60, -120), (sd * 130 - 60, -120)], 70,
                                 [(sd * 130 - 54, -150), (sd * 130 + 54, -150), (sd * 130 + 54, -110), (sd * 130 - 54, -110)], 112, "y", shell=False))
    # the coveralls: a leather tool belt, the team collar
    paint(ch, b, "torso", [(920, 8, "leather"), (995, 8, "sage"), (1004, 0, "sage"), (1355, 0, "team"), (1408, 6, None)],
          [(920, 8), (1230, 0), (1408, 6)])
    ch.loft("torso", "leather", [(-200, -140), (-130, -150), (-130, -112), (-200, -100)], 850, [(-196, -142), (-134, -152), (-134, -114), (-196, -102)], 985, "y")
    # the tool harness: two straps and a cross strap, a wrench and a screwdriver tucked in
    ch.sym(lambda sd, s: plate(ch, "torso", "leather", [(sd * 66, 1004), (sd * 108, 1004), (sd * 112, 1360), (sd * 70, 1360)], -154, -140))
    plate(ch, "torso", "leather", [(-120, 1160), (120, 1160), (120, 1200), (-120, 1200)], -158, -144)
    plate(ch, "torso", "light", [(132, 1020), (152, 1020), (152, 1140), (170, 1150), (166, 1186), (118, 1186), (114, 1150), (132, 1140)], -158, -146)
    plate(ch, "torso", "gold", [(-158, 1010), (-132, 1010), (-132, 1120), (-158, 1120)], -158, -146)
    # the team toolbox on the back: a dark lid line, a handle on top
    box = [(-150, 136), (150, 136), (150, 230), (-150, 230)]
    tb = [(box, 1020), (box, 1180), (box, 1198), (box, 1280)]
    bands(ch, "torso", tb, ["team", "team_dk", "team"])
    ink(ch, "torso", [(offset(ccw(box), ch.outline), 1020), (offset(ccw(box), ch.outline), 1280)])
    ch.rod("torso", "dark", (-40, 1300, 183), (40, 1300, 183), 16, k=4)
    # the team shoulders, their team elbow pads (the arms start here)
    for sd in (1, -1):
        cx = sd * 284
        rings = [(hexring(78, 92, cx), 1130), (hexring(84, 98, cx), 1210), (shift(hexring(56, 68, cx), -sd * 16), 1408)]
        with ch.placed("torso", rotz(-sd * 0.15), (cx, 1215, 0)):
            bands(ch, "torso", rings, ["team_dk", "team"])
            ink(ch, "torso", [(offset(ccw(p), ch.outline), w) for p, w in rings])
    # the head: a team head band, dark hair, the welding mask flipped up on the crown with its window and hinges
    paint(ch, b, "head", [(1430, 0, "skin"), (1550, 0, "skin"), (1618, 4, "team"), (1662, 4, "hair"), (1700, 0, None)],
          [(1430, 0), (1618, 4), (1700, 0)])
    with ch.placed("head", rotx(0.35), (0, 1690, 20)):
        ch.solid("head", "dark", [(-128, 1690), (128, 1690), (138, 1760), (110, 1800), (-110, 1800), (-138, 1760)], "z", -40, 40)
        plate(ch, "head", "glass", [(-80, 1730), (80, 1730), (80, 1770), (-80, 1770)], -50, -40)
    ch.sym(lambda sd, s: profile(ch, "head", "light", [(-24, 1630), (24, 1630), (24, 1705), (-24, 1705)], sd * 146, sd * 156, shell=False))
    # the face: stubble, eyes
    plate(ch, "head", "hair", [(-70, 1490), (70, 1490), (62, 1505), (-62, 1505)], -152, -140)
    plate(ch, "head", "eye", [(-60, 1584), (60, 1584), (60, 1602), (-60, 1602)], -152, -140)
    return ch
