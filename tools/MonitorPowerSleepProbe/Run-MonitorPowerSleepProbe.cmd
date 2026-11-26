@echo off
setlocal
title Monitor Power Sleep Probe

set "probeDll=%~dp0bin\Release\net10.0-windows\MonitorPowerSleepProbe.dll"

where dotnet.exe >nul 2>&1
if errorlevel 1 goto dotnet_missing
if not exist "%probeDll%" goto build_missing

where pwsh.exe >nul 2>&1
if not errorlevel 1 goto run_pwsh

where powershell.exe >nul 2>&1
if not errorlevel 1 goto run_windows_powershell
goto powershell_missing

:run_pwsh
pwsh.exe -NoLogo -NoProfile -NoExit -Command "& dotnet '%probeDll%' --request-monitor-off"
exit /b %ERRORLEVEL%

:run_windows_powershell
powershell.exe -NoLogo -NoProfile -NoExit -Command "& dotnet '%probeDll%' --request-monitor-off"
exit /b %ERRORLEVEL%

:dotnet_missing
echo .NET runtime was not found in PATH. The probe was not started.
exit /b 9009

:build_missing
echo The Release probe was not found. Build it from the repository before the test.
exit /b 2

:powershell_missing
echo PowerShell was not found. The probe was not started.
exit /b 9009
