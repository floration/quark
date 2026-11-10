"""GLADIATOR: the gladiator (box design: characters_outlined_extra.gladiator)."""
from players_lowpoly.kit import Char, Rig, body, visor, plate, profile, hexring, ring, rotz

DESC = "the gladiator: a brass helmet with a face grille and a tall team crest, bare skin, a leather strap, a brass left shoulder guard, a team skirt and greaves"


def build():
    ch = Char("GLADIATOR", DESC, Rig(hip=900, hipx=130, knee=470, ankle=40))
    # bare legs bound in team wraps under brass greaves, sandals, a team skirt of strips
    b = body(ch, legs="skin", shins="team", feet="leather", pelvis="team", torso="skin", head="brass", thigh_mid=False, calf=False,
             thigh=(80, 80, 70, 72), shin=(72, 74, 76, 68), foot=(66, 175, 80),
             pelvis_w=(190, 214), pelvis_d=(124, 132), waist=(195, 118), chest=(222, 138), shoulder=(200, 116), chest_y=1250,
             head_w=155, head_d=155, head_top=1712, jaw=0.9, crown=0.78)
    # the greaves: brass shells over the shins' fronts, up over the knees
    ch.sym(lambda sd, s: ch.loft("shin" + s, "brass", hexring(70, 30, sd * 130, -58), 140, hexring(76, 34, sd * 130, -66), 510, "y", shell=False))
    # the skirt: team strips round the thighs, the leather strip down each one
    b.wrap("thigh_", 560, 940, "team", grow=20)
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "leather", [(sd * 130 - 16, 642), (sd * 130 + 16, 642), (sd * 130 + 16, 930), (sd * 130 - 16, 930)], -106, -94))
    b.wrap("torso", 965, 1040, "leather", grow=10, shell=False)                       # the belt
    # the leather strap across the chest, from the right hip to over the left shoulder
    with ch.placed("torso", rotz(-0.62), (0, 1180, 0)):
        plate(ch, "torso", "leather", [(-34, 940), (34, 940), (34, 1420), (-34, 1420)], -160, -138)
        plate(ch, "torso", "leather", [(-34, 940), (34, 940), (34, 1420), (-34, 1420)], 116, 138)
    # the brass guard on the left shoulder, its dark lame under it; the bare right shoulder with an arm wrap
    with ch.placed("torso", rotz(0.2), (-290, 1150, 0)):
        ch.sweep("torso", "brass", [(hexring(96, 118, -282), 1150), (hexring(100, 122, -284), 1300), (hexring(62, 80, -258), 1418 - ch.outline)])
        ch.loft("torso", "brass_dk", hexring(94, 116, -282), 1080, hexring(98, 120, -282), 1152, "y", shell=False)
    with ch.placed("torso", rotz(-0.18), (280, 1225, 0)):
        ch.loft("torso", "skin", hexring(74, 90, 280), 1210, hexring(48, 60, 266), 1408, "y")
        ch.loft("torso", "leather", hexring(80, 96, 280), 1180, hexring(78, 94, 280), 1250, "y", shell=False)
    # the helmet: a brim, a grille over the face, the tall team crest front to back
    ch.loft("head", "brass_dk", ring(166, 162, c=76), 1575, ring(162, 158, c=74), 1600, "y")
    visor(ch, b, "bore", 1460, 1572, grow=3, depth=14, wrap=False)
    for x in (-30, 30):
        plate(ch, "head", "brass", [(x - 7, 1462), (x + 7, 1462), (x + 7, 1572), (x - 7, 1572)], -170, -160)
    profile(ch, "head", "team", [(-150, 1690), (-174, 1756), (-120, 1806), (120, 1806), (178, 1752), (180, 1640), (150, 1680)], -30, 30)
    return ch
