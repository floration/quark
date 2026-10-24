# Assets: the export and overrides

The owner replaces the AI-made default assets with human-made ones. Three pieces make that work:

1. **The export** (`build/quark_tests exportassets <dir>`) writes every default asset as a file, plus
   `manifest.json` listing them.
2. **The owner's tools** (the model, sound and other editors in the project supervisor) show the assets and write
   replacements. (The Asset Studio page that did this was retired 2026-10-24.)
3. **Overrides**: a file at an asset's export path under `assets/overrides/` (shipped) or the per-user
   `overrides/` folder (this computer only) replaces that asset.

## The export

```
cmake --build build -j
build/quark_tests exportassets build/assets_export
python3 tools/studio/build.py build/assets_export     # -> build/studio/ (+ manifest.json), the website /assets/ page's data (tools/site_assets.py)
```

| Category | Files | Path (= the override path) |
|---|---|---|
| Sounds | 33 sounds x 5 kits = 165 WAV (48 kHz mono) | `sounds/<kit>/<SOUND>.wav`, kit = `default` `heavy_metal` `magical` `retro` `mouth` |
| Voices | 27 announcer lines x 7 voices = 189 WAV | `voices/<voice>/<LINE>.wav`, voice = `male` `female` `british` `old_man` `e_girl` `india` `spy` |
| Quips | each voice's quip set (today the default: 8 x 7 = 56 WAV) | `voices/<voice>/quips/<NAME>.wav` |
| Models | 5 players, 11 weapon viewmodels, 7 projectiles, the crown, 6 props = 30 glTF | `models/player/<NAME>.gltf`, `models/weapon/<NAME>.gltf`, `models/projectile/<NAME>.gltf`, `models/crown/CROWN.gltf`, `models/prop/<NAME>.gltf` |
| Maps | every shipped map (.qmap) | `maps/<name>.qmap` |
| Fonts | the default UI font (Rubik), WORK SANS and the game-font look-alikes of Settings > FONT | `fonts/Rubik.ttf`, `fonts/WorkSans.ttf`, `fonts/<file in assets/fonts>` (`BebasNeue-Regular.ttf`, `LibreQuake-CONCHARS.png`, ...) |
| Images | the game icon | `images/icon.png` |
| Website | the site's pictures | `website/<file>` (from `site/img/`) |

Sound names are the SND_* names (`HIT`, `ROCKET`, `GRAPPLE_FIRE` ... `assets/sfx/custom/README.md` says when each
plays; the hitsound is four: `HIT` under 10 damage, `HIT_MED` 10 to 24, `HIT_HEAVY` 25 to 50, `HIT_BRUTAL` over 50, each
replaced at the loudness of its tier, as heard, so the four stay in order); voice lines are `THREE` `TWO` `ONE` `GO` `BOMB_PLANTED` ... `ROUND_OVER` (src/client/audio.cpp). A line a
voice has no recording of would be exported as a tenth of a second of silence (it is silent in the game), so it can still
be added; today every voice has all 27.

**Quips** (the QUIP key, G: a line a player says out loud where they stand) are their own sounds, never a gameplay
call-out, with their own setting (Settings > VIDEO + AUDIO > SOUND: QUIPS on / off and QUIPS VOLUME, apart from the
announcer's). Each voice has its own set. Until recorded quips exist, a voice's default set is its own hype lines
(`AIR_ROCKET` `GODLIKE` `HEADSHOT` `HIGH_SCORE` `NEW_LEADER` `RAMPAGE` `SPREE` `UNSTOPPABLE`, as that voice says them,
its line overrides included), exported as `voices/<voice>/quips/<NAME>.wav`. **Any** quip files for a voice
(`voices/<voice>/quips/<any name>.<wav|ogg|flac|mp3>`, shipped and yours together, yours winning a name both have) are
that voice's whole set, sorted by name, replacing the default set (not just the quip of the same name): to keep some
default quips, ship those files too (the export has them). At most 64 per voice; files in sub-folders aren't quips.
Each is matched to the loudness of the default quip of its name (any other name: the voice's HEADSHOT line), unless
`sound-levels.txt` lists it (`voices/male/quips/LAUGH`), cut at 5 s. Which quip plays: the server picks a number for the
quip, the same for everyone, and each listener plays number modulo their own voice's set (players hear quips in their
own VOICE, like the announcer), so everyone with the same voice and quip files hears the same quip.

The export is what the game uses now: the built-in assets with the shipped overrides applied, never this
computer's own overrides folder.

Models are the game's box models (src/client/models.cpp, the same lists drawWorld draws: every model in the game,
everything but the map, explosions, tracers / beams and the zone volumes), one glTF primitive per colour slot, in each
model's own space (metres, +Y up, front at -Z):

