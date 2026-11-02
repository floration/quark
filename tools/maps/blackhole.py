"""BLACK HOLE (owner, 2026-11-01): built for 3D VAMP, the zero-g 1v1 ruleset (lasers, flight, no knockback); offered for
1v1 (any ruleset), tourney, winner stays and free for all. Final Destination in space: one lone asteroid, its top a
flat 40 x 20 m floor with nothing on it (no cover: the fight is the flying), the rock tapering away under it. Nothing
else is solid. Far off to the north a black hole eats the galaxy: a black disc (far scenery) with its accretion disc
round it, a flat ring of glowing matter (lava: unlit and bright) seen nearly edge on, and the lensed ring of light
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
    m.raw("palette base 0.30 0.28 0.27 accent 0.20 0.18 0.18 metal 0 0 0 glow 1.00 0.62 0.25 sky 0 0 0")
    m.raw("void -50")
    m.raw("holdfire 3")   # (no cover, and the spawns face each other across the floor: 3 s to get airborne first, as Dojo's bow)
    asteroid(m)
    black_hole(m)
    # ---- the spawns: 1v1 at each end of the floor facing each other; free for all spread round it
    for z in (-3.75, -1.25, 1.25, 3.75):   # (four a side: winner stays draws from them)
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
