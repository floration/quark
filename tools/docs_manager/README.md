# Claude Manager (Claude Documents manager)

A read-only local page that makes Claude Documents (the agents' shared notebook, private repo
`floration/quark-claude-docs`, PC copy `Desktop\Quark\Claude Documents`) easy for a human to read: every entry in
plain words, what each one waits on (q, a push, a PR, CI, tests, Claude, nobody yet), every change with its time,
links to the versions, PRs, CI runs, branches and threads it mentions, and a timeline and flow graph of the work.
It never writes to the Claude Documents folder or repo.

## Run / install

```
python3 tools/docs_manager/install.py [--target DIR]     (or install.cmd; default %USERPROFILE%\Desktop\Quark\docs-manager)
%USERPROFILE%\Desktop\Quark\docs-manager\docs-manager.cmd  (starts the server with no window via pyw, opens http://127.0.0.1:8766/)
python3 tools/docs_manager/serve.py --docs <Claude Documents folder> [--open]
```

The Quark app starts it hidden as `serve.py --docs ... --port 8766 --quiet` and restarts it if it stops; with
`QUARK_SHELL=1` it never opens a browser. Without a console (pythonw) it logs to `cache/serve.log`.
Python 3 standard library, git, and (optional) `gh` signed in. Port 8766: the Editor Manager is 8765, the GitHub
Manager 8767, the Supervisor Board 8768 (which hosts all of them as tabs). The page uses the tools' shared `quark-tools.css` and nav;
`?embed=1` hides the nav for the app shell.

## Where the data comes from

- **Its own mirror clone** (`mirror/` next to serve.py) of the docs repo, fetched every 60 s; the history (change
  log, first written / last changed per entry) comes from it. The clone URL is the PC folder's `origin`.
- **The PC folder**, read only (file bytes, no git commands that write), to flag files edited on the PC and not
  synced yet.
- **GitHub** (floration/quark PRs, CI runs, releases, branches): the GitHub manager's
  `Quark\github-manager\cache\github.json` when it's under 5 minutes old, else `gh` every 5 minutes.
  Cached in `cache/`. Without either, everything else still works.

`mirror/`, `cache/` and snapshots hold the private docs: never commit them (see `.gitignore`).

## How entries are read

Files 01-08 are split into entries (`- **Title** ... _date, thread, status_`, README of the docs repo); 00 and
README are shown as documents. Statuses outside the word list are mapped (`asked` -> open, `not a bug: ...` ->
wontfix, `partly answered` -> answered). Entries keep their identity across edits (id = file + title slug, then a
fuzzy title match, then a cross-file match for moves), so each commit becomes "added / edited / status / moved /
removed" items.

**Waiting on** (open / in-progress items only): 07 open = q's decision; text about the owner's push or "waiting on
the owner" = q; an open linked PR = PR (and CI if its checks fail or run); a linked branch whose last CI run failed
or is running = CI; failing tests / repro / "needs measuring" = tests; in-progress otherwise = Claude; open in
01/03/04/05 = nobody yet; unread 06 items = for q to read.

## Page

Overview (Needs q first, waiting-on board, latest changes), Board (kanban, timeline per thread, flow graph threads
-> entries -> branches/PRs -> versions), Entries (filters, search), Change log (by day, Dallas time), Files
(rendered or raw), Threads. Keys: `/` search, 1-6 tabs, `?` help. Deep links: `#tab=`, `#entry=<file>:<slug>`,
`#search=<text>`, `#file=`. Text from the docs is escaped before the small markdown renderer runs.

## API (127.0.0.1 only; CORS for 127.0.0.1 / localhost ports 8765-8770; ETag / 304)

- `GET /api/data`: everything (files, entries with links / waitingOn / waitingWhy / firstSeen / lastChanged,
  changes, threads, github, quarkDir, fetchOk, ghOk).
- `GET /api/summary`: `{generatedAt, mirrorHead, fetchOk, ghOk, inProgress[{id,title,thread,threadUrl,status,where,
  waitingOn,waitingWhy,date,lastChanged}], waiting{owner,push,pr,ci,tests,claude: [ids]},
  questions[{id,title,body,thread,threadUrl,date,recommendation}], recentChanges[first 30 changes]}`.
- `GET /api/health`, `POST /api/refresh` (fetch now).

`serve.py --snapshot out.json [--no-fetch] [--gh-fixture f] [--no-gh]` writes the data once. `threads.json` maps
thread titles (and the shorter names entries use) to their project links; add new threads there.

## Tests

`QUARK_DOCS_REPO=<clone of the docs repo> python3 -m unittest discover tools/docs_manager/tests` (the history test
is skipped without it). The page runs without the server from `python3 -m http.server` in this folder with
`?data=<fixture.json>`.
