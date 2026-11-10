"""FROG: the frog mascot (box design: characters_outlined_extra.frog)."""
from players_lowpoly.kit import Char, Rig, Segment, body, band, visor, plate, ring, hexring, ngon, BACK, FRONT_WRAP

DESC = "the frog mascot: lime green with a yellow belly, a wide flat head with two eye domes on top, a wide mouth, team shorts and neck scarf, webbed feet"


def build():
    ch = Char("FROG", DESC, Rig(hip=900, hipx=130, knee=460, ankle=50))
    b = body(ch, legs="team", shins="lime", feet="lime", pelvis="team", torso="lime", head=None, thigh_mid=False, calf=False,
             thigh=(82, 84, 70, 72), shin=(70, 72, 72, 70), foot=(70, 120, 80),
             pelvis_w=(190, 212), pelvis_d=(128, 136), waist=(206, 132), chest=(232, 146), shoulder=(205, 124), chest_y=1220)
    # webbed feet: three long toes fanned out with webbing between
    def web(sd, s):
        x = sd * 130
        fan = [(x - 74, -60), (x + 74, -60), (x + 100, -238), (x + 46, -196), (x, -262), (x - 46, -196), (x - 100, -238)]
        ch.solid("foot" + s, "lime", fan, "y", 10, 34)
    ch.sym(web)
    # the yellow belly, the team scarf and its end
    band(ch, b, "torso", "canary", 935, 1290, FRONT_WRAP, grow=6, depth=16)
    ch.sweep("torso", "team", [(ring(214, 134, front=142), 1330), (ring(176, 120), 1428)])
    band(ch, b, "torso", "team", 1020, 1340, BACK, grow=6, depth=16)                 # its tails down the back
    ch.sym(lambda sd, s: ch.loft("torso", "lime", hexring(82, 96, sd * 282), 1215, hexring(54, 66, sd * 266), 1408, "y"))   # shoulders
    # the wide flat head, a yellow chin, the wide mouth
    hs = Segment(ch, "head", [(ring(118, 112), 1420), (ring(170, 158, front=154, c=88), 1540), (ring(136, 118, c=54), 1652)])
    ch.sweep("head", "lime", hs.rings)
    b.seg["head"] = hs
    visor(ch, b, "canary", 1432, 1500, grow=4, depth=14, wrap=False)
    visor(ch, b, "bore", 1503, 1522, grow=5, depth=14, wrap=False)
    # the eye domes on top: white eyes, black pupils
    def dome(sd, s):
        x, z = sd * 86, -48
        ch.sweep("head", "lime", [(hexring(64, 64, x, z), 1600), (hexring(68, 68, x, z), 1704), (hexring(38, 38, x, z), 1772)])
        plate(ch, "head", "white", ngon(x, 1692, 48, 4, 0, 0.9, 1.0), -116, -100)
        plate(ch, "head", "eye", [(x - 14, 1668), (x + 14, 1668), (x + 14, 1716), (x - 14, 1716)], -120, -114)
    ch.sym(dome)
    b.wrap("head", 1590, 1640, "team", grow=6, shell=False)                            # a sweatband
    return ch
