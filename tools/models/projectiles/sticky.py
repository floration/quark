"""STICKY: the STICKY launcher's spiked mine, in the launcher's style (octagonal sections, dark metal, team chambers).

Three dark octagonal discs crossed at right angles make a faceted shell about 100 mm round; a team-coloured cube core
fills it and shows its eight corners through the gaps between the discs (the only team colour: the game blinks the
team tint when the mine is stuck). Six steel spikes on dark collars point along the axes to 170 mm, and a small glow
lens sits on one disc face. Symmetric about the origin, so it reads the same spinning in flight and stuck at any angle.
"""
import math

from gen_weapons_lowpoly import Model
from lowpoly import loft, ngon, rotx, roty, rotz, solid

NAME = "STICKY"

R_DISC = 100     # the discs' corner radius (the old shell's half size)
T_DISC = 30      # the discs' half thickness
CORE = 64        # the core cube's half size: its corners poke out between the discs
SPIKE = 170      # spike tip distance


def build():
    m = Model(NAME, "sticky mine: three crossed dark octagonal discs over a team core cube whose corners glow through "
                    "the gaps (the blinking part), six steel spikes on dark collars along the axes, a small glow lens")
    B = m.body
    # the team core: a cube, its eight corners showing between the discs
    solid(B, "team_hot", [(-CORE, -CORE), (CORE, -CORE), (CORE, CORE), (-CORE, CORE)], "z", -CORE, CORE)
    # the shell: three crossed octagonal discs
    oct_ = ngon(0, 0, R_DISC, 8)
    for axis in ("x", "y", "z"):
        solid(B, "metal_dark", oct_, axis, -T_DISC, T_DISC)
    # six spikes: a dark hex collar and a steel cone, built along -z and turned onto each axis
    turns = [None, roty(math.pi), roty(math.pi / 2), roty(-math.pi / 2), rotx(math.pi / 2), rotx(-math.pi / 2)]
    for R in turns:
        if R:
            B.place(R)
        solid(B, "metal_mid", ngon(0, 0, 17, 6, phase=0), "z", -116, -84, shell=False)
        loft(B, "metal_steel", ngon(0, 0, 13, 6, phase=0), -112, ngon(0, 0, 2, 6, phase=0), -SPIKE)
        B.unplace()
    # the glow lens on the z disc's rim, between the +x and +y spikes
    B.place(rotz(math.pi / 4))
    a = R_DISC * math.cos(math.pi / 8)
    solid(B, "glow_hot", ngon(0, 0, 12, 6), "x", a - 2, a + 4)
    B.unplace()
    return m
