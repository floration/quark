"""GANTRY: ranked INSTAGIB. The crane hall: catwalks along both walls, a bridge across the middle over the
open loading bay (no floor: fall and you're out), two overhead cranes with their cabs and a hanging
container each. Long sightlines on every level, always a way to break line of sight. Point symmetric."""
from .prefabs import *


def build():
    m = M("Gantry", "ffa", sun=(0, 1, 0))   # (ranked instagib is a free for all; ranked: the sun overhead, MAP-17)
    m.raw("ranked instagib")
    # (1.13: rebuilt 1.5x longer, 2x wider and 2x taller: 96 x 48 m and 28 m to the roof line; two catwalk tiers,
    # cranes 18 m up, and a container yard in the middle so nobody has a shot from spawn to spawn)
    GX, GZ, GH = 48, 24, 28
    m.room(GX, GZ, GH, pits=[(-5, -14, 5, 14, "water")], sunken=[(-34, -12, -27, 12, 3.0, "metal"), (27, -12, 34, 12, 3.0, "metal")],
           style="hall", skyline="cranes")   # the flooded dock under the bridges; lorry docks sunk at each end
    m.lines = [l for l in m.lines if not l.startswith("void ")]; m.raw("void -4.2")   # (the lorry docks' floor at -3 is solid ground:
                                                                                       #  only the flooded dock kills, under its water)
    m.metal(-5, -0.3, -14.1, 5, 0, -14); m.metal(-5, -0.3, 14, 5, 0, 14.1)
    m.box(-5.1, 0, -14.1, -5, 0.9, 14.1, accent=True, mirror=True)              # bay railings
    m.metal(-5, -0.3, -1.5, 5, 0, 1.5)                                            # a gangway across the dock at floor level
    for x in (-4, -1, 2): m.box(x, 0, -1.5, x + 0.15, 0.9, -1.35, accent=True, mirror=True)   # its rail posts
    # catwalks: the low tier (4 m) the length of both long walls, the high tier (10 m) over its middle
    m.catwalk(-42, -23.8, 42, -21.4, 4.0, rails="z", mirror=True)
    m.stairs(42, -23.8, 46, -21.4, 4.0, "x", -1, mirror=True); m.stairs(-46, -23.8, -42, -21.4, 4.0, "x", 1, mirror=True)
    m.catwalk(-30, -23.8, 30, -21.4, 10.0, rails="z", mirror=True)
    m.box(30, 4.0, -23.8, 31, 10.0, -22.6, accent=True, mirror=True)          # low tier -> high tier: a ladder up a pier at each end
    m.ladder(31, -23.2, 4.0, 10.0, "w", width=0.8); m.ladder(-31, 23.2, 4.0, 10.0, "e", width=0.8)
    # bridges across the hall: the low one over the flooded dock, two high ones between the high tiers
    m.catwalk(-1.6, -21.4, 1.6, 21.4, 4.0, rails="x")
    m.catwalk(-15.6, -21.4, -12.4, 21.4, 10.0, rails="x", mirror=True)
    # the container yard in the middle of each half: stacks you fight around, one high enough to stand on
    for (x0, z0, x1, z1, h) in ((-12, -10, -6, -7.6, 2.6), (-12, 6, -6, 8.4, 2.6), (-20, -3, -14, -0.6, 2.6), (-24, 9, -18, 11.4, 2.6),
                                (-26, -13, -23.6, -7, 2.6)):
        m.container(x0, z0, x1, z1, h, mirror=True)
    m.container(-20, -3, -14, -0.6, 2.6, mirror=True); m.box(-19.5, 2.6, -2.8, -14.5, 5.2, -0.8, mat="metal", mirror=True)   # a stack of two
    m.stairs(-14, -3, -11, -0.6, 2.6, "x", -1, mirror=True)                     # up the side of the stack
    for (x0, z0, x1, z1, h) in ((-9, -2, -8, 2, 1.4), (-31, -17, -29, -15, 1.4), (-22, 15, -20, 17, 2.2), (-40, -6, -38, -4, 1.4)):
        m.box(x0, 0, z0, x1, h, z1, mirror=True)
    m.wallbang(-10, 0, -0.2, -6, 2.6, 0.2, mirror=True)
    m.wallbang(-30, 4.0, -21.4, -29.8, 6.4, -19.8, mirror=True)                 # screens on the low tier
    for x in (-34, -20, -8): m.pillar(x, -19, GH, r=0.6, mirror=True)
    # cranes: rails along both walls 18 m up, a crane bridge across the hall at each end of the yard, cabs and
    # hanging containers; a ladder up each crane's leg from the high tier (the perch over the whole hall, a long fall)
    m.box(-GX, 18, -GZ, GX, 18.6, -22.6, accent=True, mirror=True)
    for cx in (-21,):
        m.box(cx - 1, 18.4, -22.6, cx + 1, 19.0, 22.6, accent=True, mirror=True)
        m.metal(cx + 1.0, 10.0, -23.4, cx + 1.8, 19.0, -22.6, mirror=True)   # the crane's leg, on the high tier against the wall
        m.ladder(cx + 1.4, -22.6, 10.0, 19.0, "n", width=0.8); m.ladder(-(cx + 1.4), 22.6, 10.0, 19.0, "s", width=0.8)   # up onto the crane
        m.box(cx - 1.2, 15.4, 3, cx + 1.2, 18.4, 6, mirror=True); m.wallbang(cx - 1.25, 16.2, 3.4, cx - 1.2, 17.6, 5.6, mirror=True)  # the cab
        m.glow(cx - 1.3, 15.4, 3, cx + 1.3, 15.5, 6, mirror=True)
        for (dx, dz) in ((-1.2, -2.6), (1.2, -2.6), (-1.2, 2.6), (1.2, 2.6)):   # the hanging container's four slings, up to the hook block
            m.metal(cx + dx * 0.15 - 0.05, 14.5, dz * 0.15 - 0.05, cx + dx * 0.15 + 0.05, 16.4, dz * 0.15 + 0.05, mirror=True)
        m.metal(cx - 0.45, 16.4, -0.45, cx + 0.45, 17.0, 0.45, mirror=True); m.metal(cx - 0.08, 17.0, -0.08, cx + 0.08, 18.4, 0.08, mirror=True)
        m.metal(cx - 1.55, 14.5, -3.05, cx + 1.55, 14.65, 3.05, mirror=True)   # spreader beam on the lid
        m.metal(cx - 1.5, 12.5, -3, cx + 1.5, 14.5, 3, mirror=True); m.box(cx - 1.55, 13.3, -3.05, cx + 1.55, 13.5, 3.05, accent=True, mirror=True)
    for x in (-36, -24, -12):
        m.light(x, 3.4, -22.5, 9, 0.5, fixture=False, mirror=True); m.light(x, 9.4, -22.5, 9, 0.5, fixture=False, mirror=True)
    for x in (-18, -6): m.light(x, 6, 0, 14, 0.6, fixture=False, mirror=True)
    m.light(0, -1.5, 0, 12, 0.5, fixture=False)
    m.light(-21, 17, 0, 12, 0.6, fixture=False, mirror=True)
    # the loading sheds at both ends: roofed bays under the low tier's ends where each player starts (dim and
    # covered, the bright open hall between)
    m.box(-GX, 9.6, -21.4, -38, 10.0, 21.4, accent=True, mirror=True)
    m.glow(-38.1, 9.4, -21.4, -37.9, 9.6, 21.4, mirror=True)
    for z in (-12, 0, 12):
        m.light(-43, 8.6, z, 9, 0.6, mirror=True); m.metal(-43.04, 8.95, z - 0.04, -42.96, 9.6, z + 0.04, mirror=True)   # lamps hung off the shed roof
    m.box(-38, 0, -21.4, -37.6, 7.0, -4, mirror=True); m.box(-38, 0, 4, -37.6, 7.0, 21.4, mirror=True)   # shed fronts, a doorway in the middle
    m.box(-38.05, 3.0, -4, -37.55, 7.0, 4, mirror=True)                                                       # (over the doorway)
    # the lorry docks: sunk 3 m beside each loading shed, steps down at both ends
    # (4 m of solid floor between each shed's doorway and the dock's edge: you never step out of spawn into a drop)
    m.stairs(-34, -12, -27, -6, 0, "z", -1, mirror=True, base=-3.0); m.stairs(-34, 6, -27, 12, 0, "z", 1, mirror=True, base=-3.0)
    for (x0, z0, x1, z1, y) in ((-33.6, 0, -32.45, 1.15, -3), (-32.4, 0, -31.25, 1.15, -3), (-33.1, 0.1, -31.95, 1.05, -1.85)):
        m.box(x0, y, z0, x1, y + 1.15, z1, accent=(y > -2), mirror=True)          # crates on the dock floor
    m.glow(-34.05, -0.06, -6, -33.95, 0, 6, mirror=True); m.glow(-27.05, -0.06, -6, -26.95, 0, 6, mirror=True)   # dock edge strips
    m.box(-33.9, -3, -5.5, -33.5, -1.6, -5.1, accent=True, mirror=True); m.box(-33.9, -3, 5.1, -33.5, -1.6, 5.5, accent=True, mirror=True)  # buffers
    m.light(-30.5, -1.0, -3, 8, 0.6, fixture=False, mirror=True)
    for (x, y, z, yaw) in ((-44, 0, -12, -90), (-44, 0, 12, -90), (-29.5, -3, 0, -90), (-41, 4, -22.6, -90), (-29, -3, -4, -90), (-9, 0, -12.5, 0), (-26, 0, 16, 180)):
        m.spawn("any", x, y, z, yaw, "any")   # free for all: 14 spots, sheds, docks, both tiers and the yard (each point-mirrored)
    # dressing (kept to the walls, the sheds and under the tiers: the lanes stay clear for instagib)
    for (y, d) in ((1.0, 0.0), (1.6, 0.0)):   # pipes along both long walls under the low tier, brackets every 6 m
        m.metal(-40, y, -23.98, 40, y + 0.3, -23.68, mirror=True)
    for x in range(-36, 37, 12):
        m.box(x - 0.1, 0, -24, x + 0.1, 2.0, -23.6, accent=True, mirror=True)
        m.metal(x + 2.9, 0, -24, x + 3.3, GH - 2, -23.6, mirror=True)               # downpipes to the roof
    for x in (-24, 0, 24):   # glow signage on the long walls between the tiers and on the end walls over the sheds
        m.glow(x - 4, 7.0, -24, x + 4, 7.3, -23.92, mirror=True)
    m.glow(-48, 12.0, -6, -47.92, 12.5, 6, mirror=True); m.box(-48, 11.6, -6.4, -47.8, 11.8, 6.4, accent=True, mirror=True)
    def spool(x, z, r=0.9, w=0.8):   # a cable drum standing on the floor: two flanges and a wound core
        m.box(x - r, 0, z - w / 2, x + r, 2 * r, z - w / 2 + 0.12, accent=True, mirror=True)
        m.box(x - r, 0, z + w / 2 - 0.12, x + r, 2 * r, z + w / 2, accent=True, mirror=True)
        m.metal(x - r + 0.25, 0.25, z - w / 2 + 0.12, x + r - 0.25, 2 * r - 0.25, z + w / 2 - 0.12, mirror=True)
    spool(-45.5, -19.0); spool(-45.5, 18.6); spool(-17, -20.5, 0.7, 0.7)
    m.crates(-46.2, -14.6, 2, size=1.1, mirror=True); m.crates(-12.5, 18.8, 2, size=1.1, mirror=True)
    for x in (-40, -36):   # bollards along the dock road
        m.box(x - 0.2, 0, -21.0, x + 0.2, 0.9, -20.6, accent=True, mirror=True); m.glow(x - 0.22, 0.7, -21.02, x + 0.22, 0.78, -20.58, mirror=True)
    m.glow(-5.1, -0.05, -14, -4.9, 0.01, 14, mirror=True)   # hazard strip at the flooded dock's edge
    m.raw("setting crane hall")
    m.save("gantry")
