"""LUMBERJACK: the lumberjack (box design: characters_outlined_extra2.lumberjack)."""
from players_lowpoly.kit import Char, Rig, Segment, body, plate, ring, hexring, shift, offset, rotz, FRONT_WRAP

DESC = "the lumberjack: a team plaid shirt, a bushy brown beard, a knit beanie, an axe on the back, rolled-up jeans and big boots"


def stripe(ch, seg, x0, x1, t, back=False, grow=4.0, depth=10.0):
    """a vertical stripe down the flat front (or back) face of a ring()-made body piece, x0..x1, following it ring to
    ring (the plaid's lines)"""
    rings_ = []
    for p, w in seg.rings:
        z = max(v for _, v in p) if back else min(v for _, v in p)
        s = 1 if back else -1
        rings_.append(([(x0, z + s * grow), (x1, z + s * grow), (x1, z - s * depth), (x0, z - s * depth)], w))
    ch.sweep(seg.bone, t, rings_, "y", shell=False)


def build():
    ch = Char("LUMBERJACK", DESC, Rig(hip=900, hipx=130, knee=470, ankle=135))
    b = body(ch, legs="denim", feet="leather", pelvis="denim", torso="team", head=None,
             thigh_mid=False, calf=False, thigh=(84, 82, 70, 72), shin=(70, 72, 74, 70), foot=(80, 205, 100),
             pelvis_w=(175, 205), pelvis_d=(118, 128), waist=(218, 136), chest=(248, 150), shoulder=(218, 130), chest_y=1250)
    # the head: jaw, cheeks, its top under the beanie
    hs = Segment(ch, "head", [(ring(118, 120), 1430), (ring(146, 146), 1545), (ring(142, 140), 1680)])
    ch.sweep("head", "skin", hs.rings)
    b.seg["head"] = hs
    # the jeans rolled up over the big boots
    b.wrap("shin_", 125, 190, "denim", grow=9, shell=False)
    # the plaid shirt: team_dk lines across and down it, a leather belt
    b.wrap("torso", 1150, 1190, "team_dk", grow=4, shell=False)
    for x in (-110, 60):
        stripe(ch, b.seg["torso"], x, x + 40, "team_dk")
    stripe(ch, b.seg["torso"], -20, 20, "team_dk", back=True)
    b.wrap("torso", 915, 960, "leather", grow=7, shell=False)

    # the shoulders in the plaid, a line round each
    def shoulder(sd, _):
        cx = sd * 288
        with ch.placed("torso", rotz(-sd * 0.24), (cx, 1290, 0)):
            ch.loft("torso", "team", hexring(104, 118, cx), 1180, shift(hexring(64, 86, cx), -sd * 24), 1420 - ch.outline - 4, "y")
            ch.loft("torso", "team_dk", hexring(106, 120, cx), 1240, hexring(103, 117, cx), 1272, "y", shell=False)
    ch.sym(shoulder)
    # the axe on the back: the handle up past the right shoulder blade, the head beside it
    ch.rod("torso", "hat", (60, 900, 168), (110, 1390, 168), 20, k=4)
    ch.solid("torso", "light", [(110, 1250), (220, 1210), (240, 1300), (230, 1390), (110, 1360)], "z", 156, 180)
    # the eyes
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 30, 1585), (sd * 66, 1585), (sd * 66, 1605), (sd * 30, 1605)], -150, -140))
    # the bushy beard: from the sideburns round the jaw to a point on the chest
    top, bot = offset(hs.at(1575), 11), offset(hs.at(1430), 12)
    tin, bin_ = offset(hs.at(1575), -14), offset(hs.at(1430), -40)
    tp = [top[i] for i in FRONT_WRAP] + [tin[i] for i in reversed(FRONT_WRAP)]
    bp = [(u * 0.82, v - 34) for u, v in (bot[i] for i in FRONT_WRAP)] + [(u * 0.8, v - 10) for u, v in (bin_[i] for i in reversed(FRONT_WRAP))]
    ch.loft("head", "hair", bp, 1372, tp, 1575, "y")
    plate(ch, "head", "hair", [(-78, 1536), (78, 1536), (64, 1566), (-64, 1566)], -176, -150)    # the moustache
    # the knit beanie: a mid cuff, the mid_dk crown
    ch.loft("head", "mid_dk", offset(hs.at(1640), 12), 1640, ring(96, 96, cz=4), 1800, "y")
    ch.loft("head", "mid", offset(hs.at(1630), 20), 1630, offset(hs.at(1680), 18), 1690, "y", shell=False)
    return ch
