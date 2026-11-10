"""BANDIT: the bandit (box design: characters_outlined.bandit)."""
from players_lowpoly.kit import Char, Rig, body, plate, brim, hexring, ngon, ring, ccw, shift, rotz

DESC = "the bandit: a wide tan cowboy hat with a team band, a team bandana over the face, a leather vest, chaps and a bandolier"


def build():
    ch = Char("BANDIT", DESC, Rig(hip=900, hipx=130, knee=470, ankle=120))
    # denim jeans under leather chaps, the team chaps' lower legs, leather boots with gold spurs
    b = body(ch, legs="denim", shins="team", feet="leather", pelvis="leather", torso="team", head=None,
             foot=(66, 180, 85), calf=False, thigh_mid=False,
             pelvis_w=(170, 198), pelvis_d=(112, 122), waist=(205, 125), chest=(225, 138), shoulder=(210, 120))
    ch.sym(lambda sd, s: ch.loft("thigh" + s, "leather", [(sd * 130 - 84, -76), (sd * 130 + 92, -76), (sd * 130 + 92, 40), (sd * 130 - 40, 40)], 500,
                                 [(sd * 130 - 86, -76), (sd * 130 + 96, -76), (sd * 130 + 96, 40), (sd * 130 - 40, 40)], 820, "y"))
    ch.sym(lambda sd, s: ch.rod("foot" + s, "gold", (sd * 130, 60, 110), (sd * 130, 60, 150), 26, k=3, shell=False))
    # the gun belt, its buckle and the holster on the right hip
    plate(ch, "pelvis", "gold", [(-40, 850), (40, 850), (40, 930), (-40, 930)], -140, -124)
    ch.solid("pelvis", "dark", hexring(34, 56, 222, 0), "y", 730, 920)
    # the leather vest round the team shirt (the shirt shows down the front and over the shoulders), the bandolier
    fz = [(950, 132, 84), (1250, 152, 76), (1310, 145, 74)]
    ch.sweep("torso", "team", [(ccw([(-w, -f - 10), (w, -f - 10), (w, -f + 14), (-w, -f + 14)]), y) for y, f, w in fz], shell=False)
    b.wrap("torso", 940, 1310, "leather", grow=6, shell=False)
    ch.rod("torso", "dark", (-180, 990, -136), (170, 1300, -152), 26, k=4, shell=False)
    b.wrap("torso", 1350, 1418, "team", grow=10, shell=False)                                   # the bandana's knot round the neck
    def shoulder(sd, s):     # (a two-ring pauldron: the kit's has three)
        x = sd * 278
        with ch.placed("torso", rotz(-sd * 0.16), (x, 1220, 0)):
            ch.loft("torso", "team", hexring(84, 96, x), 1220, shift(hexring(56, 66, x), -sd * 20), 1408, "y")
    ch.sym(shoulder)
    # the head: the team bandana over the lower face and its point, the eyes
    ch.solid("head", "skin", ring(146, 144, 50), "y", 1430, 1660)
    ch.solid("head", "team", ring(152, 150, 52), "y", 1430, 1565, shell=False)
    plate(ch, "head", "team", [(-60, 1440), (60, 1440), (0, 1380)], -168, -150)
    plate(ch, "head", "eye", [(-70, 1584), (70, 1584), (70, 1606), (-70, 1606)], -160, -146)
    # the cowboy hat: a wide brim curled up at the sides, the team band, its crown
    brim(ch, "hat", 1650, 120, 184, thick=12, up=22, k=8)
    ch.solid("head", "team", ngon(0, 0, 124, 8), "y", 1656, 1712, shell=False)
    ch.loft("head", "hat", ngon(0, 0, 118, 8), 1710, ngon(0, 0, 92, 8), 1802, "y")
    return ch
