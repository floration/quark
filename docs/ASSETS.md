# Assets: the export, the Asset Studio and overrides

The owner replaces the AI-made default assets with human-made ones. Three pieces make that work:

1. **The export** (`build/quark_tests exportassets <dir>`) writes every default asset as a file, plus
   `manifest.json` listing them.
2. **The Asset Studio** (https://claude.ai/artifact/JuDQckiZY15XUHz1xbD3pC; `tools/studio/index.html`, its files built
   by `tools/studio/build.py` from the export) shows every asset and takes the owner's uploads.
3. **Overrides**: a file at an asset's export path under `assets/overrides/` (shipped) or the per-user
   `overrides/` folder (this computer only) replaces that asset.

## The export

```
cmake --build build -j
build/quark_tests exportassets build/assets_export
python3 tools/studio/build.py build/assets_export     # -> build/studio/ (+ manifest.json), what the page publishes
```

| Category | Files | Path (= the override path) |
|---|---|---|
| Sounds | 27 sounds x 5 kits = 135 WAV (48 kHz mono) | `sounds/<kit>/<SOUND>.wav`, kit = `default` `heavy_metal` `magical` `retro` `mouth` |
| Voices | 30 announcer lines x 7 voices = 210 WAV | `voices/<voice>/<LINE>.wav`, voice = `male` `female` `british` `old_man` `e_girl` `india` `spy` |
| Models | 5 players, 10 weapon viewmodels, 7 projectiles, the crown = 23 glTF | `models/player/<NAME>.gltf`, `models/weapon/<NAME>.gltf`, `models/projectile/<NAME>.gltf`, `models/crown/CROWN.gltf` |
| Maps | every shipped map (.qmap) | `maps/<name>.qmap` |
| Fonts | the UI font (Work Sans) | `fonts/WorkSans.ttf` |
| Images | the game icon | `images/icon.png` |
| Website | the site's pictures | `website/<file>` (from `site/img/`) |

Sound names are the SND_* names (`HIT`, `ROCKET`, `GRAPPLE_FIRE` ... `assets/sfx/custom/README.md` says when each
plays); voice lines are `THREE` `TWO` `ONE` `GO` `BOMB_PLANTED` ... `ROUND_OVER` (src/client/audio.cpp). A line a
voice has no recording of is exported as a tenth of a second of silence (it is silent in the game), so it can still be
added. The export is what the game uses now: the built-in assets with the shipped overrides applied, never this
computer's own overrides folder.

Models are the game's box models (src/client/models.cpp, the same lists drawWorld draws), one glTF primitive per
colour slot, in each model's own space (metres, +Y up, front at -Z):

- **player**: feet on the ground at the origin, 2 m tall, facing -Z. Every model shares one hitbox: stay inside it.
- **weapon**: the first-person viewmodel. Origin at the weapon's pivot (the HUD's viewmodel x / y / depth put it
  there and its size scales it), barrel along -Z.
- **projectile**: origin at the projectile, flying toward -Z (ROCKET, KNIFE, STICKY, CROSSBOW, BUBBLE, SYRINGE, ORB =
  every other projectile). The game spins the sticky and the thrown knife and wobbles the bubble.
- **crown**: the leaderboard crown, origin at the top of the head, front at -Z (the board letter stays drawn on top).

Material names pick a colour that changes at run time (prefix, any case: `team`, `Team_cloth`, `glow.001`):

