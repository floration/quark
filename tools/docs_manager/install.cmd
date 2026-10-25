@echo off
rem Installs (or updates) the Claude Documents manager into %USERPROFILE%\Desktop\Quark\docs-manager.
rem   install.cmd [target folder]      keeps the target's mirror\ and cache\
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0install.py" %*
if errorlevel 1 (pause & exit /b 1)
pause
endlocal
