"""TVHEAD: the TV head (box design: characters_outlined_extra2.tvhead)."""
from players_lowpoly.kit import Char, Rig, body, plate, rrect, hexring, rotx, rotz

DESC = "the TV head: an old cream CRT set for a head with a lit green screen face and rabbit-ear antennae, a team suit jacket, grey trousers, white shirt, dark tie"


def shoulders(ch, t, x=282, y0=1236, y1=1408, w=80, d=94, tilt=0.18):
    """a rounded shoulder each side: one tapered hexagonal block"""
    def one(sd, _):
        cx = sd * x
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            ch.loft("torso", t, hexring(w, d, cx), y0, hexring(w * 0.7, d * 0.72, cx - sd * w * 0.2), y1, "y")
    ch.sym(one)


def build():
    ch = Char("TVHEAD", DESC, Rig(hip=900, hipx=130, knee=450, ankle=70))
    b = body(ch, legs="mid_dk", feet="dark", pelvis="team", torso="team", head=None,
             thigh_mid=False, calf=False, foot=(64, 175, 85), thigh=(80, 80, 68, 72), shin=(66, 70, 70, 67),
             waist=(196, 120), chest=(222, 136), shoulder=(200, 116), pelvis_w=(178, 205), pelvis_d=(118, 130))
    # the shirt front: a white V under the lapels, the dark tie; laid on the chest's slope
    with ch.placed("torso", rotx(0.21), (0, 1250, -148)):
        plate(ch, "torso", "white", [(-74, 1405), (0, 1180), (74, 1405)], -158, -148)
        ch.sym(lambda sd, s: plate(ch, "torso", "team_dk", [(sd * 74, 1405), (sd * 104, 1405), (sd * 30, 1170), (sd * 8, 1186)], -160, -146))
        plate(ch, "torso", "dark", [(-16, 1250), (16, 1250), (24, 1370), (-24, 1370)], -164, -155)
    with ch.placed("torso", rotx(-0.096), (0, 1250, -148)):
        plate(ch, "torso", "dark", [(-16, 1250), (16, 1250), (24, 1100), (0, 1072), (-24, 1100)], -164, -146)
    plate(ch, "torso", "team_dk", [(-120, 1000), (-50, 1000), (-50, 1022), (-120, 1022)], -144, -126)     # the pocket
    # the jacket's padded shoulders, the white collar
    shoulders(ch, "team")
    ch.loft("torso", "white", hexring(118, 96), 1386, hexring(84, 70), 1446, "y")
    # the neck, the CRT set: a chamfered cream cabinet and the tube's tapered back
    ch.loft("head", "dark", hexring(52, 50), 1430, hexring(48, 46), 1466, "y", shell=False)
    cab = rrect(-166, -96, 166, 64, 42)
    ch.sweep("head", "crt", [(rrect(-150, -84, 150, 54, 36), 1452), (cab, 1474), (cab, 1650), (rrect(-150, -84, 150, 54, 36), 1672)])
    ch.loft("head", "crt", rrect(-112, 1486, 112, 1640, 30), 60, rrect(-72, 1516, 72, 1612, 20), 124, "z")
    # the dark bezel, the lit screen, its pixel face; the knobs beside it
    ch.solid("head", "dark", rrect(-150, 1482, 76, 1648, 26), "z", -104, -94, shell=False)
    ch.solid("head", "screen", rrect(-136, 1494, 62, 1636, 22), "z", -110, -102, shell=False)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(-37 + sd * 34 - 13, 1574), (-37 + sd * 34 + 13, 1574), (-37 + sd * 34 + 13, 1604), (-37 + sd * 34 - 13, 1604)], -115, -108))
    plate(ch, "head", "eye", [(-90, 1530), (16, 1530), (30, 1546), (16, 1556), (-90, 1556), (-104, 1546)], -115, -108)
    for y in (1530, 1590):
        ch.solid("head", "dark", hexring(20, 20, 120, y), "z", -112, -94, shell=False)
    # the rabbit-ear antennae
    ch.solid("head", "dark", hexring(34, 30), "y", 1660, 1690, shell=False)
    ch.sym(lambda sd, s: ch.rod("head", "light", (sd * 12, 1680, 0), (sd * 80, 1786, 4), 9, k=4))
    return ch
