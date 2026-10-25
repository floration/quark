# Quark Sound Editor

A local page for designing and levelling Quark's sounds: every kit's 33 sounds, every voice's 27 announcer lines and its quips, the
events that have no sound yet, gameplay scenes to hear edits in context, and an export the game reads. It runs from
disk (`file://`) in Chrome or Edge with no server and no network.

## Build / rebuild

You need the game's asset export (`build/quark_tests exportassets <folder>`, see docs/ASSETS.md), then:

```
python3 tools/sound_editor/build.py <export folder> <output folder>
tools\sound_editor\build.cmd <export folder> [output folder]      (Windows; default output %USERPROFILE%\Desktop\Quark\sound-editor)
```

The output folder gets `sound-editor.html` (this folder's `index.html` with `editor.css` and `js/*.js` inlined) and
`data/` (the manifest and one script per kit / voice with the audio in base64; a browser blocks `fetch()` on `file://`).
With ffmpeg on PATH the clips are stored as FLAC (lossless, about 22 MB in all), otherwise as the WAVs (about 40 MB).
Python 3 standard library only. `--page-only` rewrites only the page and keeps `data/`.
Rebuild after a game update so the page shows the current defaults. Your work is not in these files: it is autosaved in
the browser and in the project files you save.

**Install / update in one step** (non-interactive; the Quark Project app runs it): `python tools/sound_editor/install.py [out]
[--no-build]` (default out: `<checkout>/../sound-editor`). It exports the sounds with a `quark_tests` whose export names this
checkout's `VERSION`: `$QUARK_TESTS` first, then every build folder the other installs use (`build-win`, `build`,
`build-model-editor`, `build-editors`, `../working-repo-sound/build-win`, `../.src/*/build*`), newest first. A build of
another version is skipped with a warning, because it would export old sounds and voice lines. With none matching it builds
`quark_tests` itself in `build-editors` (the bot editor's folder and settings, so neither install builds twice), below normal
priority and with no console window; `--no-build` skips that. Only when no build can be made does it rebuild just the page
(if data is already there), exiting 2 when it skipped stale builds so the app can show that the sounds weren't updated. It
writes `INSTALLED.txt` and never opens a browser.

## Export to the game

**Export to game** renders every edited sound (trim, fades, chain) as 48 kHz 16-bit mono WAV at its override path,
`sounds/<kit>/<SOUND>.wav` or `voices/<voice>/<LINE>.wav`, and writes `sound-levels.txt` at the root:

- **Choose folder and write** (Chrome / Edge): pick `%APPDATA%\quark\overrides` to try sounds in the game without
  rebuilding (restart the game), or the repository's `assets\overrides` to ship them. Picking `%APPDATA%\quark` or the
  repository folder works too (the overrides folder inside is used). A folder with no overrides folder in it (Desktop...)
  is asked about before anything is written. A same-named `.ogg` / `.flac` / `.mp3` already there is moved to `.bak`,
  since the game would pick either.
- **Download .zip**: the same layout, to unzip into the overrides folder. A zip can't see the `sound-levels.txt` already
  there, so it never carries one of its own (unzipping would wipe the lines written before): pick that file in the dialog
  (**Zip: merge with the sound-levels.txt there...**) and the zip carries the merged file; otherwise it carries
  `sound-levels-add.txt`, the lines to add by hand.

The dialog has a tick per file: at first only the edits changed since you last exported them are ticked (**every edit** /
**only changed since the last export**), so unchanged files aren't rewritten and their provenance date stays true. An
edit that plays exactly the shipped sound (a step added then removed, every step bypassed, an auto trim inside the
silence the game cuts anyway) shows `ED=` / NO CHANGE and is left out: its file would only pin the old sound.

**Quips**: any quip file of a voice replaces its whole default set in the game, so exporting one quip also writes that
voice's other default quips, as the game plays them now (its line, or your edit of that line), marked "default quip" in
the dialog (untick **keep each voice's other quips** to leave them out: the dialog then says which quips the voice keeps
and loses). A folder export skips a default quip already in the folder (an earlier export of yours stays). A voice with
shipped quip files has its set already, so nothing is added.

**Remove sounds from the game...** (Project menu, or the export dialog) lists the sounds and lines in an overrides
folder and moves the ticked ones to `.bak` (the game plays the shipped sound again; delete the `.bak` files once sure),
with their `sound-levels.txt` lines and `provenance.json` entries. Ticked at first: files the editor wrote whose edit is
no longer in the project (reverted).

The game normally rescales a replacement to the loudness of the sound it replaces (DEFAULT's for every kit, by heard
loudness for the four hitsound tiers; the line it replaces for a voice). `sound-levels.txt` lists, one per line without
extension (`sounds/default/ROCKET`), the exported files whose level you set here (a levelling group, or "keep my level"):
the game keeps their own level. A folder export merges it with the file already there: lines for files exported now
follow this export, all other lines and comments stay. Sounds for missing events export the same way
(`sounds/<kit>/<NAME>.wav`); the game ignores them until it has a hook for them.

## Provenance: who made each sound

Every export (folder or zip) also records, for each exported file, who made it: `provenance.json` in the editor's own
folder (where `sound-editor.html` is, e.g. `Desktop\Quark\sound-editor`), never in overrides. Format and rules: the
provenance spec (`assets/provenance/`, `tools/provenance.py merge <sidecar>` folds it into the registry before a push):

```json
{"format": 1, "tool": "sound-editor", "items": {"sounds/default/ROCKET.wav": {"origin": "human", "by": "q", "date": "2026-10-24", "tool": "sound-editor", "note": "..."}}}
```

Ids are the export paths (`sounds/<kit>/<SOUND>.wav`, `voices/<voice>/<LINE>.wav`, missing-sound slots too). One entry
per id: the latest export replaces it (fields the editor doesn't set are kept), every other entry stays. The origin
(shown as a chip on every matrix cell, in the editor panel's **Origin** and in a column of the export dialog) is `human` only
when a person made the audio:

- **A dropped / uploaded file** (or an empty missing-sound slot): not set until you pick human, third-party (with where it
  came from: pack / site / licence) or AI, in the panel or the export dialog (bulk set for many). The export waits for it.
- **An edit of a shipped sound** (trim, fades, filters, levelling) keeps the shipped sound's origin, with a note such as
  "trim, high-pass of the AI default": DEFAULT and RETRO (synthesized) and every kit's SUPER / HYPER / LOCATOR (DEFAULT's)
  are `ai`; HEAVY METAL, MAGICAL and MOUTH (Kenney CC0 clips, `assets/sfx/CREDITS.md`) are `third-party`; the voices are
  `ai` (Piper text to speech, and the recordings in `assets/voice/`, whose maker the repository doesn't record); a default
  quip is its voice's line of that name (the game plays the line as its quip), so it has that line's origin; a sound
  with the owner's file in `assets/overrides` is `human`.
- **Design...** (the generator, shaped by hand): `human`, "made in the sound editor's generator".

Changing a dropped file's origin starts its note / source afresh (a "recorded by q" note never stays on a file switched to
third-party). The matrix shows what the game plays now: a kit sound with no edit that plays your DEFAULT edit (`=D`,
RETRO) shows that edit's origin, and a quip with no edit of its own shows its line's.

Origins are saved in the project file and the autosave. On the first export the editor asks for the sound-editor folder
(Chrome / Edge), checks it holds `sound-editor.html`, and remembers it (asking permission again in a later session).
Without a folder picker, or if you skip it, **Download provenance.json** gives the file to put in that folder (it holds
every entry this editor has exported). The .zip carries `provenance.json` at its root too: move it to the sound-editor
folder, not into overrides.

## What it does

- **Sounds / Voices**: a matrix (rows = sounds or lines, columns = kits or voices) with a mini waveform, duration,
  loudness and peak per cell; when it plays, the length guide, where it is reused, and the audit's weak spots per row.
  The Voices tab also has a **QUIPS** group: each voice's quips (the QUIP key), rows `quips/<NAME>`, exported to
  `voices/<voice>/quips/<NAME>.wav` (provenance id the same path). Any quip files for a voice replace its whole default
  set in the game (docs/ASSETS.md), so the export writes the voice's other default quips with it (see Export).
  Flags: over the length guide, cut by the game's cap (3 s sounds, 5 s lines), edited, level yours, `=D` (this kit uses
  DEFAULT's sound, so a DEFAULT edit plays here; RETRO is DEFAULT chiptuned), and who made it: `AI`, `3P` (third-party),
  `HUM` (human) or `SET?` (a dropped file whose origin isn't chosen yet). Each kit / voice header counts its origins and the
  toolbar counts the whole matrix, so you can see how much is still AI-made. Show the values as the game plays them,
  as the exported file, or as shipped. Click to play and edit; solo / mute per sound and per pack; play a row across
  kits (compare) or a column through a kit; filter by name, edited only, over guide (Voices: cut at 5 s), favourites, and
  origin ("still AI-made" keeps the rows with an AI-made cell and dims the other cells). The mini waveforms of a row share
  one time scale, its longest clip. The seven voices load together (about twice as fast as one after another).
- **Editor** (bottom dock): source waveform (zoom with the wheel, Shift+wheel scrolls), draggable trim and fade handles,
  cursor, playhead, play selection (Shift+drag a region), auto trim (start by the game's rule: the first sample at 3 %
  of the peak; end under -60 dB), the guide and the game's cap as markers, the output with where the game starts it,
  a spectrogram, and metrics for the file, the game and the original. Filter chain, each step with bypass, reorderable:
  gain, high-pass, low-pass, low shelf, peak EQ, high shelf, pitch / speed (resample), compressor, drive / soft clip,
  reverse, reverb (generated room), normalize (peak), true-peak limiter. The filters run in plain JS with the Web Audio
  BiquadFilterNode's own coefficients (the same result, without an OfflineAudioContext per step); reverb renders in an
  OfflineAudioContext. Every step's result is cached by its prefix (source, trim, fades and the steps before it), so a
  change re-renders only that step and the ones after it. A field you change keeps the focus (Tab moves on); at 2560 wide
  the side column shows trim / level and origin / notes side by side. A/B against the shipped original; B plays either the file or what the game will play (its
  leading-silence trim, cap, and level match unless "keep my level"). Undo / redo for everything.
- **Loudness**: ITU-R BS.1770-4 K-weighting; integrated LUFS (400 ms blocks, 75 % overlap, -70 / -10 gates; a clip
  under 400 ms uses its whole-clip mean square), max momentary (400 ms), true peak (4x oversampled), plus the game's own
  heard loudness (`Audio::heardLoudness`) and 20 ms level. Matches `ffmpeg ebur128` within 0.05 LU.
- **Levelling**: groups of any sounds / lines brought to one loudness: match a number, the loudest, the average or a
  reference member, by integrated, max momentary or heard loudness, with a true-peak ceiling (default -1 dBTP) and a
  soft limiter when needed. Presets: each kit's weapons, the hitsound ladder (keeps 0.7 / 0.85 / 1.0 / 1.15; per kit or
  across kits), movement, the same sound across kits, one voice's lines, one voice's quips, the same line or quip across
  voices, all voices, all quips. A preset made twice adds its group once. The member adder lists the quips too.
  Preview the gains, apply (a gain step per member, refined until each is within 0.02 dB; one undo step; re-applying
  replaces the group's own steps). Members are measured as the game plays them once their level is kept.
- **Scenes**: rifle duel, rockets and jumps, laser / sniper / headshot, movement tech, bomb round, race, killstreak,
  hitsound ladder, a pile-up, and every reused sound. Your kit, the enemies' kit and the announcer are chosen
  separately; volumes, pitch jitter, refires, the hitsound gate, 3D gains and pan (`Audio::voiceGains`), the master
  (0.6) and the mixer's soft limit follow the game. Loop them while you edit (each pass uses the edits as they are),
  see every event on a timeline, solo / mute live, render a scene offline (its mix loudness and a WAV).
- **Missing**: the audit's events with no sound (priority, 2D / 3D, when it would play, a design note), each with a
  slot per kit (or voice) to drop a file into or design one; marked "needs a game hook". Add your own names too; removing
  one removes its slots' sounds as well (after a confirm; undo brings them back), so they are not exported.
- **Overview**: a dot plot of every sound's loudness per kit (or every line per voice) with the spread per row; the voices
  chart fills in as the voices load.
- **Files**: drop a file on a cell to replace it; drop many files or a folder anywhere to assign them by name
  (`ROCKET.wav`, `heavy_metal/ROCKET.ogg`, `male_GO.wav`, `wooden/STEP.flac`; a quip: `male_quips_HEADSHOT.wav`,
  `voices/male/quips/HEADSHOT.wav`, `quips/HEADSHOT.wav`) with a confirm list; or Upload (files) / Upload folder (matched
  by its folder names too). Project
  files (.json: every edit, chain, group and note with your audio embedded) save and open; the session autosaves in the
  browser.

Extras: spectrogram, loudness overview chart, copy a chain (and trim) to other sounds, a sound designer (falling sine +
band noise, like the game's synthesis), favourites and notes per sound, row compare across kits, random pitch preview
(R), scene render to WAV, a monitor level and output meter. Keyboard: Space play / stop, Shift+Space play selection,
A / B, G (game / file / original), [ ] trim at the cursor, Ctrl+Z / Ctrl+Shift+Z, arrows move in the matrix, S / M solo /
mute, F favourite, + / - / 0 zoom, 1-6 tabs, ? the list.

## Source

`index.html` + `editor.css` + `js/`: `data.js` (tables, registry, state, undo), `audit.js` (where each sound plays, weak
spots, missing events: update it when the game changes), `loudness.js` (BS.1770 and the game's measures; also runs in
node), `dsp.js` (chain, the game's clip preparation, WAV), `engine.js` (caches, mixer state, playback), `scenes.js`,
`groups.js`, `export.js` (zip, folder, sound-levels.txt, project, autosave), `provenance.js` (origins, provenance.json), `render.js` (canvas), `ui*.js`, `boot.js`.

## Tests

`node tools/sound_editor/test/test_page.js <sound-editor folder>` (Playwright, headless Chromium) checks a built editor:
the name matching (quips), provenance notes and origins, the matrix chips / filters / counts, Voices and the overview,
the chain cache and JS filters against the Web Audio node, Missing, levelling presets, the dock and the layout at
1280x720 and 2560x1440, with no console errors. Rebuild the page first (`build.py - <folder> --page-only`).
`python3 tools/sound_editor/test/test_install.py` checks which `quark_tests` install.py picks (fake builds, no compiler).
