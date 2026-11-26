@echo off
setlocal
cd /d "%~dp0"
set "APP=%~dp0src\ClassicDesktopScreensaver.Native\bin\Portable\win-x64\ClassicDesktopScreensaver.exe"
if not exist "%APP%" (
  echo Native application not found. Run Build-Native.cmd first.
  exit /b 1
)
start "Classic Desktop Screensaver" "%APP%"
exit /b %errorlevel%
