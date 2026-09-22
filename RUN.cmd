@echo off
setlocal
set "FMCW_PACKAGE=%~dp0build\package\Windows-CenterPoint"
if not exist "%FMCW_PACKAGE%\FMCW_LiDAR.exe" goto missing
pushd "%FMCW_PACKAGE%"
if errorlevel 1 exit /b 1
if /I "%~1"=="--smoke-test" (
  start "" /wait "%FMCW_PACKAGE%\FMCW_LiDAR.exe" %*
) else (
  start "" "%FMCW_PACKAGE%\FMCW_LiDAR.exe" %*
)
set "FMCW_RESULT=%ERRORLEVEL%"
popd
exit /b %FMCW_RESULT%
:missing
echo Windows package was not found:
echo "%FMCW_PACKAGE%\FMCW_LiDAR.exe"
echo See README.md and docs\workspaces_ko.md for build and packaging steps.
if "%~1"=="" pause
exit /b 1
