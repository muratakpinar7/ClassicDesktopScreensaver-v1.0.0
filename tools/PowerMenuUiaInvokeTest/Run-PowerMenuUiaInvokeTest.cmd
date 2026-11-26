@echo off
setlocal
title One-shot Power Menu UIA Invoke Test

where dotnet >nul 2>&1
if errorlevel 1 goto dotnet_missing

echo One-shot Power Menu UIA Invoke Test
echo This test can put the computer to Sleep exactly once.
echo Keep Start closed during the first five seconds.
echo After the prompt, open Start -^> Power and hover over Uyku / Sleep without clicking.
echo The test invokes only after the same verified MenuItem is stable for three samples.
echo Any ambiguity, failed validation, timeout, or missing InvokePattern means no power action.
echo.

dotnet run --project "%~dp0PowerMenuUiaInvokeTest.csproj" --configuration Release --no-launch-profile
set "probeExitCode=%ERRORLEVEL%"

echo.
echo Diagnostic report path is shown above and saved outside the project in TEMP.
echo No retry or fallback power action is performed.
pause >nul
exit /b %probeExitCode%

:dotnet_missing
echo .NET SDK was not found in PATH. The test was not started.
echo.
pause >nul
exit /b 9009
