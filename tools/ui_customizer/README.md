# UI customizer

An offline, single-file page (`ui-customizer.html`) that shows every page of the game's UI as the game draws it
(src/client/ui.cpp, renderer.cpp), lets the owner move / resize / rename / add / remove widgets, try fonts, and exports
the result as a layout (JSON) and a change spec (Markdown) for implementing it in the game, plus who made each page
(`provenance.json`, merged into the registry by `tools/provenance.py merge`).

## Install / update (the Quark Project app does this)

```
cd Quark\repository                       (a checkout of testing)
py -3 tools\ui_customizer\install.py      (python3 elsewhere)
```

Builds `Quark\ui-customizer\ui-customizer.html` (the app's UI tab) from this folder's sources and the committed dump.
Non-interactive and safe to re-run; it opens no browser (`--open` does, never with `QUARK_SHELL=1`). It writes only
`ui-customizer.html`, `fonts/catalog.json` and `INSTALLED.txt`, creates an empty `provenance.json` when there is none,
and never deletes or rewrites anything else in that folder (the owner's provenance.json, exported layouts, `extract/`,
`fonts/ofl/`). Python 3 standard library only.

`install.py --extract` first regenerates the dump and the screenshots from this checkout's game source (below). The
screenshots (about 15 MB) are not committed: without them the page works the same, minus the reference-screenshot
overlay.
`install.py --extract-if-stale` (what the app runs, at low priority) does that only when the dump is out of date (below)
or this PC has no screenshots of it yet, then builds again: the dump follows the game without anyone running a command. Without the game's build tools, or when
the extract fails, the page is built from the old dump (red banner) and the install still succeeds.

**Out-of-date dump.** build.py counts the commits that changed `src/client` since the dump's commit. When there are
any, the build and the install print a WARNING, `INSTALLED.txt` says so, the page shows a red banner (with the refresh
command and a Copy button) and an exported spec starts with a warning. A `VERSION`-only difference (every release bumps
it; 0 UI commits) is only a note in the page's status bar and `INSTALLED.txt`: the pages match the game apart from the
version label. Only when git can't count (no git, or the dump's commit is not in the checkout) does a `VERSION`
difference raise the warning. Refresh = `install.py --extract` (or `extract/extract.py --gz` to also rewrite the
committed dump), then reload. The Quark Project app re-runs the install when the UI code, `VERSION` or the provenance
registry change (its `installs.json`), so the banner follows the game.

## Using it

