@echo off
setlocal
title Start Power Menu UIA Automation Probe

where dotnet >nul 2>&1
if errorlevel 1 goto dotnet_missing

echo Start Power Menu UIA Automation Probe
echo This non-power-action discovery test opens Start and Power options.
echo It never calls InvokePattern.Invoke and does not request Sleep or Hibernate.
echo Close Start and its Power menu before continuing.
echo Do not use the keyboard or mouse until the report and completion sound appear.
echo.

dotnet run --project "%~dp0PowerMenuUiaAutomationProbe.csproj" --configuration Release --no-launch-profile
set "probeExitCode=%ERRORLEVEL%"

echo.
echo Diagnostic report path is shown above and saved outside the project in TEMP.
pause >nul
exit /b %probeExitCode%

:dotnet_missing
echo .NET SDK was not found in PATH. The probe was not started.
echo.
pause >nul
exit /b 9009
