"""PENGUIN: the penguin mascot (box design: characters_outlined_extra2.penguin)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, plate, ring, hexring, ngon, rotz, FRONT, FRONT_WRAP

DESC = "the penguin mascot: a pear-shaped black body with a white front, flippers, an orange beak and wide orange feet, team flippers, a team bobble hat and scarf"


def pear_thighs(ch, b, t):
    """swap the kit's thighs for fat ones bulging out below the hips: each the half of the pear's bottom"""
    r = ch.rig
    for sd, sfx in ((1, "_r"), (-1, "_l")):
        x = sd * r.hipx
        ch.mesh["thigh" + sfx].tris.clear()
        th = Segment(ch, "thigh" + sfx, [(hexring(118, 150, x), r.hip + 40), (hexring(134, 162, x + sd * 6), 640), (hexring(70, 76, x), r.knee - 25)])
        ch.sweep("thigh" + sfx, t, th.rings)
        b.seg["thigh" + sfx] = th
        ch.limbs = [(n, sg) for n, sg in ch.limbs if n != "thigh" + sfx] + [("thigh" + sfx, th)]


def build():
    ch = Char("PENGUIN", DESC, Rig(hip=880, hipx=130, knee=440, ankle=40))
    # the pear: fat thighs make its bottom half (the box design's black, at the darkest big-area tone), stubby legs
    # in team stockings, wide flat orange feet
    b = body(ch, legs="dark", shins="team", feet="carrot", pelvis="dark", torso="dark", head=None,
             shin=(70, 72, 68, 68), foot=(92, 205, 70), calf=False, thigh_mid=False,
             pelvis_w=(240, 236), pelvis_d=(164, 158), waist=(230, 154), chest=(212, 140), shoulder=(168, 116), chest_y=1150)
    pear_thighs(ch, b, "dark")
    # the white front, from the bottom of the pear to the chest
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "white", [(sd * 130 - 60, 520), (sd * 130 + 60, 520), (sd * 136 + 66, 640), (sd * 130 + 58, 900), (sd * 130 - 58, 900), (sd * 136 - 66, 640)], -150, -128))
    band(ch, b, "pelvis", "white", 770, 950, FRONT, grow=5, depth=14)
    band(ch, b, "torso", "white", 930, 1310, FRONT_WRAP, grow=6, depth=14)
    # the team flippers, held out from the shoulders, their dark-team tips
    for sd in (1, -1):
        with ch.placed("torso", rotz(sd * 0.1), (sd * 190, 1380, 0)):
            plate(ch, "torso", "team", [(sd * 160, 1405), (sd * 262, 1400), (sd * 340, 1000), (sd * 300, 930), (sd * 236, 990), (sd * 168, 1240)], -50, 50, shell=True)
            plate(ch, "torso", "team_dk", [(sd * 340, 1000), (sd * 300, 930), (sd * 236, 990), (sd * 256, 1050), (sd * 330, 1050)], -54, 54)
    # the team scarf round the neck, its ends hanging front and back
    ch.loft("torso", "team", ring(176, 128), 1310, ring(146, 104), 1428 - ch.outline, "y")
    plate(ch, "torso", "team", [(46, 1330), (126, 1330), (140, 1040), (64, 1050)], -150, -130)
    plate(ch, "torso", "team", [(-136, 1330), (-52, 1330), (-40, 1070), (-122, 1060)], 118, 138)
    # the round dark head, its white face, eyes and the orange beak
    ch.sweep("head", "dark", [(ring(124, 120), 1436), (ring(156, 150), 1560), (ring(146, 140), 1650)])
    plate(ch, "head", "white", [(-96, 1480), (96, 1480), (112, 1610), (0, 1650), (-112, 1610)], -162, -144)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 36, 1580), (sd * 64, 1580), (sd * 50, 1606)], -166, -160))
    ch.loft("head", "carrot", [(-40, 1508), (40, 1508), (30, 1560), (-30, 1560)], -150, [(-8, 1522), (8, 1522), (6, 1532), (-6, 1532)], -186, "z")
    # the team bobble hat: a dark-team cuff, the hat, its bobble
    ch.loft("head", "team_dk", ring(160, 154), 1636, ring(158, 152), 1692, "y")
    ch.loft("head", "team", ring(146, 140), 1692, ring(92, 90), 1772, "y")
    ch.solid("head", "team_dk", ngon(0, 0, 42, 4), "y", 1768, 1822)
    return ch
