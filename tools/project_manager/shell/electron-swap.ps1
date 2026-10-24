# Quark Project: puts a downloaded Electron in place once the app has exited (shell/README.md "Electron updates").
# pm\updater.py (builtin:electron-update) unpacks the latest stable Electron, sha256-checked, into
# Quark\project-manager\electron.next\. A running electron.exe cannot be replaced, so main.js starts this (hidden, no
# console) when it closes or restarts; this waits for that process to end, renames electron\ -> electron.old\ and
# electron.next\ -> electron\ (putting electron\ back if the second rename fails), deletes electron.old\, and with
# -Relaunch starts the app again. Electron's helper processes can hold files for a moment after the main one ends,
# so each rename is retried for up to 30 s; when it never works, the old Electron stays and the next close tries again.
# Log: <data>\logs\electron-swap.log. PowerShell 5.1 compatible, ASCII only.
# main.js passes everything in the environment (QUARK_SWAP_PID / _PM / _LOG / _RELAUNCH / _ARGS), so no path or argument
# has to survive quoting through cmd.exe and powershell -File; the parameters are for running it by hand.
param(
  [int]$ParentPid = [int]('0' + $env:QUARK_SWAP_PID),
  [string]$Pm = $env:QUARK_SWAP_PM,
  [string]$LogFile = $env:QUARK_SWAP_LOG,
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
Log ('waiting for pid ' + $ParentPid + ' to exit')
try { Wait-Process -Id $ParentPid -Timeout 120 -ErrorAction SilentlyContinue } catch { }
Start-Sleep -Milliseconds 300

$swapped = $false
if ((Test-Path (Join-Path $next 'electron.exe')) -and (Test-Path (Join-Path $next 'VERSION'))) {
  $ver = (Get-Content (Join-Path $next 'VERSION') -TotalCount 1).Trim()
  if (Test-Path $old) { Retry { Remove-Item -Recurse -Force $old } 'removing an old electron.old' | Out-Null }
  if (-not (Test-Path $old)) {
    if (Retry { Rename-Item -Path $cur -NewName 'electron.old' } 'renaming electron to electron.old') {
      if (Retry { Rename-Item -Path $next -NewName 'electron' } 'renaming electron.next to electron') {
        $swapped = $true
        Log ('Electron ' + $ver + ' is in place')
        Retry { Remove-Item -Recurse -Force $old } 'removing electron.old' | Out-Null
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
  $exe = Join-Path $cur 'electron.exe'
  if (Test-Path $exe) {
    Log ('starting ' + $exe + ' ' + $Arguments)
    if ($Arguments) { Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $Pm }
    else { Start-Process -FilePath $exe -WorkingDirectory $Pm }
  } else {
    Log ('no ' + $exe + ': not restarted')
  }
}