- **player**: feet on the ground at the origin, 2 m tall, facing -Z. Every model shares one hitbox: stay inside it.
- **weapon**: the first-person viewmodel. Origin at the weapon's pivot (the HUD's viewmodel x / y / depth put it
  there and its size scales it), barrel along -Z.
- **projectile**: origin at the projectile, flying toward -Z (ROCKET, KNIFE, STICKY, CROSSBOW, BUBBLE, SYRINGE, ORB =
  every other projectile). The game spins the sticky and the thrown knife and wobbles the bubble.
- **crown**: the leaderboard crown, origin at the top of the head, front at -Z (the board letter stays drawn on top).
- **prop** (`models/prop/<NAME>`), the rest of what the game draws as a model; the game keeps moving them (where they
  are, aim, crouch, body size are transforms on top of the model):
  - `GUN_ARM`: the arm and gun every other player holds out (one dark block). Origin at the shoulder, reaching along
    -Z (0.7 m); turned with their aim (yaw and pitch) and scaled with their body (duel's double size).
  - `ARM`: the arm that holds out a weapon model (hide and seek / tag: whoever is it). Origin at the shoulder, along
    -Z; the hand, where the weapon's own model sits (at twice its viewmodel size), is at z -0.5.
  - `BOMB_PACK`: the bomb on its carrier's back, in the player's space (feet at the origin, facing -Z: the pack sits at
    +Z, 1.15 m up). An override is squashed with a crouch like the player model.
  - `BOMB`: the bomb on the ground, dropped or planted. Origin on the ground under it, never turned.
  - `CART`: the payload cart. Origin on the track under its middle, front (-Z) the way it's being pushed.
  - `GATE`: a payload checkpoint (both posts, either side of the track at x +-1.8). Origin on the track at the
    checkpoint, -Z the cart's way. (The default's posts stay square to the world; an override turns with the track.)

Material names pick a colour that changes at run time (prefix, any case: `team`, `Team_cloth`, `glow.001`):

| Material | Follows | Draw it in |
|---|---|---|
| `team*` | players / projectiles: the team colour; weapon: your accent colour; crown: gold (#1) or silver; cart: the pushing team (grey when neutral); gate: grey, the attackers' colour once reached | white = exactly that colour (crown: the export's gold; gate: the export's grey) |
| `glow*` | the map's glow colour; bomb: red while a planted one blinks | the export's orange (bomb: the export's dark grey) |
| `metal*`, `base*` | the map's metal / base colours (viewmodels, the cart's metal) | the export's colours |
| `accent*` | the map's accent colour (the cart's rim) | the export's colour |
| anything else | nothing: drawn as authored | |

