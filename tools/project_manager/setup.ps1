# Installs (or updates) the Supervisor Board (the Quark project manager) into Quark\project-manager\ (tools/project_manager/SPEC.md section 4).
#   setup.cmd                       (double-click; runs this)
#   powershell -ExecutionPolicy Bypass -File setup.ps1 [-Quark C:\Users\user\Desktop\Quark] [-NoElectron]
# Copies server.py, pm\, web\, shell\, project-manager.cmd, project-manager-hidden.vbs, installs.json and quark.ico; writes tabs.json / config.json only when
# missing (q's own tabs, config, history and window state are never touched); downloads the official Electron zip
# (latest stable, verified against that release's SHASUMS256.txt; afterwards the app keeps it current by itself,
# installs.json builtin:electron-update) unless it is there or -NoElectron; makes the desktop
# shortcut "Quark Project" (electron.exe directly, or wscript + project-manager-hidden.vbs without Electron: no console
# window either way). Re-running it updates the code (and brings tabs.json's default tabs up to date, keeping
# tabs.json.old). PowerShell 5.1 compatible, ASCII only.
param(
  [string]$Quark = '',
  [switch]$NoElectron
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # (Invoke-WebRequest is many times slower with the progress bar on 5.1)

$Src = $PSScriptRoot                                 # repository\tools\project_manager
$RepoRoot = Split-Path -Parent (Split-Path -Parent $Src)
if (-not $Quark) {
  # repository\ sits in Quark\ (docs/LOCAL_BUILD.md); otherwise the usual Desktop\Quark
  $Quark = Split-Path -Parent $RepoRoot
  if (-not ((Test-Path (Join-Path $Quark 'repository')) -or (Test-Path (Join-Path $Quark 'working-repo')))) {
    $Quark = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Quark'
  }
}
if (-not (Test-Path $Quark)) { throw "The Quark folder $Quark does not exist (pass -Quark <folder>)" }
$Quark = (Resolve-Path $Quark).Path
$Dest = Join-Path $Quark 'project-manager'
New-Item -ItemType Directory -Force $Dest | Out-Null
Write-Host "Installing the Supervisor Board into $Dest"

# ---- code (replaced every time) --------------------------------------------------------------------------------
Copy-Item -Force (Join-Path $Src 'server.py') $Dest
Copy-Item -Force (Join-Path $Src 'project-manager.cmd') $Dest
Copy-Item -Force (Join-Path $Src 'project-manager-hidden.vbs') $Dest
# (the tools the app installs and updates by itself; repository\tools\project_manager\installs.json wins when present)
if (Test-Path (Join-Path $Src 'installs.json')) { Copy-Item -Force (Join-Path $Src 'installs.json') $Dest }
foreach ($dir in 'pm', 'web', 'shell') {
  $from = Join-Path $Src $dir
  $to = Join-Path $Dest $dir
  if (-not (Test-Path $from)) { Write-Warning "$dir\ is missing from $Src (not copied)"; continue }
  if (Test-Path $to) { Remove-Item -Recurse -Force $to }   # (so files deleted from the repo go too)
  Copy-Item -Recurse -Force $from $to
  Get-ChildItem -Path $to -Recurse -Directory -Filter '__pycache__' | Remove-Item -Recurse -Force
  Get-ChildItem -Path $to -Recurse -Directory -Filter 'node_modules' | Remove-Item -Recurse -Force
}
$ico = Join-Path $RepoRoot 'assets\quark.ico'
if (Test-Path $ico) { Copy-Item -Force $ico $Dest } else { Write-Warning "assets\quark.ico not found: the shortcut gets no icon" }

# ---- Python (the server needs it) -------------------------------------------------------------------------------
$py = $null
if (Get-Command py -ErrorAction SilentlyContinue) { $py = @('py', '-3') }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = @('python') }
if (-not $py) { throw 'Python 3 is not installed (winget install Python.Python.3.12), then run setup again' }
$pyArgs = @()
if ($py.Count -gt 1) { $pyArgs = $py[1..($py.Count - 1)] }

# ---- q's files: only when missing --------------------------------------------------------------------------------
$tabs = Join-Path $Dest 'tabs.json'
$defaultTabs = Join-Path $Src 'shell\tabs.default.json'
if (-not (Test-Path $tabs) -and (Test-Path $defaultTabs)) {
  Copy-Item $defaultTabs $tabs
  Write-Host "wrote $tabs (from shell\tabs.default.json)"
}
# (config.json, and tabs.json when shell\tabs.default.json is absent: server.py --init writes the defaults if missing)
& $py[0] @pyArgs (Join-Path $Dest 'server.py') --quark $Quark --init
if ($LASTEXITCODE -ne 0) { throw "server.py --init failed ($LASTEXITCODE)" }

