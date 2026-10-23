# Compiles the current production release (the newest v<VERSION> tag) and the current testing version (the tip of the
# testing branch) on this computer, each into its own folder, to check the release sources build here. Unsigned, and
# the launcher is each source's own (CI takes it from the `launcher` branch), so these can differ slightly from what
# players install; tools\fetch_release.ps1 fetches the signed release files instead.
# Each channel's source is a git worktree of this checkout in ..\.src\<channel> (CI-free, detached; never edited), and
# the build goes into ..\<channel> next to the checkout (the layout in docs/LOCAL_BUILD.md; build-releases.cmd runs this).
#   powershell -ExecutionPolicy Bypass -File tools\build_release.ps1 [-Channel production|testing|both] [-Force]
param(
  [ValidateSet('production', 'testing', 'both')][string]$Channel = 'both',
  [switch]$Force   # rebuild even when the folder already holds a build of that commit (and version suffix)
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Base = Split-Path -Parent $Root
$channels = if ($Channel -eq 'both') { 'production', 'testing' } else { , $Channel }

git -C $Root fetch -q --tags origin testing
if ($LASTEXITCODE -ne 0) { throw 'git fetch failed (network?)' }
foreach ($ch in $channels) {
  $suffix = ''
  if ($ch -eq 'production') {
    # (exactly v<digits>.<digits>.<digits>: the production tag form; newest version first)
    $ref = git -C $Root tag -l 'v*' --sort=-v:refname | Where-Object { $_ -match '^v\d+\.\d+\.\d+$' } | Select-Object -First 1
    if (-not $ref) { Write-Warning 'No production tag yet'; continue }
  } else {
    $ref = 'origin/testing'
    # CI builds testing as <VERSION>-test.<run number> and tags the commit test-<that>: use the same version when tagged
    # (an untagged tip builds as plain <VERSION>)
    $tag = git -C $Root tag --points-at $ref -l 'test-*' | Select-Object -First 1
    if ($tag -match '(-test\.\d+)$') { $suffix = $Matches[1] }
  }
  $sha = (git -C $Root rev-parse "$ref^{commit}").Trim()
  $dest = Join-Path $Base $ch
  $marker = Join-Path $dest 'BUILD.txt'
  if (-not $Force -and (Test-Path $marker) -and ((Get-Content $marker -TotalCount 1) -like "$ch $ref $sha$suffix *")) {
    Write-Host "$ch is already a build of $ref ($($sha.Substring(0, 7))): $dest"; continue
  }
  $src = Join-Path (Join-Path $Base '.src') $ch
  if (Test-Path (Join-Path $src '.git')) { git -C $src checkout -q --detach $sha }
  else { git -C $Root worktree prune; git -C $Root worktree add -q --detach $src $sha }
  if ($LASTEXITCODE -ne 0) { throw "couldn't check out $ref into $src" }
  Write-Host "== $ch`: building $ref ($($sha.Substring(0, 7)))"
  & (Join-Path $PSScriptRoot 'build_windows.ps1') -Source $src -Play $dest -VersionSuffix $suffix
  Set-Content $marker "$ch $ref $sha$suffix (built on this PC $(Get-Date -Format 'yyyy-MM-dd HH:mm'); unsigned)"
  Write-Host "$ch is now a build of $ref`: $dest"
}
exit 0
