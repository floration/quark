# Quark GitHub manager

A local page that shows where floration/quark stands: production (the latest `vX.Y.Z` tag) and testing (the `testing`
branch, its VERSION and prerelease, the commits since production), every branch with its PR and last run, the pull
requests with their checks, the workflow runs with their jobs, failed steps and logs, the owner's local copies
compared with testing and production, and a history of everything that happened. It is read-only toward GitHub and
the repositories: it never pushes, never changes a branch and never writes into a working copy. It keeps its own
history and logs, which GitHub expires.

Two more tabs:

- **Editor**: q's notes (the first part of the GitHub editor; Structure and Rules are listed as coming next). A note is
  plain text with a little markdown (paragraphs, lists, `` `code` ``, **bold**, links; everything else is shown as
  typed) about a branch, PR, run, commit, version or local copy, or a general one; it can be pinned, marked done,
  edited and deleted (after a confirm; the deleted note stays in the history). Notes show where their target shows: a
  count on branch / PR / run rows and local-copy cards, all of them with "add note" in that item's details, and the
  pinned ones in a Notes card on the Overview. Every change is a `note.*` event in the History tab. Notes stay on this
  PC; nothing is sent to GitHub.
- **Tests**: the separate tests editor (http://127.0.0.1:8775/) embedded, or a line saying it isn't running. A run's
  jobs link to it (`#ci/<workflow file>/<job>?run=<id>`).

## Run it

You need Python 3 (standard library only), git, and gh signed in (`gh auth login`).

```
tools\github_manager\install.cmd [output folder]          (Windows: copy the tool there and start it)
tools\github_manager\github-manager.cmd [output folder]   (start it from where it is)
python3 tools/github_manager/install.py [--dest <folder>] [--no-start] [--help]
python3 tools/github_manager/server.py [--data <folder>] [--port N] [--open] [--once] [--quiet]
```

Output folder default: `%USERPROFILE%\Desktop\Quark\github-manager`. Nothing opens a command window that stays:

- `github-manager.cmd` opens http://127.0.0.1:8767/ if the server already answers (set `GHM_PORT` for another
  port). Otherwise it starts the server with no window at low priority (`start "" /b /low`, output to nul) with the
  real `pythonw.exe` next to the Python that `py -3` / `python` run, started directly (not `pyw -3` first: the py
  launcher keeps Python in a job that dies with the launcher, so killing pyw.exe killed the server); then `pythonw`
  on PATH, then `pyw -3`; with none of those, a hidden PowerShell `Start-Process -WindowStyle Hidden` at idle
  priority) and closes at once. The server opens the page itself once it listens. git and gh inherit the low
  priority and never show a console (CREATE_NO_WINDOW).
- `install.cmd` (or `python install.py [--dest <folder>]`; a folder given without `--dest` works too) copies the
  server, launcher and page files there, so the tool doesn't depend on the branch a working copy is on, then starts
  it the same way (pythonw, detached, idle priority), or opens the page if it is already running (no browser with
  `QUARK_SHELL=1`). `--no-start` only copies (the Quark Project app uses it, then starts and restarts the server
  itself). It never asks anything and is safe to re-run: only files that differ are copied (stale page files are
  removed), and the data already in the folder (`github-manager.json`, `cache\`, `history.jsonl`, `logs\`,
  `mirror.git`, `editor\`, `server.log`) is never touched. It never stops a running server: when files changed, it
  prints "restart needed" (the running one keeps its old code until it is restarted). What it did goes to
  `install.log`. `--help` prints the options; an unknown option prints a message and exits with code 2 (1: the copy
  failed).
- The Quark Project app runs `python server.py --data <Quark>\github-manager` hidden, with `QUARK_SHELL=1` (then
  `--open` is ignored: the app shows the page), and restarts it if it stops.

The server writes its own log, `server.log` in the output folder (rotating at about 1 MB, 3 old files kept): start
(pid, Python), one line per poll, every error with its traceback (a crash in the poll thread is logged and the next
poll runs as usual), and why it stopped (a signal, Ctrl+C, an error). Under pythonw there is no console, so nothing is
printed; with a console, the same lines are printed too (`--quiet`: errors only). A second copy on a busy port
leaves at once with exit code 2 and the line "not starting: port 8767 is already in use by a running GitHub manager"
(or "by another program"); with `--open` it opens the running one's page. Exit code 1: another startup error.
`--once` polls once, writes the files and exits. The page is served from the folder `server.py` is in; the server
listens on 127.0.0.1 only.

## Settings: `github-manager.json` in the output folder

Written with the defaults on the first start:

```
{"repo": "floration/quark", "port": 8767, "quarkDir": "C:\\Users\\q\\Desktop\\Quark"}
```

- `locals` (optional): the local copies to watch, as paths or `{"name": "...", "path": "..."}`. Without it, every
  folder directly inside `quarkDir` that is a git checkout (a `.git` folder or file) with its origin on floration/quark
  is watched, plus their worktrees wherever they are; `Claude Documents` is skipped. Copies are looked for again every
  10 minutes and on Refresh.
- `originUrl` (optional): where the mirror fetches from (default `https://github.com/<repo>.git`; the repo is public, so
  no sign-in is needed for git).

