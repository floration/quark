"""VIKING: the viking (box design: characters_outlined_extra.viking)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, BACK_WRAP, plate, ring, hexring, ngon, rotz

DESC = "the viking: a horned steel helmet, a big straw beard, a fur mantle, a team tunic and a round team shield on the back"


def trap(w, zf, zb):
    """a trapezoid (x, z): the beard's section, wide at the back (zb), narrower at the front (zf)"""
    return [(-w, zb), (-w * 0.82, zf), (w * 0.82, zf), (w, zb)]


def tri(r, cx, cz=0.0):
    """a triangle (x, z) round (cx, cz), a point to the front: a faceted horn's section"""
    return [(cx, cz - r), (cx + r * 0.87, cz + r * 0.5), (cx - r * 0.87, cz + r * 0.5)]


def build():
    ch = Char("VIKING", DESC, Rig(hip=910, hipx=130, knee=460, ankle=120))
    # the team tunic down to the knees (the box's skirt reaches mid thigh), grey trousers under it
    b = body(ch, legs="team", shins="mid", feet="fur", pelvis="team", torso="team", head=None,
             thigh=(98, 98, 84, 86), shin=(70, 74, 76, 68), foot=(66, 185, 95), thigh_mid=False, calf=False,
             pelvis_w=(175, 210), waist=(200, 124), chest=(232, 144), shoulder=(205, 120), chest_y=1240)
    # the leather leg wraps' crossing (plates on the shins' fronts)
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "leather", [(sd * 130 - 48, 250), (sd * 130 + 48, 300), (sd * 130 + 48, 340), (sd * 130 - 48, 290)], -76, -56))
    # the belt and its gold buckle
    b.wrap("torso", 930, 995, "leather", grow=8, shell=False)
    plate(ch, "torso", "gold", [(-40, 940), (40, 940), (40, 990), (-40, 990)], -146, -134)
    # the fur mantle round the shoulders, the fur shoulders
    b.wrap("torso", 1285, 1408, "fur", grow=16)

    def shoulder(sd, s):
        x = sd * 282
        with ch.placed("torso", rotz(-sd * 0.2), (x, 1230, 0)):
            ch.sweep("torso", "fur", [(hexring(92, 110, x), 1225), (hexring(62, 76, x - sd * 22), 1405)])
    ch.sym(shoulder)
    # the round team shield on the back, its steel boss
    plate(ch, "torso", "team", ngon(0, 1150, 176, 8), 146, 168, shell=True)
    plate(ch, "torso", "light", ngon(0, 1150, 46, 6), 168, 188)
    # the head: a skin face (the beard hides the jaw), a steel helmet
    face = Segment(ch, "head", [(ring(104, 100), 1430), (ring(146, 134), 1640)])
    ch.sweep("head", "skin", face.rings)
    b.seg["face"] = face
    band(ch, b, "face", "straw", 1450, 1640, BACK_WRAP, grow=6, depth=20)                        # the straw hair
    hel = Segment(ch, "head", [(ring(156, 154, front=144), 1625), (ring(148, 146, front=138), 1700), (ring(98, 92), 1752)])
    ch.sweep("head", "light", hel.rings)
    hel.wrap(1625, 1668, "mid", grow=6, shell=False)                                              # the helmet band
    plate(ch, "head", "mid", [(-17, 1548), (17, 1548), (17, 1640), (-17, 1640)], -162, -148)        # the nose guard
    plate(ch, "head", "eye", [(-60, 1590), (60, 1590), (60, 1608), (-60, 1608)], -142, -130)
    # the big straw beard from the cheeks down over the chest, braided to a point
    ch.sweep("head", "straw", [(trap(30, -176, -150), 1235), (trap(84, -182, -136), 1340), (trap(124, -150, -92), 1470),
                               (trap(130, -144, -64), 1560)])
    # the ivory horns: faceted, curving out and up
    ch.sym(lambda sd, s: ch.sweep("head", "ivory", [(tri(32, sd * 140), 1640), (tri(22, sd * 154), 1726), (tri(6, sd * 150), 1812)]))
    return ch
