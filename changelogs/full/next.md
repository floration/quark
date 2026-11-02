# Next (full, testing builds since 1.19.6)

Every change on `testing` since production 1.19.6, with what changed and why. The release PR renames this file to `changelogs/full/<VERSION>.md`.

## Languages (owner, 2026-11-02: "support the top 10 global languages", "gaming list yeah")

- **Which ten.** These are the top gaming-audience languages: English, Simplified Chinese, Russian, Spanish, Brazilian Portuguese, German, Japanese, French, Korean and Turkish. Turkish was picked over Polish for its larger Steam share.
- **How it works.** The code keeps writing English. `src/client/lang.cpp` translates each string just before the renderer draws or measures it (`Renderer::text`, `textWidth`, `fitText`, `textClip`), so no menu, HUD or notice code changed. The tables are `assets/lang/<code>.tsv`: an English key, a TAB, then the translation. `tools/lang/extract.py` collects 3695 keys from the source. Its sources are string literals, each line of multi-line text, `%s` pieces, and the whole `%s` line as a `{}` pattern. `tools/lang/embed.py` builds them into the game at build time.
- **Strings built at run time.** When a drawn string isn't a key, the lookup tries these in order:
  1. Digit runs read as `#` ("KILLS: 12" -> "KILLS: #", with the number put back).
  2. `{}` patterns, whose captured parts are translated too.
  3. A number in front of a word.
  4. The string with the decorations round it removed ("> TEAM <", "SAY: ").
  5. Splitting it at ": ", " - ", " / " or a double space.
  6. Splitting it at a word boundary whose left part is a key.
  Anything left over is drawn as it is, which covers player names, chat and map names.
- **Speed.** English is a no-op: `lang::active()` is false, so nothing is looked up. Other languages do one hash lookup per drawn string from a cache of drawn strings. The cache is cleared in `lang::frame()` once it holds over 8192 entries (that happens with changing numbers). A string drawn before costs no allocation. Picking a language parses its table once, in a few ms, and decodes its atlas in tens of ms.
- **Glyphs.** Characters past ASCII come from a per-language distance-field atlas, `assets/lang/atlas/`, baked by `tools/lang/make_atlas.py`:
  - The cells use the UI font's layout, 66 texels in a 62-column atlas (4092 wide). The atlas is drawn by the same shader from texture unit 7. A glyph id of 95 or more is an atlas glyph.
  - **base:** 341 glyphs. Latin-1, Latin Extended-A, Cyrillic, typographic marks and every language's own name. It comes from Rubik Bold, the same font as ASCII.
  - **zh-CN, ja, ko:** 1630, 1316 and 1088 glyphs. Each is base plus that language's characters, in its own forms of Noto Sans CJK Bold (SIL OFL 1.1, `assets/fonts/NotoSansCJK-OFL.txt`). Ideographs are 6 units tall, against Rubik's 5.25 capitals, and centred on the capitals.
  - All text measuring and cutting (`textfit.cpp`, `Ui::wrap`, fitted and clipped labels) now steps through UTF-8 whole characters, so a cut never splits a character.
- **Setting.** `language=` in the config: `auto` (the default), or a code. `auto` reads Windows' UI language (`GetUserDefaultLocaleName`), or `LC_ALL` / `LC_MESSAGES` / `LANG` elsewhere. An unknown language falls back to English.
- **Fit.** Translations were shortened wherever the UI test found a clipped label at 1280x720 or 1024x768. The aim was 1.6x the English width or less, and tighter on settings rows that are already full. Russian lost 9 more labels in the final pass (for example ПРЕДУПР. СЕТИ for NETWORK WARNINGS).
- **Dropdowns** open upward when the list would leave the window. The German test found the LANGUAGE list off screen.
- **Tests.** `quark_tests assets` checks every table against the key list: the same keys, and the same number of `#`. The UI test (`quark --uitest`, with `QUARK_UITEST_LANG=<code>`) ran in full in Chinese and Russian, and on the menus in the other seven languages. Its only failures were 6 editor EDIT checks, and English fails the same ones. `QUARK_LANG_MISSES=<file>` writes the drawn strings that had no translation, for the next pass.
- **Known gaps.** Some text built at run time stays in English. Bot names, map names and the editor's tool tips aren't translated. There is no kerning for atlas glyphs.

## Latency (owner, 2026-11-02, after F3 showed a gap between SWAP and GPU: "can the game enable the setting?")

- **Early flush in LOW LATENCY: ON.** OpenGL holds a frame's commands until something flushes them. Under ON, nothing did until `SwapBuffers`, so the GPU sat idle through DRAW CALLS, HUD DRAW and SWAP, then waited out the driver's submission: about 45-65 µs on q's desktop at 3000+ FPS. `drawWorld` now calls `glFlush()` after the shadow pass (before the late latch) and after the world's draw calls (before the HUD), in ON as in ULTRA. The GPU then starts while the CPU finishes the HUD. A flush costs a few µs of CPU, and the frame queue is unchanged.
- **GPU SCHEDULING** (VIDEO + AUDIO > MORE, LATENCY, under the NVIDIA rows). Windows' "Hardware-accelerated GPU scheduling" lets the GPU schedule its own queue, which shortens the hand-off from a submitted frame to the GPU starting it.
  - It's a system setting: `HwSchMode` (2 on, 1 off) under `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers`, read at boot. A game can't change it silently.
  - The row reads the value (`hwSchedState`, gpuprefs.cpp). ON means it's set, TURN ON means it's off, and N/A means there's no value (the GPU or driver doesn't offer it, or this isn't Windows).
  - TURN ON runs `reg.exe add ...` through Windows' "runas" admin prompt, hidden so no console window opens (`hwSchedEnable`), then says to restart.
- **Scanout, for the record.** F3's estimate at 240 Hz, about 2083 µs, is half a refresh: the crosshair's row is redrawn once a refresh, whatever the FPS. Only a faster refresh shortens it. The game's own part is about 210 µs.
- **F3 in nanoseconds, game vs hardware** (owner, 2026-11-02: "there should also be a view without the hardware limits just straight the game limit ... also since many things are sub us now we should measure in ns").
  - `drawPipeline` now prints every row and the total in ns, grouped by thousands. A line under the title splits the total into GAME, MOUSE POLLING (row 0: half the mouse's report interval) and DISPLAY (every row after the GPU: compositor, vsync and scanout estimates).
  - F3 now cycles five stages. Stage 4 is GAME ONLY: the hardware rows are removed and the bars are scaled to the game's part, from the mouse report's arrival to the GPU done. `perfOn()` covers stages 3 and 4, so both measure. F4's number is in ns too.
