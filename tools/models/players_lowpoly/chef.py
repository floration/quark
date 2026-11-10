"""CHEF: the chef (box design: characters_outlined_extra.chef)."""
from players_lowpoly.kit import Char, Rig, body, band, plate, ngon, FRONT_WRAP

DESC = "the chef: a tall white toque, a white double-breasted jacket, a team apron and neckerchief, checked trousers and a curly moustache"


def build():
    ch = Char("CHEF", DESC, Rig(hip=910, hipx=130, knee=460, ankle=90))
    # the team apron wraps the hips and hangs over the thighs (team thighs and pelvis: the box's apron is a front
    # panel only, which left the legs and the back grey); the checked trousers show below it
    b = body(ch, legs="team", shins="mid_dk", feet="dark", pelvis="team", torso="white", head="skin",
             thigh=(92, 90, 80, 80), shin=(72, 74, 76, 68), foot=(64, 175, 88), thigh_mid=False, calf=False,
             pelvis_w=(175, 212), waist=(200, 126), chest=(226, 140), shoulder=(200, 118), chest_y=1240,
             head_w=148, head_d=146, head_top=1680, jaw=0.82, crown=0.9)
    # the trousers' light checks
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "light", [(sd * 130 - 46, 250), (sd * 130 + 6, 250), (sd * 130 + 6, 302), (sd * 130 - 46, 302)], -74, -56))
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "light", [(sd * 130 + 4, 330), (sd * 130 + 50, 330), (sd * 130 + 50, 382), (sd * 130 + 4, 382)], -74, -56))
    # the apron's bib up the jacket's front, its strings round the waist
    band(ch, b, "torso", "team", 935, 1185, FRONT_WRAP, grow=8, depth=10, shell=True)
    b.wrap("torso", 1050, 1095, "team", grow=6, shell=False)
    # the double-breasted jacket's dark buttons
    for x in (-62, 62):
        for y in (1205, 1290):
            plate(ch, "torso", "dark", [(x - 15, y), (x + 15, y), (x + 15, y + 30), (x - 15, y + 30)], -168, -150)
    # the team neckerchief round the collar, its knot
    b.wrap("torso", 1345, 1408, "team", grow=10, shell=False)
    plate(ch, "torso", "team", [(-36, 1290), (36, 1290), (26, 1360), (-26, 1360)], -152, -128)

    # the white sleeves' tops with team cuffs
    def sleeve(sd, s):
        x = sd * 282
        ch.sweep("torso", "white", [(ngon(x, 0, 100, 6), 1190), (ngon(x - sd * 26, 0, 72, 6), 1405)])
        ch.sweep("torso", "team", [(ngon(x, 0, 108, 6), 1150), (ngon(x, 0, 106, 6), 1220)], shell=False)
    ch.sym(sleeve)
    # the face: eyes and the curly moustache
    plate(ch, "head", "eye", [(-58, 1588), (58, 1588), (58, 1606), (-58, 1606)], -152, -140)
    plate(ch, "head", "hair", [(-84, 1524), (-50, 1506), (50, 1506), (84, 1524), (102, 1554), (66, 1538), (-66, 1538), (-102, 1554)], -160, -138)
    # the tall white toque, puffed at the top, light pleats
    ch.sweep("head", "white", [(ngon(0, 0, 156, 8), 1636), (ngon(0, 0, 184, 8), 1782), (ngon(0, 0, 138, 8), 1822)])
    for x in (-50, 50):
        ch.loft("head", "light", [(x - 13, -158), (x + 13, -158), (x + 13, -140), (x - 13, -140)], 1672,
                [(x - 13, -176), (x + 13, -176), (x + 13, -158), (x - 13, -158)], 1775, "y", shell=False)
    return ch
