# GitHub manager: data contract (server <-> page)

The server (`server.py`, Python 3 stdlib) serves the page at http://127.0.0.1:8767/ and these JSON endpoints.
All times are ISO 8601 UTC strings; the page shows them in Dallas time (America/Chicago).

## Caching (every JSON GET below)

`/api/state`, `/api/summary`, `/api/run` and `/api/history` send a strong `ETag` (from the state's generatedAt and a
revision bumped by every poll or saved log; for history, the event count, the last event id and the query) with
`Cache-Control: no-cache`, and answer `If-None-Match` with `304 Not Modified` and no body. With `Accept-Encoding: gzip`
the body is gzipped (`Content-Encoding: gzip`, ETag suffix `-gz`; about 0.93 MB -> 96 KB for the state); each version
is serialized and compressed once, not per request. The page fetches with `cache: 'no-cache'`, so the browser
revalidates.

## GET /api/state?runs=<n>

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

`runs=N` sends only the newest N runs (default: all; the page asks for 300) and then adds `"runsTotal"`; `running`,
`branches[].lastRun` and `pulls[].checks.runs` are unaffected.

Extra keys the server also sends (the page may use or ignore them): `poll.lastAt` (start of the last poll),
`poll.durationSec`, `poll.ghCalls`, `poll.ghChanged` (calls that were not a 304), `poll.backlog` ({jobs, logs} still to
download from the first start's backfill), `poll.warnings` (e.g. a log download that will be retried; not a failed
poll); runSummary `startedAt`, `runNumber`, `prs` (PR numbers), `workflowFile` (the workflow's file name from the runs
API's `path`, e.g. `release.yml`; `null` for a run cached before the server sent it, until GitHub lists it again); pulls `headSha`, `closedAt`; jobs `logGone: true`
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
         | "local.commit" | "local.moved" | "local.dirty" | "poll.error" | "baseline"
         | "note.added" | "note.edited" | "note.done" | "note.deleted",
 "ref": {"run": 123} | {"pr": 8} | {"branch": "..."} | {"tag": "..."} | {"local": "working-repo"} | {}
        | {"note": "n0123456789ab", <its target: "branch" | "pr" | "run" | "commit" | "version" | "local": key>},
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
- `note.*` (see /api/notes): `at` is when the note changed; the ref names the note and its target the way the other
  events do (`{"note": id, "pr": 8}`, `{"note": id, "branch": "testing"}`; a general note: `{"note": id}`), so a note's
  events show in that PR / branch / run's history. data: target, author, status, pinned, plus `text` (added, or when
  the text changed), `changed` and `rev` (edited / done), `from` (the old target, when moved). `note.done`: the status
  went to done (reopening is a `note.edited`). `note.deleted` is the tombstone: data.note is the whole note. Ids:
  `note.added:<id>`, `note.edited:<id>:<rev>`, `note.done:<id>:<rev>`, `note.deleted:<id>`.

## GET /api/run?id=<id>

One run (runSummary + jobs, as in `runs`), for a `#run=` deep link to a run older than the ones the page asked for.
404 if the server doesn't know it.

## GET /api/summary

A small digest for the other local tools (the project manager on :8768), rebuilt each poll:
```
{"generatedAt": "...", "repo": "floration/quark", "poll": {...same as /api/state...},
 "testing": {"sha": "...", "version": "1.18.6", "prerelease": {...} | null},
 "production": {"tag": "v1.17.0", "version": "1.17.0"},
 "openPulls": [{"number": 8, "title": "...", "head": "claude/...", "checks": {"state": "pending"}, "url": "..."}],
 "running": [runSummary...], "lastFailed": runSummary | null,      // newest run that failed (no jobs)
 "lastTesting": runSummary | null,   // newest completed build-and-release run on testing (by createdAt): a re-run
                                     // that passes on the same sha turns it green
 "local": [{"name": "working-repo", "branch": "testing", "aheadOfTesting": 1, "behindTesting": 0, "dirty": 2}]}
```

## Cross-origin reads

GET (and POST /api/refresh) on `/api/*` answer with `Access-Control-Allow-Origin: <Origin>` when the Origin is
`http://127.0.0.1:<port>` or `http://localhost:<port>` for a port 8765-8768 (the dashboard, docs manager, this tool,
project manager) or 8775 (the tests editor), and nothing for any other origin (`file://` pages send `Origin: null`,
which is not allowed). `OPTIONS` preflights get Allow-Methods `GET, POST, PUT, DELETE, OPTIONS` for those origins.
Requests whose Host header is not 127.0.0.1 / localhost get 403 (DNS rebinding). Note writes are stricter (below).

