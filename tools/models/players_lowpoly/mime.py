"""MIME: the mime (box design: characters_outlined_extra2.mime)."""
from players_lowpoly.kit import Char, Rig, body, plate, ring, hexring, ngon, rotz

DESC = "the mime: a white face with black lips and a painted tear, a tilted black beret, a team-and-white striped shirt, black braces and trousers, a team neck scarf"


def build():
    ch = Char("MIME", DESC, Rig(hip=900, hipx=130, knee=450, ankle=80))
    # trousers in the team's dark shade (the box design's black: the team colour has to reach the legs), black shoes, a white striped shirt
    b = body(ch, legs="team_dk", shins="team_dk", feet="dark", pelvis="team_dk", torso="white", head="white",
             thigh=(78, 76, 64, 70), shin=(68, 70, 72, 70), foot=(62, 165, 85), calf=False, thigh_mid=False,
             waist=(182, 112), chest=(214, 128), shoulder=(196, 112), chest_y=1240, jaw=0.8, crown=0.78, head_top=1735)
    # the shirt's team stripes (wider than the white between, so the team colour reads front and back)
    for y0, y1 in ((945, 1015), (1075, 1150), (1210, 1290)):
        b.wrap("torso", y0, y1, "team", grow=5, shell=False)
    # the black braces, front and back, over the stripes
    for sd in (1, -1):
        plate(ch, "torso", "hairblk", [(sd * 84, 920), (sd * 106, 920), (sd * 100, 1400), (sd * 78, 1400)], -158, -120)
        plate(ch, "torso", "hairblk", [(sd * 84, 920), (sd * 106, 920), (sd * 100, 1400), (sd * 78, 1400)], 110, 140)
    # the sleeves' tops: team with a white cuff (the box design's striped sleeves)
    for sd in (1, -1):
        with ch.placed("torso", rotz(-sd * 0.16), (sd * 282, 1232, 0)):
            ch.loft("torso", "team", hexring(80, 92, sd * 282), 1232, hexring(56, 66, sd * 266), 1406, "y")
            ch.loft("torso", "white", hexring(86, 98, sd * 282), 1226, hexring(82, 94, sd * 280), 1280, "y", shell=False)
    # the team neck scarf and its tail
    ch.loft("torso", "team", ring(150, 104), 1350, ring(124, 92), 1428 - ch.outline, "y")
    plate(ch, "torso", "team", [(40, 1365), (92, 1365), (100, 1230), (60, 1218)], -150, -132)
    # the white face: dark eyes under arched brows, a painted tear on the left cheek, black lips
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 30, 1598), (sd * 52, 1588), (sd * 74, 1598), (sd * 52, 1610)], -162, -146))
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 26, 1628), (sd * 52, 1648), (sd * 80, 1630)], -162, -146))
    plate(ch, "head", "eye", [(-60, 1574), (-44, 1574), (-52, 1520)], -162, -146)
    plate(ch, "head", "hairblk", [(-34, 1512), (0, 1500), (34, 1512), (0, 1526)], -164, -146)
    # the beret: a puffy black disc tipped to the model's left, its stalk
    with ch.placed("head", rotz(0.2), (0, 1700, 0)):
        ch.sweep("head", "hairblk", [(ngon(-12, 0, 146, 8), 1682), (ngon(-16, 0, 160, 8), 1716), (ngon(-18, 0, 110, 8), 1752)])
        ch.solid("head", "hairblk", ngon(-18, 0, 14, 4), "y", 1748, 1786, shell=False)
    return ch
