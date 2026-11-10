"""ROCKSTAR: the rockstar (box design: characters_outlined_extra2.rockstar)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, visor, plate, ring, hexring, shift, offset, rotz, BACK_WRAP

DESC = "the rockstar: a huge black hair mane, a team guitar slung across the front, a sleeveless black vest over a team shirt, skinny jeans, team sneakers"

GUITAR = [(-50, 790), (140, 790), (205, 868), (182, 965), (150, 1075), (76, 1094), (-66, 985), (-86, 878)]


def build():
    ch = Char("ROCKSTAR", DESC, Rig(hip=900, hipx=130, knee=470, ankle=85))
    b = body(ch, legs="denim", feet="team", pelvis="denim", torso="team", head=None,
             thigh_mid=False, calf=False, thigh=(72, 72, 62, 70), shin=(64, 70, 70, 66), foot=(62, 178, 84),
             pelvis_w=(160, 185), pelvis_d=(108, 116), waist=(178, 112), chest=(212, 124), shoulder=(196, 112), chest_y=1250)
    # the head: jaw, cheeks, the crown under the mane
    hs = Segment(ch, "head", [(ring(110, 112), 1430), (ring(138, 140), 1545), (ring(134, 136), 1690)])
    ch.sweep("head", "skin", hs.rings)
    b.seg["head"] = hs
    # team bandanas tied round the thighs
    b.wrap("thigh_", 720, 890, "team", grow=7, shell=False)
    # the black belt, a gold buckle
    b.wrap("pelvis", 895, 955, "hairblk", grow=6, shell=False)
    plate(ch, "pelvis", "gold", [(-30, 898), (30, 898), (30, 952), (-30, 952)], -130, -118)
    # the black vest open over the team shirt: a panel down each side, front corner to back corner
    band(ch, b, "torso", "hairblk", 920, 1418, [1, 2, 3], grow=6, depth=12)
    band(ch, b, "torso", "hairblk", 920, 1418, [6, 7, 0], grow=6, depth=12)

    # the bare shoulders, team wristbands under them
    def shoulder(sd, _):
        cx = sd * 292
        with ch.placed("torso", rotz(-sd * 0.3), (cx, 1290, 0)):
            ch.loft("torso", "skin", hexring(96, 96, cx), 1200, shift(hexring(56, 66, cx), -sd * 24), 1420 - ch.outline - 6, "y")
            ch.loft("torso", "team", hexring(94, 98, cx), 1120, hexring(102, 102, cx), 1212, "y", shell=False)
    ch.sym(shoulder)
    # the team guitar across the front: its body at the right hip, a white pickguard, the neck up to the left shoulder
    ch.solid("torso", "team", GUITAR, "z", -178, -140)
    plate(ch, "torso", "white", [(10, 845), (130, 845), (140, 925), (0, 925)], -186, -176)
    ch.rod("torso", "hat_dk", (40, 1000, -160), (-232, 1330, -152), 24, k=4)
    plate(ch, "torso", "hairblk", [(-226, 1316), (-256, 1290), (-310, 1352), (-276, 1384)], -168, -138)
    # the face: shades
    visor(ch, b, "eye", 1572, 1606, grow=4, depth=12, wrap=False)
    # the huge black mane: over the crown, a fringe, flaring out round the sides and back past the jaw
    ch.loft("head", "hairblk", offset(hs.at(1614), 19), 1614, ring(120, 124, cz=14), 1808, "y")
    sides = []
    for w, g, y in ((1462, 40, 1462), (1650, 18, 1650)):
        p, q = offset(hs.at(w), g), offset(hs.at(w), -8)
        sides.append(([p[i] for i in BACK_WRAP] + [q[i] for i in reversed(BACK_WRAP)], y))
    ch.sweep("head", "hairblk", sides, "y")
    return ch
