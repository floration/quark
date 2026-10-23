@echo off
rem Builds the code in working-repo\ into working\ (the copy of the game you test before pushing). docs/LOCAL_BUILD.md
rem Copy this file into the folder that holds working-repo\ and working\, then double-click it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0working-repo\tools\build_windows.ps1" -Play "%~dp0working" %*
if errorlevel 1 (echo. & echo BUILD FAILED) else (echo. & echo working\ is up to date)
pause
