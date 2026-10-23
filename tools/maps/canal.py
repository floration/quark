"""CANAL: neutral payload down the middle causeway; canals full of water either side (fall in and you're
out), crossed by three bridges each, with the lock gates as the centrepiece at both ends and canal-side
houses on the banks."""
from .prefabs import *


def build():
    m = M("Canal", "npayload")
    water = []
    for sx in (-1, 1):
        a, b = (9, 14) if sx > 0 else (-14, -9)
        for (z0, z1) in ((-44, -21.5), (-18.5, -1.5), (1.5, 18.5), (21.5, 44)): water.append((a, z0, b, z1, "water"))
    m.room(22, 42, 10, pits=water, style="yard", skyline="city")
    for z in (36, 24, 12, 0, -12, -24, -36): m.raw(f"path 0 0 {z:g}")
    m.track([(0, 36), (0, -36)])
    m.signal_gantry(0, 22)
    for (z0, z1) in ((-21.5, -18.5), (-1.5, 1.5), (18.5, 21.5)):              # bridge railings
        for (a, b) in ((9, 14), (-14, -9)):
            m.box(a, 0, z0 - 0.1, b, 1.0, z0, accent=True); m.box(a, 0, z1, b, 1.0, z1 + 0.1, accent=True)
    for side in (-1, 1):                                                       # lock gates: posts, a glowing beam, gate leaves
        z = side * 31
        for (a, b) in ((8.5, 9), (14, 14.5), (-9, -8.5), (-14.5, -14)):
            m.metal(a, -3.6, z - 0.6, b, 6.5, z + 0.6)
        m.box(-14.5, 6.5, z - 0.7, -8.5, 7.2, z + 0.7, accent=True); m.box(8.5, 6.5, z - 0.7, 14.5, 7.2, z + 0.7, accent=True)
        m.glow(-14.5, 6.3, z - 0.75, -8.5, 6.5, z + 0.75); m.glow(8.5, 6.3, z - 0.75, 14.5, 6.5, z + 0.75)
        m.metal(-14, -3.6, z - 0.4, -9, 0.2, z + 0.4); m.metal(9, -3.6, z - 0.4, 14, 0.2, z + 0.4)   # gates: a narrow walkway
        m.light(-11.5, 5.5, z, 7, 0.5, fixture=False); m.light(11.5, 5.5, z, 7, 0.5, fixture=False)
    m.building(15, 24, 22, 34, 4.2, [("w", 29, 2.4), ("s", 18.5, 2.2)], windows=True, mirror=True, notch=(14.6, 32.5, 15.05, 33.5))    # canal houses
    m.building(15, -14, 22, -4, 3.6, [("w", -9, 2.4), ("n", 18.5, 2.2)], mirror=True, notch=(14.6, -7.0, 15.05, -6.0))
    m.ladder(15, 33, 0, 4.55, "e", width=0.8); m.ladder(-15, -33, 0, 4.55, "w", width=0.8)        # up onto the roofs: perches over the canals
    m.ladder(15, -6.5, 0, 3.95, "e", width=0.8); m.ladder(-15, 6.5, 0, 3.95, "w", width=0.8)
    m.box(-6, 0, 16, -3, 1.6, 20, mirror=True); m.box(3, 0, 16, 6, 1.6, 20, mirror=True)               # causeway cover
    m.box(-7, 0, 4, -4, 2.2, 7, mirror=True); m.box(4.5, 0, -1, 7, 1.4, 1, mirror=True)
    m.box(-22, 0, -22, -16, 2.5, -16, accent=True, mirror=True); m.stairs(-16, -22, -14.5, -16, 2.5, "x", -1, mirror=True)
    for x in (-6, -2, 2, 6): m.spawn("a", x, 0, 40.2, 0, "b")
    m.box(-6.6, 0, 37.4, 6.6, 2.6, 37.8, accent=True, mirror=True)               # spawn screens: no sightline down the causeway
    m.spawn("any", -18, 0, 0, 90, "any")
    # lock-keepers' ladders up the gate posts: the gate beams are a narrow walkway 7 m over the water (a sniper perch
    # you can be knocked off)
    for (x, z, f) in ((8.5, 31, "e"), (-8.5, 31, "w"), (-8.5, -31, "w"), (8.5, -31, "e")): m.ladder(x, z, 0, 7.2, f, width=0.8)
    # humpback footbridges: a high way over each canal between the flat bridges (a second level and a loop)
    for flip in (1,):       # one per canal, point symmetric (the other halves of the banks are the canal houses)
        zl, zh = sorted((flip * 4, flip * 9)); m.stairs(7, zl, 9, zh, 2.5, "z", flip, mirror=True)
        dl, dh = sorted((flip * 9, flip * 11)); m.box(7, 2.2, dl, 16, 2.5, dh, mirror=True)
        m.stairs(16, dl, 20, dh, 2.5, "x", -1, mirror=True)                    # down onto the bank, away from the water
        for e in (dl - 0.1, dh): m.box(9, 2.5, e, 14, 3.4, e + 0.1, accent=True, mirror=True)
    # boathouse canopies on the banks: shade for the quay, a roofed fight beside the open water
    m.box(16, 3.6, -2.5, 22, 3.9, 8.5, accent=True, mirror=True)
    for (x, z) in ((16.2, -2.3), (16.2, 8.3)): m.metal(x - 0.2, 0, z - 0.2, x + 0.2, 3.6, z + 0.2, mirror=True)
    m.light(19, 3.0, 3.5, 8, 0.7, fixture=False, mirror=True)
    # stacked timber on the quays: wooden, so it stops you but not a bullet
    m.wallbang(17, 0, 1.5, 18.2, 1.6, 5.5, mirror=True); m.wallbang(19.5, 0, 6, 21, 2.2, 7.2, mirror=True)
    m.wallbang(-6, 0, 9, -3, 1.4, 10.2, mirror=True)
    m.raw("setting canal town")
    m.save("canal")
