@echo off
setlocal
set "APP=%~dp0src\ClassicDesktopScreensaver.Native\bin\Portable\win-x64\ClassicDesktopScreensaver.exe"
if not exist "%APP%" (
  echo Native portable EXE not found:
  echo "%APP%"
  pause
  exit /b 1
)
start "Classic Desktop Screensaver UI test" "%APP%" --ui-test-safe
exit /b %errorlevel%
