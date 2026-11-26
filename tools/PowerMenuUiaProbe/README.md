# Power Menu UI Automation Probe

This standalone diagnostic inspects the interactive Windows desktop through Microsoft UI Automation. It samples the desktop's Control View and Raw View for 30 seconds, targeting visible Start and shell roots. The default interval is 500 ms; each actual sample time and scan duration is recorded.

It is inspection-only. It does not open menus, move the mouse, send keyboard or mouse input, click controls, call Invoke, Expand, or Collapse, or request Sleep, Hibernate, shutdown, or restart. It requires an interactive user session and does not request elevation.

## Use

Double-click Run-PowerMenuUiaProbe.cmd in Explorer. Keep Start closed during the initial five-second baseline. When the probe prints that the baseline is complete, open Start -> Power, move the mouse over Sleep, and keep it there without clicking until the completion sound. You do not need to return to the CMD window. Do not select a power command.

The probe stops automatically after approximately 30 seconds and attempts one Windows Asterisk system sound after saving the report. It writes a uniquely named report to the current user's TEMP folder, outside the repository. The CMD window displays the report path and remains open.

For each sample, the report compares GetForegroundWindow and EnumWindows results for StartMenuExperienceHost, ShellExperienceHost, and selected Explorer desktop/taskbar window classes. It records ProcessName, PID, WindowClass, HWND, visibility, and foreground status, plus identities and discovery sources of the UIA roots visited in Control View and Raw View. If a target shell window is foreground but absent from EnumWindows, the probe separately tries that HWND as a read-only UIA root and labels the fallback. While a Start host is foreground, it also inventories visible top-level windows whose root owner is that Start window and includes those popup roots in the UIA scan. The popup inventory writes no titles. The first five seconds are the pre-open baseline; later samples are labeled as after the open prompt. The report does not assume the menu opened: window visibility and foreground changes provide that evidence. Window titles are never recorded, and non-target foreground process identity is redacted.

Every sample reads the live cursor position with `GetCursorPos`, obtains the element at that point with `AutomationElement.FromPoint`, and separately queries `AutomationElement.FocusedElement`. It records related ancestors in Control View and Raw View. `WindowFromPoint` identifies the native root under the cursor, so a popup hosted outside the main Start window can be reported without scanning all desktop descendants. Every fourth second, exact `Sleep`/`Uyku` names are searched under that single window's UIA root; a bounded Raw View walk checks at most 300 nodes and depth 10. If an Opened or Power-options ancestor is present, an additional exact-name search is limited to that subtree and capped at 25 matches.

The report separates candidate discovery from validation. Every exact `Sleep`/`Uyku` candidate is recorded even when its hierarchy or session does not satisfy the verification rules. A target is verified only when it is a `MenuItem` (ControlType ID 50011), its ancestry contains Start, Opened/Açılan, and Power options/Aç/Kapa seçenekleri, and its provider process belongs to the same interactive session as the probe. The report records the candidate's AutomationId, ProcessId, UIA root, Control/Raw parent chain, visibility, enabled state, session IDs, and supported patterns. Pattern support is read only; no pattern is invoked. `Kapat` and `Hazırda Beklet` can never be marked as the Sleep target. Unrelated window titles and UI names remain excluded; access errors include operation and exception/Win32 error type without raw exception text.

Explorer file-list contents, window titles, and unrelated foreground process identity are not written to the report. Access errors are summarized by operation and type, without exception text that might contain unrelated UI details.

AutomationId, tree structure, and control patterns are runtime observations, not stable contracts. Pattern support is queried only; the probe never calls InvokePattern.Invoke().

## Build without running

    dotnet build .\tools\PowerMenuUiaProbe\PowerMenuUiaProbe.csproj --configuration Release
