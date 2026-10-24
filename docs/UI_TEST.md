# The UI test

`build/quark --uitest [report.json]` clicks every button in the game, in every situation it shows up in, and checks
each one does what it says. It fails when anything is wrong — including a widget nobody told it about. CI doesn't run it
(owner, 2026-10-24: CI checks builds, meta and sanitizers only): run it locally under Xvfb before a push, and on the
commit meant for production (docs/RELEASE_CHECKLIST.md section 0).

```
Xvfb :99 -screen 0 1920x1080x24 &
DISPLAY=:99 build/quark --uitest uitest-report.json      # ~10-15 minutes, exit code 0 = pass
DISPLAY=:99 xvfb-run -a ctest --test-dir build -R uitest  # the same through ctest (skipped without a display)
```

Development options: `--uitest-only menus,host-ffa` runs some scenarios, `--uitest-path settings` only explores the
screens under one path, and `QUARK_UITEST_VERBOSE=1` prints every activation as it happens.

It runs with its own data folder (a fresh temp dir: your settings, maps and keys are never read or changed), the network
off (`QUARK_MASTER=off`, `QUARK_PUBLIC_IP=off`: no server list, ranked, queue or Discord requests), a host port of its
own, and URLs / folders / quitting intercepted.

## How it works

Every widget call (`Ui::button`, `toggle`, `cycle`, `slider`, `textField`) takes a **`UiExp`** — the expectation:
what activating it is meant to do. It's a required argument, so a widget can't be added without one. In normal play
it's a few pointers on the stack and one null check; nothing is recorded (`Ui::probe` is null).

While the test runs, the `Ui` reports every widget it draws each frame (rect, label, value, expectation). The test then:

1. **Walks every reachable screen** from each scenario's starting point, breadth first: it activates each widget it
   hasn't tested yet in that scenario, and every new state that leads to (a screen, a selection, a toggle showing more
   controls) is explored later by replaying the clicks that led there.
2. **Activates** each widget the way a player would: a button / toggle is clicked, a cycle's `>` is clicked (then `<`
   must go back), a slider is dragged and then a number typed into it, a text field gets typed into and Enter, a keybind slot is clicked and given a key, a hotkey is pressed, a 3D-view handle is dragged.
