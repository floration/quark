@echo off
rem Quark GitHub manager: starts the read-only dashboard server at low priority and opens http://127.0.0.1:8767/
rem   github-manager.cmd [output folder]
rem Output folder default: %USERPROFILE%\Desktop\Quark\github-manager (github-manager.json, cache\, history.jsonl,
rem logs\, mirror.git). Needs Python 3, git and gh (signed in: gh auth login). Close its window to stop it.
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\github-manager"
rem no trailing backslash: "C:\x\" would hide the closing quote from Python
if "%OUT:~-1%"=="\" set "OUT=%OUT:~0,-1%"
set "PY="
py -3 -c "import sys" >nul 2>nul && set "PY=py -3"
if not defined PY python -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 was not found. Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
  pause
  exit /b 1
)
where git >nul 2>nul || echo Warning: git is not on PATH, so branches and local copies cannot be compared.
where gh >nul 2>nul || echo Warning: gh ^(GitHub CLI^) is not on PATH, so there is no GitHub data. https://cli.github.com/
if not exist "%OUT%" mkdir "%OUT%"
rem /low: the server and the git / gh it runs stay out of the game's way. It opens the page itself once it listens
rem (or, if it is already running, just opens the page).
start "Quark GitHub manager" /low /min %PY% -B "%~dp0server.py" --data "%OUT%" --open
endlocal
