@echo off
rem Quark GitHub manager: starts the read-only dashboard server with no window, at low priority, and opens
rem http://127.0.0.1:8767/ (if it is already running, only opens the page).
rem   github-manager.cmd [output folder]
rem Output folder default: %USERPROFILE%\Desktop\Quark\github-manager (github-manager.json, cache\, history.jsonl,
rem logs\, mirror.git, server.log). Needs Python 3, git and gh (signed in: gh auth login). The server's own log is
rem server.log in the output folder. Port: 8767, or set GHM_PORT if github-manager.json says otherwise.
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\github-manager"
rem no trailing backslash: "C:\x\" would hide the closing quote from Python
if "%OUT:~-1%"=="\" set "OUT=%OUT:~0,-1%"
set "PORT=%GHM_PORT%"
if "%PORT%"=="" set "PORT=8767"
set "URL=http://127.0.0.1:%PORT%/"

rem already running: just open the page (curl.exe ships with Windows 10 and 11)
where curl.exe >nul 2>nul && curl.exe -s -f -o nul -m 2 "%URL%api/summary" && (
  start "" "%URL%"
  exit /b 0
)
if not exist "%OUT%" mkdir "%OUT%"
set "GHM_SERVER=%~dp0server.py"
set "GHM_OUT=%OUT%"

rem 1) a windowless Python, started directly: the real pythonw.exe next to the Python that py -3 / python run
rem    (first: pyw -3 keeps the server in the py launcher's job, so killing pyw.exe would kill the server too),
rem    then pythonw on PATH, then pyw -3. Its output goes to nul: it writes server.log itself.
set "PYW="
set "GHM_TMP=%TEMP%\quark-ghm-pythonw.txt"
set "GHM_FIND=import os,sys; sys.stdout.write(os.path.join(os.path.dirname(sys.executable), 'pythonw.exe'))"
set "PYW_EXE="
py -3 -c "%GHM_FIND%" > "%GHM_TMP%" 2>nul && if exist "%GHM_TMP%" set /p PYW_EXE=<"%GHM_TMP%"
if defined PYW_EXE if not exist "%PYW_EXE%" set "PYW_EXE="
if not defined PYW_EXE python -c "%GHM_FIND%" > "%GHM_TMP%" 2>nul && if exist "%GHM_TMP%" set /p PYW_EXE=<"%GHM_TMP%"
if exist "%GHM_TMP%" del "%GHM_TMP%" >nul 2>nul
if defined PYW_EXE if exist "%PYW_EXE%" set PYW="%PYW_EXE%"
if not defined PYW pythonw -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>nul && set "PYW=pythonw"
if not defined PYW pyw -3 -c "import sys" >nul 2>nul && set "PYW=pyw -3"
if defined PYW (
  start "" /b /low %PYW% -B "%GHM_SERVER%" --data "%GHM_OUT%" --open >nul 2>nul
  exit /b 0
)

rem 2) no pythonw: a console Python started hidden through PowerShell
set "GHM_PY="
py -3 -c "import sys" >nul 2>nul && set "GHM_PY=py" && set "GHM_PYARG=-3"
if not defined GHM_PY python -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>nul && set "GHM_PY=python" && set "GHM_PYARG=-B"
if not defined GHM_PY (
  echo Python 3 was not found. Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "$p = Start-Process -PassThru -WindowStyle Hidden -FilePath $env:GHM_PY -ArgumentList @($env:GHM_PYARG, '-B', ('\"' + $env:GHM_SERVER + '\"'), '--data', ('\"' + $env:GHM_OUT + '\"'), '--open'); try { $p.PriorityClass = 'Idle' } catch {}"
exit /b 0
