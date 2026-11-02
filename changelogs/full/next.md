# Next (full, testing builds since 1.19.5)

Every change on `testing` since production 1.19.5, with what changed and why. The release PR renames this file to `changelogs/full/<VERSION>.md`.

## Black Hole: pillars and more space (owner, 2026-11-02: "increase the outer decoration on black hole, and add some pillars on the platform to increase visibility")

- **Pillars.** Four 1 x 1 x 7 m basalt pillars at (±8, ±5) with a glowing band 1 m under each top (`tools/maps/blackhole.py` `pillars`): something to judge height and drift by while flying, against the black sky. Too thin to be cover; the map checks still pass (16 m² of pillar tops only flight reaches, none out of bounds).
- **Outer decoration** (`space`), all non-solid (lava and far scenery), so the map's playable bounds and `Server::flownOut`'s 40 m margin are unchanged: two 3 m jets up and down out of the hole's poles (about 106 m each), a broken debris ring 100-112 m round the disc (every other 10°, far side only), and ten stray rocks as dark silhouettes 60-140 m out, some between the rock and the disc.
