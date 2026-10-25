# Quark Editor Manager

The owner's local page for the editors: Sound editor, UI editor (the UI customizer), Map editor, Bot editor, Model editor,
Gamemode editor, Physics editor and Weapons editor (the `EDITORS` table in `dashboard.py`). It links to every editor
(and says whether it is running and whether the version installed is testing's), shows what each editor's files changed
compared with TESTING and PRODUCTION, the editors' exports, how much of the game is human-made (the provenance registry,
docs/PROVENANCE.md) overall and per editor, what is left, and lets you mark assets human / third-party / AI and merge
the marks into the registry. Python 3 standard library only, one small server on 127.0.0.1, no network, no CDN. Runs on Windows
(the owner) and Linux (tests).

**Scope: editors only.** GitHub, Claude Documents, game code and tooling changes live in the Supervisor Board
(http://127.0.0.1:8768/), not here. A file that belongs to no editor is not shown.

(The program files keep their old names: `tools/editor_dashboard/`, `dashboard.py`, `dashboard.cmd`, installed in
`Quark\editor-dashboard\`, port 8765.)

## Install / run

```
tools\editor_dashboard\install.cmd [folder]        (Windows; default %USERPROFILE%\Desktop\Quark\editor-dashboard)
python3 tools/editor_dashboard/dashboard.py --install [folder]      (default <Quark folder>/editor-dashboard)
```

Then double-click `Quark\editor-dashboard\dashboard.cmd`: it starts the server on http://127.0.0.1:8765/ (the next free
port if that one is taken) and opens the browser. A second double-click while it runs just opens the page. Close the
window (or Ctrl+C) to stop it. Re-run the install after an update: it copies the program files and keeps your marks.

```
python3 dashboard.py [--quark <Quark folder>] [--repo <clone>] [--port 8765] [--no-browser]
                     [--data-dir <game user data>] [--sidecar <file>] [--install [<dir>]] [--dump]
```

- `--quark`: the folder holding `repository\`, `working-repo*\` and the editors. Default: the folder the program is
  installed in's parent (`Quark\editor-dashboard\` -> `Quark\`), else the folder holding the clone it runs from, else
  `%USERPROFILE%\Desktop\Quark`.
- `--repo`: the clone whose `assets/provenance/` is the registry (a name in the Quark folder or a path). Default
  `working-repo\` if it has the registry (it can lag behind testing), else `repository\`.
- `--data-dir`: the game's user data folder; its `overrides\` is listed as "this PC: overrides" (default
  `%APPDATA%\quark`, `~/.local/share/quark`, `~/Library/Application Support/quark`).
- `--sidecar`: where your marks go. Default `provenance.json` next to the program; when the program runs from inside a
  git clone, `Quark\editor-dashboard\provenance.json` instead (it never writes into a clone).
- `QUARK_SHELL=1` (environment): never opens a browser (the Quark Project app shows the page); if a server is
  already on the port it just exits. The app installs / updates with `python tools/editor_dashboard/dashboard.py
  --install` run in `repository\` (non-interactive, safe to re-run, keeps your marks).
- `--dump`: build the snapshot once, print every endpoint's JSON, exit (tests; no server).

## What it reads, what it writes

- **Clones**: `repository\` (the mirror of testing), every `working-repo*` folder, and every worktree `git worktree
  list` gives for them (they may live anywhere); only the editors' files in them. TESTING = `origin/testing`,
  PRODUCTION = the newest `v<major.minor.patch>` tag; the header shows `VERSION` at each (read from `repository\`,
  else the registry clone), and the testing build of testing's tip (`1.18.31-test.231`: its `test-*` tag when fetched,
  else `Quark\testing\BUILD.txt` when that build is of testing's tip). A clone whose own `origin/testing` is older is
  compared with the reference's testing commit when it has that commit; when it doesn't, its counts would include
  testing's own commits, so it is listed "not counted (needs fetch)" and left out of every total and of `/api/summary`.
- **Editors**: the folders in `EDITORS` (`sound-editor\`, `ui-customizer\`, `map-editor\`, `bot-editor\`,
  `model-editor\`, `gamemode-editor\`, `physics-editor\`, `weapons-editor\`; missing ones are skipped): their
  `provenance.json` sidecar, anything under an `export*` / `overrides` / `out*` folder, and output-looking files at the
  top (.zip .wav .gltf .qmap .json ...), but not the editor's own files there: its sidecar, `*.example.json` and the
  settings `<name>.json` next to one, the installers' stamps, and every name its source folder in the registry clone
  has (`serve.py`, `README.md` ...: what its installer copies). An exported override is compared with testing's `assets/overrides/` (one `git
  ls-tree` in the reference clone, compared by git blob id): **in testing**, **in working-repo only** (the registry
  clone has it in an unpushed commit or uncommitted), **differs from testing**, **not in testing**; and linked to its
  asset row with its origin and a **mark human** button. Files in the game's overrides folder that belong to no editor
  are counted apart ("1 file of no editor").
- **Editor links** (read-only): the Quark Project app's tab list, `Quark\project-manager\tabs.json`, else
  `Quark\project-manager\shell\tabs.default.json`, else the repository's copy. A tab with a local http url (map, bot,
  gamemode, physics, weapons: each runs its own `serve.py`) is linked to its port without `?embed=1` and probed (any HTTP
  answer within 0.3 s is "running"); a folder + file tab (sound, UI, model) is served from here under `/editors/`. Inside
  the app (`window.quarkShell`) a link switches to the editor's tab instead (the app starts a stopped server editor).
  Outside the app a stopped server editor has a **Start** button (and clicking its dimmed link starts it, then opens
  it): the Editor Manager runs the tab's `start` command the way the app does, hidden (no console window), in the
  editor's folder, on the tab's port, with this Python; its output goes to `logs\<folder>.log` next to the program.
- **Installed version**: `Quark\project-manager\installs-state.json` keeps, per `installs.json` entry, the git ids of
  its sources when the app last installed it; the same ids at testing's tip (one `cat-file --batch-check`) say whether an
  editor is **testing's**, **repository's** (HEAD of the mirror, testing is newer), **older than testing**, **update
  failed**, or **not by the app**.
- **Writes**: its own sidecar (tool `dashboard`, the editors' format); its merge file `merge-plan.json` next to it;
  only when you click **Merge into working-repo** and confirm, `tools/provenance.py merge merge-plan.json` run hidden
  (no console window) in the registry clone, which changes its `assets/provenance/*.json`; only when you click
  **Commit locally**, `git add` + `git commit -- assets/provenance/` there (that folder only, a local commit, never a
  push); a stopped server editor on **Start**. Merge and commit are refused when the registry clone is `repository\`
  (the mirror the app fast-forwards: a dirty mirror stops the automatic updates). Git runs with `--no-optional-locks`
  (even `status` leaves the index alone); there is no checkout or push anywhere. `git fetch --prune origin` runs in each clone only when you tick **fetch** and click Refresh (it moves
  only the remote-tracking refs).
- **Refreshing by itself**: every `GET /api/summary` (the app's tab bar) and `GET /api/stamp` (the page, every 4 s
  while visible) runs a cheap check, at most every 2 s: a few dozen `stat` calls on the sidecars, the registry files and
  each clone's HEAD, index, branch, packed-refs and origin/testing; and, every 6 s, a scan of the editors' files in each
  working tree (`PATH_RULES`, scandir only: a few ms per clone), so an uncommitted edit, a new or a deleted file counts
  too. A changed sidecar or registry file reloads the assets (no git, a few ms); a git change or an edit starts a
  refresh without fetch in the background (seen within about 6 s). Every endpoint keeps
  answering from the last snapshot while a refresh runs (only the very first one is waited for), and the page re-renders
  in place (scroll, search box and note kept).
- **Cost**: four git calls per clone / worktree (`status`, `log testing..HEAD`, `diff testing`, `diff --merge-base
  testing`), each limited to the editors' paths (`PATH_RULES` as git pathspecs), plus one `worktree list` per
  working-repo clone and, per shared `.git`, one `for-each-ref`; the production-to-testing diff and log and the two
  VERSIONs (one `cat-file --batch`), testing's `assets/overrides` (one `ls-tree`) and the editors' installed stamps
  (one `cat-file --batch-check`) run once, in the reference clone, and are shared with every clone at the same commits;
  a clone whose origin/testing differs from the reference's costs one or two calls more. All of it runs in parallel and
  is cached until something changes (above): about 30 git calls for 5 clones / worktrees. The tab bar shows each refresh's time and git
  call count. (A clone whose index git hasn't refreshed since checkout costs more: `--no-optional-locks` means the
  Editor Manager never refreshes it; any `git status` of your own does.)

## Views

1. **Changes by editor**: one entry per editor in the sidebar (a running / ready dot, its counts, the % of its assets
   that are human-made). Overview: a card per editor (as many across as fit: eight at 2560 px) with its **Open** link,
   running / not running, the version installed vs testing, its counts and its human-made %; each working clone's
   editor commits not in testing / uncommitted / only-local editor files; the editor folders with their links. A
   **Needs attention** card (red, only when there is something) lists the conflicts, sidecar entries Merge can't take,
   exports not in testing whose asset is still AI, failed installs, clones that need a fetch and a merged registry not
   committed yet. A commit counts as a local commit only while it brings a change testing lacks: one cherry-picked into
   testing, or undone by its revert, is listed but not counted ("+1" in the clones table). Per
   editor: the same line at the top, then local commits ahead of testing in each clone (only that editor's files) and the
   uncommitted files; each file's state: **only local** (the change isn't in testing), **in testing** (in testing, not
   in production: e.g. the commit was cherry-picked or squash-merged upstream), **in both**; the commits and files in
   testing but not in production; the exported files with their dates, their state against testing and their asset
   (**Mark all N human** marks every exported asset that isn't human-made yet, after a confirmation). Files are put in editors by `PATH_RULES` at
   the top of `dashboard.py` (first match wins; edit that one table); files no rule matches are not shown.
2. **Assets: AI vs human**: every registry item, filter by origin / category / editor / text / pending, sort by any
   column (click its title), play a sound or view a picture (the button left of the id: the newest exported override,
   else the registry clone's `assets/overrides/`, else the game's asset export `build*/assets_export/`), the editor that
   makes it (`CATEGORY_EDITOR`), % human-made overall and per category (human and third-party apart, rounded like
   `tools/provenance.py summary`), and the % with the pending marks (new items they add count in the total too).
   Pending = a sidecar entry (an editor's or yours) whose origin isn't the registry's (or a new item), shown with its
   tool. The list is virtualized (only the rows on screen are in the page), so thousands of items scroll smoothly.
   Each category is a small tile (one row of them at 2560 px).
3. **Remaining**: the items still `ai`, per category.
4. **Marking**: click rows (Shift+click for a range, or Select shown), pick human / third-party / AI, by (remembered)
   and a note, **Mark** (more than 20 items asks first). Changing the filters drops selected items that are no longer
   shown, so Mark never changes an item you can't see. "Remove my mark" deletes your entry and says when there was none.
5. **One rule decides each item** (`decide()` in `dashboard.py`), on the page and in Merge alike:
   - your mark wins over every editor's entry; without one, the editors' newest entry by date wins;
   - the item is **pending** while that winner's origin isn't the registry's (Merge would change it). Once merged it
     isn't pending, whatever an editor still says: an editor entry your mark overruled stays settled, so a second Merge
     has nothing to do (it used to flip the item back to the editor's origin);
   - a **conflict** (red) is an editor entry that disagrees with your mark and that you haven't decided, or two editors
     that disagree. Your mark records the editor entries it overrules (`seen`: their fingerprints) when you mark the
     item, so those aren't conflicts; a new export of the item by the editor (another date, note or origin) is a
     conflict again. Mark the item, or remove your mark, to decide it. The `conflict` origin filter lists them;
   - a mark that repeats the registry's origin isn't written (it removes an older mark of yours), unless an editor
     says otherwise: then it is written and overrules that editor;
   - an id of no registry category (`textures/wall.png`) is flagged "can't merge" and skipped; marking one is refused.
6. **Merge into working-repo** (after a confirmation; its output is shown) writes `merge-plan.json` next to your
   sidecar, exactly the page's choice for each pending item (the winning entry, with the tool it came from), and runs
   `tools/provenance.py merge` on it in the registry clone. Or run it yourself: **Copy** writes that file, then copies
   `python3 tools/provenance.py merge ../editor-dashboard/merge-plan.json` (Windows: `py -3 ...`), to run in the registry
   clone. Then **Review and commit** shows the diff of `assets/provenance/` and **Commit locally** commits that folder
   only (push it with your other commits).

## The nav bar, look and links

The Quark tools share `quark-tools.css` (tokens `--qt-*`, light + dark, no external fonts; the components are listed at
the top of the file; it stays the shared style file for the sibling tools). This page's bar:

```html
<nav class="qt-nav" aria-label="Quark tools">
  <a class="qt-brand" href="http://127.0.0.1:8765/" data-self>Quark</a>
  <a href="http://127.0.0.1:8765/" data-tool="dashboard" data-self aria-current="page">Editor Manager</a>
  <a href="http://127.0.0.1:8768/" data-tool="manager" target="quark-manager">Supervisor Board</a>
  <span class="qt-nav-sep"></span>
  <a href="/editors/sound-editor/sound-editor.html" data-ed="sound-editor" target="quark-sound-editor">Sound editor</a>
  <a href="http://127.0.0.1:8770/" data-ed="map-editor" class="down" target="quark-map-editor">Map editor</a>
  <!-- ... one per installed editor, in the editors' order ... -->
</nav>
```

The `data-self` links point at the port the page is served from (it takes the next free port when 8765 is busy). The
editor links come from `/api/links`; a server editor and the Supervisor Board are dimmed ("not running") when they don't
answer, and clicking one then checks again before opening it. Nothing links to GitHub or Claude Documents (the Supervisor Board
has those).

**Page editors** (sound, UI, model) are served from here (`/editors/<folder>/<page>.html` and their files, read-only), because Chrome /
Edge don't follow a `file://` link from an http page. Their browser autosave belongs to the address: the copy opened
from here (http://127.0.0.1:8765, keep the port) has its own, separate from the same page opened as a file. Move work
between them with the editor's project files.

**Deep links into this page** (the address follows what is on screen):
`#view=changes&program=<id>` (ids are the editor folders: `overview`, `sound-editor`, `ui-customizer` (also
`ui-editor`), `map-editor`, `bot-editor`, `model-editor`, `gamemode-editor`, `physics-editor`, `weapons-editor`; an
unknown id shows the overview), `#view=assets&category=<cat>&origin=<human|third-party|ai|pending>&editor=<id>&q=<text>&asset=<id>`,
`#view=remaining&category=<cat>`, `#asset=<id>` (that item, selected; one selected item stays in the address, so a
reload or a copied link selects it again). `?embed=1` hides the nav bar (the Supervisor Board on 8768 shows the tools as tabs) and the header row: the Quark Project tab bar shows Testing / Production / Human-made / Last refresh, and Refresh / fetch sit right of the view tabs (the "no git clones" error still shows a header row). The page calls `quarkShell.statsChanged()` after a Refresh or a mark so the tab bar updates at once.

## Endpoints

All on 127.0.0.1, JSON. Requests must carry `Host: 127.0.0.1:<port>` or `localhost:<port>` (DNS rebinding). GET
`/api/*` also answers the Supervisor Board's page (Origin `http://127.0.0.1:8768` / `http://localhost:8768`, with
`Access-Control-Allow-Origin`); POSTs only this page (same Origin, `Content-Type: application/json`).

| Endpoint | Shape |
|---|---|
| `GET /api/summary` | The compact, stable one for other tools: `{testing: {version, ref, sha, build}, production: {version, tag, sha}, fetched, provenance: {total, human, third_party, ai, pct_human, by_category: {<cat>: {total, human, third_party, ai}}}, pending_marks, programs: [{id, name, ahead_testing, ahead_production, uncommitted, files_changed, assets, pct_human}], remaining, stale_clones, refreshing, generated_at}`. `programs` are the eight editors, in order: `sound-editor` Sound editor, `ui-customizer` UI editor, `map-editor` Map editor, `bot-editor` Bot editor, `model-editor` Model editor, `gamemode-editor` Gamemode editor, `physics-editor` Physics editor, `weapons-editor` Weapons editor. `testing.build`: the testing build of testing's tip (`1.18.31-test.231`) or null; per program `assets` / `pct_human`: its categories' registry items and how many are human-made (null without assets); `stale_clones`: clones left out of the counts (needs fetch); `refreshing`: a refresh runs (the answer is the last snapshot). Asking also runs the cheap change check (above). `pending_marks`: assets whose winning sidecar entry (editors' and the dashboard's) the registry doesn't have yet; `conflicts`: assets where sidecars disagree undecided; `remaining`: registry items still `ai`; per program `ahead_testing`: distinct local commits not in testing touching it and bringing a change testing lacks (all working clones but stale ones), `ahead_production`: those plus testing's commits since production touching it, `uncommitted`: uncommitted files, `files_changed`: files whose change is only local; `generated_at`: the snapshot's time, ISO 8601 UTC; `sha`: 9 hex digits (empty when missing); `fetched`: the snapshot ran `git fetch` first. The Quark Project app's tab bar reads it (from its main process, no Origin header) to show Testing / Production / Human-made / Refreshed on every tab. |
| `GET /api/status` | This page's header: `{app, quark, repo, refClone, testing: {ref, sha, version, build}, production: {tag, sha, version}, human: {counts: {human, third-party, ai}, total, humanPct}, refreshed (unix s), assetsAt, ms, gitCalls, gitMs, fetched, auto (refreshed by itself), lastFetch, fetchErrors: [clone], noClones, ownSidecar, links}` |
| `GET /api/changes` | `{programs: [{id, name, clones: [{clone, commits: [{sha, h, t, a, s, files: [[letter, path, state]]}], uncommitted: [[xy, path, state]]}], files: [[path, {clone: state}]], testing: [commit with files: [[letter, path]]], testingFiles: [[letter, path]], exports: [{source, path, mtime, size, program, asset?, repo?: same/local/differs/missing (vs testing)}], clones: [..., stale]}], clones: [{name, path, role: mirror/work, outside, testingSha, base: own/reference/stale, stale, commits, uncommitted, local, errors}], exports: [{name, path, files, more, unowned: [path]}], editors: [{folder, program, present, install: {entry, name, state, at, version}}], rules}`; only the editors' files; state = local / testing / both / none; per clone `commits` / `uncommitted` / `local` count the editor commits not in testing, uncommitted editor files, only-local editor files |
| `GET /api/assets` | `{repo, registryFiles, errors, rows: [{id, cat, o (registry origin, null if not in it), by, date, tool, source, note, wip, version, pv? ("a" sound / "i" picture: /api/preview has it), p?: [{origin, by, date, note, tool, sidecar, st: win/same/over/conflict}] (when pending or in conflict: the sidecar entries, the winner first), pm? (Merge changes it), c? (conflict), bad? (why Merge skips it)}], stats: {<cat>: {human, third-party, ai, pending, conflicts, eff: {...with the pending marks}}}, sidecars: [{path, short, tool, items, pending (its winning entries Merge takes), over, conflicts, mtime, own}], ownSidecar, ownPending, toMerge, mergeCmd, mergeCwd, mergeFrom, mergeTarget: {name, path, ok, why}, regDirty: [[xy, path]] (assets/provenance/ changes not committed; null without a merge target), catEditor: {<cat>: editor id}}` |
| `GET /api/links` | `{siblings: [{id: "manager", label: "Supervisor Board", url: "http://127.0.0.1:8768/", start, up}], editors: [{folder, program, tab (the app's tab id), installed, kind: server/page/null, url, port, start, pages, file?, note?, up}], tabsFrom}` (checked live, 0.3 s at most) |
| `GET /api/stamp` | `{refreshed, assetsAt, refreshing, ready}`: what the page polls; runs the cheap change check |
| `GET /api/preview?id=<asset id>` | The asset's sound / picture (only files the snapshot found for that id) |
| `GET /api/ping` | `{app: "quark-editor-dashboard"}` |
| `POST /api/refresh` `{fetch}` | Rebuilds the snapshot (fetch first when true); answers `/api/status` |
| `POST /api/mark` `{ids, origin, by, note}` or `{ids, clear: true}` | Writes / removes your marks; answers `{assets, status, result: {marked, removed, same, overruled, bad}}` |
| `GET /api/regdiff` | `{files: [[xy, path]], stat, lines, diff}`: what a commit of `assets/provenance/` would hold (the first 400 lines) |
| `POST /api/merge` `{confirm: true}` | Writes the merge file, runs `tools/provenance.py merge` on it in the registry clone (hidden); answers `{ok, output: [line], assets, status}` |
| `POST /api/plan` `{}` | Writes the merge file only; answers `{path, items, skipped, cmd}` |
| `POST /api/commit` `{confirm: true, message}` | A local commit of `assets/provenance/` only in the registry clone; answers `{ok, output, assets, status}` |
| `POST /api/start` `{folder}` | Starts a stopped server editor hidden on its tab's port (waits up to 12 s); answers `{ok, message, links}` |
| `GET /`, `/dashboard.js`, `/dashboard.css`, `/quark-tools.css`, `/editors/<folder>/...` | The page, the editors' pages |

## Source

`dashboard.py` (server, git collection, path rules, sidecar), `index.html`, `dashboard.js` (views, virtual list, deep
links), `dashboard.css` (its layout), `quark-tools.css` (the shared look), `dashboard.cmd` / `install.cmd` (Windows),
`test_dashboard.py` (tests: `python3 tools/editor_dashboard/test_dashboard.py`; they build a throwaway Quark folder
with git clones in a temp dir; not installed).
To test on Linux, make a Quark folder with clones of the repository (`repository`, `working-repo`, a worktree), an
editor folder with a `provenance.json`, then `python3 dashboard.py --quark <it> --dump` or run it and open the page.
