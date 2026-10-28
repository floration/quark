"""CASCADE: surf, a run down a frozen gorge on steep ramps. Each stage is a pair of ramps (a ridge with a 60 degree
face either side) a little lower than the last, then a ledge with the stage's checkpoint: drop off the start onto the
first ridge, ride a face by strafing into it, fly across to the next ridge, land on the ledge. Three stages, the
last ledge is the finish. Nothing below but the drop: fall and you're back at your checkpoint."""
from .prefabs import *


def build():
    m = M("Cascade", "surf", sun=(0.35, 1.0, 0.45))
    m.raw("void -50")

    def ramp(xc, z0, z1, base, top, half=3.5, mat=0):
        """a ridge running along z: two wedges back to back, each face rising (top - base) over `half` m (60 degrees
        at 6 over 3.5), a glow strip down the ridge so the line reads from the air"""
        m.raw(f"wedge {mat} 0 {xc - half:g} {base:g} {z0:g} {xc:g} {top:g} {z1:g}")   # west face: rises toward +x
        m.raw(f"wedge {mat} 1 {xc:g} {base:g} {z0:g} {xc + half:g} {top:g} {z1:g}")   # east face: rises toward -x
        m.box(xc - half, base - 1.5, z0, xc + half, base, z1, accent=True)          # the ridge's footing
        m.glow(xc - 0.05, top, z0, xc + 0.05, top + 0.08, z1)

    def ledge(x0, z0, x1, z1, top, cp=True):
        m.box(x0, top - 1.5, z0, x1, top, z1)
        m.box(x0, top - 2.5, z0 + 1, x1, top - 1.5, z1 - 1, accent=True)
        for x in (x0, x1 - 0.12): m.glow(x, top - 0.3, z0, x + 0.12, top - 0.1, z1)
        if cp: m.raw(f"checkpoint {x0 + 1:g} {top:g} {z0 + 1:g} {x1 - 1:g} {top + 4:g} {z1 - 1:g}")
        m.light((x0 + x1) / 2, top + 5, (z0 + z1) / 2, 12, 0.8, fixture=False)

    # ---- the start: a deck high on the gorge's north end; run off its south edge onto the first ridge
    m.box(-6, 40, 86, 6, 44, 98)
    m.box(-6, 44, 97.5, 6, 48, 98, accent=True)
    for x in (-6, 5.88): m.glow(x, 43.7, 86, x + 0.12, 43.9, 98)
    for z in (91, 94.5):                                                    # the start grid: eight surfers
        for x in (-4.5, -1.5, 1.5, 4.5): m.spawn("race", x, 44, z, 0)
    m.light(0, 49, 92, 12, 0.8, fixture=False)
    # ---- stage 1: two ridges, the second offset east (ride the first's east face across onto its west face)
    ramp(0, 40, 82, 28, 34)
    ramp(7.5, -2, 36, 20, 26)
    ledge(0, -16, 14, -6, 16)
    # ---- stage 2: off the ledge's south edge onto a ridge back in the middle, then one offset west
    ramp(4, -66, -24, 4, 10)
    ramp(-5, -110, -70, -6, 0)
    ledge(-14, -126, 2, -114, -10)
    # ---- stage 3: one long ridge down the middle, the finish ledge at its end
    ramp(-6, -178, -132, -26, -20)
    ledge(-15, -196, 3, -184, -32)
    # the gorge's walls far out on either side and the ice far below (scenery: nothing to ride)
    for x0 in (-60, 40):
        m.box(x0, -60, -200, x0 + 20, 60, 100, mat="far")
    m.box(-60, -61, -200, 60, -60, 100, mat="far")
    m.raw("setting frozen gorge")
    m.save("cascade", dress=False)
