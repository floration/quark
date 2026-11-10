"""CROSSBOW: the bolt the crossbow fires (its loaded bolt, gen_weapons_lowpoly.crossbow, at projectile scale).

A light hexagonal shaft, a dark ferrule and a four-bladed team broadhead with a steel point at the front (-Z: the game
also draws the bolt stuck point first in walls, so the head is the widest, brightest part up front), three swept team
vanes and a dark notched nock at the back. About 770 mm long (z -420..+350) like the old box bolt.
"""
import math

from gen_weapons_lowpoly import Model
from lowpoly import solid, loft, ngon, rotz

NAME = "CROSSBOW"


def star(r_out, r_in, z_sx=1.0):
    """a four-pointed cross section: the broadhead's blades, points up/down/left/right"""
    pts = []
    for i in range(8):
        a = math.pi / 2 + i * math.pi / 4
        r = r_out if i % 2 == 0 else r_in
        pts.append((r * math.cos(a), r * math.sin(a)))
    return pts


def build():
    m = Model(NAME, "crossbow bolt: a light hexagonal shaft, a dark ferrule, a four-bladed team broadhead with a steel "
                    "point, three swept team vanes and a dark notched nock")
    B = m.body
    # shaft
    solid(B, "base_light", ngon(0, 0, 16, 6), "z", -318, 326)
    # ferrule: a dark collar the head sits on
    loft(B, "metal_dark", ngon(0, 0, 18, 6), -300, ngon(0, 0, 22, 6), -326)
    # broadhead: four team blades sweeping back from the point, the last of it steel
    loft(B, "team_wood", star(44, 15), -334, star(13, 5), -392)
    loft(B, "metal_steel", star(13, 5), -392, star(1.2, 0.6), -420)
    # three swept vanes (one straight up, as on the loaded bolt)
    for k in range(3):
        B.place(rotz(k * 2 * math.pi / 3), (0, 0, 0))
        solid(B, "team_wood", [(186, 14), (318, 14), (326, 60), (290, 62)], "x", -3, 3)
        B.unplace()
    # nock: a dark cap with the string notch
    solid(B, "metal_dark", [(326, -20), (350, -20), (350, -9), (336, -9), (336, 9), (350, 9), (350, 20), (326, 20)], "x", -16, 16)
    return m
