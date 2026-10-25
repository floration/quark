@echo off
rem Quark Model Editor: builds model-editor.html + data\ from the game's asset export.
rem   build.cmd <export folder> [output folder]
rem The export: quark_tests exportassets <folder>  (or the export the director sends).
rem Output folder default: %USERPROFILE%\Desktop\Quark\model-editor
setlocal
if "%~1"=="" (
  echo usage: build.cmd ^<export folder^> [output folder]
  exit /b 1
)
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\model-editor"
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% "%~dp0build.py" "%~1" "%OUT%"
if errorlevel 1 exit /b 1
echo.
echo Open "%OUT%\model-editor.html" in Chrome or Edge.
endlocal