## How it polls (light on purpose: the PC also plays the game)

One background thread. Every 60 s while any run is queued or in progress, otherwise every 300 s (20 s while the
first start's backfill is still downloading). Refresh on the page polls at once (at most one per 10 s).

- GitHub through `gh api` (REST only, no GraphQL), each call with the ETag of the last answer: an unchanged list
  answers 304, which does not count against the rate limit. Per poll: the repo, releases, open PRs, the last 30
  closed PRs (a PR's size is fetched again only when its updatedAt changes), the 30 newest runs, the queued /
  in-progress / waiting runs, and the jobs of runs that are still running. The rate limit is read from every answer
  and from `gh api rate_limit` every 30 min; when it is used up the GitHub part waits until it resets.
- A completed run and its jobs are fetched once and kept in `cache/runs/<id>.json` for good (a re-run, which is a
  new attempt, is fetched again). The first start backfills every run GitHub still lists (30 runs' jobs per poll).
- A failed job's log is downloaded once into `logs/<run>/<job>.txt` and kept forever (10 per poll; a failed
  download is retried after 30 min, a log GitHub no longer has is marked gone). Other logs are downloaded when you
  open them on the page, then kept.
- git: `mirror.git` is the tool's own bare repository: `git fetch --prune` of origin's branches and tags each poll
  (nothing to download when nothing changed), and each local copy's HEAD and branches fetched into
  `refs/local/<name>/` only when its HEAD moved. Every comparison (ahead / behind testing, commits since production,
  commits not on GitHub, the VERSION at a commit) runs in the mirror and is cached by commit. A local copy itself is
  only read with `git status --porcelain=v2` and `git worktree list --porcelain`, with `GIT_OPTIONAL_LOCKS=0` (git
  doesn't refresh its index), plus its VERSION file when git status lists it as modified.

## Files it writes (all in the output folder)

- `history.jsonl`: the event log, append-only, never trimmed (runs started / passed / failed, PRs opened / merged /
  closed / updated, branches created / moved / deleted, tags, releases, local commits / moves / clean-dirty changes,
  poll errors). Events come from comparing each poll with `state-prev.json`. Each event has a fixed id, so a restart
  or a crash never logs one twice. The very first poll writes a single `baseline` event (what it found) instead of
  thousands of events for things that already existed; while the tool is off nothing is lost, the next poll logs what
  changed.
- `cache/github.json`: the page's state minus the local copies, plus every release, rewritten each poll (the Claude
  Documents manager reads it). `cache/runs/`, `cache/pulls.json`, `cache/meta.json`: the caches above.
- `editor/notes.json`: the notes (`{"version": 1, "notes": [...]}`, see API.md), rewritten on every change through a
  temp file + replace; `editor/notes.json.bak` is the version before the last change. If notes.json can't be read at
  start, it is kept as `notes.json.bad-<time>` and the .bak is loaded (the page says so).
- `editor/notes.md`: the same notes as readable markdown, grouped by target (general, then branches, PRs, runs,
  commits, versions, local copies; pinned and open first), rewritten with notes.json, for other tools and agents to
  read. Don't edit it: the next change overwrites it.
- `logs/<run>/<job>.txt`, `mirror.git/`, `state-prev.json`, `github-manager.json`, `server.log` (+ `.1`-`.3`),
  `install.log`.

Delete `state-prev.json` and `history.jsonl` to start the history again; delete `cache/` to fetch everything again.

## API

`GET /api/state?runs=`, `/api/summary`, `/api/run?id=`, `/api/history?since=&limit=`, `/api/log?run=&job=`,
`/api/notes`, `POST /api/refresh`, and the note writes `POST /api/notes`, `PUT` / `DELETE /api/notes/<id>`; see
`API.md` (the contract with the page). The JSON answers carry ETags (304 when nothing changed) and are gzipped when
the browser asks, each version built once. `/api/*` sends CORS headers for the other local tools on 127.0.0.1 /
localhost ports 8765-8768 and the tests editor's 8775. Note writes are accepted only from this page's own origin (or
the Supervisor Board's, which embeds it), as JSON, with known fields and sizes (API.md).

## Source

`server.py` (server, poller, mirror, history, notes), `test_server.py` (`python3 tools/github_manager/test_server.py`:
the parsers, the history diff, the notes store and its HTTP checks, and an offline end-to-end poll against a local
repository with canned gh answers), `test/fixture_server.py` (the page on fixtures, with in-memory notes),
`install.py` / `install.cmd`, `github-manager.cmd`; the page: `index.html`, the CSS and `js/`.
