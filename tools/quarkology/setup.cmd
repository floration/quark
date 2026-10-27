@echo off
rem Installs or updates the Supervisor Board (Quarkology) into Quark\quarkology\ (double-click). SPEC.md section 4.
rem   setup.cmd [-Quark C:\Users\user\Desktop\Quark] [-NoElectron]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
if errorlevel 1 (echo. & echo SETUP FAILED) else (echo. & echo Quarkology is installed)
pause
