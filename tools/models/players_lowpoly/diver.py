"""DIVER: the deep-sea diver (box design: characters_outlined_extra.diver)."""
from players_lowpoly.kit import Char, Rig, body, plate, hexring, ngon, rotz

DESC = "the deep-sea diver: a big brass dome helmet with portholes, a canvas suit, lead boots and team weights front and back"


def build():
    ch = Char("DIVER", DESC, Rig(hip=910, hipx=130, knee=460, ankle=150))
    # the canvas suit; the shins are the team weighted gaiters over the lead boots (the box's are canvas: team here
    # so the legs read)
    b = body(ch, legs="canvas", shins="team", feet="dark", pelvis="team", torso="canvas", head=None,
             thigh=(90, 90, 78, 80), shin=(80, 82, 86, 80), foot=(78, 200, 100), thigh_mid=False, calf=False,
             pelvis_w=(180, 214), pelvis_d=(128, 136), waist=(212, 132), chest=(232, 144), shoulder=(200, 120), chest_y=1230)
    # the lead boots' brass toe caps, the leather straps round the gaiters' tops
    ch.sym(lambda sd, s: ch.solid("foot" + s, "brass_dk", [(-110, 4), (-188, 4), (-208, 44), (-150, 93), (-110, 93)], "x",
                                  sd * 130 - 83, sd * 130 + 83, shell=False))
    b.wrap("shin_", 380, 430, "leather", grow=6, shell=False)
    # the leather belt, the team weights front and back
    b.wrap("torso", 932, 1000, "leather", grow=8, shell=False)
    plate(ch, "torso", "team", [(-175, 1010), (175, 1010), (165, 1265), (-165, 1265)], -180, -120, shell=True)
    plate(ch, "torso", "team", [(-175, 1010), (175, 1010), (165, 1265), (-165, 1265)], 120, 180, shell=True)
    # the brass corselet round the shoulders
    b.wrap("torso", 1270, 1408, "brass", grow=16)

    # the team shoulders
    def shoulder(sd, s):
        x = sd * 278
        with ch.placed("torso", rotz(-sd * 0.18), (x, 1210, 0)):
            ch.sweep("torso", "team", [(hexring(90, 106, x), 1200), (hexring(60, 74, x - sd * 22), 1405)])
    ch.sym(shoulder)
    # the big brass dome helmet
    ch.sweep("head", "brass", [(ngon(0, 0, 128, 8), 1432), (ngon(0, 0, 172, 8), 1490), (ngon(0, 0, 176, 8), 1640),
                               (ngon(0, 0, 138, 8), 1728), (ngon(0, 0, 66, 8), 1772)])
    ch.sweep("head", "team", [(ngon(0, 0, 134, 8), 1436), (ngon(0, 0, 178, 8), 1492)], shell=False)      # the team neck ring
    ch.solid("head", "brass_dk", ngon(0, 30, 22, 4), "y", 1765, 1800, shell=False)                     # the valve on top
    # the front porthole: a dark brass frame, the glass, its grille
    ch.solid("head", "brass_dk", ngon(0, 1585, 84, 8), "z", -176, -150, shell=False)
    ch.solid("head", "glass", ngon(0, 1585, 62, 8), "z", -181, -170, shell=False)
    plate(ch, "head", "brass_dk", [(-6, 1527), (6, 1527), (6, 1643), (-6, 1643)], -186, -176)
    plate(ch, "head", "brass_dk", [(-58, 1579), (58, 1579), (58, 1591), (-58, 1591)], -186, -176)
    # the side ports and the air valve at the back
    ch.sym(lambda sd, s: ch.solid("head", "glass", ngon(0, 1585, 44, 6), "x", *sorted((sd * 150, sd * 180)), shell=False))
    ch.solid("head", "brass_dk", ngon(0, 1640, 28, 4), "z", 150, 184, shell=False)
    return ch
