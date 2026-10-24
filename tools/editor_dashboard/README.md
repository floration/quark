# Quark Editor Manager

The owner's local page for the editors: Sound editor, UI editor (the UI customizer), Map editor, Bot editor and Model
editor. It shows what each editor's files changed compared with TESTING and PRODUCTION, the editors' exports, how much of
the game is human-made (the provenance registry, docs/PROVENANCE.md), what is left, and lets you mark assets human /
third-party / AI. Python 3 standard library only, one small server on 127.0.0.1, no network, no CDN. Runs on Windows
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
- `--dump`: build the snapshot once, print every endpoint's JSON, exit (tests; no server).

## What it reads, what it writes

- **Clones**: `repository\` (the mirror of testing), every `working-repo*` folder, and every worktree `git worktree
  list` gives for them (they may live anywhere); only the editors' files in them. TESTING = `origin/testing`,
  PRODUCTION = the newest `v<major.minor.patch>` tag; the header shows `VERSION` at each (read from `repository\`,
  else the registry clone). A clone whose own `origin/testing` differs is flagged "Refresh with fetch".
- **Editors**: `sound-editor\`, `model-editor\`, `map-editor\`, `bot-editor\`, `ui-customizer\` (missing ones are
  skipped): their `provenance.json` sidecar, anything under an `export*` / `overrides` / `out*` folder, and output-looking
  files at the top (.zip .wav .gltf .qmap .json ...). An exported override is compared with the registry clone's
  `assets/overrides/` (in repo / differs / not in repo).
- **Writes**: only its own sidecar (tool `dashboard`, the editors' format). Git runs with `--no-optional-locks` (even
  `status` leaves the index alone); there is no checkout, commit, push or merge anywhere. `git fetch --prune origin`
  runs in each clone only when you tick **fetch** and click Refresh (it moves only the remote-tracking refs).
- **Cost**: four git calls per clone / worktree (`status`, `log testing..HEAD`, `diff testing`, `diff --merge-base
  testing`), each limited to the editors' paths (`PATH_RULES` as git pathspecs), plus one `worktree list` per
  working-repo clone and, per shared `.git`, one `for-each-ref`; the production-to-testing diff and log and the two
  VERSIONs (one `cat-file --batch`) run once, in the reference clone, and are shared with every clone at the same
  commits. All of it runs in parallel and is cached until Refresh: 28 git calls and about 55 ms for 5 clones /
  worktrees (was 56 calls / 90 ms with the branch and general git views). The tab bar shows each refresh's time and git
  call count. (A clone whose index git hasn't refreshed since checkout costs more: `--no-optional-locks` means the
  Editor Manager never refreshes it; any `git status` of your own does.)

## Views

1. **Changes by editor**: Sound editor, UI editor, Map editor, Bot editor, Model editor. Overview: a count per editor,
   each working clone's editor commits not in testing / uncommitted / only-local editor files, the editor folders and
   their pages. Per editor: local commits ahead of testing in each clone (only that editor's files) and the
   uncommitted files; each file's state: **only local** (the change isn't in testing), **in testing** (in testing, not
   in production: e.g. the commit was cherry-picked or squash-merged upstream), **in both**; the commits and files in
   testing but not in production; the exported files with their dates. Files are put in editors by `PATH_RULES` at
   the top of `dashboard.py` (first match wins; edit that one table); files no rule matches are not shown.
2. **Assets: AI vs human**: every registry item, filter by origin / category / text / pending, % human-made overall
   and per category (human and third-party apart, rounded like `tools/provenance.py summary`), and the % with the
   pending marks. Pending = a sidecar entry (an editor's or yours) that differs from the registry, shown with its tool.
   The list is virtualized (only the rows on screen are in the page), so thousands of items scroll smoothly.
3. **Remaining**: the items still `ai`, per category.
4. **Marking**: click rows (Shift+click for a range, or Select shown), pick human / third-party / AI, by (remembered)
   and a note, **Mark**. "Remove my mark" deletes your entry. The card says how many of your marks aren't in the repo
   yet and gives the command to run in the registry clone (paths relative to it):
   `python3 tools/provenance.py merge ../sound-editor/provenance.json ../editor-dashboard/provenance.json` (Windows:
   `py -3 ...`). After the merge, Refresh: merged entries stop being pending.

## The nav bar, look and links

The Quark tools share `quark-tools.css` (tokens `--qt-*`, light + dark, no external fonts; the components are listed at
the top of the file; it stays the shared style file for the sibling tools). This page's bar:

```html
<nav class="qt-nav" aria-label="Quark tools">
  <a class="qt-brand" href="http://127.0.0.1:8765/">Quark</a>
  <a href="http://127.0.0.1:8765/" data-tool="dashboard" aria-current="page">Editor Manager</a>
  <a href="http://127.0.0.1:8768/" data-tool="manager" target="quark-manager">Supervisor Board</a>
  <span class="qt-nav-sep"></span>
  <a href="http://127.0.0.1:8765/editors/sound-editor/sound-editor.html" target="quark-sound-editor">Sound editor</a>
  <!-- ... one per installed editor page, in the editors' order ... -->
