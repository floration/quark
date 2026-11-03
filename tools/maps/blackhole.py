"""BLACK HOLE (owner, 2026-11-01): built for 3D VAMP, the zero-g 1v1 ruleset (lasers, flight, no knockback); offered for
1v1 (any ruleset), duel, team vs, tourney and free for all. Final Destination in space: one lone asteroid, its top a
flat 40 x 20 m floor with four slim pillars on it (landmarks, not cover: the fight is the flying), the rock tapering
away under it. Nothing else is solid (2026-11-02: jets, a debris ring and stray rocks out past the edge, all scenery).
Far off to the north a black hole eats the galaxy: a black disc (far scenery) with its accretion disc round it, a
flat ring of glowing matter (lava: unlit and bright) seen nearly edge on, and the lensed ring of light
hugging the hole's edge; the sky is black, so it draws stars. The spawns face each other across the floor (14 m from
the middle, past nothing), so nobody can fire for the first 3 s of a round (holdfire 3, as Dojo's bow). Out of
bounds: falling (or flying) 50 m under the rock is the void, and a flyer more than 40 m past the rock on any side or
above it is lost to space (Server::flownOut)."""
import math
from .prefabs import M

HX, HZ = 20, 10       # the floor's half sizes (40 x 20 m), its top at y 0
HOLE = (0.0, 14.0, -150.0)   # the black hole's centre, north of the rock and only a little over it (the disc nearly edge on; both spawns see it side on)
R = 30.0              # its black disc's radius
DISC_IN, DISC_OUT = 38.0, 80.0   # the accretion disc's inner and outer radius
RING = 4.0            # the lensed ring's width round the hole's edge


def build():
    m = M("Black Hole", "1v1", sun=(0.12, 0.45, -0.88))   # (the light comes off the accretion disc, low in the north)
    m.also = ["duel", "vs"]   # (every map that plays 1v1 also offers duel and team vs; 4 + 4 team spawns)
    m.raw("palette base 0.30 0.28 0.27 accent 0.20 0.18 0.18 metal 0 0 0 glow 1.00 0.62 0.25 sky 0 0 0")
    m.raw("void -50")
    m.raw("holdfire 3")   # (no cover, and the spawns face each other across the floor: 3 s to get airborne first, as Dojo's bow)
    asteroid(m)
    pillars(m)
    black_hole(m)
    space(m)
    # ---- the spawns: 1v1 at each end of the floor facing each other; free for all spread round it
    for z in (-3.75, -1.25, 1.25, 3.75):   # (four a side: duel and team vs draw from them)
        m.spawn("a", -14, 0, z, -90, "b")
    for (x, z, yaw) in ((-16, -6, -90), (-16, 6, -90), (-6, -7, 0), (-6, 7, 180)):
        m.spawn("any", x, 0, z, yaw, "any")
    m.raw("setting a lone asteroid drifting toward a black hole")
    m.save("blackhole", dress=False)   # (no dressing: the floor stays bare)


def asteroid(m):
    # the flat top (2 m of rock), then the body narrowing under it in layers, each hidden under the one above
    m.box(-HX, -2, -HZ, HX, 0, HZ, mat="blackbasaltblocks")
    layers = [(1.5, -5, "boulderfield"), (4, -8, "boulderfield"), (7, -11, "scoria"), (10, -14, "scoria"), (13.5, -16, "glowingfissures")]
    top = -2
    for (inset, y0, mat) in layers:
        m.box(-HX + inset, y0, -HZ + min(inset, HZ - 1.5), HX - inset, top, HZ - min(inset, HZ - 1.5), mat=mat)
        top = y0


def pillars(m):
    # four slim pillars on the floor (owner, 2026-11-02: "add some pillars on the platform to increase visibility"):
    # landmarks to read your height and drift by while flying, too thin to hide behind (1 m, 7 m tall), a glowing band
    # 1 m under each top so they show against the black sky
    for x in (-8, 8):
        for z in (-5, 5):
            m.box(x - 0.5, 0, z - 0.5, x + 0.5, 5.5, z + 0.5, mat="blackbasaltblocks")
            m.box(x - 0.5, 5.5, z - 0.5, x + 0.5, 6.0, z + 0.5, mat="glowingfissures")
            m.box(x - 0.5, 6.0, z - 0.5, x + 0.5, 7.0, z + 0.5, mat="blackbasaltblocks")


