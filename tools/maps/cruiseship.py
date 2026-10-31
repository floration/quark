"""CRUISE SHIP (1.19.6): hide and seek (tag, free for all) on a cruise liner at sea at dusk. The ship is 147 x 26 m
(x -72 stern .. 75 bow, z -13..13), with sea all round (far scenery; the void under it).
  Engine room (-7, open up to deck 2 between x -36 and 12): two diesel engines, the control room, hull catwalks at
  deck 1's height with bridges to the mid stair tower, the shaft tunnel to the stern (a dead end).
  Deck 1, crew (-3.4): the laundry, the crew mess and crew cabins aft; the galley, cold stores, storerooms and the
  chain locker forward.
  Deck 2, cabins (0): port and starboard corridors (2.4 m, fire-door dog-legs every 18 m) with 48 cabins on the
  hull side (3 x 4 m: a bunk, a wardrobe to crouch in, a bathroom pod; a third locked), linen rooms and suites on the
  inner side, the atrium.
  Deck 3, promenade (3.4): the shopping arcade, the casino round the atrium, the dining room; open promenades down
  both sides, the aft deck and the foredeck: a lap of the ship.
  Deck 4, lido (6.8): the pool and its slide, the hot tub, the bar, the funnel (a ladder inside to its top grate).
  Sun deck (10.2): the lifeboats (one way in each), the bridge and its radar mast.
Three stair towers (aft, mid, fore) on the centre line join the decks: each a pair of flights per deck, side by
side, with landings at both ends crossing the ship. The atrium (12 x 12 m, deck 2 up to the lido) has its own
stairs and a static glass lift. Rails and invisible walls keep everyone aboard.
(1.19.6 fix round, all review notes YES: the fire-door stubs are 1.3 m from opposite walls, overlapping, so no straight
line runs down a corridor (line of sight checked across z 6.8..8.8), and there are three a corridor, not five (four legs
of 25-35 m for tag); the sea is the engine's water look, not far scenery; the hull is dark below a blue band and white
above, the lido carries the white superstructure up to the sun deck as a windscreen with blue panes, and a two-storey
deckhouse with a balcony row stands on the sun deck; short ladders up the engines' forward ends (their top walks
cleared of the deckhead pipes); one locked crew cabin a side on deck 1; warm glow exit signs at every stair-tower
landing; corridor lights 0.7; an ffa-any spawn set over decks 1, 2 and 4, the engine room and the aft deck.)"""
import math
from .prefabs import M
from .m6kit import Kit, wall_x, wall_z, slab, yaw_to, minus, tiles, num

E, D1, D2, D3, D4, D5 = -7.0, -3.4, 0.0, 3.4, 6.8, 10.2
X0, XB, XT = -72.0, 50.0, 75.0          # stern, the bow taper's start, the bow's tip
HZ = 13.0
LID = 70.0
BOW = [(XB, -HZ), (XT, 0.0), (XB, HZ)]
T1, T2, T3 = -52.0, -20.0, 26.0         # the stair towers (x0 .. x0 + 10, z -2.4..2.4)
ATR = (-6, -6, 6, 6)                    # the atrium's well


def tower_flights(k, x0, decks, mat="steeldeckplate", wall="painteddrywall"):
    """a stair tower: flight j (decks[j] -> decks[j + 1]) in lane j % 2 (z -2.2..0 / 0..2.2), x0 + 2 .. x0 + 8,
    rising +x on even j and -x on odd; tube walls (z +-2.4, the divider at 0) along the flights from the lowest deck
    to the top one's ceiling. Returns {deck: hole rect} for the deck slabs (over the flight below)."""
    holes = {}
    for j in range(len(decks) - 1):
        lane = (-2.2, 0.0) if j % 2 == 0 else (0.0, 2.2)
        k.stairs(mat, x0 + 2, lane[0], x0 + 8, lane[1], decks[j + 1], "x", 1 if j % 2 == 0 else -1, base=decks[j], solid=(j == 0))
        holes[decks[j + 1]] = (x0 + 2, lane[0], x0 + 8, lane[1])
    for (z0, z1) in ((-2.6, -2.2), (2.2, 2.6), (-0.06, 0.06)):
        k.box(wall, x0 + 2, decks[0], z0, x0 + 8, decks[-1], z1)
    return holes


def deck(k, mat, y, x0, x1, holes=(), bow=False):
    """a deck slab (top at y) across the hull from x0 to x1, less holes; with bow, the tapered bow too"""
    slab(k, mat, (x0, -HZ, x1, HZ), y, holes=holes, size=24)
    if bow: k.prism(mat, BOW, y - 0.4, y)


def cabin_row(k, xs, side, y, locked):
    """cabins 3 m wide along the hull on one side (side -1: z -13..-9, door onto z -9; 1: z 9..13), one per x in
    xs: thin partitions (wallbang), a front with a door (or a locked door), a bathroom pod, a bunk, a wardrobe you
    can crouch in"""
    def Z(v): return side * (9.0 + v)                 # depth v (0 at the corridor wall .. 4 at the hull)
    def zb(v0, v1): return sorted((Z(v0), Z(v1)))
    h = y + 3.0
    for i, x in enumerate(xs):
        za, zb_ = zb(0, 4)
        k.box("wainscotpanelling", x - 0.06, y, za, x + 0.06, h, zb_)                         # partition (west)
        f0, f1 = zb(-0.06, 0.06)
        k.box("painteddrywall", x, y, f0, x + 0.3, h, f1); k.box("painteddrywall", x + 1.5, y, f0, x + 3, h, f1)
        k.box("painteddrywall", x + 0.3, y + 2.4, f0, x + 1.5, h, f1)
        if i in locked: k.box("steeldoorskin", x + 0.3, y, f0, x + 1.5, y + 2.4, f1)           # a locked door
        p0, p1 = zb(0.06, 1.6)                                                                # the bathroom pod
        k.box("whitesquaretile", x + 1.6, y, p0, x + 1.7, h, p1)
        q0, q1 = zb(1.5, 1.6); k.box("whitesquaretile", x + 1.6, y, q0, x + 2.94, h, q1)
        d0, d1 = zb(0.5, 1.4); k.box("whitesquaretile", x + 1.6, y + 2.2, d0, x + 1.7, h, d1)  # (its doorway's lintel)
        b0, b1 = zb(1.9, 3.94)
        k.box("quiltedleather", x + 1.1, y, b0, x + 2.94, y + 0.5, b1)                        # the bunk
        k.box("wideplanks", x + 1.1, y + 1.8, b0, x + 2.94, y + 1.95, b1)
        w0, w1 = zb(2.9, 3.94)                                                                # the wardrobe
        k.box("wideplanks", x + 0.95, y, w0, x + 1.05, y + 2.2, w1)
        g0, g1 = zb(2.85, 2.95); k.box("wideplanks", x + 0.06, y + 1.3, g0, x + 1.05, y + 2.2, g1)
        k.box("wideplanks", x + 0.06, y + 2.2, w0, x + 1.05, y + 2.3, w1)
    x = xs[-1] + 3
    za, zb_ = zb(0, 4)
    k.box("wainscotpanelling", x - 0.06, y, za, x + 0.06, h, zb_)


