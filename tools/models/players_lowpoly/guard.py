"""GUARD: the royal guard (box design: characters_outlined_extra2.guard)."""
from players_lowpoly.kit import tone, Char, Rig, Segment, body, plate, ring, hexring, ngon, rotx, rotz

DESC = "the royal guard: a towering black bearskin hat, a team tunic with gold buttons, white cross-belts, dark trousers with a team stripe, shiny boots"

# a black that stays at the visibility floor for big areas (sRGB luminance 0.35; the box set's hairblk is 0.22)
HAIR = tone("bearskin", (0.37, 0.35, 0.37))


def build():
    ch = Char("GUARD", DESC, Rig(hip=900, hipx=130, knee=450, ankle=120))
    b = body(ch, legs="dark", feet="hairblk", pelvis="team", torso="team", head=None,
             thigh_mid=False, calf=False, foot=(66, 180, 86), thigh=(80, 80, 68, 72), shin=(66, 70, 70, 68),
             waist=(196, 122), chest=(222, 138), shoulder=(200, 116), pelvis_w=(184, 208), pelvis_d=(122, 132))

    # the trousers' team stripe down the outside of each leg; the boots' shine
    def leg(sd, s):
        x = sd * 130
        def stripe(bone, y0, w0, y1, w1, hz):
            q = lambda w: [(x + sd * (w - 10), -hz), (x + sd * (w + 5), -hz), (x + sd * (w + 5), hz), (x + sd * (w - 10), hz)]
            ch.loft(bone, "team", q(w0), y0, q(w1), y1, "y", shell=False)
        stripe("thigh" + s, 470, 69, 900, 79, 26)
        stripe("shin" + s, 120, 68, 460, 66, 24)
        plate(ch, "foot" + s, "white", [(x - 40, 90), (x + 40, 90), (x + 34, 118), (x - 34, 118)], -150, -100)
    ch.sym(leg)
    # the white belt and its gold buckle, the gold buttons
    b.wrap("torso", 950, 1000, "white", grow=8, shell=False)
    plate(ch, "torso", "gold", [(-30, 958), (30, 958), (30, 992), (-30, 992)], -142, -126)
    for y in (1080, 1190):
        plate(ch, "torso", "gold", [(-16, y), (16, y), (16, y + 30), (-16, y + 30)], -158, -132)
    # the white cross-belt from the left hip over the right shoulder, front and back (two pieces, on the chest's slopes)
    with ch.placed("torso", rotx(-0.096), (0, 1250, -150)):
        plate(ch, "torso", "white", [(-196, 1010), (-150, 1010), (56, 1250), (6, 1250)], -162, -146)
    with ch.placed("torso", rotx(0.21), (0, 1250, -150)):
        plate(ch, "torso", "white", [(6, 1250), (56, 1250), (190, 1408), (140, 1408)], -162, -146)
    with ch.placed("torso", rotx(0.048), (0, 1250, 138)):
        plate(ch, "torso", "white", [(-196, 1010), (-150, 1010), (56, 1250), (6, 1250)], 130, 146)
    with ch.placed("torso", rotx(-0.138), (0, 1250, 138)):
        plate(ch, "torso", "white", [(6, 1250), (56, 1250), (190, 1408), (140, 1408)], 130, 146)
    # the team shoulders with gold epaulettes, the dark collar
    def shoulder(sd, _):
        cx = sd * 282
        with ch.placed("torso", rotz(-sd * 0.18), (cx, 1236, 0)):
            ch.loft("torso", "team", hexring(80, 94, cx), 1236, hexring(58, 70, cx - sd * 14), 1396, "y")
            ch.loft("torso", "gold", hexring(64, 78, cx - sd * 10), 1376, hexring(50, 60, cx - sd * 16), 1412, "y", shell=False)
    ch.sym(shoulder)
    ch.loft("torso", "team_dk", hexring(112, 96), 1384, hexring(90, 78), 1440, "y", shell=False)
    # the face under the hat, its gold chin strap
    hs = Segment(ch, "head", [(ring(66, 64, c=26), 1430), (ring(96, 98, c=40), 1470), (ring(98, 100, c=40), 1600)])
    ch.sweep("head", "skin", hs.rings)
    ch.sym(lambda sd, s: plate(ch, "head", "eye", [(sd * 26, 1532), (sd * 54, 1532), (sd * 54, 1552), (sd * 26, 1552)], -112, -96))
    hs.wrap(1462, 1480, "gold", grow=5, shell=False)
    # the towering bearskin, bulging, fur-faceted (HAIR, not the box's hairblk: a big area)
    K = 8
    ch.sweep("head", HAIR, [(ngon(0, 4, 138, K), 1566), (ngon(0, 2, 168, K), 1628), (ngon(0, -2, 170, K), 1736), (ngon(0, -6, 118, K), 1796)])
    return ch
