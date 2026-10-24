@echo off
rem Quark Model Editor: install or update from this checkout (non-interactive, safe to re-run).
rem   tools\model_editor\install.cmd [output folder]     (run from anywhere; default %USERPROFILE%\Desktop\Quark\model-editor)
rem Builds only quark_tests (MinGW, Release, in build-model-editor\), exports the game assets, rebuilds the page.
rem Needs git, cmake, MinGW-w64 g++ / mingw32-make and Python 3 on PATH (as tools\build_windows.ps1 -Setup installs).
setlocal
pushd "%~dp0..\.." || exit /b 1
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\Quark\model-editor"
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
for /f "delims=" %%p in ('%PY% -c "import sys;print(sys.executable)"') do set "PYEXE=%%p"
cmake -S . -B build-model-editor -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DPython3_EXECUTABLE=%PYEXE%" -DQUARK_BUILD_TESTS=ON >nul || goto fail
cmake --build build-model-editor -j %NUMBER_OF_PROCESSORS% --target quark_tests || goto fail
if exist build-model-editor\assets_export rmdir /s /q build-model-editor\assets_export
build-model-editor\quark_tests.exe exportassets build-model-editor\assets_export || goto fail
call "%~dp0build.cmd" build-model-editor\assets_export "%OUT%" || goto fail
popd
exit /b 0
:fail
echo model editor install failed
popd
exit /b 1