3. **Checks the expectation** (and that a toggle / cycle / slider / text field's own value changed):

| UiExp | checked |
|---|---|
| `UiExp::nav("host")` | the screen becomes `host` (`Game::uiScreenPath`). `"x*"`: starts with x; `"~x"`: ends with x (the same page in the menus and in game) |
| `UiExp::cfg("fov")` | that config key changed (`"hud_*"`: any key starting with it), reads back the same through quark.cfg, and is on disk once the screen is left (Esc / BACK) |
| `UiExp::state("widgets")` | the observable (`Game::uiObserve`) changed; `state("hosting", "1")`: became that value |
| `UiExp::cmd("team spec")` | a server command starting with that was sent. In game it's recorded and kept from the server, so the game stays as it was for the next widget |
| `UiExp::edit()` | the editor's map changed, there's an undo step, and Ctrl+Z puts the map back exactly (in an edit session: through the server) |
| `UiExp::bind()` | a keybind slot: it captures the next key, which is saved to quark.cfg at once |
| `UiExp::url()` / `quit()` | a URL / folder was opened / the game would quit (both intercepted) |
| `UiExp::local()` | only the widget's own value changes (a field read later, a list position) |
| `UiExp::inert()` | nothing changes (and a greyed-out widget is always checked this way) |

   Modifiers: `.withId("MAP")` gives a stable id when the label shows data (a name, a number); `.typing("fov 111")`
   is what's typed into a text field (default `7`); `.optional()` for data rows that may be gone (server list rows);
   `.clipOk()` when a label is meant to be cut off.
   **Every slider takes a typed number**: after the drag, its number is clicked and a value typed that's off any
   grid a drag snaps to (37% along, in the slider's own decimals, ending in 7). The slider must show exactly that
   (to its decimals), still a couple of frames later; a `cfg` slider's value must come back the same from quark.cfg
   read back as at the next start, and a custom mode's (`state("custom")`) from the `.qmode` text read back.
   Callers that snap a dragged value (0.05 steps, quarter pixels, 15 degrees, 30 s) snap only when
   `!ui_.typed()`; typed values are only clamped to the range the file allows. Then the slider is **right-clicked**:
   it must show its default (`Ui::slider`'s last argument), read back the same way, and not start typing.
4. **Esc from every screen** must go back to a screen on the way to it.
5. **Layout**, at 1280x720 while exploring, then every screen again at 1920x1080 and 1024x768 (4:3): no widget off
   screen, no two widgets overlapping, no label clipped (`Ui::fit` shrinking a label is fine; cutting it off isn't).
6. **The coverage gate**: the test fails if a widget has no expectation (`UiExp::NONE`), fails its check, or was seen
   but never tested. Every failure, untested widget, layout and Esc problem is listed in the summary and the report.

Things that aren't widgets register themselves only while the test runs (`Game::uiTestActions`, `uiTestHandles`):
in-game hotkeys (menu, chat, console, scoreboard, votes, perf), the chat / console input, the spectator's
click-to-watch-next, the editor's 3D view (click to select), the selection's drag handles and the editor hotkeys.

### Scenarios

| scenario | starts at | what's different there |
|---|---|---|
| `host-ffa` | hosting free for all (temple, bots) | every in-game screen: pause menu (host controls), votes, queues, live map / mode pickers, all settings pages in game, HUD widget editor, chat, console, hotkeys |
| `host-vs` | hosting team vs | JOIN TEAM A / B |
| `host-tourney` | hosting tourney | warmup votes: tourney maps / rulesets (multi-select pickers) |
| `host-ranked` | hosting ranked 1v1 | no map editing, LADDER / RANKED OFF votes, no bots |
| `client-spectator` | another server's game, spectating | DISCONNECT, map / mode changes as votes, click to watch the next player |
| `demo` | watching a demo | RESUME / STOP DEMO / SETTINGS / QUIT |
| `edit-host` | hosting an edit session from the editor | PLAY MODE, END SESSION, SAVE AS, every editor tool through the server |
| `edit-guest` | someone else's edit session | VOTE: PLAY IT, SAVE A COPY, LEAVE and the "save your copy?" prompt |
| `hide-seeker` | hosting hide and seek, seeking | the locator's slot key (0) |
| `hide-rps` | hosting hide and seek, at the tiebreak | rock / paper / scissors (slot keys 1 / 2 / 3) |
| `editor-builtin` | the offline editor, a built-in map with edits | the "keep your edits as a new map?" prompt from each way out (save, leave, test, host, open); only that prompt's buttons |
| `menus` | the main menu, offline | everything else: join, host (custom modes: every tab), vs bots, settings (every page, FONT too: each font is picked and the layout checked in it), leaderboard, info, demos, the offline map editor (every tool, material, selection kind, handle) |

The same widget is tested once per scenario and screen (`<scenario> :: <screen> [<what it acts on>] > <id>`); the
editor's selection tools are tested once per selection kind (box, spawn, light, site, hill, checkpoint, path, ladder,
group / room / stairs).

Settings > FONT (`settings/fonts`): each font entry is a `cfg("ui_font")` button (the one in use: `inert()`), with its
name as a stable id whichever is in use; REFRESH is `state("fontscan")`, OPEN FOLDER `url()`. The test puts the
settings back between widgets, and Game::frame applies `ui_font` when it differs, so every check after a font pick runs
in the default font again. The entries depend on the machine (a game-styled entry is listed only when its look-alike
is built in or the real font is found), so the number of widgets there can differ between computers.

## Adding a widget

Give the widget call its `UiExp`, declaring what it does in the state it's drawn in:

```cpp
if (ui_.button(x, y, w, h, "SPECTATE VOTE", UiExp::cmd("callvote spectate"))) net_.sendCommand("callvote spectate");
ui_.toggle(x, y, w, h, "DISABLE CHAT", cfg_.chatOff, UiExp::cfg("chat_off"));                  // + the key in Settings
if (ui_.button(x, y, w, h, "ROCK", UiExp::cmd("rps rock"))) ...                                  // a minigame choice
if (ui_.button(x, y, w, h, "KEEP AS NEW MAP", UiExp::state("usermaps"))) ...                      // a file written
if (ui_.button(x, y, w, h, "LOCATOR", UiExp::nav("ingame/pause/locator"))) ...                    // a new page
```

- A **new screen or overlay** needs a name in `Game::uiScreenPath` (src/client/uitest.cpp), so NAV can point at it
  and Esc can be checked; its buttons then say where they lead.
- A **new kind of effect** that none of the observables shows: add one to `Game::uiObserve` and use `UiExp::state`.
- A **new setting**: `UiExp::cfg("<its quark.cfg key>")` — the test checks the round trip through the file too, so
  the key must be written by `Settings::toText` and read back by `Settings::set` unchanged.
- A **new in-game hotkey** or anything clickable that isn't a `Ui` widget: register it in `Game::uiTestActions` with
  `ui_.keyAction` / `ui_.view` / `ui_.handle` (only called while the test runs).
- A **button that can't do anything right now** should be greyed out (`enabled = false`): the test checks greyed-out
  widgets change nothing. If it stays enabled, declare `UiExp::inert()` for that state.
- A **new situation** (a mode whose menus differ, a new kind of session): add a scenario in `UiTest::scenarios()`
  (`only` limits it to some widgets when the rest of its screens are another scenario's).
- A **new slider**: `Ui::slider` already takes typed numbers. Snap a dragged value only when `!ui_.typed()`, and
  clamp to the range its file (quark.cfg, .qmode, the map) reads back, not to the drag range. Its last argument is
  its default (what a right-click puts back): take it from `Game::defaultSettings()`, a default `CustomRules` or the
  constant the thing is created with, never a second copy of the number.
- A **modal** (a prompt over a panel): set `ui_.muted = true` while drawing what's under it, so only the prompt's
  widgets are reported.
- A command that must really reach the server during the test (rare: the edit session's `ed` ops do) goes in
  `UiTest::cmdHook`'s pass list.

If the test then fails, the summary says which widget, on which screen, in which scenario, expected what, and what
happened instead (`build/uitest-report.json` has the same, plus widgets per screen).
