# Builds quark on Windows (native MinGW-w64, like release.yml's windows job) and keeps every build on this computer:
# each one goes into its own folder under -OutDir, named <version>-<commit>, next to a zip of it, and `latest` is
# refreshed to the newest. -Play also copies it into the folder you play from (build-working.cmd in the owner's folder
# layout passes its `working` folder). Unsigned (signing happens only in CI). docs/LOCAL_BUILD.md has the details.
#   powershell -ExecutionPolicy Bypass -File tools\build_windows.ps1 -Setup     (first time: installs the tools)
#   powershell -ExecutionPolicy Bypass -File tools\build_windows.ps1 [-Tests] [-OutDir D:\quark-builds]
param(
  [string]$OutDir = (Join-Path $env:USERPROFILE 'quark-builds'),
  [switch]$Setup,   # install Git, CMake, Python and MinGW-w64 (WinLibs, posix threads) with winget, then build
  [switch]$Tests,   # also build quark_tests and run it (every group; a busy machine can fail the timing groups)
  [switch]$Editors,   # also build the editors' helpers (quark_maptool, quark_tests) here, not run: shared code compiles once
  [switch]$Clean,   # start from an empty build-win/
  [string]$Play = '',   # also copy the build into this folder (merged over what's there)
  [string]$Source = '',  # build this checkout instead of the one holding this script (build_release.ps1 uses it)
  [string]$VersionSuffix = ''   # appended to VERSION as CI does for testing builds (.testing.<run number>; build_release.ps1)
)
$ErrorActionPreference = 'Stop'
$Root = if ($Source) { (Resolve-Path $Source).Path } else { Split-Path -Parent $PSScriptRoot }
Set-Location $Root

function Refresh-Path {   # (winget installs change PATH for new shells only; keep this shell's own entries too)
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + $env:Path
}

if ($Setup) {
  # winget id -> the command it provides; tools already on PATH are left alone
  $tools = [ordered]@{ 'Git.Git' = 'git'; 'Kitware.CMake' = 'cmake'; 'Python.Python.3.12' = 'py'; 'BrechtSanders.WinLibs.POSIX.UCRT' = 'g++' }
  $haveWinget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
  foreach ($id in $tools.Keys) {
    if (Get-Command $tools[$id] -ErrorAction SilentlyContinue) { Write-Host "have $($tools[$id]), skipping $id"; continue }
    if (-not $haveWinget) { Write-Warning "winget isn't available (App Installer from the Microsoft Store): install $id yourself"; continue }
    Write-Host "winget: $id"
    winget install --id $id -e --accept-package-agreements --accept-source-agreements --disable-interactivity
    # (exit codes: 0 installed; already installed / no upgrade is not an error)
  }
  Refresh-Path
  git config core.hooksPath tools/githooks   # the secrets check before every commit (CLAUDE.md)
}

$missing = @()
foreach ($t in 'git', 'cmake', 'g++', 'mingw32-make') { if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { $missing += $t } }
# the Microsoft Store's python.exe stub is on PATH even with no Python installed: ask the launcher (py) first
$py = $null
if (Get-Command py -ErrorAction SilentlyContinue) { $py = (& py -3 -c 'import sys; print(sys.executable)' 2>$null) }
if (-not $py) { $c = Get-Command python -ErrorAction SilentlyContinue; if ($c -and $c.Source -notmatch 'WindowsApps') { $py = $c.Source } }
if (-not $py) { $missing += 'python3' }
if ($missing.Count) {
  Write-Error "Missing: $($missing -join ', '). Run once with -Setup (or install them yourself), then open a new terminal."
}

$version = (Get-Content VERSION -TotalCount 1).Trim() + $VersionSuffix
$commit = (git rev-parse --short HEAD).Trim()
if (git status --porcelain) { $commit += '-dirty' }   # uncommitted changes (new files too) are in this build
$name = "quark-$version-$commit"