An override's colour in a slot is multiplied by (the current colour / the colour shown in the export), so a model drawn
in the exported colours looks like the default. A slot the model's drawing doesn't colour (`glow`, `metal`, `base`,
`accent` on a player or the crown; `metal` / `base` / `accent` on a projectile; any slot on an arm or the backpack) is
drawn as authored (tint 1). Vertex colours (COLOR_0) times the material's baseColorFactor give the colour; textures
are ignored. A material with alphaMode BLEND is drawn see-through (like the bubble's shell).

### Box models

Every exported model is also a **box model**: its first mesh carries the box list in `meshes[0].extras.quark`. An
override whose file has it (version 2) is drawn by the game's own box path, exactly like a built-in model: 1 px
outlines, a crouch squashing only the boxes it squashes, `visOnly` boxes hidden on your own body, per-box shadows, the
same colour formulas. The triangles in the file are then only for other viewers (Blender, the website, the model
editor); the game reads the boxes. An export put back unchanged as an override draws pixel-identically to the default.
Anything else (no extras, another version) is drawn as a mesh, as before. A box list that fails the checks below is
logged and the default is kept. Parsed once at startup; drawing costs what the built-in boxes cost.

```
"extras": {"quark": {"version": 2, "boxes": [
  {"c": [x, y, z], "h": [hx, hy, hz],           // centre and half size, metres, model space
   "slot": "team",                              // body | team | glow | metal | base | accent (default body)
   "color": [r, g, b],                          // linear colour as exported (the reference palette)
   "mul": [r, g, b], "add": [r, g, b],          // slot boxes: drawn = slot colour * mul + team colour * teamMul + add
   "teamMul": [r, g, b],                        // (optional, default 0: another slot's box that also takes team colour)
   "alpha": 1,                                  // (optional) < 1: see-through (projectiles)
   "shadow": true, "visOnly": false,            // casts a shadow; drawn only seen from outside (face plates)
   "crouch": false,                             // players: a crouch also squashes the half height
   "name": "legs"}                              // (optional, ignored by the game)
]}}
```

- Without `mul`, a slot box is tinted like a mesh: mul = color / the export's colour of that slot, add 0. An editor
  that recolours a box should rewrite `mul` / `add` (or drop them).
- Players: every box's centre height follows a crouch; its half height too when `crouch` is true. The default
  STANDARD's legs and body have it, the head and face plate don't.
- Checks: 1 to 256 boxes; every number finite; `c` within 64 m; `h` over 0 and at most 32 m; `color`, `mul`, `add`,
  `teamMul` 0 to 16; `alpha` over 0, at most 1; known slot names; booleans true / false. Numbers are written with 9
  significant digits, so floats come back exactly.
- Not animated in a box override: the rocket's flame flicker, the sticky's core blink and the bubble's wobble (the
  game spins and turns it as usual).

The website shows the same export: https://floration.github.io/quark/assets/ (every model and map in 3D) and
`/sounds/` (every sound and voice line), each with a PRODUCTION / TESTING switch and a "make your own" mode (a box
builder that exports .glb, a synth that saves .wav). `python3 tools/site_assets.py` fills `site/assets/data/production/`
from a local build; CI publishes production with the site and testing with each testing build (release.yml).

## Overrides

A file at `<overrides>/<export path>` replaces that asset. Two places, the user's winning:

- **shipped**: `assets/overrides/<path>` in the repository. Embedded in the game at build time
  (tools/embed_overrides.py), so it ships with the next release as the new default. Maps, the icon and website
  pictures are build steps instead (below).
- **yours**: `<user data folder>/overrides/<path>` (Windows `%APPDATA%/quark`, macOS `~/Library/Application
  Support/quark`, Linux `~/.local/share/quark`; `--data-dir` / `QUARK_DATA_DIR` move it). Read at startup: try an
  asset without rebuilding. **Ranked matches and time trials ignore your models** (players there see the shipped
  models, or the default boxes): a model of your own could make enemies bigger or brighter for you alone. Both sets
  are loaded at startup (`ModelOverrides`, models.h) and the game picks one once a frame (`rankedPlayer()`: spectators
  of a ranked match still see yours). Your sounds and font still apply there.

Matching ignores case, and the extension may be any the asset accepts (an upload keeps its own: `ROCKET.ogg` replaces
`ROCKET.wav`):

