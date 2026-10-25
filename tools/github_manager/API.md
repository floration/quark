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
                "unrelated": false,                     // true: no shared history with testing (gh-pages); ahead / behind null
                "pr": 8 | null,                         // its open PR, else a closed one only while the branch is still at that PR's head
                "lastRun": runSummary | null, "url": "..."}],
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
  "releases": [{"tagName": "v1.17.0", "isPrerelease": false, "publishedAt": "...", "url": "..."}],  // newest first, no drafts
  "notRead": ["pulls", "releases", "runs"]   // GitHub parts not read since the server started (gh signed out, offline):
                                             // their lists are empty / cached and production.publishedAt is the tag's date
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
download from the first start's backfill; the page shows it on every tab), `poll.warnings` (e.g. a log download that
will be retried, the Claude Documents copy failing; not a failed poll; the page shows them on every tab); runSummary `startedAt`, `runNumber`, `prs` (PR numbers), `workflowFile` (the workflow's file name from the runs
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
         | "note.added" | "note.edited" | "note.done" | "note.deleted" | "note.restored"
         | "structure.changed" | "rules.changed"
         | "github.branch.created" | "github.branch.deleted" | "github.branch.renamed"
         | "github.ruleset.created" | "github.ruleset.updated" | "github.ruleset.deleted"
         | "github.apply.failed" | "github.apply.refused" | "github.apply.stale" | "branch.renamed",
 "ref": {"run": 123} | {"pr": 8} | {"branch": "..."} | {"tag": "..."} | {"local": "working-repo"} | {}
        | {"note": "n0123456789ab", <its target: "branch" | "pr" | "run" | "commit" | "version" | "local": key>}
        | {"ruleset": 25800001} | {"branch": "...", "ruleset": ...},
 "text": "build-and-release on testing failed (windows-tests)", "data": {...}}
```
- `id` is deterministic (type + object + version), so a restart never appends the same event twice.
- `at` is when it happened where GitHub says (a run's end, a PR's merge, a release's publish), else the poll's time.
- `baseline` (ref `{}`): written once, on the first poll that read GitHub (`notRead` empty; until then no events at
  all), instead of one event per existing branch / tag / run
  ("Started watching floration/quark: 18 branches, 105 tags, 2 open PRs, 290 runs, 4 local copies"; `data` has the
  counts). Things that existed before it never get events. Later, an item first seen in a poll gets an event only if
  it changed after the previous poll (minus 10 min), so the tool being off for a day still logs that day's runs.
- `run.started` (a new run or a re-run attempt, text "... re-run #2 started on ..."), `run.completed` (data.conclusion:
  success / cancelled / skipped), `run.failed` (failure / timed_out / startup_failure; data.failedJobs).
- `pr.opened` also covers a reopened PR. `pr.updated`: the open PR's head commit changed (data.from / data.to).
- `branch.moved` data: from, to, forward (false = force-push), count (commits added). `branch.renamed` (ref the new
  name, id `branch.renamed:<old>:<new>:<sha>`): a branch gone and a new one at the same commit in one poll (data:
  from, to, sha), instead of a delete plus a create.
- `local.commit`: HEAD moved forward with commits that are not on origin's testing (data.count, data.subject).
  `local.moved`: anything else (a pull of testing, a reset, a branch switch). `local.dirty`: only when a copy goes
  from clean to dirty or back (data.dirty, data.was), not on every count change.
- `poll.error`: once per new error text (data.error), not on every failed poll.
- `note.*` (see /api/notes): `at` is when the note changed; the ref names the note and its target the way the other
  events do (`{"note": id, "pr": 8}`, `{"note": id, "branch": "testing"}`; a general note: `{"note": id}`), so a note's
  events show in that PR / branch / run's history. data: target, author, status, pinned, plus `text` (added, or when
  the text changed), `changed` and `rev` (edited / done), `from` (the old target, when moved). `note.done`: the status
  went to done (reopening is a `note.edited`). `note.deleted` is the tombstone: data.note is the whole note. Ids:
  `note.added:<id>`, `note.edited:<id>:<rev>`, `note.done:<id>:<rev>`, `note.deleted:<id>`. `note.restored` (ref
  `{}`, id `note.restored:<digest>`): notes merged back from the Claude Documents (below); data: source (the file),
  `added` and `updated` note ids.
- `structure.changed` / `rules.changed` (the GitHub editor, below): a local change of the branch roles / descriptions
  or the agent rules. ref `{"branch": name}` when one branch changed, else `{}`. data: `rev` and what changed (the
  changed branch entries, `null` = removed; for rules `added`, `edited`, `removed` ids, the new `rules` and the
  `removedRules`). Ids `structure.changed:<rev>:<digest>`, `rules.changed:<rev>:<digest>`.
- `github.*`: a pending change applied on GitHub (`github.branch.created` / `deleted` / `renamed`,
  `github.ruleset.created` / `updated` / `deleted`, id `<type>:<change id>`), or an Apply that didn't go through:
  `github.apply.failed` (GitHub or gh refused the call, or the change can't be made: no such branch...),
  `github.apply.refused` (the production lock) and `github.apply.stale` (GitHub changed since the preview), id
  `<type>:<change id>:<at>`. data: change, type, params, before, after, call (method, path, body) and result; a
  not-applied one: change, type, call, status, error. An applied rename also moves that branch's structure entry
  (`structure.changed`) and its notes (`note.edited` with `from`) to the new name.

## GET /api/run?id=<id>

One run (runSummary + jobs, as in `runs`), for a `#run=` deep link to a run older than the ones the page asked for.
404 if the server doesn't know it.

