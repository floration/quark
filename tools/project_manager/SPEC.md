# Quark project manager: spec (director's contract for the workers)

Names (q, 2026-10-24): the PM page is the **Supervisor Board** (top level). Under it: **GitHub Manager** (8767),
**Claude Manager** (8766, the Claude Documents manager), **Editor Manager** (8765, the editor dashboard, editors only),
and under that the Sound, UI, Map, Bot and Model editors. Map editor = server on 8770, bot editor = server on 8769.
The tab tree uses a `parent` field in tabs.json (see `shell/tabs.default.json`, the source of truth for defaults).
Where this file and the code differ, the code and `shell/README.md` win; this was the starting contract.

Two parts, both local tooling on q's Windows PC (`C:\Users\user\Desktop\Quark`, below `Quark\`). Nothing here is game
code. Python 3 standard library only (q's PC has Python 3.14, `py -3`). No secrets in any file; never print a key.

## 1. The app ("Quark Project"), one window that hosts only the project tabs

- **Electron shell** (`tools/project_manager/shell/`: `main.js`, `preload.js`, `tabbar.html`, `package.json`).
  Electron is NOT installed with npm: `setup.ps1` downloads the official prebuilt zip
  (`electron-v<ver>-win32-x64.zip` from github.com/electron/electron releases; version = the latest stable release,
  resolved once at setup and written to `Quark\project-manager\electron\VERSION`), verifies it against that release's
  `SHASUMS256.txt`, unzips to `Quark\project-manager\electron\`. Started as `electron.exe <app folder>`.
- Why Electron and not Edge `--app`: the editors are `file://` pages that use the File System Access API
  (Choose folder / Export to game). Chromium blocks file pickers in cross-origin iframes, and an `--app` window has no
  tab strip, so an iframe shell would break the editors. Electron gives one native window, one `WebContentsView` per
  tab (each loads its tool directly, its own `persist:<id>` session so editors' autosaves never collide), and a
  locked-down navigation policy.
- **Fallback** (no Electron, or `--edge`): `project-manager.cmd --edge` opens Edge (else Chrome) as
  `--app=http://127.0.0.1:8768/ --user-data-dir=Quark\project-manager\profile`, the PM page, whose tab list then opens
  each tool in its own `--app` window with that profile.
- **Tab bar**: a thin top strip (`tabbar.html`, 34 px) with the Q logo, one button per tab (name + status dot:
  green = up, grey = not running, amber = starting), a reload button, and on the right the time in Dallas
  (America/Chicago). Ctrl+1..9 switch tabs, Ctrl+Tab / Ctrl+Shift+Tab cycle, Ctrl+R / F5 reload the active tab,
  F12 devtools for the active tab, Ctrl+Shift+R reload the tab list. Tabs are created lazily on first open and kept
  alive (switching never loses an editor's state).
- **Locked**: no menu bar; `window.open` / `target=_blank` / navigation to anything outside the tab's own origin
  (or, for a `file://` tab, outside its folder) is denied in-app and, if it is an `https://` link, opened in the
  default browser (`shell.openExternal`, only `https:`). No new BrowserWindows. Downloads go to a Save dialog.
  Permissions: grant `fileSystem` / `clipboard-sanitized-write` / `media` (audio playback) / `pointerLock` (physics editor Drive) to local tabs only; deny
  everything else. `contextIsolation: true`, `sandbox: true`, `nodeIntegration: false` for every tab.
- **Servers** (current behaviour, including the keepAlive supervisor and the watchdog: `shell/README.md`): before showing a server tab, main checks `http://127.0.0.1:<port>/` (500 ms timeout). If down and the
  tab has `start`, it spawns that command hidden (`windowsHide`, detached, working dir = the tab's folder), shows
  "starting" and polls up to 20 s, then loads it. The PM server (8768) is started the same way. Servers the shell
  started are stopped when the window closes (kill the process tree with `taskkill /T /F /PID`); servers that were
  already running are left alone.
- **Window**: remembers size / position / last tab in `Quark\project-manager\state.json`. Title "Quark Project".
  Icon `quark.ico` (copied from the repo's `assets/quark.ico`). Single instance (`requestSingleInstanceLock`; a second
  start focuses the window).

### tabs.json (`Quark\project-manager\tabs.json`; the default is written by setup, q may edit)

```json
{"tabs": [
  {"id": "project",   "name": "Project",   "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
   "start": "py -3 \"%PM%\\server.py\""},
  {"id": "dashboard", "name": "Editors",   "url": "http://127.0.0.1:8765/?embed=1", "port": 8765, "folder": "editor-dashboard"},
  {"id": "docs",      "name": "Claude Docs","url": "http://127.0.0.1:8766/?embed=1", "port": 8766, "folder": "docs-manager"},
  {"id": "github",    "name": "GitHub",    "url": "http://127.0.0.1:8767/?embed=1", "port": 8767, "folder": "github-manager"},
  {"id": "ui",    "name": "UI",     "folder": "ui-customizer", "file": "ui-customizer.html"},
  {"id": "sound", "name": "Sound",  "folder": "sound-editor",  "file": "sound-editor.html"},
  {"id": "model", "name": "Models", "folder": "model-editor"},
  {"id": "map",   "name": "Maps",   "folder": "map-editor"},
  {"id": "bot",   "name": "Bots",   "folder": "bot-editor"}
]}
```

Rules: `folder` is relative to `Quark\`. A server tab without `start` uses the first `*.cmd` in its folder whose name
contains the folder name (e.g. `docs-manager.cmd`), else `start.cmd`. A tab without `url`/`file` uses
`<folder>.html`, else `index.html`, else the first `*.html` at the folder's top. A tab whose folder or file is
missing is shown greyed with "not installed". `%PM%` = `Quark\project-manager`, `%QUARK%` = `Quark\`. A future tool
is added by appending one entry. The same resolution is implemented in Python (`tabs.py`) for `/api/tabs` and in
`main.js`; both read the same file.

## 2. The PM server, 127.0.0.1:8768 (`tools/project_manager/server.py` + `pm/*.py`)

Bind 127.0.0.1 only. Started with `py -3 server.py [--quark C:\Users\user\Desktop\Quark] [--port 8768]`; default
Quark folder = the parent of the folder holding server.py when installed (`Quark\project-manager\` →
`Quark\`). A background thread refreshes each source on its own interval; requests are served from memory (never
block on a source). Each source's result carries `{ok, fetchedAt, error?, data}`; one failing source never breaks the
others. `?refresh=1` on `/api/pm` asks for an immediate refresh (rate-limited to once per 10 s).

Endpoints: `GET /` the PM page (`web/index.html`), `/static/...` its files, `GET /api/pm` (below), `GET /api/tabs`
(resolved tabs.json with `installed`, `up`), `GET /api/history?limit=` (the PM's own event log),
`GET /api/health` → `ok`, `GET /api/updates` + `POST /api/updates/run` / `/api/updates/ack?id=` (section 5). CORS: echo `Origin` for `http://127.0.0.1|localhost:8765-8768`.

### Sources (interval)

| key | what | how |
|---|---|---|
| `github` (30 s) | testing sha/version/prerelease, production tag/version, open PRs + check state, running CI runs, last failed run, local copies ahead/behind/dirty | `GET http://127.0.0.1:8767/api/summary`; if down, read `Quark\github-manager\cache\github.json`; if neither, `ok:false, error:"GitHub manager not running"` |
| `docs` (30 s) | in-progress entries (title, thread, threadUrl, status, where, waitingOn, waitingWhy), waiting buckets, open questions for q, recent changes | `GET http://127.0.0.1:8766/api/summary` |
| `editors` (30 s) | testing/production versions, provenance totals + % human + by category, pending marks, programs (id, name, ahead_testing, ahead_production, uncommitted, files_changed), remaining | `GET http://127.0.0.1:8765/api/summary` |
| `builds` (60 s) | the local game folders `production\`, `testing\`, `working\`, `testing-f3\`: version + commit from each `BUILD.txt` (show its raw lines too; format may vary), mtime | local files |
| `repo` (60 s) | `Quark\repository\` (mirror of testing): `VERSION`, newest 8 `changelogs/<v>.md` (version, first 12 lines, mtime), whether `changelogs/full/<VERSION>.md` exists; `git log -12 --format=%H%x1f%h%x1f%cI%x1f%s` | local files + `git` (read-only commands only) |
| `servers` (120 s) | public server list from the ranking Worker: `GET https://quark-master.jamesrandallmcamis.workers.dev/list?v=<version>` for the production and the testing version (one address per line; count them, keep the addresses), and `/queue` raw text (players queued) | HTTPS, 5 s timeout, User-Agent `quark-project-manager` |
| `oracle` (600 s, and on demand at most once per 120 s) | the Oracle box: production game server + Discord bot health | SSH, below |
| `checklist` (300 s) | the section headings of `Quark\repository\docs\RELEASE_CHECKLIST.md` (for the "before production" step list) | local file |

### The Oracle box check (live players: keep it tiny)

One SSH connection per check, never more often than every 120 s, read-only, everything under
`nice -n 19 ionice -c3`, 45 s timeout (login from the PC takes ~11 s), `-o BatchMode=yes -o GSSAPIAuthentication=no -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new`.
Host / user / key path come from `Quark\project-manager\config.json` (`oracle: {host, user, key}`, default
`ubuntu@129.151.8.15`, key `C:\Users\user\Downloads\SSH Keys\ssh-key.key`; Windows OpenSSH `ssh.exe`). If the key file
is missing → `ok:false, error:"SSH key not found"` and no attempt. The remote command (one shell line) prints only:
`uptime -p`, `cat /proc/loadavg`, `free -m | sed -n 2p`, and for each unit in `quark quark-testing quark-discord
quark-update.timer quark-testing-update.timer quark-discord-update.timer`: `systemctl show -p
Id,ActiveState,SubState,ActiveEnterTimestamp <unit>`, plus `journalctl -u quark-discord-update -n 3 --no-pager -o
cat` and `journalctl -u quark-update -n 3 --no-pager -o cat` (journal read may need the user to be in
`systemd-journal`/`adm`; if it prints nothing, report nothing). Never start, stop, restart or write anything there.
Parse into `{uptime, load:[1,5,15], memUsedMb, memTotalMb, units:{name:{active, sub, since}}, botUpdate:[lines],
serverUpdate:[lines]}`.

### Event log

The server appends to `Quark\project-manager\history.jsonl` when something changes between refreshes: testing
version or sha, production tag, a new prerelease, a PR opened/closed, a CI run finished (with conclusion), a unit's
active state, an in-progress entry added / finished, % human changed. `{time, kind, text, link?}`. Kept forever
(it is small); `/api/history` returns newest first.

### `GET /api/pm` shape

```json
{"generatedAt": "...Z",
 "sources": {"github": {"ok": true, "fetchedAt": "...", "error": null, "data": {...as fetched...}}, "docs": {...},
             "editors": {...}, "builds": {...}, "repo": {...}, "servers": {...}, "oracle": {...}, "checklist": {...}},
 "derived": {
   "versions": {"production": "1.18.x", "testing": "1.18.6", "testingPrerelease": "test-...", "working": "...",
                "nextFree": "1.18.7"},
   "pipeline": [{"stage": "working|testing|production", "version": "...", "sha": "...", "state": "ok|ahead|behind|unknown", "note": "..."}],
   "inProgress": [{"title", "thread", "threadUrl", "status", "where", "waitingOn", "waitingWhy"}],
   "nextSteps": [{"text", "why", "owner": "q|claude", "link"}],
   "attention": [{"level": "red|amber", "text", "link"}],
   "halting": [{"id", "title", "what", "blocks", "thread", "threadUrl", "since", "link", "source"}], "haltingCount": 1,
   "humanPct": 16.9}}
```

Derived rules (keep them simple and explainable; each item says where it came from):
- `nextFree` = testing VERSION with the patch +1 (informational; the coordinator assigns versions).
- `nextSteps`: open questions for q (owner q); in-progress entries waiting on owner / push (owner q); entries waiting on
  PR / CI / tests / claude (owner claude); "Hand-test <testing version> with RELEASE_CHECKLIST" when testing is ahead of
  production; local copies with unpushed commits ("working-repo has N commits not on testing").
- `attention`: last CI run failed on testing; a unit on the Oracle box not active; a source down; production server
  list empty; uncommitted changes in a local copy older than a day.
- `halting` (top priority: something only q can do is blocking a Claude agent; tests waiting do not count): the Claude
  Manager's `halting` list plus in-progress entries whose status / priority / waitingOn says halting (or `(HALT)`),
  newest first; then the board's own (`source` oracle: the SSH check fails in a way only q can fix, e.g. "SSH key not
  found" or a timeout; `source` updates, added per request from the updater: repository\ not fast-forwardable, an
  install that failed). An item shown here is left out of `nextSteps` / `attention`.

## 3. The PM page (`tools/project_manager/web/`: `index.html`, `pm.css`, `pm.js`)

One page, no build step, no external requests (no CDN, no web fonts; the system font stack until the shared
`quark-tools.css` arrives from the editor dashboard, then link it and keep `pm.css` for what is PM-specific). Light
and dark (`prefers-color-scheme`), CSS custom properties on `:root`. `?embed=1` hides the page's own header (the shell
draws tabs). Polls `/api/pm` every 15 s (and pauses when the page is hidden). Every time shown in Dallas time.
Each card says which source it came from and how old it is; a down source shows a grey "not running" card with the
fix ("start the GitHub manager" / "SSH key not found").

Layout (top to bottom):
0. **HALTING — Claude is blocked on you** (only when there is something): a solid red full-width card, each item
   with what to do (bold), what it blocks, since (Dallas time and age), the source and a link (the Claude Manager entry,
   else the thread). The count goes in the page title, "(2) Supervisor Board", in `window.quarkHalting`, and to the
   shell (`quarkShell.setBadge(n)`: a red count on the Supervisor Board tab, a red dot over the taskbar icon).
1. **Release pipeline**: three big stages left to right, working → testing → production, each with version, short
   sha, age, and the arrow between them labelled with what is pending (e.g. "3 commits", "hand-test checklist",
   "PR #9 open"). Plus the testing prerelease tag and `nextFree`.
2. **Needs you** (attention + nextSteps owned by q), each with a link (thread / PR / run / doc entry).
3. **In progress**: one row per entry, grouped by what it waits on (owner, push, PR, CI, tests, Claude), a coloured
   pill for the bucket, the thread link; a small bar chart of counts per bucket.
4. **CI & PRs**: running runs (live, with elapsed time), last failure, open PRs with check state.
5. **Assets**: % human as a big number + a stacked bar human / third-party / AI per category; editors' programs with
   ahead-of-testing / uncommitted counts.
6. **Servers & bot**: production / testing server counts from the list, players queued; the Oracle box card (uptime,
   load, memory bar, each unit as a pill, last update lines), with its age and a "check now" button (POST
   `/api/refresh?source=oracle`, honours the 120 s minimum).
7. **Local copies & builds**: each local game folder and repo copy with version / sha / ahead / behind / dirty.
8. **Updates**: the newest changelogs (version + first lines), and the PM's own event log (latest 30) as a timeline.
9. **Tabs**: when not inside the shell (no `embed`), a row of links to every tab from `/api/tabs`.

Deep links into sibling tabs use their hashes: dashboard `#view=changes&program=<id>`, `#view=assets&category=<cat>`;
docs `#entry=<id>`, `#tab=board`; GitHub `#pr=<n>`, `#run=<id>`, `#branch=<name>`, `#local=<name>`. Inside the shell,
a link to `http://127.0.0.1:876x/...` is sent to the shell as "open that tab at this hash"
(`window.quarkShell?.open(tabId, hash)`, exposed by `preload.js`; in the browser fallback it is a normal link).

## 4. Install (`tools/project_manager/setup.ps1`, `setup.cmd`)

`setup.cmd` (double-click) → `setup.ps1 [-Quark <folder>] [-NoElectron]`: copies `server.py`, `pm\`, `web\`, `shell\`,
`project-manager.cmd`, `quark.ico` into `Quark\project-manager\`; writes `tabs.json` / `config.json` only if missing;
downloads + verifies Electron unless present or `-NoElectron`; makes a desktop shortcut "Quark Project" →
`electron\electron.exe "<PM>\shell"` (no console window; without Electron: `wscript.exe project-manager-hidden.vbs`,
which runs `project-manager.cmd --edge` hidden) with the icon. Re-running it updates the code and keeps q's tabs/config/history/state.
`project-manager.cmd [--edge]` starts the server if 8768 is down, then Electron (or the Edge fallback).

## 5. Automatic installs and updates

After setup, nothing is installed by hand: on each app open the server pulls testing into `Quark\repository\`
(fast-forward only) and runs the installer of every tool in `installs.json` whose sources changed, hidden; the shell
restarts the affected servers and offers a restart when the app updated itself. Details: `shell/README.md`
("Automatic installs and updates") and `pm/updater.py`.