def chicane(k, x, side):
    """a fire-door frame across a corridor (z 6.6..9 on side 1): two stubs 1.3 m wide, 1 m apart, from opposite
    walls: they overlap by 0.2 m, so the 1.1 m gaps never line up and no straight slot runs down the corridor"""
    a, b = sorted((side * 6.6, side * 9.0))
    k.box("steeldoorskin", x, 0, a, x + 0.3, 3.0, a + 1.3) if side > 0 else k.box("steeldoorskin", x, 0, b - 1.3, x + 0.3, 3.0, b)
    k.box("steeldoorskin", x + 1.3, 0, b - 1.3, x + 1.6, 3.0, b) if side > 0 else k.box("steeldoorskin", x + 1.3, 0, a, x + 1.6, 3.0, a + 1.3)
    k.box("steeldoorskin", x, 2.6, a, x + 1.6, 3.0, b)


def decorate(k):
    """sparingly: on walls, ceilings and the ship's edges (no clutter in the corridors or on the decks' lanes)"""
    # lifebuoys on the superstructure's sides along the promenades (flush), a foremast (a hook) on the foredeck
    for x in (-56, -32, -8, 24, 46):
        for s in (-1, 1):
            a, b = sorted((s * 10.15, s * 10.3))
            k.box("hazard", x - 0.4, D3 + 1.2, a, x + 0.4, D3 + 2.0, b)
    k.disc("galvanizedsteel", 68, 0, 0.25, D3, 18.0, n=8)
    k.box("galvanizedsteel", 67.9, 15.5, -2.5, 68.1, 15.7, 2.5)
    # the funnel's black top band is built with it; a red stripe round it
    k.ring("paintedredbrick", -58.0, 0.0, 3.4, 3.5, 17.0, 18.2, n=8, a0=-22.5)
    # the radar on the mast; navigation lights on the bridge's wings
    k.box("galvanizedsteel", 41.2, 19.2, -1.4, 42.8, 19.6, 1.4)
    k.box("glow", 45.8, D5 + 2.6, -10.3, 46.2, D5 + 3.0, -9.9); k.box("glow", 45.8, D5 + 2.6, 9.9, 46.2, D5 + 3.0, 10.3)
    # exit signs (glow, the palette's warm colour) on the stair towers' dividers at both landings of every deck
    for (x0, decks) in ((T1, (D1, D2, D3)), (T2, (E, D1, D2, D3)), (T3, (D1, D2, D3, D4))):
        for yy in decks:
            k.box("glow", x0 + 1.95, yy + 2.2, -0.3, x0 + 2.0, yy + 2.5, 0.3)
            k.box("glow", x0 + 8.0, yy + 2.2, -0.3, x0 + 8.05, yy + 2.5, 0.3)
    # the casino's sign over its aft entrance, the dining room's ceiling lamps, the arcade's shop signs
    k.box("neonmaze", -23.8, D3 + 2.4, -2.5, -23.6, D3 + 2.9, 2.5)
    for x in (15, 20, 40, 45):
        k.disc("glow", x, 0, 0.6, D4 - 0.55, D4 - 0.4, n=8)
    for x in (-61.8, -53.4, -45, -36.6, -28.2):
        for s in (-1, 1):
            a, b = sorted((s * 3.9, s * 3.8))
            k.box("glow", x - 1.2, D3 + 2.65, a, x + 1.2, D3 + 2.9, b)
    # pipes along the engine room's deckhead; potted palms at the atrium's corners on deck 2
    for z in (-2, 2):   # (over the aisle between the engines: the engines' top walks keep their headroom)
        k.box("greasestainedmetal", -36, -1.2, z - 0.2, 12, -0.8, z + 0.2)
    for (x, z) in ((-5.2, -5.2), (5.2, 5.2)):
        k.box("terracotta", x - 0.4, D2, z - 0.4, x + 0.4, D2 + 0.6, z + 0.4); k.box("jungleleaves", x - 0.6, D2 + 0.6, z - 0.6, x + 0.6, D2 + 1.6, z + 0.6)
    # umbrellas at the pool's ends (out of the lanes: against the rim)
    for x in (-42, -22):
        k.box("chrome", x - 0.05, D4, -0.05, x + 0.05, D4 + 2.4, 0.05)
        k.cut("stripedwallpaper", (x - 1.4, D4 + 2.2, -1.4, x + 1.4, D4 + 2.8, 1.4), [(1, 1, 0, x + 1.4 + D4 + 2.2), (-1, 1, 0, -x + 1.4 + D4 + 2.2), (0, 1, 1, 1.4 + D4 + 2.2), (0, 1, -1, 1.4 + D4 + 2.2)])


