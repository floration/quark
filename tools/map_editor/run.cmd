@echo off
rem Quark Map Editor: starts the local server (127.0.0.1 only, a free port) and opens the page in your browser.
rem Close this window (or Ctrl+C) to stop it.
setlocal
rem Python 3: the py launcher (python.org installs) first, else python on PATH.
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 is needed: install it from https://www.python.org/downloads/ and run this again.
  pause
  exit /b 1
)
cd /d "%~dp0"
rem (quoted: the folder may have spaces, e.g. C:\Users\user\Desktop\Quark\map-editor)
%PY% "%~dp0serve.py" %*
rem keep the window open on an error so it can be read
if errorlevel 1 pause
endlocal