</nav>
```

The editor links come from `/api/links`; the Supervisor Board link is dimmed ("not running") when it doesn't answer, and
clicking it then checks again before opening it. Nothing links to GitHub or Claude Documents (the Supervisor Board
has those).

**Editor pages** are served from here (`/editors/<folder>/<page>.html` and their files, read-only), because Chrome /
Edge don't follow a `file://` link from an http page. Their browser autosave belongs to the address: the copy opened
from here (http://127.0.0.1:8765, keep the port) has its own, separate from the same page opened as a file. Move work
between them with the editor's project files.

**Deep links into this page** (the address follows what is on screen):
`#view=changes&program=<id>` (ids are the editor folders: `overview`, `sound-editor`, `ui-customizer` (also
`ui-editor`), `map-editor`, `bot-editor`, `model-editor`; an unknown id shows the overview),
`#view=assets&category=<cat>&origin=<human|third-party|ai|pending>&q=<text>`, `#view=remaining&category=<cat>`,
`#asset=<id>` (that item, selected). `?embed=1` hides the nav bar (the Supervisor Board on 8768 shows the tools as tabs).

## Endpoints

All on 127.0.0.1, JSON. Requests must carry `Host: 127.0.0.1:<port>` or `localhost:<port>` (DNS rebinding). GET
`/api/*` also answers the Supervisor Board's page (Origin `http://127.0.0.1:8768` / `http://localhost:8768`, with
`Access-Control-Allow-Origin`); POSTs only this page (same Origin, `Content-Type: application/json`).

| Endpoint | Shape |
|---|---|
| `GET /api/summary` | The compact, stable one for other tools: `{testing: {version, ref}, production: {version, tag}, provenance: {total, human, third_party, ai, pct_human, by_category: {<cat>: {total, human, third_party, ai}}}, pending_marks, programs: [{id, name, ahead_testing, ahead_production, uncommitted, files_changed}], remaining, generated_at}`. `programs` are the five editors, in order: `sound-editor` Sound editor, `ui-customizer` UI editor, `map-editor` Map editor, `bot-editor` Bot editor, `model-editor` Model editor. `pending_marks`: assets with a pending sidecar entry (editors' and the dashboard's); `remaining`: registry items still `ai`; per program `ahead_testing`: distinct local commits not in testing touching it (all working clones), `ahead_production`: those plus testing's commits since production touching it, `uncommitted`: uncommitted files, `files_changed`: files whose change is only local; `generated_at`: the snapshot's time, ISO 8601 UTC. |
| `GET /api/status` | This page's header: `{app, quark, repo, refClone, testing: {ref, sha, version}, production: {tag, sha, version}, human: {counts: {human, third-party, ai}, total, humanPct}, refreshed (unix s), ms, gitCalls, gitMs, fetched, fetchErrors: [clone], noClones, ownSidecar, links}` |
| `GET /api/changes` | `{programs: [{id, name, clones: [{clone, commits: [{sha, h, t, a, s, files: [[letter, path, state]]}], uncommitted: [[xy, path, state]]}], files: [[path, {clone: state}]], testing: [commit with files: [[letter, path]]], testingFiles: [[letter, path]], exports: [{source, path, mtime, size, program, asset?, repo?: same/differs/missing}]}], clones: [{name, path, role: mirror/work, outside, testingSha, stale, commits, uncommitted, local, errors}], exports: [{name, path, files, more}], editors: [{folder, program, present}], rules}`; only the editors' files; state = local / testing / both / none; per clone `commits` / `uncommitted` / `local` count the editor commits not in testing, uncommitted editor files, only-local editor files |
| `GET /api/assets` | `{repo, registryFiles, errors, rows: [{id, cat, o (registry origin, null if not in it), by, date, tool, source, note, wip, version, p?: [{origin, by, date, note, tool, sidecar}] (pending, newest first)}], stats: {<cat>: {human, third-party, ai, pending, eff: {...with the pending marks}}}, sidecars: [{path, short, tool, items, pending, mtime, own}], ownSidecar, ownPending, mergeCmd, mergeCwd}` |
| `GET /api/links` | `{siblings: [{id: "manager", label: "Supervisor Board", url: "http://127.0.0.1:8768/", start, up}], editors: [{folder, program, pages, url, file}]}` (checked live) |
| `GET /api/ping` | `{app: "quark-editor-dashboard"}` |
| `POST /api/refresh` `{fetch}` | Rebuilds the snapshot (fetch first when true); answers `/api/status` |
| `POST /api/mark` `{ids, origin, by, note}` or `{ids, clear: true}` | Writes / removes your marks; answers `{assets, status}` |
| `GET /`, `/dashboard.js`, `/dashboard.css`, `/quark-tools.css`, `/editors/<folder>/...` | The page, the editors' pages |

## Source

`dashboard.py` (server, git collection, path rules, sidecar), `index.html`, `dashboard.js` (views, virtual list, deep
links), `dashboard.css` (its layout), `quark-tools.css` (the shared look), `dashboard.cmd` / `install.cmd` (Windows).
To test on Linux, make a Quark folder with clones of the repository (`repository`, `working-repo`, a worktree), an
editor folder with a `provenance.json`, then `python3 dashboard.py --quark <it> --dump` or run it and open the page.
