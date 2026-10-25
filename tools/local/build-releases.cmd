@echo off
rem Compiles the current production release (newest v tag) into production\ and the tip of the testing branch into
rem testing\, from repository\. -Force rebuilds even when a folder is already current. docs/LOCAL_BUILD.md
rem Copy this file into the folder that holds repository\, testing\ and production\, then double-click it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0repository\tools\build_release.ps1" %*
if errorlevel 1 (echo. & echo BUILD FAILED) else (echo. & echo testing\ and production\ are up to date)
pause
