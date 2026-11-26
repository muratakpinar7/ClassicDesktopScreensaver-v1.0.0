# Monitor Power Sleep Probe

This is an independent C# diagnostic for the observed `SC_MONITORPOWER` path. Its request logic is native C#; the optional CMD launcher opens a PowerShell console only to host the interactive prompt. The current computer's SleepStudy report repeatedly shows a `SC_MONITORPOWER` Screen Off state followed immediately by a Modern Standby Sleep state. This is evidence for this computer and configuration, not a Windows contract that a display-off request enters Sleep.

## Safety and commands

- No arguments or `--help`: display usage only.
- `--self-test`: validate local constants and pointer size only. It sends no native message and requests no power action.
- `--request-monitor-off`: report the current Windows identity, administrator role, process architecture, and session. If elevated, the probe refuses to continue. In an interactive, non-elevated console, a blank Enter sends exactly one `WM_SYSCOMMAND` / `SC_MONITORPOWER` request with `lParam=2` to `HWND_BROADCAST`. This asks Windows to turn off displays. On the tested system, its power policy may proceed into S0 Sleep. Run this only after explicit approval for that test.

The probe never calls `SetSuspendState`, a Hibernate API, UI Automation, or input simulation. It has no retry or fallback. It writes a short record to the current user's TEMP folder before sending the message, then appends the return status when the call completes. If it cannot write the pre-request record, it sends nothing.

## API and limits

`WM_SYSCOMMAND` (`0x0112`) and `SC_MONITORPOWER` (`0xF170`) are documented Windows messages. For `SC_MONITORPOWER`, `lParam=2` means the display is being shut off. This specifies a display state, not a system sleep state. Windows 10/11 can separately advance from Screen Off into Modern Standby Sleep as the OS carries out its power policy. That progression can depend on platform, power source, outstanding requests, media activity, connected displays, and system policy; the observed SleepStudy sessions do not establish a universal guarantee.

The P/Invoke uses pointer-sized `HWND`, `WPARAM`, and `LPARAM` values. `HWND_BROADCAST` is represented as `0xFFFF`. `SendMessageTimeoutW` uses `SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT` and a 100 ms per-window timeout. Windows documents that a broadcast can wait up to the timeout multiplied by the number of top-level windows, and that the return value does not identify individual windows that timed out. A successful API return therefore means neither that every window processed the message nor that the system entered Sleep. The API return and the window-procedure result (`lpdwResult`) are separate values. The post-call last-error value is recorded for diagnostics but is not interpreted as an API failure when the API return is nonzero. A zero API return is recorded and never retried.

The earlier PowerShell example declared `hWnd` as `Int32` and passed `-1` (`0xFFFFFFFF` as a 32-bit value). That is not the exact documented pointer-sized `HWND_BROADCAST` value `0xFFFF`. The old signature is not ABI-correct for a pointer-sized `HWND`; the SleepStudy reason records `SC_MONITORPOWER`, but does not reveal the numeric handle that USER32 received. The C# prototype uses the documented value `0x000000000000FFFF` in its 64-bit process. Its `SendMessageTimeoutW` behavior is also not identical to `SendMessage`: both synchronously send, but the latter waits for window procedures without a timeout, while the prototype applies the per-recipient timeout/flags and may not report individual broadcast recipients that time out.

## Build and safe check

From the repository root:

```powershell
dotnet build .\tools\MonitorPowerSleepProbe\MonitorPowerSleepProbe.csproj -c Release
dotnet run --project .\tools\MonitorPowerSleepProbe\MonitorPowerSleepProbe.csproj -c Release -- --self-test
```

Neither command sends a Windows message. The `--request-monitor-off` path is intentionally excluded from build/regression checks.

After separate approval for a real test, `Run-MonitorPowerSleepProbe.cmd` can be launched from Explorer. It opens a visible PowerShell window in the current user's token, does not invoke `runas`, and starts the already-built Release DLL. The probe prints `ŞİMDİ TESTE BAŞLAYABİLİRSİN.` as its last output line and waits; pressing Enter on the blank line sends the one request. Any other input cancels. Do not launch it before approval. After the display turns off, wake the computer with a key and provide the generated TEMP result path for analysis.
