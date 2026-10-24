@echo off
rem Installs (or updates) the tests editor into the tests-editor folder beside this checkout (Quark\tests-editor).
rem   install.cmd [--target folder]      keeps the target's tests_editor.json and cache\
setlocal
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0install.py" %*
if errorlevel 1 (pause & exit /b 1)
endlocal
