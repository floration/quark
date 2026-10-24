@echo off
rem Quark tests editor: starts the local server with no window and opens http://127.0.0.1:8775/ (README.md).
rem If it is already running, this only opens the page. With QUARK_SHELL=1 (the Quark app) it opens no browser.
rem Its log: cache\serve.log. To stop it: close it from the Quark app, or end "pythonw" in Task Manager.
setlocal
cd /d "%~dp0"
set "PYW="
where pyw >nul 2>nul && set "PYW=pyw -3"
if not defined PYW where pythonw >nul 2>nul && set "PYW=pythonw"
if not defined PYW (
  echo Python 3 was not found. Install it from https://www.python.org/ and run this again.
  pause
  exit /b 1
)
set "ARGS=--quiet"
if "%QUARK_SHELL%"=="1" set "ARGS=--quiet --no-browser"
start "" %PYW% "%~dp0serve.py" %ARGS% %*
endlocal
