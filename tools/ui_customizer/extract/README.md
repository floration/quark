# The UI dump (for the UI customizer)

`../data/ui-dump.json.gz`: every page of the game's UI at the commit in its `source` (`commit`, `version`, `extracted`),
at 1280x720 (layout scale 1.0): the elements of each page in draw order, plus `style` (this folder's `style.json`:
colours and how each widget is drawn, from `src/client/ui.cpp`). `extract.py` makes it, with one screenshot per page
(`Quark\ui-customizer\extract\shots\`, not committed; `shots/SOURCE.txt` names their commit).

## How it is made (`extract.py`)

1. A scratch clone of the checkout at the commit (`Quark\ui-customizer\.extract-src`, reused for incremental builds),
   with `probe.patch` applied there only (never committed to the game):
   - `Renderer::rect / rectGradient / text / textMono / logo` append to a draw log while one is attached
     (`Renderer::drawLog`, only at `drawDepth` 0).
   - Every `Ui` call (panel, label / labelFit / labelCentered / hint / hintCentered with how it was fitted, button,
     tab, toggle, cycle with its option names, slider with min / max / def / fmt, text field, colour picker, dropdown,
     the open popup, keyAction, handle, view, custom) logs itself as one JSON element and raises `drawDepth`, so its
     own rects and text aren't logged twice. Widgets under a modal are `"muted": true`, under an open popup
     `"blocked": true`.
   - `uitest.cpp`: for every scenario / screen path / context, the frame with the most widgets is kept. It is drawn
     again with the mouse parked off the window (no hover looks), and written to `raw/<n>.json` + `raw/<n>.png`.
     In-game frames get `#scores`, `#perf`, `#perfmini` added to the path while those overlays are up.
   - Safety: under `--uitest` the window is created hidden and unfocused, every display mode change keeps it hidden at
     -20000,-20000, and no audio device is opened. The UI test makes its own data folder (in the private TMP
     extract.py gives it): the player's settings, maps and demos are never touched.
2. `cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DQUARK_BUILD_TESTS=OFF`, target `quark` (`build-x/`).
3. `QUARK_EXTRACT_DIR=<run> QUARK_EXTRACT_SHOTS=1 TMP=<private> quark --uitest <run>/report.json` (about 4 minutes),
   no console window; killed by its PID after `--timeout`.
4. `build_dump.py --out <extract> <run>/raw`: groups the captures by path + context. In each group the richest variant
   is the page and its scenarios are listed; a variant whose widgets overlap the page's by less than 60% gets a page of
   its own, `id [scenario]`. The HUD (`ingame`, `ingame/scores`) gets one page per situation: host-vs, hide-seeker,
   hide-rps, client-spectator, demo.

The previous dump in `extract/` is kept as `extract-<version>` when it is of another commit (the one from before the
1.18.13 redesign: `extract-1.18.2`, commit 9b88c36, on the owner's PC only).

## Updating the probe

When `probe.patch` no longer applies (the UI code changed), redo its edits in the scratch clone at the new commit
(`git -C .extract-src checkout -f --detach <sha>`, then the hunks by hand: they are small and each says "(extract,
scratch only)"), build, run once, check a few pages with `node test/overlay.js`, then save it with
`git -C .extract-src diff -- src > tools/ui_customizer/extract/probe.patch`. A new widget in `ui.h` needs a `logEl`
line in the patch (and a case in `src/render.js`).

## Element notes

- Draw order is the order in the list. Kinds: button, tab, toggle, cycle, slider, text (text field), colorpicker,
  dropdown, popup, key, handle, view, custom, label, panel, rect. A `label` with `"raw": true` is a direct
  `Renderer::text` call (`"mono": true`: console font); `"hint": true` is `Ui::hint` (text scale 1.5); `style: "logo"`
  the QUARK title (`Renderer::logo`, with the `enemy` colour of the Q's right half).
- `x, y` of text is the top of its 8*textScale line box. A centred label gives its left edge and `cx`. `textScale` is
  the size asked for; `maxW` the width it was fitted to, `fitScale` / `track` how (smaller / closed up), `shown` the
  cut text when it had to be cut.
- `action` is the widget's UiExp: `nav:<screen>` (`~x`: ends with x), `cfg:<quark.cfg key>` (`*` = prefix),
  `state:<observable>[=value]`, `cmd:<server command>`, `url`, `quit`, `edit`, `local`, `inert`, `bind`. `id` is the
  stable id when the label shows data, and `"dynamic": true` marks OPTIONAL data rows. A dropdown's `pick` is what
  picking an entry does.
- A cycle's `label` is an id that isn't drawn: its `value` and `options` are the names shown. `key` elements have no
  rect. A `custom` is a hand-drawn button: the rects and labels around it are its look, it is only the hit area.
- Page `ctx`: `sel:<kind>` is the editor's selection; `keep:<n>` the "keep your edits?" prompt, by the way out that
  raised it.

## Not covered / limits

- Data comes from an offline test: no internet server list, the leaderboard can't reach the ranking server, map and
  mode lists are the built-in ones. Values are the test's defaults (player name "player").
- Never captured: vote-in-progress panels, the in-game loading screen, the kill feed and round-end banners beyond what
  showed by chance, tooltips and hover hints, a focused text field's caret, a slider being typed into (all described
  in `style.widgetLook`). Each page is one moment: what's on screen depends on the state the test left it in.
- Only 1280x720. Positions at other sizes follow `Ui::s()` = height/720 (textScale = round(2*s)), but some screens lay
  out differently.