| Asset | Accepted | What happens |
|---|---|---|
| sounds | .wav .ogg .flac .mp3 | Mono, leading silence trimmed, cut at 3 s, as loud as the sound it replaces (kits stay fair). A DEFAULT override also becomes every kit's sound that was DEFAULT's, and RETRO's (chiptuned). Players' own `mods/sounds/` still win over it. |
| voices | .wav .ogg .flac .mp3 | The same, cut at 5 s, as loud as the line it replaces |
| sound levels | `sound-levels.txt` (at the overrides root) | Sounds and voice lines that keep their own level: one asset path a line without extension (`sounds/default/ROCKET`, `voices/male/GO`), any case, `#` comment lines. A listed file is still made mono, trimmed, cut and faded, but not matched to the sound it replaces (the sound editor levels groups by LUFS, and matching would undo it); only a peak over 1.0 is scaled down to 1.0. A kept DEFAULT stays at its level in the kits it becomes. Your file's lines add to the shipped one's. Embedded like the rest (the one .txt that is). Players' mods: `mods/sounds/sound-levels.txt`, paths relative to it (`default/ROCKET`, `all/ROCKET`). |
| models | .glb .gltf | Drawn in place of the box model: a box model file (Box models, above) exactly as the game draws boxes; any other mesh lit and shadowed, with no outline. Buffers embedded or in files next to the .gltf. At most 20 000 triangles a model (they stream every frame); a bad file is logged and the default kept. |
| font | `fonts/Rubik.ttf` / `.otf` (the default UI font), `fonts/WorkSans.ttf` / `.otf` (the WORK SANS entry of Settings > FONT) | That font's distance-field atlas is rasterised from it when it is in use (stb_truetype: at startup for the default, on picking it for WORK SANS), same layout as tools/make_font_atlas.py, sized so its capitals are as tall as Rubik Bold's. Upload a static font of the weight to use (a variable font gives its default instance: Rubik's is Light, Work Sans's Regular). Up to 1.18 `fonts/WorkSans.*` replaced the UI font; it now replaces only the WORK SANS entry (Rubik is the default). A look-alike's file name (`fonts/BebasNeue-Regular.ttf`, `fonts/LibreQuake-CONCHARS.png` a 16x16 sheet, ...) replaces that look-alike; a player's own copy of the real game font still comes first. |
| icon | `images/icon.png` / `.jpg` | Shipped: run `python3 tools/make_icon.py` (Pillow) after adding it: it makes assets/quark.png / .ico, the launcher's and the site's icon from it and records it in assets/quark.icon-source (quark_tests fails until that matches). In the game: the window icon. The menus' QUARK title is drawn like the drawn icon (not from an override): `tools/make_logo.py` (Pillow + numpy) bakes it from `assets/fonts/Rubik.ttf` at wght 800 into `src/client/logo_data.h` (an RGBA PNG of masks: the friendly half of the Q + UARK, the enemy half, the black outline and seam), which the game tints with the player's team colours (the app icon, launcher and website keep the fixed cyan / red); re-run it after changing the font or the icon's look. With another UI font picked (Settings > FONT) the title is that font at the same size, its Q split the same way by the text shader. |
| maps | `maps/<name>.qmap` | Shipped: tools/make_maps.py writes the override instead of generating that map and tools/embed_maps.py embeds it (an official map: ranked uses its sha256). It counts as human-made: quark_tests runs every map check on it and reports the numbers, labelled HUMAN-MADE, without failing on them ([MAP_METRICS.md](MAP_METRICS.md)). Yours: loaded in place of the shipped map (a copy of it in your `maps/` folder from an older version still comes first; the editor no longer writes one, it asks to save your edits under a new name); ranked won't accept it. |
| website | `website/<file>` (.jpg / .png, the same name and type) | The release workflow copies it over `site/img/<file>` when it deploys the site. The pictures: `hero.jpg` (the home page's big in-game shot, 1920x1080), `<map>.jpg` for each of the 21 maps (960x540), `hud.jpg` and `models.jpg`. |

**Cost when there are none**: nothing. With no shipped overrides the embedded table is empty, and the user folder is
one directory check at startup; every loader returns at once (`overrides::any()`). Drawing a model checks one null
pointer. The decoders (third_party/stb, third_party/dr_libs) only ever run on override files.

## UI fonts (Settings > FONT)

The UI font is **Rubik Bold** by default. SETTINGS > FONT lists the built-in fonts, then every `.ttf` / `.otf` in the
game folder's `mods/fonts/` (next to `mods/sounds/`; OPEN FOLDER opens it, REFRESH looks again). Picking one applies it
at once and saves it as `ui_font` in quark.cfg (a built-in's id, or the file's name). A font that can't be loaded
(the file gone or broken) falls back to Rubik with a notice; the setting is kept, so the font is back with its file.

How each is built (once, when it is picked or at startup; never per frame, so there is no frame-time cost after the
switch):

| Font | Where it comes from | Cost |
|---|---|---|
| RUBIK (default) | `src/client/font_ui.h`: Rubik (`assets/fonts/Rubik.ttf`, a variable font) at wght 700, baked by `tools/make_font_atlas.py` as raw texels | on the GPU at startup, no decoding |
| WORK SANS, QUARK CLASSIC and the look-alikes | `src/client/font_builtin.h`: each atlas baked by the same tool, stored as a PNG | ~3 ms to decode when picked; 15-65 KB of binary each |
| your mods/fonts files, the real game fonts below | rasterised from the file (stb_truetype) | ~50 ms when picked |

Why the built-ins are baked PNGs and not font files rasterised at run time: Pillow can set a variable font's weight
(Work Sans Bold is wght 700 of `assets/fonts/WorkSans.ttf`; stb_truetype only draws a variable font's default
instance, Regular), the bake sizes every font so its capitals are as tall as Rubik's, and a distance field compresses
about 5:1 as a PNG (Work Sans: 65 KB instead of 488 KB raw texels, or the 361 KB font file). The default stays raw:
no decoding at startup.

**Your own fonts** (`mods/fonts/`): any TrueType / OpenType file, ASCII 32-126 drawn. A variable font is drawn at its
**default instance** (often Regular or Light; use a static Bold file for bold). Each is sized so its capitals match
Rubik's. Names are shown upper case, `_` as spaces.

**Every font fits by itself** (src/client/textfit.h; nothing to check per font): when a font is picked (never per
frame) the game measures every glyph's ink from its atlas. (1) **Line height:** if any glyph's ink, with its outline
(0.5 units: 1 px at the base text scale), reaches above or below the line box Rubik uses (the 8-unit line, or Rubik's
own ink where that goes further), the whole font is drawn just enough smaller to fit, and moved only as far as that
needs (keeping the middle of its capitals where Rubik's are when it can). Of the built-ins, Overwatch / Bebas Neue is
drawn at 0.876 (its 1.2x size took its `|`, `,` and `;` below the box) and TF2 / Lilita One at 0.982; Work Sans and
TF2 Secondary / Jost only move (0.05 and 0.13 units up); quark_tests checks every built-in stays in the box.
(2) **Width:** a widget's label or a label in its box (`Ui::label` / `hint` with a width, or the current `Ui::box`)
that's too wide is first closed up (each pair of letters only as far as its ink allows: the fills stay at least an
outline apart, and no pair closer by more than 15% of a letter), then drawn smaller, and only cut with "..." if it
still doesn't fit at the font's smallest size. Each fit is worked out once and cached (per text, width, size and font).

**QUARK CLASSIC** is the original 5x7 pixel font (before 1.17: the `kFont` table of `git show 8678b00 --
src/client/renderer.cpp`, now `CLASSIC` in tools/make_font_atlas.py), with its original metrics: 1 unit a pixel, 6
units a glyph, drawn from the line top, lower case drawn as upper case. Its distance field uses the max norm, so
edges and the outline stay square at every size.

**Game-styled fonts**: each row of `kBuiltinFonts` (src/client/uifonts.cpp) has a free look-alike bundled in the game
and a list of places where the player's **own copy of the real font** may be. The first one found is used instead
(the menu then says `USING YOUR <file>`); otherwise the look-alike (`CLOSEST FREE MATCH: <game>`). The game only
reads those files on the player's computer, when the menu opens (or REFRESH) and when the font is picked: they are
never shipped, uploaded or copied. A row with neither is not listed.

| Entry | Bundled look-alike (licence, file in assets/fonts) | The player's own copy, first found |
|---|---|---|
| OVERWATCH | Bebas Neue (OFL; `BebasNeue-Regular.ttf`, `BebasNeue-OFL.txt`) | mods/fonts: `big_noodle_titling*`, `BigNoodleTitling*`, `BigNoodleToo*` (.ttf / .otf). Big Noodle Titling's EULA forbids redistribution: never bundled |
| COCAINE DIESEL | Decalotype Bold (OFL; `Decalotype-Bold.ttf`, `Decalotype-OFL.txt`): the font the game itself uses | - |
| QUAKE | LibreQuake's CONCHARS (BSD-3-Clause; `LibreQuake-CONCHARS.png`, `LibreQuake-COPYING.txt`) | mods/fonts: `conchars.lmp`, `conchars.png`, `gfx.wad`; Steam: `Quake/id1/gfx.wad`, `Quake/id1/pak0.pak`, `Quake/rerelease/id1/pak0.pak` (the CONCHARS lump of gfx.wad, inside the PAK; palette index 0 is paper) |
| HALF-LIFE 2 | DejaVu Sans Bold (Bitstream Vera licence; `DejaVuSans-Bold.ttf`, `DejaVu-LICENSE.txt`) | mods/fonts `verdanab.ttf`; the system's Verdana Bold (HL2's menu font): Windows `%WINDIR%/Fonts/verdanab.ttf`, macOS `/Library/Fonts` or `/System/Library/Fonts/Supplemental/Verdana Bold.ttf`, Linux msttcorefonts |
| TF2 | Lilita One (OFL; `LilitaOne-Regular.ttf`, `LilitaOne-OFL.txt`) | mods/fonts `tf2build*`; Steam `Team Fortress 2/tf/resource/tf2build.ttf` |
| TF2 SECONDARY | Jost SemiBold (OFL, variable, baked at wght 600; `Jost.ttf`, `Jost-OFL.txt`) | mods/fonts `tf2secondary*`; Steam `Team Fortress 2/tf/resource/tf2secondary.ttf` |
| APEX LEGENDS | Barlow Condensed Bold (OFL; `BarlowCondensed-Bold.ttf`, `BarlowCondensed-OFL.txt`) | mods/fonts: `Apex-Bold.ttf`, `Apex-SemiBold.ttf`, `Apex-ExtraBold.ttf`, any `Apex-*` / `apex*` (the fan family "Apex" by Felix's Foundry is personal use only: never bundled). The real font is inside the game's archives |

Steam libraries: the default one (Windows: the registry's `HKCU\Software\Valve\Steam\SteamPath`, else
`C:/Program Files (x86)/Steam`; Linux `~/.steam/steam`, `~/.local/share/Steam`, the Flatpak's; macOS
`~/Library/Application Support/Steam`) and every `"path"` in its `steamapps/libraryfolders.vdf`.

**Sources of the bundled files** (each with its licence beside it in `assets/fonts/`; added 2026-10-23 with the
owner's approval): Bebas Neue, Lilita One, Jost and Barlow Condensed from github.com/google/fonts (`ofl/<name>/` and
its OFL.txt); Decalotype Bold from the Cocaine Diesel repository (`base/fonts`; `Decalotype-OFL.txt` is the OFL with
the font's own copyright line, the repository has none); LibreQuake's `lq1/gfx-wad/CONCHARS.png` and `docs/COPYING`;
DejaVu Sans Bold from the dejavu-fonts release (`DejaVuSans-Bold.ttf` and `LICENSE`). A `BUILTIN` row whose file is
missing is skipped by the tool (the menu then shows that entry only when the player has the real font). Never commit
an atlas made from a player's or a commercial font.

**Tuning** (from screenshots of every font on the main menu, settings and FONT pages, and the UI test's layout checks
run in each with `QUARK_UITEST_FONT`): Bebas Neue is capitals only and very condensed, so at Rubik's cap height its
1-pixel strokes broke up in small labels; it is baked 20% taller (`SIZE` in the tool; it has no descenders, so it still
fits the line). The pixel fonts (QUARK CLASSIC, LibreQuake and a player's Quake CONCHARS) are 1 unit a pixel from the
line top (an 8-pixel cell is the 8-unit line), may be drawn down to scale 1 (1 screen pixel a font pixel;
`BakedFont::minScale`, `FontAtlas::minScale`) instead of Rubik's 1.3, and LibreQuake / CONCHARS glyphs are as wide as
their ink (Quake draws every glyph 8 pixels wide; that clipped 6 settings labels at 1280x720). The sheet's dark drop
shadow is paper: the UI draws its own black outline.

**Adding a built-in**: a row in `kBuiltinFonts` (src/client/uifonts.cpp: id, menu name, its baked atlas, the grey
hint, where the real font may be) and, for a bundled font, its row in `BUILTIN` (tools/make_font_atlas.py: kind,
file, weight) plus the file and its licence in `assets/fonts/`; re-run the tool. Credit it in README.

## Shipping the owner's replacements

The owner's editors (model editor, sound editor, ...) export files at their override paths. To ship one, put it in
`assets/overrides/<path>` (keep its extension), merge the editor's `provenance.json` (below), build, run
`build/quark_tests` and mention it in the changelog. An icon also needs `tools/make_icon.py`.

## Provenance

Which assets are still AI-made: `assets/provenance/<category>.json` lists every asset (plus the UI pages and bot areas)
as `ai`, `human` or `third-party`, with where it came from ([PROVENANCE.md](PROVENANCE.md)). After the export, run
`python3 tools/provenance.py sync build/assets_export` (a shipped override or custom recording marks its item `human`)
and `python3 tools/provenance.py merge <sidecar>` for an editor's `provenance.json`; `summary` shows how far along it
is, and every release's notes end with that line. `tools/studio/build.py`'s manifest.json (the website /assets/ page's data) carries each item's `origin`.
