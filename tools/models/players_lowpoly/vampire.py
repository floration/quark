"""VAMPIRE: the vampire (box design: characters_outlined_extra.vampire)."""
from players_lowpoly.kit import Char, Rig, body, helmet, plate, offset, rotx

DESC = "the vampire: a black cape with team lining and a tall flared collar, a team waistcoat and white jabot, pale skin, slicked hair, red eyes"


def arc(w, zb, zf, t, c, wf=None):
    """a C-shaped section (x, z) open toward -z: a cape or a collar wrapping the back and the sides; w its half
    width at the back, wf at its front edges (wider: the sides flare open and show their inside), zb its back, zf
    where its sides end at the front, t thick, c the back corners' cut"""
    wf = w if wf is None else wf
    return [(wf, zf), (w, zb - c), (w - c, zb), (-w + c, zb), (-w, zb - c), (-wf, zf),
            (-wf + t, zf), (-w + t, zb - c), (-w + c, zb - t), (w - c, zb - t), (w - t, zb - c), (wf - t, zf)]


def strip(ch, seg, t, x0, x1, y0, y1, grow=5.0, depth=14.0):
    """a flat strip on the front face of a ring-built piece from y0 to y1, following its slope"""
    ws = [y0] + [w for _, w in seg.rings if y0 < w < y1] + [y1]
    rings_ = []
    for w in ws:
        z = min(v for _, v in seg.at(w))
        rings_.append(([(x0, z - grow), (x1, z - grow), (x1, z + depth), (x0, z + depth)], w))
    ch.sweep(seg.bone, t, rings_, "y", False)


def build():
    ch = Char("VAMPIRE", DESC, Rig(hip=900, hipx=130, knee=470, ankle=90))
    b = body(ch, legs="dark", shins="dark", feet="dark", pelvis="dark", torso="team", head="pale", thigh_mid=False, calf=False,
             thigh=(76, 78, 68, 70), shin=(68, 70, 70, 68), foot=(64, 180, 90),
             pelvis_w=(175, 195), pelvis_d=(116, 124), waist=(196, 120), chest=(222, 132), shoulder=(205, 118), chest_y=1250,
             head_w=134, head_d=128, head_top=1768, jaw=0.8, crown=0.74)
    # the white jabot down the waistcoat, the red brooch
    strip(ch, b.seg["torso"], "white", -54, 54, 1120, 1400)
    plate(ch, "torso", "glove", [(-18, 1320), (18, 1320), (18, 1350), (-18, 1350)], -146, -134)
    # the cape: dark outside, team lining inside, a team hem; it hangs from the shoulders, flaring back and out
    top, bot = arc(280, 150, -40, 22, 80, 300), arc(320, 240, -10, 22, 100, 410)
    ch.sweep("torso", "dark", [(bot, 600), (top, 1418 - ch.outline)])
    ch.sweep("torso", "team", [(arc(297, 217, -10, 8, 100, 387), 610), (arc(257, 127, -40, 8, 80, 277), 1400)], shell=False)
    ch.sweep("torso", "team", [(offset(bot, 5), 590), (offset(arc(306, 209, -20, 22, 93, 372), 5), 880)], shell=False)
    # its back emblem (a team cross), laid on the cape's slope
    with ch.placed("torso", rotx(-0.125), (0, 1000, 200)):
        plate(ch, "torso", "team", [(-150, 1110), (150, 1110), (150, 1196), (-150, 1196)], 205, 215)
        plate(ch, "torso", "team", [(-50, 900), (50, 900), (50, 1320), (-50, 1320)], 205, 215)
    # the tall flared collar behind the head, team inside
    ch.sweep("torso", "dark", [(arc(150, 150, 10, 16, 50, 150), 1370), (arc(150, 158, -30, 16, 56, 176), 1700)])
    ch.sweep("torso", "team", [(arc(133, 141, 10, 6, 46, 133), 1380), (arc(133, 141, -30, 6, 50, 159), 1694)], shell=False)
    # slicked black hair with a widow's peak, red eyes, a dark mouth
    helmet(ch, b, "dark", 1660, grow=8, top=14)
    plate(ch, "head", "dark", [(-40, 1660), (40, 1660), (0, 1612)], -138, -124)
    ch.sym(lambda sd, s: plate(ch, "head", "redeye", [(sd * 26, 1576), (sd * 62, 1576), (sd * 60, 1596), (sd * 28, 1592)], -136, -124))
    plate(ch, "head", "eye", [(-40, 1508), (40, 1508), (40, 1518), (-40, 1518)], -128, -114)
    return ch
