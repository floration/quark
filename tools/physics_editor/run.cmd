@echo off
rem Quark Physics Editor: starts the local server (http://127.0.0.1:8772/) in this window and opens the browser.
rem Arguments go to serve.py (--port N, --maptool PATH, --data-dir DIR, --maps-dir DIR, --no-open, --install [FOLDER]).
rem The Quarkology app runs it with QUARK_SHELL=1 (no browser): it shows the page itself.
cd /d "%~dp0"
where py >NUL 2>NUL
if %errorlevel%==0 (
  py -3 serve.py %*
) else (
  python serve.py %*
)
