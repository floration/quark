@echo off
rem Quark Gamemode Editor: starts the local server (http://127.0.0.1:8771/) with no console window and opens the browser.
rem Arguments go to serve.py (--port N, --maptool PATH, --quark-tests PATH, --data-dir DIR, --no-open).
rem The Supervisor Board runs it with QUARK_SHELL=1 (no browser): it shows the page itself.
rem pythonw has no console; without it, `start "" /b` keeps python in this (already open) window, never a new one.
cd /d "%~dp0"
where pyw >NUL 2>NUL
if %errorlevel%==0 (
  start "" pyw -3 serve.py %*
  goto :eof
)
where pythonw >NUL 2>NUL
if %errorlevel%==0 (
  start "" pythonw serve.py %*
  goto :eof
)
where py >NUL 2>NUL
if %errorlevel%==0 (
  start "" /b py -3 serve.py %*
) else (
  start "" /b python serve.py %*
)
