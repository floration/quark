"""CASCADE: surf, a run through a frozen gorge on steep ramps (52 degree ridges, a face either side). Four stages, each
ending on a ledge with its checkpoint; the last ledge is the finish. The ramps are clean (owner, 2026-10-28: no
obstacles on a ramp): the run is in the transfers between them. Ridges that end and start again across a gap, lower
and to one side, so you fly from one face onto the next one's; narrower, steeper ridges (60 degrees) further on; a turn
through 90 degrees; zig-zags from one side to the other; and two kickers (50 degree faces across the way) that throw
you up onto a ledge only if you arrive fast. Nothing below but the drop: fall and you're back at your checkpoint."""
import math
from .prefabs import *

HALF = round(6 / math.tan(math.radians(52)), 2)   # a face's run for its 6 m rise: 52 degrees (owner 2026-10-28: 60 was too slippery)
STEEP = round(6 / math.tan(math.radians(60)), 2)  # the narrow ridges: 60 degrees, harder to hold


def build():
    m = M("Cascade", "surf", sun=(0.35, 1.0, 0.45))
    m.raw("void -80")

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

    # ---- stage 1, transfers: a long ridge, across a gap onto one offset east and lower, across again onto a narrow
    # steep one back west, down onto the ledge
    ridge_z(0, 44, 88, 34, 40)
    ridge_z(2 * HALF, 6, 39, 27, 33)
    ridge_z(0.5, -26, 1, 20, 26, half=STEEP)
    ledge(-6, -44, 8, -32, 12)

    # ---- stage 2, the turn: off the ledge's east edge onto a ridge running east, across a gap onto one offset
    # north, then the kicker: a 50 degree face across the way that throws you up onto the next ledge if you hit it fast
    # (slow, you slide back off into the drop)
    ridge_x(-38, 14, 50, -2, 4)
    ridge_x(-38 + 2 * HALF, 55, 82, -7, -1)
    m.raw("wedge 0 0 84.2 -11 -52 93 -0.5 -24")                            # the kicker: 50 degrees, rises toward +x
    m.glow(92.9, -0.5, -52, 93, -0.42, -24)
    ledge(93, -52, 108, -24, -0.5)

    # ---- stage 3, the broken ridge: off the ledge's south edge, a ridge in three pieces with gaps between, each a
    # little lower than the last, the second offset west, the third narrow and steep back east
    ridge_z(100, -88, -60, -18, -12)
    ridge_z(100 - 2 * HALF, -121, -94, -22, -16)
    ridge_z(100 - HALF + STEEP, -152, -127, -27, -21, half=STEEP)
    ledge(88, -170, 108, -158, -36)

    # ---- stage 4, the zig-zag and the finish: off the ledge's west edge onto short ridges running west, each
    # offset to the other side of the last (fly across from face to face), then the kicker up onto the finish
    zc = -164
    for k, (x0, x1) in enumerate(((60, 82), (36, 56), (12, 32))):
        off = HALF if k % 2 == 0 else -HALF
        ridge_x(zc + off, x0, x1, -48 - 3 * k, -42 - 3 * k)
    m.raw(f"wedge 0 1 -0.8 -58 {zc - 2 * HALF:g} 8 -47.5 {zc + 2 * HALF:g}")   # the kicker: 50 degrees, rises toward -x
    m.glow(-0.8, -47.5, zc - 2 * HALF, -0.7, -47.42, zc + 2 * HALF)
    ledge(-18, zc - 10, -0.8, zc + 10, -47.5)                                  # the finish

    # the gorge's walls far out on either side and the ice far below (scenery: nothing to ride)
    m.box(-60, -95, -200, -40, 70, 120, mat="far")
    m.box(140, -95, -200, 160, 70, 120, mat="far")
    m.box(-60, -96, -200, 160, -95, 120, mat="far")
    m.raw("setting frozen gorge")
    m.save("cascade", dress=False)
