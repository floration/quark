# GitHub manager: data contract (server <-> page)

The server (`server.py`, Python 3 stdlib) serves the page at http://127.0.0.1:8767/ and these JSON endpoints.
All times are ISO 8601 UTC strings; the page shows them in Dallas time (America/Chicago).

## GET /api/state

```
{
  "generatedAt": "...", "repo": "floration/quark", "defaultBranch": "claude/compassionate-ritchie-07ah6i",
  "poll": {"lastOk": "...", "lastError": null | "text", "nextAt": "...", "intervalSec": 60, "rateRemaining": 4800},
  "production": {"tag": "v1.17.0", "sha": "...", "version": "1.17.0", "publishedAt": "...", "url": "..."},
  "testing": {"sha": "...", "version": "1.18.6", "date": "...", "subject": "...", "prerelease": {"tag": "test-1.18.6-test.150", "publishedAt": "...", "url": "..."} | null,
              "aheadOfProduction": 42, "commitsSinceProduction": [commit, ...]},     // first-parent, newest first, max 200
  "branches": [{"name": "...", "sha": "...", "date": "...", "subject": "...", "author": "...",
                "kind": "production" | "testing" | "claude" | "infra" | "other",       // infra = launcher, site, discord-bot
                "ahead": 3, "behind": 10,                                               // vs testing (computed in the mirror: same as GitHub compare)
                "pr": 8 | null, "lastRun": runSummary | null, "url": "..."}],
  "pulls": [{"number": 8, "title": "...", "state": "open" | "closed", "merged": true, "mergedAt": "...", "draft": false,
             "head": "claude/...", "base": "testing", "author": "...", "createdAt": "...", "updatedAt": "...",
             "url": "...", "checks": {"state": "success" | "failure" | "pending" | "none", "runs": [runSummary...]},
             "additions": 1, "deletions": 2, "changedFiles": 3}],                      // open + last 30 closed
  "runs": [runSummary + {"jobs": [{"id": 1, "name": "...", "status": "...", "conclusion": "...", "startedAt": "...",
                                   "completedAt": "...", "url": "...", "hasLog": true,
                                   "failedSteps": ["step name", ...]}]}],              // newest first, last 100 + everything in the local log
  "local": [{"name": "working-repo", "path": "C:\\...\\working-repo", "branch": "...", "sha": "...", "subject": "...",
             "date": "...", "dirty": 3, "dirtyFiles": ["M src/x.cpp", ...] (max 50), "upstream": "origin/testing" | null,
             "aheadOfTesting": 6, "behindTesting": 2, "aheadOfProduction": 40,
             "commits": [commit...] (local commits not on origin/testing, max 100), "version": "1.18.4" (VERSION file),
             "worktreeOf": null | "working-repo", "error": null | "text"}],
  "running": [runSummary...],    // status queued/in_progress/waiting/pending
  "releases": [{"tagName": "v1.17.0", "isPrerelease": false, "publishedAt": "...", "url": "..."}]   // newest first, no drafts
}
commit     = {"sha": "...", "short": "abc1234", "subject": "...", "author": "...", "date": "...", "url": "..." | null}
runSummary = {"id": 123, "workflow": "build-and-release", "branch": "...", "sha": "...", "event": "push", "title": "...",
              "status": "completed", "conclusion": "success" | "failure" | "cancelled" | "skipped" | null,
              "createdAt": "...", "updatedAt": "...", "durationSec": 600, "attempt": 1, "url": "..."}
```

