"""FLAMER: the flamer (box design: characters_outlined_extra2.flamer)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, plate, ngon

DESC = "the flamer: a sealed baggy hazmat suit, a gas mask with round goggles and twin filters, team boots and gloves straps, twin team fuel tanks and a hose on the back"


def build():
    ch = Char("FLAMER", DESC, Rig(hip=900, hipx=130, knee=470, ankle=130))
    b = body(ch, legs="hazmat", shins="team", feet="team", pelvis="hazmat", torso="hazmat", head="hazmat", calf=False, thigh_mid=False,
             thigh=(90, 88, 84, 86), shin=(84, 86, 88, 80), foot=(78, 195, 96), pelvis_w=(185, 215), pelvis_d=(125, 138),
             waist=(218, 136), chest=(240, 150), shoulder=(212, 128), chest_y=1220, head_w=154, head_d=138, head_top=1770, jaw=0.86, crown=0.74)
    # (the team rubber boots come up to the knee)
    # the team belt
    b.wrap("torso", 925, 1050, "team", grow=8, shell=False)
    # the twin team fuel tanks on the back, the strap and the hose round to the right hip
    ch.sym(lambda sd, s: ch.solid("torso", "team", ngon(sd * 100, 240, 92, 6, phase=0), "y", 950, 1395))
    plate(ch, "torso", "dark", [(-196, 1150), (196, 1150), (196, 1196), (-196, 1196)], 140, 334)
    ch.rod("torso", "dark", (150, 1000, 300), (262, 990, 150), 22, k=4)
    ch.rod("torso", "dark", (262, 990, 150), (255, 930, -40), 22, k=4)
    pauldron(ch, "hazmat", x=284, y0=1190, w=86, d=104, tilt=0.18, band="team")     # the gloves' team straps under them
    # the gas mask: a dark face plate, round goggles, the snout and its twin filters
    visor(ch, b, "dark", 1470, 1665, grow=5, depth=12)
    ch.sym(lambda sd, s: plate(ch, "head", "glass", ngon(sd * 58, 1608, 40, 6), -150, -140))
    ch.loft("head", "dark", [(-42, -142), (42, -142), (42, -100), (-42, -100)], 1475, [(-30, -176), (30, -176), (30, -142), (-30, -142)], 1545, "y")
    ch.sym(lambda sd, s: ch.rod("head", "mid", (sd * 34, 1500, -138), (sd * 82, 1468, -126), 28, k=6, shell=False))
    return ch
