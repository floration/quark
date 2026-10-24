# Quark GitHub manager

A local page that shows where floration/quark stands: production (the latest `vX.Y.Z` tag) and testing (the `testing`
branch, its VERSION and prerelease, the commits since production), every branch with its PR and last run, the pull
requests with their checks, the workflow runs with their jobs, failed steps and logs, the owner's local copies
compared with testing and production, and a history of everything that happened. It is read-only: it never pushes,
never changes a branch and never writes into a working copy. It keeps its own history and logs, which GitHub expires.

## Run it

You need Python 3 (standard library only), git, and gh signed in (`gh auth login`).

```
tools\github_manager\install.cmd [output folder]      (Windows: copy the tool there and start it)
tools\github_manager\github-manager.cmd [output folder]   (start it from where it is)
python3 tools/github_manager/server.py [--data <folder>] [--port N] [--open] [--once]
```

Output folder default: `%USERPROFILE%\Desktop\Quark\github-manager`. `install.cmd` (or `python install.py <folder>`)
copies the server, launcher and page files there, so the tool doesn't depend on the branch a working copy is on;
the data already in the folder stays. Run it again after updating the repository. `github-manager.cmd` starts the
server minimized at low priority (`start /low`, which git and gh inherit) and opens http://127.0.0.1:8767/ in the
default browser; if it is already running it only opens the page. Close its window to stop it. `--once` polls once,
writes the files and exits. The page is served from the folder `server.py` is in; the server listens on 127.0.0.1 only.

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
- `logs/<run>/<job>.txt`, `mirror.git/`, `state-prev.json`, `github-manager.json`.

Delete `state-prev.json` and `history.jsonl` to start the history again; delete `cache/` to fetch everything again.

## API

`GET /api/state?runs=`, `/api/summary`, `/api/run?id=`, `/api/history?since=&limit=`, `/api/log?run=&job=`,
`POST /api/refresh`; see `API.md` (the contract with the page). The JSON answers carry ETags (304 when nothing
changed) and are gzipped when the browser asks, each version built once. `/api/*` sends CORS headers for the other
local tools on 127.0.0.1 / localhost ports 8765-8768.

## Source

`server.py` (server, poller, mirror, history), `test_server.py` (`python3 tools/github_manager/test_server.py`: the
parsers, the history diff and an offline end-to-end poll against a local repository with canned gh answers),
`install.py` / `install.cmd`, `github-manager.cmd`; the page: `index.html`, the CSS and `js/`.
