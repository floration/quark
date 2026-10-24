@echo off
rem Quark Claude Manager (Claude Documents): starts the local server with no window and opens
rem http://127.0.0.1:8766/. If it is already running, this only opens the page. Its log: cache\serve.log.
rem To stop it: close it from the Quark app, or end "pythonw" in Task Manager.
setlocal
cd /d "%~dp0"
set "PORT=8766"
set "PYW="
where pyw >nul 2>nul && set "PYW=pyw -3"
if not defined PYW where pythonw >nul 2>nul && set "PYW=pythonw"
if not defined PYW (
  echo Python 3 was not found. Install it from https://www.python.org/ and run this again.
  pause
  exit /b 1
)
start "" %PYW% "%~dp0serve.py" --docs "%USERPROFILE%\Desktop\Quark\Claude Documents" --port %PORT% --open --quiet
endlocal