# ---- Electron ----------------------------------------------------------------------------------------------------
$electronDir = Join-Path $Dest 'electron'
if ($NoElectron) {
  Write-Host 'Skipping Electron (-NoElectron): project-manager.cmd will use the Edge window'
} elseif (Test-Path (Join-Path $electronDir 'electron.exe')) {
  $have = ''
  if (Test-Path (Join-Path $electronDir 'VERSION')) { $have = (Get-Content (Join-Path $electronDir 'VERSION') -TotalCount 1).Trim() }
  Write-Host "Electron $have is already installed (the app updates it by itself: a newer one is staged in electron.next and swapped in when Quark Project closes)"
  # (leftovers of an interrupted Electron update)
  foreach ($left in 'electron.old', 'electron.next.tmp', 'electron.download') {
    $p = Join-Path $Dest $left
    if (Test-Path $p) { Remove-Item -Recurse -Force $p -ErrorAction SilentlyContinue }
  }
} else {
  [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
  $headers = @{ 'User-Agent' = 'quark-project-manager-setup'; 'Accept' = 'application/vnd.github+json' }
  $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/electron/electron/releases/latest' -Headers $headers
  $ver = ([string]$rel.tag_name).TrimStart('v')
  if ($ver -notmatch '^\d+\.\d+\.\d+$') { throw "Unexpected Electron release tag '$($rel.tag_name)'" }
  $zipName = "electron-v$ver-win32-x64.zip"
  $zipAsset = $rel.assets | Where-Object { $_.name -eq $zipName } | Select-Object -First 1
  $sumAsset = $rel.assets | Where-Object { $_.name -eq 'SHASUMS256.txt' } | Select-Object -First 1
  if (-not $zipAsset -or -not $sumAsset) { throw "Electron $ver has no $zipName or SHASUMS256.txt" }
  $tmp = Join-Path $env:TEMP "quark-electron-$PID"
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
  New-Item -ItemType Directory -Force $tmp | Out-Null
  try {
    $zip = Join-Path $tmp $zipName
    $sums = Join-Path $tmp 'SHASUMS256.txt'
    Write-Host "Downloading Electron $ver ($([math]::Round($zipAsset.size / 1MB)) MB)"
    Invoke-WebRequest -UseBasicParsing -Headers @{ 'User-Agent' = 'quark-project-manager-setup' } -Uri $zipAsset.browser_download_url -OutFile $zip
    Invoke-WebRequest -UseBasicParsing -Headers @{ 'User-Agent' = 'quark-project-manager-setup' } -Uri $sumAsset.browser_download_url -OutFile $sums
    $expected = $null
    foreach ($line in Get-Content $sums) {
      $parts = $line.Trim() -split '\s+', 2
      if ($parts.Count -eq 2 -and $parts[1].TrimStart('*') -eq $zipName -and $parts[0] -match '^[0-9a-fA-F]{64}$') { $expected = $parts[0]; break }
    }
    if (-not $expected) { throw "SHASUMS256.txt of Electron $ver does not list $zipName" }
    $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash
    if ($actual -ne $expected) { throw "Electron download is corrupt: sha256 $actual, expected $expected" }
    Write-Host "sha256 verified ($expected)"
    $unpacked = Join-Path $Dest 'electron.new'   # (same volume as the target: Move-Item of a folder cannot cross volumes)
    if (Test-Path $unpacked) { Remove-Item -Recurse -Force $unpacked }
    Expand-Archive -Force $zip $unpacked
    if (-not (Test-Path (Join-Path $unpacked 'electron.exe'))) { throw "$zipName has no electron.exe" }
    if (Test-Path $electronDir) { Remove-Item -Recurse -Force $electronDir }
    Move-Item $unpacked $electronDir
    Set-Content -Encoding ASCII (Join-Path $electronDir 'VERSION') $ver
    Write-Host "Electron $ver installed in $electronDir"
  } finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    $leftover = Join-Path $Dest 'electron.new'
    if (Test-Path $leftover) { Remove-Item -Recurse -Force $leftover }
  }
}

# ---- desktop shortcut ------------------------------------------------------------------------------------------
$desktop = [Environment]::GetFolderPath('Desktop')
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut((Join-Path $desktop 'Quark Project.lnk'))
$electronExe = Join-Path $electronDir 'electron.exe'
if ((-not $NoElectron) -and (Test-Path $electronExe)) {
  # no cmd at all: the shell starts (hidden) and keeps alive the Supervisor Board server and the managers itself
  $lnk.TargetPath = $electronExe
  $lnk.Arguments = '"' + (Join-Path $Dest 'shell') + '"'
} else {
  # no Electron: wscript runs project-manager.cmd --edge with window style 0 (no console window)
  $lnk.TargetPath = Join-Path $env:SystemRoot 'System32\wscript.exe'
  $lnk.Arguments = '"' + (Join-Path $Dest 'project-manager-hidden.vbs') + '"'
}
$lnk.WorkingDirectory = $Dest
$lnk.Description = 'Quark Project (Supervisor Board)'
$lnk.WindowStyle = 1   # (normal: the target is a GUI program, nothing shows a console)
if (Test-Path (Join-Path $Dest 'quark.ico')) { $lnk.IconLocation = (Join-Path $Dest 'quark.ico') + ',0' }
$lnk.Save()
Write-Host "Desktop shortcut: $(Join-Path $desktop 'Quark Project.lnk')"
Write-Host ''
try {
  Invoke-WebRequest -UseBasicParsing -TimeoutSec 1 'http://127.0.0.1:8768/api/health' | Out-Null
  Write-Host 'The Supervisor Board server is running the old code: close Quark Project (or end its py / pyw process) and start it again.'
} catch { }
Write-Host 'Done. Start it with the "Quark Project" shortcut (or project-manager.cmd --edge for the browser window).'