# a build-win\ made before the checkout was moved points at the old path: CMake refuses it, so start again
$cache = 'build-win\CMakeCache.txt'
if ((Test-Path $cache) -and -not (Select-String -Path $cache -SimpleMatch "CMAKE_HOME_DIRECTORY:INTERNAL=$($Root -replace '\\', '/')" -Quiet)) { $Clean = $true }
if ($Clean -and (Test-Path build-win)) { Remove-Item -Recurse -Force build-win }
$testsFlag = if ($Tests -or $Editors) { 'ON' } else { 'OFF' }
cmake -S . -B build-win -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release "-DPython3_EXECUTABLE=$py" "-DQUARK_BUILD_TESTS=$testsFlag" "-DQUARK_VERSION_SUFFIX=$VersionSuffix"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
$targets = @('quark', 'quark_server', 'quark_launcher'); if ($Tests -or $Editors) { $targets += 'quark_tests' }; if ($Editors) { $targets += @('quark_maptool', 'copy_maps') }
cmake --build build-win -j $env:NUMBER_OF_PROCESSORS --target $targets
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

if ($Tests) {
  $env:TMPDIR = Join-Path $env:TEMP "quark_tests.$PID"; New-Item -ItemType Directory -Force $env:TMPDIR | Out-Null   # (HANDOVER: a private TMPDIR)
  & .\build-win\quark_tests.exe
  if ($LASTEXITCODE -ne 0) { throw "quark_tests failed (exit $LASTEXITCODE)" }
}

# the official maps' light, baked by this quark.exe as CI does (packages ship it zlib'd in maps\lightcache, so no first
# join waits for a bake). Kept raw in build-win\lightcache between builds: only maps that changed are baked again (all
# of them take ~25 s on 20 threads; none changed: under a second), then packed (~1.5 s). (piped: PowerShell waits for a
# windowed exe only then)
$bakeTime = Measure-Command { & .\build-win\quark.exe --bake-maps build-win\lightcache | Write-Host }
if ($LASTEXITCODE -ne 0) { throw "quark --bake-maps failed (exit $LASTEXITCODE)" }
Write-Host ("light bakes: {0:N1} s" -f $bakeTime.TotalSeconds)

# the package, laid out like CI's quark-windows.zip (exes, maps, mods, README). The launcher is this checkout's
# own quark_launcher; releases take it from the `launcher` branch instead (tools/launcher_build.sh), which only matters
# when testing the launcher itself.
$dest = Join-Path $OutDir $name
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item build-win\quark.exe, build-win\quark_server.exe, build-win\quark_launcher.exe $dest
Copy-Item -Recurse maps, mods $dest
& $py tools\pack_lightcache.py build-win\lightcache "$dest\maps\lightcache"   # (any other bake there is removed)
if ($LASTEXITCODE -ne 0) { throw 'tools\pack_lightcache.py failed' }
Copy-Item README.md $dest
Compress-Archive -Force -Path $dest -DestinationPath "$dest.zip"
$latest = Join-Path $OutDir 'latest'
if (Test-Path $latest) { Remove-Item -Recurse -Force $latest }
Copy-Item -Recurse $dest $latest
if ($Play) {   # (copied over the old files, never emptied first: settings and your own maps live in %APPDATA%\quark anyway)
  New-Item -ItemType Directory -Force $Play | Out-Null
  # (the shipped light bakes are replaced, not merged: older builds' bakes of changed maps would pile up)
  if (Test-Path "$Play\maps\lightcache") { Remove-Item -Recurse -Force "$Play\maps\lightcache" }
  robocopy $dest $Play /E /NFL /NDL /NJH /NJS /NP | Out-Null   # (merges into the existing folders; Copy-Item would nest maps\maps)
  if ($LASTEXITCODE -ge 8) { throw "copying into $Play failed (robocopy $LASTEXITCODE)" }
  Set-Content (Join-Path $Play 'BUILD.txt') "$name (local build, $(Get-Date -Format 'yyyy-MM-dd HH:mm'))"
}

Write-Host ""
Write-Host "Built $name"
Write-Host "  $dest\quark.exe"
Write-Host "  $dest.zip"
Write-Host "  $latest  (always the newest build)"
if ($Play) { Write-Host "  $Play  (the copy you play)" }
exit 0   # (robocopy leaves a nonzero code for success)
