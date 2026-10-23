# Downloads the Windows build that GitHub Actions (release.yml, build-and-release) made for a branch's newest
# successful run, unpacked into its own folder under -OutDir next to your local builds (tools\build_windows.ps1).
# Needs the GitHub CLI, signed in once: winget install GitHub.cli; gh auth login
#   powershell -ExecutionPolicy Bypass -File tools\fetch_ci_build.ps1 [-Branch testing] [-Platform windows] [-OutDir D:\quark-builds]
param(
  [string]$Branch = 'testing',
  [ValidateSet('windows', 'linux', 'macos')][string]$Platform = 'windows',
  [string]$OutDir = (Join-Path $env:USERPROFILE 'quark-builds'),
  [string]$Repo = 'floration/quark'
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  Write-Error 'Needs the GitHub CLI: winget install GitHub.cli, then gh auth login (once).'
}
$run = gh run list -R $Repo --workflow build-and-release --branch $Branch --status success --limit 1 --json databaseId,headSha,displayTitle | ConvertFrom-Json
if (-not $run) { Write-Error "No successful build-and-release run on '$Branch' yet." }
$run = $run[0]
$sha = $run.headSha.Substring(0, 7)
$dest = Join-Path $OutDir "ci-$Branch-$sha-$Platform"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null
# (artifacts are kept 90 days; the run's "windows" artifact holds quark-windows.zip, .qpak and the launcher)
gh run download $run.databaseId -R $Repo -n $Platform -D $dest
if ($LASTEXITCODE -ne 0) { throw 'download failed (see gh above: the artifact may have expired, or GitHub had a passing error such as HTTP 503; try again)' }
Get-ChildItem $dest -Filter *.zip | ForEach-Object { Expand-Archive -Force $_.FullName $dest }
Write-Host "Run $($run.databaseId) on $Branch ($sha): $($run.displayTitle)"
Write-Host "  $dest"
