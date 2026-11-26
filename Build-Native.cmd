@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Native.ps1"
set "result=%errorlevel%"
exit /b %result%
