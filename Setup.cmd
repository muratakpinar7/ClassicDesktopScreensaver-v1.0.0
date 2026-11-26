@echo off
if /I not "%~1"=="--conhost" (
    if exist "%SystemRoot%\System32\conhost.exe" (
        start "" "%SystemRoot%\System32\conhost.exe" "%ComSpec%" /d /c ""%~f0" --conhost"
        exit /b 0
    )
)
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion
title Classic Desktop Screensaver Setup
if exist "%~dp0SetupConsole.ps1" powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupConsole.ps1" >nul 2>&1
set "EXENAME=ClassicDesktopScreensaver.exe"
set "RELEASE_VERSION=2.0.0"
if exist "%~dp0ReleaseVersion.txt" (
    for /f "usebackq delims=" %%V in ("%~dp0ReleaseVersion.txt") do set "RELEASE_VERSION=%%V"
)
set "INSTALLDIR=%APPDATA%\Classic Desktop Screensaver"
set "LEGACYINSTALLDIR=%APPDATA%\ClassicDesktopScreensaver"
set "TARGET=%INSTALLDIR%\%EXENAME%"
set "SETTINGS=%INSTALLDIR%\settings.ini"
set "STARTMENULINK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Classic Desktop Screensaver.lnk"
set "STARTUPLINK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\Classic Desktop Screensaver.lnk"
set "UILANG=en"
for /f "delims=" %%L in ('powershell.exe -NoProfile -Command "[Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName" 2^>nul') do set "UILANG=%%L"
if /I "%UILANG%"=="tr" goto menu_tr
goto menu_en

:menu_tr
cls
echo Classic Desktop Screensaver v%RELEASE_VERSION% - Kurulum
echo.
echo Bu program yalnizca mevcut Windows kullanicisi icin kurulur.
echo Kurulum klasoru: %INSTALLDIR%
echo Yonetici izni, servis veya surucu gerekmez.
echo Test edilen surumler: Windows 10 22H2 ve Windows 11 26H2.
echo Daha eski Windows surumleri test edilmemistir.
echo.
echo [1] Install - Yalnizca yukle
echo [2] Install and Run - Yukle ve Ayarlari ac
echo [3] Uninstall - Kaldir
echo [4] Exit - Cikis
choice /C 1234 /N /M "Secimin: "
if errorlevel 4 exit /b 0
if errorlevel 3 goto uninstall_tr
if errorlevel 2 (
    set "RUN_AFTER_INSTALL=1"
    goto install_tr
)
set "RUN_AFTER_INSTALL=0"
goto install_tr

:install_tr
cls
echo === Install ===
if not exist "%~dp0%EXENAME%" goto missing_files_tr
if not exist "%~dp0settings.ini" goto missing_files_tr
echo Hedef: %INSTALLDIR%
if "!RUN_AFTER_INSTALL!"=="1" echo Kurulumdan sonra Ayarlar penceresi acilacak.
if "!RUN_AFTER_INSTALL!"=="0" echo Program otomatik calistirilmayacak.
echo Var olan settings.ini korunacak.
choice /C YN /N /M "Devam edilsin mi? [Y/N]: "
if errorlevel 2 goto menu_tr
call :do_install
if errorlevel 1 goto install_failed_tr
echo Program basariyla kuruldu.
if "!RUN_AFTER_INSTALL!"=="1" echo Ayarlar penceresi acildi.
if "!RUN_AFTER_INSTALL!"=="0" echo Program calistirilmadi.
timeout /t 3 /nobreak >nul
exit /b 0

:missing_files_tr
echo HATA: EXE veya settings.ini Setup.cmd ile ayni klasorde bulunamadi.
pause
exit /b 1

:install_failed_tr
echo HATA: Kurulum tamamlanamadi. Program aciksa tepsi menusunden Cikis'i secip yeniden dene.
pause
exit /b 1

:uninstall_tr
cls
echo Program mevcut Windows kullanicisi icin kaldirilacak.
echo Kurulum klasorundeki settings.ini dosyasi da silinecek.
echo Saklamak istedigin ayarlari once yedekle.
choice /C YN /N /M "Devam edilsin mi? [Y/N]: "
if errorlevel 2 goto menu_tr
call :do_uninstall
if errorlevel 1 goto uninstall_failed_tr
echo Program ve olusturulan kisayollar kaldirildi.
timeout /t 3 /nobreak >nul
exit /b 0

:uninstall_failed_tr
echo Guvenli kaldirma tamamlanamadi. Uygulama kapanmadiysa tepsi menusunden Cikis'i sec.
pause
exit /b 1

