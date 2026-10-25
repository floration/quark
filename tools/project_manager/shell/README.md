# Quark Project: the Electron shell

One window ("Quark Project") that holds the project tabs: the Supervisor Board (PM page), the GitHub / Claude / Editor
managers and the editors. The contract is `../SPEC.md`, section 1. No npm packages and no build step: it runs on the
plain prebuilt Electron that `setup.ps1` downloads.

```
Quark\project-manager\electron\electron.exe Quark\project-manager\shell [--tab=<id>] [--quark=<Quark folder>]
```

## Files

| file | what |
|---|---|
| `main.js` | window, tab views, tabs.json, server start / stop, navigation lock, permissions, downloads, shortcuts |
| `preload.js` | `window.quarkShell` in every tab, `window.quarkTabbar` in the tab bar |
| `tabbar.html` / `.css` / `.js` | the 34 px strip: Q logo, tabs with status dots, the Editor Manager's facts, reload, Dallas time |
| `tabs.default.json` | used when `Quark\project-manager\tabs.json` is missing (setup can copy it there) |
| `guard.py` | stops the servers the shell started when the shell dies without closing (see Supervisor) |
| `electron-swap.ps1` | puts a downloaded Electron (`electron.next\`) in place after the app exits (see Electron updates) |
| `package.json` | app name / version, entry point. `quarkShell.version` and `shell-status.json`'s `shellVersion` are this version plus a hash of the shell's files (`1.1.0+1a2b3c4d`), so they tell which copy runs; bump the version when the shell changes in a way q should notice |

Next to the `shell` folder (`Quark\project-manager\`): `tabs.json` and `quark.ico`.

The shell's own data lives in `%LOCALAPPDATA%\QuarkProject\` on Windows (off the Desktop, so OneDrive never syncs a
live Chromium profile) and in `Quark\project-manager\` elsewhere: `logs\shell-errors.log` (main-process errors; they never show a modal box), `shell-status.json` (what the tab bar shows: each tab's status, port, pid, plus selfUpdated and the last automatic relaunch; for checking the app without a screen, rewritten when it changes), `state.json` (window bounds, maximized, last tab,
last download folder; an older `Quark\project-manager\state.json` is read once if the new one is missing),
`electron-data\` (the Chromium profile: one `persist:<id>` partition per tab, so each editor's IndexedDB /
localStorage autosaves stay separate) and `logs\<id>.log` (output of servers the shell started, with a timestamped
line for each start, exit and watchdog decision; rewritten on the first start of a session, appended to on restarts,
capped near 4 MB).

The desktop shortcut "Quark Project" (made by `setup.ps1`) targets `electron\electron.exe` with the argument
`"<PM>\shell"`: no cmd.exe, so no console window. Without Electron (`setup.ps1 -NoElectron`) it targets `wscript.exe
project-manager-hidden.vbs`, which runs `project-manager.cmd --edge` with window style 0 (hidden).

## Paths

`%PM%` = the folder holding `shell\` (`Quark\project-manager`), `%QUARK%` = its parent, or `--quark=<dir>`.

## tabs.json

Same rules as SPEC.md, plus two optional fields:

- `parent`: the id of the tab this one sits under. The default tree is Supervisor Board → Play, Claude Manager,
  GitHub Manager (→ Tests), Editor Manager → the editors. Tabs are ordered depth first (siblings keep their file order); Ctrl+1..8 and
  Ctrl+Tab follow that order (Ctrl+1..8 counting only the tabs the bar shows, so each hint held under Ctrl matches its
  key), Ctrl+9 is the last shown tab. The top level and its children are always shown; deeper tabs (the editors) are shown as
  a labelled group after their parent while the parent or one of them is active, otherwise the parent shows a caret
  with the number of hidden tabs. An unknown parent or a cycle makes the tab top level.
- `childLabel`: the small label in front of a parent's group of child tabs (default: the parent's name).
- `keepAlive: true`: the shell keeps that local server running (see Supervisor below). Default tabs: every local server
  (Supervisor Board, GitHub, Tests, Claude, Editor Manager and the Map, Bot, Gamemode, Physics and Weapons editors), so every
  dot is green without opening the tab (q, 2026-10-24). Each is an idle Python server, so running them all costs little.
- `lowPriority: true`: the server runs at idle CPU priority (what `start /low` gave the GitHub manager).

Adding a tool is one entry: `id`, `name`, `parent`, then `url` + `port` + `folder` + `start` for a server
(`"start": "py -3 serve.py --port <port> --no-open"`, run in the folder), or `folder` (+ `file`) for a page.
`setup.ps1` (`server.py --init`) adds default tabs that are new since q's tabs.json was written (listed in its
`defaultsSeen`), gives default tabs a new default start command only when they still have an older default one, and
adds `keepAlive` / `lowPriority` when a default tab does not set them; a default url that changed is given once to a
tab that still has the old one (`URL_ONCE` in pm/tabs.py, recorded in `defaultsUpgraded`; so far the Map Editor's
`?embed=1`); q's own edits and removed tabs stay (the previous file is kept as `tabs.json.old`).

tabs.json is re-read when it changes (each update poll, 15 s) and with Ctrl+Shift+R. A tab whose url changed is
reloaded, unless its page has unsaved changes: then it keeps its old entry and page until they are saved (tried again
each poll). A tabs.json that cannot be read (a typo, a save half written) keeps the tabs already loaded and shows
the error in the bar; only at app start does a bad file fall back to `tabs.default.json`.

Server tabs (`url` on 127.0.0.1 / localhost): probed with `GET /` (500 ms). If down, `start` (or the first `*.cmd` in the
folder whose name contains the folder name, else `start.cmd`) runs hidden in the tab's folder (`%PM%` for a tab without
a folder) with `QUARK_SHELL=1` in its environment; a relative program (`run.cmd`) is the file in that folder; `py -3 …`
falls back to `python …` when the `py` launcher is missing; `%PM%` / `%QUARK%` are expanded in `start`. The tab shows
"starting" and loads when the port answers (up to 20 s), else "not running" with the exit code and the last lines of
output and a Start button. A "not running" tab loads by itself when its server comes up later. On window close the
shell kills what it started (`taskkill /T /F /PID`); servers that were already running are left alone. When the shell
dies without closing (a crash, End task), `guard.py` stops them (Supervisor below).

Hidden, always: commands run as `cmd.exe /d /s /c "<start>"` with `windowsHide` and piped output, never `detached`
and never through `start` (with DETACHED_PROCESS, or a `start /min`, Windows gives `py.exe` → `python.exe` a console
window). That is why the managers' default `start` commands run their Python server directly instead of their
`.cmd` launchers (which use `start /min`). A `lowPriority` tab whose `start` is a plain `program args` command is
spawned without cmd.exe and its priority lowered at once, so `python.exe` (a child of `py.exe`) inherits it; through
cmd.exe the listening process is lowered as well once the port answers.

## Supervisor (keepAlive tabs)

- At app start every `keepAlive` server that is down is started at once (in parallel, hidden), not when its tab is
  first shown. A tab opened meanwhile shows "starting" and loads when its server answers.
- A watchdog probes the keepAlive servers every 10 s (a miss is probed again at once, 1.5 s timeout, so a busy server
  is not called dead). A shell-started server that exits is noticed at once. A down server is restarted after a wait
  that grows with the restarts in the last 5 minutes: 10 s, 30 s, 60 s, then every 2 minutes. Just before the restart
  the port is probed again (someone else may have started it); a server the shell started that is alive but not
  answering is killed first. Everything goes to `logs\<id>.log`.
- A server that was already running when the app started and later stops is restarted too; the shell then owns it
  (and stops it on window close). One that hangs (holds the port, never answers) is stopped first on Windows when the
  command line of the process listening on its port names the tool's folder (as for update restarts), else left alone.
- Tab dots: green up, amber pulsing starting, amber ring = down and the watchdog restarts it shortly, red down (a
  server that is not kept alive and is not running), grey not checked yet, hollow grey not installed.
  A tab showing the "not running" placeholder says when it restarts and loads the page when the server is back; a
  loaded page is left alone (the tools' pages poll their server and reconnect by themselves). The Start button
  restarts it at once.
- Window close: only what the shell started is stopped; the watchdog stops with it.
- The shell dies without closing: Electron's libuv puts each process it spawns in a kill-on-close job with silent
  breakaway, so only the direct child (cmd.exe / py.exe) died and the real server (python.exe) kept serving old code
  that the next open adopted. `guard.py` starts hidden at app start (python.exe under cmd.exe, so it outlives the shell
  the same way) and gets `add <pid>` / `del <pid>` lines on its stdin for every server the shell starts or sees exit
  (for a start command that hands off and exits, the process that listens). It sleeps in a blocking read; when the pipe
  closes without `quit` (the shell's process is gone) it stops each listed server with its descendants, found by
  parent pid in a process snapshot (the direct child is usually already gone, so `taskkill /T` would miss them) and
  counted only when created after its parent (a reused pid never matches). Off Windows: each server's process group.

## Automatic installs and updates (`pm/updater.py`, `installs.json`)

q never runs an installer by hand: each time the app opens, the Supervisor Board server (one background thread,
started with the server; `server.py --no-update` turns it off) does this, hidden, one step at a time:

1. `Quark\repository\`: `git fetch origin testing`, then `git merge --ff-only origin/testing` when it is on branch
   `testing` with no changes to tracked files (untracked files such as `build-model-editor\` do not count). Otherwise
   it does nothing and says why ("has local changes", "is on <branch>, not testing", "fast-forward refused"). Never a
   reset or a force, and never any other folder (`working-repo*` are q's). Git never prompts (`GIT_TERMINAL_PROMPT=0`).
2. Every entry of `installs.json` (read from `repository\tools\project_manager\` right after the pull, else the copy
   next to `server.py`): its stamp is the git object ids of its `sources` at `repository\` HEAD (`git rev-parse
   HEAD:<path>`; a folder gives a tree id, a file a blob id). When the stamp differs from the one in
   `Quark\project-manager\installs-state.json`, or its `check` file is missing, `run` runs hidden (`cmd.exe /d /s /c`
   with CREATE_NO_WINDOW, working folder `repository\`, stdin closed, `QUARK_SHELL=1`, `QUARK_DIR=<Quark>`,
   timeout `timeoutSec`, default 600 s, then the process tree is killed). Output: `Quark\project-manager\logs\install-<id>.log`.
   The new stamp is stored only on exit code 0, so a failed install is tried again on the next app open or "Update
   now". An entry whose first source is not at HEAD yet (not merged) is `waiting` and skipped; a later missing source
   counts as `-` in the stamp.
3. After a successful install of an entry with `restartsPort`, a restart request goes into `GET /api/updates`
   (`restart: [{id, port, name, folder}]`). The shell polls that every 15 s (and asks for one install pass,
   `POST /api/updates/run`, on its first poll) and, per request: a server it started is stopped (`taskkill /T /F`) and
   started again; a running server it did not start is stopped only when the command line of the process listening on
   that port (netstat -ano, then Get-CimInstance Win32_Process, else wmic) contains the tab's folder name, otherwise it
   is left alone and the tab's log says so; a keepAlive server that is down is started; any other down server starts
   with the new code when its tab is opened. A loaded page is then reloaded (a page with unsaved changes asks). Each
   request is acknowledged (`POST /api/updates/ack?id=`) so it never repeats; unacknowledged ones expire after 10 min.
   An entry without `restartsPort` (a page tool: Sound, UI, Model editors) queues a reload request instead
   (`{id, port: null, reload: true, folder, name}`, the folder being the first part of `check`): the shell reloads each
   open file tab in that folder; one with unsaved changes keeps its old page and shows a blue ↻ on its tab ("Updated:
   save, then reload") until its next load.
4. The `project-manager` entry (`"run": "builtin:self-update"`) copies `server.py`, `project-manager.cmd`,
   `project-manager-hidden.vbs`, `installs.json`, `pm\`, `web\` and `shell\` from `repository\tools\project_manager`
   into `Quark\project-manager\` (file by file, only what changed, files deleted from the repo go; `tabs.json`,
   `config.json`, `history.jsonl`, `state.json`, `installs-state.json`, `logs\` and `electron\` are never touched),
   then runs the new `server.py --init` (tabs.json gets new default tabs). When any file changed, `selfUpdated` is set
   and the tab bar shows "Quark Project updated — reopen to finish" with a Restart button (it asks, then stops the old
   Supervisor Board server and the servers the shell started, and relaunches the app, whose supervisor starts the new
   server). When the update is found within 5 minutes of opening the app (the usual case: the pass runs at open), the
   shell does that relaunch by itself without asking, so one open is enough; at most once every 10 minutes, only once
   `/api/updates` says the install pass is over (`running: false`; a relaunch stops the Supervisor Board server and with
   it the install under way, a 30 min build that would start again from zero), only within those 5 minutes, and never
   while a tab has unsaved changes: otherwise the Restart button stays. Restart pressed while installs run offers
   "Restart when the installs finish" (the banner then says so), "Restart now" or "Later". A Restart asks once more
   when tabs have unsaved changes, naming them.
   A tool installed while the app is open starts right away too: each update poll (15 s) re-reads the tab list while
   a tab is not installed or tabs.json changed, and when anything differs reloads it, so the new tool's tab appears
   and its keepAlive server starts without another open.

5. The `electron` entry (`"run": "builtin:electron-update"`, last, no sources): see Electron updates below.

The Supervisor Board's "Tools & updates" card shows the repository state, each entry with a status pill (`ok`,
`updating`, `failed`, `pending`, `waiting`, `disabled`), the last run time (Dallas) and the log's last lines, and an
"Update now" button (`POST /api/updates/run`, at most once per 60 s).

### installs.json

```json
{"entries": [
  {"id": "docs", "name": "Claude Manager", "sources": ["tools/docs_manager"],
   "run": "py -3 tools\\docs_manager\\install.py", "check": "docs-manager\\index.html", "restartsPort": 8766}
]}
```

| field | |
|---|---|
| `id` | letters, digits, `_ . -` (names the log and the state) |
| `name` | shown on the card |
| `sources` | repo paths (folders or files); the first one is the tool's own folder (none for `builtin:electron-update`) |
| `run` | the install command, run in `repository\`; `%QUARK%`, `%PM%`, `%REPO%` are expanded; `py -3` becomes `python` when the py launcher is missing; `builtin:self-update` is the app itself, `builtin:electron-update` its Electron. Pass the Quark folder to installers that take an output folder (`install.cmd "%QUARK%\model-editor"`): their own default is `%USERPROFILE%\Desktop\Quark`, which is not always it |
| `check` | optional: a file relative to `Quark\` that must exist (installs again when it is missing) |
| `restartsPort` | optional: a port or a list of ports whose server is restarted after an install (without it, the open tabs of the folder in `check` reload) |
| `timeoutSec` | optional, default 600 |
| `lowPriority` | optional: run at below-normal CPU priority (builds), inherited by everything it starts |
| `enabled` | optional: `false` skips the entry |

Every tab's tool is in the registry (the UI Editor, `ui-customizer\`, since 2026-10-24: `tools/ui_customizer/install.py`).
A new tool with an installer is one more line, e.g.

```
{"id": "gamemode", "name": "Gamemode Editor", "sources": ["tools/<its folder>"], "run": "<its installer command>",
 "check": "gamemode-editor\\<a file it installs>", "restartsPort": 8771}
```

## Electron updates (`builtin:electron-update`, `electron-swap.ps1`)

`setup.ps1` installs the latest stable Electron once; after that the app keeps it current by itself. On an install
pass, at most every 12 hours (a failed check is tried again on the next pass), the updater asks
`api.github.com/repos/electron/electron/releases/latest` (the newest stable release: never a beta or a draft). When it
is newer than `electron\VERSION`, it downloads `electron-v<ver>-win32-x64.zip` and that release's `SHASUMS256.txt`,
checks the zip's sha256 (streamed, capped at 400 MB), unpacks it into `electron.next.tmp\`, writes its `VERSION` and
renames it to `electron.next\`. `electron\` itself is never touched while the app runs (Windows cannot replace a
running exe). With no `electron\` (setup `-NoElectron`) nothing is downloaded. The Tools & updates row shows the
installed version (-> the latest stable one when newer), a "downloaded: restart to finish" pill while `electron.next\`
waits, and the note ("Electron 45.1.0 is downloaded; it replaces 44.9.2 when Quark Project next closes or restarts").
Between two checks the row is rebuilt from `electron\VERSION` and `electron.next\` (local files only), so it follows
the swap at once instead of repeating the last check's note for up to 12 hours.

When the window closes, or the app relaunches, and `electron.next\` is complete, the shell starts
`electron-swap.ps1` hidden (through cmd.exe, so libuv's job does not take it down with the shell; paths and arguments
go in `QUARK_SWAP_*` environment variables). It waits for the shell's process to end, renames `electron\` to
`electron.old\` and `electron.next\` to `electron\` (each rename retried for 30 s while Electron's helper processes let
go; the first is put back if the second fails) and, on a relaunch, starts the app with the same arguments. If the swap
never works the old Electron stays and the next close tries again.

Rollback (a new Electron major can change an API `main.js` uses): `electron.old\` stays while the new Electron is on
probation. `main.js` checks that first thing, before any Electron call: once its window is up it writes
`electron.old\window-opened` and deletes `electron.old\` a minute later. If the window has not opened within 60 s (the
error went to `shell-errors.log` and the process lives on with no window), it starts `electron-swap.ps1` in rollback
mode and exits: the script puts `electron.old\` back as `electron\`, deletes the new one, writes its version to
`electron.rejected` and opens the app again. On a relaunch the script also watches the new Electron for 40 s and does
the same when it exits before its window opened (a crash before `main.js` runs). The updater never downloads a version
in `electron.rejected` again (the row says so); a newer release is tried. A native crash on a start q makes by hand
(after a close, not a relaunch) is not caught: then rename `electron\` away and `electron.old\` back by hand. Log:
`logs\electron-swap.log` (and `logs\updates.log`). `shell-status.json` shows `electron` (running), `electronStaged` and
`electronProbation`.

## Behaviour

- Navigation: a server tab stays on its origin; a file tab stays inside its file's folder. A link to another tab's
  origin switches to that tab (with the link's `#hash`); an `https:` link opens in the default browser; anything else is
  dropped. `window.open` / `target=_blank` never opens a window.
- `window.quarkShell.open(tabId, hash)` switches tab and sets `location.hash` (on first open the page loads with that
  hash). `quarkShell.version` is the shell version. `quarkShell.retry()` is for the shell's own placeholder page.
  `quarkShell.setBadge(n)` (accepted from the `project` tab only) shows the Supervisor Board's halting count as a red
  badge on its tab button and, on Windows, a red dot over the taskbar icon (elsewhere the app badge count).
- Permissions: `fileSystem`, `clipboard-sanitized-write`, `media` and `pointerLock` for local pages (file:// and 127.0.0.1 /
  localhost); everything else is denied (notifications, geolocation, HID / USB / serial, ...).
- File System Access (`showDirectoryPicker`, `showSaveFilePicker`, handles kept in IndexedDB) works in file:// and local
  server tabs. Chromium refuses some folders (a whole user folder such as Desktop or Documents, the home folder,
  AppData, system folders): the shell then asks "Allow this folder / Choose another / Cancel" instead of failing silently.
- Downloads open a Save dialog in the last folder used.
- A page with unsaved changes (`beforeunload`) asks before Ctrl+R / F5 reloads it. Closing the window or restarting
  the app asks once, naming every tab with unsaved changes (Cancel shows the first). Each page is asked by running a
  cancelable `beforeunload` event through its own handlers (nothing unloads; editors that save a draft on beforeunload
  save it once more); a page that does not answer within 1.5 s counts as clean, so a hung page never blocks closing.
- Right-click in a page: Undo, Redo, Cut, Copy, Paste, Paste as plain text, Select all (enabled by what the page
  allows there), Open / Copy link for links, Find (the selection), Reload, Inspect element. Right-click on a tab: Open,
  Reload, Move left / right, Open its log, Developer tools.
- Tab order: drag a tab among its siblings (an accent line shows where it lands; the press opens the tab at once, as in
  a browser, and Esc, in the bar or the page, cancels the drag and gives the previous tab back), Ctrl+Shift+PageUp / PageDown, or
  Move left / right in its menu. The order is written into `tabs.json` (made from `tabs.default.json` when q has none,
  with its `defaults*` keys, so a later `server.py --init` never takes the dragged order for an older default order and
  "upgrades" it back), keeping every other field; a parent's children move with it.
- Narrow windows: first the Editor Manager's facts shrink (below), then the tabs shorten (inactive tabs drop
  "Editor" / "Manager", "Editor Manager" becomes "Editor Mgr", then they end in an ellipsis; the active tab keeps its
  whole name; the full name is the tooltip) and the facts come back at the fullest level that fits beside the short
  names, then the tabs that still do not fit go behind a "+N" button (a list of them; the strip also fades out and scrolls
  with the mouse wheel).
- Zoom: Ctrl+= / Ctrl+- / Ctrl+mouse wheel (at most one 10 % step per 40 ms) for every tab, Ctrl+0 back to 100 %.
  While the zoom is not 100 % a pill in the bar shows it; a click resets it.
- Find: Ctrl+F opens a find box in the tab bar (no extra view): match count, Enter / F3 next, Shift+Enter / Shift+F3
  previous, Esc closes; switching tabs closes it. A page that handles Ctrl+F itself (the GitHub Manager's log search)
  keeps it: preload.js asks the shell only when the page did not call `preventDefault()`.
- Hold Ctrl in any tab to see the Ctrl+N numbers on the tabs.
- The taskbar button flashes when the Supervisor Board's halting count rises while the window is not focused.
- Keys: Ctrl+1..8 tab (of the ones shown), Ctrl+9 last shown tab, Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PageDown / PageUp) cycle, Ctrl+Shift+PageUp /
  PageDown move the active tab, Ctrl+R / F5 reload (Ctrl+F5 without cache), Ctrl+Shift+R re-read tabs.json, Ctrl+F find,
  F11 full screen, Ctrl+= / Ctrl+- / Ctrl+wheel zoom every tab (Ctrl+0: back to 100%), F12 devtools of the active tab. No menu bar and no title bar: the tab bar moves the window (drag its empty space, double-click to maximize) and the window buttons sit at its right end; the window opens maximized the first time.
- One instance: starting it again focuses the window (and switches to `--tab=<id>` if given).

## The Play tab

The `play` tab is `http://127.0.0.1:8768/play?embed=1`, the Supervisor Board server's own page (SPEC.md 3b). Two tabs
on one port share one server: a start of one waits for a start of the other already under way, update restarts skip
the PM port, and a link to `127.0.0.1:8768/play` opens the Play tab (tabs on one origin are told apart by path).
The shell polls `GET /api/play/pending` every 2 s while a game runs or a note waits, else every 15 s (and at once when
the Play page calls `quarkShell.playChanged()` after starting a game or saving a note). The count of notes waiting is
a blue badge on the Play tab. When a new note waits (a game it started closed), the shell opens the Play tab at
`#note=<session>`, restores and shows the window, puts it on top for a moment and focuses it; when Windows refuses the
focus the taskbar button flashes until the window is focused. Notes already waiting when the app starts only badge.
`server.py --init` (tabs.upgrade) puts a new default tab right after the default tab before it that q's tabs.json has
(Play: right after the Supervisor Board, its first child), not at the end; a Play tab an older upgrade appended (or
one without its `parent`) is moved there once (`defaultsPlaced` in tabs.json), after which q's placement stays.
A reorder of the defaults (`MOVE_ONCE` in pm/tabs.py; so far the Claude Manager moved before the GitHub Manager, q
2026-10-24) is applied once to a tabs.json that still has the older default order (the default ids it has in the old
relative order, the two tabs under the same parent) and recorded in `defaultsMoved`; an order q chose stays as it is.
A note form whose session was saved or skipped elsewhere closes on the next refresh; Save / Skip on it answer 409
`already-noted` (Skip of a skipped session is an ok no-op), so a note is never written twice.

