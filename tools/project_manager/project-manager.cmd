@echo off
rem Quark Project is now Quarkology (2026-10-27): this starts Quark\quarkology\quarkology.cmd with the same arguments.
rem The old app's self-update put it in Quark\project-manager\ (tools/project_manager/, the hand-over, kept for one
rem release) so an old shortcut or habit still opens the app.
setlocal
set "NEW=%~dp0..\quarkology\quarkology.cmd"
if not exist "%NEW%" goto :missing
call "%NEW%" %*
exit /b %errorlevel%
:missing
echo Quarkology is not installed in %~dp0..\quarkology\ yet: run repository\tools\quarkology\setup.cmd
pause
exit /b 1
