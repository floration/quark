@echo off
rem Quark Map Editor: assembles the editor (page, server, quark_maptool, default maps, player models).
rem   build.cmd [output folder] [--build <cmake build folder>]
rem Build the game first (cmake --build build), so quark_maptool and the maps exist.
rem Output folder default: %USERPROFILE%\Desktop\Quark\map-editor
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\map-editor"
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0build.py" "%OUT%" %2 %3 %4
if errorlevel 1 exit /b 1
echo.
echo Run "%OUT%\run.cmd" to start the editor.
endlocal
