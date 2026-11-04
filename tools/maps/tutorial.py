"""TUTORIAL (1.19.9): the movement-tech tutorial's map (MAIN MENU > TUTORIAL; src/game/tutorial.h, src/client/tutorial.cpp).
Never in a map list or vote (hidden by name: mapHiddenFromLists, map.h): only the tutorial hosts it.

A sealed training bay per lesson, side by side along z in the lessons' order, each running along +x from x -90
(src/game/tutorial.h kLessons: the same lengths and heights; quark_tests "tutorial" fails if a lesson's ghost script
stops working on this map). Side by side, not in a row: a row of them ran past the snapshots' +-512 m. Every bay is
24 m across: the ghost runs its lane on the left (6 m left of the middle) and you start on the right, both 3 m in from
the bay's start wall, facing +x. Nothing joins the bays: the tutorial moves you from one to the next, and a fall into
the lava puts you back at your bay's start. Each bay is built for its one tech: a long flat lane for strafing and the
chains, a lava gap only a dash crosses, a wall to bounce off, a 10 m ledge for the grapple, 8 m ledges for the rocket
and sticky jumps, a 72 m tall hall for the launch, an 80 m one for the bubble launch, and two bays where a second bot, the PARTNER, runs across the
middle (grapple a player) or hooks you from the far end (dash out of a grapple): a disc marks where it starts."""
from .prefabs import *

# (id, inside length, ceiling): kLessons in src/game/tutorial.h, in order
BAYS = [("move", 50, 12), ("strafe", 150, 12), ("dash", 50, 12), ("sidedash", 30, 12), ("super", 60, 12),
        ("hyper", 60, 12), ("pop", 70, 20), ("chain", 180, 16), ("slide", 60, 12), ("bounce", 12, 12),
        ("rampdash", 40, 20), ("grapple", 40, 18), ("ceilhang", 40, 16), ("swing", 70, 16), ("hookwalk", 80, 12),
        ("hookchain", 120, 16), ("hookplayer", 50, 14), ("breakfree", 50, 12), ("rocketjump", 40, 24),
        ("fallbounce", 30, 28), ("rocketsuper", 110, 16), ("rocketpop", 100, 60), ("launch", 160, 72),
        ("hookboost", 120, 16), ("stickyjump", 40, 24), ("stickystack", 30, 64),
        ("bubblelaunch", 30, 80), ("rocketgrapple", 110, 16)]
WALL = 1.0      # between bays (kWall)
HZ = 12.0       # half the width (kBayHalfZ)
X0 = -90.0      # every bay's start wall, inside face (kBayX0)
GHOST_Z, PLAYER_Z, START_IN = -6.0, 6.0, 3.0
GAP = (16, 25)  # the dash bay's lava gap (x into the bay; the check: G_GAP's aux 16 and target 26)
HURDLES = (15, 27)
LEDGE_GRAPPLE = (18, 10.0)   # from x, height
LEDGE_JUMP = (14, 8.0)
RAMP = (18, 10.4)   # the ramp dash bay's ramp: from x (in), its height (a 60 degree face: tut::G_RAMP's aux)


def bay_z(i):
    """the bay's middle (tut::bayZ)"""
    return (i - (len(BAYS) - 1) * 0.5) * (2 * HZ + WALL)


