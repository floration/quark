# Tests editor: data contract (server <-> page)

`serve.py` (Python 3 stdlib only) serves `web/` at http://127.0.0.1:8775/ and the JSON below. It reads and writes
one working copy of the quark repo (`repo` in `tests_editor.json`, default `..\working-repo` next to the install
folder, else the repo the tool sits in). It never commits, pushes or touches git state; saving writes the file only.
`?embed=1` hides the tools nav and the page's "Tests editor" name; the header bar with its buttons stays (it is shown
inside the GitHub Manager / Supervisor Board). All times are ISO
8601 UTC; the page shows them in Dallas time (America/Chicago). CORS: 127.0.0.1 ports 8765-8775.

## GET /api/model

Everything the page shows, parsed from the working copy's files on every request whose files changed (mtime cache).

```
{
  "generatedAt": "...",
  "repo": {"path": "...", "branch": "testing", "sha": "abc1234", "dirty": ["M tests/tests.cpp", ...], "version": "1.18.11"},
  "rules": [{"id": "ci-scope", "text": "CI runs only meta, builds and sanitizers; no gameplay tests in CI.", "source": "project memory / CLAUDE.md"},
            {"id": "final", "text": "Sanitizers run only for a human-marked [final]; agents never add [final].", ...},
            {"id": "local-before-commit", "text": "Build and run build/quark_tests before every commit.", ...}],
  "suites": [suite, ...],                 // order: quark_tests, CI workflows, scripts, hooks, ctest
  "functions": {"testWeapons": fn, ...},  // every test* / *Tests function defined under tests/
  "lint": [{"level": "warn" | "error", "file": "...", "line": 12, "text": "..."}]   // rule checks, see below
}

suite = {
  "id": "quark_tests" | "ci:release.yml" | "script:release_check.sh" | "hook:pre-commit" | "ctest",
  "kind": "local" | "ci" | "script" | "hook" | "ctest",
  "title": "quark_tests", "file": "tests/tests.cpp", "line": 6867,
  "when": [when, ...],                    // when this suite runs, in plain words + machine form
  "items": [item, ...]                    // groups (local), jobs (ci), steps (script/hook)
}
when = {"trigger": "push" | "tag" | "pull_request" | "workflow_dispatch" | "schedule" | "workflow_run" | "manual"
                    | "before-commit" | "pre-commit" | "ctest" | "release-check",
        "text": "a push to any branch",  "branches": [], "tags": [], "paths": [], "inputs": [{"name","type","default","description"}],
        "cron": null, "file": "...", "line": 3}

item (local group) = {
  "id": "group:weapons", "kind": "group", "name": "weapons", "list": "par" | "alone" | <other vector name>,
  "order": 5,                              // position in its list
  "file": "tests/tests.cpp", "line": 6980, "endLine": 6980,
  "calls": [{"fn": "testWeapons", "args": "TP(28530)", "line": 6980}],   // in order; fn resolves in model.functions
                                           // (a call inside a lambda handed to another call has "via": "timingTest")
  "run": {"cmd": "build/quark_tests group weapons", "parallel": true},    // alone = false
  "timing": {"sec": 31.0, "source": "local run 2026-10-24T..." | "imported log ..." | "release-tests/timing/step1.txt"} | null,
  "lastRun": {"status": "passed" | "failed", "at": "...", "run": "<run id>" | null, "fails": 0, "source": "...",
              "failLines": [{"text": "FAIL /x/tests/tests.cpp:120: a == b", "file": "tests/tests.cpp", "line": 120}]} | null,
  "flaky": "text" | null
}
item (also kind "shortcut") = a one-off `if (argv[1] == "x")` command in main(): {"id": "cmd:signing", "kind": "shortcut",
  "name", "file", "line", "calls": [...], "run": {"cmd": "build/quark_tests signing", "parallel": false, "byHand": "why" (not run
  from the page)}, "aliases": ["checkbrain", ...], "argsSpec": {"min": 1, "max": 1 | null (any), "params": [{"name", "default"}],
  "hint": "the if line's comment", "choices": ["overtime", ...] (rules NAME)}}  (may disappear; tool commands like
  exportassets/serverbench are kind "tool")
fn = {"name": "testWeapons", "file": "tests/tests.cpp", "line": 3100, "endLine": 3290, "args": "uint16_t port",
      "doc": "comment block right above it", "checks": [{"line": 3110, "expr": "a.hp == 100"}], "calls": ["testX", ...],
      "usedBy": ["group:weapons", "cmd:weapons"],               // groups / commands that call it directly
      "runsVia": [{"item": "group:rules2", "path": ["testGameRules"]}]}   // ...and through other functions (shortest chain)

item (ci job) = {
  "id": "job:linux", "kind": "job", "jobId": "linux", "name": "display name", "file": ".github/workflows/release.yml",
  "line": 40, "endLine": 120, "runsOn": "ubuntu-24.04", "needs": ["meta"], "if": "raw expression" | null,
  "ifText": "plain words, e.g. 'only when meta says sanitizers=true ([final])'", "timeoutMin": 30,
  "matrix": {"group": ["a","b"]} | null, "env": {...},
  "lines": {"if": 44 | null, "needs": 42 | null, "runsOn": 41, "steps": 50},
  "steps": [{"name": "Build", "line": 52, "endLine": 60, "id": "b" | null, "if": "..." | null, "uses": "actions/checkout@<sha>" | null,
             "run": "shell text" | null, "with": {...}}],
  "outputs": {"name": "expr"}
}
item (script/hook step) = {"id": "step:3", "kind": "step", "name": "parallel groups", "line": 14, "endLine": 17, "text": "..."}
```

