@echo off
rem Quark Project: starts the Supervisor Board server (127.0.0.1:8768) if it is not running, then the app window.
rem   project-manager.cmd          Electron (electron\electron.exe, put there by setup.ps1)
rem   project-manager.cmd --edge   Edge (else Chrome) as an --app window with its own profile in
rem                                %LOCALAPPDATA%\QuarkProject\edge-profile (no Electron needed)
rem Installed by tools\project_manager\setup.ps1 into Quark\project-manager\ (SPEC.md section 4).
rem The desktop shortcut does not use this file (no console window): it starts electron\electron.exe directly, or
rem without Electron runs project-manager-hidden.vbs (this file with --edge, hidden).
setlocal EnableExtensions
set "PM=%~dp0"
set "URL=http://127.0.0.1:8768/"
set "MODE=electron"
if /i "%~1"=="--edge" set "MODE=edge"
if "%MODE%"=="electron" if not exist "%PM%electron\electron.exe" set "MODE=noelectron"
if "%MODE%"=="noelectron" echo Electron is not installed in %PM%electron\ (run setup.cmd): using Edge instead.
if "%MODE%"=="noelectron" set "MODE=edge"

call :up
if not errorlevel 1 goto :open
rem Hidden: pyw has no console window; without it, a minimized py.
where pyw >nul 2>&1
if errorlevel 1 goto :start_py
start "" pyw -3 "%PM%server.py"
goto :started
:start_py
start "Quark PM server" /min py -3 "%PM%server.py"
:started
rem (wait up to about 10 s: the Edge window needs the page now, and Electron would otherwise start a second copy
rem  of the server if it looked before this one was listening)
call :wait

:open
if "%MODE%"=="edge" goto :edge
start "" "%PM%electron\electron.exe" "%PM%shell"
exit /b 0

:edge
set "BROWSER="
if exist "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" set "BROWSER=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not defined BROWSER if exist "%ProgramFiles%\Microsoft\Edge\Application\msedge.exe" set "BROWSER=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if not defined BROWSER if exist "%LocalAppData%\Microsoft\Edge\Application\msedge.exe" set "BROWSER=%LocalAppData%\Microsoft\Edge\Application\msedge.exe"
if not defined BROWSER if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" set "BROWSER=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if not defined BROWSER if exist "%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe" set "BROWSER=%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
if not defined BROWSER if exist "%LocalAppData%\Google\Chrome\Application\chrome.exe" set "BROWSER=%LocalAppData%\Google\Chrome\Application\chrome.exe"
if defined BROWSER goto :have_browser
echo Neither Edge nor Chrome was found. Open %URL% in a browser.
pause
exit /b 1
:have_browser
rem (the profile lives outside the Desktop, which may be OneDrive-synced)
set "PROFILE=%LOCALAPPDATA%\QuarkProject\edge-profile"
if not exist "%PROFILE%" mkdir "%PROFILE%" >nul 2>&1
start "" "%BROWSER%" --app=%URL% --user-data-dir="%PROFILE%" --no-first-run --no-default-browser-check
exit /b 0

:up
rem errorlevel 0 when the PM server answers /api/health (curl.exe ships with Windows 10 1803 and later)
where curl >nul 2>&1
if errorlevel 1 goto :up_ps
curl -s -f -m 1 -o nul "%URL%api/health" >nul 2>&1
exit /b %errorlevel%
:up_ps
powershell -NoProfile -Command "try { Invoke-WebRequest -UseBasicParsing -TimeoutSec 1 '%URL%api/health' | Out-Null; exit 0 } catch { exit 1 }" >nul 2>&1
exit /b %errorlevel%

:wait
set /a TRIES=0
:wait_loop
call :up
if not errorlevel 1 exit /b 0
set /a TRIES+=1
if %TRIES% geq 10 exit /b 1
rem (about one second)
ping -n 2 127.0.0.1 >nul 2>&1
goto :wait_loop
