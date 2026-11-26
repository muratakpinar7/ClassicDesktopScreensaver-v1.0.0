@echo off
setlocal
title Power Menu UIA Probe

where dotnet >nul 2>&1
if errorlevel 1 goto dotnet_missing

echo Power Menu UIA Probe
echo This inspection is read-only. Do not click a power command.
echo The probe will inspect the live mouse position, keyboard focus, and related Control/Raw View ancestry for 30 seconds.
echo Keep Start closed for the first five seconds of baseline measurement.
echo Then open Start -^> Power, move the mouse over Sleep, and leave it there without clicking until the completion sound.
echo The probe does not move the mouse or invoke any UI element.
echo.

dotnet run --project "%~dp0PowerMenuUiaProbe.csproj" --configuration Release --no-launch-profile -- --duration-seconds 30 --sample-interval-ms 500
set "probeExitCode=%ERRORLEVEL%"

echo.
echo The report path is shown above. The report is saved outside the project in TEMP.
echo The monitor stops automatically. No power option was invoked.
pause >nul
exit /b %probeExitCode%

:dotnet_missing
echo .NET SDK was not found in PATH. The probe was not started.
echo.
pause >nul
exit /b 9009
