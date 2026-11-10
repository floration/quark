"""CAT: the cat mascot (box design: characters_outlined_extra.cat)."""
from players_lowpoly.kit import Char, Rig, body, plate, ring, ngon, hexring

DESC = "the cat mascot: a black cat head with pointed ears, a white muzzle and glowing eyes, a team jersey and shorts, a red collar, a curled tail"


def ear(ch, sd):
    """a pointed ear: a three-sided pyramid (its tip a tiny triangle), the pink inside pushed out of its front"""
    x = sd * 98
    base = [(x - sd * 60, -46), (x + sd * 56, -36), (x + sd * 10, 50)]
    tip = [(x + sd * 14 - 3, -6), (x + sd * 14 + 3, -6), (x + sd * 14, 0)]
    ch.sweep("head", "dark", [(base, 1690), (tip, 1818)])
    inner = [(x - sd * 30, -48), (x + sd * 30, -42), (x + sd * 4, -20)]
    itip = [(x + sd * 12 - 2, -14), (x + sd * 12 + 2, -14), (x + sd * 12, -11)]
    ch.sweep("head", "pink", [(inner, 1700), (itip, 1786)], shell=False)


def build():
    ch = Char("CAT", DESC, Rig(hip=910, hipx=130, knee=480, ankle=90))
    # black legs and paws, team shorts, a team jersey
    b = body(ch, legs="dark", shins="dark", feet="dark", pelvis="team", torso="team", head=None, thigh_mid=False, calf=False,
             thigh=(78, 80, 68, 70), shin=(68, 70, 70, 68), foot=(66, 175, 90),
             pelvis_w=(185, 205), pelvis_d=(124, 132), waist=(205, 128), chest=(228, 140), shoulder=(205, 120), chest_y=1240)
    b.wrap("thigh_", 690, 950, "team", grow=10)                                       # the shorts' legs
    # the white toes on each paw
    ch.sym(lambda sd, s: ch.loft("foot" + s, "white", [(sd * 130 - 70, -178), (sd * 130 + 70, -178), (sd * 130 + 72, -110), (sd * 130 - 72, -110)], 6,
                                 [(sd * 130 - 56, -150), (sd * 130 + 56, -150), (sd * 130 + 60, -100), (sd * 130 - 60, -100)], 80, "y", shell=False))
    # the curled tail: up from the seat, over and down to a white tip
    pts = [(0, 880, 120), (0, 905, 290), (0, 1050, 340), (0, 1160, 300)]
    for p0, p1 in zip(pts, pts[1:]):
        ch.rod("pelvis", "dark", p0, p1, 26, k=4)
    ch.rod("pelvis", "white", pts[-1], (0, 1196, 228), 28, k=4)
    # the white bib, black shoulders
    plate(ch, "torso", "white", [(-100, 1110), (100, 1110), (88, 1330), (0, 1370), (-88, 1330)], -154, -138)
    ch.sym(lambda sd, s: ch.loft("torso", "dark", hexring(80, 94, sd * 284), 1220, hexring(54, 64, sd * 268), 1408, "y"))
    # the red collar and its gold bell
    ch.sweep("torso", "glove", [(ring(150, 122), 1366), (ring(134, 114), 1430)], shell=False)
    plate(ch, "torso", "gold", ngon(0, 1338, 30, 4, 0), -152, -136)
    # the head: round cheeks, a flat crown, a white muzzle, glowing eyes, pointed ears
    ch.sweep("head", "dark", [(ring(104, 100), 1420), (ring(168, 150, front=156, c=80), 1556), (ring(128, 118, c=50), 1716)])
    ch.loft("head", "white", ngon(0, 1500, 76, 6, None, 1.0, 0.72), -140, ngon(0, 1496, 58, 6, None, 1.0, 0.68), -176, "z")
    plate(ch, "head", "pink", [(-20, 1534), (20, 1534), (0, 1512)], -180, -168)                 # the nose
    def eye(sd, _):
        x = sd * 64
        plate(ch, "head", "zeye", [(x - 34, 1580), (x + 34, 1580), (x + 30, 1612), (x + sd * 6, 1630), (x - 30, 1612)], -156, -146)
        plate(ch, "head", "eye", [(x - 6, 1584), (x + 6, 1584), (x + 6, 1622), (x - 6, 1622)], -159, -154)
    ch.sym(eye)
    # whiskers
    ch.sym(lambda sd, s: ch.rod("head", "white", (sd * 70, 1484, -170), (sd * 178, 1496, -64), 6, k=3, shell=False))
    ch.sym(lambda sd, s: ear(ch, sd))
    return ch
