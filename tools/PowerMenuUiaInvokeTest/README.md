# One-shot Power Menu UIA Invoke Test

This is an independent, interactive-session diagnostic. It does not modify the main application, power plan, drivers, or Windows settings. It does not send mouse or keyboard input. It can call `InvokePattern.Invoke()` once, and only for a fully verified Windows Start Sleep menu item. That call can put the computer into Sleep.

## Run

Double-click `Run-PowerMenuUiaInvokeTest.cmd` from Explorer. Keep Start closed during the five-second baseline. When prompted, open Start -> Power and hold the mouse pointer over Uyku / Sleep without clicking. The tool samples the native window under the cursor, UIA `FromPoint`, UIA keyboard focus, and the Control/Raw ancestry for up to 60 seconds.

The tool requires one unique `Sleep` / `Uyku` item that is a `MenuItem` (50011), belongs to the shell window under the pointer, is visible and enabled, has the Start -> Opened -> Power options ancestry in both Control View and Raw View, belongs to the current interactive session, contains the cursor in its UIA bounding rectangle, and supports `InvokePattern`. The same UIA runtime element must pass three consecutive samples and a fresh pre-invocation check. Text labels, Hibernate, Shut down, Restart, multiple targets, or any incomplete/uncertain result cannot invoke a power command.

Immediately before the sole Invoke call, the tool writes and flushes a detailed report to the current user's TEMP folder, then atomically creates a one-time claim file. If the claim already exists, the tool exits without invoking. After Invoke returns, the report records that fact; the tool never retries or falls back to another power action. A timeout or failed validation also writes a report and performs no power action.

`--self-test` exercises the pure invocation policy without opening UI Automation or performing an action. `--help` prints usage information.
