"""PIMP: the 1970s flash (box design: characters_outlined_extra2.pimp)."""
from players_lowpoly.kit import Char, Rig, Segment, body, visor, brim, plate, profile, ring, hexring, shift, rotz

DESC = "the 1970s flash: a long team coat with a white fur collar and cuffs, a wide-brim team hat with a gold band and a tall feather, big shades, a gold chain, platform shoes, a cane at the hip"


def shoulder(ch, t, x, y0, y1, w, d, tilt, cuff=None):
    """a cheaper shoulder than kit.pauldron (one loft, 40 triangles a side): a hexagon at y0 narrowing to the top;
    cuff: a tone for a flush band round its bottom"""
    def one(sd, _):
        cx = sd * x
        with ch.placed("torso", rotz(-sd * tilt), (cx, y0, 0)):
            ch.loft("torso", t, hexring(w, d, cx), y0, shift(hexring(w * 0.7, d * 0.75, cx), -sd * w * 0.22), y1 - ch.outline, "y")
            if cuff:
                ch.loft("torso", cuff, hexring(w * 1.06, d * 1.06, cx), y0 - 34, hexring(w * 1.04, d * 1.04, cx), y0 + 8, "y", shell=False)
    Char.sym(one)


def build():
    ch = Char("PIMP", DESC, Rig(hip=900, hipx=130, knee=470, ankle=110))
    # the coat comes down over the thighs: the legs are team, the white flared trousers show from the knee down
    b = body(ch, legs="team", shins="white", feet="white", pelvis="team", torso="team", head=None, calf=False, thigh_mid=False,
             thigh=(80, 78, 74, 76), shin=(66, 70, 70, 92), foot=(70, 175, 80), pelvis_w=(178, 204), pelvis_d=(120, 130),
             waist=(196, 124), chest=(218, 136), shoulder=(196, 118), chest_y=1240)
    # the head: jaw, cheeks, the top hidden by the hat (three rings; the kit's has four)
    hs = Segment(ch, "head", [(ring(114, 114), 1430), (ring(150, 150), 1550), (ring(128, 128), 1660)])
    ch.sweep("head", "skin", hs.rings)
    b.seg["head"] = hs
    # the gold platform soles
    ch.sym(lambda sd, s: ch.solid("foot" + s, "gold", [(-190, 0), (88, 0), (88, 46), (-182, 46)], "x", sd * 130 - 76, sd * 130 + 76, shell=False))
    # the coat's white fur hem
    b.wrap("thigh_", 470, 530, "white", grow=14, shell=False)
    # the plum shirt, the gold chain and medallion
    plate(ch, "torso", "plum", [(-60, 1040), (60, 1040), (70, 1330), (-70, 1330)], -148, -134)
    plate(ch, "torso", "gold", [(-78, 1330), (-60, 1330), (-6, 1185), (6, 1185), (60, 1330), (78, 1330), (14, 1170), (-14, 1170)], -156, -146)
    plate(ch, "torso", "gold", [(0, 1100), (34, 1140), (0, 1180), (-34, 1140)], -158, -146)
    # the white fur collar, the team shoulders with white fur cuffs
    ch.loft("torso", "white", ring(212, 160), 1300, ring(176, 140), 1428, "y")
    shoulder(ch, "team", 276, 1195, 1418, 86, 98, 0.16, cuff="white")
    # the cane at the left hip, its gold knob
    ch.rod("torso", "hairblk", (-262, 400, -70), (-262, 960, -70), 20, k=4)
    ch.rod("torso", "gold", (-262, 950, -70), (-262, 1010, -70), 36, k=4)
    # big shades
    visor(ch, b, "hairblk", 1552, 1616, grow=6, depth=12, wrap=False)
    # the hat: a wide team brim, the crown with its gold band, the tall feather
    brim(ch, "team", 1640, 112, 187, thick=24, k=8)
    ch.loft("head", "team", ring(112, 106), 1660, ring(96, 92), 1790, "y")
    ch.loft("head", "gold", ring(118, 112), 1662, ring(114, 108), 1702, "y", shell=False)
    profile(ch, "head", "honey", [(-6, 1690), (40, 1690), (34, 1770), (10, 1806), (-12, 1760)], 112, 126)
    return ch
