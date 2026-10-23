# Puts the current production or testing release of quark (the signed builds on the GitHub Releases page, what the
# launcher installs) into a folder on this computer. production = the latest release, testing = the newest
# test-* prerelease. Each channel goes into ..\<channel> next to the checkout when that folder exists (the layout
# in docs/LOCAL_BUILD.md); build-releases.cmd compiles the same versions instead.
# Needs the GitHub CLI, signed in once: winget install GitHub.cli; gh auth login
#   powershell -ExecutionPolicy Bypass -File tools\fetch_release.ps1 [-Channel production|testing|both] [-Into D:\quark\production]
param(
  [ValidateSet('production', 'testing', 'both')][string]$Channel = 'both',
  [string]$Into = '',   # (one channel only) default: ..\<channel> next to the checkout, else %USERPROFILE%\quark-builds\<channel>
  [string]$Repo = 'floration/quark'
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  Write-Error 'Needs the GitHub CLI: winget install GitHub.cli, then gh auth login (once).'
}
$Root = Split-Path -Parent $PSScriptRoot
$channels = if ($Channel -eq 'both') { 'production', 'testing' } else { , $Channel }

foreach ($ch in $channels) {
  $rels = gh api "repos/$Repo/releases?per_page=30" | ConvertFrom-Json
  if ($ch -eq 'production') { $rel = $rels | Where-Object { -not $_.prerelease -and -not $_.draft -and $_.tag_name -like 'v*' } | Select-Object -First 1 }
  else { $rel = $rels | Where-Object { $_.prerelease -and -not $_.draft -and $_.tag_name -like 'test-*' } | Select-Object -First 1 }
  if (-not $rel) { Write-Warning "No $ch release yet"; continue }
  $dest = $Into
  if (-not $dest) {
    $dest = Join-Path (Split-Path -Parent $Root) $ch
    if (-not (Test-Path $dest)) { $dest = Join-Path (Join-Path $env:USERPROFILE 'quark-builds') $ch }
  }
  $marker = Join-Path $dest 'BUILD.txt'
  if ((Test-Path $marker) -and ((Get-Content $marker -TotalCount 1) -like "$($rel.tag_name) *")) { Write-Host "$ch is already $($rel.tag_name): $dest"; continue }
  $tmp = Join-Path $env:TEMP "quark-$ch-$PID"
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
  gh release download $rel.tag_name -R $Repo -p quark-windows.zip -D $tmp
  if ($LASTEXITCODE -ne 0) { throw "download of $($rel.tag_name) failed (see gh above; GitHub has passing errors such as HTTP 503: try again)" }
  Expand-Archive -Force (Join-Path $tmp 'quark-windows.zip') $tmp
  # (copied over the old files, never emptied first: settings and your own maps live in %APPDATA%\quark anyway)
  New-Item -ItemType Directory -Force $dest | Out-Null
  robocopy (Join-Path $tmp 'quark-windows') $dest /E /NFL /NDL /NJH /NJS /NP | Out-Null   # (merges into the existing folders)
  if ($LASTEXITCODE -ge 8) { throw "copying into $dest failed (robocopy $LASTEXITCODE)" }
  Set-Content $marker "$($rel.tag_name) (GitHub release, published $($rel.published_at))"
  Remove-Item -Recurse -Force $tmp
  Write-Host "$ch is now $($rel.tag_name): $dest"
}
exit 0   # (robocopy leaves a nonzero code for success)