def black_hole(m):
    cx, cy, cz = HOLE
    # the hole: a black disc facing the rock (far scenery: no light, the black metal colour), rows 2 m tall
    rows = 30
    for i in range(rows):
        y0 = cy - R + 2 * R * i / rows; y1 = cy - R + 2 * R * (i + 1) / rows
        h = max(abs(y0 - cy), abs(y1 - cy)) if (y0 - cy) * (y1 - cy) > 0 else 0.0
        w = math.sqrt(max(0.0, R * R - h * h))
        m.box(round(cx - w, 2), round(y0, 2), cz - 6, round(cx + w, 2), round(y1, 2), cz + 6, mat="far")
    # the lensed ring: a thin band of light just round the disc's edge, behind it
    n = 24
    for i in range(n):
        y0 = cy - R - RING + 2 * (R + RING) * i / n; y1 = cy - R - RING + 2 * (R + RING) * (i + 1) / n
        yo = max(abs(y0 - cy), abs(y1 - cy)) if (y0 - cy) * (y1 - cy) > 0 else 0.0
        yi = min(abs(y0 - cy), abs(y1 - cy)) if (y0 - cy) * (y1 - cy) > 0 else 0.0
        wo = math.sqrt(max(0.0, (R + RING) ** 2 - yi * yi))
        wi = math.sqrt(max(0.0, R * R - yo * yo)) if yo < R else 0.0
        if wi <= 0.0:
            m.box(round(cx - wo, 2), round(y0, 2), cz - 7, round(cx + wo, 2), round(y1, 2), cz - 6.5, mat="lava")
        else:
            m.box(round(cx - wo, 2), round(y0, 2), cz - 7, round(cx - wi + 1, 2), round(y1, 2), cz - 6.5, mat="lava")
            m.box(round(cx + wi - 1, 2), round(y0, 2), cz - 7, round(cx + wo, 2), round(y1, 2), cz - 6.5, mat="lava")
    # the accretion disc: a flat ring round the hole (seen from the rock almost edge on: a bright band across it, its
    # far half behind the black disc), strips 8 m wide
    step = 8.0
    x = -DISC_OUT
    while x < DISC_OUT - 1e-6:
        x0, x1 = x, x + step
        xn = min(abs(x0), abs(x1)) if x0 * x1 > 0 else 0.0
        xf = max(abs(x0), abs(x1))
        zo = math.sqrt(max(0.0, DISC_OUT ** 2 - xn * xn))
        zi = math.sqrt(max(0.0, DISC_IN ** 2 - xf * xf)) if xf < DISC_IN else 0.0
        a, b = round(cx + x0, 2), round(cx + x1, 2)
        if zi <= 0.0:
            m.box(a, cy - 0.5, round(cz - zo, 2), b, cy + 0.5, round(cz + zo, 2), mat="lava")
        else:
            m.box(a, cy - 0.5, round(cz - zo, 2), b, cy + 0.5, round(cz - zi, 2), mat="lava")
            m.box(a, cy - 0.5, round(cz + zi, 2), b, cy + 0.5, round(cz + zo, 2), mat="lava")
        x += step


def space(m):
    # the outer decoration (owner, 2026-11-02: "increase the outer decoration on black hole"), all of it non-solid
    # (far scenery and lava: outside the playable bounds, so Server::flownOut's 40 m margin stays round the rock) and
    # at least 60 m out, past where a flyer is lost to space
    cx, cy, cz = HOLE
    # the jets: two thin beams straight up and down out of the hole's poles
    for (y0, y1) in ((cy + R + 4, cy + R + 110), (cy - R - 110, cy - R - 4)):
        m.box(cx - 1.5, round(y0, 2), cz - 1.5, cx + 1.5, round(y1, 2), cz + 1.5, mat="lava")
    # a broken outer ring of debris round the accretion disc (every other 10 degrees, 100 to 112 m out)
    for i in range(0, 36, 2):
        a = math.radians(i * 10 + 5)
        x, z = cx + 106 * math.cos(a), cz + 106 * math.sin(a)
        if z > -60:   # (the near side of the ring would pass over the rock: left out)
            continue
        m.box(round(x - 3, 2), cy - 0.4, round(z - 3, 2), round(x + 3, 2), cy + 0.4, round(z + 3, 2), mat="lava")
    # stray rocks drifting toward the hole: dark silhouettes against its light, some between the rock and the disc
    rocks = [(-45, 6, -80, 4), (38, -4, -95, 6), (-70, 20, -120, 5), (75, 12, -110, 7), (-20, -12, -70, 3),
             (95, -20, -60, 8), (-100, 10, -65, 9), (15, 30, -85, 3), (-55, -30, -100, 5), (60, 40, -140, 6)]
    for (x, y, z, r) in rocks:
        m.box(x - r, y - r * 0.7, z - r, x + r, y + r * 0.7, z + r, mat="far")
        m.box(x - r * 0.6, y + r * 0.7, z - r * 0.5, x + r * 0.5, y + r, z + r * 0.6, mat="far")
