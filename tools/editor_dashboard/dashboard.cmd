@echo off
rem Quark editor dashboard: starts it and opens it in the browser (README.md). Close this window to stop it.
rem Options pass through, e.g.  dashboard.cmd --quark D:\Quark  or  --repo repository
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0dashboard.py" %*
if errorlevel 1 pause
endlocal
