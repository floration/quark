#!/usr/bin/env python3
"""The shipped weapons and grapple launcher as low-poly outlined models (owner, 2026-11-06: the low-poly AK, then
"remake all weapons and grapple with this style").

Mesh overrides built from lowpoly.py: real angled and curved shapes, flat-shaded, an inverted-hull outline on every
solid, colours that follow the map and team like the box models. Each weapon keeps its moving parts as anim_<part>
nodes (docs/ASSETS.md "Animated parts") and its muzzle where kMuzzle (game.cpp) puts it. The grapple launcher
(PROP_HOOK_ARM) keeps its barrel's mouth at kHookMuzzle and the claw (PROP_HOOK) its rope tie at the origin.

  python3 tools/models/gen_weapons_lowpoly.py [--out DIR] [--only NAME,...]

Writes <out>/models/weapon/<NAME>.gltf and <out>/models/prop/{HOOK_ARM,HOOK}.gltf (default out: assets/overrides).
Deterministic.
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from lowpoly import Mesh, solid, loft, rod, ngon, rrect, offset, ccw, rotx, roty, rotz, write_gltf  # noqa: E402

MUZZLE = {   # kMuzzle (game.cpp), mm
    "RIFLE": (0, 8, -290), "ROCKET": (0, 0, -250), "KNIFE": (0, 0, -210), "SNIPER": (0, 6, -448), "LASER": (0, 0, -193),
    "SHOTGUN": (0, 12, -300), "STICKY": (0, 0, -212), "CROSSBOW": (0, 32, -230), "BUBBLE": (0, 4, -257),
    "SYRINGE": (0, 0, -200), "LOCATOR": (0, 0, -125),
}


class Model:
    def __init__(self, name, desc):
        self.name, self.desc = name, desc
        self.body = Mesh()
        self.parts = []   # (part, Mesh, pivot)

    def part(self, name, pivot):
        m = Mesh()
        self.parts.append((name, m, pivot))
        return m

    def nodes(self):
        return [(self.name, self.body, None)] + [("anim_" + n, m, p) for n, m, p in self.parts]


def grip(B, mat, z0, y0, rake=30, length=70, w=12):
    """a raked pistol grip from (z0, y0) down and back"""
    solid(B, mat, [(z0, y0), (z0 + 28, y0), (z0 + 28 + rake, y0 - length + 6), (z0 + 26 + rake, y0 - length),
                   (z0 + 4 + rake, y0 - length), (z0 + rake - 4, y0 - length + 12), (z0 + 4, y0 - 14)], "x", -w, w)


def trigger(B, z0, y0):
    """a trigger guard (z0: its front, y0: the receiver's bottom) and the trigger"""
    solid(B, "metal_mid", [(z0, y0), (z0 + 5, y0), (z0 + 10, y0 - 14), (z0 + 42, y0 - 14), (z0 + 42, y0 - 18), (z0 + 7, y0 - 18)], "x", -3.5, 3.5)
    solid(B, "metal_mid", [(z0 + 38, y0), (z0 + 42, y0), (z0 + 42, y0 - 16), (z0 + 38, y0 - 16)], "x", -3.5, 3.5)
    solid(B, "base_light", [(z0 + 18, y0), (z0 + 24, y0), (z0 + 26, y0 - 6), (z0 + 23, y0 - 12), (z0 + 20, y0 - 11), (z0 + 21, y0 - 5)], "x", -2, 2)


# ---- RIFLE: the AK ------------------------------------------------------------------------------------------------

def rifle():
    m = Model("RIFLE", "low-poly AK: receiver with an arched dust cover, team wood handguard, grip and dropping stock, "
                       "gas tube, tall front sight, slant brake, long curved magazine; the charging handle kicks back")
    B = m.body
    D = 36
    def S(poly):
        return [(z + D, y) for z, y in poly]
    solid(B, "metal_dark", S([(-100, 16), (72, 16), (76, 10), (76, -18), (58, -22), (-26, -22), (-34, -28),
                               (-74, -28), (-80, -22), (-100, -22)]), "x", -18, 18)
    solid(B, "metal_dark", S([(-130, 14), (-100, 14), (-100, -20), (-130, -16)]), "x", -16, 16)   # front trunnion
    arch = [(-16, 15), (16, 15), (16, 22), (13, 27), (7, 30), (-7, 30), (-13, 27), (-16, 22)]
    solid(B, "metal_mid", arch, "z", -92 + D, 66 + D)
    solid(B, "metal_mid", ngon(0, 23, 5, 8), "z", 66 + D, 74 + D)
    solid(B, "metal_dark", S([(-124, 14), (-100, 14), (-100, 31), (-110, 31), (-124, 22)]), "x", -12, 12)
    solid(B, "metal_mid", S([(-120, 31), (-104, 31), (-104, 35), (-118, 34)]), "x", -7, 7)
    solid(B, "metal_mid", S([(-74, 9), (6, 9), (10, 4), (6, 1), (-74, 5)]), "x", 18, 20.5)
    solid(B, "metal_bore", S([(-64, 2), (-10, 2), (-10, 13), (-64, 13)]), "x", 17.6, 18.6, shell=False)
    solid(B, "glow", S([(-72, 3), (-36, 3), (-36, 11), (-72, 11)]), "x", -18.8, -17.6, shell=False)
    low = [(-17, 10), (17, 10), (19, -4), (15, -16), (7, -22), (-7, -22), (-15, -16), (-19, -4)]
    solid(B, "team_wood", low, "z", -212, -100)
    solid(B, "metal_dark", offset(ccw(low), 1.5), "z", -102, -94)
    solid(B, "metal_dark", offset(ccw(low), 1.0), "z", -218, -212)
    up = [(-12, 11), (12, 11), (13, 22), (9, 29), (-9, 29), (-13, 22)]
    solid(B, "team_wood", up, "z", -200, -94)
    solid(B, "metal_mid", ngon(0, 8, 7, 8), "z", -276, -94)
    solid(B, "metal_mid", ngon(0, 24, 6, 8), "z", -230, -198)
    solid(B, "metal_dark", [(-240, -1), (-226, -1), (-226, 31), (-232, 33), (-240, 30)], "x", -10, 10)
    solid(B, "metal_dark", [(-272, -2), (-250, -2), (-250, 15), (-255, 20), (-256, 40), (-262, 40), (-264, 20), (-272, 15)], "x", -8, 8)
    solid(B, "metal_dark", [(-266, 18), (-254, 18), (-254, 38), (-266, 38)], "x", 6, 9.5)
    solid(B, "metal_dark", [(-266, 18), (-254, 18), (-254, 38), (-266, 38)], "x", -9.5, -6)
    solid(B, "metal_dark", [(-290, -1), (-272, -1), (-272, 18), (-280, 18), (-290, 11)], "x", -9, 9)
    solid(B, "metal_bore", ngon(0, 8, 4.5, 8), "z", -290.6, -289.9, shell=False)
    def mag(t):
        e = t ** 1.5
        return (-72 + D - 50 * e, -22 - 118 * t), (-30 + D - 58 * e, -22 - 124 * t)
    front = [mag(i / 8)[0] for i in range(9)]
    back = [mag(i / 8)[1] for i in range(9)]
    solid(B, "metal_mid", front + back[::-1], "x", -12, 12)
    ribF = [(f[0] + 0.32 * (b[0] - f[0]), f[1]) for f, b in zip(front, back)][1:-1]
    ribB = [(f[0] + 0.62 * (b[0] - f[0]), b[1] + 0.3 * (f[1] - b[1])) for f, b in zip(front, back)][1:-1]
    solid(B, "metal_dark", ribF + ribB[::-1], "x", -13.5, 13.5)
    fb, bb = front[-1], back[-1]
    solid(B, "team_dark", [(fb[0] - 3, fb[1] + 1), (fb[0] - 1, fb[1] - 7), (bb[0] + 3, bb[1] - 7), (bb[0] + 3, bb[1] + 1)], "x", -14, 14)
    solid(B, "metal_mid", S([(-32, -22), (-27, -22), (-22, -36), (14, -36), (14, -40), (-25, -40)]), "x", -3.5, 3.5)
    solid(B, "metal_mid", S([(10, -22), (14, -22), (14, -38), (10, -38)]), "x", -3.5, 3.5)
    solid(B, "base_light", S([(-8, -22), (-2, -22), (0, -28), (-3, -34), (-6, -33), (-5, -27)]), "x", -2, 2)
    solid(B, "team_wood", S([(12, -22), (40, -22), (70, -84), (70, -92), (44, -94), (36, -82), (16, -36)]), "x", -12, 12)
    solid(B, "team_wood", [(72 + D, 14), (232, 0), (232, -62), (214, -62), (170, -42), (80 + D, -22), (72 + D, -16)], "x", -14, 14)
    solid(B, "metal_dark", [(232, 2), (242, 2), (242, -64), (232, -64)], "x", -15, 15)
    P = m.part("bolt", (20, 9, -24 + D))   # the charging handle: back 2.5 cm and home, each shot
    solid(P, "metal_mid", S([(-44, 4), (-2, 4), (-2, 13), (-44, 13)]), "x", 18, 20.5)
    solid(P, "metal_mid", [(20, 4.5), (34, 4.5), (37, 7), (37, 11), (34, 13.5), (20, 13.5)], "z", -40 + D, -32 + D)
    return m


# ---- ROCKET ---------------------------------------------------------------------------------------------------------

def rocket():
    m = Model("ROCKET", "launcher: octagonal tube with two team bands, flared muzzle bell and exhaust cone, glow charge "
                        "window, side scope with a glow lens, foregrip, grip, shoulder pad; the warhead reloads")
    B = m.body
    solid(B, "metal_mid", ngon(0, 0, 46, 8), "z", -212, 180)
    for z0, z1 in ((-152, -126), (88, 112)):
        solid(B, "team_wood", ngon(0, 0, 49, 8), "z", z0, z1)
    solid(B, "metal_dark", ngon(0, 0, 50, 8), "z", -30, -18)   # mid collar
    loft(B, "metal_dark", ngon(0, 0, 47, 8), -212, ngon(0, 0, 58, 8), -244)   # muzzle bell
    solid(B, "metal_dark", ngon(0, 0, 58, 8), "z", -250, -244)
    solid(B, "metal_bore", ngon(0, 0, 40, 8), "z", -250.7, -250, shell=False)
    loft(B, "metal_dark", ngon(0, 0, 47, 8), 180, ngon(0, 0, 56, 8), 212)       # exhaust cone
    solid(B, "metal_bore", ngon(0, 0, 40, 8), "z", 212, 212.7, shell=False)
    solid(B, "glow", [(-80, -10), (20, -10), (20, 10), (-80, 10)], "x", -47.5, -46, shell=False)
    # top rail and side scope (left), lens glowing
    solid(B, "metal_dark", rrect(-100, 44, 70, 52, 3), "x", -10, 10)
    solid(B, "metal_dark", [(-66, 34), (-52, 34), (-52, 48), (-66, 48)], "x", -44, -36)   # scope arm
    solid(B, "metal_dark", ngon(-60, 30, 15, 8), "z", -112, -24)                         # scope tube
    loft(B, "metal_dark", ngon(-60, 30, 15, 8), -112, ngon(-60, 30, 19, 8), -126)
    solid(B, "glow", ngon(-60, 30, 15, 8), "z", -127, -126.2, shell=False)
    solid(B, "team_wood", ngon(-60, 30, 16.5, 8), "z", -60, -52)
    # foregrip, grip, trigger, shoulder pad
    solid(B, "metal_dark", [(-122, -44), (-92, -44), (-96, -98), (-118, -98)], "x", -12, 12)
    solid(B, "metal_mid", [(-128, -40), (-86, -40), (-86, -48), (-128, -48)], "x", -14, 14)
    grip(B, "metal_dark", 26, -44, rake=26, length=58, w=13)
    trigger(B, -14, -44)
    solid(B, "metal_dark", [(100, -40), (176, -40), (176, -64), (112, -60)], "x", -28, 28)
    P = m.part("round", (0, 0, -236))   # the warhead: gone on the shot, slides back up the tube
    loft(P, "team_wood", ngon(0, 0, 26, 8), -232, ngon(0, 0, 10, 8), -252)
    solid(P, "team_wood", ngon(0, 0, 26, 8), "z", -232, -222)
    return m


# ---- KNIFE ----------------------------------------------------------------------------------------------------------

def knife():
    m = Model("KNIFE", "combat knife: clip-point light blade with a dark spine and a glow fuller, dark guard, ringed grip, "
                       "team pommel")
    B = m.body
    blade = [(-20, 8), (-150, 8), (-186, 2), (-212, -6), (-196, -16), (-150, -24), (-20, -24)]
    solid(B, "base_light", blade, "x", -2.6, 2.6)
    solid(B, "metal_dark", [(-20, 4), (-148, 4), (-148, 9), (-20, 9)], "x", -3.4, 3.4)
    for sx in (1, -1):
        lo, hi = (2.7, 3.4) if sx > 0 else (-3.4, -2.7)
        solid(B, "glow", [(-40, -6), (-140, -6), (-140, -2), (-40, -2)], "x", lo, hi, shell=False)
    for k in range(4):
        z = -100 + k * 12
        solid(B, "metal_dark", [(z, 9), (z - 6, 9), (z - 3, 14)], "x", -3, 3)
    solid(B, "metal_dark", [(-22, 16), (-10, 16), (-10, -34), (-22, -34), (-24, -26), (-24, 8)], "x", -16, 16)
    solid(B, "metal_dark", ngon(0, -8, 14, 8, sx=0.85, sy=1.15), "z", -10, 92)
    for z in (12, 42, 72):
        solid(B, "metal_mid", ngon(0, -8, 15, 8, sx=0.85, sy=1.15), "z", z, z + 7)
    loft(B, "team_wood", ngon(0, -8, 15, 8, sx=0.85, sy=1.15), 92, ngon(0, -8, 11, 8), 106)
    solid(B, "metal_mid", ngon(0, -8, 5, 6), "z", 106, 114)
    return m


# ---- SNIPER: .50-style --------------------------------------------------------------------------------------------

def sniper():
    m = Model("SNIPER", ".50-style rifle: long receiver, heavy fluted barrel, big ported brake, team handguard, scope with "
                        "bells, turrets and rings, magazine, grip, skeleton stock with team cheek rest; the bolt cycles")
    B = m.body
    solid(B, "metal_dark", [(-112, 26), (128, 26), (134, 18), (134, -14), (40, -20), (-20, -20), (-30, -28), (-90, -28), (-100, -20), (-112, -20)], "x", -20, 20)
    solid(B, "metal_mid", rrect(-108, 26, 122, 36, 4), "x", -15, 15)
    solid(B, "metal_dark", rrect(-110, 36, 110, 41, 1.5), "x", -9, 9)
    solid(B, "metal_bore", [(30, 2), (80, 2), (80, 20), (30, 20)], "x", 19.6, 20.6, shell=False)
    solid(B, "metal_mid", ngon(0, 6, 9, 8), "z", -408, -112)
    for a in range(4):   # flutes
        ang = math.pi / 4 + a * math.pi / 2
        solid(B, "metal_dark", ngon(9 * math.cos(ang), 6 + 9 * math.sin(ang), 2.5, 4), "z", -392, -290)
    solid(B, "metal_dark", rrect(-448, -10, -408, 22, 4), "x", -17, 17)                 # brake
    for z in (-441, -429, -417):
        solid(B, "metal_bore", [(z, -2), (z + 7, -2), (z + 7, 14), (z, 14)], "x", -17.8, 17.8, shell=False)
    solid(B, "metal_bore", ngon(0, 6, 6, 8), "z", -448.7, -448, shell=False)
    hg = [(-18, 26), (18, 26), (20, 4), (16, -14), (-16, -14), (-20, 4)]
    solid(B, "team_wood", hg, "z", -286, -112)
    for z in (-262, -230, -198, -166):
        solid(B, "metal_dark", [(z, 4), (z + 18, 4), (z + 18, 16), (z, 16)], "x", 19.7, 20.8, shell=False)
        solid(B, "metal_dark", [(z, 4), (z + 18, 4), (z + 18, 16), (z, 16)], "x", -20.8, -19.7, shell=False)
    # scope: tube, bells, lenses, turrets, rings, team band
    sy = 60
    solid(B, "metal_dark", ngon(0, sy, 14, 8), "z", -110, 84)
    loft(B, "metal_dark", ngon(0, sy, 14, 8), -110, ngon(0, sy, 21, 8), -150)
    solid(B, "metal_dark", ngon(0, sy, 21, 8), "z", -158, -150)
    solid(B, "glow", ngon(0, sy, 16, 8), "z", -158.8, -158, shell=False)
    loft(B, "metal_dark", ngon(0, sy, 14, 8), 84, ngon(0, sy, 18, 8), 104)
    solid(B, "metal_dark", ngon(0, sy, 18, 8), "z", 104, 116)
    solid(B, "team_wood", ngon(0, sy, 22, 8), "z", -146, -138)
    solid(B, "team_wood", ngon(0, sy, 19, 8), "z", 106, 112)
    solid(B, "metal_mid", ngon(0, -22, 8, 8), "y", sy + 12, sy + 26)                   # elevation turret (x, z)
    solid(B, "metal_mid", ngon(-22, sy, 8, 8), "x", 12, 28) if False else None
    solid(B, "metal_mid", ngon(-22, sy, 8, 8), "x", 13, 26)                            # windage (z, y)
    for z in (-80, 46):
        solid(B, "metal_mid", [(z, 36), (z + 18, 36), (z + 18, sy - 10), (z, sy - 10)], "x", -8, 8)
        solid(B, "metal_mid", ngon(0, sy, 16, 8), "z", z + 4, z + 14)
    # magazine, trigger, grip, stock
    solid(B, "metal_mid", [(-86, -28), (-34, -28), (-30, -70), (-82, -70)], "x", -14, 14)
    solid(B, "team_dark", [(-84, -70), (-28, -70), (-28, -76), (-84, -76)], "x", -15, 15)
    trigger(B, 34, -20)
    grip(B, "metal_dark", 84, -20, rake=30, length=72, w=13)
    solid(B, "base_light", [(134, 18), (250, 12), (252, -64), (232, -64), (196, -36), (160, -36), (148, -20), (134, -14)], "x", -16, 16)
    solid(B, "team_wood", rrect(146, 10, 222, 30, 4), "x", -14, 14)
    solid(B, "metal_dark", [(250, 14), (262, 14), (262, -68), (250, -66)], "x", -18, 18)
    P = m.part("bolt", (-20, 12, 60))   # the handle on the left wall: up 70 degrees, back, forward, down
    solid(P, "metal_mid", ngon(12, 60, 4, 6), "x", -42, -20)
    solid(P, "base_light", ngon(12, 60, 8, 8), "x", -56, -42)
    return m


# ---- LASER ----------------------------------------------------------------------------------------------------------

def laser():
    m = Model("LASER", "beam rifle: a wedge-nosed body with side fins and a glow strip, glowing coil rings on a spine, a "
                       "tapered emitter whose three-blade rotor spins while you fire, grip, power cell with a team cap")
    B = m.body
    solid(B, "metal_mid", [(-150, 30), (-120, 42), (148, 42), (156, 34), (156, -30), (100, -38), (-120, -38), (-150, -22)], "x", -34, 34)
    solid(B, "metal_dark", [(-112, 42), (138, 42), (130, 50), (-104, 50)], "x", -24, 24)
    for sx in (1, -1):
        lo, hi = (34, 46) if sx > 0 else (-46, -34)
        solid(B, "metal_dark", [(-100, 22), (40, 22), (64, 0), (40, -24), (-100, -24), (-112, 0)], "x", lo, hi)
    solid(B, "glow", [(-90, -5), (30, -5), (30, 5), (-90, 5)], "x", 46, 47.3, shell=False)
    solid(B, "glow", [(-90, -5), (30, -5), (30, 5), (-90, 5)], "x", -47.3, -46, shell=False)
    for k in range(4):   # coil rings on the spine
        z = -92 + k * 52
        solid(B, "glow" if k % 2 == 0 else "metal_dark", ngon(0, 62, 14, 8), "z", z, z + 16)
    solid(B, "metal_dark", ngon(0, 62, 6, 8), "z", -104, 92)
    solid(B, "metal_dark", [(-100, 50), (-90, 50), (-90, 58), (-100, 58)], "x", -5, 5)
    solid(B, "team_wood", [(90, 50), (110, 50), (106, 64), (94, 64)], "x", -6, 6)
    loft(B, "metal_dark", ngon(0, 0, 32, 8), -150, ngon(0, 0, 18, 8), -178)           # emitter taper
    solid(B, "metal_dark", ngon(0, 0, 8, 8), "z", -194, -178)
    solid(B, "glow_hot", ngon(0, 0, 5, 8), "z", -194.7, -194, shell=False)
    grip(B, "metal_dark", 16, -38, rake=28, length=62, w=13)
    trigger(B, -24, -38)
    solid(B, "metal_dark", rrect(96, -62, 150, -16, 5), "x", -22, 22)
    solid(B, "team_wood", rrect(150, -58, 157, -20, 2), "x", -18, 18)
    P = m.part("spin", (0, 0, -185))   # the rotor: spins about the bore while you fire
    for k in range(3):
        a = math.pi / 2 + k * 2 * math.pi / 3
        P.place(rotz(a), (0, 0, 0))
        solid(P, "glow", [(6, -5), (26, -3), (26, 3), (6, 5)], "z", -190, -181)
        P.unplace()
    solid(P, "team_wood", ngon(0, 0, 9, 6), "z", -191, -180)
    return m


# ---- SHOTGUN --------------------------------------------------------------------------------------------------------

def shotgun():
    m = Model("SHOTGUN", "pump shotgun: barrel over a magazine tube, bead and muzzle band, a ribbed team pump that racks, "
                         "receiver with team shells in a side carrier, a sporting stock with a pistol grip and team pad")
    B = m.body
    solid(B, "metal_mid", ngon(0, 12, 12, 8), "z", -300, -36)
    solid(B, "metal_dark", rrect(-300, 22, -40, 26, 1), "x", -3, 3)
    solid(B, "base_light", ngon(0, 30, 3, 6), "z", -296, -290)
    solid(B, "metal_dark", ngon(0, 12, 14, 8), "z", -300, -288)
    solid(B, "metal_bore", ngon(0, 12, 8, 8), "z", -300.7, -300, shell=False)
    solid(B, "metal_dark", ngon(0, -12, 10, 8), "z", -284, -36)
    solid(B, "metal_dark", [(-286, 18), (-272, 18), (-272, -22), (-286, -22)], "x", -8, 8)   # barrel clamp
    solid(B, "metal_dark", [(-40, 30), (96, 30), (104, 20), (104, -30), (-40, -30)], "x", -24, 24)
    solid(B, "metal_mid", rrect(-36, 30, 92, 36, 3), "x", -18, 18)
    solid(B, "team_wood", rrect(-30, 36, 86, 40, 1.5), "x", -6, 6)
    solid(B, "metal_bore", [(0, 0), (60, 0), (60, 20), (0, 20)], "x", -24.9, -23.9, shell=False)
    solid(B, "metal_dark", rrect(-28, -14, 40, 18, 3), "x", 24, 27)   # shell carrier
    for z in (-22, 2, 26):
        solid(B, "team_wood", ngon(z + 6, 2, 6, 8), "x", 27, 36)
        solid(B, "base_light", ngon(z + 6, 2, 6.5, 8), "x", 36, 39)
    trigger(B, 36, -30)
    solid(B, "team_wood", [(96, 20), (230, 4), (232, -66), (210, -68), (150, -40), (128, -40), (138, -96), (118, -100),
                           (100, -70), (96, -30)], "x", -15, 15)
    solid(B, "team_dark", [(230, 6), (242, 6), (242, -68), (232, -68)], "x", -16, 16)
    P = m.part("pump", (0, -14, -150))   # racks back 6 cm and home after each shot
    pump = [(-21, 2), (21, 2), (24, -12), (20, -26), (-20, -26), (-24, -12)]
    solid(P, "team_wood", pump, "z", -214, -94)
    for k in range(5):
        z = -206 + k * 24
        solid(P, "metal_dark", offset(ccw(pump), 1.6), "z", z, z + 6)
    return m


# ---- STICKY ---------------------------------------------------------------------------------------------------------

def sticky():
    m = Model("STICKY", "sticky launcher: a fat barrel with a flared mouth, a twelve-sided drum whose six team chambers "
                        "turn one a shot, canister with a glow strip, grip, folding stock")
    B = m.body
    solid(B, "metal_mid", ngon(0, 0, 30, 8), "z", -196, 6)
    loft(B, "metal_dark", ngon(0, 0, 32, 8), -196, ngon(0, 0, 44, 8), -212)
    solid(B, "metal_bore", ngon(0, 0, 26, 8), "z", -212.7, -212, shell=False)
    solid(B, "team_wood", ngon(0, 0, 33, 8), "z", -150, -136)
    cy = -20
    solid(B, "metal_dark", ngon(0, cy, 56, 12), "z", 10, 76)
    solid(B, "metal_mid", ngon(0, cy, 50, 12), "z", 76, 86)
    for k in range(6):   # glow windows round the drum's side
        a = math.radians(k * 60)
        B.place(rotz(a), (0, cy, 0))
        solid(B, "glow", [(-8, cy + 55.5), (8, cy + 55.5), (8, cy + 57.2), (-8, cy + 57.2)], "z", 30, 56, shell=False)
        B.unplace()
    solid(B, "metal_dark", rrect(-70, 24, 30, 52, 6), "x", -14, 14)
    solid(B, "glow", [(-60, 52), (20, 52), (20, 54.5), (-60, 54.5)], "x", -8, 8, shell=False)
    solid(B, "metal_dark", rrect(-176, -38, -90, -24, 2), "x", -24, 24)
    grip(B, "metal_dark", 92, -40, rake=26, length=62, w=14)
    trigger(B, 52, -40)
    solid(B, "metal_mid", [(86, 0), (196, -8), (196, -22), (110, -22), (100, -40), (86, -40)], "x", -10, 10)
    solid(B, "metal_mid", [(176, -8), (196, -8), (196, -58), (176, -58)], "x", -18, 18)
    solid(B, "team_wood", [(196, -6), (204, -6), (204, -60), (196, -60)], "x", -20, 20)
    P = m.part("drum", (0, cy, 40))   # one chamber a shot (60 degrees), about the drum's axis
    solid(P, "metal_mid", ngon(0, cy, 50, 12), "z", 2, 10)
    for k in range(6):
        a = math.radians(90 + 60 * k)
        x, y = 30 * math.cos(a), cy + 30 * math.sin(a)
        solid(P, "metal_bore" if k == 0 else "team_wood", ngon(x, y, 10, 8), "z", -2, 2)
    solid(P, "base_light", ngon(0, cy, 8, 6), "z", -3, 2)
    return m


# ---- CROSSBOW -------------------------------------------------------------------------------------------------------

def crossbow():
    m = Model("CROSSBOW", "crossbow: a dark stock with a light rail, a swept team prod with cams, a string that snaps "
                          "forward, the loaded bolt with a team head and fletching, scope, foregrip, grip, stock")
    B = m.body
    solid(B, "metal_dark", [(-222, 22), (60, 22), (140, 10), (140, -14), (60, -14), (-200, -10), (-222, 0)], "x", -15, 15)
    solid(B, "base_light", rrect(-226, 22, 20, 27, 1.5), "x", -6, 6)
    # the prod: a swept band seen from above, its cams at the tips
    band = []
    for i in range(9):
        t = -1 + 2 * i / 8
        x = 122 * t
        band.append((x, -196 + 26 * t * t))
    band2 = [(x, z + 16 - 4 * abs(x) / 122) for x, z in band]
    solid(B, "team_wood", band + band2[::-1], "y", 12, 28)
    solid(B, "metal_mid", rrect(-30, -214, 30, -186, 4), "y", 8, 32)
    for sx in (1, -1):
        solid(B, "metal_dark", ngon(sx * 124, -168, 11, 8), "y", 8, 32)
    solid(B, "metal_dark", rrect(-14, 22, 30, 44, 3), "x", -10, 10)                     # latch / sight base
    solid(B, "metal_dark", ngon(0, 52, 8, 8), "z", -40, 20)                             # scope
    solid(B, "glow", ngon(0, 52, 6, 8), "z", -40.7, -40, shell=False)
    solid(B, "team_wood", ngon(0, 52, 9, 8), "z", 0, 6)
    solid(B, "metal_mid", [(-150, -14), (-92, -14), (-96, -42), (-146, -42)], "x", -11, 11)
    trigger(B, 2, -14)
    grip(B, "metal_dark", 44, -14, rake=26, length=62, w=13)
    solid(B, "base_light", [(140, 12), (226, 6), (228, -50), (210, -52), (170, -30), (140, -14)], "x", -16, 16)
    solid(B, "team_wood", [(226, 8), (238, 8), (238, -54), (228, -52)], "x", -18, 18)
    P = m.part("string", (0, 30, -168))   # snaps forward on the shot, drawn back
    for sx in (1, -1):
        rod(P, "base_light", (sx * 124, 30, -168), (0, 30, -16), 1.6, 4)
    P = m.part("bolt", (0, 32, -60))      # the loaded bolt: gone on the shot, slides in
    solid(P, "base_light", ngon(0, 32, 2.6, 6), "z", -226, -40)
    loft(P, "team_wood", ngon(0, 32, 5.5, 4, phase=0), -226, ngon(0, 32, 0.8, 4, phase=0), -240)
    for k in range(3):
        a = math.pi / 2 + k * 2 * math.pi / 3
        P.place(rotz(a), (0, 32, 0))
        solid(P, "team_wood", [(-62, 0), (-40, 0), (-44, 9), (-58, 9)], "x", -0.8, 0.8) if False else None
        P.unplace()
    for a in (math.pi / 2, math.pi / 2 + 2.094, math.pi / 2 - 2.094):
        P.place(rotz(a - math.pi / 2), (0, 32, 0))
        solid(P, "team_wood", [(-64, 34), (-40, 34), (-44, 43), (-60, 43)], "x", -0.9, 0.9)
        P.unplace()
    return m


# ---- BUBBLE ---------------------------------------------------------------------------------------------------------

def bubble():
    m = Model("BUBBLE", "bubble gun: a rounded body, a big glass soap tank with glowing soap and a team cap, a flared "
                        "nozzle with a team ring, an octagonal wand ring, grip and a team lever that pulls")
    B = m.body
    solid(B, "metal_mid", rrect(-112, -34, 112, 34, 12), "x", -28, 28)
    solid(B, "metal_dark", rrect(-104, 30, 100, 40, 4), "x", -20, 20)
    # the soap tank: a twelve-sided drum across the gun, glowing soap inside a lighter shell
    solid(B, "glow", ngon(20, 70, 29, 12), "x", -22, 22)
    solid(B, "base_glass", ngon(20, 70, 38, 12), "x", -28, 28, shell=False)
    for sx in (1, -1):
        lo, hi = (26, 36) if sx > 0 else (-36, -26)
        solid(B, "metal_dark", ngon(20, 70, 38, 12), "x", lo, hi) if False else None
    solid(B, "team_wood", rrect(10, 106, 30, 116, 3), "x", -12, 12)
    solid(B, "metal_dark", rrect(12, 34, 28, 42, 2), "x", -8, 8)
    for x, y, z, r in ((-30, 8, -40, 7), (30, -10, 50, 9)):
        solid(B, "glow", ngon(z, y, r, 8), "x", (x - 3) if x < 0 else x - 1, (x + 1) if x < 0 else x + 3)
    solid(B, "metal_dark", ngon(0, 4, 13, 8), "z", -200, -112)
    solid(B, "team_wood", ngon(0, 4, 18, 8), "z", -128, -118)
    loft(B, "team_wood", ngon(0, 4, 13, 8), -200, ngon(0, 4, 26, 8), -214)
    solid(B, "metal_dark", ngon(0, 4, 5, 6), "z", -250, -214)
    for k in range(4):   # spokes to the wand ring
        B.place(rotz(math.pi / 4 + k * math.pi / 2), (0, 4, 0))
        solid(B, "metal_dark", [(-2, 4 + 4), (2, 4 + 4), (2, 4 + 25), (-2, 4 + 25)], "z", -254, -250)
        B.unplace()
    for k in range(8):   # the wand ring: eight segments
        a = 2 * math.pi * k / 8
        B.place(rotz(a), (0, 4, 0))
        solid(B, "metal_mid", [(-11, 4 + 24), (11, 4 + 24), (13, 4 + 31), (-13, 4 + 31)], "z", -257, -248)
        B.unplace()
    grip(B, "metal_dark", 18, -34, rake=24, length=60, w=13)
    solid(B, "metal_dark", rrect(112, -30, 124, 30, 4), "x", -26, 26)
    P = m.part("lever", (0, -38, -10))   # pulls back 25 degrees on the shot
    solid(P, "base_light", [(-18, -36), (-6, -36), (-4, -70), (-14, -74)], "x", -4, 4)
    solid(P, "team_wood", rrect(-20, -82, -2, -72, 2), "x", -6, 6)
    return m


# ---- SYRINGE --------------------------------------------------------------------------------------------------------

def syringe():
    m = Model("SYRINGE", "syringe: a see-through glass barrel with glowing serum and dose marks, dark caps, a team finger "
                         "flange, hub and needle with a hot tip; the plunger pushes in")
    B = m.body
    solid(B, "glow", ngon(0, 0, 15, 8), "z", -84, 56, shell=False)
    solid(B, "base_glass", ngon(0, 0, 21, 8), "z", -90, 62, shell=False)
    for z in (-50, -15, 20):
        solid(B, "metal_dark", [(-8, 20), (8, 20), (8, 22.5), (-8, 22.5)], "z", z, z + 4, shell=False)
    solid(B, "metal_dark", ngon(0, 0, 22, 8), "z", -100, -90)
    solid(B, "metal_dark", ngon(0, 0, 22, 8), "z", 62, 72)
    solid(B, "team_wood", rrect(-46, -8, 46, 8, 5), "z", 72, 84)
    loft(B, "metal_dark", ngon(0, 0, 12, 8), -100, ngon(0, 0, 5, 8), -116)
    solid(B, "metal_mid", ngon(0, 0, 2.2, 6), "z", -198, -116)
    solid(B, "glow_hot", ngon(0, 0, 1.6, 6), "z", -201, -198, shell=False)
    P = m.part("plunger", (0, 0, 90))   # pushes in 3 cm on the shot and back
    solid(P, "metal_mid", ngon(0, 0, 6, 8), "z", 56, 132)
    solid(P, "metal_dark", ngon(0, 0, 18, 8), "z", 48, 56, shell=False)
    solid(P, "metal_dark", ngon(0, 0, 20, 8), "z", 132, 138)
    solid(P, "team_wood", ngon(0, 0, 15, 8), "z", 138, 142)
    return m


# ---- LOCATOR --------------------------------------------------------------------------------------------------------

def locator():
    m = Model("LOCATOR", "tracker: a chamfered slab with a dark bezel and a dim radar screen with a sweeping line and a "
                         "team blip, an emitter with a glow lens, grip, and a whip antenna that springs as you turn")
    B = m.body
    solid(B, "metal_mid", [(-100, 10), (-90, 18), (62, 18), (70, 10), (70, -18), (60, -26), (-90, -26), (-100, -18)], "x", -42, 42)
    solid(B, "metal_dark", rrect(-36, -76, 36, 64, 8), "y", 18, 22)                   # bezel (x, z)
    solid(B, "glow_dim", rrect(-30, -46, 30, 54, 6), "y", 22, 22.8, shell=False)
    solid(B, "team_wood", ngon(12, -22, 5, 8), "y", 22.8, 24, shell=False)
    solid(B, "metal_dark", rrect(-100, -18, -90, 12, 3), "x", -36, 36) if False else None
    loft(B, "metal_dark", rrect(-22, -18, 22, 12, 4), -100, rrect(-14, -12, 14, 6, 3), -122)
    solid(B, "glow", ngon(0, -3, 8, 8), "z", -125, -122)
    solid(B, "metal_dark", [(26, -26), (60, -26), (72, -90), (68, -96), (44, -96), (40, -84), (28, -40)], "x", -15, 15)
    solid(B, "team_wood", [(36, -40), (56, -40), (64, -80), (46, -80)], "x", 15, 16.5, shell=False)
    solid(B, "team_wood", [(36, -40), (56, -40), (64, -80), (46, -80)], "x", -16.5, -15, shell=False)
    solid(B, "metal_dark", ngon(30, 50, 6, 8), "y", 14, 22)                              # antenna base (x, z)
    P = m.part("sweep", (0, 24, 4))     # turns once every 2 s
    solid(P, "glow_hot", [(-1.5, 4), (1.5, 4), (1.5, -24), (-1.5, -24)], "y", 22.8, 23.6, shell=False)
    P = m.part("antenna", (30, 16, 50)) # springs about X as you turn
    solid(P, "metal_mid", ngon(30, 50, 2.2, 6), "y", 20, 104)
    solid(P, "team_wood", ngon(30, 50, 5, 8), "y", 104, 113)
    return m


# ---- the grapple launcher and its claw ---------------------------------------------------------------------------

def hook_arm():
    """PROP_HOOK_ARM: the left forearm (z 0 to -540) with the launcher strapped on: housing, cable drum on the
    outside (-X), barrel on top whose mouth is kHookMuzzle (0, 70, -660)"""
    m = Model("HOOK_ARM", "grapple launcher: a faceted sleeve and glove, a housing strapped on with team straps, the cable "
                          "drum on the outside, a long barrel with a flared mouth and a team stripe")
    B = m.body
    loft(B, "sleeve", ngon(0, -10, 58, 8, sx=0.95, sy=1.05), 0, ngon(0, -10, 50, 8, sx=0.95, sy=1.05), -470)
    loft(B, "sleeve", ngon(0, -12, 50, 8, sx=0.95, sy=1.05), -470, ngon(0, -16, 42, 8), -545)   # the hand
    solid(B, "metal_dark", rrect(-62, -30, 62, 72, 12), "z", -520, -300)                     # housing
    loft(B, "metal_dark", rrect(-62, -30, 62, 72, 12), -300, rrect(-52, -20, 52, 58, 10), -270)
    for z in (-470, -360):
        solid(B, "team_wood", rrect(-64.5, -32.5, 64.5, 74.5, 13), "z", z, z + 16)
    solid(B, "team_wood", rrect(-26, -508, 26, -312, 4), "y", 72, 76)                         # team stripe (x, z)
    solid(B, "metal_mid", ngon(-38, 15, 46, 10), "x", -86, -62)                               # cable drum (z, y)
    solid(B, "metal_dark", ngon(-380, 15, 18, 8), "x", -90, -86)
    solid(B, "metal_mid", ngon(0, 70, 40, 8), "z", -620, -520)                               # barrel
    loft(B, "metal_dark", ngon(0, 70, 40, 8), -620, ngon(0, 70, 48, 8), -660)
    solid(B, "metal_bore", ngon(0, 70, 34, 8), "z", -660.7, -660, shell=False)
    solid(B, "glow", [(-600, -4), (-540, -4), (-540, 4), (-600, 4)], "x", -41.6, -40.4, shell=False) if False else None
    return m


def hook():
    """PROP_HOOK: the claw. Origin: where the rope ties on; prongs toward -Z"""
    m = Model("HOOK", "grapple claw: an eye, a faceted shank, a head and three prongs that sweep out and hook back")
    B = m.body
    solid(B, "metal_steel", ngon(0, 0, 8, 6), "z", -6, 6) if False else None
    solid(B, "metal_steel", ngon(0, 0, 13, 8), "z", -62, 0)
    loft(B, "metal_steel", ngon(0, 0, 18, 8), -62, ngon(0, 0, 34, 8), -80)
    solid(B, "metal_steel", ngon(0, 0, 34, 8), "z", -92, -80)
    for k in range(3):
        a = math.pi / 2 + k * 2 * math.pi / 3
        c, s = math.cos(a), math.sin(a)
        p0 = (24 * c, 24 * s, -88)
        p1 = (56 * c, 56 * s, -132)
        p2 = (40 * c, 40 * s, -158)
        rod(B, "metal_steel", p0, p1, 8, 6)
        rod(B, "metal_steel", p1, p2, 7, 6)
    return m


WEAPONS = {"RIFLE": rifle, "ROCKET": rocket, "KNIFE": knife, "SNIPER": sniper, "LASER": laser, "SHOTGUN": shotgun,
           "STICKY": sticky, "CROSSBOW": crossbow, "BUBBLE": bubble, "SYRINGE": syringe, "LOCATOR": locator}
PROPS = {"HOOK_ARM": hook_arm, "HOOK": hook}


def bounds(m):
    pts = [p for mesh in [m.body] + [x[1] for x in m.parts] for tris in mesh.tris.values() for t in tris for p in t]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "assets", "overrides"))
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = [s.strip().upper() for s in a.only.split(",") if s.strip()]
    ok = True
    for group, table in (("weapon", WEAPONS), ("prop", PROPS)):
        for name, fn in table.items():
            if only and name not in only:
                continue
            m = fn()
            lo, hi = bounds(m)
            tris = write_gltf(os.path.join(a.out, "models", group, name + ".gltf"), name, m.nodes(),
                              {"style": "low-poly outlined", "model": name, "description": m.desc})
            note = ""
            if name in MUZZLE:
                front = lo[2]
                if abs(front - MUZZLE[name][2]) > 14:
                    note = "  ERROR: front at z %.0f, the muzzle is at %.0f" % (front, MUZZLE[name][2])
                    ok = False
            if tris > 20000:
                note += "  ERROR: over 20 000 triangles"
                ok = False
            print("%-9s %5d triangles, %d moving parts, x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f%s" % (
                name, tris, len(m.parts), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], note))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
