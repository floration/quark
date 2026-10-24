@echo off
rem Quark Claude Manager (Claude Documents): starts the local read-only server and opens http://127.0.0.1:8766/
rem If it is already running, this only opens the page. Close this window to stop the server.
setlocal
cd /d "%~dp0"
set "PORT=8766"
set "URL=http://127.0.0.1:%PORT%/"
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 was not found. Install it from https://www.python.org/ and run this again.
  pause
  exit /b 1
)
%PY% serve.py --ping --port %PORT% >nul 2>nul
if not errorlevel 1 (
  start "" "%URL%"
  exit /b 0
)
rem Run minimized: start a minimized copy of this script and leave.
if not "%~1"=="--here" (
  start "Quark docs manager" /min cmd /c ""%~f0" --here"
  exit /b 0
)
title Quark Claude Manager
echo Quark Claude Manager running at %URL%  (close this window to stop)
%PY% serve.py --docs "%USERPROFILE%\Desktop\Quark\Claude Documents" --port %PORT% --open --quiet
if errorlevel 1 pause
endlocal
