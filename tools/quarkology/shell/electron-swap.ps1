# Quarkology: puts a downloaded Electron in place once the app has exited, and takes it back out when it does not
# run the app (shell/README.md "Electron updates").
# qo\updater.py (builtin:electron-update) unpacks the latest stable Electron, sha256-checked, into
# Quark\quarkology\electron.next\. A running electron.exe cannot be replaced, so main.js starts this (hidden, no
# console) when it closes or restarts; this waits for that process to end, renames electron\ -> electron.old\ and
# electron.next\ -> electron\ (putting electron\ back if the second rename fails) and with -Relaunch starts the app
# again. electron.old\ is kept: the new Electron is on probation until main.js has shown its window and run for a
# minute, then main.js deletes electron.old\. Two ways back:
#   - Mode rollback: main.js starts this when its window did not open in time (a new Electron major broke an API it
#     uses); after it exits, electron\ -> electron.bad\, electron.old\ -> electron\, and the app opens again.
#   - Mode swap with -Relaunch: the new Electron is watched for 40 s; if it exits in that time (a crash before main.js
#     could even run), the same rollback.
# A rolled-back version goes in electron.rejected, so the updater does not download it again (a newer one is tried).
# Electron's helper processes can hold files for a moment after the main one ends, so each rename is retried for up
# to 30 s; when it never works, the old Electron stays and the next close tries again.
# Log: <data>\logs\electron-swap.log. PowerShell 5.1 compatible, ASCII only.
# main.js passes everything in the environment (QUARK_SWAP_PID / _PM / _LOG / _MODE / _RELAUNCH / _ARGS), so no path or
# argument has to survive quoting through cmd.exe and powershell -File; the parameters are for running it by hand.
param(
  [int]$ParentPid = [int]('0' + $env:QUARK_SWAP_PID),
  [string]$Pm = $env:QUARK_SWAP_PM,
  [string]$LogFile = $env:QUARK_SWAP_LOG,
  [string]$Mode = $(if ($env:QUARK_SWAP_MODE) { $env:QUARK_SWAP_MODE } else { 'swap' }),
  [switch]$Relaunch = ($env:QUARK_SWAP_RELAUNCH -eq '1'),
  [string]$Arguments = $env:QUARK_SWAP_ARGS
)
if (-not $Pm -or -not (Test-Path $Pm)) { exit 1 }
$ErrorActionPreference = 'Stop'
function Log([string]$msg) {
  if (-not $LogFile) { return }
  try { Add-Content -Encoding ASCII -Path $LogFile -Value ('[' + (Get-Date).ToString('s') + '] ' + $msg) } catch { }
}
function Retry([scriptblock]$step, [string]$what) {
  for ($i = 0; $i -lt 60; $i++) {
    try { & $step; return $true } catch { Start-Sleep -Milliseconds 500 }
  }
  Log ($what + ' failed: ' + $Error[0].Exception.Message)
  return $false
}

$cur = Join-Path $Pm 'electron'
$next = Join-Path $Pm 'electron.next'
$old = Join-Path $Pm 'electron.old'
$bad = Join-Path $Pm 'electron.bad'

function Rollback([string]$why) {
  # electron\ (the new one) -> electron.bad\, electron.old\ -> electron\; its version -> electron.rejected
  Log ('rolling back: ' + $why)
  if (-not (Test-Path (Join-Path $old 'electron.exe'))) { Log 'no electron.old to go back to'; return $false }
  $badVer = ''
  try { $badVer = (Get-Content (Join-Path $cur 'VERSION') -TotalCount 1).Trim() } catch { }
  if (Test-Path $bad) { Retry { Remove-Item -Recurse -Force $bad } 'removing an old electron.bad' | Out-Null }
  if (-not (Retry { Rename-Item -Path $cur -NewName 'electron.bad' } 'renaming electron to electron.bad')) { return $false }
  if (-not (Retry { Rename-Item -Path $old -NewName 'electron' } 'renaming electron.old to electron')) {
    Retry { Rename-Item -Path $bad -NewName 'electron' } 'putting electron back' | Out-Null
    return $false
  }
  if ($badVer) {
    try { Set-Content -Encoding ASCII -Path (Join-Path $Pm 'electron.rejected') -Value $badVer } catch { }
  }
  Log ('back on the previous Electron; ' + $badVer + ' is not downloaded again (electron.rejected)')
  Retry { Remove-Item -Recurse -Force $bad } 'removing electron.bad' | Out-Null
  return $true
}
function StartApp {
  $exe = Join-Path $cur 'electron.exe'
  if (-not (Test-Path $exe)) { Log ('no ' + $exe + ': not restarted'); return $null }
  Log ('starting ' + $exe + ' ' + $Arguments)
  if ($Arguments) { return Start-Process -PassThru -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $Pm }
  return Start-Process -PassThru -FilePath $exe -WorkingDirectory $Pm
}

Log ('waiting for pid ' + $ParentPid + ' to exit (' + $Mode + ')')
try { Wait-Process -Id $ParentPid -Timeout 120 -ErrorAction SilentlyContinue } catch { }
Start-Sleep -Milliseconds 300

if ($Mode -eq 'rollback') {
  Rollback 'main.js reported that the new Electron did not open its window' | Out-Null
  StartApp | Out-Null
  exit 0
}

$swapped = $false
if ((Test-Path (Join-Path $next 'electron.exe')) -and (Test-Path (Join-Path $next 'VERSION'))) {
  $ver = (Get-Content (Join-Path $next 'VERSION') -TotalCount 1).Trim()
  # (an electron.old\ from a probation that never ended: the running Electron got this far, so it is the one to keep)
  if (Test-Path $old) { Retry { Remove-Item -Recurse -Force $old } 'removing an old electron.old' | Out-Null }
  if (-not (Test-Path $old)) {
    if (Retry { Rename-Item -Path $cur -NewName 'electron.old' } 'renaming electron to electron.old') {
      if (Retry { Rename-Item -Path $next -NewName 'electron' } 'renaming electron.next to electron') {
        $swapped = $true
        Log ('Electron ' + $ver + ' is in place (on probation: electron.old stays until it has run Quarkology for a minute)')
      } else {
        Retry { Rename-Item -Path $old -NewName 'electron' } 'putting electron back' | Out-Null
      }
    }
  }
} else {
  Log 'electron.next is not complete: nothing to swap'
}
if (-not $swapped) { Log 'the old Electron stays (the next close tries again)' }

if ($Relaunch) {
  $p = StartApp
  if ($swapped -and $p) {
    # (an exit in the first 40 s is a crash before main.js could run; after that main.js judges the probation itself,
    #  its window deadline being 60 s, so the two never both roll back)
    for ($i = 0; $i -lt 40; $i++) {
      Start-Sleep -Seconds 1
      if ($p.HasExited) {
        # (closed by q after its window opened: main.js marks that in electron.old\window-opened)
        if (Test-Path (Join-Path $old 'window-opened')) { break }
        if (Rollback ('Electron ' + $ver + ' exited (code ' + $p.ExitCode + ') right after it started')) { StartApp | Out-Null }
        break
      }
    }
  }
}