## GET /api/summary

A small digest for the other local tools (the project manager on :8768), rebuilt each poll:
```
{"generatedAt": "...", "repo": "floration/quark", "poll": {...same as /api/state...},
 "testing": {"sha": "...", "version": "1.18.6", "prerelease": {...} | null},
 "production": {"tag": "v1.17.0", "version": "1.17.0", "sha": "...", "publishedAt": "..."},
 "openPulls": [{"number": 8, "title": "...", "head": "claude/...", "base": "testing", "draft": false,
                "updatedAt": "...", "checks": {"state": "pending"}, "url": "..."}],   // base: the production PR has the default branch
 "notRead": [],                                                    // as in /api/state
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
Requests whose Host header is not 127.0.0.1 / localhost get 403 (DNS rebinding). Writes (notes and the GitHub editor) are stricter (below).

## Notes (the Editor tab)

q's notes, kept by the server in `editor/notes.json` (and copied to the Claude Documents as `11-github-notes.md` and
`github-notes.json`, and merged back from there on a new install; see the GitHub editor below). Nothing goes to GitHub or a working copy.
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

## The GitHub editor: structure, rules & limits, pending changes

The rest of the Editor tab. Two kinds of data:

- **Local** (`editor/structure.json`, `editor/rules.json`): branch roles and descriptions, and the agent rules (what
  every Claude follows). Saved at once; the agent rules and the notes are also written to the Claude Documents
  (below). Nothing goes to GitHub.
- **GitHub** (branch create / rename / delete, branch rulesets): never sent when queued. A change goes into
  `editor/pending.json` with a preview read from GitHub, and is sent only by `POST /api/pending/<id>/apply` (the
  page's Apply button), with q's own `gh` login.

**The production lock** (only q pushes to production; it can't be turned off, edited or bypassed from the page or
the API). The server refuses, with 422 and `"locked": true`, when queueing, previewing again and once more right
before the call to GitHub:
- any ruleset create / update / delete when the ruleset (before or after the change) targets the default branch
  (read from `repos/<repo>` every time, never cached), `~DEFAULT_BRANCH`, `~ALL`, or a pattern that could match it
  (matched conservatively: `*`, `**`, `?` and `[...]` all widen the match) unless an exclude pattern surely matches
  it (matched narrowly: `*` and `?` stay inside one `/` part, `**` crosses them, a `[...]` never counts; or
  `~DEFAULT_BRANCH`), so `claude/*` minus the default branch passes; `~ALL` is refused whatever it excludes; any
  ruleset whose target is not `branch` (tag and push rulesets: `v*` tags); and the rulesets named `production rule`
  and `tagging rule`;
- deleting or renaming the default branch, `testing`, `launcher`, `site`, `discord-bot` or `gh-pages` (and renaming
  a branch to one of those names);
- any tag change (creating or moving `v*` tags is never offered; a `tag.*` type is refused as locked).
The agent rule `only-q-production` can't be edited, unlocked or removed (422 locked).

### GET /api/structure
```
{"branches": {"<name>": {"role": "work" | "infra" | "other" | null, "description": "markdown", "updatedAt": "..."}},
 "rev": 4, "updatedAt": "...", "loadError": null,
 "roles": ["production", "testing", "infra", "work", "other"], "settableRoles": ["work", "infra", "other"],
 "protectedBranches": ["testing", "launcher", "site", "discord-bot", "gh-pages"], "defaultBranch": "...",
 "staleDays": 7, "staleScope": {"type": "branch", "match": "claude/*"}, "staleRule": "limit-stale-days"}
```
The state's branches (/api/state) carry what this sets: `role` (production / testing from the repository, else q's
role or the one the name gives), `roleSet` (q set it), `description`, `kind` (the Branches tab's kind: `claude` for
work) and `stale`: a work / other branch, not protected, with no commit in `staleDays` days (the limit rule
`limit-stale-days`) that is merged into testing (nothing ahead) or behind it, and inside that rule's scope
(`staleScope`: a branch pattern limits it, e.g. `claude/*`; any other scope means every work / other branch).

`PUT /api/structure` `{"branches": {"<name>": {"role", "description"} | null}, "rev"?}` merges those entries (null
removes one; at most 100 per call, 2000 in all; description at most 2000 bytes). The role of testing and the default
branch can't be set. With `rev`, a structure changed since answers 409. -> 200 with the GET body.

### GET /api/rules
```
{"rules": [rule...], "rev": 7, "updatedAt": "...", "loadError": null,
 "kinds": ["push", "merge", "version", "ci", "limit", "other"], "scopes": ["all", "branch", "tool"],
 "hardRule": "only-q-production", "defaultBranch": "...", "limits": {"rules": 200, "titleBytes": 200, "textBytes": 4000},
 "docsSync": {"enabled": true, "state": "idle" | "waiting" | "syncing" | "ok" | "failed" | "off", "offReason": null,
              "repo": "floration/quark-claude-docs", "branch": "main", "files": [...], "dueAt": null,
              "lastOk": "...", "lastCommit": "<sha>", "lastAttempt": "...", "lastError": null,
              "failingSince": null | "...", "notesHeld": null | "text"}}
              // failingSince: the first failure of the current run of failures; notesHeld: see below
rule = {"id": "limit-open-prs-per-thread", "title": "...", "text": "markdown",
        "scope": {"type": "all" | "branch" | "tool", "match": "" | "<branch pattern>" | "<tool>"},
        "kind": "push" | "merge" | "version" | "ci" | "limit" | "other",
        "value": 1 | null, "unit": "PRs" | "", "locked": false, "updatedAt": "..."}
```
Seeded on the first start with the rules as they stand (the production rule first, then pushing / merging /
versions / CI rules and three limits: `limit-open-prs-per-thread`, `limit-stale-days`, `limit-ci-job-minutes`).
A seeded set is marked `"seeded": true` in rules.json until q edits it; before its first push to the Claude Documents
the server reads the `github-rules.json` already there and, when there is one, takes those rules instead (a
`rules.changed` event "loaded from the Claude Documents"), so a new install never pushes the seed over rules edited
elsewhere.
A limit has a numeric `value` (0-100000); other kinds have `value: null`. `locked` on other rules is q's mark
("agents never change it or work around it"); the page asks before changing one.

`PUT /api/rules` `{"rules": [rule...], "rev"?}` replaces the whole list (`updatedAt` is ignored and set by the
server; ids `[a-z0-9-]`, unique; at most 200 rules, title 200 and text 4000 bytes, body at most 256 KB). The
production rule must stay exactly as it is (422 locked otherwise). With `rev`, rules changed since answer 409.
-> 200 with the GET body; a `rules.changed` event.

### GET /api/pending
```
{"changes": [change...], "waiting": 3, "protectedBranches": [...], "defaultBranch": "...", "loadError": null}
change = {"id": "c0123456789ab",
          "type": "branch.create" | "branch.delete" | "branch.rename"
                  | "ruleset.create" | "ruleset.update" | "ruleset.delete",
          "params": {"name", "from"} | {"name"} | {"name", "newName"} | {"form"} | {"id", "form"} | {"id"},
          "status": "pending" | "applying" | "applied" | "failed" | "stale" | "refused",
          "summary": "Delete branch claude/x (at bc674fb)", "ref": {"branch": "..."} | {"ruleset": id},
          "before": {...} | null, "after": {...} | null,    // a ruleset's JSON, or the branch {name, sha, ...}
          "beforeText": "...", "afterText": "...",           // one sentence each
          "diff": ["--- before", "+++ after", "@@ ...", " ...", "+...", "-..."],   // rulesets: unified JSON diff
          "warnings": ["1 commit on claude/x is not in testing: ..."],          // unmerged commits, open PRs
          "basis": {...},             // what GitHub said at preview time; apply compares it again
          "call": {"method": "DELETE", "path": "repos/o/r/git/refs/heads/x", "body": null | {...}},
          "command": "gh api -i --method DELETE -H ... repos/o/r/git/refs/heads/x",
          "previewHash": "16 hex", "createdAt": "...", "previewedAt": "...",
          "result": null | {"ok": true, "at": "...", "httpStatus": 201, "sha" | "rulesetId" | "url" | "name": ...}
                         | {"ok": false, "at": "...", "error": "HTTP 422: ...", "httpStatus": 422, "locked"?: true}}
```
- `POST /api/pending` `{"type", "params"}` reads GitHub, checks the lock, builds the preview -> 201 `{"change"}`.
  Branch names are validated strictly (`refs/heads/` and `origin/` are dropped); `from` is a branch or a 7-40 hex
  sha (default `testing`). A ruleset `form` = `{name, enforcement: active|disabled, include: [1-20 patterns],
  exclude: [<=20], blockForcePush, blockDeletion, linearHistory, requirePr, approvals: 0-10, requiredChecks: [<=50
  names], strictChecks, adminBypass}`; rule types and bypass actors the form doesn't show are kept from the ruleset
  as it is. 409: the same change is already waiting, 100 are waiting, or the preview found a problem (the branch is
  gone, the name is taken, ...). 422: the lock. 502: GitHub could not be read.
- `DELETE /api/pending/<id>` (no fields) -> `{"discarded": id}`: nothing is sent to GitHub. Applied changes stay in
  the list (the last 30) until discarded ("Clear").
- `POST /api/pending/<id>/preview` (no fields) reads GitHub again and rebuilds the preview; the status goes back to
  pending -> `{"change"}`.
- `POST /api/pending/<id>/apply` `{"previewHash"?}`: with the hash the page showed, a change previewed again since
  answers 409 untouched. The server then reads the default branch and the target again, checks the lock again,
  rebuilds the call from `params` (never from the file) and compares it and the basis with the preview: a
  difference -> status `stale`, 409, error "changed on GitHub since preview, review again: <what changed>". Then
  `gh api -i --method <M> -H Accept -H X-GitHub-Api-Version <path> --input -` (the body on stdin). -> 200 applied,
  409 stale, 422 refused (`"locked": true`), 502 failed (GitHub's status and message, e.g. `HTTP 422: Repository
  rule violations found; Cannot delete this branch`); each `{"change", "error"?, "locked"?}`. Applied: a `github.*`
  event, the next poll runs at once and the rulesets are read again.
- `POST /api/pending/apply-all` (no fields) applies every `pending` change in order, stopping at the first one that
  isn't applied -> `{"results": [change...], "stoppedAt": id | null, "applied": n}`.

### GET /api/github/rulesets[?fresh=1]

The repository's rulesets, read from GitHub (with ETags) at most once a minute unless `fresh=1`:
```
{"rulesets": [{"id", "name", "target": "branch" | "tag" | "push", "enforcement", "sourceType", "createdAt",
               "updatedAt", "url", "ruleset": {normalized JSON}, "summary": ["blocks deletion", ...],
               "applies": "testing, claude/*", "locked": true, "lockReasons": ["..."],
               "form": null | {the form above}, "otherRules": ["update", ...], "currentUserCanBypass": "..."}],
 "defaultBranch": "...", "checks": ["meta", "linux", ...],   // job names of the recent runs + checks already required
 "fetchedAt": "...", "error": null | "text", "lockedNames": ["production rule", "tagging rule"],
 "protectedBranches": [...]}
```
A locked ruleset has `form: null` and says why (it targets the default branch, it is a tag ruleset, its name is
locked, it belongs to the organization).

### Writes

Every editor write (`PUT /api/structure`, `PUT /api/rules`, `POST /api/pending`, `DELETE /api/pending/<id>`,
`POST /api/pending/<id>/preview|apply`, `POST /api/pending/apply-all`) has the same checks as the note writes: the
Origin (or Referer) is this page or the Supervisor Board's 8768 (403), `Content-Type: application/json` (415), valid
JSON (400), at most 64 KB (256 KB for the structure and the rules; 413), only the fields above (400). Errors are
`{"error": "text"}` plus `"locked": true` for the lock and `"change"` when there is one. A file that can't be
written answers 500 and changes nothing.

### The Claude Documents copy

When the agent rules or the notes change, the server writes four files into its own clone of the private repo
floration/quark-claude-docs (branch main, folder `claude-docs/`, cloned with `gh repo clone` and pushed with gh's
credential helper): `10-github-rules.md` (the rules as markdown, with the header "Generated by the GitHub Manager;
edit there, not here"), `github-rules.json` (`{"generatedBy", "note", "repo", "defaultBranch", "rev", "updatedAt", "rules": [...]}`),
`11-github-notes.md` (the notes as markdown) and `github-notes.json` (`{"generatedBy", "note", "repo", "version": 1,
"notes": [note...]}`, sorted by id).

Notes round-trip like the rules: on the first sync from a data folder (a new PC, a lost data folder, notes written
before that first sync worked) and while this PC has written no note (no `editor/notes.json`), the notes already in
the Claude Documents are merged into the local store before anything is written (`github-notes.json`, else
`11-github-notes.md` parsed back; a note both have keeps the newer copy; one `note.restored` event). If the notes
there still can't be read back (a hand-written file), this PC's empty notes are never written over them: the two
notes files are left as they are (`docsSync.notesHeld`, a `poll.warnings` line) until a note is written here. 10 s after the last change (a burst is one commit), and 30 s after a start when the files
differ from what was last pushed: fetch, reset the clone to origin/main, write, commit "GitHub Manager:
rules/notes updated", push; a rejected push is retried once after `git pull --rebase`. Nothing else in that repo is
touched. A failure shows in `docsSync` (above), in the page and in `poll.warnings`, and is retried on every poll
(and on the next change) until it works; it never stops the server. Off with `GHM_DOCS_SYNC=off`, `"claudeDocs": false`, or a repo other than
floration/quark without `claudeDocsRepo`.


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
- `editor/structure.json`, `editor/rules.json`, `editor/pending.json` (each with a `.bak` of the version before the
  last change), `editor/docs-sync.json` (what was last pushed to the Claude Documents: digest, lastOk, lastCommit).
- `claude-docs/`: the clone of floration/quark-claude-docs the Claude Documents copy is written in (never served).
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
note), `&kind=general|branch|pr|run|commit|version|local`, `&status=open|done|all`, `&q=<text>`; its sections
`#tab=editor&sec=notes|structure|rules|pending` (Rules: `&rkind=all|push|merge|version|ci|limit|other`, `&q=`). The
Branches tab's `&kind=` also takes `stale`.

The Tests tab embeds the tests editor, `http://127.0.0.1:8775/?embed=1` (made only when the tab opens; when 8775
doesn't answer within 1.5 s it says so). Each job of a run (the run's drawer) and each failed job on a run row links to
it as `http://127.0.0.1:8775/#ci/<workflowFile>/<job name, URL-encoded>?run=<run id>` (the tests editor drops matrix
suffixes such as ` (net)` itself).
