@echo off
rem Installs or updates the Quark project manager into Quark\project-manager\ (double-click). SPEC.md section 4.
rem   setup.cmd [-Quark C:\Users\user\Desktop\Quark] [-NoElectron]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
if errorlevel 1 (echo. & echo SETUP FAILED) else (echo. & echo Quark Project is installed)
pause
