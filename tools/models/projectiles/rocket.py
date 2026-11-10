"""ROCKET: the ROCKET launcher's rocket, in the launcher's style (octagonal sections, team bands, dark collars).

A team-coloured pointed nose with a glowing seeker eye at the tip, a team band, a steel octagonal body with dark panel
rings, a tapered tail, four swept fins and a flared nozzle whose mouth is open and dark at z +285: the game draws the
flickering flame (rocketFlameParts, models.cpp) centred at z +330..+360, right behind it.
"""
from gen_weapons_lowpoly import Model
from lowpoly import loft, ngon, solid

NAME = "ROCKET"


def build():
    m = Model(NAME, "rocket: team nose cone with a glowing seeker eye, team band, steel octagonal body with dark panel "
                    "rings and side glow windows, tapered tail, four swept fins, flared nozzle with a dark open mouth (the game adds the flame)")
    B = m.body
    o = lambda r: ngon(0, 0, r, 8)
    # nose: two tapers to a point, the seeker eye glowing at the tip
    loft(B, "team_wood", o(60), -150, o(36), -232)
    loft(B, "team_hot", o(36), -232, o(13), -268)
    loft(B, "glow_hot", o(13), -268, o(3), -282, shell=False)
    solid(B, "metal_dark", o(63), "z", -156, -148)            # collar where the nose meets the body
    # body with a team band and panel rings
    solid(B, "metal_steel", o(60), "z", -148, 196)
    solid(B, "team_wood", o(65), "z", -104, -60)
    for z in (-10, 90):
        solid(B, "metal_dark", o(61.5), "z", z, z + 5, shell=False)
    for lo, hi in ((55.3, 56.4), (-56.4, -55.3)):                 # a glow window on each side, like the launcher's
        solid(B, "glow", [(8, -9), (80, -9), (80, 9), (8, 9)], "x", lo, hi, shell=False)
    # tapered tail and the flared nozzle
    loft(B, "metal_mid", o(60), 196, o(42), 246)
    loft(B, "metal_dark", o(38), 246, o(50), 285)
    solid(B, "metal_bore", o(42), "z", 284.4, 285.2, shell=False)   # the open mouth, dark
    # four swept fins (a side profile across x for the up/down pair, a top-down outline for the left/right pair)
    for s in (1, -1):
        fin = [(140, 56 * s), (262, 56 * s), (282, 140 * s), (238, 140 * s)]
        solid(B, "metal_mid", fin, "x", -5, 5)
        solid(B, "metal_mid", [(y, z) for z, y in fin], "y", -5, 5)
    return m
