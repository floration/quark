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
- `keepAlive: true`: the shell keeps that local server running (see Supervisor below). Default tabs: Supervisor Board,
  GitHub, Claude and Editor Manager. The editors start on demand (when their tab is opened).
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

## Behaviour

- Navigation: a server tab stays on its origin; a file tab stays inside its file's folder. A link to another tab's
  origin switches to that tab (with the link's `#hash`); an `https:` link opens in the default browser; anything else is
  dropped. `window.open` / `target=_blank` never opens a window.
- `window.quarkShell.open(tabId, hash)` switches tab and sets `location.hash` (on first open the page loads with that
  hash). `quarkShell.version` is the shell version. `quarkShell.retry()` is for the shell's own placeholder page.
- Permissions: `fileSystem`, `clipboard-sanitized-write` and `media` for local pages (file:// and 127.0.0.1 /
  localhost); everything else is denied (notifications, geolocation, HID / USB / serial, ...).
- File System Access (`showDirectoryPicker`, `showSaveFilePicker`, handles kept in IndexedDB) works in file:// and local
  server tabs. Chromium refuses some folders (a whole user folder such as Desktop or Documents, the home folder,
  AppData, system folders): the shell then asks "Allow this folder / Choose another / Cancel" instead of failing silently.
- Downloads open a Save dialog in the last folder used.
- A page with unsaved changes (`beforeunload`) asks before Ctrl+R / F5 reloads it.
- Keys: Ctrl+1..8 tab, Ctrl+9 last tab, Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PageDown / PageUp) cycle, Ctrl+R / F5 reload (Ctrl+F5
  without cache), Ctrl+Shift+R re-read tabs.json, F12 devtools of the active tab. No menu bar.
- One instance: starting it again focuses the window (and switches to `--tab=<id>` if given).

## Testing without Windows

`--selftest-shot=<png>` (with `--selftest-wait=<ms>`, default 2500) captures the tab bar plus the active tab and quits.
With `QUARK_SHELL_SELFTEST=1`, `--selftest=<file.js>` loads a script that gets the shell's internals and can drive it.
On Linux as root add `--no-sandbox` (that is Chromium's process sandbox; the pages stay `sandbox: true`).
