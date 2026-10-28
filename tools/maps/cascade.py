"""CASCADE: surf, a run through a frozen gorge on steep ramps (52 degree ridges, a face either side). Four stages, each
ending on a ledge with its checkpoint; the last ledge is the finish. Not just downhill: beams across the ridges' faces
make you ride high or low, the second stage turns you through 90 degrees and ends on a kicker (a 50 degree face across
the way) that throws you up onto a ledge only if you arrive fast and ride high enough, the third breaks its ridge into pieces with gaps and posts on the ridge line, the
fourth weaves between two ridges and kicks you up onto the finish. Nothing below but the drop: fall and you're back at
your checkpoint."""
import math
from .prefabs import *

HALF = round(6 / math.tan(math.radians(52)), 2)   # a face's run for its 6 m rise: 52 degrees (owner 2026-10-28: 60 was too slippery)


def build():
    m = M("Cascade", "surf", sun=(0.35, 1.0, 0.45))
    m.raw("void -55")

    def ridge_z(xc, z0, z1, base, top, half=HALF):
        """a ridge along z: a face rising toward +x (west), one toward -x (east), a glow strip on the ridge line"""
        m.raw(f"wedge 0 0 {xc - half:g} {base:g} {z0:g} {xc:g} {top:g} {z1:g}")
        m.raw(f"wedge 0 1 {xc:g} {base:g} {z0:g} {xc + half:g} {top:g} {z1:g}")
        m.box(xc - half, base - 1.5, z0, xc + half, base, z1, accent=True)
        m.glow(xc - 0.05, top, z0, xc + 0.05, top + 0.08, z1)

    def ridge_x(zc, x0, x1, base, top, half=HALF):
        """a ridge along x: a face rising toward +z (south), one toward -z (north)"""
        m.raw(f"wedge 0 2 {x0:g} {base:g} {zc - half:g} {x1:g} {top:g} {zc:g}")
        m.raw(f"wedge 0 3 {x0:g} {base:g} {zc:g} {x1:g} {top:g} {zc + half:g}")
        m.box(x0, base - 1.5, zc - half, x1, base, zc + half, accent=True)
        m.glow(x0, top, zc - 0.05, x1, top + 0.08, zc + 0.05)

    def beam_z(xc, z, side, upper, base, top, half=HALF):
        """a beam across one face of a z ridge at z: over its upper half (ride low past it) or its lower half (ride
        high). side -1 west face, +1 east face."""
        a, b = (0.0, half / 2) if upper else (half / 2, half)
        x0, x1 = sorted((xc + side * a, xc + side * b))
        m.metal(x0, base, z - 0.4, x1, top + 1.5, z + 0.4)

    def beam_x(zc, x, side, upper, base, top, half=HALF):
        a, b = (0.0, half / 2) if upper else (half / 2, half)
        z0, z1 = sorted((zc + side * a, zc + side * b))
        m.metal(x - 0.4, base, z0, x + 0.4, top + 1.5, z1)

    def ledge(x0, z0, x1, z1, top, cp=True):
        m.box(x0, top - 1.5, z0, x1, top, z1)
        m.box(x0, top - 2.5, z0 + 1, x1, top - 1.5, z1 - 1, accent=True)
        for x in (x0, x1 - 0.12): m.glow(x, top - 0.3, z0, x + 0.12, top - 0.1, z1)
        if cp: m.raw(f"checkpoint {x0 + 1:g} {top:g} {z0 + 1:g} {x1 - 1:g} {top + 4:g} {z1 - 1:g}")
        m.light((x0 + x1) / 2, top + 5, (z0 + z1) / 2, 12, 0.8, fixture=False)

    # ---- the start: a deck high on the gorge's north end; run off its south edge onto the first ridge
    m.box(-6, 46, 92, 6, 50, 104)
    m.box(-6, 50, 103.5, 6, 54, 104, accent=True)
    for x in (-6, 5.88): m.glow(x, 49.7, 92, x + 0.12, 49.9, 104)
    for z in (97, 100.5):                                                    # the start grid: eight surfers
        for x in (-4.5, -1.5, 1.5, 4.5): m.spawn("race", x, 50, z, 0)
    m.light(0, 55, 98, 12, 0.8, fixture=False)

    # ---- stage 1, the slalom: one long ridge with beams across its faces, high, low, high; then across onto a
    # second ridge offset east, and down onto the ledge
    ridge_z(0, 30, 88, 34, 40)
    for (z, side, upper) in ((76, 1, True), (66, 1, False), (56, 1, True), (46, 1, False),
                             (71, -1, False), (61, -1, True), (51, -1, False), (41, -1, True)):
        beam_z(0, z, side, upper, 34, 40)
    ridge_z(2 * HALF, -10, 26, 26, 32)
    beam_z(2 * HALF, 12, -1, True, 26, 32); beam_z(2 * HALF, 4, 1, False, 26, 32)
    ledge(2, -26, 16, -14, 22)

    # ---- stage 2, the turn: off the ledge's east edge onto a ridge running east, along it, then the kicker: a 50
    # degree face across the way that throws you up onto the next ledge if you hit it fast (slow, you slide back off
    # into the drop)
    ridge_x(-20, 22, 70, 8, 14)
    beam_x(-20, 40, 1, True, 8, 14); beam_x(-20, 50, -1, True, 8, 14)
    ridge_x(-20 + 2 * HALF, 74, 90, 0, 6)
    m.raw("wedge 0 0 91.2 -4 -28 100 6.5 -6")                              # the kicker: 50 degrees, rises toward +x
    m.glow(99.9, 6.5, -28, 100, 6.58, -6)
    ledge(100, -30, 115, -4, 6.5)

    # ---- stage 3, the broken ridge: off the ledge's south edge, a ridge in three pieces with gaps between, each a
    # little lower and the last offset west; a post on the ridge line of the middle piece
    ridge_z(109, -66, -38, -10, -4)
    ridge_z(109, -96, -71, -13, -7)
    m.metal(108.6, -7, -84.4, 109.4, -3, -83.6)
    ridge_z(109 - 2 * HALF, -128, -100, -17, -11)
    beam_z(109 - 2 * HALF, -114, 1, True, -17, -11)
    ledge(90, -146, 108, -134, -22)

    # ---- stage 4, the weave and the finish: off the ledge's west edge onto two ridges running west side by side (a
    # gap between: switch from one to the other at the beams), then the kicker up onto the finish
    zc = -140
    ridge_x(zc - HALF - 1, 30, 84, -36, -30)
    ridge_x(zc + HALF + 1, 30, 84, -36, -30)
    for (x, z, side, upper) in ((70, zc - HALF - 1, 1, False), (58, zc + HALF + 1, -1, False), (46, zc - HALF - 1, 1, True)):
        beam_x(z, x, side, upper, -36, -30)
    m.raw(f"wedge 0 1 19.2 -40 {zc - 2 * HALF - 1:g} 28 -29.5 {zc + 2 * HALF + 1:g}")   # the kicker: 50 degrees, rises toward -x
    m.glow(19.2, -29.5, zc - 2 * HALF - 1, 19.3, -29.42, zc + 2 * HALF + 1)
    ledge(1, zc - 10, 19.2, zc + 10, -29.5)                                    # the finish

    # the gorge's walls far out on either side and the ice far below (scenery: nothing to ride)
    m.box(-50, -70, -170, -30, 70, 120, mat="far")
    m.box(140, -70, -170, 160, 70, 120, mat="far")
    m.box(-50, -71, -170, 160, -70, 120, mat="far")
    m.raw("setting frozen gorge")
    m.save("cascade", dress=False)
