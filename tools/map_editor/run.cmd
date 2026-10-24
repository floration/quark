@echo off
rem Quark Map Editor: starts the local server (127.0.0.1 only, a free port) and opens the page in your browser.
rem Close this window (or Ctrl+C) to stop it.
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
cd /d "%~dp0"
%PY% "%~dp0serve.py" %*
endlocal
