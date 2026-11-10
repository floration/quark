"""MUMMY: the mummy (box design: characters_outlined_extra.mummy)."""
from players_lowpoly.kit import Char, Rig, Segment, body, plate, ring, hexring, rotz, rotx

DESC = "the mummy: cream bandages wrapped head to toe with darker and team bands, loose strips, a hunched head with one glowing eye"


def build():
    ch = Char("MUMMY", DESC, Rig(hip=900, hipx=130, knee=450, ankle=90))
    b = body(ch, legs="bandage", shins="bandage", feet="bandage", pelvis="bandage_dk", torso="bandage", head=None,
             thigh=(82, 82, 72, 74), shin=(70, 74, 74, 68), foot=(64, 175, 85), thigh_mid=False, calf=False,
             waist=(188, 118), chest=(210, 128), shoulder=(192, 112), chest_y=1250)

    def tilted(name, bone, y0, y1, t, tilt, cx=0.0, grow=6.0):
        """a bandage band wound on a slant: tilted by `tilt` (radians) about the piece's axis"""
        with ch.placed(bone, rotz(tilt), (cx, (y0 + y1) / 2, 0)):
            b.seg[name].wrap(y0, y1, t, grow=grow, shell=False)

    # the legs: a wide team band and a darker one each, wound on a slant
    def leg(sd, s):
        x = sd * 130
        tilted("thigh" + s, "thigh" + s, 540, 740, "team", sd * 0.16, x)
        tilted("thigh" + s, "thigh" + s, 790, 830, "bandage_dk", -sd * 0.14, x)
        tilted("shin" + s, "shin" + s, 200, 370, "team", -sd * 0.16, x)
    ch.sym(leg)
    # the body: two wide team bands and a darker one, slanted, the team amulet, the loose strips
    tilted("torso", "torso", 975, 1100, "team", 0.12)
    tilted("torso", "torso", 1255, 1370, "team", -0.12)
    plate(ch, "torso", "team", [(-52, 1150), (52, 1150), (52, 1226), (0, 1250), (-52, 1226)], -150, -126)
    plate(ch, "torso", "bandage", [(-172, 780), (-130, 780), (-120, 990), (-162, 990)], -132, -118)
    plate(ch, "torso", "bandage", [(78, 830), (120, 830), (126, 1040), (84, 1040)], 112, 126)

    # the wrapped shoulders, a team band round each
    def shoulder(sd, s):
        x = sd * 272
        with ch.placed("torso", rotz(-sd * 0.2), (x, 1220, 0)):
            sh = Segment(ch, "torso", [(hexring(86, 96, x), 1210), (hexring(58, 66, x - sd * 22), 1405)])
            ch.sweep("torso", "bandage", sh.rings)
            sh.wrap(1245, 1335, "team", grow=6, shell=False)
    ch.sym(shoulder)
    # the darker neck wrap
    b.wrap("torso", 1360, 1408, "bandage_dk", grow=12, shell=False)
    # the hunched head, pushed forward, its darker wraps, one glowing eye and a slit for the other
    hs = Segment(ch, "head", [(ring(116, 108, cz=-8), 1430), (ring(156, 158, front=134, cz=-16), 1540),
                              (ring(154, 160, front=134, cz=-20), 1660), (ring(108, 108, front=92, cz=-22), 1766)])
    ch.sweep("head", "bandage", hs.rings)
    b.seg["head"] = hs
    with ch.placed("head", rotx(0.1), (0, 1520, -16)):
        hs.wrap(1500, 1536, "bandage_dk", grow=6, shell=False)
    with ch.placed("head", rotx(-0.12), (0, 1650, -20)):
        hs.wrap(1620, 1712, "team", grow=6, shell=False)
    plate(ch, "head", "zeye", [(24, 1584), (70, 1584), (70, 1614), (24, 1614)], -166, -148)
    plate(ch, "head", "eye", [(-70, 1594), (-24, 1594), (-24, 1606), (-70, 1606)], -166, -148)
    return ch