:menu_en
cls
echo Classic Desktop Screensaver v%RELEASE_VERSION% - Setup
echo.
echo This program is installed only for the current Windows user.
echo Install folder: %INSTALLDIR%
echo Administrator rights, services, and drivers are not required.
echo Tested on Windows 10 22H2 and Windows 11 26H2.
echo Earlier Windows versions have not been tested.
echo.
echo [1] Install - Install only
echo [2] Install and Run - Install and open Settings
echo [3] Uninstall
echo [4] Exit
choice /C 1234 /N /M "Select an option: "
if errorlevel 4 exit /b 0
if errorlevel 3 goto uninstall_en
if errorlevel 2 (
    set "RUN_AFTER_INSTALL=1"
    goto install_en
)
set "RUN_AFTER_INSTALL=0"
goto install_en

:install_en
cls
echo === Install ===
if not exist "%~dp0%EXENAME%" goto missing_files_en
if not exist "%~dp0settings.ini" goto missing_files_en
echo Destination: %INSTALLDIR%
if "!RUN_AFTER_INSTALL!"=="1" echo The Settings window will open after installation.
if "!RUN_AFTER_INSTALL!"=="0" echo The app will not start after installation.
echo An existing settings.ini will be preserved.
choice /C YN /N /M "Continue? [Y/N]: "
if errorlevel 2 goto menu_en
call :do_install
if errorlevel 1 goto install_failed_en
echo The program was installed successfully.
if "!RUN_AFTER_INSTALL!"=="1" echo The Settings window was opened.
if "!RUN_AFTER_INSTALL!"=="0" echo The app was not started.
timeout /t 3 /nobreak >nul
exit /b 0

:missing_files_en
echo ERROR: The EXE or settings.ini was not found next to Setup.cmd.
pause
exit /b 1

:install_failed_en
echo ERROR: Installation could not be completed. If the app is open, choose Exit from its tray menu and retry.
pause
exit /b 1

:uninstall_en
cls
echo The program will be removed for the current Windows user.
echo The settings.ini in the installation folder will also be deleted.
echo Back up your settings first if you want to keep them.
choice /C YN /N /M "Continue? [Y/N]: "
if errorlevel 2 goto menu_en
call :do_uninstall
if errorlevel 1 goto uninstall_failed_en
echo The program and its created shortcuts were removed.
timeout /t 3 /nobreak >nul
exit /b 0

:uninstall_failed_en
echo Safe uninstall could not be completed. If the app did not close, choose Exit from its tray menu.
pause
exit /b 1

:do_install
if not exist "%~dp0%EXENAME%" exit /b 1
if not exist "%~dp0settings.ini" exit /b 1
call :request_app_exit
if errorlevel 1 exit /b 1
if not exist "%INSTALLDIR%" mkdir "%INSTALLDIR%" >nul 2>&1
if not exist "%INSTALLDIR%" exit /b 1
copy /Y "%~dp0%EXENAME%" "%TARGET%" >nul
if errorlevel 1 exit /b 1
if not exist "%SETTINGS%" (
    copy /Y "%~dp0settings.ini" "%SETTINGS%" >nul
    if errorlevel 1 exit /b 1
)
for %%D in ("%STARTMENULINK%") do if not exist "%%~dpD" mkdir "%%~dpD" >nul 2>&1
set "CDS_TARGET=%TARGET%"
set "CDS_LINK=%STARTMENULINK%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut($env:CDS_LINK); $s.TargetPath = $env:CDS_TARGET; $s.Arguments = '--settings'; $s.WorkingDirectory = Split-Path $env:CDS_TARGET; $s.IconLocation = $env:CDS_TARGET + ',0'; $s.Description = 'Classic Desktop Screensaver'; $s.Save()"
if errorlevel 1 exit /b 1
if "!RUN_AFTER_INSTALL!"=="1" start "" "%TARGET%" --settings
exit /b 0

:do_uninstall
call :request_app_exit
if errorlevel 1 exit /b 1
if exist "%STARTMENULINK%" del /F /Q "%STARTMENULINK%" >nul 2>&1
if exist "%STARTMENULINK%" exit /b 1
if exist "%STARTUPLINK%" del /F /Q "%STARTUPLINK%" >nul 2>&1
if exist "%STARTUPLINK%" exit /b 1
if exist "%INSTALLDIR%" rmdir /S /Q "%INSTALLDIR%" >nul 2>&1
if exist "%INSTALLDIR%" exit /b 1
if exist "%LEGACYINSTALLDIR%" rmdir /S /Q "%LEGACYINSTALLDIR%" >nul 2>&1
if exist "%LEGACYINSTALLDIR%" exit /b 1
exit /b 0

:request_app_exit
if not exist "%~dp0%EXENAME%" exit /b 0
start "" /wait "%~dp0%EXENAME%" --exit >nul 2>&1
powershell.exe -NoLogo -NoProfile -Command "$sid=[Diagnostics.Process]::GetCurrentProcess().SessionId; $until=[DateTime]::UtcNow.AddSeconds(6); do { $p=@(Get-Process -Name 'ClassicDesktopScreensaver' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid }); if($p.Count -eq 0){exit 0}; Start-Sleep -Milliseconds 250 } while([DateTime]::UtcNow -lt $until); exit 1" >nul 2>&1
exit /b %ERRORLEVEL%
