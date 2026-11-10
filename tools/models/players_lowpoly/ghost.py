"""GHOST: the ghost (box design: characters_outlined_extra.ghost)."""
from players_lowpoly.kit import Char, Rig, Segment, plate, ring, hexring, ngon, offset, ccw, tone_color, material

DESC = "the ghost: a white sheet from head to hem with team bedsheet stripes, black eye holes and an O mouth, tattered hem that swings as it walks"


def ragged(ch, bone, t, top, y_top, bot, ys, shell=True):
    """a closed solid (x, z rings) from a flat top ring at y_top down to a bottom ring whose corners each sit at their
    own height ys (a tattered hem); the bottom is a fan round its middle"""
    m = ch.mesh[bone]
    tone_color(t)
    ch.tones.add(t)
    n = len(top)
    if ccw(top) != list(top):
        top, bot, ys = top[::-1], bot[::-1], ys[::-1]

    def faces(tp, yt, bt, yb):
        cx = sum(p[0] for p in bt) / n
        cz = sum(p[1] for p in bt) / n
        cy = sum(yb) / n
        f = []
        for i in range(1, n - 1):                                  # the top cap (a convex ring: a fan)
            f.append(((tp[0][0], yt, tp[0][1]), (tp[i][0], yt, tp[i][1]), (tp[i + 1][0], yt, tp[i + 1][1])))
        for i in range(n):                                         # the ragged bottom
            j = (i + 1) % n
            f.append(((cx, cy, cz), (bt[j][0], yb[j], bt[j][1]), (bt[i][0], yb[i], bt[i][1])))
        for i in range(n):                                         # the sides
            j = (i + 1) % n
            a0, a1 = (bt[i][0], yb[i], bt[i][1]), (bt[j][0], yb[j], bt[j][1])
            b0, b1 = (tp[i][0], yt, tp[i][1]), (tp[j][0], yt, tp[j][1])
            f += [(a0, a1, b1), (a0, b1, b0)]
        return f

    # (x, z) counter-clockwise seen from +y is clockwise in x-y-z with y up: flip each face as lowpoly's "y" sweeps do
    for a, b, c in faces(top, y_top, bot, ys):
        m.add(material(t), a, c, b)
    if shell:
        o = ch.outline
        for a, b, c in faces(offset(top, o), y_top + o, offset(bot, o), [y - o for y in ys]):
            m.add("ink", a, b, c)


def build():
    ch = Char("GHOST", DESC, Rig(hip=850, hipx=130, knee=450, ankle=60))
    r = ch.rig

    # the sheet below the hips: a half on each leg, the upper half swinging with the thigh, the lower with the shin
    def leg(sd, s):
        x = sd * r.hipx
        th = Segment(ch, "thigh" + s, [(hexring(108, 130, x), r.hip + 40), (hexring(116, 136, x), r.knee - 20)])
        ch.sweep("thigh" + s, "white", th.rings)
        th.wrap(520, 720, "team", grow=6, shell=False)                                   # the sheet's stripe
        top, bot = hexring(114, 134, x), hexring(124, 144, x)
        ragged(ch, "shin" + s, "white", top, r.knee + 30, bot, [186, 64, 170, 60, 180, 70])   # the tattered hem
        sh = Segment(ch, "shin" + s, [(top, r.knee + 30), (bot, 150)])
        sh.wrap(210, 380, "team", grow=6, shell=False)                                   # the hem's stripe
        ch.limbs += [("thigh" + s, th), ("shin" + s, sh)]
        # a white toe peeping out under the hem
        ch.solid("foot" + s, "white", [(40, 10), (-150, 10), (-170, 40), (-110, 80), (40, 80)], "x", x - 50, x + 50)

    Char.sym(leg)
    # the hips and the body under the sheet, the bedsheet's stripes
    ch.sweep("pelvis", "white", [(ring(222, 140), 760), (ring(236, 146), 930)])
    body = Segment(ch, "torso", [(ring(236, 146), 900), (ring(250, 154, front=160), 1220), (ring(236, 144), 1350), (ring(176, 116, c=60), 1428)])
    ch.sweep("torso", "white", body.rings)
    body.wrap(950, 1080, "team", grow=6, shell=False)
    body.wrap(1160, 1290, "team", grow=6, shell=False)
    # the sheet drooping over the arms
    ch.sym(lambda sd, s: ch.loft("torso", "white", hexring(66, 86, sd * 252), 1418 - ch.outline, hexring(88, 112, sd * 300), 1180, "y"))
    ch.sym(lambda sd, s: ch.loft("torso", "team", hexring(90, 114, sd * 301), 1186, hexring(89, 113, sd * 300), 1240, "y", shell=False))
    # the head: a rounded sheet, two black eye holes and an O mouth
    ch.sweep("head", "white", [(ring(150, 140), 1410), (ring(160, 156, front=158, c=70), 1560), (ring(146, 142, c=62), 1700), (ring(78, 78, c=32), 1786)])
    ch.sym(lambda sd, s: plate(ch, "head", "eye", ngon(sd * 60, 1618, 40, 6, None, 0.75, 1.3), -168, -154))
    plate(ch, "head", "eye", ngon(0, 1520, 26, 6, None, 1.0, 1.1), -170, -154)
    ch.sweep("head", "team", [(offset(ring(144, 140, c=61), 6), 1680), (offset(ring(116, 114, c=50), 6), 1740)], shell=False)   # a stripe over the crown
    return ch
