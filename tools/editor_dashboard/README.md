# Quark editor dashboard

A local page on the owner's PC that ties the editors and the git clones together: what changed per program compared
with TESTING and PRODUCTION, how much of the game is human-made (the provenance registry, docs/PROVENANCE.md), what is
left, and marking assets human / third-party / AI. Python 3 standard library only, one small server on 127.0.0.1, no
network, no CDN. Runs on Windows (the owner) and Linux (tests).

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
  list` gives for them (they may live anywhere). TESTING = `origin/testing`, PRODUCTION = the newest `v<major.minor.patch>`
  tag; the header shows `VERSION` at each. Refs and the remote branches come from `repository\` (else the registry
  clone); a clone whose own `origin/testing` differs is flagged "Refresh with fetch".
- **Editors**: `sound-editor\`, `model-editor\`, `map-editor\`, `bot-editor\`, `ui-customizer\` (missing ones are
  skipped): their `provenance.json` sidecar, anything under an `export*` / `overrides` / `out*` folder, and output-looking
  files at the top (.zip .wav .gltf .qmap .json ...). An exported override is compared with the registry clone's
  `assets/overrides/` (in repo / differs / not in repo).
- **Writes**: only its own sidecar (tool `dashboard`, the editors' format). Git runs with `--no-optional-locks` (even
  `status` leaves the index alone); there is no checkout, commit, push or merge anywhere. `git fetch --prune origin`
  runs in each clone only when you tick **fetch** and click Refresh (it moves only the remote-tracking refs).
- **Cost**: git calls are batched per clone (one `for-each-ref` for all refs, one `cat-file --batch` for both
  VERSIONs, one `for-each-ref %(ahead-behind:)` for every remote branch on git 2.41+) and run in parallel; the result is
  cached until Refresh. About 0.4 s and 56 git calls for 5 clones / worktrees and 6 remote branches. The tab bar shows
  each refresh's time and git call count.

## Views

1. **Changes by program**: Sound editor, Model editor, Map editor, Bot editor, UI customizer, Game code, Tooling/CI,
   Other. Overview: every clone (branch, HEAD, ahead / behind testing, ahead of production, uncommitted), a count per
   program, the editor folders and their pages. Per program: local commits ahead of testing in each clone and the
   uncommitted files; each file's state: **only local** (the change isn't in testing), **in testing** (in testing, not
   in production: e.g. the commit was cherry-picked or squash-merged upstream), **in both**; the unmerged remote
   branches (ahead / behind, commits, files); the commits and files in testing but not in production; the exported
   files with their dates. Files are put in programs by `PATH_RULES` at the top of `dashboard.py` (first match wins;
   edit that one table).
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

## The shared nav bar, look and links

All Quark tools share one top bar and `quark-tools.css` (tokens `--qt-*`, light + dark, no external fonts; the
components are listed at the top of the file). The bar:

```html
<nav class="qt-nav" aria-label="Quark tools">
  <a class="qt-brand" href="http://127.0.0.1:8765/">Quark</a>
  <a href="http://127.0.0.1:8765/" data-tool="dashboard">Dashboard</a>
  <a href="http://127.0.0.1:8767/" data-tool="github" target="quark-github">GitHub</a>
  <a href="http://127.0.0.1:8766/" data-tool="docs" target="quark-docs">Claude Documents</a>
  <span class="qt-nav-sep"></span>
  <a href="http://127.0.0.1:8765/editors/sound-editor/sound-editor.html" target="quark-sound-editor">Sound editor</a>
  <!-- ... one per editor page ... -->
</nav>
```

The current tool's link has `aria-current="page"`. Here the editor links come from `/api/links`; GitHub (the GitHub
manager, `Quark\github-manager\github-manager.cmd`) and Claude Documents (`Quark\docs-manager\docs-manager.cmd`) are
dimmed with "start ...cmd" when they don't answer. Commits in testing / on remote branches, remote branch names and the
header's versions link into the GitHub manager: `http://127.0.0.1:8767/#commit=<sha>`, `#branch=<name>` (no
`origin/`), `#version=<X.Y.Z>`. Local commits aren't on GitHub, so they don't link.

