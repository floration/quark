"""DOWNTOWN: king of the hill in a city block. The hill is the plaza at the crossroads: a statue on a plinth in a
fountain basin, benches and planters to hold it from. Four avenues lead in; the blocks between them are an
office building (two floors and a roof up a fire escape) and a row of cafes with a roof terrace up the side
stairs; an underpass runs under the plaza with a hatch ladder up into it. Parked cars, bus shelters, a metro
entrance and lamp posts on the streets; lit windows on every facade. Point symmetric: A from the south, B north."""
from .prefabs import *


def build():
    m = M("Downtown", "koth", sun=(0.35, 1.0, 0.5))
    m.room(24, 24, 14, sunken=[(-19, -1.5, 19, 1.5, 3.5, "accent")], style="factory", skyline="city")
    # the plaza (the hill): paved and raised a step, the statue in the fountain, benches and planters round it
    for (a0, b0, a1, b1) in minus((-7, -7, 7, 7), [(3.3, 0.3, 4.7, 1.5), (-4.7, -1.5, -3.3, -0.3)]): m.box(a0, 0, b0, a1, 0.5, b1, accent=True)
    m.raw("hill -6 0.5 -6 6 3.5 6")
    m.box(-1.2, 0.5, -1.2, 1.2, 2.5, 1.2); m.box(-1.4, 2.5, -1.4, 1.4, 2.7, 1.4, accent=True)            # plinth
    m.box(-0.45, 2.7, -0.35, 0.45, 5.6, 0.35, mat="metal"); m.box(-0.2, 5.6, -0.2, 0.2, 6.1, 0.2, mat="metal")  # the statue
    m.box(0.45, 4.6, -0.12, 1.4, 4.85, 0.12, mat="metal"); m.glow(1.3, 4.85, -0.15, 1.5, 5.4, 0.15)          # raised arm, the torch
    for (a0, b0, a1, b1) in ((-3.4, -3.4, -1.2, -3.0), (-3.4, -3.0, -3.0, -1.2)):                          # fountain basin: four corners,
        m.box(a0, 0.5, b0, a1, 1.3, b1, mirror=True); m.box(-a1, 0.5, b0, -a0, 1.3, b1, mirror=True)       # open on the four ways in
    m.box(-3.0, 0.5, -3.0, 3.0, 0.62, 3.0, mat="water")
    for (x, z) in ((-5.6, -5.6), (5.6, -5.6)): m.box(x - 0.6, 0.5, z - 0.6, x + 0.6, 1.4, z + 0.6, accent=True, mirror=True)   # planters
    for (x0, z0, x1, z1) in ((-5.2, -2.5, -4.6, 0.5), (4.6, -2.5, 5.2, 0.5)): m.box(x0, 0.5, z0, x1, 1.0, z1, mirror=True)       # benches
    # the underpass beneath the plaza: tiled, lit, a hatch ladder up into the plaza at each end of the fountain
    for (a0, b0, a1, b1) in minus((-13, -1.5, 13, 1.5), [(3.3, 0.3, 4.7, 1.5), (-4.7, -1.5, -3.3, -0.3)]): m.box(a0, -0.3, b0, a1, 0, b1)
    m.ladder(4, 1.5, -3.5, 0.5, "s", width=0.8); m.ladder(-4, -1.5, -3.5, 0.5, "n", width=0.8)
    for x in range(-12, 13, 4): m.light(x, -0.9, 0, 5, 0.7, fixture=False); m.glow(x - 0.6, -0.36, -0.3, x + 0.6, -0.3, 0.3)
    for x in range(-11, 12, 6): m.glow(x - 1.0, -2.2, 1.44, x + 1.0, -1.0, 1.5, mirror=True)               # posters on the tiles
    for (a0, a1) in ((-19, -4.8), (-3.2, 19)): m.box(a0, -3.5, -1.5, a1, -3.4, -1.2, accent=True, mirror=True)   # kerb (not under the hatch ladders)
    # metro entrances at both ends of the underpass: stairs down, railings round the opening, a canopy over them
    m.stairs(13, -1.5, 19, 1.5, 0, "x", 1, mirror=True, base=-3.5)
    m.box(13, 0, -1.6, 19, 1.0, -1.5, accent=True, mirror=True); m.box(13, 0, 1.5, 19, 1.0, 1.6, accent=True, mirror=True)
    m.box(12.9, 0, -1.6, 13, 1.0, 1.6, accent=True, mirror=True)
    for (x, z) in ((13.1, -1.5), (13.1, 1.4), (18.9, -1.5), (18.9, 1.4)): m.metal(x - 0.1, 0, z - 0.05, x + 0.1, 3.2, z + 0.15, mirror=True)
    m.box(12.6, 3.2, -2.0, 19.4, 3.45, 2.0, accent=True, mirror=True); m.glow(15, 3.0, -0.3, 17, 3.2, 0.3, mirror=True)
    # NE block: the office building. Ground floor lobby (doors to the avenue and the side street), stairs inside to the
    # first floor (windows over the plaza), a fire escape ladder up the outside to the roof (the highest perch)
    m.building(9, -20, 17, -9, 4.2, [("w", -14, 1.8), ("s", 12.5, 1.8)], windows=True, mirror=True, notch=[(14.8, -19.8, 16.8, -14.2), (16.95, -12.75, 17.45, -11.25)])
    m.stairs(15, -19.4, 16.6, -12.6, 4.55, "z", -1, mirror=True)
    m.building(9, -20, 17, -9, 3.6, [], windows=True, mirror=True, base=4.55, notch=(16.95, -12.75, 17.45, -11.25))
    for (a0, b0, a1, b1) in ((8.7, -20.3, 17.3, -20), (8.7, -9, 17.3, -8.7), (8.7, -20, 9, -9)): m.box(a0, 8.5, b0, a1, 9.1, b1, accent=True, mirror=True)   # parapet
    m.box(17, 8.5, -20, 17.3, 9.1, -12.8, accent=True, mirror=True); m.box(17, 8.5, -11.2, 17.3, 9.1, -9, accent=True, mirror=True)
    m.ladder(17, -12, 0, 8.5, "w", width=0.8)
    m.ladder(-17, 12, 0, 8.5, "e", width=0.8)
    for (x0, z0, x1, z1) in ((10, -18.5, 12.5, -17.5), (10, -11.5, 12.5, -10.5)): m.box(x0, 4.55, z0, x1, 5.3, z1, mirror=True)   # desks upstairs
    m.glow(8.95, 1.0, -18, 9.0, 3.4, -16, mirror=True)                                                     # lobby sign
    # NE corner: a car park behind the offices (cars to hide behind, a dumpster in the alley)
    for (x, z) in ((20.5, -20), (20.5, -14.5), (20.5, -9)):
        m.box(x - 1.0, 0.3, z - 2.0, x + 1.0, 1.1, z + 2.0, accent=True, mirror=True); m.box(x - 0.85, 1.1, z - 1.0, x + 0.85, 1.6, z + 1.0, mirror=True)
    m.metal(18, 0, -23.5, 20, 1.4, -22.3, mirror=True)
    # NW block: a row of cafes with an awning over the pavement, side stairs up to the roof terrace (tables and planters
    # up there: a lower perch over the north avenue)
    m.building(-20, -14, -9, -9, 4.0, [("e", -11.5, 1.8), ("s", -17, 1.8), ("s", -12, 1.8)], windows=True, mirror=True)
    m.box(-20, 3.2, -9, -9, 3.4, -7.4, accent=True, mirror=True)
    for x in (-19.8, -14.5, -9.2): m.metal(x - 0.1, 0, -7.6, x + 0.1, 3.2, -7.4, mirror=True)
    m.stairs(-22.2, -14.2, -20.4, -8, 4.35, "z", -1, mirror=True)
    for (x, z) in ((-17, -12.5), (-13, -11)): m.box(x - 0.5, 4.35, z - 0.5, x + 0.5, 5.1, z + 0.5, mirror=True)
    for (a0, b0, a1, b1) in ((-20.3, -14.3, -8.7, -14), (-9, -14, -8.7, -8.7), (-20.3, -9, -9, -8.7)): m.box(a0, 4.35, b0, a1, 4.9, b1, accent=True, mirror=True)
    m.box(-8.7, 4.35, -11, -8.4, 4.36, -10, mat="trim", mirror=True)
    for (x, z) in ((-18, -11.5), (-12, -11.5)): m.light(x, 3.2, z, 6, 0.7, mirror=True)
    # behind the cafes: the back alley (bins, crates, a lamp)
    for (x, z) in ((-19, -18), (-14, -21), (-10, -17.5)): m.box(x - 0.8, 0, z - 0.6, x + 0.8, 1.3, z + 0.6, mirror=True)
    m.metal(-22.5, 0, -22.5, -21, 1.4, -20, mirror=True)
    # street furniture: parked cars along the avenues, bus shelters (glass backs: wallbang), lamp posts, traffic lights
    for (x, z, ax) in ((-2.8, -12, "z"), (2.8, -15, "z"), (-10, 3.2, "x"), (-21, -3.0, "x")):
        if ax == "z": m.box(x - 1.0, 0.3, z - 2.0, x + 1.0, 1.1, z + 2.0, accent=True, mirror=True); m.box(x - 0.85, 1.1, z - 1.0, x + 0.85, 1.6, z + 1.0, mirror=True)
        else: m.box(x - 2.0, 0.3, z - 1.0, x + 2.0, 1.1, z + 1.0, accent=True, mirror=True); m.box(x - 1.0, 1.1, z - 0.85, x + 1.0, 1.6, z + 0.85, mirror=True)
    for (x, z) in ((5.5, -12), (-12, 5.5)):
        m.box(x - 0.6, 2.5, z - 1.8, x + 0.6, 2.7, z + 1.8, accent=True, mirror=True) if abs(z) > abs(x) else m.box(x - 1.8, 2.5, z - 0.6, x + 1.8, 2.7, z + 0.6, accent=True, mirror=True)
        if abs(z) > abs(x): m.wallbang(x + 0.4, 0.2, z - 1.7, x + 0.5, 2.5, z + 1.7, mirror=True)
        else: m.wallbang(x - 1.7, 0.2, z - 0.5, x + 1.7, 2.5, z - 0.4, mirror=True)
    for (x, z) in ((-5, -10), (5, -16), (-5, -22), (-10, 5), (-16, -5), (-22, 5), (10, -5), (22, -5)):
        m.metal(x - 0.1, 0, z - 0.1, x + 0.1, 4.2, z + 0.1, mirror=True); m.light(x, 4.0, z, 8, 0.7, mirror=True)
    for (x, z) in ((-7.6, -7.6), (7.6, -7.6)):
        m.metal(x - 0.12, 0, z - 0.12, x + 0.12, 3.4, z + 0.12, mirror=True); m.glow(x - 0.2, 2.6, z - 0.2, x + 0.2, 3.4, z + 0.2, mirror=True)
    # the facades all round: rows of lit windows, shop signs at street level, a billboard on each end wall
    for y in (3.5, 6.5, 9.5):
        for t in range(-22, 23, 3):
            m.glow(t - 0.5, y, -23.98, t + 0.5, y + 1.3, -23.9, mirror=True); m.glow(23.9, y, t - 0.5, 23.98, y + 1.3, t + 0.5, mirror=True)
    for t in range(-20, 21, 8):
        m.box(t - 2.5, 2.4, -23.9, t + 2.5, 2.9, -23.6, accent=True, mirror=True); m.box(23.6, 2.4, t - 2.5, 23.9, 2.9, t + 2.5, accent=True, mirror=True)
    m.glow(-6, 11, -23.95, 6, 13, -23.85, mirror=True)
    for x in (-6, -3, 0, 3): m.spawn("a", x, 0, 22, 0, "b")   # (four a side: a full 8-player server, MAP-2)
    m.wallbang(-6.6, 0.2, 19.4, 3.6, 2.5, 19.5, mirror=True); m.box(-6.7, 2.5, 19.1, 3.7, 2.7, 20.3, accent=True, mirror=True)   # bus shelters: no sightline spawn to spawn
    for (x, z, yaw) in ((-22, 10, -90), (10, 22, 0), (-22, -16, -90)): m.spawn("any", x, 0, z, yaw, "any")
    m.raw("setting city block")
    m.save("downtown")
