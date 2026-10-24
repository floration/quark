@echo off
rem Quark GitHub manager: copies the tool into the output folder and starts it from there with no window.
rem   install.cmd [output folder]      (default %USERPROFILE%\Desktop\Quark\github-manager)
rem What it did is written to <output folder>\install.log.
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\github-manager"
rem no trailing backslash: "C:\x\" would hide the closing quote from Python
if "%OUT:~-1%"=="\" set "OUT=%OUT:~0,-1%"
set "PY="
pyw -3 -c "import sys" >nul 2>nul && set "PY=pyw -3"
if not defined PY py -3 -c "import sys" >nul 2>nul && set "PY=py -3"
if not defined PY python -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 was not found. Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
  pause
  exit /b 1
)
start "" /b %PY% -B "%~dp0install.py" --dest "%OUT%"
exit /b 0