- Pages on the left, grouped by screen (main menu first). Each row shows who made the page: **HU** human, **AI** AI-made
  (a page changed in this layout counts as yours, or as the AI proposal's in a proposal build; the others are as the
  game's provenance registry, `assets/provenance/ui.json`, says). The counts at the top filter the list.
- Search lists the pages whose name or widgets match; clicking one (or Enter for the next) selects its matching widgets.
- Click / drag / resize / arrow keys on the page, the inspector on the right; `?` lists every key. Zoom up to 400 %,
  Ctrl+wheel zooms at the mouse.
- **View** previews the page at another resolution / aspect ratio (1080p, 1440p, 4K, 16:10, 21:9, 32:9, 5:4), laid out
  as the game scales: 720p units times ui.s() = height / 720, text at max(1, round(2s)). The dump has positions, not the
  code's anchors, so widgets (or the panel around them) that hug a side edge stay on it, full-width bars stretch, and
  everything else keeps its offset from the centre; widgets that end up off the screen are outlined in red. A widget's
  **anchor** (inspector: auto = that guess, left, center, right, stretch; a panel's anchor moves what is inside it)
  replaces the guess, and the spec states it with the expression to use. Read only but for that: a click selects (to set
  the anchor), and dragging, the palette, + Page, keys (paste, delete, arrows) and the rest of the inspector are off
  until View is back on 1280x720.
- **Check** (K) runs a layout check while you edit, the in-page version of `audit/audit.py`'s editing rules: widgets
  overlapping, labels running into widgets, anything off the screen, text the game would cut ("...") or shrink, widgets
  sticking out of their panel, tiny text. Red = bad, amber = check it; the page inspector lists them (click one to select
  its widgets). With a View on, it checks the page at that resolution and text rounding (21:9, 32:9 ...).
- Light or dark as the system / the Quark Project app is (`?theme=light` or `dark` pins one); the game's pages keep
  the game's dark look.
- Everything autosaves in the browser. A page still as the game has it is saved as a reference to the dump (a few KB
  per save, not 1.7 MB), so after a dump refresh the pages you never touched show the new game; Export layout writes
  every page.
  The last 30 undo steps (up to 1 MB) are kept with it, written in idle time and on closing: Undo still works after a
  reload or an app restart, even for an Import or a Reset.
- **Export spec**: the changes, then widget tables of the changed pages (the spec window switches to every page, about
  800 KB, or to no tables). Dialogs are in the page (the Quark Project app's Electron has no `window.prompt`).

## Files

| | |
|---|---|
| `src/` | the page: `app.html` (the template build.py fills), `app.css`, `app.js` (UI), `model.js` (layouts, diff, spec), `render.js` (draws widgets like ui.cpp), `fonts.js` (font trials, licence / cap-height parsing) |
| `build.py` | inlines CSS, JS, the dump, the UI font, font trials and provenance into one HTML file (`--out-dir`, `--dump`, `--layout` for a PROPOSAL build) |
| `install.py` | the installer (above) |
| `data/ui-dump.json.gz` | the game's UI at the commit in its `source` (every page's elements in draw order, 1280x720), made by `extract/` |
| `extract/` | regenerating the dump: `extract.py`, `probe.patch`, `build_dump.py`, `style.json`, README |
| `audit/audit.py` | a layout audit (balance, overlap / clipping, edges, text size) of a dump or an exported layout |
| `test/` | `node test/model.test.js`, `provenance.test.js`, `fonts.test.js`, `python3 test/freshness.test.py`; `e2e.js` and `overlay.js` drive the built page headless |

Fonts: the game's own (`assets/fonts`) are embedded; Windows fonts (Bahnschrift, Segoe UI, ...) are read from this PC's
`C:\Windows\Fonts` at build time, preview only (Microsoft-licensed: never committed or shipped); open fonts downloaded
to `Quark\ui-customizer\fonts\ofl\<family>\` (with their OFL.txt) are picked up too.

## Regenerating the dump (after UI changes)

```
py -3 tools\ui_customizer\extract\extract.py --gz     (or install.py --extract, without --gz)
```

Builds a patched copy of the game in a scratch clone (`Quark\ui-customizer\.extract-src`), runs the UI test there with
a hidden window, no audio and its own data folder, and writes `Quark\ui-customizer\extract\` (dump + screenshots);
`--gz` also rewrites `data/ui-dump.json.gz` here, to commit. About 8 minutes. Details: `extract/README.md`.

It also runs on Linux (no Windows needed): `DISPLAY=:99 python3 tools/ui_customizer/extract/extract.py --gz` under
Xvfb (`Xvfb :99 &`); needs git, cmake, a C++ compiler and the game's build dependencies. The screenshots come out the
same way.

## Tests

```
node test/model.test.js && node test/provenance.test.js && node test/fonts.test.js && python3 test/freshness.test.py
node test/e2e.js [screenshot dir]                 (the installed page, or UIC_HTML=<file>)
node test/overlay.js [html] [out dir] [page ids]  (the page drawn over the game's screenshots, with the difference)
```

e2e / overlay need playwright-core (`npx playwright --version` caches it, or `PW_CORE=<folder>`) and an installed Edge
or Chrome (`BROWSER_EXE`; without one, Playwright's own Chromium). e2e covers drags, undo, the in-page dialogs, the
resolution preview, the origin badges and the spec's layout tables.
