"""SNOWMAN: the snowman (box design: characters_outlined_extra2.snowman)."""
from players_lowpoly.kit import Char, Rig, Segment, brim, plate, ring, hexring

DESC = "the snowman: three stacked snowballs, coal eyes, team buttons, a carrot nose, a team top hat, scarf and mittens on stick arms"


def build():
    ch = Char("SNOWMAN", DESC, Rig(hip=880, hipx=130, knee=460, ankle=50))
    r = ch.rig
    # (no kit body: the bottom snowball is split down the middle between the two thighs, as the box design's is, so it
    # parts a little as he walks; under it round snow feet in knitted team leg warmers, which match the hat, scarf and
    # mittens and put the team colour on the legs)
    def leg(sd, s):
        x = sd * r.hipx
        ch.solid("foot" + s, "white", [(90, 10), (-130, 10), (-165, 40), (-120, 96), (60, 96)], "x", x - 74, x + 74)
        sh = Segment(ch, "shin" + s, [(hexring(74, 74, x, 4), 40), (hexring(70, 72, x), r.knee + 20)])
        ch.sweep("shin" + s, "team", sh.rings)
        bx = sd * 118
        th = Segment(ch, "thigh" + s, [(hexring(122, 150, bx), 440), (hexring(155, 192, bx), 640), (hexring(136, 170, bx), 830)])
        ch.sweep("thigh" + s, "white", th.rings)
        ch.limbs += [("thigh" + s, th), ("shin" + s, sh)]
    Char.sym(leg)
    # the bottom ball's top over the hips
    ch.loft("pelvis", "white", ring(222, 172), 790, ring(146, 112), 940, "y")
    # the middle ball, its team buttons
    ch.sweep("torso", "white", [(ring(150, 116), 900), (ring(218, 162), 1110), (ring(160, 124), 1330)])
    for y, zf in ((1010, -139), (1100, -160), (1190, -150)):
        plate(ch, "torso", "team", [(-32, y - 32), (32, y - 32), (32, y + 32), (-32, y + 32)], zf - 4, zf + 12)
    # the team scarf, one tail down the front and one down the back
    ch.loft("torso", "team", ring(196, 152), 1250, ring(156, 124), 1428, "y")
    plate(ch, "torso", "team", [(-176, 940), (-56, 940), (-50, 1270), (-170, 1270)], -184, -166)
    plate(ch, "torso", "team", [(50, 980), (170, 980), (166, 1270), (46, 1270)], 166, 184)
    # the stick arms out to the team mittens
    ch.sym(lambda sd, s: ch.rod("torso", "leather", (sd * 180, 1200, 0), (sd * 318, 1238, 0), 18, k=4))
    ch.sym(lambda sd, s: ch.loft("torso", "team", hexring(54, 50, sd * 344), 1160, hexring(44, 42, sd * 344), 1310, "y"))
    # the head ball: coal eyes, a coal smile, the carrot nose
    ch.sweep("head", "white", [(ring(122, 122), 1432), (ring(160, 160), 1560), (ring(120, 120), 1680)])
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 36, 1586), (sd * 72, 1586), (sd * 72, 1618), (sd * 36, 1618)], -156, -144))
    plate(ch, "head", "eye", [(-64, 1510), (-34, 1484), (34, 1484), (64, 1510), (34, 1498), (-34, 1498)], -150, -136)   # the coal smile
    ch.loft("head", "carrot", [(-24, 1522), (24, 1522), (24, 1566), (-24, 1566)], -146, [(-5, 1540), (5, 1540), (5, 1550), (-5, 1550)], -184, "z")
    # the team earmuffs
    ch.sym(lambda sd, s: ch.solid("head", "team", [(z, y) for z, y in ((-50, 1520), (0, 1495), (50, 1520), (50, 1590), (0, 1615), (-50, 1590))], "x", sd * 156 - 18, sd * 156 + 18, shell=False))
    # the team top hat: the brim, the tall crown, its dark band
    brim(ch, "team", 1660, 118, 178, thick=14, k=8)
    ch.loft("head", "team", ring(112, 112), 1672, ring(108, 108), 1800, "y")
    ch.loft("head", "team_dk", ring(118, 118), 1676, ring(116, 116), 1714, "y", shell=False)
    return ch
