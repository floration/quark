@echo off
rem Copies the Quark editor dashboard to a folder of its own (your marks there, provenance.json, are kept).
rem   install.cmd [folder]      default: %USERPROFILE%\Desktop\Quark\editor-dashboard
setlocal
set "DEST=%~1"
if "%DEST%"=="" set "DEST=%USERPROFILE%\Desktop\Quark\editor-dashboard"
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0dashboard.py" --install "%DEST%"
if errorlevel 1 exit /b 1
echo.
echo Double-click "%DEST%\dashboard.cmd" to start it.
endlocal