def build():
    m = M("Cruise Ship", "hide", sun=(-0.85, 0.3, 0.25))
    m.also = ["tag"]
    k = Kit(m)
    m.raw("palette base 0.86 0.84 0.8 accent 0.2 0.3 0.5 metal 0.34 0.35 0.38 glow 1 0.78 0.5 sky 0.42 0.26 0.44")
    m.raw("void -10")
    m.raw("setting a cruise liner at sea at dusk")

    # ================================================================== the towers first (their holes go in the decks)
    h1 = tower_flights(k, T1, [D1, D2, D3, D4])
    h2 = tower_flights(k, T2, [E, D1, D2, D3, D4])
    h3 = tower_flights(k, T3, [D1, D2, D3, D4, D5])
    def holes_at(y): return [h[y] for h in (h1, h2, h3) if y in h]

    # ================================================================== hull, decks, sea, bounds
    HULL = "shiphullplating"
    k.box(HULL, X0, E - 0.8, -HZ, XB, E - 0.4, HZ); k.prism(HULL, BOW, E - 0.8, E - 0.4)          # the bottom
    # the hull's sides: dark plating to the waterline and up to the cabin deck, a blue band, white above (the
    # promenade's bulwark: a liner's white upperworks)
    bands = [(HULL, E - 0.8, 1.8), ("bluedsteel", 1.8, 2.6), ("whiteconcrete", 2.6, D3 + 1.1)]
    for (mat, ya, yb) in bands:
        for s in (-1, 1):
            a, b = sorted((s * HZ, s * (HZ + 0.5)))
            k.box(mat, X0 - 0.5, ya, a, XB, yb, b)
        k.box(mat, X0 - 0.5, ya, -HZ, X0, yb, HZ)
        for s in (-1, 1):   # the bow's flanks (a bar each side, from the taper's start to the tip)
            k.bar(mat, (XB, s * (HZ + 0.25)), (XT + 0.2, s * 0.15), 0.5, ya, yb)
    # lower hull: the space under deck 1 outside the engine room and the tunnel is sealed off
    for (x0, x1) in ((X0, -66), (-66, -36), (12, XB)):
        for (a, b) in ((-HZ, -1.2), (1.2, HZ)) if (x0, x1) == (-66, -36) else ((-HZ, HZ),):
            k.box("sheetpiling", x0, E - 0.4, a, x1, D1 - 0.4, b)
    k.prism("sheetpiling", BOW, E - 0.4, D1 - 0.4)
    # engine room floor and the tunnel's
    deck(k, "steelcheckerfloor", E, -36, 12)
    k.box("steelcheckerfloor", -66, E - 0.4, -1.2, -36, E, 1.2)
    # deck 1 (aft and forward of the engine room), deck 2, deck 3 (the promenade's floor), deck 4 (the lido)
    deck(k, "linoleum", D1, X0, -36, holes_at(D1))
    deck(k, "linoleum", D1, 12, XB, holes_at(D1), bow=True)
    deck(k, "officecarpettiles", D2, X0, XB, holes_at(D2), bow=True)
    a3 = [(-6, -6, 6, 3.6), (-6, 3.6, 3, 6)]           # the atrium's well at deck 3 (less the south stair's landing)
    a4 = [(-6, -3.6, 6, 6), (-1, -6, 6, -3.6)]         # ... at deck 4 (less the north stair's landing)
    POOL = (-40, -4, -24, 4)
    deck(k, "wideplanks", D3, X0, XB, holes_at(D3) + a3, bow=True)
    deck(k, "wideplanks", D4, -66, XB, holes_at(D4) + a4 + [POOL])
    slab(k, "steeldeckplate", (8, -11, 46, 11), D5, holes=holes_at(D5))
    # the sea: water round the hull (the engine's unlit rippling water look, blue against the pink dusk; none inside
    # the hull), the void under it
    for (x0, z0, x1, z1) in tiles(minus((-400, -400, 400, 400), [(X0 - 0.5, -HZ - 0.5, XT + 0.5, HZ + 0.5)]), 200):
        k.box("water", x0, -2.4, z0, x1, -2.0, z1)
    for s in (-1, 1):
        k.prism("water", [(XB, s * (HZ + 0.5)), (XT + 0.5, s * (HZ + 0.5)), (XT + 0.5, s * 0.5)], -2.4, -2.0)
    # the invisible walls: round the hull from the bulwark up, over the bow's flanks, the lid
    k.box("clip", X0 - 2.5, D3 + 1.1, -HZ - 2.5, X0 - 0.5, LID, HZ + 2.5)
    for s in (-1, 1):
        a, b = sorted((s * (HZ + 0.5), s * (HZ + 2.5)))
        k.box("clip", X0 - 0.5, D3 + 1.1, a, XB, LID, b)
        k.bar("clip", (XB, s * (HZ + 1.5)), (XT + 1.2, s * 0.6), 2.0, D3 + 1.1, LID)
    k.box("clip", XT, D3 + 1.1, -2, XT + 2, LID, 2)
    k.box("clip", X0 - 2.5, LID, -HZ - 2.5, XT + 2, LID + 1, HZ + 2.5)

    # ================================================================== the engine room (x -36..12, -7 .. deck 2)
    for x in (-36, 12):
        wall_z(k, "boilerplate", -HZ, HZ, x, E, D1 - 0.4, [(0, 2.4, 3.0)] if x == -36 else [])
        wall_z(k, "boilerplate", -HZ, HZ, x, D1, D2 - 0.4, [(-11.6, 2.0, 2.6), (11.6, 2.0, 2.6)])
    # the hull catwalks at deck 1's height (z +-10.6..12.6) and the bridges to the mid tower's landings
    for s in (-1, 1):
        a, b = sorted((s * 10.6, s * HZ))
        k.box("meshcatwalk", -36, D1 - 0.3, a, 12, D1, b)
        c = s * 10.6
        for (xa, xb) in ((-36, T2), (T2 + 2, T2 + 8), (T2 + 10, 12)):
            k.box("galvanizedsteel", xa, D1, min(c, c + 0.1 * s), xb, D1 + 1.1, max(c, c + 0.1 * s))
        for x in (T2, T2 + 8):
            a, b = sorted((s * 2.4, s * 10.6))
            k.box("meshcatwalk", x, D1 - 0.3, a, x + 2, D1, b)
    for x in (T2, T2 + 8):
        k.box("meshcatwalk", x, D1 - 0.3, -2.4, x + 2, D1, 2.4)       # the tower's landings at deck 1's height
    # (the catwalk rails' gaps where the bridges meet them)
    # the engines (x -34..-24, z +-3.6..9.4, 3.6 m) with a walk along each top; machinery forward; the control room
    for s in (-1, 1):
        a, b = sorted((s * 3.6, s * 9.4))
        k.box("engineblock", -34, E, a, -24, E + 3.6, b)
        k.box("greasestainedmetal", -33, E + 3.6, a + 0.6, -24, E + 4.4, b - 0.6)
        k.ladder(-24.0, s * 6.5, E, E + 4.4, "w")            # a short ladder up its forward end (bots too)
        a, b = sorted((s * 4.0, s * 8.0))
        k.box("guttedmachinery", -4, E, a, 4, E + 2.4, b)
    wall_x(k, "boilerplate", 2, 12, -5, E, E + 3.2, [(7, 1.6, 2.6), (4.5, 2.0, 2.6, 1.0)], t=0.3, glass="glass")
    wall_z(k, "boilerplate", -HZ, -5, 2, E, E + 3.2, [(-9, 2.0, 2.4, 1.0)], t=0.3, glass="glass")
    k.box("boilerplate", 2, E + 3.2, -HZ, 12, E + 3.5, -5)
    k.box("gaugecluster", 3, E, -12.9, 11.6, E + 1.2, -12)
    # the shaft tunnel (x -66..-36, z -1.2..1.2, ceiling -3.8): the propeller shaft along its floor's side
    k.box("castiron", -66, E, 0.6, -36, E + 0.6, 1.1)

    # ================================================================== deck 1: crew (aft) and stores (forward)
    y = D1
    # aft: x -72..-56 the laundry (port) and the crew mess (starboard); x -56..-36 crew cabins beside the aft tower,
    # passages along the hull to the engine room's catwalks
    wall_z(k, "painteddrywall", -HZ, HZ, -56, y, D2 - 0.4, [(-10.8, 2.4, 2.6), (10.8, 2.4, 2.6)])
    wall_x(k, "painteddrywall", X0, -56, 0, y, D2 - 0.4, [(-64, 2.0, 2.6)])
    for (x, z) in ((-70, -11), (-67.5, -11), (-65, -11), (-62.5, -11)):
        k.box("whitesquaretile", x - 1, y, z - 1, x + 1, y + 1.4, z + 1)            # washers
    for (x, z) in ((-66, -5), (-61, -4)):                                          # laundry bins (open tops)
        k.box("chipboard", x - 1, y, z - 1, x + 1, y + 0.1, z + 1)
        for (a, b, c, d) in ((x - 1, z - 1, x + 1, z - 0.9), (x - 1, z + 0.9, x + 1, z + 1), (x - 1, z - 0.9, x - 0.9, z + 0.9), (x + 0.9, z - 0.9, x + 1, z + 0.9)):
            k.box("chipboard", a, y, b, c, y + 1.0, d)
    for x in (-68, -61):
        k.box("wideplanks", x - 2, y, 4, x + 2, y + 0.9, 9)                         # mess tables
    # crew cabins x -56..-52, -50..-44 (beside the tubes), -42..-36 (z +-2.6..8.7), doors onto the hull passages; the
    # tower's landings (x -52..-50, -44..-42) cross between the passages; small store cupboards off the landings
    for s in (-1, 1):
        wall_x(k, "painteddrywall", -56, -52, s * 8.7, y, D2 - 0.4, [(-54, 1.2, 2.4)], t=0.2)
        wall_x(k, "painteddrywall", -50, -44, s * 8.7, y, D2 - 0.4, [(-47, 1.2, 2.4)], t=0.2)
        lx = -47 if s < 0 else -39                               # one locked crew cabin a side (deck 1 has some too)
        k.box("steeldoorskin", lx - 0.6, y, s * 8.7 - 0.1, lx + 0.6, y + 2.4, s * 8.7 + 0.1)
        wall_x(k, "painteddrywall", -42, -36, s * 8.7, y, D2 - 0.4, [(-39, 1.2, 2.4)], t=0.2)
        for x in (-52, -50, -44, -42):
            k.box("painteddrywall", x - 0.06, y, min(s * 2.6, s * 8.7), x + 0.06, D2 - 0.4, max(s * 2.6, s * 8.7))
        for (x0, x1) in ((-56, -52), (-42, -36)):
            k.box("painteddrywall", x0, y, min(s * 2.4, s * 2.6), x1, D2 - 0.4, max(s * 2.4, s * 2.6))
        for x in (-54, -47, -39):
            k.box("quiltedleather", x - 1.0, y, min(s * 5.4, s * 7.4), x + 1.0, y + 0.5, max(s * 5.4, s * 7.4))
    # forward: the galley (x 12..26), the cold stores beside the fore tower, the storerooms (x 36..50) on a centre
    # passage, the chain locker in the bow (a dead end)
    wall_z(k, "whitesquaretile", -HZ, HZ, T3, y, D2 - 0.4, [(-8, 1.6, 2.4), (0, 4.4, 2.6), (8, 1.6, 2.4)])
    for s in (-1, 1):
        k.box("whitesquaretile", T3, y, min(s * 2.4, s * 2.6), T3 + 10, D2 - 0.4, max(s * 2.4, s * 2.6))
        k.box("chrome", 15, y, s * 6 - 0.6, 24, y + 1.0, s * 6 + 0.6)                 # galley counters
        k.box("quiltedstainless", T3 + 3, y, min(s * 9, s * 12.6), T3 + 9, y + 2.2, max(s * 9, s * 12.6))
    wall_z(k, "painteddrywall", -HZ, HZ, 36, y, D2 - 0.4, [(0, 2.4, 2.6)])
    for s in (-1, 1):
        wall_x(k, "painteddrywall", 36, XB, s * 1.4, y, D2 - 0.4, [(40, 1.4, 2.4), (46, 1.4, 2.4)], t=0.2)
        wall_z(k, "painteddrywall", min(s * 1.4, s * HZ), max(s * 1.4, s * HZ), 43, y, D2 - 0.4, t=0.2)
        for x in (38.5, 45):
            k.box("cratewood", x, y, s * 9 - 1, x + 2.5, y + 1.6, s * 9 + 1)
    wall_z(k, "painteddrywall", -HZ, HZ, XB, y, D2 - 0.4, [(0, 1.4, 2.4)])
    k.box("chainheap", 60, y, -1.5, 64, y + 0.8, 1.5)

    # ================================================================== deck 2: cabins
    y = D2
    xs_aft = [-60 + 3 * i for i in range(12)]          # x -60..-24
    xs_fore = [12 + 3 * i for i in range(12)]          # x 12..48
    for s in (-1, 1):
        cabin_row(k, xs_aft, s, y, {1, 4, 9} if s < 0 else {2, 6, 10})
        cabin_row(k, xs_fore, s, y, {3, 7, 11} if s < 0 else {0, 5, 8})
        # the suites round the atrium (x -24..12, hull side): three rooms 12 m wide, a door each, a bed
        for (x0, x1) in ((-24, -12), (-12, 0), (0, 12)):
            wall_x(k, "wainscotpanelling", x0, x1, s * 9.0, y, 3.0, [(x0 + 3, 1.4, 2.4)], t=0.12)
            k.box("wainscotpanelling", x1 - 0.06, y, min(s * 9, s * HZ), x1 + 0.06, 3.0, max(s * 9, s * HZ))
            k.box("quiltedleather", x0 + 5, y, min(s * 10.5, s * 12.9), x0 + 8, y + 0.6, max(s * 10.5, s * 12.9))
        # the inner side (z +-0.2..6.6): rooms off the corridor between the towers and the atrium
        inner = [(-66, -52), (-42, -31), (-31, -20), (-10, -6), (6, 16), (16, 26), (36, 43), (43, XB)]
        for (x0, x1) in inner:
            a, b = sorted((s * 0.2, s * 6.6))
            wall_x(k, "painteddrywall", x0, x1, s * 6.6, y, 3.0, [((x0 + x1) / 2, 1.4, 2.4)], t=0.2)
            k.box("painteddrywall", x0 - 0.1, y, a, x0 + 0.1, 3.0, b); k.box("painteddrywall", x1 - 0.1, y, a, x1 + 0.1, 3.0, b)
        for (x0, x1) in ((T1 + 2, T1 + 8), (T2 + 2, T2 + 8), (T3 + 2, T3 + 8)):     # rooms beside the tubes
            wall_x(k, "painteddrywall", x0, x1, s * 6.6, y, 3.0, [((x0 + x1) / 2, 1.2, 2.4)], t=0.2)
            for x in (x0, x1):
                k.box("painteddrywall", x - 0.06, y, min(s * 2.6, s * 6.6), x + 0.06, 3.0, max(s * 2.6, s * 6.6))
        for x in (-63, -36, -25, 10, 20, 40, 47):
            k.box("cratewood", x - 1, y, s * 3.8 - 1, x + 1, y + 1.4, s * 3.8 + 1)
    k.box("painteddrywall", -66, y, -0.2, -52, 3.0, 0.2); k.box("painteddrywall", -42, y, -0.2, -20, 3.0, 0.2)
    k.box("painteddrywall", -10, y, -0.2, -6, 3.0, 0.2); k.box("painteddrywall", 6, y, -0.2, 26, 3.0, 0.2)
    k.box("painteddrywall", 36, y, -0.2, XB, 3.0, 0.2)
    # fire-door dog-legs in both corridors: three, so the corridors are four legs of 25-35 m (fewer stops for tag)
    for x in (-40.5, -14, 22.5):
        for s in (-1, 1):
            chicane(k, x, s)
    # the aft lobby (x -72..-66) and the bow hold (x 50..75): open rooms joining both corridors
    wall_z(k, "painteddrywall", -HZ, HZ, -66, y, 3.0, [(-7.8, 2.0, 2.6), (7.8, 2.0, 2.6), (0, 2.4, 2.6)])
    wall_z(k, "painteddrywall", -HZ, HZ, XB, y, 3.0, [(-7.8, 2.0, 2.6), (7.8, 2.0, 2.6)])
    for (x, z) in ((56, -3), (58, 3), (62, 0)):
        k.box("palletstack", x - 1, y, z - 1, x + 1, y + 2.0, z + 1)
    # the atrium at deck 2: open to both corridors (6 m), its stairs, the glass lift
    for s in (-1, 1):
        k.box("painteddrywall", -6, y, min(s * 6.4, s * 6.6), -3, 3.0, max(s * 6.4, s * 6.6))
        k.box("painteddrywall", 3, y, min(s * 6.4, s * 6.6), 6, 3.0, max(s * 6.4, s * 6.6))
    k.stairs("marble", -4, 3.6, 3, 6, D3, "x", 1, base=D2)                          # deck 2 -> 3 (south side)
    k.stairs("marble", -1, -6, 6, -3.6, D4, "x", -1, base=D3, solid=False)           # deck 3 -> 4 (north side)
    k.box("glass", -1, D2, -1, 1, D4 + 2.0, 1)

    # ================================================================== deck 3: the promenade deck
    y = D3
    # the superstructure's sides (z +-10) with doors and windows onto the open promenades (z +-10..13)
    for s in (-1, 1):
        doors = [(x, 2.0, 2.6) for x in (-62, -38, -14, 8, 30)] + [(x, 4.0, 2.4, 1.0) for x in (-50, -26, -2, 20, 42)]
        wall_x(k, "whiteconcrete", -66, XB, s * 10.0, y, D4 - 0.4, doors, t=0.3, glass="glass")
    wall_z(k, "whiteconcrete", -10, 10, -66, y, D4 - 0.4, [(0, 2.4, 2.6), (-6, 4.0, 2.2, 1.0), (6, 4.0, 2.2, 1.0)], t=0.3, glass="glass")
    wall_z(k, "whiteconcrete", -10, 10, XB, y, D4 - 0.4, [(0, 2.4, 2.6), (-5, 3.0, 2.2, 1.0), (5, 3.0, 2.2, 1.0)], t=0.3, glass="glass")
    # the shopping arcade (x -66..-24): shops on both sides of a central walk (z -4..4), doors onto it
    for s in (-1, 1):
        xs = [-66, -57.6, -49.2, -40.8, -32.4, -24]
        wall_x(k, "glassblock", -66, -24, s * 4.0, y, D4 - 0.4, [((a + b) / 2, 2.0, 2.6) for (a, b) in zip(xs, xs[1:])], t=0.2)
        for x in xs[1:-1]:
            k.box("painteddrywall", x - 0.1, y, min(s * 4, s * 10), x + 0.1, D4 - 0.4, max(s * 4, s * 10))
        for x in xs[:-1]:
            k.box("cratewood", x + 1.5, y, min(s * 8.4, s * 9.6), x + 6.5, y + 1.6, max(s * 8.4, s * 9.6))   # shelves
    k.box("bluepooltile", POOL[0], D4 - 1.4, POOL[1], POOL[2], D4 - 1.0, POOL[3])        # the pool's basin (the arcade under it: 2 m)
    # the casino (x -24..12) round the mid tower and the atrium: slot rows, card tables
    wall_z(k, "stripedwallpaper", -10, 10, -24, y, D4 - 0.4, [(0, 8.0, 2.8)], t=0.3)
    wall_z(k, "stripedwallpaper", -10, 10, 12, y, D4 - 0.4, [(0, 6.0, 2.8), (-7.5, 2.0, 2.6), (7.5, 2.0, 2.6)], t=0.3)
    for s in (-1, 1):
        for x0 in (-23, -9):
            k.box("controlpanel", x0, y, min(s * 7.4, s * 8.2), x0 + (12 if x0 < -10 else 2), y + 1.8, max(s * 7.4, s * 8.2))
        k.box("controlpanel", 7, y, min(s * 7.4, s * 8.2), 11, y + 1.8, max(s * 7.4, s * 8.2))
        k.disc("pooltablefelt", 9, s * 3.5, 1.2, y, y + 1.0, n=8)
    for (x0, z0, x1, z1) in [(-6, -6.1, 6, -6), (-6, 6, 3, 6.1), (-6.1, -6, -6, 6), (6, -3.6, 6.1, 3.6)]:
        k.box("glass", x0, y, z0, x1, y + 0.95, z1); k.box("brass", x0, y + 0.95, z0, x1, y + 1.1, z1)   # the atrium's rails
    # the dining room (x 12..50): tables you can crouch under (tops at 1.3), round the fore tower
    for x in (15, 20, 40, 45):
        for z in (-7.5, -4, 4, 7.5):
            k.box("wideplanks", x - 1, y + 1.3, z - 1, x + 1, y + 1.4, z + 1)
            k.box("wideplanks", x - 0.1, y, z - 0.1, x + 0.1, y + 1.3, z + 0.1)
    # the foredeck (x 50..75): a winch and the bollards; the aft deck (x -72..-66): a bench
    k.box("castiron", 58, y, -1.5, 61, y + 1.4, 1.5)
    for (x, z) in ((54, -9), (54, 9), (66, -4.5), (66, 4.5)):
        k.disc("castiron", x, z, 0.4, y, y + 0.8, n=8)
    k.box("wideplanks", -71, y, -4, -70, y + 0.5, 4)

    # ================================================================== deck 4: the lido
    y = D4
    for s in (-1, 1):   # rails along both edges (x -40..46: the white windscreen wall instead) and across the stern end
        for (xa, xb) in ((-66, -40), (46, XB)):
            k.box("chrome", xa, y, min(s * 12.85, s * 13), xb, y + 1.1, max(s * 12.85, s * 13))
        # the windscreen: the superstructure's white side carried up from the lido to the sun deck's height (a white
        # bulwark, blue glass panes between posts, a white band on top): the liner's stacked white decks from outside
        a, b = sorted((s * 12.7, s * 13.0))
        k.box("whiteconcrete", -40, y, a, 46, y + 1.1, b)
        k.box("whiteconcrete", -40, D5 - 0.6, a, 46, D5, b)
        g0, g1 = sorted((s * 12.8, s * 12.9))
        k.box("bluetintedglass", -40, y + 1.1, g0, 46, D5 - 0.6, g1)
        for i in range(21):
            x = -40 + 4.3 * i
            k.box("whiteconcrete", x - 0.15, y + 1.1, a, x + 0.15, D5 - 0.6, b)
    k.box("chrome", -66, y, -13, -65.85, y + 1.1, 13)
    k.box("chrome", XB - 0.15, y, -13, XB, y + 1.1, 13)
    # the pool (x -40..-24, z -4..4: 1.4 deep), its rim; the hot tub; the bar; the slide off its platform
    for (x0, z0, x1, z1) in [(-40, -4, -24, -3.7), (-40, 3.7, -24, 4), (-40, -3.7, -39.7, 3.7), (-24.3, -3.7, -24, 3.7)]:
        k.box("bluepooltile", x0, y - 1.0, z0, x1, y, z1)
    k.box("clearlagoon", -39.7, y - 1.0, -3.7, -24.3, y - 0.2, 3.7)
    for (x0, z0, x1, z1) in [(-20, 7, -15, 7.3), (-20, 10.7, -15, 11), (-20, 7.3, -19.7, 10.7), (-15.3, 7.3, -15, 10.7)]:
        k.box("bluepooltile", x0, y, z0, x1, y + 0.6, z1)
    k.box("hotspring", -19.7, y, 7.3, -15.3, y + 0.5, 10.7)
    k.box("wideplanks", 12, y, -11, 22, y + 1.1, -10); k.box("wideplanks", 21, y, -10, 22, y + 1.1, -6)
    for (x, z) in ((12, -11.5), (24, -11.5), (12, -5.5), (24, -5.5)):
        k.box("chrome", x - 0.1, y, z - 0.1, x + 0.1, y + 3.0, z + 0.1)
    k.box("corrugated", 11.5, y + 3.0, -12, 24.5, y + 3.2, -5)
    # the slide: a platform (x -18..-14, z -6..-2.6 at 10.2, a ladder up its east side) and a chute down west over
    # the pool's north edge (ends over the water at x -30)
    k.box("fibreglassgrating", -18, D5 - 0.3, -6, -14, D5, -2.6)
    for (x, z) in ((-17.8, -5.8), (-14.2, -5.8), (-17.8, -2.8), (-14.2, -2.8)):
        k.box("chrome", x - 0.1, y, z - 0.1, x + 0.1, D5 - 0.3, z + 0.1)
    k.box("chrome", -18, D5, -6, -14, D5 + 1.0, -5.9); k.box("chrome", -18, D5, -2.7, -14, D5 + 1.0, -2.6)
    k.ladder(-13.88, -4.3, y, D5, "w", mat="chrome")
    s_ = (D5 - 7.7) / 12.0
    k.cut("powdercoatyellow", (-30, 7.4, -4.6, -18, D5, -3.0), [(-s_, 1, 0, D5 + s_ * 18), (s_, -1, 0, -(D5 + s_ * 18) + 0.3 * math.sqrt(1 + s_ * s_))])
    for z in (-4.7, -3.0):
        k.cut("powdercoatyellow", (-30, 7.4, z, -18, D5 + 0.6, z + 0.1), [(-s_, 1, 0, D5 + 0.6 + s_ * 18)])
    k.box("chrome", -23.1, y, -3.9, -22.9, 8.6, -3.7)
    # the atrium's rails at deck 4 and its glass canopy at 10.2 on four posts
    for (x0, z0, x1, z1) in [(-6, 6, 6, 6.1), (-6.1, -3.6, -6, 6), (6, -6, 6.1, 6), (-1, -6.1, 6, -6)]:
        k.box("glass", x0, y, z0, x1, y + 0.95, z1); k.box("brass", x0, y + 0.95, z0, x1, y + 1.1, z1)
    for (x, z) in ((-6.8, -6.8), (6.6, -6.8), (-6.8, 6.6), (6.6, 6.6)):
        k.box("brass", x, y, z, x + 0.2, D5, z + 0.2)
    k.box("glass", -7, D5, -7, 7, D5 + 0.15, 7)
    # the tower tops on the lido (aft, mid): rails round the open well over the last flight
    for x0 in (T1, T2):
        k.box("chrome", x0 + 2, y, -2.6, x0 + 8, y + 1.1, -2.2); k.box("chrome", x0 + 2, y, 2.2, x0 + 8, y + 1.1, 2.6)
        k.box("chrome", x0 + 2, y, -0.06, x0 + 8, y + 1.1, 0.06)
    # the stair up to the sun deck (x 2..8, z 8..10.6) and the fore tower's walls up to it
    k.stairs("wideplanks", 2, 8, 8, 10.6, D5, "x", 1, base=D4, solid=False)
    # the funnel (octagon r 3.4, x -58, z 0, from the lido to 23.2): a door east, a free-standing panel inside with a
    # ladder up its east face to the top grate at 21.6 (over the panel's top onto the grate)
    FX, FZ = -58.0, 0.0
    k.ring("powdercoatyellow", FX, FZ, 3.0, 3.4, D4, D4 + 2.8, n=8, a0=-22.5, skip=(0,))
    k.ring("powdercoatyellow", FX, FZ, 3.0, 3.4, D4 + 2.8, 21.6, n=8, a0=-22.5)
    k.ring("anodizedblack", FX, FZ, 3.0, 3.4, 21.6, 23.2, n=8, a0=-22.5)
    k.box("galvanizedsteel", FX - 0.2, D4, FZ - 1.0, FX, 21.6, FZ + 1.0)
    k.ladder(FX + 0.12, FZ, D4, 21.6, "w", mat="galvanizedsteel")
    for (x0, z0, x1, z1) in minus((FX - 3.0, FZ - 3.0, FX + 3.0, FZ + 3.0), [(FX, FZ - 1.0, FX + 3.0, FZ + 1.0)]):
        k.box("galvanizedgrating", x0, 21.2, z0, x1, 21.6, z1)
    # deckchairs (low, along the rails: out of the lanes)
    for x in range(-62, -44, 3):
        for s in (-1, 1):
            k.box("wideplanks", x, y, min(s * 11.2, s * 12.6), x + 1.0, y + 0.45, max(s * 11.2, s * 12.6))

    # ================================================================== the sun deck (x 8..46, z -11..11 at 10.2)
    y = D5
    for s in (-1, 1):
        k.box("chrome", 8, y, min(s * 10.85, s * 11), 46, y + 1.1, max(s * 10.85, s * 11))
    k.box("chrome", 45.85, y, -11, 46, y + 1.1, 11)
    k.box("chrome", 7.85, y, -11, 8, y + 1.1, 7.8); k.box("chrome", 7.85, y, 10.7, 8, y + 1.1, 11)
    # the fore tower's top: rails round the well
    k.box("chrome", T3 + 2, y, -2.6, T3 + 8, y + 1.1, -2.2); k.box("chrome", T3 + 2, y, 2.2, T3 + 8, y + 1.1, 2.6)
    k.box("chrome", T3 + 2, y, -0.06, T3 + 8, y + 1.1, 0.06)
    # lifeboats on davits (x 10..16 and 18..24, z +-6.6..9.4): open hulls under a cargo-net cover, one way in (aft)
    for x0 in (10, 18):
        for s in (-1, 1):
            a, b = sorted((s * 6.6, s * 9.4))
            k.box("powdercoatyellow", x0, y + 0.2, a, x0 + 6, y + 0.4, b)
            k.box("powdercoatyellow", x0, y + 0.2, a, x0 + 6, y + 1.8, a + 0.12); k.box("powdercoatyellow", x0, y + 0.2, b - 0.12, x0 + 6, y + 1.8, b)
            k.box("powdercoatyellow", x0 + 5.88, y + 0.2, a, x0 + 6, y + 1.8, b)          # (the aft end open: the way in)
            k.box("cargonet", x0, y + 1.8, a, x0 + 6, y + 1.9, b)
            for x in (x0 + 0.5, x0 + 5.5):
                k.box("galvanizedsteel", x - 0.1, y, (a + b) / 2 - 0.1, x + 0.1, y + 0.2, (a + b) / 2 + 0.1)
                k.box("galvanizedsteel", x - 0.1, y, min(s * 10.4, s * 10.6), x + 0.1, y + 3.2, max(s * 10.4, s * 10.6))
                k.box("galvanizedsteel", x - 0.1, y + 3.0, min(s * 8, s * 10.6), x + 0.1, y + 3.2, max(s * 8, s * 10.6))
    # the deckhouse (x 25..36 either side of the fore tower's well, two storeys to 15.8): dark windows below, a row of
    # balconies above (slabs, glass-railed, white fins between them) facing the sea; its roof a grapple-only perch
    for s in (-1, 1):
        a, b = sorted((s * 5.4, s * 9.4))
        k.box("whiteconcrete", 25, y, a, 36, y + 5.6, b)
        k.box("whiteconcrete", 24.8, y + 5.6, min(s * 5.2, s * 10.6), 36.2, y + 5.8, max(s * 5.2, s * 10.6))
        f0, f1 = sorted((s * 9.4, s * 9.45))
        for i in range(5):
            x = 25.6 + 2.2 * i
            k.box("anodizedblack", x, y + 0.9, f0, x + 1.6, y + 2.2, f1)            # lower windows
            k.box("anodizedblack", x + 0.2, y + 2.95, f0, x + 1.4, y + 5.0, f1)     # balcony doors
        p0, p1 = sorted((s * 9.4, s * 10.4))
        k.box("whiteconcrete", 25, y + 2.8, p0, 36, y + 2.95, p1)                   # the balcony slab
        r0, r1 = sorted((s * 10.33, s * 10.4))
        k.box("bluetintedglass", 25, y + 2.95, r0, 36, y + 3.9, r1)
        k.box("chrome", 25, y + 3.9, r0, 36, y + 4.0, r1)
        for i in range(6):
            x = 25 + 2.2 * i
            k.box("whiteconcrete", x - 0.08, y + 2.95, p0, x + 0.08, y + 5.6, p1)   # fins
    # the bridge (x 38..46, z -10..10): windows forward and on its wings, a door aft, the helm; the radar mast on top
    wall_x(k, "whiteconcrete", 38, 46, -10, y, y + 3.0, [(42, 6.0, 2.4, 1.0)], t=0.3, glass="glass")
    wall_x(k, "whiteconcrete", 38, 46, 10, y, y + 3.0, [(42, 6.0, 2.4, 1.0)], t=0.3, glass="glass")
    wall_z(k, "whiteconcrete", -10, 10, 38, y, y + 3.0, [(0, 1.6, 2.4)], t=0.3)
    wall_z(k, "whiteconcrete", -10, 10, 45.7, y, y + 3.0, [(-5, 6.0, 2.4, 1.0), (5, 6.0, 2.4, 1.0)], t=0.3, glass="glass")
    k.box("whiteconcrete", 37.7, y + 3.0, -10.3, 46.3, y + 3.3, 10.3)
    k.box("controlpanel", 43, y, -4, 45, y + 1.1, 4); k.disc("brass", 41.5, 0, 0.5, y, y + 1.2, n=8)
    k.box("galvanizedsteel", 41.8, y + 3.3, -0.2, 42.2, 20, 0.2)
    k.box("galvanizedsteel", 41.9, 18.5, -3, 42.1, 18.8, 3)

    decorate(k)

    # ================================================================== lights: corridors every 9 m, rooms, deck lamps
    L = []
    for x in range(-63, 50, 9):
        for s in (-1, 1):
            L.append((x, D2 + 2.6, s * 7.8, 7, 0.7))
    for x in (-60, -45, -30, 0, 18, 30, 44):
        L.append((x, D3 + 2.6, 0, 10, 0.55))
    for x in (-62, -46, 20, 31, 44, 60):
        L.append((x, D1 + 2.6, 0 if x > 0 else 6, 9, 0.5))
    L += [(-64, D1 + 2.6, -6, 8, 0.5), (-46, D1 + 2.6, -10.8, 8, 0.45), (-46, D1 + 2.6, 10.8, 8, 0.45),
          (-29, -1.5, 0, 14, 0.5), (0, -1.5, 0, 12, 0.45), (-51, -5, 0, 8, 0.4), (7, -4.5, -9, 6, 0.5),
          (-58, D2 + 2.6, 0, 8, 0.5), (60, D2 + 2.6, 0, 9, 0.5), (60, D1 + 2.6, 0, 6, 0.4),
          (-47, D2 + 2.6, 0, 6, 0.4), (-15, D2 + 2.6, 0, 6, 0.4), (31, D2 + 2.6, 0, 6, 0.4), (0, D2 + 2.6, 4, 10, 0.55),
          (-18, 21.4, 0, 2, 0.1)]
    for (x0, x1) in ((-24, -12), (-12, 0), (0, 12)):
        for s in (-1, 1):
            L.append(((x0 + x1) / 2, D2 + 2.6, s * 11, 7, 0.45))
    for s in (-1, 1):
        L.append((-36, D2 + 2.6, s * 3.4, 7, 0.4)); L.append((21, D2 + 2.6, s * 3.4, 7, 0.4))
    for x in (-32, 0, 30):
        L.append((x, D4 + 3.0, 0, 16, 0.4))
    L.append((-32, D4 - 0.4, 0, 7, 0.5))     # the pool's underwater light
    L.append((42, D5 + 2.6, 0, 8, 0.5))
    for (x, y, z, r, i) in L:
        if (x, y, z) == (-18, 21.4, 0): continue
        k.light(x, y, z, r, i)

    # ================================================================== spawns: deck 3, the promenades, bow to stern
    for (x, z) in ((-58, -11.5), (-34, 11.5), (-10, -11.5), (16, 11.5), (38, -11.5), (-58, 11.5), (-22, -11.5), (36, 11.5)):
        k.spawn("any", x, D3, z, yaw_to(x, z, x + (10 if z > 0 else -10), z))
    # free for all spreads out over the other decks (ffa-any, format 11)
    for (x, y, z, tx, tz) in [(-64.5, D1, 6.5, -64.5, 0), (18, D1, -9, 18, 0), (-45, D2, 7.8, -30, 7.8), (30, D2, -7.8, 15, -7.8),
                              (-69, D2, -4, -69, 4), (-10, E, 0, 0, 0), (-50, D4, -9, -40, -9), (30, D4, 8, 20, 8),
                              (0, D3, 8.5, 10, 8.5), (-70, D3, 8, -70, 0)]:
        k.spawn("ffa-any", x, y, z, yaw_to(x, z, tx, tz))
    m.save("cruiseship", dress=False)
