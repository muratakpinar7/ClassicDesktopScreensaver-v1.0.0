using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Automation;

namespace PowerMenuUiaAutomationProbe;

internal static class Program
{
    private const int ButtonControlTypeId = 50000;
    private const int MenuItemControlTypeId = 50011;
    private const ushort VirtualKeyLeftWindows = 0x5B;
    private const ushort VirtualKeySpace = 0x20;
    private const uint InputKeyboard = 1;
    private const uint KeyEventKeyUp = 0x0002;
    private const int PollIntervalMilliseconds = 250;
    private const int StableSamplesRequired = 3;
    private const int MaximumCandidatesPerWindow = 50;
    private const int MaximumAncestorDepth = 16;
    private const int GlobalTimeoutMilliseconds = 40000;
    private static readonly TimeSpan StartMenuTimeout = TimeSpan.FromSeconds(8);
    private static readonly TimeSpan PowerButtonTimeout = TimeSpan.FromSeconds(8);
    private static readonly TimeSpan PowerMenuTimeout = TimeSpan.FromSeconds(12);
    private static readonly HashSet<string> StartHostProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "StartMenuExperienceHost",
        "ShellExperienceHost"
    };
    private static readonly HashSet<string> ShellProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "StartMenuExperienceHost",
        "ShellExperienceHost",
        "explorer"
    };
    private static readonly string[] PowerButtonNames = ["Power", "Power button", "Güç", "Güç düğmesi", "Aç/Kapa"];
    private static readonly string[] SleepNames = ["Sleep", "Uyku"];
    private static readonly string[] StartNames = ["Start", "Start menu", "Başlat", "Başlat menüsü", "Başlangıç", "Başlangıç menüsü"];
    private static readonly string[] OpenNames = ["Open", "Opened", "Açılan", "Expanded", "Expanded menu"];
    private static readonly string[] PowerMenuNames = [
        "Power options", "Power options menu", "Power menu options", "Aç/Kapa seçenekleri",
        "Güç seçenekleri", "Aç/Kapa menüsü", "Güç menüsü"
    ];

    private delegate bool EnumWindowsCallback(IntPtr windowHandle, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr GetConsoleWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetWindowThreadProcessId(IntPtr windowHandle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr windowHandle, StringBuilder className, int maxCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr windowHandle);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint inputCount, [In] Input[] inputs, int inputSize);

    public static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        if (args.Contains("--help", StringComparer.OrdinalIgnoreCase))
        {
            Console.WriteLine("PowerMenuUiaAutomationProbe [--help | --self-test]");
            Console.WriteLine("Default run: open Start and Power options, then read-only Sleep discovery.");
            Console.WriteLine("No InvokePattern.Invoke call or power transition is performed.");
            return 0;
        }
        if (args.Contains("--self-test", StringComparer.OrdinalIgnoreCase))
            return RunSelfTests();

        return RunInteractive();
    }

    private static int RunInteractive()
    {
        using Process process = Process.GetCurrentProcess();
        if (process.SessionId == 0)
        {
            Console.Error.WriteLine("Araç etkileşimli Windows kullanıcı oturumunda çalıştırılmalıdır. İşlem başlatılmadı.");
            return 2;
        }

        string reportPath = CreateReportPath();
        var state = new ProbeState(reportPath, process.SessionId, process.Id, DateTimeOffset.Now);
        Console.WriteLine("Start Power Menu UIA Automation Probe");
        Console.WriteLine("Bu aşama yalnız menüyü açar ve Uyku öğesini okur; hiçbir InvokePattern çağrısı ve güç eylemi yoktur.");
        Console.WriteLine("Başlat menüsü ve Güç menüsü kapalı olmalı. CMD penceresi önde kalmalı.");
        Console.WriteLine($"Rapor: {reportPath}");

        var worker = new Thread(() => RunAutomation(state, GetConsoleWindow()))
        {
            IsBackground = true,
            Name = "Power menu UI Automation worker"
        };
        worker.SetApartmentState(ApartmentState.MTA);
        worker.Start();

        if (!worker.Join(GlobalTimeoutMilliseconds))
        {
            state.Cancel();
            state.Add("Global watchdog expired. The background UIA worker is abandoned; no Sleep Invoke exists in this prototype.");
            state.SetOutcome("Aborted: global UI Automation timeout.", 20);
        }

        state.WriteReport();
        Console.WriteLine($"Sonuç: {state.Outcome}");
        Console.WriteLine($"Uyku hedefi doğrulandı: {state.SleepTargetVerified}");
        Console.WriteLine($"InvokePattern.Invoke çağrısı: yapılmadı");
        Console.WriteLine($"Rapor: {reportPath}");
        TrySoundAlert();
        return state.ExitCode;
    }

    private static void RunAutomation(ProbeState state, IntPtr consoleWindow)
    {
        try
        {
            if (consoleWindow == IntPtr.Zero || GetForegroundWindow() != consoleWindow)
            {
                state.SetOutcome("Aborted: the probe console is not the foreground window; no input was sent.", 3);
                return;
            }

            List<NativeWindowInfo> initialWindows = EnumerateShellWindows(state.InteractiveSessionId, state);
            List<PowerButtonCandidate> initiallyVisibleButtons = FindPowerButtons(initialWindows, state);
            List<SleepCandidate> initiallyVisibleSleepItems = FindSleepItems(initialWindows, state);
            if (initiallyVisibleButtons.Any(candidate => candidate.IsPowerButtonCandidate) ||
                initiallyVisibleSleepItems.Any(candidate => candidate.IsSafe))
            {
                state.Add("Initial UIA inspection found a visible Start PowerButton or verified Sleep item; refusing to toggle an already-open menu.");
                state.SetOutcome("Aborted: Start or Power menu was already open. Close it and run again.", 4);
                return;
            }

            state.Add("Initial scan found no visible verified PowerButton or Sleep item.");
            if (GetForegroundWindow() != consoleWindow)
            {
                state.SetOutcome("Aborted: foreground changed before Start-key input; no input was sent.", 5);
                return;
            }

            state.Add("About to send one Left Windows key stroke to the foreground console to open Start.");
            state.WriteReport();
            if (!SendKeyStroke(VirtualKeyLeftWindows, state))
            {
                state.SetOutcome("Aborted: the Start key stroke was not fully accepted.", 6);
                return;
            }
            state.Add("Start key stroke sent once.");

            Stopwatch startTimer = Stopwatch.StartNew();
            List<NativeWindowInfo> startWindows = [];
            while (startTimer.Elapsed < StartMenuTimeout)
            {
                startWindows = EnumerateShellWindows(state.InteractiveSessionId, state)
                    .Where(window => StartHostProcesses.Contains(window.ProcessName))
                    .ToList();
                if (startWindows.Count > 0)
                    break;
                Thread.Sleep(PollIntervalMilliseconds);
            }
            if (startWindows.Count == 0)
            {
                state.SetOutcome($"Aborted: no visible Start host window appeared within {StartMenuTimeout.TotalSeconds:0} seconds.", 7);
                return;
            }
            state.Add($"Start host appeared: {string.Join(", ", startWindows.Select(window => $"{window.ProcessName}/{window.WindowClass}" ).Distinct(StringComparer.OrdinalIgnoreCase))}.");

            Stopwatch powerButtonTimer = Stopwatch.StartNew();
            List<PowerButtonCandidate> powerButtons = [];
            while (powerButtonTimer.Elapsed < PowerButtonTimeout)
            {
                List<NativeWindowInfo> windows = EnumerateShellWindows(state.InteractiveSessionId, state);
                powerButtons = FindPowerButtons(windows, state);
                state.RecordPowerCandidates(powerButtons);
                PowerButtonCandidate[] possible = powerButtons.Where(candidate => candidate.IsPowerButtonCandidate).ToArray();
                if (possible.Length > 1)
                {
                    state.SetOutcome("Aborted: multiple PowerButton identity candidates were found in the Start shell windows.", 7);
                    return;
                }
                PowerButtonCandidate[] safe = powerButtons.Where(candidate => candidate.IsSafe).ToArray();
                if (safe.Length > 1)
                {
                    state.SetOutcome("Aborted: multiple verified Start PowerButton elements were found.", 7);
                    return;
                }
                if (safe.Length == 1)
                    break;
                Thread.Sleep(PollIntervalMilliseconds);
            }

            PowerButtonCandidate[] verifiedButtons = powerButtons.Where(candidate => candidate.IsSafe).ToArray();
            if (verifiedButtons.Length != 1)
            {
                state.SetOutcome($"Aborted: a unique verified PowerButton was not found within {PowerButtonTimeout.TotalSeconds:0} seconds.", 8);
                return;
            }

            PowerButtonCandidate selectedButton = verifiedButtons[0];
            state.Add("Verified one PowerButton in the current Start shell window. No InvokePattern method will be called.");
            state.Add($"PowerButton: {FormatPowerButton(selectedButton)}");
            state.SetStage("PowerButton ready for activation");

            try
            {
                selectedButton.Element.SetFocus();
            }
            catch (Exception ex)
            {
                state.AddError("PowerButton.SetFocus", ex);
                state.SetOutcome("Aborted: UIA could not focus the verified PowerButton; no Space input was sent.", 9);
                return;
            }

            Stopwatch focusTimer = Stopwatch.StartNew();
            bool focused = false;
            while (focusTimer.Elapsed < TimeSpan.FromSeconds(2))
            {
                try
                {
                    focused = SameRuntimeId(selectedButton.RuntimeId, AutomationElement.FocusedElement.GetRuntimeId());
                }
                catch (Exception ex)
                {
                    state.AddError("FocusedElement.PowerButton", ex);
                }
                if (focused)
                    break;
                Thread.Sleep(100);
            }
            if (!focused)
            {
                state.SetOutcome("Aborted: focused UIA element did not match the verified PowerButton; no Space input was sent.", 10);
                return;
            }

            NativeWindowInfo? foreground = ReadWindow(GetForegroundWindow(), state.InteractiveSessionId, state);
            if (foreground is null || foreground.ProcessId != selectedButton.Snapshot.ProcessId || !foreground.IsVisible)
            {
                state.Add("Focused PowerButton foreground window did not resolve to the same visible shell process.");
                state.SetOutcome("Aborted: PowerButton foreground context was uncertain; no Space input was sent.", 11);
                return;
            }

            List<NativeWindowInfo> refreshedWindows = EnumerateShellWindows(state.InteractiveSessionId, state);
            PowerButtonCandidate[] refreshedButtons = FindPowerButtons(refreshedWindows, state)
                .Where(candidate => candidate.IsPowerButtonCandidate)
                .ToArray();
            if (refreshedButtons.Length != 1 || !refreshedButtons[0].IsSafe ||
                !string.Equals(refreshedButtons[0].RuntimeId, selectedButton.RuntimeId, StringComparison.Ordinal))
            {
                state.SetOutcome("Aborted: PowerButton changed or became ambiguous before menu activation.", 12);
                return;
            }

            bool finalFocusMatches;
            try
            {
                finalFocusMatches = SameRuntimeId(selectedButton.RuntimeId, AutomationElement.FocusedElement.GetRuntimeId());
            }
            catch (Exception ex)
            {
                state.AddError("Final FocusedElement.PowerButton", ex);
                finalFocusMatches = false;
            }
            NativeWindowInfo? finalForeground = ReadWindow(GetForegroundWindow(), state.InteractiveSessionId, state);
            if (!finalFocusMatches || finalForeground is null ||
                finalForeground.ProcessId != selectedButton.Snapshot.ProcessId || !finalForeground.IsVisible)
            {
                state.SetOutcome("Aborted: final focus/foreground check failed; no Space input was sent.", 12);
                return;
            }

            state.Add("FocusedElement runtime ID, foreground shell process, session, and PowerButton identity revalidated.");
            state.Add("About to send exactly one Space key stroke while the verified PowerButton owns keyboard focus.");
            state.WriteReport();
            if (!SendKeyStroke(VirtualKeySpace, state))
            {
                state.SetOutcome("Aborted: the PowerButton Space key stroke was not fully accepted.", 13);
                return;
            }
            state.Add("Space key stroke sent once to open the Power options menu. No UIA InvokePattern was called.");

            Stopwatch menuTimer = Stopwatch.StartNew();
            string? stableKey = null;
            int stableCount = 0;
            List<SleepCandidate> sleepItems = [];
            while (menuTimer.Elapsed < PowerMenuTimeout)
            {
                List<NativeWindowInfo> windows = EnumerateShellWindows(state.InteractiveSessionId, state);
                sleepItems = FindSleepItems(windows, state);
                state.RecordSleepCandidates(sleepItems);

                SleepCandidate[] menuItems = sleepItems
                    .Where(candidate => candidate.IsSleepMenuItemInPowerContext)
                    .GroupBy(candidate => candidate.StabilityKey, StringComparer.Ordinal)
                    .Select(group => group.First())
                    .ToArray();
                if (menuItems.Length > 1)
                {
                    state.SetOutcome("Aborted: multiple Sleep MenuItems in a Start Power context were found.", 14);
                    return;
                }

                SleepCandidate[] safe = menuItems.Where(candidate => candidate.IsSafe).ToArray();
                if (safe.Length == 1)
                {
                    SleepCandidate candidate = safe[0];
                    if (string.Equals(candidate.StabilityKey, stableKey, StringComparison.Ordinal))
                        stableCount++;
                    else
                    {
                        stableKey = candidate.StabilityKey;
                        stableCount = 1;
                    }

                    state.Add($"Verified Sleep candidate observation {stableCount}/{StableSamplesRequired}: {FormatSleepCandidate(candidate)}");
                    if (stableCount >= StableSamplesRequired)
                    {
                        List<NativeWindowInfo> finalWindows = EnumerateShellWindows(state.InteractiveSessionId, state);
                        SleepCandidate[] final = FindSleepItems(finalWindows, state)
                            .Where(item => item.IsSafe && string.Equals(item.StabilityKey, stableKey, StringComparison.Ordinal))
                            .ToArray();
                        if (final.Length != 1)
                        {
                            state.SetOutcome("Aborted: final read-only Sleep target validation failed or was ambiguous.", 15);
                            return;
                        }

                        state.SetSleepTargetVerified(true);
                        state.Add($"Final read-only Sleep target validation passed: {FormatSleepCandidate(final[0])}");
                        state.SetOutcome("Discovery succeeded. Start and Power menus were opened; Sleep was verified but never invoked.", 0);
                        return;
                    }
                }
                else
                {
                    stableKey = null;
                    stableCount = 0;
                }

                Thread.Sleep(PollIntervalMilliseconds);
            }

            state.SetOutcome($"Timed out after {PowerMenuTimeout.TotalSeconds:0} seconds without one stable, fully verified Sleep MenuItem. No power action was performed.", 16);
        }
        catch (Exception ex)
        {
            state.AddError("Automation worker", ex);
            state.SetOutcome("Aborted after an unexpected error. No Sleep Invoke exists in this prototype.", 17);
        }
    }

    private static List<NativeWindowInfo> EnumerateShellWindows(int sessionId, ProbeState state)
    {
        var windows = new List<NativeWindowInfo>();
        EnumWindowsCallback callback = (handle, _) =>
        {
            try
            {
                NativeWindowInfo? window = ReadWindow(handle, sessionId, state);
                if (window is not null && window.IsVisible && ShellProcesses.Contains(window.ProcessName))
                    windows.Add(window);
            }
            catch (Exception ex)
            {
                state.AddError("EnumWindows callback", ex);
            }
            return true;
        };

        if (!EnumWindows(callback, IntPtr.Zero))
            state.AddError("EnumWindows", Marshal.GetLastWin32Error());
        return windows;
    }

    private static NativeWindowInfo? ReadWindow(IntPtr handle, int sessionId, ProbeState state)
    {
        if (handle == IntPtr.Zero)
            return null;
        uint threadId = GetWindowThreadProcessId(handle, out uint rawProcessId);
        if (threadId == 0 || rawProcessId == 0)
            return null;

        int processId = unchecked((int)rawProcessId);
        string processName;
        int processSessionId;
        try
        {
            using Process process = Process.GetProcessById(processId);
            processName = process.ProcessName;
            processSessionId = process.SessionId;
        }
        catch (Exception ex)
        {
            state.AddError("Shell window process/session lookup", ex);
            return null;
        }

        if (!ShellProcesses.Contains(processName) || processSessionId != sessionId)
            return null;

        var className = new StringBuilder(256);
        int length = GetClassName(handle, className, className.Capacity);
        if (length == 0)
            state.AddError("GetClassName", Marshal.GetLastWin32Error());

        return new NativeWindowInfo(handle, processId, processName, className.ToString(),
            IsWindowVisible(handle), processSessionId);
    }

    private static List<PowerButtonCandidate> FindPowerButtons(
        IReadOnlyList<NativeWindowInfo> windows,
        ProbeState state)
    {
        var found = new Dictionary<string, PowerButtonCandidate>(StringComparer.Ordinal);
        Condition[] identities = [
            new PropertyCondition(AutomationElement.AutomationIdProperty, "PowerButton", PropertyConditionFlags.IgnoreCase),
            .. PowerButtonNames.Select(name => new PropertyCondition(AutomationElement.NameProperty, name, PropertyConditionFlags.IgnoreCase))
        ];
        Condition condition = new AndCondition(
            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button),
            new OrCondition(identities));

        foreach (NativeWindowInfo window in windows)
        {
            try
            {
                AutomationElement root = AutomationElement.FromHandle(window.Handle);
                AutomationElementCollection elements = root.FindAll(TreeScope.Descendants, condition);
                int count = Math.Min(elements.Count, MaximumCandidatesPerWindow);
                for (int index = 0; index < count; index++)
                {
                    try
                    {
                        PowerButtonCandidate candidate = InspectPowerButton(elements[index], window, state);
                        string key = candidate.RuntimeId.Length > 0
                            ? $"{candidate.Snapshot.ProcessId}|{candidate.RuntimeId}"
                            : $"no-runtime|{candidate.Snapshot.ProcessId}|{candidate.Snapshot.AutomationId}|{candidate.Snapshot.Name}";
                        if (!found.ContainsKey(key))
                            found.Add(key, candidate);
                    }
                    catch (Exception ex)
                    {
                        state.AddError("Inspect PowerButton candidate", ex);
                    }
                }
                if (elements.Count > MaximumCandidatesPerWindow)
                    state.Add("PowerButton candidate results were capped for one shell window.");
            }
            catch (Exception ex)
            {
                state.AddError("FindAll PowerButton in shell window", ex);
            }
        }
        return found.Values.ToList();
    }

    private static PowerButtonCandidate InspectPowerButton(
        AutomationElement element,
        NativeWindowInfo root,
        ProbeState state)
    {
        UiElementSnapshot snapshot = ReadElement(element);
        string runtimeId = ReadRuntimeId(element);
        AncestorSnapshot control = ReadAncestors(element, TreeWalker.ControlViewWalker, "Control View", state);
        AncestorSnapshot raw = ReadAncestors(element, TreeWalker.RawViewWalker, "Raw View", state);
        int? providerSession = ReadSession(snapshot.ProcessId, state, "PowerButton provider session");
        bool invoke = TryPattern(element, InvokePattern.Pattern, state, "PowerButton InvokePattern");
        bool expandCollapse = TryPattern(element, ExpandCollapsePattern.Pattern, state, "PowerButton ExpandCollapsePattern");
        bool toggle = TryPattern(element, TogglePattern.Pattern, state, "PowerButton TogglePattern");
        bool inStartContext = StartHostProcesses.Contains(root.ProcessName) ||
                              HasStartMarker(control.Nodes) || HasStartMarker(raw.Nodes);
        var candidate = new PowerButtonCandidate(
            element, snapshot, runtimeId, root, providerSession, control, raw,
            invoke, expandCollapse, toggle, inStartContext);
        return candidate;
    }

    private static List<SleepCandidate> FindSleepItems(
        IReadOnlyList<NativeWindowInfo> windows,
        ProbeState state)
    {
        var found = new Dictionary<string, SleepCandidate>(StringComparer.Ordinal);
        Condition[] names = SleepNames
            .Select(name => new PropertyCondition(AutomationElement.NameProperty, name, PropertyConditionFlags.IgnoreCase))
            .ToArray();
        Condition condition = new OrCondition(names);

        foreach (NativeWindowInfo window in windows)
        {
            if (!StartHostProcesses.Contains(window.ProcessName))
                continue;
            try
            {
                AutomationElement root = AutomationElement.FromHandle(window.Handle);
                AutomationElementCollection elements = root.FindAll(TreeScope.Descendants, condition);
                int count = Math.Min(elements.Count, MaximumCandidatesPerWindow);
                for (int index = 0; index < count; index++)
                {
                    try
                    {
                        SleepCandidate candidate = InspectSleepItem(elements[index], window, state);
                        string key = candidate.StabilityKey.Length > 0
                            ? candidate.StabilityKey
                            : $"no-runtime|{candidate.Snapshot.ProcessId}|{candidate.Snapshot.ControlTypeId}|{candidate.Snapshot.Name}";
                        if (!found.ContainsKey(key))
                            found.Add(key, candidate);
                    }
                    catch (Exception ex)
                    {
                        state.AddError("Inspect Sleep candidate", ex);
                    }
                }
                if (elements.Count > MaximumCandidatesPerWindow)
                    state.Add("Sleep candidate results were capped for one Start shell window.");
            }
            catch (Exception ex)
            {
                state.AddError("FindAll Sleep in Start shell window", ex);
            }
        }
        return found.Values.ToList();
    }

    private static SleepCandidate InspectSleepItem(
        AutomationElement element,
        NativeWindowInfo root,
        ProbeState state)
    {
        UiElementSnapshot snapshot = ReadElement(element);
        string runtimeId = ReadRuntimeId(element);
        AncestorSnapshot control = ReadAncestors(element, TreeWalker.ControlViewWalker, "Control View", state);
        AncestorSnapshot raw = ReadAncestors(element, TreeWalker.RawViewWalker, "Raw View", state);
        int? providerSession = ReadSession(snapshot.ProcessId, state, "Sleep provider session");
        bool? invokeSupported = TryPatternOptional(element, InvokePattern.Pattern, state, "Sleep InvokePattern");
        bool controlHierarchy = control.IsComplete && HasSleepHierarchy(root.ProcessName, control.Nodes);
        bool rawHierarchy = raw.IsComplete && HasSleepHierarchy(root.ProcessName, raw.Nodes);
        return new SleepCandidate(
            element, snapshot, runtimeId, root, providerSession, state.InteractiveSessionId,
            control, raw, invokeSupported, controlHierarchy, rawHierarchy);
    }

    private static int? ReadSession(int processId, ProbeState state, string operation)
    {
        try
        {
            using Process process = Process.GetProcessById(processId);
            return process.SessionId;
        }
        catch (Exception ex)
        {
            state.AddError(operation, ex);
            return null;
        }
    }

    private static bool TryPattern(AutomationElement element, AutomationPattern pattern, ProbeState state, string operation)
    {
        try
        {
            return element.TryGetCurrentPattern(pattern, out _);
        }
        catch (Exception ex)
        {
            state.AddError(operation, ex);
            return false;
        }
    }

    private static bool? TryPatternOptional(AutomationElement element, AutomationPattern pattern, ProbeState state, string operation)
    {
        try
        {
            return element.TryGetCurrentPattern(pattern, out _);
        }
        catch (Exception ex)
        {
            state.AddError(operation, ex);
            return null;
        }
    }

    private static UiElementSnapshot ReadElement(AutomationElement element)
    {
        AutomationElement.AutomationElementInformation current = element.Current;
        return new UiElementSnapshot(
            current.Name ?? string.Empty,
            current.AutomationId ?? string.Empty,
            current.ControlType?.ProgrammaticName ?? "(unknown)",
            current.ControlType?.Id ?? 0,
            current.ProcessId,
            current.ClassName ?? string.Empty,
            current.IsEnabled,
            current.IsOffscreen,
            current.IsKeyboardFocusable);
    }

    private static AncestorSnapshot ReadAncestors(
        AutomationElement element,
        TreeWalker walker,
        string viewName,
        ProbeState state)
    {
        var nodes = new List<UiElementSnapshot>();
        AutomationElement? current;
        try
        {
            current = walker.GetParent(element);
        }
        catch (Exception ex)
        {
            state.AddError($"{viewName} GetParent", ex);
            return new AncestorSnapshot([], false);
        }

        for (int depth = 0; current is not null && depth < MaximumAncestorDepth; depth++)
        {
            try
            {
                nodes.Add(ReadElement(current));
                current = walker.GetParent(current);
            }
            catch (Exception ex)
            {
                state.AddError($"{viewName} read ancestor", ex);
                return new AncestorSnapshot(nodes.AsEnumerable().Reverse().ToArray(), false);
            }
        }

        bool complete = current is null;
        if (!complete)
            state.Add($"{viewName} ancestry stopped at depth {MaximumAncestorDepth}.");
        return new AncestorSnapshot(nodes.AsEnumerable().Reverse().ToArray(), complete);
    }

    private static bool HasStartMarker(IReadOnlyList<UiElementSnapshot> nodes) => nodes.Any(node =>
        Values(node).Any(value => IsOneOf(value, StartNames) ||
                                  Normalize(value).Contains("startmenu", StringComparison.Ordinal) ||
                                  Normalize(value).Contains("başlangıç menüsü", StringComparison.Ordinal)));

    private static bool HasSleepHierarchy(string processName, IReadOnlyList<UiElementSnapshot> ancestors)
    {
        int startIndex = -1;
        for (int index = 0; index < ancestors.Count; index++)
        {
            if (Values(ancestors[index]).Any(value => IsOneOf(value, StartNames)))
            {
                startIndex = index;
                break;
            }
        }
        if (startIndex < 0 && !StartHostProcesses.Contains(processName))
            return false;

        int openedIndex = -1;
        for (int index = startIndex + 1; index < ancestors.Count; index++)
        {
            if (Values(ancestors[index]).Any(value => IsOneOf(value, OpenNames)))
            {
                openedIndex = index;
                break;
            }
        }
        if (openedIndex < 0)
            return false;

        for (int index = openedIndex + 1; index < ancestors.Count; index++)
        {
            if (Values(ancestors[index]).Any(IsPowerMenuName))
                return true;
        }
        return false;
    }

    private static IEnumerable<string> Values(UiElementSnapshot node)
    {
        yield return node.Name;
        yield return node.AutomationId;
        yield return node.ClassName;
    }

    private static bool IsPowerMenuName(string value) => IsOneOf(value, PowerMenuNames) ||
        Normalize(value).Contains("power options", StringComparison.Ordinal) ||
        Normalize(value).Contains("aç kapa seçenekleri", StringComparison.Ordinal) ||
        Normalize(value).Contains("güç seçenekleri", StringComparison.Ordinal);

    private static bool IsPowerButtonIdentity(string automationId, string name) =>
        string.Equals(automationId, "PowerButton", StringComparison.OrdinalIgnoreCase) ||
        IsOneOf(name, PowerButtonNames);

    private static bool IsSleepName(string value) => IsOneOf(value, SleepNames);

    private static bool IsOneOf(string value, IEnumerable<string> choices)
    {
        string normalized = Normalize(value);
        return choices.Any(choice => normalized == Normalize(choice));
    }

    private static string Normalize(string value) => string.Join(
        ' ', value.Normalize(NormalizationForm.FormKC)
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        .ToLowerInvariant();

    private static string ReadRuntimeId(AutomationElement element) =>
        string.Join('.', element.GetRuntimeId().Select(value => value.ToString("X8", CultureInfo.InvariantCulture)));

    private static bool SameRuntimeId(string first, int[] second) =>
        second.Length > 0 && string.Equals(first,
            string.Join('.', second.Select(value => value.ToString("X8", CultureInfo.InvariantCulture))),
            StringComparison.Ordinal);

    private static bool SendKeyStroke(ushort virtualKey, ProbeState state)
    {
        if (state.IsCancelled)
        {
            state.Add("Cancellation was requested before keyboard input; no key was sent.");
            return false;
        }

        Input[] inputs = [
            new() { Type = InputKeyboard, Data = new InputUnion { Keyboard = new KeyboardInput { VirtualKey = virtualKey } } },
            new() { Type = InputKeyboard, Data = new InputUnion { Keyboard = new KeyboardInput { VirtualKey = virtualKey, Flags = KeyEventKeyUp } } }
        ];
        uint sent = SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
        if (sent == inputs.Length)
            return true;

        int error = Marshal.GetLastWin32Error();
        state.Add($"SendInput accepted {sent}/{inputs.Length} keyboard records; Win32Error=0x{error:X8}.");
        if (sent == 1)
        {
            Input release = new()
            {
                Type = InputKeyboard,
                Data = new InputUnion { Keyboard = new KeyboardInput { VirtualKey = virtualKey, Flags = KeyEventKeyUp } }
            };
            uint released = SendInput(1, [release], Marshal.SizeOf<Input>());
            state.Add($"Best-effort key release after a partial SendInput result: accepted={released == 1}.");
        }
        return false;
    }

    private static string FormatPowerButton(PowerButtonCandidate candidate) =>
        $"Name=\"{Escape(candidate.Snapshot.Name)}\"; AutomationId=\"{Escape(candidate.Snapshot.AutomationId)}\"; " +
        $"ControlType={candidate.Snapshot.ControlType} ({candidate.Snapshot.ControlTypeId}); " +
        $"Process={candidate.Root.ProcessName}; PID={candidate.Root.ProcessId}; Session={candidate.ProviderSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}; " +
        $"WindowClass={candidate.Root.WindowClass}; HWND=0x{candidate.Root.Handle.ToInt64():X}; " +
        $"Visible={!candidate.Snapshot.IsOffscreen && candidate.Root.IsVisible}; Enabled={candidate.Snapshot.IsEnabled}; " +
        $"KeyboardFocusable={candidate.Snapshot.IsKeyboardFocusable}; StartContext={candidate.InStartContext}; " +
        $"InvokePatternSupported={candidate.InvokePatternSupported}; ExpandCollapsePatternSupported={candidate.ExpandCollapsePatternSupported}; " +
        $"TogglePatternSupported={candidate.TogglePatternSupported}; ControlChain=\"{FormatChain(candidate.ControlAncestors.Nodes)}\"; " +
        $"RawChain=\"{FormatChain(candidate.RawAncestors.Nodes)}\"; Safe={candidate.IsSafe}";

    private static string FormatSleepCandidate(SleepCandidate candidate) =>
        $"Name=\"{Escape(candidate.Snapshot.Name)}\"; AutomationId=\"{Escape(candidate.Snapshot.AutomationId)}\"; " +
        $"ControlType={candidate.Snapshot.ControlType} ({candidate.Snapshot.ControlTypeId}); " +
        $"Process={candidate.Root.ProcessName}; PID={candidate.Snapshot.ProcessId}; ProviderSession={candidate.ProviderSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}; " +
        $"InteractiveSession={candidate.InteractiveSessionId}; Visible={!candidate.Snapshot.IsOffscreen && candidate.Root.IsVisible}; " +
        $"Enabled={candidate.Snapshot.IsEnabled}; InvokePatternSupported={FormatOptional(candidate.InvokePatternSupported)}; " +
        $"ControlChainComplete={candidate.ControlAncestors.IsComplete}; ControlChain=\"{FormatChain(candidate.ControlAncestors.Nodes)}\"; " +
        $"RawChainComplete={candidate.RawAncestors.IsComplete}; RawChain=\"{FormatChain(candidate.RawAncestors.Nodes)}\"; " +
        $"ControlHierarchy={candidate.ControlHierarchyMatches}; RawHierarchy={candidate.RawHierarchyMatches}; Safe={candidate.IsSafe}; " +
        $"RuntimeId={candidate.RuntimeId}";

    private static string FormatChain(IReadOnlyList<UiElementSnapshot> nodes)
    {
        if (nodes.Count == 0)
            return "(no accessible parent)";
        return string.Join(" -> ", nodes.Select(node =>
        {
            bool relevant = Values(node).Any(value => IsOneOf(value, StartNames) || IsOneOf(value, OpenNames) || IsPowerMenuName(value));
            string label = relevant ? Escape(string.IsNullOrWhiteSpace(node.Name) ? node.AutomationId : node.Name) : "(name redacted)";
            return $"{label} [{node.ControlType}; PID={node.ProcessId}]";
        }));
    }

    private static string FormatOptional(bool? value) => value switch
    {
        true => "true",
        false => "false",
        _ => "unknown"
    };

    private static string Escape(string value) => Regex.Replace(value.Replace('"', '\''), @"[\r\n\t]+", " ");

    private static string CreateReportPath() => Path.Combine(
        Path.GetTempPath(),
        $"ClassicDesktopScreensaver-PowerMenuUiaAutomationProbe-{DateTimeOffset.Now:yyyyMMdd-HHmmss-fff}-{Guid.NewGuid():N}.txt");

    private static void TrySoundAlert()
    {
        try
        {
            System.Media.SystemSounds.Asterisk.Play();
        }
        catch
        {
            // A sound failure must not affect the report or exit status.
        }
    }

    private static int RunSelfTests()
    {
        int failures = 0;
        var powerTests = new (string Name, PowerPolicyEvidence Evidence, bool Expected)[]
        {
            ("Turkish Start PowerButton", new("PowerButton", "Güç", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, true, true, true, true, true), true),
            ("localized name without PowerButton id", new("", "Power", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, true, true, true, true, true), false),
            ("wrong automation id and label", new("CommandButton", "Sleep", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, true, true, true, true, true), false),
            ("wrong control type", new("PowerButton", "Power", 50011, "StartMenuExperienceHost", 1, 1, true, true, true, true, true), false),
            ("wrong session", new("PowerButton", "Power", ButtonControlTypeId, "StartMenuExperienceHost", 2, 1, true, true, true, true, true), false),
            ("non-Start process", new("PowerButton", "Power", ButtonControlTypeId, "notepad", 1, 1, true, true, true, true, true), false),
            ("offscreen button", new("PowerButton", "Power", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, true, false, true, true, true), false),
            ("disabled button", new("PowerButton", "Power", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, false, true, true, true, true), false),
            ("not keyboard focusable", new("PowerButton", "Power", ButtonControlTypeId, "StartMenuExperienceHost", 1, 1, true, true, false, true, true), false)
        };

        foreach ((string name, PowerPolicyEvidence evidence, bool expected) in powerTests)
        {
            bool actual = IsPowerButtonSafe(evidence, 1);
            Check(name, expected, actual, ref failures);
        }
        PowerPolicyEvidence validPower = powerTests[0].Evidence;
        Check("incomplete PowerButton Control ancestry", false,
            IsPowerButtonSafe(validPower with { ControlChainComplete = false }, 1), ref failures);
        Check("incomplete PowerButton Raw ancestry", false,
            IsPowerButtonSafe(validPower with { RawChainComplete = false }, 1), ref failures);
        Check("PowerButton runtime identity unavailable", false,
            IsPowerButtonSafe(validPower with { HasRuntimeId = false }, 1), ref failures);

        var sleepTests = new (string Name, SleepPolicyEvidence Evidence, bool Expected)[]
        {
            ("verified Turkish Sleep MenuItem", new("Uyku", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, true, true), true),
            ("verified English Sleep MenuItem", new("Sleep", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, true, true), true),
            ("Hibernate label", new("Hazırda Beklet", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, true, true), false),
            ("Shut down label", new("Kapat", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, true, true), false),
            ("Text label", new("Uyku", 50020, 1, 1, 1, true, true, true, true, true, true, true, true), false),
            ("wrong provider session", new("Sleep", MenuItemControlTypeId, 2, 1, 1, true, true, true, true, true, true, true, true), false),
            ("wrong root session", new("Sleep", MenuItemControlTypeId, 1, 2, 1, true, true, true, true, true, true, true, true), false),
            ("incomplete Control hierarchy", new("Sleep", MenuItemControlTypeId, 1, 1, 1, false, true, true, true, true, true, true, true), false),
            ("incomplete Raw hierarchy", new("Sleep", MenuItemControlTypeId, 1, 1, 1, true, false, true, true, true, true, true, true), false),
            ("wrong native process", new("Sleep", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, false, true), false),
            ("InvokePattern unavailable", new("Sleep", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, true, true, true, false), false),
            ("disabled Sleep", new("Sleep", MenuItemControlTypeId, 1, 1, 1, true, true, true, true, false, true, true, true), false)
        };

        foreach ((string name, SleepPolicyEvidence evidence, bool expected) in sleepTests)
        {
            bool actual = IsSleepTargetSafe(evidence, 1);
            Check(name, expected, actual, ref failures);
        }

        Check("Power hierarchy ordering", true,
            HasOrderedHierarchy("StartMenuExperienceHost", ["Başlangıç", "Açılan", "Aç/Kapa seçenekleri"]), ref failures);
        Check("wrong Power hierarchy ordering", false,
            HasOrderedHierarchy("StartMenuExperienceHost", ["Başlangıç", "Aç/Kapa seçenekleri", "Açılan"]), ref failures);
        Check("space key INPUT ABI", IntPtr.Size == 8 ? 40 : 28, Marshal.SizeOf<Input>(), ref failures);
        int totalTests = powerTests.Length + sleepTests.Length + 6;
        Console.WriteLine($"Self-test result: {totalTests - failures}/{totalTests} passed. No UIA query, key input, InvokePattern call, or power action was performed.");
        return failures == 0 ? 0 : 1;
    }

    private static bool IsPowerButtonSafe(PowerPolicyEvidence item, int sessionId) =>
        string.Equals(item.AutomationId, "PowerButton", StringComparison.OrdinalIgnoreCase) &&
        item.ControlTypeId == ButtonControlTypeId &&
        ShellProcesses.Contains(item.ProcessName) &&
        item.ProviderSessionId == sessionId &&
        item.RootSessionId == sessionId &&
        item.RootProcessMatches &&
        item.InStartContext &&
        item.IsEnabled && item.IsVisible && item.IsKeyboardFocusable &&
        item.ControlChainComplete && item.RawChainComplete && item.HasRuntimeId;

    private static bool IsSleepTargetSafe(SleepPolicyEvidence item, int sessionId) =>
        IsSleepName(item.Name) &&
        item.ControlTypeId == MenuItemControlTypeId &&
        item.ProviderSessionId == sessionId &&
        item.RootSessionId == sessionId &&
        item.InteractiveSessionId == sessionId &&
        item.ControlChainComplete && item.RawChainComplete &&
        item.ControlHierarchyMatches && item.RawHierarchyMatches &&
        item.IsEnabled && item.IsVisible && item.RootProcessMatches &&
        item.InvokePatternSupported == true;

    private static bool HasOrderedHierarchy(string processName, IReadOnlyList<string> values)
    {
        int start = -1;
        for (int index = 0; index < values.Count; index++)
        {
            if (IsOneOf(values[index], StartNames))
            {
                start = index;
                break;
            }
        }
        if (start < 0 && !StartHostProcesses.Contains(processName))
            return false;
        int opened = -1;
        for (int index = start + 1; index < values.Count; index++)
        {
            if (IsOneOf(values[index], OpenNames))
            {
                opened = index;
                break;
            }
        }
        if (opened < 0)
            return false;
        return values.Skip(opened + 1).Any(IsPowerMenuName);
    }

    private static void Check(string name, bool expected, bool actual, ref int failures)
    {
        bool passed = expected == actual;
        Console.WriteLine($"{(passed ? "PASS" : "FAIL")}: {name}; expected={expected}; actual={actual}");
        if (!passed)
            failures++;
    }

    private static void Check(string name, int expected, int actual, ref int failures)
    {
        bool passed = expected == actual;
        Console.WriteLine($"{(passed ? "PASS" : "FAIL")}: {name}; expected={expected}; actual={actual}");
        if (!passed)
            failures++;
    }

    private sealed class ProbeState(string reportPath, int interactiveSessionId, int processId, DateTimeOffset startedAt)
    {
        private readonly object _gate = new();
        private readonly List<string> _trace = [];
        private readonly List<string> _powerEvidence = [];
        private readonly List<string> _sleepEvidence = [];
        private readonly Dictionary<string, int> _errors = new(StringComparer.Ordinal);
        private string _outcome = "Running";
        private int _exitCode = 1;
        private bool _sleepTargetVerified;
        private bool _cancelled;

        public string ReportPath { get; } = reportPath;
        public int InteractiveSessionId { get; } = interactiveSessionId;
        public int ProcessId { get; } = processId;
        public DateTimeOffset StartedAt { get; } = startedAt;
        public string Outcome { get { lock (_gate) return _outcome; } }
        public int ExitCode { get { lock (_gate) return _exitCode; } }
        public bool SleepTargetVerified { get { lock (_gate) return _sleepTargetVerified; } }
        public bool IsCancelled { get { lock (_gate) return _cancelled; } }

        public void Cancel()
        {
            lock (_gate)
                _cancelled = true;
        }

        public void Add(string line)
        {
            lock (_gate)
                _trace.Add($"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz} {line}");
        }

        public void AddError(string operation, Exception exception)
        {
            string key = $"{operation}; {exception.GetType().Name}; HRESULT=0x{exception.HResult:X8}";
            lock (_gate)
                _errors[key] = _errors.GetValueOrDefault(key) + 1;
        }

        public void AddError(string operation, int win32Error)
        {
            string key = $"{operation}; Win32Error=0x{win32Error:X8}";
            lock (_gate)
                _errors[key] = _errors.GetValueOrDefault(key) + 1;
        }

        public void SetOutcome(string outcome, int exitCode)
        {
            lock (_gate)
            {
                _outcome = outcome;
                _exitCode = exitCode;
            }
            Add($"Outcome: {outcome}");
        }

        public void SetStage(string stage) => Add($"Stage: {stage}.");

        public void SetSleepTargetVerified(bool value)
        {
            lock (_gate)
                _sleepTargetVerified = value;
        }

        public void RecordPowerCandidates(IEnumerable<PowerButtonCandidate> candidates)
        {
            lock (_gate)
            {
                foreach (PowerButtonCandidate candidate in candidates)
                {
                    string line = FormatPowerButton(candidate);
                    if (!_powerEvidence.Contains(line, StringComparer.Ordinal))
                        _powerEvidence.Add(line);
                }
            }
        }

        public void RecordSleepCandidates(IEnumerable<SleepCandidate> candidates)
        {
            lock (_gate)
            {
                foreach (SleepCandidate candidate in candidates)
                {
                    string line = FormatSleepCandidate(candidate);
                    if (!_sleepEvidence.Contains(line, StringComparer.Ordinal))
                        _sleepEvidence.Add(line);
                }
            }
        }

        public void WriteReport()
        {
            string content;
            lock (_gate)
            {
                var report = new StringBuilder();
                report.AppendLine("Classic Desktop Screensaver - Start Power Menu UIA Automation Probe");
                report.AppendLine("Mode: automated Start/Power menu opening and read-only Sleep discovery.");
                report.AppendLine($"Start time: {StartedAt:yyyy-MM-dd HH:mm:ss.fff zzz}");
                report.AppendLine($"Report time: {DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz}");
                report.AppendLine($"Session={InteractiveSessionId}; PID={ProcessId}; Architecture={(Environment.Is64BitProcess ? "64-bit" : "32-bit")}");
                report.AppendLine($"Outcome: {_outcome}");
                report.AppendLine($"Verified Sleep target: {_sleepTargetVerified}");
                report.AppendLine("InvokePattern.Invoke called: false");
                report.AppendLine("Sleep, Hibernate, Shut down, Restart, SetSuspendState, and power APIs were never called.");
                report.AppendLine("Keyboard input: at most one Left Windows stroke to open Start and one Space stroke after the exact PowerButton was focused and revalidated.");
                report.AppendLine();
                report.AppendLine("Trace:");
                foreach (string line in _trace)
                    report.AppendLine(line);
                report.AppendLine();
                report.AppendLine("PowerButton evidence:");
                if (_powerEvidence.Count == 0)
                    report.AppendLine("  No matching PowerButton candidate was recorded.");
                else
                    foreach (string line in _powerEvidence)
                        report.AppendLine($"  {line}");
                report.AppendLine();
                report.AppendLine("Sleep candidate evidence:");
                if (_sleepEvidence.Count == 0)
                    report.AppendLine("  No Sleep/Uyku candidate was recorded.");
                else
                    foreach (string line in _sleepEvidence)
                        report.AppendLine($"  {line}");
                report.AppendLine();
                report.AppendLine("Aggregated UIA/native errors:");
                if (_errors.Count == 0)
                    report.AppendLine("  None recorded.");
                else
                    foreach ((string key, int count) in _errors.OrderBy(pair => pair.Key, StringComparer.Ordinal))
                        report.AppendLine($"  {key}; Count={count}");
                content = report.ToString();
            }

            byte[] bytes = new UTF8Encoding(false).GetBytes(content);
            using var stream = new FileStream(ReportPath, FileMode.Create, FileAccess.Write, FileShare.Read);
            stream.Write(bytes);
            stream.Flush(flushToDisk: true);
        }
    }

    private sealed record NativeWindowInfo(
        IntPtr Handle,
        int ProcessId,
        string ProcessName,
        string WindowClass,
        bool IsVisible,
        int SessionId);

    private sealed record UiElementSnapshot(
        string Name,
        string AutomationId,
        string ControlType,
        int ControlTypeId,
        int ProcessId,
        string ClassName,
        bool IsEnabled,
        bool IsOffscreen,
        bool IsKeyboardFocusable);

    private sealed record AncestorSnapshot(IReadOnlyList<UiElementSnapshot> Nodes, bool IsComplete);

    private sealed record PowerButtonCandidate(
        AutomationElement Element,
        UiElementSnapshot Snapshot,
        string RuntimeId,
        NativeWindowInfo Root,
        int? ProviderSessionId,
        AncestorSnapshot ControlAncestors,
        AncestorSnapshot RawAncestors,
        bool InvokePatternSupported,
        bool ExpandCollapsePatternSupported,
        bool TogglePatternSupported,
        bool InStartContext)
    {
        public bool IsSafe => IsPowerButtonSafe(new PowerPolicyEvidence(
            Snapshot.AutomationId, Snapshot.Name, Snapshot.ControlTypeId, Root.ProcessName,
            ProviderSessionId, Root.SessionId, Root.ProcessId == Snapshot.ProcessId,
            InStartContext, Snapshot.IsEnabled, !Snapshot.IsOffscreen && Root.IsVisible,
            Snapshot.IsKeyboardFocusable, ControlAncestors.IsComplete, RawAncestors.IsComplete,
            RuntimeId.Length > 0), Root.SessionId);
        public bool IsPowerButtonCandidate =>
            Snapshot.ControlTypeId == ButtonControlTypeId &&
            IsPowerButtonIdentity(Snapshot.AutomationId, Snapshot.Name) &&
            Root.IsVisible && !Snapshot.IsOffscreen;
    }

    private sealed record SleepCandidate(
        AutomationElement Element,
        UiElementSnapshot Snapshot,
        string RuntimeId,
        NativeWindowInfo Root,
        int? ProviderSessionId,
        int InteractiveSessionId,
        AncestorSnapshot ControlAncestors,
        AncestorSnapshot RawAncestors,
        bool? InvokePatternSupported,
        bool ControlHierarchyMatches,
        bool RawHierarchyMatches)
    {
        public string StabilityKey => RuntimeId.Length == 0 ? string.Empty : $"{Snapshot.ProcessId}|{RuntimeId}";
        public bool IsSleepMenuItemInPowerContext =>
            IsSleepName(Snapshot.Name) && Snapshot.ControlTypeId == MenuItemControlTypeId &&
            (ControlHierarchyMatches || RawHierarchyMatches);
        public bool IsSafe => IsSleepTargetSafe(new SleepPolicyEvidence(
            Snapshot.Name, Snapshot.ControlTypeId, ProviderSessionId, Root.SessionId, InteractiveSessionId,
            ControlAncestors.IsComplete, RawAncestors.IsComplete,
            ControlHierarchyMatches, RawHierarchyMatches, Snapshot.IsEnabled,
            !Snapshot.IsOffscreen && Root.IsVisible,
            Root.ProcessId == Snapshot.ProcessId, InvokePatternSupported), InteractiveSessionId) &&
            RuntimeId.Length > 0;
    }

    private sealed record PowerPolicyEvidence(
        string AutomationId,
        string Name,
        int ControlTypeId,
        string ProcessName,
        int? ProviderSessionId,
        int RootSessionId,
        bool RootProcessMatches,
        bool InStartContext,
        bool IsEnabled,
        bool IsVisible,
        bool IsKeyboardFocusable,
        bool ControlChainComplete = true,
        bool RawChainComplete = true,
        bool HasRuntimeId = true);

    private sealed record SleepPolicyEvidence(
        string Name,
        int ControlTypeId,
        int? ProviderSessionId,
        int RootSessionId,
        int InteractiveSessionId,
        bool ControlChainComplete,
        bool RawChainComplete,
        bool ControlHierarchyMatches,
        bool RawHierarchyMatches,
        bool IsEnabled,
        bool IsVisible,
        bool RootProcessMatches,
        bool? InvokePatternSupported);

    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint Type;
        public InputUnion Data;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MouseInput Mouse;
        [FieldOffset(0)] public KeyboardInput Keyboard;
        [FieldOffset(0)] public HardwareInput Hardware;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int Dx;
        public int Dy;
        public uint MouseData;
        public uint Flags;
        public uint Time;
        public UIntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort VirtualKey;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public UIntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HardwareInput
    {
        public uint Message;
        public ushort ParameterLow;
        public ushort ParameterHigh;
    }
}
