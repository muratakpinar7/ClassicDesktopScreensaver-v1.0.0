# Native Windows 11 Test Plan

This plan is for the isolated portable package in `dist/Windows11-Test-Native-20261123`.
It does not install the application or modify Windows power settings. Use a normal,
non-elevated Windows account. The package contains its own default `settings.ini`;
do not copy an existing user's settings file into it.

## Before starting

1. Copy the entire `Windows11-Test-Native-20261123` folder to the Windows 11
   computer (for example, to the Desktop).
2. Close any other Classic Desktop Screensaver instance.
3. Run `ClassicDesktopScreensaver.exe` from the copied folder.
4. Confirm its Settings window shows the isolated test settings and that a second
   launch routes to the existing instance instead of starting another one.
5. Do not press Sleep or Hibernate shortcuts during Stage A. The test package is
   not the safe UI-test build; its ordinary configured actions are real.

## Stage A - no power transition

Mark each item Pass, Fail, or Not tested in a separate copy of this file.

- [ ] The app starts as a normal user and the tray icon appears.
- [ ] Settings opens, remains interactive, and can be closed and reopened.
- [ ] The tray menu opens once; Settings and Exit work; Exit restores the
      Windows screen-saver runtime state and closes the app.
- [ ] Relaunch works after Exit; the second-instance behavior is correct.
- [ ] Shortcut editors accept and save changes in this isolated folder. Inspect
      the shortcut values without invoking Sleep or Hibernate.
- [ ] The four Hot Corner selections are populated and can be saved.
- [ ] The selected/custom `.scr` test command launches the chosen saver; close
      the saver with its normal exit gesture.
- [ ] If testing the app's automatic saver, configure a short timeout in this
      isolated settings file and confirm it starts once after the idle threshold.
- [ ] If testing automatic Windows lock, save work first and confirm the account
      password is available; the test will lock the user's session.
- [ ] Full-screen and presentation guard behavior is checked without playing
      windowed video. Windowed-video detection is intentionally not supported.
- [ ] Change resolution/DPI using Windows display settings only if desired; keep
      the app open and confirm Settings and the tray menu remain accessible.
- [ ] Optional shell-recovery check: restart Windows Explorer from Task Manager
      and confirm the tray icon returns. Do not terminate the app itself.
- [ ] In a terminal, do not use a power shortcut as an A-stage test. The bare
      physical Pause pass-through is covered by automated tests; verify its live
      behavior later only if the Stage B risk is acceptable.

## Stage B - user-started power transitions

These are real system power actions. Start each manually and only when ready.
Do not use an automated test command, and do not allow an automatic fallback.

1. Before each transition, run `powercfg /a` and save its output. Do not enable
   Hibernate or change a power plan to make a test possible.
2. If the desired state is reported as available, use the app's Pause shortcut
   once for Sleep. Wait about 60-90 seconds, wake with the keyboard, then record
   the result. Do not repeat the request if the first one fails.
3. Test Shift+Pause separately only when S4 and the hibernation file are both
   reported available. Wait about 60-90 seconds after the hibernate transition,
   then wake and record the result. No Sleep or Shutdown fallback is expected.
4. Before and after each action, note the local time. After waking, inspect the
   app log and relevant Kernel-Power events; use SleepStudy when available to
   distinguish Screen Off from an actual Sleep interval and DRIPS residency.
5. Report the OS build, whether AC or battery was used, connected displays, the
   `powercfg /a` result, observed transition, and any event/report excerpts.

## Temporary Windows screen-saver behavior

In ordinary mode, the app temporarily disables the Windows screen-saver active
flag with `SystemParametersInfoW` and `fWinIni=0`; it does not write the user
profile. It normally restores the original runtime value when exiting and also
handles the Windows session-end notification. Password-protected or recognized
policy-managed screen-saver configurations are left alone. Close the app using
the tray Exit command. An abrupt crash or forced process termination can bypass
cleanup and may leave the active flag off for the current logon session; this
cannot be repaired by an in-process exit handler. If that happens, restore the
Windows setting manually or sign out/restart as appropriate.

## Result notes

- Windows 11 build:
- Display resolution / scale:
- AC or battery:
- Stage A results:
- `powercfg /a`:
- Stage B results (if performed):
- App log path:
- Follow-up observations:
