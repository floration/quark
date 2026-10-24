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
| `tabbar.html` / `.css` / `.js` | the 34 px strip: Q logo, tabs with status dots, reload, Dallas time |
| `tabs.default.json` | used when `Quark\project-manager\tabs.json` is missing (setup can copy it there) |
| `package.json` | app name / version (`quarkShell.version`), entry point |

Next to the `shell` folder (`Quark\project-manager\`): `tabs.json` and `quark.ico`.

The shell's own data lives in `%LOCALAPPDATA%\QuarkProject\` on Windows (off the Desktop, so OneDrive never syncs a
live Chromium profile) and in `Quark\project-manager\` elsewhere: `state.json` (window bounds, maximized, last tab,
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

- `parent`: the id of the tab this one sits under. The default tree is Supervisor Board → GitHub Manager, Claude
  Manager, Editor Manager → the editors. Tabs are ordered depth first (siblings keep their file order); Ctrl+1..8 and
  Ctrl+Tab follow that order, Ctrl+9 is the last tab. The top level and its children are always shown; deeper tabs (the editors) are shown as
  a labelled group after their parent while the parent or one of them is active, otherwise the parent shows a caret
  with the number of hidden tabs. An unknown parent or a cycle makes the tab top level.
- `childLabel`: the small label in front of a parent's group of child tabs (default: the parent's name).
- `keepAlive: true`: the shell keeps that local server running (see Supervisor below). Default tabs: every local server
  (Supervisor Board, GitHub, Tests, Claude, Editor Manager and the Map, Bot, Gamemode and Physics editors), so every
  dot is green without opening the tab (q, 2026-10-24). Each is an idle Python server, so running them all costs little.
- `lowPriority: true`: the server runs at idle CPU priority (what `start /low` gave the GitHub manager).

Adding a tool is one entry: `id`, `name`, `parent`, then `url` + `port` + `folder` + `start` for a server
(`"start": "py -3 serve.py --port <port> --no-open"`, run in the folder), or `folder` (+ `file`) for a page.
`setup.ps1` (`server.py --init`) adds default tabs that are new since q's tabs.json was written (listed in its
`defaultsSeen`), gives default tabs a new default start command only when they still have an older default one, and
adds `keepAlive` / `lowPriority` when a default tab does not set them; q's own edits and removed tabs stay
(the previous file is kept as `tabs.json.old`).

Server tabs (`url` on 127.0.0.1 / localhost): probed with `GET /` (500 ms). If down, `start` (or the first `*.cmd` in the
folder whose name contains the folder name, else `start.cmd`) runs hidden in the tab's folder (`%PM%` for a tab without
a folder) with `QUARK_SHELL=1` in its environment; a relative program (`run.cmd`) is the file in that folder; `py -3 …`
falls back to `python …` when the `py` launcher is missing; `%PM%` / `%QUARK%` are expanded in `start`. The tab shows
"starting" and loads when the port answers (up to 20 s), else "not running" with the exit code and the last lines of
output and a Start button. A "not running" tab loads by itself when its server comes up later. On window close the
shell kills what it started (`taskkill /T /F /PID`); servers that were already running are left alone.

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
  (and stops it on window close).
- Tab dots: green up, amber pulsing starting, amber ring = down and the watchdog restarts it shortly, grey down.
  A tab showing the "not running" placeholder says when it restarts and loads the page when the server is back; a
  loaded page is left alone (the tools' pages poll their server and reconnect by themselves). The Start button
  restarts it at once.
- Window close: only what the shell started is stopped; the watchdog stops with it.

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
4. The `project-manager` entry (`"run": "builtin:self-update"`) copies `server.py`, `project-manager.cmd`,
   `project-manager-hidden.vbs`, `installs.json`, `pm\`, `web\` and `shell\` from `repository\tools\project_manager`
   into `Quark\project-manager\` (file by file, only what changed, files deleted from the repo go; `tabs.json`,
   `config.json`, `history.jsonl`, `state.json`, `installs-state.json`, `logs\` and `electron\` are never touched),
   then runs the new `server.py --init` (tabs.json gets new default tabs). When any file changed, `selfUpdated` is set
   and the tab bar shows "Quark Project updated — reopen to finish" with a Restart button (it asks, then stops the old
   Supervisor Board server and the servers the shell started, and relaunches the app, whose supervisor starts the new
   server).

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
| `sources` | repo paths (folders or files); the first one is the tool's own folder |
| `run` | the install command, run in `repository\`; `%QUARK%`, `%PM%`, `%REPO%` are expanded; `py -3` becomes `python` when the py launcher is missing; `builtin:self-update` is the app itself |
| `check` | optional: a file relative to `Quark\` that must exist (installs again when it is missing) |
| `restartsPort` | optional: a port or a list of ports whose server is restarted after an install |
| `timeoutSec` | optional, default 600 |
| `lowPriority` | optional: run at below-normal CPU priority (builds), inherited by everything it starts |
| `enabled` | optional: `false` skips the entry |

Not in the registry yet: the UI Editor (`ui-customizer\`), the Gamemode Editor (8771) and the Physics Editor (8772),
whose source folders and installer commands are not decided. When one has an installer, add one line, e.g.

```
{"id": "gamemode", "name": "Gamemode Editor", "sources": ["tools/<its folder>"], "run": "<its installer command>",
 "check": "gamemode-editor\\<a file it installs>", "restartsPort": 8771}
```

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
- A page with unsaved changes (`beforeunload`) asks before Ctrl+R / F5 reloads it.
- Keys: Ctrl+1..8 tab, Ctrl+9 last tab, Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PageDown / PageUp) cycle, Ctrl+R / F5 reload (Ctrl+F5
  without cache), Ctrl+Shift+R re-read tabs.json, F11 full screen, Ctrl+= / Ctrl+- zoom every tab (Ctrl+0: back to 100%), F12 devtools of the active tab. No menu bar and no title bar: the tab bar moves the window (drag its empty space, double-click to maximize) and the window buttons sit at its right end; the window opens maximized the first time.
- One instance: starting it again focuses the window (and switches to `--tab=<id>` if given).

## Testing without Windows

`--selftest-shot=<png>` (with `--selftest-wait=<ms>`, default 2500) captures the tab bar plus the active tab and quits.
With `QUARK_SHELL_SELFTEST=1`, `--selftest=<file.js>` loads a script that gets the shell's internals and can drive it.
On Linux as root add `--no-sandbox` (that is Chromium's process sandbox; the pages stay `sandbox: true`).