## Notes (the Editor tab)

q's notes, kept by the server in `editor/notes.json`. The only thing the page can change; nothing goes to GitHub or
a working copy.
```
note = {"id": "n0123456789ab", "target": {"kind": "general" | "branch" | "pr" | "run" | "commit" | "version" | "local",
                                           "key": "" | "testing" | "8" | "123" | "<sha>" | "1.18.6" | "working-repo"},
        "text": "markdown", "pinned": false, "status": "open" | "done", "author": "q",
        "createdAt": "...", "updatedAt": "...", "rev": 3}           // rev: +1 on every change
```
- `GET /api/notes`: `{"notes": [note...] (newest change first), "loadError": null | "text", "limits": {"textBytes":
  20480, "notes": 5000}}`, with an ETag (304 when unchanged) and gzip like the other GETs. loadError: notes.json was
  unreadable at start (it is kept as `notes.json.bad-<time>` and the .bak was loaded).
- `POST /api/notes` `{target, text, pinned?, status?, author?}` -> 201 `{"note": note}`.
- `PUT /api/notes/<id>` any of `{target, text, pinned, status, author}` plus optional `rev` -> 200 `{"note": note}`.
  With `rev`, a note changed since (another tab) answers 409 and is not changed. Nothing different: no write, no event.
- `DELETE /api/notes/<id>` (no fields; `{}` is fine) -> 200 `{"deleted": id}`. The note stays in the history
  (`note.deleted` with the whole note).
- Normalized: pr / run keys are numbers as strings (`"#8"` and `8` -> `"8"`), commit keys lowercase hex (4-64),
  version keys `X.Y.Z[-suffix]` (a leading `v` is dropped), branch keys without `origin/`; general has `"key": ""`.
  Text CRLF becomes LF. author defaults to `"q"`.
- Writes are refused unless all of these hold: the `Origin` header (or, with none, the `Referer`) is this page,
  `http://127.0.0.1:<port>` / `http://localhost:<port>`, or the Supervisor Board's `http://127.0.0.1:8768` /
  `http://localhost:8768` (403 otherwise, also for no header at all); `Content-Type: application/json` (415); a body
  of at most 64 KB (413) that is valid JSON (400); only the fields above (an unknown field, `id`, `createdAt`, ... is
  400); text non-empty and at most 20 KB of UTF-8, author at most 80 printable characters, key at most 200 (400); at
  most 5000 notes (409). Errors are `{"error": "text"}`; 404 for an unknown id.
- Each change rewrites `editor/notes.json` (temp file + replace; the previous version is kept as `notes.json.bak`)
  and `editor/notes.md`, and appends a `note.*` event to the history. A failed write answers 500 and changes nothing.


## GET /api/log?run=<id>&job=<id>

Plain text of a job's log, from `logs/<run>/<job>.txt` (failed jobs are downloaded once and kept forever; others on
request, then kept). 404 if GitHub no longer has it and it was never saved.

## POST /api/refresh?runs=<n>

Polls now (rate limited to one per 10 s) and re-scans for local copies. Returns the new state (`runs` as for
/api/state; header
`X-Refresh: polled`), or the current one within 10 s of the last refresh (`X-Refresh: skipped (one per 10 s)`). It
waits for a poll already running. Read-only toward GitHub and the repositories.

## Files the server writes (all inside its own folder, default `%USERPROFILE%\Desktop\Quark\github-manager\`)

- `cache/github.json`: the same object as /api/state minus `local`, rewritten every poll (the Claude Documents manager reads it).
- `editor/notes.json` (`{"version": 1, "notes": [note...]}`), `editor/notes.json.bak` (the version before the last
  change), `editor/notes.md` (the same notes as markdown, grouped by target, for other tools and agents).
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

`#tab=overview|branches|local|pulls|runs|history|editor|tests`, `#pr=N`, `#branch=<name>`, `#run=<id>`,
`#commit=<sha>`, `#version=<X.Y.Z>`, `#local=<name>`; the Editor: `#tab=editor&note=<id>` (shows and highlights that
note), `&kind=general|branch|pr|run|commit|version|local`, `&status=open|done|all`, `&q=<text>`.

The Tests tab embeds the tests editor, `http://127.0.0.1:8775/?embed=1` (made only when the tab opens; when 8775
doesn't answer within 1.5 s it says so). Each job of a run (the run's drawer) and each failed job on a run row links to
it as `http://127.0.0.1:8775/#ci/<workflowFile>/<job name, URL-encoded>?run=<run id>` (the tests editor drops matrix
suffixes such as ` (net)` itself).
