# Start Power Menu UIA Automation Probe

This independent first-stage prototype runs in the interactive Windows session. It opens Start with a single Windows-key press, discovers the current Start `PowerButton` through dynamically enumerated shell windows, focuses that verified button, and sends one Space keystroke to open its Power options. It then reads the UI Automation tree until it finds the real Sleep item.

The probe does **not** call `InvokePattern.Invoke()` on any control or execute any UIA control-pattern action. It only reads pattern availability. In particular, it never activates Sleep, Hibernate, Shut down, or Restart. It writes its report to the current user's TEMP folder and leaves the Start/Power menu open. A timeout, multiple candidate, session mismatch, missing pattern, or incomplete hierarchy stops the probe without a power action.

## Run

Close Start and the Power menu. Double-click `Run-PowerMenuUiaAutomationProbe.cmd` in Explorer and do not use the keyboard or mouse until the report appears and the completion sound plays. The probe requires its console to be foreground before it sends the Windows key. The run has bounded Start-menu, Power-button, submenu, and global timeouts.

PowerButton activation requires the observed `AutomationId=PowerButton`, a unique visible enabled Button, complete Control/Raw ancestry, and the current Start shell process/session. Localized exact names (English/Turkish) are collected for diagnostics, never used alone to activate a button. The probe focuses that exact element, verifies its runtime ID is the focused element, then sends one Space keystroke to open Power options. Sleep matching accepts only exact `Sleep` / `Uyku` names and verifies `MenuItem` (50011), session, enabled/visible state, ordered Start → Opened → Power options ancestry in Control and Raw View, and `InvokePattern` support. It only reads Invoke support.

`AutomationId` is useful for language-independent matching when provided, but Microsoft notes that providers are not required to expose it and that values are not guaranteed stable across application releases/builds. Therefore Windows 11 or a future Windows 10 build must be verified on the target machine; an unrecognized tree fails closed.

UIA searches are scoped to dynamically enumerated visible shell windows rather than the entire desktop subtree. Microsoft warns that a desktop-wide descendant search can be expensive and recommends a separate thread when a client may interact with its own UI. See [FindAll](https://learn.microsoft.com/en-us/dotnet/api/system.windows.automation.automationelement.findall?view=windowsdesktop-10.0), [UI Automation threading](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading), and [AutomationId guidance](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.automation.automationproperties.automationid?view=winrt-28000).

`--self-test` runs local policy checks only. It sends no input and performs no UI Automation or power action.
