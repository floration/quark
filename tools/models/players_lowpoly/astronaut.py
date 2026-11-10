"""ASTRONAUT: the astronaut (box design: characters_outlined_extra.astronaut)."""
from players_lowpoly.kit import Char, Rig, Segment, body, pauldron, visor, plate, backpack, ring, hexring, ngon

DESC = "the astronaut: a puffy white suit, a big round helmet with a gold visor, a life-support pack, team shoulders, knees and chest box"


def build():
    ch = Char("ASTRONAUT", DESC, Rig(hip=910, hipx=130, knee=460, ankle=150))
    b = body(ch, legs="white", shins="white", feet="white", pelvis="light", torso="white", head=None,
             thigh=(94, 94, 84, 86), shin=(84, 86, 88, 80), foot=(78, 205, 105), thigh_mid=False, calf=False,
             pelvis_w=(185, 215), pelvis_d=(128, 140), waist=(215, 140), chest=(240, 150), shoulder=(215, 132), chest_y=1240)
    # the moon boots' dark soles
    ch.sym(lambda sd, s: ch.solid("foot" + s, "dark", [(112, 10), (-212, 10), (-216, 52), (112, 52)], "x", sd * 130 - 84, sd * 130 + 84, shell=False))
    # puffy team knee pads round the knee, the thighs' team pockets band, the light boot rings
    b.wrap("shin_", 330, 485, "team", grow=10)
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "team", [(sd * 130 - 60, 560), (sd * 130 + 60, 560), (sd * 130 + 64, 800), (sd * 130 - 64, 800)], -102, -70))
    # the suit: a team belt, the chest box with its dial and status light
    b.wrap("torso", 935, 1070, "team", grow=9, shell=False)
    plate(ch, "torso", "team", [(-120, 1080), (120, 1080), (120, 1270), (-120, 1270)], -186, -140, shell=True)
    plate(ch, "torso", "light", [(-100, 1185), (-15, 1185), (-15, 1250), (-100, 1250)], -192, -180)    # the dial
    plate(ch, "torso", "visor", [(25, 1205), (85, 1205), (85, 1245), (25, 1245)], -192, -180)       # the status light
    # the life-support pack and its team panel
    backpack(ch, "light", 185, 990, 1395, 135, 105)
    plate(ch, "torso", "team", [(-150, 1020), (150, 1020), (150, 1360), (-150, 1360)], 236, 250)
    pauldron(ch, "team", x=292, y0=1190, w=92, d=116, tilt=0.16)
    # the neck ring and a big round helmet
    hs = Segment(ch, "head", [(ring(126, 124), 1436), (ring(160, 156), 1505), (ring(156, 152), 1695), (ring(92, 90), 1768)])
    ch.sweep("head", "white", hs.rings)
    b.seg["head"] = hs
    visor(ch, b, "amber", 1505, 1695, grow=5, depth=22)                                    # the gold visor
    # the team ear lamps and a team stripe over the helmet
    ch.sym(lambda sd, s: ch.solid("head", "team", ngon(0, 1600, 44, 4), "x", *sorted((sd * 150, sd * 182)), shell=False))
    ch.solid("head", "team", [(-156, 1690), (-98, 1773), (-50, 1792), (50, 1792), (98, 1773), (156, 1690)], "x", -50, 50, shell=False)
    return ch
