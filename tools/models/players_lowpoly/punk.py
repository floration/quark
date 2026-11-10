"""PUNK: the punk (box design: characters_outlined_extra.punk)."""
from players_lowpoly.kit import Char, Rig, body, visor, plate, profile, hexring, ngon

DESC = "the punk: a tall team mohawk, dark shades, a black leather jacket with silver studs over a team shirt, ripped grey jeans, tall boots"


def strip(ch, seg, t, x0, x1, y0, y1, back=False, grow=5.0, depth=14.0, shell=False):
    """a flat strip laid on the front (or back) face of a ring-built piece from y0 to y1, following its slope through
    every ring between: a shirt front, a back patch"""
    ws = [y0] + [w for _, w in seg.rings if y0 < w < y1] + [y1]
    rings_ = []
    for w in ws:
        vs = [v for _, v in seg.at(w)]
        if back:
            z = max(vs)
            rings_.append(([(x0, z - depth), (x1, z - depth), (x1, z + grow), (x0, z + grow)], w))
        else:
            z = min(vs)
            rings_.append(([(x0, z - grow), (x1, z - grow), (x1, z + depth), (x0, z + depth)], w))
    ch.sweep(seg.bone, t, rings_, "y", shell)


def build():
    ch = Char("PUNK", DESC, Rig(hip=900, hipx=130, knee=470, ankle=160))
    # tartan team trousers, tall dark boots, a dark belt; a dark leather jacket
    b = body(ch, legs="team", shins="team", feet="dark", pelvis="dark", torso="dark", head="skin", thigh_mid=False, calf=False,
             thigh=(78, 80, 68, 70), shin=(68, 70, 70, 68), foot=(70, 190, 96),
             pelvis_w=(180, 200), pelvis_d=(118, 126), waist=(212, 132), chest=(232, 142), shoulder=(210, 124), chest_y=1250,
             head_w=154, head_d=154, head_top=1712, jaw=0.82, crown=0.72)
    # the tartan: a dark band round each thigh, a dark stripe down each thigh, rips showing skin
    b.wrap("thigh_", 690, 718, "dark", grow=4, shell=False)
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "dark", [(sd * 168 - 10, 470), (sd * 168 + 10, 470), (sd * 172 + 10, 900), (sd * 172 - 10, 900)], -74, -40))
    ch.sym(lambda sd, s: plate(ch, "thigh" + s, "skin", [(sd * 100 - 22, 660), (sd * 100 + 26, 666), (sd * 100 + 18, 708), (sd * 100 - 24, 700)], -82, -66))
    ch.sym(lambda sd, s: plate(ch, "shin" + s, "skin", [(sd * 150 - 20, 320), (sd * 150 + 22, 326), (sd * 150 + 18, 356), (sd * 150 - 18, 352)], -76, -60))
    ch.sym(lambda sd, s: b.wrap("shin" + s, 160, 236, "dark", grow=8, shell=False))              # the boots' tall cuffs
    plate(ch, "pelvis", "light", [(-40, 860), (40, 860), (40, 924), (-40, 924)], -142, -124)        # the buckle
    # the team shirt between the open jacket, the team patch on its back
    strip(ch, b.seg["torso"], "team", -108, 108, 935, 1400)
    strip(ch, b.seg["torso"], "team", -170, 170, 980, 1350, back=True)
    # the shoulders with silver spikes, the popped collar behind the neck
    def shoulder(sd, s):
        x = sd * 284
        ch.loft("torso", "dark", hexring(84, 98, x), 1215, hexring(56, 66, x - sd * 14), 1408, "y")
        for dz in (-40, 40):
            ch.loft("torso", "light", ngon(x - sd * 10, dz, 26, 4), 1392, ngon(x - sd * 10, dz, 3, 4), 1432, "y", shell=False)
    ch.sym(shoulder)
    profile(ch, "torso", "dark", [(30, 1340), (150, 1350), (136, 1476), (100, 1490), (50, 1420)], -110, 110)
    # dark shades, a smirk, the tall team mohawk
    visor(ch, b, "eye", 1572, 1614, grow=6, depth=14)
    plate(ch, "head", "skin_dk", [(-10, 1500), (44, 1506), (42, 1514), (-10, 1510)], -142, -130)
    profile(ch, "head", "team", [(-128, 1650), (-150, 1748), (-96, 1730), (-60, 1800), (-20, 1764), (30, 1808), (60, 1766),
                                 (110, 1790), (120, 1736), (166, 1730), (150, 1640)], -24, 24)
    return ch
