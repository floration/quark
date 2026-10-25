# Quark project manager: spec (director's contract for the workers)

Names (q, 2026-10-24): the PM page is the **Supervisor Board** (top level). Under it (in this order since 2026-10-24): **Claude
Manager** (8766, the Claude Documents manager), **GitHub Manager** (8767), **Editor Manager** (8765, the editor dashboard, editors only),
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
  `SHASUMS256.txt`, unzips to `Quark\project-manager\electron\`. Started as `electron.exe <app folder>`. After that it
  updates itself: the updater stages each newer stable release (same checks) in `electron.next\` and the shell swaps it
  in after it exits (`shell/README.md`, "Electron updates").
- Why Electron and not Edge `--app`: the editors are `file://` pages that use the File System Access API
  (Choose folder / Export to game). Chromium blocks file pickers in cross-origin iframes, and an `--app` window has no
  tab strip, so an iframe shell would break the editors. Electron gives one native window, one `WebContentsView` per
  tab (each loads its tool directly, its own `persist:<id>` session so editors' autosaves never collide), and a
  locked-down navigation policy.
- **Fallback** (no Electron, or `--edge`): `project-manager.cmd --edge` opens Edge (else Chrome) as
  `--app=http://127.0.0.1:8768/ --user-data-dir=Quark\project-manager\profile`, the PM page, whose tab list then opens
  each tool in its own `--app` window with that profile.
- **Tab bar**: a thin top strip (`tabbar.html`, 34 px) with the Q logo, the tab tree (Supervisor Board → Play, Claude
  Manager, GitHub Manager → Tests, Editor Manager → the editors), each with a status dot (green = up, red = not
  running, amber = starting, amber ring = restarting by itself, grey = not checked yet), the Editor Manager's facts, a
  reload button, and on the right the time in Dallas (America/Chicago). Ctrl+1..9 switch tabs, Ctrl+Tab /
  Ctrl+Shift+Tab cycle, Ctrl+R / F5 reload the active tab, F12 devtools for the active tab, Ctrl+Shift+R reload the tab
  list, Ctrl+F find; tabs can be dragged among their siblings (saved to tabs.json) and have a right-click menu. Tabs
  are created lazily on first open and kept alive (switching never loses an editor's state). Closing the window or
  restarting asks first when a tab has unsaved changes.
- **Locked**: no menu bar; `window.open` / `target=_blank` / navigation to anything outside the tab's own origin
  (or, for a `file://` tab, outside its folder) is denied in-app and, if it is an `https://` link, opened in the
  default browser (`shell.openExternal`, only `https:`). No new BrowserWindows. Downloads go to a Save dialog.
  Permissions: grant `fileSystem` / `clipboard-sanitized-write` / `media` (audio playback) / `pointerLock` (physics editor Drive) to local tabs only; deny
  everything else. `contextIsolation: true`, `sandbox: true`, `nodeIntegration: false` for every tab.
- **Servers** (current behaviour, including the keepAlive supervisor and the watchdog: `shell/README.md`): before showing a server tab, main checks `http://127.0.0.1:<port>/` (500 ms timeout). If down and the
  tab has `start`, it spawns that command hidden (`windowsHide`, never `detached` on Windows: that gives `python.exe` a
  console window; working dir = the tab's folder), shows "starting" and polls up to 20 s, then loads it. keepAlive
  servers start with the app and are restarted when they stop. The PM server (8768) is started the same way. Servers
  the shell started are stopped when the window closes (kill the process tree with `taskkill /T /F /PID`), and by
  `shell/guard.py` when the shell dies without closing; servers that were already running are left alone.
- **Window**: remembers size / position / last tab in `state.json` (`%LOCALAPPDATA%\QuarkProject\` on Windows, off the
  OneDrive-synced Desktop). Title "Quark Project".
  Icon `quark.ico` (copied from the repo's `assets/quark.ico`). Single instance (`requestSingleInstanceLock`; a second
  start focuses the window).

### tabs.json (`Quark\project-manager\tabs.json`; the default is written by setup, q may edit)

The defaults are `shell/tabs.default.json` (kept identical to `pm/tabs.py` DEFAULT_TABS by a test). An excerpt:

```json
{"tabs": [
  {"id": "project",   "name": "Supervisor Board", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
   "start": "py -3 \"%PM%\\server.py\"", "keepAlive": true},
  {"id": "dashboard", "name": "Editor Manager", "parent": "project", "url": "http://127.0.0.1:8765/?embed=1", "port": 8765,
   "folder": "editor-dashboard", "start": "py -3 dashboard.py --quark \"%QUARK%\" --no-browser", "keepAlive": true,
   "childLabel": "Editors"},
  {"id": "sound", "name": "Sound Editor", "parent": "dashboard", "folder": "sound-editor", "file": "sound-editor.html"},
  {"id": "map",   "name": "Map Editor", "parent": "dashboard", "url": "http://127.0.0.1:8770/?embed=1", "port": 8770,
   "folder": "map-editor", "start": "py -3 serve.py --port 8770 --no-browser", "keepAlive": true}
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
block on a source). Each source's result carries `{ok, fetchedAt, error?, data, lastOkAt}`; one failing source never
breaks the others. A source that fails keeps its last good data: `{ok: false, kept: true, data: <as of lastOkAt>}`, so
a sibling's auto-update restart never blanks the board (HALT items, in progress, CI & PRs, Assets stay, marked stale);
the event log and the disagreement check use only data from a fetch that worked. `?refresh=1` on `/api/pm` asks for
an immediate refresh (rate-limited to once per 10 s).

Endpoints: `GET /` the PM page (`web/index.html`), `/static/...` its files, `GET /api/pm` (below), `GET /api/tabs`
(resolved tabs.json with `installed`, `up`), `GET /api/history?limit=` (the PM's own event log),
`GET /api/health` → `ok`, `GET /api/updates` + `POST /api/updates/run[?id=<tool>]` / `/api/updates/ack?id=` (section 5;
`?id=` runs one tool's install when the updater supports it, `updates.canRetryOne`, else 501),
`GET /api/updates/log?id=<tool>` (the end of `Quark\project-manager\logs\install-<tool>.log`, at most 256 KB),
`GET /api/checklist` (`{version, mtime, url, sections: [{heading, items: [{text, group}]}], ticks}`),
`POST /api/checklist` (JSON `{version, section, done, build?}`: q's hand-test ticks, kept per testing VERSION in
`Quark\project-manager\checklist-ticks.json`, newest 20 versions; a section is keyed by its heading without the number;
`section` must be a heading of RELEASE_CHECKLIST.md, `done` true or false and `version` not newer than testing, else 400). CORS: echo `Origin` for `http://127.0.0.1|localhost:8765-8768`.

### Sources (interval)

| key | what | how |
|---|---|---|
| `github` (30 s) | testing sha/version/prerelease, production tag/version, open PRs + check state, running CI runs, last failed run, local copies ahead/behind/dirty | `GET http://127.0.0.1:8767/api/summary`; if down, read `Quark\github-manager\cache\github.json`; if neither, `ok:false, error:"GitHub manager not running"` |
| `docs` (30 s) | in-progress entries (title, thread, threadUrl, status, where, waitingOn, waitingWhy), waiting buckets, open questions for q, recent changes | `GET http://127.0.0.1:8766/api/summary` |
| `editors` (30 s) | testing/production versions, provenance totals + % human + by category, pending marks, programs (id, name, ahead_testing, ahead_production, uncommitted, files_changed), remaining | `GET http://127.0.0.1:8765/api/summary` |
| `builds` (60 s) | the local game folders `production\`, `testing\`, `working\`, `testing-f3\`: version + commit from each `BUILD.txt` (show its raw lines too; format may vary), mtime | local files |
| `repo` (60 s) | `Quark\repository\` (mirror of testing): `VERSION`, newest 8 `changelogs/<v>.md` (version, first 12 lines, mtime, `committedAt` = the commit that added it: one `git log --diff-filter=A`, cached per HEAD), whether `changelogs/full/<VERSION>.md` exists; `git log -12 --format=%H%x1f%h%x1f%cI%x1f%s` | local files + `git` (read-only commands only) |
| `servers` (120 s) | public server list from the ranking Worker: `GET https://quark-master.jamesrandallmcamis.workers.dev/list?v=<version>` for the production and the testing version (one address per line; count them, keep the addresses), and `/queue` raw text (players queued) | HTTPS, 5 s timeout, User-Agent `quark-project-manager` |
| `oracle` (600 s, and on demand at most once per 120 s) | the Oracle box: production game server + Discord bot health | SSH, below |
| `checklist` (300 s) | the section headings of `Quark\repository\docs\RELEASE_CHECKLIST.md` (for the "before production" step list) and each section's check items (table rows with ☐, bullets; `group` = the ### heading above) | local file |

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
{"generatedAt": "...Z", "serverNow": 1761335000123,
 "sources": {"github": {"ok": true, "fetchedAt": "...", "error": null, "data": {...as fetched...}}, "docs": {...},
             "editors": {...}, "builds": {...}, "repo": {...}, "servers": {...}, "oracle": {...}, "checklist": {...}},
 "derived": {
   "versions": {"production": "1.18.x", "testing": "1.18.6", "testingPrerelease": "test-...", "working": "...",
                "nextFree": "1.18.7"},
   "pipeline": [{"stage": "working|testing|production", "version": "...", "sha": "...", "state": "ok|ahead|behind|unknown", "note": "..."}],
   "inProgress": [{"title", "thread", "threadUrl", "status", "where", "waitingOn", "waitingWhy"}],
   "nextSteps": [{"text", "why", "owner": "q|claude", "link"}],
   "attention": [{"level": "red|amber", "text", "link"}],
   "halting": [{"id", "title", "what", "blocks", "thread", "threadUrl", "since", "link", "source", "body?", "file?"}], "haltingCount": 1,
   "checklist": {"version": "1.18.6", "done": {"Settings": {"at", "build"}}, "count": 1},
   "humanPct": 16.9}}
```
`serverNow` (ms) is the page's clock; `generatedAt` is the last derive (up to a source interval old). The page's copy is
slim: the raw `data` of `docs`, `github` and `editors` is left out (`{}` with `omitted: true`; `derived` holds what is
shown) and the checklist sends headings and `itemCounts` only (about 20 KB instead of 120 KB); `?raw=1` keeps
everything. Each answer has an `ETag` over everything but `serverNow` (also sent as `X-Server-Now`): the page polls with
`If-None-Match` and a 304 has no body. Items made from a source's kept data carry `stale: true`; `derived.kept` lists
those sources.

Derived rules (keep them simple and explainable; each item says where it came from):
- `nextFree` = testing VERSION with the patch +1 (informational; the coordinator assigns versions).
- `nextSteps`: open questions for q (owner q); in-progress entries waiting on owner / push (owner q); entries waiting on
  PR / CI / tests / claude (owner claude); "Hand-test <testing version> with RELEASE_CHECKLIST" when testing is ahead of
  production; local copies with unpushed commits ("working-repo has N commits not on testing").
- `attention`: last CI run failed on testing; a unit on the Oracle box not active; a source down (with `fix`); production
  server list empty; uncommitted changes in a local copy older than a day; a local source (repository\, the Editor
  Manager) whose testing version / sha or production version differs from the GitHub Manager's for more than 10 minutes
  (`kind: disagree`, with what to do; without GitHub the two local ones are compared).
- Production stage: sha and date fall back to the Editor Manager's sha and `production\BUILD.txt` (publishedAt, else its
  mtime); `versions.productionTag` = GitHub's tag, else the Editor Manager's, BUILD.txt's, else `v<VERSION>`. The
  testing prerelease falls back to `test-<testing\BUILD.txt version>` when that is a -test build of the testing VERSION.
- `halting` (top priority: something only q can do is blocking a Claude agent; tests waiting do not count): the Claude
  Manager's `halting` list plus in-progress entries whose status / priority / waitingOn says halting (or `(HALT)`),
  newest first; then the board's own (`source` oracle: the SSH check fails in a way only q can fix, e.g. "SSH key not
  found" or a timeout; `source` updates, added per request from the updater: repository\ not fast-forwardable, the
  installs that failed: one item for one, one grouped item "N installs failed" for several, with the shared exit code /
  note). An item shown here is left out of `nextSteps` / `attention`. An item from a 07 (questions) entry, and every
  open question, has `answerLink` (the Claude Manager's `#answer=<id>`, its answer box).
- A source that is down is "not running" when it does not answer (fix: start it) and "running but failing" when it
  answers with an error (fix: restart it and read `%LOCALAPPDATA%\QuarkProject\logs\<tab>.log`). Local copies have no
  link into the GitHub Manager's tab while it is not running (`goto: local`: the page's own Local copies card).

## 3. The PM page (`tools/project_manager/web/`: `index.html`, `pm.css`, `pm.js`)

One page, no build step, no external requests (no CDN, no web fonts; the system font stack until the shared
`quark-tools.css` arrives from the editor dashboard, then link it and keep `pm.css` for what is PM-specific). Light
and dark (`prefers-color-scheme`), CSS custom properties on `:root`. `?embed=1` hides the page's own header (the shell
draws tabs). Polls `/api/pm` every 15 s (and pauses when the page is hidden); after Refresh, Check now and Update now it
polls every 1.5-3 s until the new data is in (Check now shows "Checking…"). Each card is drawn to a string and compared
with what it shows; live ages are attributes the 1 s ticker fills, so only a card whose data changed is replaced (text
selection, hover, keyboard focus and open details survive a poll; a checklist tick changes the boxes, bar and counts in
place). Cards flow as masonry (4 px grid rows, each card spans its height): two columns from 1060 px, three from 1800 px,
four from 2200 px, five from 2400 px. Every time shown in Dallas time; on a new Dallas day the cards and the timeline
are redrawn ("today" / "yesterday"). Each card says which source it came from and how old it is; a down source shows a
grey card ("not running" / "running but failing", with the fix and the age of its last good data) and, when the server
kept its last good data, that data greyed below it.

Layout (top to bottom):
0. **HALTING — Claude is blocked on you** (only when there is something): a solid red full-width card, each item
   with what to do (bold), what it blocks, since (Dallas time and age), the source and a link (the Claude Manager entry,
   else the thread). The count goes in the page title, "(2) Supervisor Board", in `window.quarkHalting`, and to the
   shell (`quarkShell.setBadge(n)`: a red count on the Supervisor Board tab, a red dot over the taskbar icon). Each item
   has a Copy button for what to do, and each `code` span of the entry text (a command) gets its own. A HALT that was
   not there at the last poll brings up a red toast at the top and a short chime (Mute on the toast, kept in
   localStorage). Only a HALT that was not there at the last poll counts, and only from a source whose list was already
   in (the Claude Manager starting after the board, or coming back after an outage, is not news); items from a down
   source's kept list stay, marked "last known". The what-to-do text renders its `code` and **bold**; Copy copies it
   without the markup. Several HALTs sit two or three across on a wide screen. A question's HALT has "Answer →" (the
   Claude Manager's `#answer=<id>`: its answer box); answering inline is not possible (the Claude Manager accepts edits
   only from its own page).
1. **Release pipeline**: three big stages left to right, working → testing → production, each with version, short
   sha, age, and the arrow between them labelled with what is pending (e.g. "3 commits", "hand-test checklist",
   "PR #9 open"; a PR whose base is testing, or with no base and a head other than testing, is "PR #N into testing" on the
   first arrow). Plus the testing prerelease tag, the production tag and `nextFree`. "Before production" is the
   checklist with a tick box per section (saved per testing version on the PM server) and a progress bar; each section
   opens to its check items (loaded from `GET /api/checklist` the first time); the arrow and the Needs-you hand-test step
   show "N/M".
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
   **Tools & updates**: repository\ and each tool in installs.json with its state; "Log" opens the tool's whole install
   log in a dialog (`GET /api/updates/log`), "Retry" (a failed tool, when `updates.canRetryOne`) runs just its install.
8. **Updates**: the newest 3 changelogs (version + first lines, dated by the commit that added them; "Show N older" for
   the rest of the 8), and the PM's own
   event log (latest 30) as a timeline; "Full history…" opens a dialog with every event (`/api/history?limit=10000`),
   searchable and filtered by kind.
9. **Tabs**: when not inside the shell (no `embed`), a row of links to every tab from `/api/tabs`.

Deep links into sibling tabs use their hashes: dashboard `#view=changes&program=<id>`, `#view=assets&category=<cat>`;
docs `#entry=<id>`, `#tab=board`; GitHub `#pr=<n>`, `#run=<id>`, `#branch=<name>`, `#local=<name>`. Inside the shell,
a link to `http://127.0.0.1:876x/...` is sent to the shell as "open that tab at this hash"
(`window.quarkShell?.open(tabId, hash)`, exposed by `preload.js`; in the browser fallback it is a normal link).

## 3b. The Play tab (`web/play.html`, `play.js`, `play.css`; `pm/play.py`)

Tab `play` ("Play", under the Supervisor Board) is a page of this same server (`/play`, port 8768; no keepAlive of its
own, the project tab keeps the server up). It lists the local game folders (production, testing, working, testing-f3)
and the older versions installed in `Quark\versions\<tag>\`, each card with Play, its own launch options box
(remembered per build in the page's localStorage, with recent options and a few presets; Enter starts it), Stop while
it runs, and Folder. Below: the play notes, recent games, and the GitHub releases of floration/quark.

Releases: the public API without sign-in, **every page** (100 a page until a short page, like the launcher's
`fetchAll`), cached 10 min; page 1's ETag answering 304 keeps the list; a later page that fails keeps what was read
(and the older releases of the last list) and says so. Refresh is taken at most every 30 s (the button counts down).
Rows show the channel, which local folder holds that release ("in testing"), "newer than testing / production",
and how an installed one was checked. At 1800 px and wider production and testing are two tables side by side.

Install (`quark-windows.zip` into `versions\<tag>`, unpacked without its top folder, `BUILD.txt` like
`fetch_release.ps1`, `play-install.json` with how it was checked) verifies like the launcher (docs/SIGNING.md): from
1.17.0 on (or any release with a `.sig`) the zip's Ed25519ph signature (`quark-windows.zip.sig`, pure-Python check,
same algorithm as `quark_sign verify`) against the release key (`RELEASE_PUBLIC_KEY` = `kReleasePublicKey` in
`src/common/sign.cpp`; a test keeps them equal) and the signed manifest (`quark-release.txt` + `.sig`) naming this
tag, version, channel and the zip's sha256; anything missing or wrong installs nothing. A release claiming 1.17.0+
with no signature is shown "unsigned: not offered". Releases from before signing install without a prompt (owner
decision B6), checked against `SHA256SUMS` when they have one, and are marked "unsigned" / "not checked". Cancel
stops a download, check or unzip and leaves nothing behind. Remove deletes the folder after a confirm.

A game starts hidden from any console (a short-lived starter process, so the shell's `taskkill /T` of this server never
reaches the game), from its own folder, at normal priority; its output goes to `logs\play-<session>.log` (the last 20
kept). Stop asks it to close (Windows: `taskkill /PID` without `/F`, i.e. WM_CLOSE; elsewhere SIGTERM to its process
group) and ends it after 5 s (TerminateProcess / SIGKILL). Recent games flag how each ended (crashed: a signal or a
Windows exception status such as 0xC0000005; error exit: another non-zero code; stopped) and have a Log button: the
log's last 256 KB in a dialog, with Open the file and Logs folder. Sessions are in `project-manager\play-sessions.json`;
when the game exits the session wants a note (`needsNote`) and the page opens the form (did / noticed / want changed /
anything else, up to 4000 characters a box with a counter, line breaks kept; "add the last lines of the game's log",
on by default after a crash or error exit; Save, Skip, Later: Later defers every waiting note for the page session).
"Add a note" writes one without a game (a listed build, or the launcher + a version). A server restart while a game
runs picks the game up again by pid (Windows: only when its creation time matches).

Notes: `project-manager\play-notes.jsonl` (local copy, append-only: an edit or removal is its own record) and one
`open` entry at the top of `## Notes` in `Claude Documents\12-play-notes.md` (line breaks as " / "), committed (that
file only) and pushed; offline it stays committed on the PC and is retried every 5 min. Edit / Remove work while the
entry is still `open` (no agent has taken it): the entry is changed in place or removed (commit "<title> (edited)" /
"(removed)"). The notes card links to `12-play-notes.md` in the Claude Manager tab (`#file=`). The Supervisor Board's
status line shows "Play · N play notes open · M waiting" (`/api/pm` field `play`). When the server doesn't answer the
page shows a banner in plain words and greys out every action. It polls every 2.5 s only while a game runs or an
install / fetch is on (10 s otherwise), and the release list is sent only when it changed (`?rel=<sig>`).

Endpoints: `GET /play`, `GET /api/play[?refresh=1][&rel=<sig>]` (everything the page shows; `releases.list` is null
when the sig matches), `GET /api/play/pending` (`{running, pending: [{id, label, version, endedAt, durationSec}],
openNotes}`, for the shell), `GET /api/play/log?session=<id>`; POST with a JSON body: `/api/play/launch {build, args}`,
`/api/play/note {session, did, noticed, change, other, attachLog}` (no session: `{build}` or `{label, version}`),
`/api/play/skip {session}`, `/api/play/stop {session}`, `/api/play/open {what: build|logs|log|docs|notes-file, build |
session}`, `/api/play/note-edit {id, did, ...}`, `/api/play/note-delete {id}`, `/api/play/install {tag}`,
`/api/play/install-cancel {tag}`, `/api/play/remove {tag}`, `/api/play/sync` (retry the push now). server.py hands
`/api/play/<what>` routes it doesn't know to `Play.api_get` / `Play.api_post`.

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
restarts the affected servers and offers a restart when the app updated itself (by itself right after opening, once
the installs are done and no tab has unsaved changes). Electron updates itself too (latest stable, sha256-checked,
swapped in after the app closes). Details: `shell/README.md` ("Automatic installs and updates", "Electron updates")
and `pm/updater.py`.
