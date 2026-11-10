"""FIGHTER: the brawler (box design: characters_outlined.fighter)."""
from players_lowpoly.kit import Char, Rig, body, plate, profile, helmet, hexring, ngon, rotx, rotz

DESC = "the brawler: bare arms, a team tank top and headband, red boxing gloves, dark trousers, bare feet"


def build():
    ch = Char("FIGHTER", DESC, Rig(hip=900, hipx=130, knee=470, ankle=70))
    b = body(ch, legs="dark", shins="dark", feet="skin", pelvis="dark", torso="team", head="skin",
             thigh=(84, 84, 70, 72), foot=(60, 160, 75), calf=False, thigh_mid=False,
             pelvis_w=(170, 200), pelvis_d=(112, 122), waist=(185, 118), chest=(225, 138), shoulder=(215, 122), chest_y=1270,
             head_w=157, head_d=152, head_top=1765, jaw=0.8, crown=0.78)
    # the trousers: a team stripe down each outer side, the light ankle wraps
    ch.sym(lambda sd, s: ch.solid("thigh" + s, "team", ngon(sd * 196, 0, 50, 4, phase=0, sx=0.5), "y", 500, 930, shell=False))
    ch.sym(lambda sd, s: ch.solid("shin" + s, "team", ngon(sd * 184, 0, 46, 4, phase=0, sx=0.5), "y", 140, 470, shell=False))
    b.wrap("shin_", 60, 125, "light", grow=6, shell=False)
    b.wrap("pelvis", 850, 925, "team", grow=8, shell=False)                          # the belt
    # the tank top's skin neckline
    plate(ch, "torso", "skin", [(-80, 1418), (80, 1418), (0, 1250)], -160, -136)
    # the deltoids, the bare arms hanging at the sides, the red gloves
    def arm(sd, s):
        with ch.placed("torso", rotz(-sd * 0.12), (sd * 276, 1196, 0)):
            ch.loft("torso", "skin", hexring(84, 88, sd * 276), 1196, hexring(60, 68, sd * 258), 1408, "y")
        ch.rod("torso", "skin", (sd * 272, 1230, 0), (sd * 262, 1015, -8), 58, k=4)
        with ch.placed("torso", rotx(0.12), (0, 940, 0)):
            ch.loft("torso", "glove", hexring(80, 90, sd * 266, -18), 850, hexring(76, 84, sd * 264, -10), 1030, "y")
    ch.sym(arm)
    # the head: short dark hair, the team headband and its tail, eyes
    helmet(ch, b, "hair", 1690, grow=6, top=10)
    b.wrap("head", 1630, 1690, "team", grow=8, shell=False)
    profile(ch, "head", "team", [(150, 1680), (178, 1660), (176, 1520), (156, 1540)], -30, 30)
    plate(ch, "head", "eye", [(-62, 1570), (62, 1570), (62, 1592), (-62, 1592)], -160, -148)
    return ch
