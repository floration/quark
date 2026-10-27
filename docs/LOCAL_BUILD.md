# Building quark on your own computer (Windows)

Two ways to get a build onto your machine. Both keep every build in its own folder under `%USERPROFILE%\quark-builds`
(change it with `-OutDir`), so older builds stay around to compare against.

| | What you get | When |
|---|---|---|
| `tools\build_windows.ps1` | A build of your local checkout, made on your PC | Trying your own changes, performance checks (REMAINING 0.3) |
| `tools\fetch_ci_build.ps1` | The Windows build GitHub Actions made for a branch | Trying what was pushed, without building |

Neither is signed: signing happens only in CI's release jobs (docs/SIGNING.md).

## The folder layout

The owner's PC keeps one folder per version of the game, with two clones beside them:
```
Desktop\Quark\
  repository\     a clone that mirrors GitHub's testing branch (only ever pulled)
  quarkology-repo\  Quarkology's own clone (floration/quarkology, private: the Supervisor Board app, the editors and
                  wip\), on main; Quarkology clones and updates it by itself and syncs its wip\ between your PCs
  working-repo\   your own clone: local code changes live here, committed locally, pushed when you choose
  working\        the game built from working-repo\              (build-working.cmd)
  testing\        the tip of the testing branch, built here       (build-releases.cmd)
  production\     the newest v<VERSION> tag, built here            (build-releases.cmd)
  .src\           the testing and production sources build-releases.cmd checks out (git worktrees of repository\)
  build-working.cmd     builds working-repo\ into working\
  build-releases.cmd    builds the current production and testing sources into their folders
```
Both .cmd files are double-clickable; their sources are `tools/local/`. Everything is compiled on the PC, to prove the
sources build there; `build-releases.cmd` skips a folder that already holds a build of the current commit (`-Force`
rebuilds). These builds are unsigned and use each source's own launcher, so they can differ slightly from what players
install; `tools\fetch_release.ps1` puts the signed release files into the same folders instead. Each game folder has a `BUILD.txt` saying what's in it; updates copy
over the old files and never empty a folder first. All three share one set of settings, keys and your own maps
(`%APPDATA%\quark`); to keep one apart, start it with `quark.exe --data-dir <folder>`.

working-repo\ is a separate clone, not a worktree of repository\, so either one survives the other being damaged.
The editors and the Supervisor Board app are no longer in this repository (1.20): Quarkology installs them from
quarkology-repo\ and builds their native helper, `quark_maptool`, from repository\ (`tools/maptool/`).
To bring in work pushed from elsewhere (a cloud session's PR merged into testing): `git -C working-repo pull`.

## 1. One-time setup

In PowerShell:
```
winget install Git.Git
git clone https://github.com/floration/quark $env:USERPROFILE\quark
cd $env:USERPROFILE\quark
powershell -ExecutionPolicy Bypass -File tools\build_windows.ps1 -Setup
```
`-Setup` installs whichever of CMake, Python 3.12 and MinGW-w64 (WinLibs, posix threads: the same compiler family CI
uses) aren't on PATH yet with winget (without winget it warns and you install them yourself), points git at `tools/githooks` (the secrets check before every commit), then builds. Open a new terminal
afterwards so the new PATH is picked up. MSVC isn't supported.

## 2. Build locally

```
cd $env:USERPROFILE\quark
git pull
powershell -ExecutionPolicy Bypass -File tools\build_windows.ps1
```
The result:
- `quark-builds\quark-<VERSION>-<commit>\quark.exe` (plus `quark_server.exe`, `quark_launcher.exe`, `maps\`, `mods\`), laid out like CI's `quark-windows.zip`;
- `quark-builds\quark-<VERSION>-<commit>.zip`, the same folder zipped;
- `quark-builds\latest\`, always the newest build.

A build of uncommitted changes gets `-dirty` in its name. Options: `-Tests` also builds and runs `quark_tests`
(on a busy machine the timing groups can fail: HANDOVER section 3), `-Clean` starts from an empty `build-win\`.
The build's own files (`build-win\`) stay in the checkout and are ignored by git.
Linking prints `ld.exe: .rsrc merge failure: multiple non-default manifests` and newer GCCs warn that NOMINMAX is
already defined: both harmless (the exes carry only quark's own manifest). A full build takes about a minute.
After building, `quark.exe --bake-maps` bakes the official maps' light and `tools\pack_lightcache.py` puts it, zlib'd,
into the build's `maps\lightcache\` (as CI does for every package: no first join waits for a bake). Raw bakes are kept in
`build-win\lightcache\` between builds, so only changed maps are baked again: about 25 s for all 21 on a 20-thread CPU,
under a second when none changed, plus ~1.5 s to pack.

## 3. Download a CI build

Every push to any branch is built and tested by `.github/workflows/release.yml` (build-and-release). To pull the
newest successful Windows build of a branch onto your PC:
```
winget install GitHub.cli
gh auth login
powershell -ExecutionPolicy Bypass -File tools\fetch_ci_build.ps1 -Branch testing
```
It lands in `quark-builds\ci-<branch>-<commit>-windows\`. (`tools\fetch_release.ps1` instead fetches the signed
production and testing releases into their folders.) `-Platform linux` or `macos` fetches those instead.
A build of a branch without pushing anything new: Actions → build-and-release → Run workflow, pick the branch,
leave **site** unticked (it builds and tests only). Artifacts expire after 90 days; testing and production builds
also stay on the Releases page.

## 4. The git workflow from a local clone

A local clone pushes with your own credentials, so it can push `testing` and tags directly (CLAUDE.md "Releases"):
```
git switch testing && git pull            # all new work goes here
# ...change, build, test...
git commit -am "what and why"
git push                                  # CI builds it and publishes a signed testing prerelease
```
Production stays as before: the `testing` → default-branch PR, then `git tag v<VERSION> <merge commit> && git push origin v<VERSION>`.