## The Editor Manager's facts (tab bar, right side)

After the tabs, before reload and the clock, every tab shows the Editor Manager's snapshot: Testing version, Production
version, Human-made % with a small human / third-party / AI bar, and the time of its last refresh (Dallas) with "no
fetch" / "with fetch". The hover title adds the shas, the tag and the counts; a click opens the Editor Manager tab.
main.js reads `GET http://127.0.0.1:8765/api/summary` from the main process (no Origin header, so the tab bar's CSP
stays `default-src 'none'`) 3 s after start, then every 45 s (15 s while it has no answer, 4x slower minimized) and at
once when the Editor Manager page calls `quarkShell.statsChanged()` after a Refresh or a mark; the tab bar redraws only
when the answer changed. While the Editor Manager does not answer, the last facts stay, dimmed. As the window narrows
the facts shrink before the tabs ever do (tabbar.js `fit()`, on render and resize only): short labels, then no
Refreshed, then no Production and bar, then hidden. Embedded (`?embed=1`) the Editor Manager page drops its own header
row (the facts) and puts Refresh / fetch to the right of its view tabs; in a browser it keeps the header.

## Testing without Windows

`--selftest-shot=<png>` (with `--selftest-wait=<ms>`, default 2500) captures the tab bar plus the active tab and quits.
With `QUARK_SHELL_SELFTEST=1`, `--selftest=<file.js>` loads a script that gets the shell's internals and can drive it.
On Linux as root add `--no-sandbox` (that is Chromium's process sandbox; the pages stay `sandbox: true`).