The page computes the graph and the blocks from this model; the server does not lay anything out.

## GET /api/file?path=<repo-relative> -> {"path", "text", "sha256", "mtime"}

Only files under `tests/`, `.github/workflows/`, `tools/*.sh`, `tools/githooks/`, `CMakeLists.txt`.

## POST /api/file {"path", "text", "baseSha256"} -> {"ok": true, "sha256", "model"}

Writes the whole file (atomic: temp file + rename; LF kept as the file had it). 409 `{"error": "changed", "sha256"}`
when the file on disk no longer has `baseSha256` (someone else edited it). 403 outside the allowlist. Before writing,
the server keeps the previous text in `cache/backups/<path>/<time>.txt` (last 50 per file).

## GET /api/diff?path=<p> -> {"path", "diff"}   (unified diff vs HEAD; no path = every allowed file)
## POST /api/revert {"path"} -> restores the file from HEAD (`git show HEAD:<p>`, written like a save; backup kept)

## POST /api/run {"cmd": "suite" | "group:<name>" | "cmd:<name>" | "script:release_check.sh" | "groups", "groups"?, "args"?} -> {"id"}
`suite` runs `build/quark_tests` (the whole suite, as before a commit); with `"groups": ["a", "b"]` just those, as
`QUARK_TEST_ONLY=a,b` (par at once, then alone). `cmd:<name>` takes `"args": ["5", "1"]` (short words only), checked
against the command's `argsSpec` (400 otherwise); a command with `run.byHand` (writes files into the working copy:
exportassets, export*; the tests' own bot process) gets 403.
Runs locally in the working copy, low priority (Windows: BELOW_NORMAL + no window; Linux: nice 10), one at a time
(409 if one is running). The test binary is found at `build/quark_tests[.exe]`, else `build-*/quark_tests[.exe]`
(newest), else `quark_tests` in `tests_editor.json`; 404 `{"error": "no build"}` if none.
## GET /api/run?id=<id>&from=<byte> -> {"id", "cmd", "args", "only", "status": "running" | "passed" | "failed" | "stopped", "exit", "startedAt", "endedAt", "sec", "output" (from byte `from`), "size", "groupResults"}
## POST /api/run/stop {"id"}
## GET /api/runs -> [{"id", "cmd", "args", "only", "status", "exit", "startedAt", "endedAt", "sec", "groupResults"}]   (local history, cache/runs.json, last 500)
Every finished run feeds `cache/groups.json` (the tool's data folder, kept across updates): per group its newest time,
from the `(group X: N s)` lines of the whole suite, `group X: N s` of one group or `X rc=N group X: N s` of
release_check.sh, and its newest result (passed / failed, its FAIL lines; release_check.sh: from the group's own log).
A stopped suite run keeps the groups that finished; a stopped single group gives nothing. That file is
`item.timing` (it beats the release-tests notes) and `item.lastRun`.
## POST /api/import-log {"text", "name"?} -> {"ok", "groups": n}
The output of a run made elsewhere (`build/quark_tests > log.txt` in a terminal): its group times and results go into
`cache/groups.json` as above ("imported log <time>").

## GET /api/ci -> proxy of the GitHub Manager (http://127.0.0.1:8767/api/state), reduced to
`{"available": bool, "jobs": {"<workflow file>/<job id or name>": {"conclusion", "url", "runId", "branch", "at", "durationSec", "failedSteps": []}}}`
(the last run per job). `available: false` when 8767 is down.

## GET /api/summary -> {"suites": n, "groups": n, "jobs": n, "checks": n, "dirty": n, "lint": n, "lastRun": {...} | null}

## Lint (in /api/model)

Rule checks the server runs on every model: a CI job that runs `quark_tests` gameplay groups (CI scope rule); a
`[final]` added anywhere except the meta trigger's `case`; a function defined in tests/ that no group or command calls;
a group calling a function that doesn't exist; two groups in `par` sharing a TP() port; a job whose `needs` names a
job that doesn't exist; an action not pinned by a 40-char commit SHA.

## Page routes (hash)

`#suite/<suite id>`, `#group/<name>`, `#cmd/<name>`, `#fn/<name>`, `#ci/<workflow file>/<job id or name>[?run=<run id>]` (a name
like "linux-tests (netcode)" drops its matrix suffix), `#when` (the calendar of when everything runs). Each item has
three views (tabs): **Code**, **Graph**, **Blocks**.

## Extra keys the server also sends (compatible additions; the page may use or ignore them)

- model: `buildMs`, `buildGroups` (`{binary, builtAt, groups, error}` from `quark_tests groups` of the newest build,
  run in the background; null without a build), `timingSource`; `repo.git` (false: git unavailable).
- quark_tests suite: `lists` (`[{name, line, endLine, groups, parallel}]`), `checkMacros` (CHECK, ACHECK, LCHECK...).
- group / shortcut items: `ports` (`[[port, fn]]`), call `argsValue` (`p` resolved to `TP(28900)`) and `port`;
  `timing.at` (and `timing.run` for a local run); shortcut `shadowsGroup`.
- fn: `ret`, `checks[].macro`, `helpers` (non-test functions it calls), `calledBy`, `flaky`, `portOffsets` (the K of
  every `port + K`), `overloads`.
- ci suite: `workflowName`, `env`, `concurrency`, `about` (header comment), `sanitizerTrigger` (`{file, job, step,
  output, var, line, endLine, arms: [{line, condition, text}], text}`: meta's `case` that sets sanitizers), `error` /
  `errorLine` when the YAML doesn't parse. Job: `lines.timeoutMin/outputs/env/permissions`, `permissions`,
  `environment`, `continueOnError`, `comment`. Step: `ifText`, `runLine` (first line of the run text), `env`,
  `workingDirectory`, `timeoutMin`, `continueOnError`, `lines` (key -> line).
- when: `via` (the suite / job that runs quark_tests), `types` (pull_request), `workflows` (workflow_run).
- script / hook suites: `about`; ctest items are kind "step" with `command`, `timeoutSec`, `skipCode`, `condition`.
- lint: `rule` (ci-scope, final, needs, pin, yaml, missing-fn, unused, port, shadow, script-list, build-groups). `unused`
  follows calls through other functions and into lambdas handed to a call (timingTest).
- GET /api/file: `eol` ("lf" | "crlf"), `size`; 415 for a file that isn't UTF-8. POST /api/file with
  `baseSha256: null` creates a new file (409 if it exists). /api/run accepts any `script:<name>` suite in the model;
  GET /api/run also has `from`, `next`, `more` (output is sent 512 KB at a time), `groupTimes`. /api/ci jobs: `sha`;
  plus `polledAt`. /api/summary: `lintErrors`, `generatedAt`, `version`. GET /api/health -> `{ok, version, repo, tool}`.
- POST requests from an Origin outside 127.0.0.1 / localhost 8765-8775 get 403.
