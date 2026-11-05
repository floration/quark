#!/usr/bin/env python3
"""Outlined arena weapons: all 11 first-person weapons as box models (q, 2026-11-05: "outlined for sure", "maybe
just outlined but nicer models?").

Box models (meshes[0].extras.quark version 2), so the game draws them on its own box path with its 1 px outlines.
Same palette rules as direction 1 of gen_clean_styles.py: dark = metal x 0.55, mid = metal, light = base (both
follow each map's colours like the built-ins), bore = metal x 0.25, big team panels, a little glow. More shape than
the built-ins: stepped chamfers, rails with teeth, brakes with ports, crossed boxes for round tubes.

Moving parts: a box with "part" + "pivot" (docs/ASSETS.md "Box models", game commit "Box models: moving weapon
parts") moves like an anim_ node in first person (weaponPartMotion).

Coordinates are millimetres here (x right, y up, -z toward the muzzle; the files are metres). The muzzle of every
weapon sits where the built-in's is (kMuzzle, game.cpp), so flashes and tracers line up.

  python3 tools/models/gen_weapons_outlined.py [--ingame] [--out DIR]   (writes assets/overrides/models/weapon; shots in --scratch)
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GAME = os.path.join(REPO, "build", "quark")

REF = {
    "metal": tuple(c ** 2.2 * 1.6 + 0.05 for c in (0.30, 0.33, 0.37)),
    "base": tuple(c ** 2.2 * 1.1 for c in (0.62, 0.62, 0.64)),
    "team": (1.0, 1.0, 1.0),
    "glow": (1.0, 0.55, 0.15),
}
TONES = {   # tone -> (slot, factor of the slot's reference colour)
    "dark": ("metal", 0.55), "mid": ("metal", 1.0), "bore": ("metal", 0.25), "light": ("base", 1.0),
    "team": ("team", 1.0), "glow": ("glow", 1.0), "glowdim": ("glow", 0.55), "glowhot": ("glow", 1.4),
}
MUZZLE = {   # kMuzzle (game.cpp), mm
    "RIFLE": (0, 8, -290), "ROCKET": (0, 0, -250), "KNIFE": (0, 0, -210), "SNIPER": (0, 6, -448), "LASER": (0, 0, -193),
    "SHOTGUN": (0, 12, -300), "STICKY": (0, 0, -212), "CROSSBOW": (0, 32, -230), "BUBBLE": (0, 4, -257),
    "SYRINGE": (0, 0, -200), "LOCATOR": (0, 0, -125),
}
ORDER = ["RIFLE", "ROCKET", "KNIFE", "SNIPER", "LASER", "SHOTGUN", "STICKY", "CROSSBOW", "BUBBLE", "SYRINGE", "LOCATOR"]


class W:
    def __init__(self, name, desc):
        self.name, self.desc, self.boxes = name, desc, []
        self.part, self.pivot = None, None

    def b(self, x0, x1, y0, y1, z0, z1, tone, label="", alpha=1.0):
        assert x1 > x0 and y1 > y0 and z1 > z0, (self.name, label)
        self.boxes.append({"lo": (x0, y0, z0), "hi": (x1, y1, z1), "tone": tone, "name": label,
                           "part": self.part, "pivot": self.pivot, "alpha": alpha})

    def c(self, cx, cy, cz, hx, hy, hz, tone, label="", alpha=1.0):   # centre + half size
        self.b(cx - hx, cx + hx, cy - hy, cy + hy, cz - hz, cz + hz, tone, label, alpha)

    def sym(self, x0, x1, y0, y1, z0, z1, tone, label=""):   # a pair mirrored across x = 0 (x0, x1 > 0)
        self.b(x0, x1, y0, y1, z0, z1, tone, label + " R")
        self.b(-x1, -x0, y0, y1, z0, z1, tone, label + " L")

    def tube(self, cy, r, z0, z1, tone, label="", cx=0, k=0.82):   # a round tube as two crossed boxes (an octagon)
        self.b(cx - r, cx + r, cy - r * k, cy + r * k, z0, z1, tone, label + " a")
        self.b(cx - r * k, cx + r * k, cy - r, cy + r, z0, z1, tone, label + " b")

    def moving(self, part, pivot):
        self.part, self.pivot = part, pivot

    def still(self):
        self.part, self.pivot = None, None


# ----------------------------------------------------------------------------------------------------------------------

def rifle():
    w = W("RIFLE", "carbine: stepped upper and lower receiver, toothed top rail, light handguard with team panels and a "
                   "rail, gas block, ported brake, curved stepped magazine, angled grip, light stock with team pad")
    w.b(-22, 22, 0, 30, -100, 95, "dark", "upper receiver")
    w.b(-17, 17, 30, 35, -96, 90, "mid", "upper chamfer")
    w.b(-20, 20, -28, 0, -82, 62, "mid", "lower receiver")
    w.b(-9, 9, 35, 39, -94, 70, "dark", "top rail")
    for k in range(7):
        w.b(-11, 11, 39, 42, -88 + k * 22, -78 + k * 22, "dark", "rail tooth")
    w.b(22, 23.5, 6, 20, -44, -4, "bore", "ejection port")
    w.b(-23.5, -22, 6, 16, -62, -22, "glow", "ammo counter")
    w.b(-17, 17, -40, -28, -62, -14, "dark", "magazine well")
    w.b(-13, 13, -62, -40, -56, -20, "mid", "magazine 1")
    w.b(-13, 13, -88, -62, -50, -14, "mid", "magazine 2")
    w.b(-13, 13, -108, -88, -44, -8, "mid", "magazine 3")
    w.b(-15, 15, -114, -108, -46, -6, "team", "magazine plate")
    w.b(-4, 4, -46, -42, -14, 32, "mid", "trigger guard")
    w.b(-2, 2, -42, -28, 2, 9, "light", "trigger")
    w.b(-14, 14, -60, -28, 34, 64, "dark", "grip 1")
    w.b(-14, 14, -96, -60, 44, 76, "dark", "grip 2")
    w.b(-20, 20, -18, 24, -232, -100, "light", "handguard")
    w.b(-15, 15, -23, -18, -228, -104, "light", "handguard chamfer")
    w.sym(20, 21.5, -8, 14, -218, -116, "team", "handguard panel")
    w.b(-8, 8, 24, 29, -226, -104, "dark", "handguard rail")
    for k in range(5):
        w.b(-10, 10, 29, 32, -222 + k * 24, -212 + k * 24, "dark", "handguard tooth")
    w.b(-16, 16, -14, 20, -238, -232, "dark", "handguard cap")
    w.b(-7, 7, 1, 15, -276, -238, "mid", "barrel")
    w.b(-10, 10, -3, 19, -262, -250, "dark", "gas block")
    w.b(-3, 3, 32, 50, -230, -222, "dark", "front sight post")
    w.b(-7, 7, 29, 34, -232, -220, "dark", "front sight base")
    w.b(-11, 11, -3, 19, -300, -276, "dark", "brake")
    for z in (-293, -284):
        w.b(-12, 12, 3, 13, z, z + 5, "bore", "brake port")
    w.b(-5, 5, 3, 13, -301, -300, "bore", "bore")
    w.b(-10, 10, 42, 52, 62, 82, "dark", "rear sight")
    w.b(-3, 3, 46, 49, 61, 62, "glow", "rear aperture")
    w.b(-12, 12, -2, 20, 95, 142, "dark", "buffer tube")
    w.b(-18, 18, -40, 24, 142, 226, "light", "stock")
    w.b(-15, 15, -46, -40, 158, 226, "light", "stock heel")
    w.b(-14, 14, 24, 32, 150, 216, "dark", "cheek riser")
    w.sym(18, 19.5, -12, 6, 152, 214, "team", "stock panel")
    w.b(-20, 20, -48, 28, 226, 240, "team", "butt pad")
    # moving: the charging handle at the rear of the upper (back 2.5 cm and home, each shot)
    w.moving("bolt", (0, 26, 96))
    w.b(-13, 13, 21, 29, 95, 108, "mid", "charging handle")
    w.b(13, 21, 21, 29, 99, 108, "light", "charging latch")
    w.still()
    return w


def rocket():
    w = W("ROCKET", "launcher: octagonal tube (crossed boxes), flared muzzle collar and exhaust, two team bands, glow "
                    "charge window, toothed top rail, side scope with a glow lens, foregrip, grip, shoulder rest")
    w.tube(0, 44, -214, 182, "mid", "tube")
    w.tube(0, 54, -250, -214, "dark", "muzzle collar")
    w.b(-34, 34, -34, 34, -251, -250, "bore", "bore")
    w.tube(0, 46, -150, -122, "team", "front band")
    w.tube(0, 46, 88, 114, "team", "rear band")
    w.b(-46, -44.5, -9, 9, -70, 30, "glow", "charge window")
    w.b(44.5, 46, -30, 30, -40, 70, "light", "heat shield")
    w.tube(0, 50, 182, 204, "dark", "exhaust")
    w.b(-38, 38, -38, 38, 204, 214, "dark", "venturi")
    w.b(-27, 27, -27, 27, 214, 215, "bore", "exhaust bore")
    w.b(-12, 12, 44, 52, -104, 84, "dark", "top rail")
    for k in range(8):
        w.b(-14, 14, 52, 55, -98 + k * 23, -88 + k * 23, "dark", "rail tooth")
    w.b(-74, -50, 10, 40, -100, -30, "dark", "scope")
    w.b(-70, -54, 14, 36, -101, -100, "glow", "scope lens")
    w.b(-50, -44, 18, 32, -82, -48, "mid", "scope mount")
    w.b(-76, -48, 8, 42, -36, -30, "team", "scope ring")
    w.b(-12, 12, -96, -44, -116, -86, "dark", "foregrip")
    w.b(-14, 14, -54, -44, -120, -82, "mid", "foregrip clamp")
    w.b(-14, 14, -100, -44, 30, 60, "dark", "grip")
    w.b(-4, 4, -58, -54, -2, 30, "mid", "trigger guard")
    w.b(-2, 2, -54, -44, 10, 16, "light", "trigger")
    w.b(-30, 30, -62, -44, 108, 172, "dark", "shoulder rest")
    w.moving("round", (0, 0, -236))   # the round's nose: gone on the shot, slides back up the tube
    w.tube(0, 22, -246, -232, "team", "warhead")
    w.b(-11, 11, -11, 11, -253, -246, "team", "warhead tip")
    w.still()
    return w


def knife():
    w = W("KNIFE", "combat knife: ringed dark grip, guard with tips, stepped spine and light edge to a point, glow fuller, "
                   "spine teeth, team pommel and lanyard ring")
    w.b(-12, 12, -26, 6, 0, 95, "dark", "grip")
    for z in (14, 44, 74):
        w.b(-13, 13, -27, 7, z, z + 7, "mid", "grip ring")
    w.b(-34, 34, -18, 2, -22, -6, "mid", "guard")
    w.sym(34, 40, -24, 8, -21, -7, "dark", "guard tip")
    w.b(-5, 5, -2, 10, -196, -22, "mid", "spine")
    w.b(-3, 3, -24, -2, -188, -22, "light", "edge")
    w.b(-3, 3, -18, -2, -202, -188, "light", "edge tip")
    w.b(-4, 4, -10, 6, -212, -196, "light", "point")
    w.b(-5.5, 5.5, 1, 4, -158, -40, "glow", "fuller")
    for k in range(4):
        w.b(-4, 4, 10, 13, -96 + k * 12, -90 + k * 12, "dark", "spine tooth")
    w.b(-14, 14, -24, 4, 95, 105, "team", "pommel")
    w.b(-5, 5, -16, -4, 105, 116, "mid", "lanyard ring")
    return w


def sniper():
    w = W("SNIPER", ".50-style anti-materiel rifle: long stepped receiver with a toothed rail, fluted barrel, big ported "
                    "brake, light handguard with team panels, scope with bell, team band, turrets and glow lens, bolt "
                    "on the left, magazine, grip, light stock with team cheek rest")
    w.b(-22, 22, -20, 28, -110, 130, "dark", "receiver")
    w.b(-17, 17, 28, 34, -106, 124, "mid", "receiver chamfer")
    w.b(-9, 9, 34, 39, -110, 112, "dark", "top rail")
    for k in range(9):
        w.b(-11, 11, 39, 41, -104 + k * 24, -94 + k * 24, "dark", "rail tooth")
    w.b(22, 23.5, 2, 20, 30, 80, "bore", "ejection port")
    w.b(-8, 8, -2, 14, -406, -280, "mid", "barrel")
    for y0 in (14, -3.5):
        w.b(-2.5, 2.5, y0, y0 + 1.5, -396, -290, "dark", "flute")
    w.b(-16, 16, -9, 21, -448, -406, "dark", "brake")
    for z in (-441, -429, -417):
        w.b(-17.5, 17.5, -1, 13, z, z + 7, "bore", "brake port")
    w.b(-6, 6, 0, 12, -449, -448, "bore", "bore")
    w.b(-19, 19, -18, 26, -280, -110, "light", "handguard")
    w.b(-14, 14, -23, -18, -276, -114, "light", "handguard chamfer")
    w.sym(19, 20.5, -6, 14, -266, -126, "team", "handguard panel")
    w.b(-15, 15, 45, 71, -112, 80, "dark", "scope tube")
    w.b(-20, 20, 38, 78, -152, -112, "dark", "scope bell")
    w.b(-14, 14, 44, 72, -153, -152, "glow", "scope lens")
    w.b(-21, 21, 37, 79, -124, -116, "team", "scope band")
    w.b(-18, 18, 41, 75, 80, 110, "dark", "eyepiece")
    w.b(-19, 19, 40, 76, 100, 106, "team", "eyepiece ring")
    w.b(-6, 6, 71, 84, -36, -14, "mid", "turret top")
    w.b(15, 28, 52, 64, -36, -14, "mid", "turret side")
    for z in (-86, 44):
        w.b(-10, 10, 39, 45, z, z + 20, "mid", "scope ring")
    w.b(-14, 14, -62, -20, -56, 8, "mid", "magazine")
    w.b(-16, 16, -68, -62, -58, 10, "team", "magazine plate")
    w.b(-4, 4, -46, -42, 34, 82, "mid", "trigger guard")
    w.b(-2, 2, -42, -20, 52, 58, "light", "trigger")
    w.b(-14, 14, -60, -20, 82, 116, "dark", "grip 1")
    w.b(-14, 14, -92, -60, 92, 126, "dark", "grip 2")
    w.b(-18, 18, -40, 20, 130, 242, "light", "stock")
    w.b(-10, 10, -50, -40, 160, 236, "dark", "stock rail")
    w.b(-14, 14, 20, 34, 150, 222, "team", "cheek rest")
    w.b(-20, 20, -56, 28, 242, 256, "dark", "butt pad")
    # moving: the bolt handle on the left wall (up 70 degrees about the bore axis, back 1.5 cm, forward, down)
    w.moving("bolt", (-22, 12, 60))
    w.b(-40, -22, 8, 16, 55, 65, "mid", "bolt stem")
    w.b(-52, -40, 4, 20, 52, 68, "light", "bolt knob")
    w.still()
    return w


def laser():
    w = W("LASER", "beam rifle: stepped body with side fins, a glow strip, four coil segments on top, emitter housing "
                   "with a spinning cross-shaped coil, team sight, grip and power cell")
    w.b(-36, 36, -36, 40, -150, 150, "mid", "body")
    w.b(-28, 28, 40, 47, -142, 142, "dark", "body top")
    w.b(-28, 28, -42, -36, -130, 120, "dark", "body bottom")
    w.sym(36, 44, -26, 30, -104, 64, "dark", "fin")
    w.b(44, 45.5, -6, 6, -84, 44, "glow", "capacitor")
    for k in range(4):
        w.b(-22, 22, 47, 58, -96 + k * 50, -76 + k * 50, "glow" if k % 2 == 0 else "dark", "coil")
    w.b(-8, 8, 47, 52, -100, 80, "dark", "coil spine")
    w.b(-6, 6, 58, 66, 88, 106, "team", "sight")
    w.b(-31, 31, -31, 31, -176, -150, "dark", "emitter housing")
    w.b(-9, 9, -9, 9, -194, -176, "dark", "nozzle")
    w.b(-14, 14, -100, -42, 14, 46, "dark", "grip")
    w.b(-4, 4, -56, -42, -24, 14, "mid", "trigger guard")
    w.b(-22, 22, -56, -14, 96, 150, "dark", "power cell")
    w.b(-18, 18, -52, -18, 150, 156, "team", "cell cap")
    w.b(-30, 30, -34, 38, 150, 160, "dark", "back plate")
    # moving: the coil at the emitter spins about the bore while you fire
    w.moving("spin", (0, 0, -185))
    w.b(-26, 26, -6, 6, -190, -180, "glow", "coil bar")
    w.b(-6, 6, -26, -6, -190, -180, "glow", "coil bar low")
    w.b(-6, 6, 6, 26, -190, -180, "glow", "coil bar high")
    for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        w.c(sx * 19, sy * 19, -185, 5, 5, 6, "team", "coil nub")
    w.still()
    return w


def shotgun():
    w = W("SHOTGUN", "pump shotgun: twin barrels with a rib, bead and band, a toothed light pump with team grips, "
                     "magazine tube, heavy receiver with a team shell carrier, grip, light stock with team pad")
    w.b(-23, -1, 1, 23, -300, -40, "mid", "barrel L")
    w.b(1, 23, 1, 23, -300, -40, "mid", "barrel R")
    w.b(-3, 3, 23, 27, -300, -40, "dark", "rib")
    w.b(-3, 3, 27, 33, -297, -290, "light", "bead")
    w.b(-26, 26, -1, 25, -300, -286, "dark", "muzzle band")
    w.b(-25, 25, 0, 24, -205, -197, "dark", "barrel band")
    for x0 in (-18, 6):
        w.b(x0, x0 + 12, 6, 18, -301, -300, "bore", "bore")
    w.b(-12, 12, -30, -6, -290, -40, "dark", "magazine tube")
    w.b(-30, 30, -35, 35, -40, 100, "dark", "receiver")
    w.b(-24, 24, 35, 40, -36, 94, "mid", "receiver chamfer")
    w.b(-8, 8, 40, 44, -30, 88, "team", "top stripe")
    for z in (-24, 0, 24):
        w.b(30, 36, -10, 18, z, z + 14, "team", "shell")
        w.b(30, 36, -10, 18, z + 14, z + 18, "light", "shell rim")
    w.b(-31.5, -30, 2, 22, 10, 60, "bore", "ejection port")
    w.b(-5, 5, -56, -35, 30, 80, "mid", "trigger guard")
    w.b(-2, 2, -48, -35, 44, 50, "light", "trigger")
    w.b(-16, 16, -62, -35, 80, 112, "dark", "grip 1")
    w.b(-16, 16, -94, -62, 90, 122, "dark", "grip 2")
    w.b(-24, 24, -50, 20, 100, 220, "light", "stock")
    w.b(-18, 18, -56, -50, 120, 220, "light", "stock heel")
    w.b(-26, 26, -58, 26, 220, 233, "team", "butt pad")
    # moving: the pump racks back 6 cm and home after each shot
    w.moving("pump", (0, -18, -150))
    w.b(-22, 22, -36, -2, -212, -92, "light", "pump")
    for k in range(4):
        w.b(-24, 24, -38, 0, -204 + k * 30, -198 + k * 30, "dark", "pump rib")
    w.sym(22, 23.5, -28, -10, -192, -108, "team", "pump grip")
    w.still()
    return w


def sticky():
    w = W("STICKY", "sticky launcher: stepped barrel with a flared mouth, an octagonal drum with a team band and glow "
                    "windows whose six team chamber caps turn one chamber a shot, canister with glow strip, grip, stock")
    w.tube(0, 30, -192, -70, "mid", "barrel")
    w.tube(0, 37, -202, -192, "dark", "mouth")
    w.tube(0, 43, -212, -202, "mid", "flare")
    w.b(-24, 24, -24, 24, -213, -212, "bore", "bore")
    w.b(-32, 32, -40, -32, -172, -88, "dark", "under rail")
    cy = -20
    w.tube(cy, 55, 6, 76, "dark", "drum")
    w.tube(cy, 57, 34, 46, "team", "drum band")
    for z0 in (14, 54):
        w.sym(56, 58.5, cy - 8, cy + 8, z0, z0 + 14, "glow", "drum window")
    w.b(-40, 40, cy - 40, cy + 40, 0, 6, "mid", "drum front plate")
    w.b(-40, 40, cy - 40, cy + 40, 76, 82, "mid", "drum back plate")
    w.b(-14, 14, 30, 58, -70, 30, "dark", "canister")
    w.b(-8, 8, 58, 61, -60, 20, "glow", "canister strip")
    w.b(-10, 10, 34, 54, 30, 37, "mid", "canister cap")
    w.b(-14, 14, -105, -55, 88, 118, "dark", "grip")
    w.b(-4, 4, -82, -76, 50, 88, "mid", "trigger guard")
    w.b(-20, 20, -55, -5, 90, 190, "mid", "stock")
    w.b(-22, 22, -58, -2, 190, 198, "team", "butt pad")
    # moving: the drum's chamber caps and hub turn 60 degrees a shot about the drum's axis
    w.moving("drum", (0, cy, 40))
    import math
    for k in range(6):
        a = math.radians(90 + 60 * k)
        x, y = 28 * math.cos(a), cy + 28 * math.sin(a)
        w.c(x, y, -1.5, 8, 8, 1.5, "bore" if k == 0 else "team", "chamber cap")
    w.c(0, cy, -2, 7, 7, 2, "light", "hub")
    w.still()
    return w


def crossbow():
    w = W("CROSSBOW", "crossbow: dark stock with a light flight rail, stepped prod and team limbs to dark cams, a light "
                      "string, latch with team sight and glow dot, foregrip, grip, light stock with team pad")
    w.b(-16, 16, -10, 22, -222, 140, "dark", "body")
    w.b(-12, 12, -14, -10, -210, 130, "dark", "body chamfer")
    w.b(-6, 6, 22, 27, -226, 0, "light", "flight rail")
    w.b(-30, 30, 8, 30, -212, -188, "mid", "prod")
    w.sym(30, 80, 12, 27, -202, -186, "mid", "limb")
    w.sym(80, 116, 14, 25, -188, -172, "team", "limb tip")
    w.sym(114, 130, 8, 31, -182, -160, "dark", "cam")
    w.b(-10, 10, 22, 40, -10, 32, "dark", "latch")
    w.b(-4, 4, 40, 52, 0, 22, "team", "sight")
    w.b(-2, 2, 46, 49, -1, 0, "glow", "sight dot")
    w.b(-12, 12, -40, -10, -152, -88, "mid", "foregrip")
    w.b(-14, 14, -82, -10, 40, 76, "dark", "grip")
    w.b(-4, 4, -36, -32, 0, 40, "mid", "trigger guard")
    w.b(-2, 2, -32, -10, 16, 22, "light", "trigger")
    w.b(-18, 18, -40, 18, 140, 230, "light", "stock")
    w.b(-20, 20, -46, 22, 230, 240, "team", "butt pad")
    # moving: the string snaps forward on the shot and is drawn back
    w.moving("string", (0, 30, -168))
    w.b(-121, 121, 28.5, 31.5, -170, -166, "light", "string")
    # moving: the loaded bolt (gone on the shot, slides in once the string is back)
    w.moving("bolt", (0, 32, -60))
    w.b(-2.5, 2.5, 28, 35, -228, -38, "light", "bolt shaft")
    w.b(-5, 5, 27, 37, -238, -226, "team", "bolt head")
    w.b(-1, 1, 35, 45, -62, -40, "team", "fletching top")
    w.sym(2.5, 11, 30, 33, -62, -40, "team", "fletching side")
    w.still()
    return w


def bubble():
    w = W("BUBBLE", "bubble gun: body, a big light soap tank with glow windows and a team cap, dark nozzle with a team "
                    "ring and flare, a square bubble wand, grip and a team trigger lever that pulls")
    w.b(-30, 30, -35, 34, -112, 112, "mid", "body")
    w.b(-24, 24, 34, 38, -100, 100, "dark", "body top")
    w.tube(66, 40, -30, 70, "light", "tank")
    w.sym(39, 41.5, 46, 86, -10, 50, "glow", "tank window")
    w.b(-14, 14, 106, 115, 6, 34, "team", "tank cap")
    w.b(-8, 8, 32, 40, 12, 28, "dark", "tank neck")
    w.c(-31, 10, -40, 6, 6, 6, "glow", "side bubble")
    w.c(31, -10, 50, 8, 8, 8, "glow", "side bubble")
    w.b(-14, 14, -10, 18, -200, -112, "dark", "nozzle")
    w.b(-18, 18, -14, 22, -128, -118, "team", "nozzle ring")
    w.b(-30, 30, -26, 34, -214, -200, "team", "flare")
    w.b(-26, 26, 26, 30, -257, -249, "mid", "wand top")
    w.b(-26, 26, -22, -18, -257, -249, "mid", "wand bottom")
    w.sym(22, 26, -18, 26, -257, -249, "mid", "wand side")
    w.b(-6, 6, -8, 16, -249, -214, "dark", "wand stem")
    w.b(-14, 14, -96, -35, 14, 56, "dark", "grip")
    w.b(-28, 28, -30, 30, 112, 126, "dark", "back plate")
    # moving: the trigger lever pulls back 25 degrees on the shot
    w.moving("lever", (0, -38, -10))
    w.b(-4, 4, -74, -38, -16, -4, "light", "lever")
    w.b(-7, 7, -80, -74, -19, -1, "team", "lever pad")
    w.still()
    return w


def syringe():
    w = W("SYRINGE", "syringe: see-through light barrel with glow serum and dose marks, dark caps, team finger flange, "
                     "hub and needle with a hot tip, plunger rod, seal and team thumb pad that push in")
    w.b(-16, 16, -16, 16, -84, 56, "glow", "serum")
    w.b(-22, 22, -22, 22, -90, 62, "light", "barrel", alpha=0.45)
    for z in (-50, -15, 20):
        w.b(-8, 8, 22, 23.5, z, z + 4, "dark", "dose mark")
    w.b(-19, 19, -19, 19, -100, -90, "dark", "front cap")
    w.b(-19, 19, -19, 19, 62, 72, "dark", "back cap")
    w.b(-46, 46, -8, 8, 72, 84, "team", "finger flange")
    w.b(-10, 10, -10, 10, -112, -100, "dark", "hub")
    w.b(-2.5, 2.5, -2.5, 2.5, -198, -112, "mid", "needle")
    w.b(-1.5, 1.5, -1.5, 1.5, -201, -198, "glowhot", "needle tip")
    # moving: the plunger pushes in 3 cm on the shot and pulls back
    w.moving("plunger", (0, 0, 90))
    w.b(-6, 6, -6, 6, 84, 130, "mid", "plunger rod")
    w.b(-20, 20, -20, 20, 130, 136, "dark", "thumb pad")
    w.b(-16, 16, -16, 16, 136, 140, "team", "thumb pad top")
    w.still()
    return w


def locator():
    w = W("LOCATOR", "tracker: grip, slab body with a dark bezel and a dim glow radar screen with a sweeping line and a "
                     "team blip, emitter with a glow lens, whip antenna with a team tip that springs as you turn")
    w.b(-15, 15, -95, -24, 26, 66, "dark", "grip")
    w.sym(15, 16.5, -80, -40, 34, 58, "team", "grip strip")
    w.b(-42, 42, -24, 16, -100, 70, "mid", "body")
    w.b(-36, 36, -30, -24, -90, 60, "mid", "body chamfer")
    w.b(-36, 36, 16, 22, -78, 64, "dark", "bezel")
    w.b(-30, 30, 22, 23, -46, 54, "glowdim", "screen")
    w.b(8, 18, 23, 24.5, -28, -18, "team", "blip")
    w.b(-20, 20, -16, 10, -122, -100, "dark", "emitter")
    w.b(-12, 12, -10, 4, -125, -122, "glow", "emitter lens")
    # moving: the sweep line turns about the screen's centre, once every 2 s
    w.moving("sweep", (0, 24, 4))
    w.b(-1.5, 1.5, 23, 25, -24, 4, "glowhot", "sweep line")
    # moving: the antenna springs about X as you turn
    w.moving("antenna", (30, 16, 50))
    w.b(28, 32, 16, 104, 48, 52, "mid", "antenna mast")
    w.b(26, 34, 104, 112, 46, 54, "team", "antenna tip")
    w.still()
    return w


BUILDERS = {"RIFLE": rifle, "ROCKET": rocket, "KNIFE": knife, "SNIPER": sniper, "LASER": laser, "SHOTGUN": shotgun,
            "STICKY": sticky, "CROSSBOW": crossbow, "BUBBLE": bubble, "SYRINGE": syringe, "LOCATOR": locator}


# ----------------------------------------------------------------------------------------------------------------------

def colour(tone):
    slot, f = TONES[tone]
    return slot, tuple(round(c * f, 6) for c in REF[slot])


def write(w, path):
    import base64
    import struct
    pos, nrm, col, idx = [], [], [], []
    boxes = []
    by_mat = {}
    for b in w.boxes:
        slot, rgb = colour(b["tone"])
        lo = [v / 1000.0 for v in b["lo"]]
        hi = [v / 1000.0 for v in b["hi"]]
        c = [(lo[i] + hi[i]) / 2 for i in range(3)]
        h = [(hi[i] - lo[i]) / 2 for i in range(3)]
        e = {"c": [round(v, 6) for v in c], "h": [round(v, 6) for v in h], "slot": slot, "color": list(rgb),
             "shadow": True, "visOnly": False, "crouch": False, "name": b["name"]}
        if b["alpha"] < 1:
            e["alpha"] = b["alpha"]
        if b["part"]:
            e["part"] = b["part"]
            e["pivot"] = [round(v / 1000.0, 6) for v in b["pivot"]]
        boxes.append(e)
        by_mat.setdefault((slot + "_" + b["tone"], b["alpha"]), []).append((lo, hi, rgb))
    bin_ = bytearray()
    views, accs, prims, mats = [], [], [], []

    def put(data, target):
        off = len(bin_)
        bin_.extend(data)
        while len(bin_) % 4:
            bin_.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        return len(views) - 1

    faces = [((0, 1), (1, 0, 0)), ((0, -1), (-1, 0, 0)), ((1, 1), (0, 1, 0)), ((1, -1), (0, -1, 0)), ((2, 1), (0, 0, 1)), ((2, -1), (0, 0, -1))]
    tris = 0
    for mi, ((mname, alpha), items) in enumerate(by_mat.items()):
        P, N, C, F = [], [], [], []
        for lo, hi, rgb in items:
            for (ax, sgn), n in faces:
                u, v = [a for a in range(3) if a != ax]
                base = len(P)
                for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    p = [0, 0, 0]
                    p[ax] = hi[ax] if sgn > 0 else lo[ax]
                    p[u] = hi[u] if du else lo[u]
                    p[v] = hi[v] if dv else lo[v]
                    P.append(p); N.append(n); C.append(rgb)
                # winding: counter-clockwise seen from outside
                a, b2, c2, d = base, base + 1, base + 2, base + 3
                cross = [0, 0, 0]
                e1 = [P[b2][i] - P[a][i] for i in range(3)]
                e2 = [P[c2][i] - P[a][i] for i in range(3)]
                cross = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                if sum(cross[i] * n[i] for i in range(3)) > 0:
                    F += [a, b2, c2, a, c2, d]
                else:
                    F += [a, c2, b2, a, d, c2]
        tris += len(F) // 3
        pb = b"".join(struct.pack("<3f", *p) for p in P)
        nb = b"".join(struct.pack("<3f", *n) for n in N)
        cb = b"".join(struct.pack("<3f", *c) for c in C)
        fb = b"".join(struct.pack("<I", i) for i in F)
        a0 = len(accs)
        accs.append({"bufferView": put(pb, 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                     "min": [min(p[i] for p in P) for i in range(3)], "max": [max(p[i] for p in P) for i in range(3)]})
        accs.append({"bufferView": put(nb, 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
        accs.append({"bufferView": put(cb, 34962), "componentType": 5126, "count": len(C), "type": "VEC3"})
        accs.append({"bufferView": put(fb, 34963), "componentType": 5125, "count": len(F), "type": "SCALAR"})
        prims.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "COLOR_0": a0 + 2}, "indices": a0 + 3, "material": mi, "mode": 4})
        m = {"name": mname, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, alpha], "metallicFactor": 0, "roughnessFactor": 1}}
        if alpha < 1:
            m["alphaMode"] = "BLEND"
        mats.append(m)
    doc = {
        "asset": {"version": "2.0", "generator": "rifle-styles gen_outlined.py"},
        "extras": {"style": "outlined arena", "weapon": w.name, "description": w.desc, "provenance": "AI-made placeholder"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": w.name, "mesh": 0}],
        "meshes": [{"name": w.name, "primitives": prims, "extras": {"quark": {"version": 2, "boxes": boxes}}}],
        "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(bin_), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(bin_)).decode("ascii")}],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(json.dumps(doc, separators=(",", ":")) + "\n")
    return len(boxes), tris


def check(w):
    errs = []
    mx, my, mz = MUZZLE[w.name]
    front = min(b["lo"][2] for b in w.boxes)
    if abs(front - mz) > 13:
        errs.append("front at z %.0f, the muzzle is at %.0f" % (front, mz))
    if len(w.boxes) > 256:
        errs.append("%d boxes" % len(w.boxes))
    parts = {b["part"] for b in w.boxes if b["part"]}
    return errs, parts


# ----------------------------------------------------------------------------------------------------------------------
# in-game shots

WEAPON_ARG = {n: i + 1 for i, n in enumerate(ORDER)}
CROP = (660, 300, 1280, 720)


def game_running():
    r = subprocess.run(["pgrep", "-f", "build/quark "], capture_output=True, text=True)
    return [l for l in r.stdout.split() if l.strip() and int(l) != os.getpid()]


def ensure_xvfb():
    r = subprocess.run(["pgrep", "-f", "Xvfb :91"], capture_output=True, text=True)
    if not r.stdout.strip():
        subprocess.Popen(["Xvfb", ":91", "-screen", "0", "1920x1080x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)


def shoot(datadir, name, mapname, out, log, cam):
    for _ in range(90):
        if not game_running():
            break
        time.sleep(1)
    env = dict(os.environ, DISPLAY=":91")
    mode = "tag" if name == "LOCATOR" else "ffa"
    cmd = [GAME, "--data-dir", datadir, "--host", mode, "--map", mapname, "--weapon", str(WEAPON_ARG[name]), "--screenshot", out]
    if cam:
        cmd += ["--cam"] + cam.split()
    with open(log, "w") as f:
        subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=150)


def sheet(shots, path, title):
    from PIL import Image, ImageDraw, ImageFont
    cw, ch = CROP[2] - CROP[0], CROP[3] - CROP[1]
    cols, pad, top = 4, 8, 56
    rows = (len(ORDER) + cols - 1) // cols
    img = Image.new("RGB", (cols * (cw + pad) + pad, top + rows * (ch + pad + 26)), (22, 24, 28))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(os.path.join(REPO, "assets", "fonts", "Rubik.ttf"), 22)
        small = ImageFont.truetype(os.path.join(REPO, "assets", "fonts", "Rubik.ttf"), 17)
    except Exception:
        font = small = ImageFont.load_default()
    d.rectangle((0, 0, img.width, 4), fill=(58, 212, 255))
    d.text((pad + 4, 16), title, fill=(242, 244, 247), font=font)
    for i, n in enumerate(ORDER):
        x, y = pad + (i % cols) * (cw + pad), top + (i // cols) * (ch + pad + 26)
        d.text((x + 4, y), n, fill=(58, 212, 255), font=small)
        p = shots.get(n)
        if p and os.path.exists(p):
            img.paste(Image.open(p).convert("RGB").crop(CROP), (x, y + 24))
    img.save(path, optimize=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "assets", "overrides"))
    ap.add_argument("--scratch", default="/tmp/quark-outlined")
    ap.add_argument("--ingame", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--builtin", action="store_true", help="also shoot the built-in models for a side-by-side")
    ap.add_argument("--map", default="qk3")
    ap.add_argument("--cam", default="-6 1.6 -15 158 -5")
    args = ap.parse_args()
    only = [s.strip().upper() for s in args.only.split(",") if s.strip()]
    ok = True
    wdir = os.path.join(args.out, "models", "weapon")
    for n in ORDER:
        if only and n not in only:
            continue
        w = BUILDERS[n]()
        errs, parts = check(w)
        nb, tris = write(w, os.path.join(wdir, n + ".gltf"))
        print("%-8s %3d boxes %5d tris  parts: %s" % (n, nb, tris, ", ".join(sorted(parts)) or "-"))
        for e in errs:
            print("   ERROR: " + e)
            ok = False
    if args.ingame:
        ensure_xvfb()
        data = os.path.join(args.scratch, "data")
        ov = os.path.join(data, "overrides", "models", "weapon")
        shutil.rmtree(ov, ignore_errors=True)
        os.makedirs(ov)
        for n in ORDER:
            shutil.copyfile(os.path.join(wdir, n + ".gltf"), os.path.join(ov, n + ".gltf"))
        shots, built = {}, {}
        for n in ORDER:
            if only and n not in only:
                continue
            out = os.path.join(args.scratch, "ingame-%s.png" % n)
            if os.path.exists(out):
                os.remove(out)
            log = os.path.join(args.scratch, "log-%s.txt" % n)
            shoot(data, n, args.map, out, log, args.cam)
            text = open(log).read()
            if "override: models/weapon/%s.gltf" % n.lower() not in text:
                print("   ERROR: %s override not loaded" % n)
                ok = False
            for l in text.splitlines():
                if "override" in l.lower() and any(k in l.lower() for k in ("bad", "fail", "error", "box model:")):
                    print("   GAME: " + l)
                    ok = False
            if os.path.exists(out):
                os.makedirs(os.path.join(args.scratch, "ingame"), exist_ok=True)
                shutil.copyfile(out, os.path.join(args.scratch, "ingame", n + ".png"))
                shots[n] = out
            if args.builtin:
                bd = os.path.join(args.scratch, "builtin")
                os.makedirs(os.path.join(bd, "data"), exist_ok=True)
                bo = os.path.join(bd, "ingame-%s.png" % n)
                shoot(os.path.join(bd, "data"), n, args.map, bo, os.path.join(bd, "log-%s.txt" % n), args.cam)
                built[n] = bo
        if not only:
            sheet(shots, os.path.join(args.scratch, "outlined-weapons.png"), "OUTLINED ARENA: all 11 weapons in game on %s (box models, the game's outlines)" % args.map)
            if built:
                sheet(built, os.path.join(args.scratch, "builtin-weapons.png"), "BUILT-IN today, same view")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
