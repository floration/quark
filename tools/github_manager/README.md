# Quark GitHub manager

A local page that shows where floration/quark stands: production (the latest `vX.Y.Z` tag) and testing (the `testing`
branch, its VERSION and prerelease, the commits since production), every branch with its PR and last run, the pull
requests with their checks, the workflow runs with their jobs, failed steps and logs, the owner's local copies
compared with testing and production, and a history of everything that happened. It never pushes to the game repo
and never writes into a working copy; the only GitHub changes it makes are the branch and ruleset changes q queues
in the Editor and applies there by pressing Apply (below), and the only repo it pushes to is the Claude Documents
(its copy of the rules and notes). It keeps its own history and logs, which GitHub expires.

Two more tabs:

- **Editor**, the GitHub editor, in four sections (left list, `#tab=editor&sec=...`):
  - **Notes** (below).
  - **Structure**: the branches grouped by role (production, testing, infra, work `claude/*`, other, stale: no
    commit in N days, the `limit-stale-days` rule, inside that rule's branch scope (`claude/*` by default), and merged
    into or behind testing). Per branch q sets a role and a
    description (local, `editor/structure.json`); they set the Branches tab's kind and show in the branch's details.
    Create a branch (from testing, another branch or a sha), rename or delete one (not the default branch, testing,
    launcher, site, discord-bot or gh-pages; a delete warns about commits not in testing and open PRs; an applied
    rename takes the branch's role, description and notes along): these are GitHub changes, queued in Pending
    changes.
  - **Rules & limits**, two kinds, labelled: *Enforced by GitHub*, the repository's branch rulesets (block force
    push, block deletion, require a PR, required status checks picked from the recent runs' job names, linear
    history), edited with a form and queued; and *Followed by every Claude*, the agent rules (push / merge / version
    / CI / limit / other, scoped to all, a branch pattern or a tool), with the limits' values editable inline, saved
    locally (`editor/rules.json`) at once.
  - **Pending changes**: each queued GitHub change with its before -> after (a sentence for a branch, the JSON diff
    for a ruleset), warnings, the exact `gh api` call, and Apply / Preview again / Discard (plus Apply all). A delete,
    or a change with warnings, asks before it goes; Apply all's confirm lists every change with its warnings. After
    an Apply the page follows the server's own poll and redraws as soon as GitHub's new state is in. Nothing
    is sent to GitHub before Apply. Right before the call the server reads GitHub again, re-checks the production
    lock and refuses a change whose preview is out of date ("changed on GitHub since preview, review again"). The
    result (or GitHub's exact error) stays on the card and goes into the History tab (`github.*` events).

  **The production lock**: only q pushes to production, and the editor can't change that. It refuses any change to
  a ruleset that targets the default branch (read from GitHub each time) or v* tags (every non-branch ruleset), the
  rulesets "production rule" and "tagging rule" (shown locked, with why), deleting or renaming the default branch,
  testing, launcher, site, discord-bot and gh-pages, and any tag change; the agent rule "Only q pushes to
  production" can't be edited or removed. Checked in the page, when queueing, and again on the server right before
  each call.

  The agent rules and the notes are also written to the Claude Documents (the private repo
  floration/quark-claude-docs) as `10-github-rules.md`, `github-rules.json`, `11-github-notes.md` and
  `github-notes.json`, 10 s after the last change, by the server's own clone (`claude-docs/`) with q's gh login; a
  failure shows in the page and in the poll warnings and is retried on every poll until it works. On a first start the built-in rules give
  way to the `github-rules.json` already in the Claude Documents (rules edited on another PC, or before the data
  folder was lost), so the seed is never pushed over them, and the notes there are merged back into this PC's
  (`github-notes.json`, or `11-github-notes.md` read back from an older copy), so a new install never pushes empty
  notes over q's. Notes there that can't be read back (a hand-written file) are left as they are, with a warning,
  until a note is written here.

  Notes: a note is
  plain text with a little markdown (paragraphs, lists, `` `code` ``, **bold**, links; everything else is shown as
  typed) about a branch, PR, run, commit, version or local copy, or a general one; it can be pinned, marked done,
  edited and deleted (after a confirm; the deleted note stays in the history). A note about a branch, PR, run or local copy the page
  doesn't know (a typo, a deleted branch) gets a warning in the form, and can still be saved. Notes show where their target shows: a
  count on branch / PR / run rows and local-copy cards, all of them with "add note" in that item's details, and the
  pinned ones in a Notes card on the Overview. Every change is a `note.*` event in the History tab. Notes stay on this
  PC (and in the Claude Documents copy); nothing is sent to GitHub.
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
  `mirror.git`, `editor\`, `claude-docs\`, `server.log`) is never touched. It never stops a running server: when files changed, it
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
- `claudeDocs` (optional, default true): `false` turns the Claude Documents copy off (also `GHM_DOCS_SYNC=off` in the
  environment). `claudeDocsRepo` / `claudeDocsBranch` (default `floration/quark-claude-docs` / `main`): where it goes;
  with a `repo` other than floration/quark the copy is off unless `claudeDocsRepo` is set.

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
  or a crash never logs one twice. The first poll that reads GitHub writes a single `baseline` event (what it found) instead of
  thousands of events for things that already existed (while gh is signed out or offline nothing is logged, so a
  failed first poll never records "0 PRs, 0 runs"); while the tool is off nothing is lost, the next poll logs what
  changed.
- `cache/github.json`: the page's state minus the local copies, plus every release, rewritten each poll (the Claude
  Documents manager reads it). `cache/runs/`, `cache/pulls.json`, `cache/meta.json`: the caches above.
- `editor/notes.json`: the notes (`{"version": 1, "notes": [...]}`, see API.md), rewritten on every change through a
  temp file + replace; `editor/notes.json.bak` is the version before the last change. If notes.json can't be read at
  start, it is kept as `notes.json.bad-<time>` and the .bak is loaded (the page says so).
- `editor/notes.md`: the same notes as readable markdown, grouped by target (general, then branches, PRs, runs,
  commits, versions, local copies; pinned and open first), rewritten with notes.json, for other tools and agents to
  read. Don't edit it: the next change overwrites it.
- `editor/structure.json`, `editor/rules.json` (seeded with today's rules on the first start), `editor/pending.json`
  (each with a `.bak`), `editor/docs-sync.json` (what was last pushed to the Claude Documents), and `claude-docs/`
  (the clone of the Claude Documents repo it writes into; nothing else in that repo is touched).
- `logs/<run>/<job>.txt`, `mirror.git/`, `state-prev.json`, `github-manager.json`, `server.log` (+ `.1`-`.3`),
  `install.log`.

Delete `state-prev.json` and `history.jsonl` to start the history again; delete `cache/` to fetch everything again.

## API

`GET /api/state?runs=`, `/api/summary`, `/api/run?id=`, `/api/history?since=&limit=`, `/api/log?run=&job=`,
`/api/notes`, `/api/structure`, `/api/rules`, `/api/pending`, `/api/github/rulesets[?fresh=1]`, `POST /api/refresh`,
the note writes `POST /api/notes`, `PUT` / `DELETE /api/notes/<id>`, and the editor writes `PUT /api/structure`,
`PUT /api/rules`, `POST /api/pending`, `DELETE /api/pending/<id>`, `POST /api/pending/<id>/preview`,
`POST /api/pending/<id>/apply`, `POST /api/pending/apply-all`; see
`API.md` (the contract with the page). The JSON answers carry ETags (304 when nothing changed) and are gzipped when
the browser asks, each version built once. `/api/*` sends CORS headers for the other local tools on 127.0.0.1 /
localhost ports 8765-8768 and the tests editor's 8775. Writes (notes and the editor) are accepted only from this
page's own origin (or the Supervisor Board's, which embeds it), as JSON, with known fields and sizes (API.md).

## Source

`server.py` (server, poller, mirror, history, notes, the GitHub editor and the Claude Documents copy), `test_server.py`
(`python3 tools/github_manager/test_server.py`: the parsers, the history diff, the notes store and its HTTP checks, the
production lock (every refused case), previews and diffs, apply / stale / refused / failed against an in-memory GitHub
(`test/fake_github.py`), the stale check, rules -> markdown, the Claude Documents copy against a local bare repo with
a fake gh, the editor endpoints' protections, and an offline end-to-end poll against a local repository with canned
gh answers; `GHM_LIVE=1 ... LiveRulesets` reads the real repo's rulesets, read-only), `test/fixture_server.py` (the
page on fixtures, with in-memory notes, structure, rules and pending changes and a fake apply; `--no-editor`,
`--docs-error`, `--signed-out`: gh signed out since the start, so the GitHub parts show as unknown),
`install.py` / `install.cmd`, `github-manager.cmd`; the page: `index.html`, the CSS and `js/`.
