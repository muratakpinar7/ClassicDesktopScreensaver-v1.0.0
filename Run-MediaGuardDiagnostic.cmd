@echo off
setlocal

set "probe=%~dp0tests\ClassicDesktopScreensaver.Native.Tests\bin\x64\Release\ClassicDesktopScreensaver.Native.Tests.exe"
set "report=%TEMP%\ClassicDesktopScreensaver-MediaGuardDiagnostic.txt"

if not exist "%probe%" (
    echo Diagnostic executable was not found:
    echo %probe%
    echo Build the native project first with Build-Native.cmd.
    pause
    exit /b 1
)

"%probe%" --media-query-diagnostic >"%report%" 2>&1
set "result=%ERRORLEVEL%"
type "%report%"
echo.
echo Report saved to: %report%
if not "%result%"=="0" echo Diagnostic did not complete successfully. Exit code: %result%
echo This diagnostic queries Windows media-session metadata and samples GPU VideoDecode counters for five seconds.
echo It does not play, stop, or control media, and it performs no power or settings action.
powershell.exe -NoLogo -NoProfile -NonInteractive -Command "try { [System.Media.SystemSounds]::Asterisk.Play(); Start-Sleep -Milliseconds 500 } catch {}" >nul 2>&1
pause
exit /b %result%
