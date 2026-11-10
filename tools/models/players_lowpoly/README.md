# Low-poly players: how to write one

The low-poly set remakes each box design (`tools/models/characters_outlined*.py`) as faceted meshes with an ink
outline. One module per character: `players_lowpoly/<name lower>.py`, with `DESC` (the box design's description) and
`build()` returning a `kit.Char`. `gen_players_lowpoly.py` finds the modules by name and skips the names that have none.
Samples: `standard.py`, `heavy.py`, `knight.py`, `ninja.py`.

```
python3 tools/models/gen_players_lowpoly.py --only NAME [--out DIR] [--scratch DIR] [--no-preview] [--outline MM]
```
Without `--out` it writes `/mnt/project-files/player-models/lowpoly/` (models/player/NAME.gltf, lineup-lowpoly*.png,
walk-lowpoly.png, box-vs-lowpoly.png). Use `--out <your scratch>/` while you iterate. The output is deterministic.

## Writing a character
- Units are mm. Feet on the floor at the origin, facing -Z, +X is the model's right; `_l` is the model's LEFT (-X).
- `ch = Char(NAME, DESC, Rig(hip, hipx, knee, ankle))`. The joints become the anim_ nodes' pivots, which the game
  reads back (gltf.cpp `playerMeshRig`). Take them from the box design: hip = top of the thigh boxes, knee = their
  bottom, ankle = top of the foot boxes. The neck stays 1440.
- `b = body(ch, legs=, shins=, feet=, pelvis=, torso=, head=, ...)` builds the 9 parts. Knobs: `bulk`, `thigh`,
  `shin`, `foot`, `pelvis_w/_d`, `waist`/`chest`/`shoulder`, `chest_y`, `head_w/_d`, `head_top`, `jaw`, `crown`.
  `calf=False` and `thigh_mid=False` each save 48 triangles. `head=None` skips the kit's head so you can build your own.
- Props (kit.py): `pauldron` (shoulder pads; the game draws the arms from the shoulders, so models have no arms),
  `b.wrap(piece, y0, y1, tone)` (belts, cuffs, headbands, helm bands; `"thigh_"` does both sides), `band` (a panel
  that follows the front or back of the torso, pelvis or head: tabards, chest plates), `visor`, `helmet`, `brim`,
  `plate` (flat, front or back), `profile` (crests, plumes, blades), `crest`, `backpack`, `cape`, `ch.rod`, and
  `ch.sweep`/`ch.solid`/`ch.loft` for anything else. Put a piece on a bone by naming the bone. `ch.placed(bone, R, about)`
  tilts the pieces built inside it. `ch.sym(fn)` calls `fn(sd, "_r"/"_l")` with sd +1 for the right and -1 for the left.
- Tones are the box set's (`gen_players_outlined.TONES`), so the colours match exactly: dark, mid_dk, mid, light,
  bore, team, team_dk, visor, eye, skin, and the rest. `kit.tone(name, srgb)` adds a fixed colour. Its name must not
  start with team/glow/metal/base/accent/body, because the game picks the tint slot by name prefix and drives only
  team on players.
- Small flush details (`shell=False`) get no outline of their own and cost half as much. Give anything that sticks
  out of the silhouette a shell.

## The rules (the generator checks them: ERROR fails, warning is printed)
1. At most **1000 triangles**, counted in the file and including the ink hulls (every shelled piece costs double).
   The bare body is about 600, so you have about 400 for props. See where they go with
   `{b: {k: len(v) for k, v in m.tris.items()} for b, m in ch.mesh.items()}`.
2. **Size (q, 2026-11-09: "+-10%, in any dimension")**: aim to fill the hitbox nearly exactly. Every vertex, the
   outline included, must stay inside the hitbox grown by 10%: the body cylinder (r 440 mm) below the neck line at
   1440 mm, the head cylinder (r 198 mm) above it. Extents are checked per dimension against the hitbox. Body: X and Z
   against an 800 mm diameter, Y against 1440 mm. Head: X and Z against 360 mm, height above the neck against 360 mm.
   Over 110% fails. Under 90% in X or Y fails. Under 90% in Z (depth) only warns, and every slim body warns there, so
   just report it. In practice: shoulders (pauldrons) reach about ±370 mm, the head is about 330 mm wide, and its top
   with crests, hats and plumes sits at 1765-1830 mm. Keep anything wider than the head under 1440 mm, outline
   included (the torso's top is 1418 mm).
3. **Walking**: all 9 anim_ nodes, with the rig's pivots. Check the walk preview and the game (`--walk`).
4. **Colours**: every non-exempt tone at least sRGB 0.18 (fails). These visibility rules only warn:
   - team colour on at least **35%** (aim for 40%) of the front + back view, on the limbs and head as well, not only
     a chest panel;
   - big non-team areas at least sRGB **0.35**: under 10% of the view darker than 0.35, under 25% darker than 0.25;
   - mean dE >= **35** and C* >= **20** against a dark and a bright backdrop.

   Near-black only for ink and small details (eyes, slits). "dark" (0.36) is the darkest big area allowed.
5. **Outline**: an inverted hull, `kit.OUTLINE` = **10 mm** (the rule's range is 6-15 mm), near-black `ink`, never
   team-tinted (the game recolours and rescales a player's `ink` from the outline settings). Leave it alone unless q asks.
6. **Limbs** at least 120 mm thick (the kit's defaults are).
7. No crouch. Deterministic, no randomness. AI-made placeholders (the file carries the provenance).

## Before you hand in
- `--only NAME` prints no ERROR, and you report the warnings you couldn't fix (Z depth is expected).
- Look at the previews: is it the same character as its box version (colours, props, silhouette)? Does the team
  colour read on the legs and head?
- In the game: copy the .gltf to `<data dir>/overrides/models/player/` and run
  `DISPLAY=:99 build/quark --data-dir <data dir> --host ffa --map spire --walk --screenshot out.png`. `--walk` lines
  up page 0's 11 models side-on and walking; `--modelpage N` shows models 11N to 11N+10, and `--models` faces them.
- The set is SHIPPED (2026-11-10): `python3 tools/models/gen_players_lowpoly.py --out assets/overrides --no-preview`
  writes the game's default players (commit them with the module). Never edit kit.py's defaults without telling the
  coordinator (every character uses them, and the shipped files change with them).
