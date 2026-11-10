"""DEMOLITION: the bomb tech (box design: characters_outlined_extra2.demolition)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, band, plate, ring, FRONT, BACK

DESC = "the bomb tech: a bulky sand blast suit, a huge padded collar round a helmet with a flat clear visor, team padding, a belt of team charges with wires"


def build():
    ch = Char("DEMOLITION", DESC, Rig(hip=880, hipx=140, knee=460, ankle=130))
    b = body(ch, legs="sand", shins="sand", feet="dark", pelvis="sand", torso="sand", head="sand", calf=False, thigh_mid=False,
             thigh=(92, 90, 82, 84), shin=(82, 84, 86, 76), foot=(80, 200, 100), pelvis_w=(195, 235), pelvis_d=(135, 150),
             waist=(235, 150), chest=(262, 166), shoulder=(228, 142), chest_y=1215, head_w=154, head_d=154, head_top=1772, jaw=0.92, crown=0.74)
    # the team padding: thigh rolls round each thigh, a pad down the front of each shin
    b.wrap("thigh_", 520, 840, "team", grow=10, shell=False)
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "team", [(sd * 140 - 62, 170), (sd * 140 + 62, 170), (sd * 140 + 54, 420), (sd * 140 - 54, 420)], -86, -66))
    # the blast plates, front and back
    band(ch, b, "torso", "team", 990, 1330, FRONT, grow=14, depth=10)
    band(ch, b, "torso", "team", 990, 1340, BACK, grow=12, depth=10)
    # the charge belt: four team charges, a lit timer in the middle
    b.wrap("torso", 905, 985, "leather", grow=9, shell=False)
    for x in (-215, -130, 60, 145):
        plate(ch, "torso", "team", [(x, 895), (x + 70, 895), (x + 70, 985), (x, 985)], -190, -150)
    plate(ch, "torso", "visor", [(-30, 905), (30, 905), (30, 975), (-30, 975)], -176, -150)
    pauldron(ch, "sand", x=292, y0=1150, w=92, d=112, tilt=0.2, band="team")
    # the padded collar round the helmet's base
    ch.loft("torso", "sand", ring(205, 160, front=160), 1290, ring(250, 200, front=190), 1428, "y")
    # the helmet: a team pad round the back and sides, the flat clear visor over it a lamp on top
    b.wrap("head", 1485, 1660, "team", grow=6, shell=False)
    visor(ch, b, "glass", 1480, 1650, grow=9, depth=10, wrap=False)
    ch.loft("head", "lamp", [(-30, -100), (30, -100), (30, -60), (-30, -60)], 1740, [(-22, -92), (22, -92), (22, -66), (-22, -66)], 1785, "y", shell=False)
    return ch
