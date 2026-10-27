# Tests editor

A local page that shows every test quark has and when each one runs: the `quark_tests` groups and commands
(tests/tests.cpp `main()`), every test function with its doc comment and CHECKs, the CI workflows' jobs and steps
(with their `if:` in plain words, and how `[final]` turns the sanitizers on), the scripts (tools/release_check.sh,
test_editor.sh, uitest.sh), the git hooks and ctest. Each one as code, a graph and blocks; you can edit the files,
see the diff, revert, and run locally: the whole suite, a chosen set of groups (QUARK_TEST_ONLY), the failed groups
again, one group, or a command with its arguments (rules NAME, serverbench secs run...). Each group's last local
result shows in the tree; FAIL lines in the run output are marked, with a jump to the next one and a link to the CHECK.
Search (`/`) matches names, called functions, job steps, and a function's doc comment and CHECK expressions.
Side panels: drag their edge to resize, `[` / `]` (or the header buttons) to hide them. In the code editor Tab indents;
Esc then Tab leaves it. Rule checks (lint) flag CI running gameplay tests, a stray
`[final]`, functions nothing runs, calls to missing functions, two parallel groups on one port, bad `needs`, unpinned
actions, a script's group list that no longer matches tests.cpp.

The page is `web/`; the server is `serve.py` (Python 3 standard library only); the data contract is `API.md`.

## Start it

```
tests-editor.cmd                                    (Windows: no window, opens http://127.0.0.1:8775/)
py -3 serve.py --port 8775 --no-browser             (how the Supervisor Board starts it, hidden)
python3 serve.py [--repo <quark checkout>] [--port 8775] [--no-browser] [--config tests_editor.json]
python3 serve.py --snapshot model.json              (write the model once and exit)
```

It listens on 127.0.0.1 only. With `QUARK_SHELL=1` (the Quark app) it never opens a browser. If one is already
running on the port it only opens the page. Logs go to `cache/serve.log`.

## Install / update

```
py -3 tools\tests_editor\install.py [--target DIR] [--quiet]      (or install.cmd)
```

Copies the tool into the `tests-editor` folder beside the checkout it runs from (from `Quark\repository\` that is
`Quark\tests-editor\`). The Quarkology app runs exactly `py -3 tools\tests_editor\install.py --quiet` from
`repository\` every time it opens: it asks nothing, opens nothing, overwrites the tool files (serve.py, model/, web/,
API.md, README.md, tests-editor.cmd), keeps an existing `tests_editor.json` and `cache/`, and is safe to re-run while
the server runs from there (the app restarts it). Exit 0 on success; on failure one line on stderr and exit 1. A
first install writes `tests_editor.json` pointing at `..\working-repo`.

## What it reads and writes

Reads, in the working copy: `tests/*.cpp`, `tests/*.h`, `.github/workflows/*.yml`, `tools/*.sh`, `tools/githooks/*`,
`CMakeLists.txt`, `VERSION`, `CLAUDE.md`, plus `git status` / `git diff HEAD` / `git show HEAD:<file>`. Each file is
parsed again only when its time or size changes. Group timings come from this machine's runs
(`cache/groups.json`: every run from the page, and logs imported with "Import a run log…"; run the whole suite once
on a new machine), else from the release-tests notes (`timingDir`, see below). Known flakes come from the notes too. CI results come from the GitHub Manager (port 8767), if it runs.

Writes, in the working copy, only when you save or revert, and only these files: `tests/**` (text files),
`.github/workflows/*.yml`, `tools/*.sh`, `tools/githooks/*`, `CMakeLists.txt`. A save is refused (409) when the file
changed since you opened it; the old text is kept in `cache/backups/<path>/` (last 50 per file). Line ends stay as
the file had them. It never commits, pushes or changes git state.

Runs (one at a time, low priority, no window): `build/quark_tests` (the whole suite, or `QUARK_TEST_ONLY=a,b`),
`build/quark_tests group NAME`, `build/quark_tests NAME [args]`, `tools/release_check.sh` (needs bash: Git for
Windows), the UI test (`tools/uitest.sh build/quark <report>`, about 10-15 minutes) and `tools/test_editor.sh`
(`QUARK=build/quark`). The last two need a display: on Linux the server starts a private Xvfb (:100 and up) for the
run and stops it with the run (else it uses the desktop's DISPLAY); on Windows the game opens a normal window. A
script's exit 77 (no display) shows as skipped. The UI test's report is kept beside the run's log and shown under
the script. Each map release_check.sh hosts keeps its last result, screenshot and log (`cache/maps/`), shown as
"Maps hosted" under release_check.sh. Commands that write files into the working copy (exportassets, export*) are not run from the page; their read-only
aliases (checkbrain FILE, printweapons FILE, modeast FILE...) have entries of their own and are. An argument is a word
or a repo-relative path (no `..`). The binary is `build/quark_tests[.exe]`,
else the newest `build-*/quark_tests[.exe]`, else `quark_tests` from the config. Output: `cache/runs/<id>.log`,
history `cache/runs.json` (last 500), each group's newest time and result, its last 12 times (a ▲ in the tree and
a note under Timing when the newest is over 50% above their median) and each map's result `cache/groups.json`; each run gets its own temp folder (`cache/tmp/<id>`, last 5 kept). Stop ends
the run and everything it started (by its process id).

## tests_editor.json (beside serve.py; see tests_editor.example.json)

| key | default | what |
|---|---|---|
| `repo` | `../working-repo`, else the checkout serve.py is in | the quark working copy (relative to this file) |
| `port` | 8775 | the port (`--port` wins) |
| `timingDir` | `null`: the cloud's `/mnt/project-files/release-tests/timing` if it exists | folder of the timing notes (`stepN.txt`, `baseline.txt`), used for a group this machine has no time for; `""` = none |
| `overview` | `<timingDir>/../overview.md` | the release-tests overview; section 5 lists the known flakes |
| `quark_tests` | `null` | a test binary to use when there's no build folder |
| `githubManager` | `http://127.0.0.1:8767` | the GitHub Manager, for `/api/ci` |

## Tests

`python3 tools/tests_editor/test_serve.py`: parses the real repo and the fixture repo `test/repo/`, then runs the
server against a temp copy (save / 409 / backup / revert / diff, the allowlist and path traversal, runs with a fake
quark_tests, the CI proxy, CORS, install). It never writes into the repo.

`node tools/tests_editor/web/test/run.mjs`: the page in headless Chromium against `web/test/fixture_server.py` (a
stand-in server over a temp copy): views, block edits and their patches, 409s, routes, search, panels, runs (FAIL
lines, jump, args, chosen groups). `node tools/tests_editor/web/test/patch_test.mjs`: the block patches on the real files.
