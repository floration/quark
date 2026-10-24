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

Kept next to the `shell` folder (`Quark\project-manager\`): `tabs.json`, `state.json` (window bounds, maximized, last
tab, last download folder), `quark.ico`, `electron-data\` (the Chromium profile: one `persist:<id>` partition per tab,
so each editor's IndexedDB / localStorage autosaves stay separate) and `logs\<id>.log` (output of servers the shell
started; rewritten on each start, cut at 4 MB).

## Paths

`%PM%` = the folder holding `shell\` (`Quark\project-manager`), `%QUARK%` = its parent, or `--quark=<dir>`.

## tabs.json

Same rules as SPEC.md, plus two optional fields:

- `parent`: the id of the tab this one sits under. The default tree is Supervisor Board → GitHub Manager, Claude
  Manager, Editor Manager → the editors. Tabs are ordered depth first (siblings keep their file order); Ctrl+1..9 and
  Ctrl+Tab follow that order. The top level and its children are always shown; deeper tabs (the editors) are shown as
  a labelled group after their parent while the parent or one of them is active, otherwise the parent shows a caret
  with the number of hidden tabs. An unknown parent or a cycle makes the tab top level.
- `childLabel`: the small label in front of a parent's group of child tabs (default: the parent's name).

Server tabs (`url` on 127.0.0.1 / localhost): probed with `GET /` (500 ms). If down, `start` (or the first `*.cmd` in the
folder whose name contains the folder name, else `start.cmd`) runs hidden in the tab's folder (`%PM%` for a tab without
a folder) with `QUARK_SHELL=1` in its environment; a relative program (`run.cmd`) is the file in that folder; `py -3 …`
falls back to `python …` when the `py` launcher is missing. The tab shows "starting" and loads when the port answers
(up to 20 s), else "not running" with the exit code and the last lines of output and a Start button. A "not running"
tab loads by itself when its server comes up later. On window close the shell kills what it started
(`taskkill /T /F /PID`); servers that were already running are left alone.

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
- Keys: Ctrl+1..9 tab, Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PageDown / PageUp) cycle, Ctrl+R / F5 reload (Ctrl+F5
  without cache), Ctrl+Shift+R re-read tabs.json, F12 devtools of the active tab. No menu bar.
- One instance: starting it again focuses the window (and switches to `--tab=<id>` if given).

## Testing without Windows

`--selftest-shot=<png>` (with `--selftest-wait=<ms>`, default 2500) captures the tab bar plus the active tab and quits.
With `QUARK_SHELL_SELFTEST=1`, `--selftest=<file.js>` loads a script that gets the shell's internals and can drive it.
On Linux as root add `--no-sandbox` (that is Chromium's process sandbox; the pages stay `sandbox: true`).