| Material | Follows | Draw it in |
|---|---|---|
| `team*` | players / projectiles: the team colour; weapon: your accent colour; crown: gold (#1) or silver | white = exactly that colour (crown: the export's gold) |
| `glow*` | the map's glow colour | the export's orange |
| `metal*`, `base*` | the map's metal / base colours (viewmodels) | the export's colours |
| anything else | nothing: drawn as authored | |

An override's colour in a slot is multiplied by (the current colour / the colour shown in the export), so a model drawn
in the exported colours looks like the default. Vertex colours (COLOR_0) times the material's baseColorFactor give the
colour; textures are ignored. A material with alphaMode BLEND is drawn see-through (like the bubble's shell).

## Overrides

A file at `<overrides>/<export path>` replaces that asset. Two places, the user's winning:

- **shipped**: `assets/overrides/<path>` in the repository. Embedded in the game at build time
  (tools/embed_overrides.py), so it ships with the next release as the new default. Maps, the icon and website
  pictures are build steps instead (below).
- **yours**: `<user data folder>/overrides/<path>` (Windows `%APPDATA%/quark`, macOS `~/Library/Application
  Support/quark`, Linux `~/.local/share/quark`; `--data-dir` / `QUARK_DATA_DIR` move it). Read at startup: try an
  asset without rebuilding.

Matching ignores case, and the extension may be any the asset accepts (an upload keeps its own: `ROCKET.ogg` replaces
`ROCKET.wav`):

| Asset | Accepted | What happens |
|---|---|---|
| sounds | .wav .ogg .flac .mp3 | Mono, leading silence trimmed, cut at 3 s, as loud as the sound it replaces (kits stay fair). A DEFAULT override also becomes every kit's sound that was DEFAULT's, and RETRO's (chiptuned). Players' own `mods/sounds/` still win over it. |
| voices | .wav .ogg .flac .mp3 | The same, cut at 5 s, as loud as the line it replaces |
| models | .glb .gltf | Drawn in place of the box model (lit, shadowed, no outline). Buffers embedded or in files next to the .gltf. At most 20 000 triangles a model (they stream every frame); a bad file is logged and the default kept. |
| font | `fonts/WorkSans.ttf` / `.otf` | The UI's distance-field atlas is rasterised from it at startup (stb_truetype), same layout and metrics as tools/make_font_atlas.py. Upload a static font of the weight to use (a variable font gives its default instance). |
| icon | `images/icon.png` / `.jpg` | Shipped: run `python3 tools/make_icon.py` (Pillow) after adding it: it makes assets/quark.png / .ico, the launcher's and the site's icon from it and records it in assets/quark.icon-source (quark_tests fails until that matches). In the game: the window icon. |
| maps | `maps/<name>.qmap` | Shipped: tools/make_maps.py writes the override instead of generating that map and tools/embed_maps.py embeds it (an official map: ranked uses its sha256). It counts as human-made: quark_tests runs every map check on it and reports the numbers, labelled HUMAN-MADE, without failing on them ([MAP_METRICS.md](MAP_METRICS.md)). Yours: loaded in place of the shipped map (a copy of it in your `maps/` folder from an older version still comes first; the editor no longer writes one, it asks to save your edits under a new name); ranked won't accept it. |
| website | `website/<file>` (.jpg / .png, the same name and type) | The release workflow copies it over `site/img/<file>` when it deploys the site. The pictures: `hero.jpg` (the home page's big in-game shot, 1920x1080), `<map>.jpg` for each of the 21 maps (960x540), `hud.jpg` and `models.jpg`. |

**Cost when there are none**: nothing. With no shipped overrides the embedded table is empty, and the user folder is
one directory check at startup; every loader returns at once (`overrides::any()`). Drawing a model checks one null
pointer. The decoders (third_party/stb, third_party/dr_libs) only ever run on override files.

## Shipping the owner's uploads

The studio keeps each upload in its `replacements` collection (status `waiting`) and the file in its asset store as
JSON `{assetId, target, filename, mime, data: base64}`. For each waiting one: decode `data` into
`assets/overrides/<target>` with the uploaded file's extension, build, run `build/quark_tests`, mention it in the
changelog, then set the doc's status to `shipped` (`shippedIn: <VERSION>`). An icon also needs `tools/make_icon.py`.
After the push, rebuild the studio files (commands above) and republish the page.