Extra keys the server also sends (the page may use or ignore them): `poll.lastAt` (start of the last poll),
`poll.durationSec`, `poll.ghCalls`, `poll.ghChanged` (calls that were not a 304), `poll.backlog` ({jobs, logs} still to
download from the first start's backfill), `poll.warnings` (e.g. a log download that will be retried; not a failed
poll); runSummary `startedAt`, `runNumber`, `prs` (PR numbers); pulls `headSha`, `closedAt`; jobs `logGone: true`
(GitHub no longer has the log and it was never saved); `testing.prerelease.sha`. `durationSec` of a running run is
the time so far. Values the server can't know yet are `null` (e.g. ahead / behind before the mirror has the commit).
Jobs are `[]` until fetched (the first start backfills every run GitHub still lists, 30 runs' jobs per poll).

## GET /api/history?since=<iso>&limit=<n>

The local log (`history.jsonl`, append-only, never trimmed): one event per line, newest first in the response (by
`at`). `limit` defaults to 500; `since` keeps events at or after that time.
```
{"id": "run.failed:123:1", "at": "...",
 "type": "run.started" | "run.completed" | "run.failed" | "pr.opened" | "pr.merged" | "pr.closed" | "pr.updated"
         | "branch.created" | "branch.moved" | "branch.deleted" | "tag.created" | "release.published"
         | "local.commit" | "local.moved" | "local.dirty" | "poll.error" | "baseline",
 "ref": {"run": 123} | {"pr": 8} | {"branch": "..."} | {"tag": "..."} | {"local": "working-repo"} | {},
 "text": "build-and-release on testing failed (windows-tests)", "data": {...}}
```
- `id` is deterministic (type + object + version), so a restart never appends the same event twice.
- `at` is when it happened where GitHub says (a run's end, a PR's merge, a release's publish), else the poll's time.
- `baseline` (ref `{}`): written once, on the very first poll, instead of one event per existing branch / tag / run
  ("Started watching floration/quark: 18 branches, 105 tags, 2 open PRs, 290 runs, 4 local copies"; `data` has the
  counts). Things that existed before it never get events. Later, an item first seen in a poll gets an event only if
  it changed after the previous poll (minus 10 min), so the tool being off for a day still logs that day's runs.
- `run.started` (a new run or a re-run attempt, text "... re-run #2 started on ..."), `run.completed` (data.conclusion:
  success / cancelled / skipped), `run.failed` (failure / timed_out / startup_failure; data.failedJobs).
- `pr.opened` also covers a reopened PR. `pr.updated`: the open PR's head commit changed (data.from / data.to).
- `branch.moved` data: from, to, forward (false = force-push), count (commits added).
- `local.commit`: HEAD moved forward with commits that are not on origin's testing (data.count, data.subject).
  `local.moved`: anything else (a pull of testing, a reset, a branch switch). `local.dirty`: only when a copy goes
  from clean to dirty or back (data.dirty, data.was), not on every count change.
- `poll.error`: once per new error text (data.error), not on every failed poll.

## GET /api/summary

A small digest for the other local tools (the project manager on :8768), rebuilt each poll:
```
{"generatedAt": "...", "repo": "floration/quark", "poll": {...same as /api/state...},
 "testing": {"sha": "...", "version": "1.18.6", "prerelease": {...} | null},
 "production": {"tag": "v1.17.0", "version": "1.17.0"},
 "openPulls": [{"number": 8, "title": "...", "head": "claude/...", "checks": {"state": "pending"}, "url": "..."}],
 "running": [runSummary...], "lastFailed": runSummary | null,      // newest run that failed (no jobs)
 "local": [{"name": "working-repo", "branch": "testing", "aheadOfTesting": 1, "behindTesting": 0, "dirty": 2}]}
```

## Cross-origin reads

GET (and POST /api/refresh) on `/api/*` answer with `Access-Control-Allow-Origin: <Origin>` when the Origin is
`http://127.0.0.1:<port>` or `http://localhost:<port>` for a port 8765-8768 (the dashboard, docs manager, this tool,
project manager), and nothing for any other origin (`file://` pages send `Origin: null`, which is not allowed).
`OPTIONS` preflights get Allow-Methods `GET, POST, OPTIONS` for those origins. Requests whose Host header is not
127.0.0.1 / localhost get 403 (DNS rebinding).

## GET /api/log?run=<id>&job=<id>

Plain text of a job's log, from `logs/<run>/<job>.txt` (failed jobs are downloaded once and kept forever; others on
request, then kept). 404 if GitHub no longer has it and it was never saved.

## POST /api/refresh

Polls now (rate limited to one per 10 s) and re-scans for local copies. Returns the new state (header
`X-Refresh: polled`), or the current one within 10 s of the last refresh (`X-Refresh: skipped (one per 10 s)`). It
waits for a poll already running. Read-only toward GitHub and the repositories.

## Files the server writes (all inside its own folder, default `%USERPROFILE%\Desktop\Quark\github-manager\`)

- `cache/github.json`: the same object as /api/state minus `local`, rewritten every poll (the Claude Documents manager reads it).
- `history.jsonl`: the event log. `state-prev.json`: last state, for diffing. `logs/<run>/<job>.txt`: job logs.
- `mirror.git`: its own bare mirror of the repo (`git init --bare` with origin's branches as `refs/heads/*` and tags as
  `refs/tags/*`, `git fetch --prune` each poll; not `clone --mirror`, whose `+refs/*:refs/*` prune would delete the
  local refs). Each local copy is fetched into it as `refs/local/<key>/HEAD` and `refs/local/<key>/heads/*` (key = the
  name with anything but letters, digits, `.`, `_`, `-` turned into `-`), only when its HEAD moved, so the comparisons
  never touch q's working copies (only `git status --porcelain=v2` / `git worktree list --porcelain` read them, with
  GIT_OPTIONAL_LOCKS=0; plus the VERSION file itself when git status lists it as modified).
- `cache/runs/<id>.json` (every completed run seen, with its jobs: never refetched), `cache/pulls.json` (PR sizes by
  updatedAt), `cache/meta.json`.

## Page deep links

`#tab=overview|branches|local|pulls|runs|history`, `#pr=N`, `#branch=<name>`, `#run=<id>`, `#commit=<sha>`,
`#version=<X.Y.Z>`, `#local=<name>`.