**Editor pages** are served from here (`/editors/<folder>/<page>.html` and their files, read-only), because Chrome /
Edge don't follow a `file://` link from an http page. Their browser autosave belongs to the address: the copy opened
from here (http://127.0.0.1:8765, keep the port) has its own, separate from the same page opened as a file. Move work
between them with the editor's project files.

**Deep links into this page** (the address follows what is on screen):
`#view=changes&program=<id>` (ids: `overview`, `sound-editor`, `model-editor`, `map-editor`, `bot-editor`,
`ui-customizer`, `game-code`, `tooling-ci`, `other`), `#view=assets&category=<cat>&origin=<human|third-party|ai|pending>&q=<text>`,
`#view=remaining&category=<cat>`, `#asset=<id>` (that item, selected). `?embed=1` hides the nav bar (the project
manager on 8768 shows the tools as tabs).

## Endpoints

All on 127.0.0.1, JSON. Requests must carry `Host: 127.0.0.1:<port>` or `localhost:<port>` (DNS rebinding). GET
`/api/*` also answers the project manager's page (Origin `http://127.0.0.1:8768` / `http://localhost:8768`, with
`Access-Control-Allow-Origin`); POSTs only this page (same Origin, `Content-Type: application/json`).

| Endpoint | Shape |
|---|---|
| `GET /api/summary` | The compact, stable one for other tools: `{testing: {version, ref}, production: {version, tag}, provenance: {total, human, third_party, ai, pct_human, by_category: {<cat>: {total, human, third_party, ai}}}, pending_marks, programs: [{id, name, ahead_testing, ahead_production, uncommitted, files_changed}], remaining, generated_at}`. `pending_marks`: assets with a pending sidecar entry (editors' and the dashboard's); `remaining`: registry items still `ai`; per program `ahead_testing`: distinct local commits not in testing touching it (all working clones), `ahead_production`: those plus testing's commits since production touching it, `uncommitted`: uncommitted files, `files_changed`: files whose change is only local; `generated_at`: the snapshot's time, ISO 8601 UTC. |
| `GET /api/status` | This page's header: `{app, quark, repo, refClone, testing: {ref, sha, version}, production: {tag, sha, version}, human: {counts: {human, third-party, ai}, total, humanPct}, refreshed (unix s), ms, gitCalls, gitMs, fetched, fetchErrors: [clone], noClones, ownSidecar, links}` |
| `GET /api/changes` | `{programs: [{name, clones: [{clone, branch, commits: [{sha, h, t, a, s, files: [[letter, path, state]]}], uncommitted: [[xy, path, state]]}], files: [[path, {clone: state}]], branches: [{name, t, ahead, behind, commits, files: [path]}], testing: [commit with files: [[letter, path]]], testingFiles: [[letter, path]], exports: [{source, path, mtime, size, program, asset?, repo?: same/differs/missing}]}], clones: [{name, path, role: mirror/work, outside, branch, upstream, aheadUp, behindUp, head, aheadTesting, behindTesting, aheadProd, testingSha, stale, commits, uncommitted, errors}], branches, branchesMerged, exports: [{name, path, files, more}], editors: [{folder, program, present}], rules}`; state = local / testing / both / none |
| `GET /api/assets` | `{repo, registryFiles, errors, rows: [{id, cat, o (registry origin, null if not in it), by, date, tool, source, note, wip, version, p?: [{origin, by, date, note, tool, sidecar}] (pending, newest first)}], stats: {<cat>: {human, third-party, ai, pending, eff: {...with the pending marks}}}, sidecars: [{path, short, tool, items, pending, mtime, own}], ownSidecar, ownPending, mergeCmd, mergeCwd}` |
| `GET /api/links` | `{siblings: [{id: github/docs, label, url, start, up}], editors: [{folder, program, pages, url, file}]}` (checked live) |
| `GET /api/ping` | `{app: "quark-editor-dashboard"}` |
| `POST /api/refresh` `{fetch}` | Rebuilds the snapshot (fetch first when true); answers `/api/status` |
| `POST /api/mark` `{ids, origin, by, note}` or `{ids, clear: true}` | Writes / removes your marks; answers `{assets, status}` |
| `GET /`, `/dashboard.js`, `/dashboard.css`, `/quark-tools.css`, `/editors/<folder>/...` | The page, the editors' pages |

## Source

`dashboard.py` (server, git collection, path rules, sidecar), `index.html`, `dashboard.js` (views, virtual list, deep
links), `dashboard.css` (its layout), `quark-tools.css` (the shared look), `dashboard.cmd` / `install.cmd` (Windows).
To test on Linux, make a Quark folder with clones of the repository (`repository`, `working-repo`, a worktree), an
editor folder with a `provenance.json`, then `python3 dashboard.py --quark <it> --dump` or run it and open the page.
