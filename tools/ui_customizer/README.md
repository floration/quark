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

## Files

| | |
|---|---|
| `src/` | the page: `app.html` (the template build.py fills), `app.css`, `app.js` (UI), `model.js` (layouts, diff, spec), `render.js` (draws widgets like ui.cpp), `fonts.js` (font trials, licence / cap-height parsing) |
| `build.py` | inlines CSS, JS, the dump, the UI font, font trials and provenance into one HTML file (`--out-dir`, `--dump`, `--layout` for a PROPOSAL build) |
| `install.py` | the installer (above) |
| `data/ui-dump.json.gz` | the game's UI at the commit in its `source` (every page's elements in draw order, 1280x720), made by `extract/` |
| `extract/` | regenerating the dump: `extract.py`, `probe.patch`, `build_dump.py`, `style.json`, README |
| `audit/audit.py` | a layout audit (balance, overlap / clipping, edges, text size) of a dump or an exported layout |
| `test/` | `node test/model.test.js`, `provenance.test.js`, `fonts.test.js`; `e2e.js` and `overlay.js` drive the built page headless |

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

## Tests

```
node test/model.test.js && node test/provenance.test.js && node test/fonts.test.js
node test/e2e.js [screenshot dir]                 (the installed page, or UIC_HTML=<file>)
node test/overlay.js [html] [out dir] [page ids]  (the page drawn over the game's screenshots, with the difference)
```

e2e / overlay need playwright-core (`npx playwright --version` caches it, or `PW_CORE=<folder>`) and an installed Edge
or Chrome (`BROWSER_EXE`).
