"""ROBOT: the machine (box design: characters_outlined.robot)."""
from players_lowpoly.kit import Char, Rig, body, pauldron, visor, band, plate, backpack, ngon, FRONT_WRAP

DESC = "the machine: a boxy head with glowing eyes and an antenna, piston legs, a team chest and thigh panels"


def build():
    ch = Char("ROBOT", DESC, Rig(hip=880, hipx=130, knee=480, ankle=140))
    # chunky team thigh blocks over thin mid shins (the pistons), a light chassis, a squared-off light head
    b = body(ch, legs="team", shins="mid", feet="dark", pelvis="dark", torso="light", head="light",
             thigh=(86, 84, 76, 76), shin=(70, 72, 70, 70), calf=False, thigh_mid=False, foot=(84, 185, 105),
             pelvis_w=(170, 190), pelvis_d=(110, 112), waist=(205, 140), chest=(240, 150), shoulder=(225, 140),
             head_w=152, head_d=150, head_top=1700, jaw=0.94, crown=0.93, chest_y=1280)
    # the piston covers down the shins (front and back) and a dark knee joint
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "team", [(sd * 130 - 52, 190), (sd * 130 + 52, 190), (sd * 130 + 46, 440), (sd * 130 - 46, 440)], -76, -54))
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "team", [(sd * 130 - 46, 220), (sd * 130 + 46, 220), (sd * 130 + 42, 430), (sd * 130 - 42, 430)], 54, 74))
    ch.sym(lambda sd, s: ch.rod("shin" + s, "dark", (sd * 130 - 78, 480, 0), (sd * 130 + 80, 480, 0), 46, k=3, shell=False))
    # the chest panel and its glowing core
    band(ch, b, "torso", "team", 1040, 1350, FRONT_WRAP, grow=8, depth=16)
    plate(ch, "torso", "visor", [(-48, 1130), (48, 1130), (48, 1210), (-48, 1210)], -176, -160)
    # the team back block with two dark vents
    backpack(ch, "team", 185, 1000, 1350, 135, 70, c=30)
    ch.sym(lambda sd, s: plate(ch, "torso", "bore", [(sd * 150 - 14, 1050), (sd * 150 + 14, 1050), (sd * 150 + 14, 1300), (sd * 150 - 14, 1300)], 200, 210))
    # square shoulders with a dark bolt through each
    pauldron(ch, "mid", x=285, y0=1200, w=76, d=94, tilt=0.12)
    ch.sym(lambda sd, s: ch.rod("torso", "dark", (sd * 335, 1320, 0), (sd * 378, 1320, 0), 36, k=4, shell=False))
    # the head: an eye bar with two glowing eyes, the team jaw band, ear bolts, the antenna
    visor(ch, b, "bore", 1550, 1620, grow=4, depth=12, wrap=False)
    ch.sym(lambda sd, s: plate(ch, "head", "visor", [(sd * 30, 1565), (sd * 80, 1565), (sd * 80, 1605), (sd * 30, 1605)], -180, -168))
    b.wrap("head", 1505, 1532, "team", grow=6, shell=False)
    ch.sym(lambda sd, s: ch.rod("head", "dark", (sd * 148, 1585, 0), (sd * 172, 1585, 0), 40, k=4, shell=False))
    ch.rod("head", "mid", (55, 1680, 45), (55, 1765, 45), 12, k=4, shell=False)
    ch.solid("head", "team", ngon(55, 45, 30, 4), "y", 1760, 1795)
    return ch