def build():
    m = M("Tutorial", "ffa", sun=(0.3, 1.0, 0.2))
    floors = ["concrete", "tile"]
    for i, (name, length, height) in enumerate(BAYS):
        x0, x1, zc = X0, X0 + length, bay_z(i)
        z0, z1 = zc - HZ, zc + HZ
        # the shell: floor, ceiling, the two long walls, the start wall and the wall at the far end
        thick = 6 if name == "dash" else 1
        if name == "dash":
            g0, g1 = x0 + GAP[0], x0 + GAP[1]
            m.box(x0, -thick, z0, g0, 0, z1, mat=floors[i % 2])
            m.box(g1, -thick, z0, x1, 0, z1, mat=floors[i % 2])
            m.box(g0, -thick - 1, z0, g1, -thick, z1, mat="metal")                   # the pit's bottom
            m.box(g0, -thick, z0, g1, -thick + 0.4, z1, mat="lava")                  # the lava in it (touch it: back to the start)
            m.box(g0 - 0.3, -0.06, z0, g0, 0, z1, mat="hazard")                    # the edges, striped
            m.box(g1, -0.06, z0, g1 + 0.3, 0, z1, mat="hazard")
        else:
            m.box(x0, -1, z0, x1, 0, z1, mat=floors[i % 2])
        m.box(x0 - WALL, height, z0 - WALL, x1 + WALL, height + 1, z1 + WALL)             # the ceiling
        m.box(x0 - WALL, -thick - 1, z0 - WALL, x1 + WALL, height, z0)                    # the long walls
        m.box(x0 - WALL, -thick - 1, z1, x1 + WALL, height, z1 + WALL)
        m.box(x0 - WALL, -thick - 1, z0, x0, height, z1)                                  # the start wall
        m.box(x1, -thick - 1, z0, x1 + WALL, height, z1, mat="hazard" if name == "bounce" else None)   # (bounce: the wall to dash into)
        # the lanes: a strip down the middle between them, the start pads, a line across every 10 m
        m.box(x0, 0, zc - 0.08, x1, 0.01, zc + 0.08, mat="trim")
        for z in (GHOST_Z, PLAYER_Z):
            m.box(x0 + START_IN - 0.6, 0, zc + z - 0.6, x0 + START_IN + 0.6, 0.01, zc + z + 0.6, mat="trim")
        for d in range(10, int(length), 10):
            if name == "dash" and GAP[0] - 1 <= d <= GAP[1] + 1: continue
            m.box(x0 + d - 0.05, 0, z0, x0 + d + 0.05, 0.01, z1, mat="trim")
        # glow bands along the walls (a tall hall: every 16 m up), and the lights (a tall hall: rows up its walls)
        for gy in [height - 0.6] + [y for y in range(15, int(height) - 8, 16)]:
            m.glow(x0, gy, z0, x1, gy + 0.1, z0 + 0.1)
            m.glow(x0, gy, z1 - 0.1, x1, gy + 0.1, z1)
        n = max(1, int(length // 14))
        for k in range(n):
            x = x0 + (k + 0.5) * length / n
            for z in (-6, 6):
                m.light(x, height - 1.2, zc + z, max(14.0, min(height * 0.9, 24.0)), 0.75)
        if height > 20:
            for y in range(6, int(height) - 6, 16):
                for k in range(max(1, int(length // 32))):
                    x = x0 + (k + 0.5) * length / max(1, int(length // 32))
                    for z in (-HZ + 1.5, HZ - 1.5):
                        m.light(x, y, zc + z, 20.0, 0.7, fixture=False)
        # each bay's own pieces
        if name == "move":
            for d in HURDLES:
                m.box(x0 + d, 0, z0, x0 + d + 0.4, 0.8, z1, mat="metal")
            m.box(x0 + 44, 0, z0, x0 + 45, 0.01, z1, mat="glow")   # the finish line
        elif name == "swing":   # the pillar to swing round (tut::LessonDef::aimPoint: 32 m in, 1 m left of the middle)
            m.box(x0 + 31.4, 0, zc - 1.6, x0 + 32.6, height, zc - 0.4, mat="metal")
        elif name == "rampdash":   # the 60 degree ramp (tan 60: 1.73 up a metre along), 18 m in, onto a 10.4 m shelf
            r0, r1, top = x0 + RAMP[0], x0 + RAMP[0] + RAMP[1] / 1.732, RAMP[1]
            m.raw(f"wedge 0 0 {r0:g} 0 {z0:g} {r1:.3f} {top:g} {z1:g}")
            m.box(r1, 0, z0, x1, top, z1)
            m.box(r0 - 0.3, 0, z0, r0, 0.01, z1, mat="hazard")   # (where the ramp starts, striped)
        elif name in ("hookplayer", "breakfree"):   # the PARTNER's spot (tut::LessonDef::partnerIn): a glowing pad
            px = x0 + (30 if name == "hookplayer" else 34)
            m.box(px - 0.8, 0, zc - 0.8, px + 0.8, 0.01, zc + 0.8, mat="glow")
        elif name in ("grapple", "rocketjump", "stickyjump"):
            d, h = LEDGE_GRAPPLE if name == "grapple" else LEDGE_JUMP
            m.box(x0 + d, 0, z0, x1, h - 0.3, z1)                        # the ledge (a flat face: nothing to snag on)
            m.box(x0 + d, h - 0.3, z0, x0 + d + 0.4, h, z1, mat="hazard")   # ...its edge, striped
            m.box(x0 + d + 0.4, h - 0.3, z0, x1, h, z1)
    # the spawns (a match on this map: the tutorial puts you at your lesson's start itself): the first bay's start
    zc = bay_z(0)
    for k in range(8):
        m.spawn("any", X0 + 6 + (k // 4) * 3, 0, zc - 9 + (k % 4) * 6, -90)
    m.raw("setting a movement training hall: one sealed bay per movement tech")
    m.save("tutorial", dress=False)
