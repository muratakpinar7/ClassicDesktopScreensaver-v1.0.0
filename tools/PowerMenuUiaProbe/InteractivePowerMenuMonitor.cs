using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Automation;

namespace PowerMenuUiaProbe;

internal static class InteractivePowerMenuMonitor
{
    private const int DefaultDurationSeconds = 30;
    private const int DefaultIntervalMilliseconds = 500;
    private const int DefaultBaselineSeconds = 5;
    private const int MaxDepth = 14;
    private const int MaxNodesPerView = 1500;
    private const int MaxScopedSleepMatches = 25;
    private const int MaxCursorRootRawNodes = 300;
    private const int MaxCursorRootRawDepth = 10;
    private const int PopupOwner = 4;
    private const int RootOwnerAncestor = 3;
    private const int RootAncestor = 2;
    private const int CursorRootSearchIntervalSamples = 8;
    private const string PreOpenBaselinePhase = "Pre-open baseline";
    private const string AfterOpenPromptPhase = "After-open prompt";

    private static readonly HashSet<string> StartHostProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "StartMenuExperienceHost",
        "ShellExperienceHost"
    };

    private static readonly HashSet<string> ExplorerShellClasses = new(StringComparer.OrdinalIgnoreCase)
    {
        "Shell_TrayWnd",
        "Shell_SecondaryTrayWnd",
        "Progman",
        "WorkerW",
        "Windows.UI.Core.CoreWindow",
        "Xaml_WindowedPopupClass",
        "DV2ControlHost"
    };

    private static readonly Dictionary<string, string> PowerActionLabels = new(StringComparer.Ordinal)
    {
        ["sleep"] = "Sleep",
        ["uyku"] = "Sleep",
        ["hibernate"] = "Hibernate",
        ["hazırda beklet"] = "Hibernate",
        ["hazırda bekletme"] = "Hibernate",
        ["shut down"] = "Shut down",
        ["shutdown"] = "Shut down",
        ["kapat"] = "Shut down",
        ["restart"] = "Restart",
        ["yeniden başlat"] = "Restart",
        ["update and shut down"] = "Update and shut down",
        ["güncelleştir ve kapat"] = "Update and shut down",
        ["update and restart"] = "Update and restart",
        ["güncelleştir ve yeniden başlat"] = "Update and restart"
    };

    private static readonly Dictionary<int, string> ProcessNames = [];
    private static readonly Dictionary<int, int?> ProcessSessionIds = [];

    private delegate bool EnumWindowsCallback(IntPtr windowHandle, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr windowHandle, uint command);

    [DllImport("user32.dll")]
    private static extern IntPtr GetAncestor(IntPtr windowHandle, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetCursorPos(out NativePoint point);

    [DllImport("user32.dll")]
    private static extern IntPtr WindowFromPoint(NativePoint point);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetWindowThreadProcessId(IntPtr windowHandle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr windowHandle, StringBuilder className, int maxCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr windowHandle);

    public static int Run(string[] args)
    {
        int durationSeconds = ReadOption(args, "--duration-seconds", DefaultDurationSeconds, 5, 120);
        int intervalMilliseconds = ReadOption(args, "--sample-interval-ms", DefaultIntervalMilliseconds, 100, 2000);
        if (durationSeconds < 0 || intervalMilliseconds < 0)
            return 2;

        using Process currentProcess = Process.GetCurrentProcess();
        if (currentProcess.SessionId == 0)
        {
            Console.Error.WriteLine("Bu araç etkileşimli Windows kullanıcı oturumunda çalıştırılmalıdır. Tarama yapılmadı.");
            return 3;
        }

        int baselineSeconds = Math.Min(DefaultBaselineSeconds, Math.Max(1, durationSeconds / 6));
        TimeSpan baselineDuration = TimeSpan.FromSeconds(baselineSeconds);
        DateTimeOffset startedAt = DateTimeOffset.Now;
        var samples = new List<SampleRecord>();
        var histories = new Dictionary<string, ElementHistory>(StringComparer.Ordinal);
        var events = new List<PresenceEvent>();
        var totalTimer = Stopwatch.StartNew();

        Console.WriteLine("Classic Desktop Screensaver - Power Menu UIA Probe");
        Console.WriteLine("Salt okunur tarama: UIA Control View ve Raw View; hiçbir kontrol veya güç eylemi çağrılmaz.");
        Console.WriteLine($"Windows session ID: {currentProcess.SessionId}");
        Console.WriteLine($"Tarama süresi: {durationSeconds} saniye; hedef aralık: {intervalMilliseconds} ms; ilk {baselineSeconds} saniye başlangıç ölçümüdür.");
        Console.WriteLine("Başlangıç ölçümü sırasında Başlat menüsünü kapalı tutun. Program ölçüm bittiğinde açmanızı söyleyecek.");
        Console.WriteLine("CMD penceresine dönmeniz gerekmez. Hiçbir güç seçeneğine tıklamayın.");

        int sampleNumber = 0;
        bool startMenuPromptShown = false;
        while (totalTimer.Elapsed < TimeSpan.FromSeconds(durationSeconds))
        {
            TimeSpan elapsed = totalTimer.Elapsed;
            string phase = elapsed < baselineDuration ? PreOpenBaselinePhase : AfterOpenPromptPhase;
            if (!startMenuPromptShown && phase == AfterOpenPromptPhase)
            {
                Console.WriteLine("Başlangıç ölçümü tamamlandı. Şimdi Başlat -> Güç menüsünü açın; tıklamadan fareyi Uyku seçeneğinin üzerinde tutun ve bitiş sesini bekleyin.");
                startMenuPromptShown = true;
            }

            DateTimeOffset capturedAt = DateTimeOffset.Now;
            var sampleTimer = Stopwatch.StartNew();
            SampleRecord sample = CaptureSample(sampleNumber + 1, capturedAt, phase, currentProcess.SessionId);
            sample.ScanMilliseconds = sampleTimer.ElapsedMilliseconds;
            samples.Add(sample);
            UpdateHistories(sample, histories, events);
            sampleNumber++;

            if (sampleNumber % Math.Max(1, 5000 / intervalMilliseconds) == 0)
            {
                Console.WriteLine(
                    $"Ölçüm {sampleNumber}: {capturedAt:HH:mm:ss.fff zzz}; " +
                    $"Start/Güç bağlamındaki öğe: {sample.Elements.Count}.");
            }

            long remainingMilliseconds = durationSeconds * 1000L - totalTimer.ElapsedMilliseconds;
            int sleepMilliseconds = (int)Math.Min(
                Math.Max(0, intervalMilliseconds - sampleTimer.ElapsedMilliseconds),
                Math.Max(0, remainingMilliseconds));
            if (sleepMilliseconds > 0)
                Thread.Sleep(sleepMilliseconds);
        }

        DateTimeOffset endedAt = DateTimeOffset.Now;
        string reportPath = CreateReportPath(endedAt);
        string report = BuildReport(
            startedAt, endedAt, currentProcess.SessionId, currentProcess.Id,
            durationSeconds, baselineSeconds, intervalMilliseconds, samples, histories, events);

        try
        {
            File.WriteAllText(reportPath, report, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Rapor geçici dosyaya yazılamadı: {ex.GetType().Name}");
            return 1;
        }

        string alertResult = TrySoundAlert();
        try
        {
            File.AppendAllText(
                reportPath,
                Environment.NewLine + $"End alert: {alertResult}",
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Sesli uyarı sonucu rapora eklenemedi: {ex.GetType().Name}");
        }

        Console.WriteLine();
        Console.WriteLine($"Tarama bitti: {endedAt:yyyy-MM-dd HH:mm:ss.fff zzz}");
        Console.WriteLine($"Sesli uyarı: {alertResult}");
        Console.WriteLine($"Rapor: {reportPath}");
        return 0;
    }

    private static SampleRecord CaptureSample(
        int number,
        DateTimeOffset capturedAt,
        string phase,
        int interactiveSessionId)
    {
        var sample = new SampleRecord
        {
            Number = number,
            CapturedAt = capturedAt,
            Phase = phase,
            InteractiveSessionId = interactiveSessionId
        };

        List<NativeWindowInfo> enumWindows = EnumerateShellWindows(sample);
        sample.ForegroundWindow = ReadForegroundWindow(sample);
        if (sample.ForegroundWindow is
            {
                IsTargetShellWindow: true,
                ProcessName: not null,
                ProcessId: int startProcessId
            } startWindow && StartHostProcesses.Contains(startWindow.ProcessName))
        {
            sample.ForegroundStartProcessId = startProcessId;
        }
        sample.ForegroundWindowInEnumWindows = sample.ForegroundWindow.IsTargetShellWindow
            ? enumWindows.Any(window => window.Handle == sample.ForegroundWindow.Handle)
            : null;

        List<StartOwnedPopupInfo> startOwnedPopups = EnumerateStartOwnedPopups(sample, sample.ForegroundWindow);
        sample.StartOwnedPopups.AddRange(startOwnedPopups);

        CaptureCursorAndFocus(sample);

        List<NativeWindowInfo> rootWindows = enumWindows
            .Select(window => window with
            {
                IsForeground = window.Handle == sample.ForegroundWindow?.Handle
            })
            .ToList();
        rootWindows.AddRange(startOwnedPopups.Select(popup => new NativeWindowInfo(
            popup.Handle,
            popup.ProcessId,
            popup.ProcessName,
            popup.WindowClass,
            popup.IsVisible,
            IsForeground: false,
            DiscoverySource: "Start-owned popup (root-owner matched foreground)")));
        sample.ShellWindows.AddRange(rootWindows.Where(window =>
            !string.Equals(
                window.DiscoverySource,
                "Start-owned popup (root-owner matched foreground)",
                StringComparison.Ordinal)));

        if (sample.ForegroundWindow is
            {
                IsTargetShellWindow: true,
                ProcessName: not null,
                ProcessId: int foregroundProcessId,
                WindowClass: not null
            } foreground &&
            sample.ForegroundWindowInEnumWindows == false)
        {
            rootWindows.Add(new NativeWindowInfo(
                foreground.Handle,
                foregroundProcessId,
                foreground.ProcessName,
                foreground.WindowClass,
                foreground.IsVisible ?? false,
                IsForeground: true,
                DiscoverySource: "GetForegroundWindow fallback (not returned by EnumWindows)"));
        }

        foreach ((string viewName, TreeWalker walker) in new[]
                 {
                     ("Control View", TreeWalker.ControlViewWalker),
                     ("Raw View", TreeWalker.RawViewWalker)
                 })
        {
            Dictionary<string, RootReference> roots = CollectRoots(walker, rootWindows, sample);
            int visited = 0;

            foreach (RootReference root in roots.Values)
            {
                if (visited >= MaxNodesPerView)
                    break;

                Walk(
                    root.Element, root.Info, viewName, walker, [], sample,
                    ref visited, depth: 0, isStartRelatedPopupRoot: root.IsStartRelatedPopup);
            }

            sample.NodesVisited[viewName] = visited;
            sample.RootsVisited[viewName] = roots.Count;
            sample.RootDetails[viewName] = roots.Values
                .Select(root => CreateRootWindowInfo(root.Info, sample.ForegroundWindow, root.DiscoverySources))
                .DistinctBy(root => root.Key)
                .OrderBy(root => root.ProcessName, StringComparer.OrdinalIgnoreCase)
                .ThenBy(root => root.WindowClass, StringComparer.OrdinalIgnoreCase)
                .ThenBy(root => root.Handle)
                .ToList();
        }

        sample.VerifiedSleepTargetCount = sample.Elements.Values
            .Where(element => element.IsVerifiedSleepTarget)
            .Select(element => $"{element.ProcessId}|{element.AutomationId}|{element.Name}|{element.ControlTypeId}")
            .Concat(sample.SleepCandidates
                .Where(candidate => candidate.IsVerified)
                .Select(candidate => $"{candidate.ProcessId}|{candidate.AutomationId}|{candidate.Name}|{candidate.ControlTypeId}"))
            .Distinct(StringComparer.Ordinal)
            .Count();
        return sample;
    }

    private static void CaptureCursorAndFocus(SampleRecord sample)
    {
        sample.CursorProbeAttempts++;
        if (!GetCursorPos(out NativePoint cursor))
        {
            AddNativeWindowError(sample, "GetCursorPos", Marshal.GetLastWin32Error());
        }
        else
        {
            sample.CursorWindowSummary = "not returned";
            IntPtr nativeWindow = WindowFromPoint(cursor);
            IntPtr rootWindow = nativeWindow == IntPtr.Zero
                ? IntPtr.Zero
                : GetAncestor(nativeWindow, RootAncestor);
            if (rootWindow == IntPtr.Zero)
                rootWindow = nativeWindow;

            if (rootWindow != IntPtr.Zero)
            {
                NativeWindowInfo? shellWindow = TryReadShellWindow(rootWindow, sample, "WindowFromPoint root");
                sample.CursorWindowSummary = shellWindow is null
                    ? "non-target window (identity redacted)"
                    : $"Process={shellWindow.ProcessName}; PID={shellWindow.ProcessId}; " +
                      $"WindowClass={Escape(shellWindow.ClassName)}; HWND=0x{shellWindow.Handle.ToInt64():X}";

                if (shellWindow is not null)
                {
                    try
                    {
                        AutomationElement windowRoot = AutomationElement.FromHandle(rootWindow);
                        UiNodeInfo rootInfo = ReadNode(windowRoot, sample);
                        sample.CursorWindowUiaRoot = FormatUiaRootIdentity(rootInfo, []);
                        if (sample.Number % CursorRootSearchIntervalSamples == 1)
                            CaptureCursorWindowRootSleepSearch(windowRoot, rootInfo, sample);
                    }
                    catch (Exception ex)
                    {
                        AddReadError(sample, ex, "AutomationElement.FromHandle/WindowFromPoint root");
                    }
                }
            }

            try
            {
                AutomationElement cursorElement = AutomationElement.FromPoint(
                    new System.Windows.Point(cursor.X, cursor.Y));
                sample.CursorProbeSuccesses++;
                CaptureInteractiveElement(cursorElement, "Mouse cursor: FromPoint", sample);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "AutomationElement.FromPoint/current cursor");
            }
        }

        sample.FocusProbeAttempts++;
        try
        {
            AutomationElement focusedElement = AutomationElement.FocusedElement;
            sample.FocusProbeSuccesses++;
            CaptureInteractiveElement(focusedElement, "Keyboard focus: GetFocusedElement", sample);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "AutomationElement.FocusedElement");
        }
    }

    private static void CaptureInteractiveElement(
        AutomationElement element,
        string source,
        SampleRecord sample)
    {
        UiNodeInfo info;
        try
        {
            info = ReadNode(element, sample);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, $"{source}/ReadNode");
            return;
        }

        bool isSleepCandidate = IsSleepCandidate(info.Name);
        bool isPowerAction = TryClassifyPowerAction(info.Name, out string action);
        bool anyStartContext = false;
        bool anyPowerContext = false;

        foreach ((string viewName, TreeWalker walker) in new[]
                 {
                     ("Control View", TreeWalker.ControlViewWalker),
                     ("Raw View", TreeWalker.RawViewWalker)
                 })
        {
            List<AncestorReference> ancestors = ReadAncestorChain(element, walker, viewName, source, sample);
            UiNodeInfo[] ancestorInfo = ancestors.Select(item => item.Info).ToArray();
            bool inStartContext = IsStartContextNode(info, sample) ||
                                  ancestorInfo.Any(node => IsStartContextNode(node, sample));
            bool inPowerContext = IsPowerContextNode(info) ||
                                  ancestorInfo.Any(IsPowerContextNode) ||
                                  ancestorInfo.Any(IsPowerOptionsNode) ||
                                  IsOpenedMarker(info.Name) || IsOpenedMarker(info.AutomationId) ||
                                  IsOpenedMarker(info.ClassName) ||
                                  ancestorInfo.Any(node => IsOpenedMarker(node.Name) ||
                                      IsOpenedMarker(node.AutomationId) || IsOpenedMarker(node.ClassName));
            bool relevant = isSleepCandidate || inPowerContext || (isPowerAction && inStartContext);
            anyStartContext |= inStartContext;
            anyPowerContext |= inPowerContext;

            if (relevant)
            {
                string parentChain = FormatSanitizedParentChain(ancestorInfo);
                string rootIdentity = FormatUiaRootIdentity(info, ancestorInfo);
                bool? invokeSupported = null;
                if (!isSleepCandidate)
                {
                    bool patternsRead = TryReadPatterns(element, sample, out string[] hitPatterns);
                    if (patternsRead)
                        invokeSupported = hitPatterns.Any(pattern => pattern.Contains("InvokePattern", StringComparison.OrdinalIgnoreCase));
                }
                sample.InteractiveHitObservations.Add(new InteractiveHitObservation(
                    sample.Number,
                    sample.CapturedAt,
                    source,
                    viewName,
                    info.Name,
                    info.AutomationId,
                    info.ControlType,
                    info.ControlTypeId,
                    info.ProcessName,
                    info.ProcessId,
                    info.IsEnabled,
                    !info.IsOffscreen,
                    invokeSupported,
                    rootIdentity,
                    parentChain));

                if (isSleepCandidate)
                {
                    RecordSleepCandidate(
                        element,
                        info,
                        $"{source} / {viewName}",
                        ancestorInfo,
                        rootIdentity,
                        sample);
                }
                else if (inStartContext || inPowerContext)
                {
                    AddRelevantElement(
                        element,
                        info,
                        $"{source} / {viewName}",
                        isPowerAction ? $"Interactive hit in Start/power context: {action}" : "Interactive hit in Start/power context",
                        isPowerAction ? action : string.Empty,
                        ancestorInfo,
                        sample,
                        parentChain);
                }
            }

            CaptureScopedSleepSearch(element, info, ancestors, walker, viewName, source, sample);
        }

        sample.InteractiveHitSummaries[source] =
            $"{FormatPointHitTestKind(info)}; StartContext={anyStartContext}; " +
            $"PowerContext={anyPowerContext}; SleepName={isSleepCandidate}";
    }

    private static List<AncestorReference> ReadAncestorChain(
        AutomationElement element,
        TreeWalker walker,
        string viewName,
        string source,
        SampleRecord sample)
    {
        var reversed = new List<AncestorReference>();
        AutomationElement? current;
        try
        {
            current = walker.GetParent(element);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, $"{source}/{viewName}/TreeWalker.GetParent");
            return reversed;
        }

        for (int depth = 0; current is not null && depth < MaxDepth; depth++)
        {
            UiNodeInfo info;
            try
            {
                info = ReadNode(current, sample);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, $"{source}/{viewName}/ReadNode ancestor");
                break;
            }

            reversed.Add(new AncestorReference(current, info));
            try
            {
                current = walker.GetParent(current);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, $"{source}/{viewName}/TreeWalker.GetParent");
                break;
            }
        }

        reversed.Reverse();
        return reversed;
    }

    private static void CaptureScopedSleepSearch(
        AutomationElement hitElement,
        UiNodeInfo hitInfo,
        IReadOnlyList<AncestorReference> ancestors,
        TreeWalker walker,
        string viewName,
        string source,
        SampleRecord sample)
    {
        AncestorReference? scope = ancestors.LastOrDefault(item =>
            IsPowerOptionsNode(item.Info) || IsOpenedMarker(item.Info.Name) ||
            IsOpenedMarker(item.Info.AutomationId) || IsOpenedMarker(item.Info.ClassName));
        if (scope is null &&
            (IsPowerOptionsNode(hitInfo) || IsOpenedMarker(hitInfo.Name) ||
             IsOpenedMarker(hitInfo.AutomationId) || IsOpenedMarker(hitInfo.ClassName)))
        {
            scope = new AncestorReference(hitElement, hitInfo);
        }

        if (scope is null)
            return;

        string scopeKey = $"{scope.Info.ProcessId}|{scope.Info.NativeWindowHandle}|{scope.Info.AutomationId}|{scope.Info.Name}";
        if (!sample.ScopedSleepSearchKeys.Add(scopeKey))
            return;

        sample.ScopedSleepSearchAttempts++;
        try
        {
            Condition[] labels = new[] { "Sleep", "Uyku" }
                .Select(label => (Condition)new PropertyCondition(
                    AutomationElement.NameProperty,
                    label,
                    PropertyConditionFlags.IgnoreCase))
                .ToArray();
            AutomationElementCollection matches = scope.Element.FindAll(
                TreeScope.Descendants,
                new OrCondition(labels));
            sample.ScopedSleepSearchMatches += matches.Count;

            int cappedCount = Math.Min(matches.Count, MaxScopedSleepMatches);
            for (int index = 0; index < cappedCount; index++)
            {
                AutomationElement match = matches[index];
                UiNodeInfo info;
                try
                {
                    info = ReadNode(match, sample);
                }
                catch (Exception ex)
                {
                    AddReadError(sample, ex, $"{source}/{viewName}/scoped Sleep search/ReadNode");
                    continue;
                }

                if (!IsSleepCandidate(info.Name))
                    continue;

                List<AncestorReference> matchAncestors = ReadAncestorChain(
                    match, walker, viewName, $"{source}/scoped Sleep search", sample);
                UiNodeInfo[] ancestorInfo = matchAncestors.Select(item => item.Info).ToArray();
                RecordSleepCandidate(
                    match,
                    info,
                    $"{source} / scoped power-menu subtree / {viewName}",
                    ancestorInfo,
                    FormatUiaRootIdentity(info, ancestorInfo),
                    sample);
            }

            if (matches.Count > MaxScopedSleepMatches)
                sample.ScopedSleepSearchCappedMatches += matches.Count - MaxScopedSleepMatches;
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, $"{source}/{viewName}/scoped power-menu Sleep search");
        }
    }

    private static void RecordSleepCandidate(
        AutomationElement element,
        UiNodeInfo info,
        string source,
        IReadOnlyList<UiNodeInfo> ancestors,
        string rootIdentity,
        SampleRecord sample)
    {
        if (!IsSleepCandidate(info.Name))
            return;

        SleepTargetValidation validation = ValidateSleepTarget(info, ancestors, sample);
        bool patternsRead = TryReadPatterns(element, sample, out string[] patterns);
        bool? invokeSupported = patternsRead
            ? patterns.Any(pattern => pattern.Contains("InvokePattern", StringComparison.OrdinalIgnoreCase))
            : null;
        string parentChain = FormatSanitizedParentChain(ancestors);
        var candidate = new SleepCandidateObservation(
            sample.Number,
            sample.CapturedAt,
            source,
            info.Name,
            info.AutomationId,
            info.ControlType,
            info.ControlTypeId,
            info.ProcessName,
            info.ProcessId,
            rootIdentity,
            parentChain,
            validation.InStartContext,
            validation.IsMenuItem,
            validation.HasOpenedAncestor,
            validation.HasPowerOptionsAncestor,
            validation.HierarchyMatches,
            validation.ProviderSessionId,
            validation.SameInteractiveSession,
            validation.IsVerified,
            info.IsEnabled,
            !info.IsOffscreen,
            invokeSupported,
            patterns);
        sample.SleepCandidates.Add(candidate);
        sample.SleepCandidateDiscoveryCount++;
        if (validation.HierarchyMatches)
            sample.SleepCandidateHierarchyMatchCount++;
        if (validation.IsVerified)
            sample.SleepCandidateVerifiedCount++;
    }

    private static string FormatUiaRootIdentity(UiNodeInfo target, IReadOnlyList<UiNodeInfo> ancestors)
    {
        UiNodeInfo root = ancestors.FirstOrDefault(node => node.ControlType == "ControlType.Window") ??
                          (ancestors.Count > 0 ? ancestors[0] : target);
        string name = IsStartContextNode(root) || IsPowerContextNode(root) || IsPowerOptionsNode(root)
            ? Escape(root.Name)
            : "(name redacted)";
        string process = IsKnownShellProcess(root.ProcessName)
            ? root.ProcessName
            : "(process redacted)";
        return $"topmost accessible ancestor: {name} [{root.ControlType}; Process={process}; " +
               $"PID={root.ProcessId}; AutomationId={Escape(root.AutomationId)}; HWND=0x{root.NativeWindowHandle:X}]";
    }

    private static bool IsSleepCandidate(string name) => Normalize(name) is "sleep" or "uyku";

    private static void CaptureCursorWindowRootSleepSearch(
        AutomationElement windowRoot,
        UiNodeInfo rootInfo,
        SampleRecord sample)
    {
        sample.ScopedSleepSearchAttempts++;
        try
        {
            Condition[] labels = new[] { "Sleep", "Uyku" }
                .Select(label => (Condition)new PropertyCondition(
                    AutomationElement.NameProperty,
                    label,
                    PropertyConditionFlags.IgnoreCase))
                .ToArray();
            AutomationElementCollection matches = windowRoot.FindAll(
                TreeScope.Descendants,
                new OrCondition(labels));
            sample.ScopedSleepSearchMatches += matches.Count;

            int cappedCount = Math.Min(matches.Count, MaxScopedSleepMatches);
            for (int index = 0; index < cappedCount; index++)
            {
                AutomationElement match = matches[index];
                UiNodeInfo info;
                try
                {
                    info = ReadNode(match, sample);
                }
                catch (Exception ex)
                {
                    AddReadError(sample, ex, "WindowFromPoint UIA root/Control View Sleep search/ReadNode");
                    continue;
                }

                if (!IsSleepCandidate(info.Name))
                    continue;

                List<AncestorReference> ancestors = ReadAncestorChain(
                    match, TreeWalker.ControlViewWalker, "Control View", "WindowFromPoint UIA root search", sample);
                UiNodeInfo[] ancestorInfo = ancestors.Select(item => item.Info).ToArray();
                RecordSleepCandidate(
                    match,
                    info,
                    "WindowFromPoint UIA root / Control View FindAll",
                    ancestorInfo,
                    FormatUiaRootIdentity(rootInfo, []),
                    sample);
            }

            if (matches.Count > MaxScopedSleepMatches)
                sample.ScopedSleepSearchCappedMatches += matches.Count - MaxScopedSleepMatches;
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "WindowFromPoint UIA root/Control View Sleep search");
        }

        int visited = 0;
        WalkCursorRootRawForSleep(
            windowRoot,
            rootInfo,
            [],
            sample,
            ref visited,
            depth: 0);
        sample.CursorRootRawNodesVisited += visited;
    }

    private static void WalkCursorRootRawForSleep(
        AutomationElement element,
        UiNodeInfo info,
        IReadOnlyList<UiNodeInfo> ancestors,
        SampleRecord sample,
        ref int visited,
        int depth)
    {
        if (depth > MaxCursorRootRawDepth || visited >= MaxCursorRootRawNodes)
            return;

        visited++;
        if (IsSleepCandidate(info.Name))
        {
            RecordSleepCandidate(
                element,
                info,
                "WindowFromPoint UIA root / bounded Raw View walk",
                ancestors,
                FormatUiaRootIdentity(info, ancestors),
                sample);
        }

        if (depth >= MaxCursorRootRawDepth || visited >= MaxCursorRootRawNodes)
            return;

        AutomationElement? child;
        try
        {
            child = TreeWalker.RawViewWalker.GetFirstChild(element);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "WindowFromPoint UIA root/Raw View GetFirstChild");
            return;
        }

        while (child is not null && visited < MaxCursorRootRawNodes)
        {
            UiNodeInfo childInfo;
            try
            {
                childInfo = ReadNode(child, sample);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "WindowFromPoint UIA root/Raw View ReadNode");
                try
                {
                    child = TreeWalker.RawViewWalker.GetNextSibling(child);
                }
                catch (Exception siblingEx)
                {
                    AddReadError(sample, siblingEx, "WindowFromPoint UIA root/Raw View GetNextSibling");
                    return;
                }
                continue;
            }

            var childAncestors = new List<UiNodeInfo>(ancestors) { info };
            WalkCursorRootRawForSleep(child, childInfo, childAncestors, sample, ref visited, depth + 1);
            try
            {
                child = TreeWalker.RawViewWalker.GetNextSibling(child);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "WindowFromPoint UIA root/Raw View GetNextSibling");
                return;
            }
        }
    }

    private static Dictionary<string, RootReference> CollectRoots(
        TreeWalker walker,
        IReadOnlyList<NativeWindowInfo> nativeWindows,
        SampleRecord sample)
    {
        var roots = new Dictionary<string, RootReference>(StringComparer.Ordinal);

        try
        {
            AutomationElement desktop = AutomationElement.RootElement;
            AutomationElement? child = walker.GetFirstChild(desktop);
            while (child is not null)
            {
                try
                {
                    UiNodeInfo info = ReadNode(child, sample);
                    if (IsEligibleShellRoot(info))
                        AddRoot(roots, child, info, "UIA desktop tree");
                }
                catch (Exception ex)
                {
                    AddReadError(sample, ex, "ReadNode/desktop-root");
                }

                try
                {
                    child = walker.GetNextSibling(child);
                }
                catch (Exception ex)
                {
                    AddReadError(sample, ex, "TreeWalker.GetNextSibling/desktop");
                    break;
                }
            }
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "TreeWalker.GetFirstChild/desktop");
        }

        foreach (NativeWindowInfo nativeWindow in nativeWindows)
        {
            if (!nativeWindow.IsVisible)
                continue;

            try
            {
                AutomationElement element = AutomationElement.FromHandle(nativeWindow.Handle);
                UiNodeInfo info = ReadNode(element, sample) with
                {
                    NativeWindowHandle = unchecked((int)nativeWindow.Handle.ToInt64()),
                    NativeWindowVisible = nativeWindow.IsVisible
                };
                bool isStartRelatedPopup = string.Equals(
                    nativeWindow.DiscoverySource,
                    "Start-owned popup (root-owner matched foreground)",
                    StringComparison.Ordinal);
                if (IsEligibleShellRoot(info) || isStartRelatedPopup)
                    AddRoot(roots, element, info, nativeWindow.DiscoverySource, isStartRelatedPopup);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "AutomationElement.FromHandle");
            }
        }

        return roots;
    }

    private static void Walk(
        AutomationElement element,
        UiNodeInfo info,
        string viewName,
        TreeWalker walker,
        List<UiNodeInfo> ancestors,
        SampleRecord sample,
        ref int visited,
        int depth,
        bool isStartRelatedPopupRoot)
    {
        if (depth > MaxDepth || visited >= MaxNodesPerView)
            return;

        visited++;
        var chain = new List<UiNodeInfo>(ancestors) { info };
        bool inStartContext = isStartRelatedPopupRoot || chain.Any(IsStartContextNode);
        bool inPowerContext = ancestors.Any(IsPowerOptionsNode);
        bool isPowerContextNode = IsPowerContextNode(info);

        if (inStartContext && isPowerContextNode)
            AddRelevantElement(element, info, viewName, "Power menu context", string.Empty, ancestors, sample);

        if (TryClassifyPowerAction(info.Name, out string action))
        {
            if (IsWindowCloseControl(info, ancestors))
            {
                sample.CloseControlsExcluded++;
            }
            else if (inStartContext)
            {
                if (action == "Sleep")
                    RecordSleepCandidate(
                        element, info, $"Shell tree walker / {viewName}", ancestors,
                        FormatUiaRootIdentity(info, ancestors), sample);

                SleepTargetValidation? sleepValidation = action == "Sleep"
                    ? ValidateSleepTarget(info, ancestors, sample)
                    : null;
                string role = sleepValidation?.HierarchyMatches == true
                    ? sleepValidation.IsVerified
                        ? "Verified Sleep MenuItem"
                        : "Sleep MenuItem; session unverified"
                    : inPowerContext
                        ? $"Power label (not a Sleep target): {action}"
                        : isStartRelatedPopupRoot
                            ? $"Start-owned popup label (not a Sleep target): {action}"
                            : $"Unconfirmed Start label (not a Sleep target): {action}";
                string? safeParentChain = sleepValidation?.HierarchyMatches == true
                    ? FormatSanitizedParentChain(ancestors)
                    : null;
                AddRelevantElement(
                    element,
                    info,
                    viewName,
                    role,
                    action,
                    ancestors,
                    sample,
                    safeParentChain,
                    isVerifiedSleepTarget: sleepValidation?.IsVerified == true);
            }
            else
            {
                sample.ActionLabelsOutsideStartContext++;
            }
        }

        if (depth >= MaxDepth || visited >= MaxNodesPerView)
            return;

        AutomationElement? child;
        try
        {
            child = walker.GetFirstChild(element);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, $"{viewName}/TreeWalker.GetFirstChild");
            return;
        }

        while (child is not null && visited < MaxNodesPerView)
        {
            UiNodeInfo childInfo;
            try
            {
                childInfo = ReadNode(child, sample);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, $"{viewName}/ReadNode");
                try
                {
                    child = walker.GetNextSibling(child);
                }
                catch (Exception siblingEx)
                {
                    AddReadError(sample, siblingEx, $"{viewName}/TreeWalker.GetNextSibling");
                    return;
                }
                continue;
            }

            Walk(
                child, childInfo, viewName, walker, chain, sample,
                ref visited, depth + 1, isStartRelatedPopupRoot);

            try
            {
                child = walker.GetNextSibling(child);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, $"{viewName}/TreeWalker.GetNextSibling");
                return;
            }
        }
    }

    private static void AddRelevantElement(
        AutomationElement element,
        UiNodeInfo info,
        string viewName,
        string role,
        string action,
        IReadOnlyList<UiNodeInfo> ancestors,
        SampleRecord sample,
        string? parentChainOverride = null,
        bool isVerifiedSleepTarget = false)
    {
        bool patternsRead = TryReadPatterns(element, sample, out string[] patterns);
        bool? supportsInvoke = patternsRead
            ? patterns.Any(pattern => pattern.Contains("InvokePattern", StringComparison.OrdinalIgnoreCase))
            : null;

        string parentChain = parentChainOverride ?? FormatParentChain(ancestors);
        string runtimeId = GetRuntimeId(element, sample);
        int? providerSessionId = GetProcessSessionId(info.ProcessId, sample);
        bool? sameInteractiveSession = providerSessionId.HasValue
            ? providerSessionId.Value == sample.InteractiveSessionId
            : null;
        string key = string.Join(
            "|",
            viewName,
            info.ProcessId.ToString(CultureInfo.InvariantCulture),
            providerSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown",
            sameInteractiveSession?.ToString(CultureInfo.InvariantCulture) ?? "unknown",
            info.NativeWindowHandle.ToString("X", CultureInfo.InvariantCulture),
            runtimeId,
            role,
            info.ControlType,
            info.ControlTypeId.ToString(CultureInfo.InvariantCulture),
            info.AutomationId,
            parentChain,
            info.Name,
            info.IsKeyboardFocusable.ToString(CultureInfo.InvariantCulture),
            isVerifiedSleepTarget.ToString(CultureInfo.InvariantCulture));

        var observation = new RelevantElement(
            key,
            sample.Number,
            sample.CapturedAt,
            viewName,
            role,
            action,
            info.Name,
            info.AutomationId,
            info.ControlType,
            info.ClassName,
            info.ProcessName,
            info.ProcessId,
            info.ControlTypeId,
            info.IsKeyboardFocusable,
            providerSessionId,
            sample.InteractiveSessionId,
            sameInteractiveSession,
            info.NativeWindowHandle,
            parentChain,
            !info.IsOffscreen,
            info.IsOffscreen,
            info.IsEnabled,
            info.NativeWindowVisible,
            supportsInvoke,
            patterns,
            isVerifiedSleepTarget);
        sample.Elements.TryAdd(key, observation);
    }

    private static bool TryReadPatterns(AutomationElement element, SampleRecord sample, out string[] patterns)
    {
        try
        {
            patterns = element.GetSupportedPatterns()
                .Select(pattern => pattern.ProgrammaticName)
                .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
                .ToArray();
            return true;
        }
        catch (Exception ex)
        {
            patterns = [];
            AddReadError(sample, ex, "AutomationElement.GetSupportedPatterns");
            return false;
        }
    }

    private static UiNodeInfo ReadNode(AutomationElement element, SampleRecord sample)
    {
        AutomationElement.AutomationElementInformation current = element.Current;
        int processId = current.ProcessId;
        int nativeHandle = current.NativeWindowHandle;
        bool? nativeVisible = nativeHandle == 0
            ? null
            : IsWindowVisible(new IntPtr(nativeHandle));

        return new UiNodeInfo(
            current.Name ?? string.Empty,
            current.AutomationId ?? string.Empty,
            current.ControlType?.ProgrammaticName ?? string.Empty,
            current.ClassName ?? string.Empty,
            GetProcessName(processId, sample, "UIA.ProcessId"),
            processId,
            nativeHandle,
            current.IsEnabled,
            current.IsKeyboardFocusable,
            current.ControlType?.Id ?? 0,
            current.IsOffscreen,
            nativeVisible);
    }

    private static List<NativeWindowInfo> EnumerateShellWindows(SampleRecord sample)
    {
        var windows = new List<NativeWindowInfo>();
        EnumWindowsCallback callback = (handle, _) =>
        {
            sample.TopLevelWindowCount++;
            if (IsWindowVisible(handle))
                sample.TopLevelVisibleWindowCount++;

            try
            {
                NativeWindowInfo? window = TryReadShellWindow(handle, sample, "EnumWindows");
                if (window is not null)
                    windows.Add(window);
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "EnumWindows.callback");
            }

            return true;
        };

        if (!EnumWindows(callback, IntPtr.Zero))
            AddNativeWindowError(sample, "EnumWindows", Marshal.GetLastWin32Error());

        GC.KeepAlive(callback);
        return windows;
    }

    private static NativeWindowInfo? TryReadShellWindow(IntPtr handle, SampleRecord sample, string operation)
    {
        if (handle == IntPtr.Zero)
            return null;

        uint threadId = GetWindowThreadProcessId(handle, out uint rawProcessId);
        if (threadId == 0 || rawProcessId == 0)
        {
            AddNativeWindowError(sample, $"{operation}.GetWindowThreadProcessId", Marshal.GetLastWin32Error());
            return null;
        }

        int processId = unchecked((int)rawProcessId);
        string processName = GetProcessName(processId, sample, $"{operation}.ProcessLookup");
        if (!IsKnownShellProcess(processName))
            return null;

        var className = new StringBuilder(256);
        int classLength = GetClassName(handle, className, className.Capacity);
        if (classLength == 0)
        {
            AddNativeWindowError(sample, $"{operation}.GetClassName", Marshal.GetLastWin32Error());
            return null;
        }

        string windowClass = className.ToString();
        if (!IsAllowedShellClass(processName, windowClass))
            return null;

        return new NativeWindowInfo(
            handle,
            processId,
            processName,
            windowClass,
            IsWindowVisible(handle),
            IsForeground: false,
            DiscoverySource: "EnumWindows");
    }

    private static ForegroundWindowInfo ReadForegroundWindow(SampleRecord sample)
    {
        try
        {
            IntPtr handle = GetForegroundWindow();
            if (handle == IntPtr.Zero)
                return ForegroundWindowInfo.None;

            NativeWindowInfo? shellWindow = TryReadShellWindow(handle, sample, "GetForegroundWindow");
            if (shellWindow is null)
            {
                // Keep the HWND for comparison, but do not disclose identity or class for unrelated windows.
                return new ForegroundWindowInfo(handle, false, null, null, null, IsWindowVisible(handle));
            }

            return new ForegroundWindowInfo(
                shellWindow.Handle,
                true,
                shellWindow.ProcessName,
                shellWindow.ProcessId,
                shellWindow.ClassName,
                shellWindow.IsVisible);
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "GetForegroundWindow");
            return ForegroundWindowInfo.None;
        }
    }

    private static List<StartOwnedPopupInfo> EnumerateStartOwnedPopups(
        SampleRecord sample,
        ForegroundWindowInfo? foregroundWindow)
    {
        if (foregroundWindow is not
            {
                IsTargetShellWindow: true,
                ProcessName: not null,
                IsVisible: true
            } foreground ||
            !StartHostProcesses.Contains(foreground.ProcessName))
        {
            return [];
        }

        var popups = new List<StartOwnedPopupInfo>();
        IntPtr foregroundRootOwner = GetAncestor(foreground.Handle, RootOwnerAncestor);
        EnumWindowsCallback callback = (handle, _) =>
        {
            if (handle == foreground.Handle || !IsWindowVisible(handle))
                return true;

            IntPtr owner = GetWindow(handle, PopupOwner);
            IntPtr rootOwner = GetAncestor(handle, RootOwnerAncestor);
            if (owner == IntPtr.Zero ||
                (owner != foreground.Handle &&
                 owner != foregroundRootOwner &&
                 rootOwner != foreground.Handle &&
                 rootOwner != foregroundRootOwner))
            {
                return true;
            }

            try
            {
                uint threadId = GetWindowThreadProcessId(handle, out uint rawProcessId);
                if (threadId == 0 || rawProcessId == 0)
                {
                    AddNativeWindowError(sample, "Start-owned popup.GetWindowThreadProcessId", Marshal.GetLastWin32Error());
                    return true;
                }

                var className = new StringBuilder(256);
                int classLength = GetClassName(handle, className, className.Capacity);
                if (classLength == 0)
                {
                    AddNativeWindowError(sample, "Start-owned popup.GetClassName", Marshal.GetLastWin32Error());
                    return true;
                }

                int processId = unchecked((int)rawProcessId);
                string processName = GetProcessName(processId, sample, "Start-owned popup.ProcessLookup");
                popups.Add(new StartOwnedPopupInfo(
                    handle,
                    processId,
                    processName,
                    className.ToString(),
                    IsVisible: true,
                    owner,
                    rootOwner));
            }
            catch (Exception ex)
            {
                AddReadError(sample, ex, "Start-owned popup enumeration");
            }

            return true;
        };

        if (!EnumWindows(callback, IntPtr.Zero))
            AddNativeWindowError(sample, "EnumWindows/Start-owned popups", Marshal.GetLastWin32Error());

        GC.KeepAlive(callback);
        return popups;
    }

    private static SleepTargetValidation ValidateSleepTarget(
        UiNodeInfo element,
        IReadOnlyList<UiNodeInfo> ancestors,
        SampleRecord sample)
    {
        bool isSleepName = Normalize(element.Name) is "sleep" or "uyku";
        bool isMenuItem = element.ControlTypeId == ControlType.MenuItem.Id;
        bool inStartContext = IsStartContextNode(element, sample) ||
                              ancestors.Any(node => IsStartContextNode(node, sample));
        bool hasOpenedAncestor = ancestors.Any(node =>
            IsOpenedMarker(node.Name) || IsOpenedMarker(node.AutomationId) || IsOpenedMarker(node.ClassName));
        bool hasPowerOptionsAncestor = ancestors.Any(IsPowerOptionsNode);
        bool hierarchyMatches = isSleepName && isMenuItem && inStartContext &&
                                hasOpenedAncestor && hasPowerOptionsAncestor;

        int? providerSessionId = element.ProcessId > 0
            ? GetProcessSessionId(element.ProcessId, sample)
            : null;
        bool? sameInteractiveSession = providerSessionId.HasValue
            ? providerSessionId.Value == sample.InteractiveSessionId
            : null;
        return new SleepTargetValidation(
            isSleepName,
            isMenuItem,
            inStartContext,
            hasOpenedAncestor,
            hasPowerOptionsAncestor,
            hierarchyMatches,
            providerSessionId,
            sameInteractiveSession);
    }

    private static string FormatPointHitTestKind(UiNodeInfo info)
    {
        bool isStartHost = StartHostProcesses.Contains(info.ProcessName);
        bool isKnownExplorerShellWindow =
            string.Equals(info.ProcessName, "explorer", StringComparison.OrdinalIgnoreCase) &&
            ExplorerShellClasses.Contains(info.ClassName);
        string process = isStartHost
            ? info.ProcessName
            : isKnownExplorerShellWindow
                ? "explorer (shell window)"
                : "non-target (identity redacted)";
        string className = isStartHost || isKnownExplorerShellWindow
            ? info.ClassName
            : "(redacted)";
        return $"Process={process}; ControlType={info.ControlType}; ClassName={className}";
    }

    private static string FormatSanitizedParentChain(IReadOnlyList<UiNodeInfo> ancestors)
    {
        if (ancestors.Count == 0)
            return "(no accessible parent)";

        var parts = new List<string>(ancestors.Count);
        foreach (UiNodeInfo node in ancestors)
        {
            bool isContextMarker = IsPowerContextNode(node) || IsPowerOptionsNode(node) ||
                                   IsOpenedMarker(node.Name) || IsOpenedMarker(node.AutomationId) ||
                                   IsOpenedMarker(node.ClassName) || IsStartMarker(node.Name) ||
                                   IsStartMarker(node.AutomationId);
            string label = TryClassifyPowerAction(node.Name, out string action)
                ? action
                : isContextMarker
                    ? Escape(node.Name)
                    : "(name redacted)";
            string automationId = node.AutomationId is "PowerButton" or "NavigationPane" or "PaneRoot" or
                "SplitViewFrameXAMLWindow" or "LightDismiss" ||
                IsPowerContextNode(node) || IsPowerOptionsNode(node) || IsOpenedMarker(node.AutomationId) ||
                IsStartMarker(node.AutomationId)
                ? $", AutomationId={Escape(node.AutomationId)}"
                : string.Empty;
            bool isKnownShell = StartHostProcesses.Contains(node.ProcessName) ||
                                string.Equals(node.ProcessName, "explorer", StringComparison.OrdinalIgnoreCase);
            string process = isKnownShell
                ? $"{node.ProcessName}:{node.ProcessId}"
                : "other process (identity redacted)";
            parts.Add($"{label} [{node.ControlType}{automationId}; {process}]");
        }

        return string.Join(" -> ", parts);
    }

    private static RootWindowInfo CreateRootWindowInfo(
        UiNodeInfo node,
        ForegroundWindowInfo? foregroundWindow,
        IEnumerable<string> discoverySources)
    {
        bool? visible = node.NativeWindowVisible ?? !node.IsOffscreen;
        uint nativeHandle = unchecked((uint)node.NativeWindowHandle);
        bool isForeground = foregroundWindow is { IsTargetShellWindow: true } &&
                            foregroundWindow.Handle.ToInt64() == nativeHandle;
        return new RootWindowInfo(
            node.ProcessName,
            node.ProcessId,
            node.ClassName,
            nativeHandle,
            visible,
            isForeground,
            discoverySources.Distinct(StringComparer.Ordinal).OrderBy(source => source, StringComparer.Ordinal).ToArray());
    }

    private static void AddRoot(
        Dictionary<string, RootReference> roots,
        AutomationElement element,
        UiNodeInfo info,
        string discoverySource,
        bool isStartRelatedPopup = false)
    {
        string key = GetRootKey(info);
        if (roots.TryGetValue(key, out RootReference? existing))
        {
            existing.DiscoverySources.Add(discoverySource);
            existing.IsStartRelatedPopup |= isStartRelatedPopup;
            return;
        }

        roots.Add(key, new RootReference(element, info, discoverySource, isStartRelatedPopup));
    }

    private static bool IsEligibleShellRoot(UiNodeInfo node)
    {
        bool visible = node.NativeWindowVisible ?? !node.IsOffscreen;
        return visible && IsAllowedShellClass(node.ProcessName, node.ClassName);
    }

    private static bool IsAllowedShellClass(string processName, string className)
    {
        if (StartHostProcesses.Contains(processName))
            return true;

        return string.Equals(processName, "explorer", StringComparison.OrdinalIgnoreCase) &&
               ExplorerShellClasses.Contains(className);
    }

    private static bool IsKnownShellProcess(string processName) =>
        StartHostProcesses.Contains(processName) ||
        string.Equals(processName, "explorer", StringComparison.OrdinalIgnoreCase);

    private static bool IsStartContextNode(UiNodeInfo node)
    {
        if (StartHostProcesses.Contains(node.ProcessName))
            return true;

        return IsStartMarker(node.Name) ||
               IsStartMarker(node.AutomationId) ||
               IsStartMarker(node.ClassName);
    }

    private static bool IsStartContextNode(UiNodeInfo node, SampleRecord sample) =>
        IsStartContextNode(node) ||
        (sample.ForegroundStartProcessId.HasValue && node.ProcessId == sample.ForegroundStartProcessId.Value) ||
        sample.StartOwnedPopups.Any(popup =>
            popup.ProcessId == node.ProcessId ||
            unchecked((uint)node.NativeWindowHandle) == unchecked((uint)popup.Handle.ToInt64()));

    private static bool IsStartMarker(string value)
    {
        string normalized = Normalize(value);
        return normalized is "start" or "start menu" or "başlat" or "başlat menüsü" or "başlangıç" or "başlangıç menüsü" ||
               normalized.Contains("startmenu", StringComparison.Ordinal) ||
               normalized.Contains("startbutton", StringComparison.Ordinal) ||
               normalized.Contains("başlat menüsü", StringComparison.Ordinal) ||
               normalized.Contains("başlangıç menüsü", StringComparison.Ordinal);
    }

    private static bool IsPowerContextNode(UiNodeInfo node) =>
        IsPowerMarker(node.Name) ||
        IsPowerMarker(node.AutomationId) ||
        IsPowerMarker(node.ClassName);

    private static bool IsPowerMarker(string value)
    {
        string normalized = Normalize(value);
        string slashNormalized = Normalize(value.Replace('/', ' '));
        return normalized is "power" or "güç" or "guc" or "power menu" or "güç menüsü" or "guc menusu" ||
               slashNormalized is "power options" or "aç kapa seçenekleri" or "güç seçenekleri" or "guc seçenekleri" ||
               normalized.Contains("powermenu", StringComparison.Ordinal) ||
               normalized.Contains("poweroptions", StringComparison.Ordinal) ||
               normalized.Contains("powerflyout", StringComparison.Ordinal) ||
               normalized.Contains("powerbutton", StringComparison.Ordinal) ||
               normalized.Contains("gucmenusu", StringComparison.Ordinal) ||
               normalized.Contains("gucdugmesi", StringComparison.Ordinal);
    }

    private static bool IsPowerOptionsNode(UiNodeInfo node) =>
        IsPowerOptionsMarker(node.Name) ||
        IsPowerOptionsMarker(node.AutomationId) ||
        IsPowerOptionsMarker(node.ClassName);

    private static bool IsPowerOptionsMarker(string value)
    {
        string normalized = Normalize(value.Replace('/', ' '));
        return normalized is "power options" or "power options menu" or "power menu options" or
               "aç kapa seçenekleri" or "güç seçenekleri" or "guc seçenekleri" or "aç kapa menüsü" or
               "güç menüsü" or "guc menusu" ||
               normalized.Contains("power options", StringComparison.Ordinal) ||
               normalized.Contains("aç kapa seçenekleri", StringComparison.Ordinal) ||
               normalized.Contains("güç seçenekleri", StringComparison.Ordinal) ||
               normalized.Contains("guc seçenekleri", StringComparison.Ordinal);
    }

    private static bool IsOpenedMarker(string value)
    {
        string normalized = Normalize(value);
        return normalized is "opened" or "open" or "açılan" or "acilan" or "expanded" or "expanded menu";
    }

    private static bool IsWindowCloseControl(UiNodeInfo node, IReadOnlyList<UiNodeInfo> ancestors)
    {
        if (string.Equals(node.AutomationId, "Close", StringComparison.OrdinalIgnoreCase))
            return true;

        if (node.ControlType.Contains("TitleBar", StringComparison.OrdinalIgnoreCase))
            return true;

        return ancestors.Any(ancestor =>
            ancestor.ControlType.Contains("TitleBar", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(ancestor.AutomationId, "TitleBar", StringComparison.OrdinalIgnoreCase));
    }

    private static bool TryClassifyPowerAction(string name, out string action)
    {
        if (PowerActionLabels.TryGetValue(Normalize(name), out string? classified))
        {
            action = classified;
            return true;
        }

        action = string.Empty;
        return false;
    }

    private static string FormatParentChain(IReadOnlyList<UiNodeInfo> ancestors)
    {
        if (ancestors.Count == 0)
            return "(no accessible parent)";

        int startIndex = ancestors.ToList().FindIndex(node =>
            StartHostProcesses.Contains(node.ProcessName) ||
            IsStartMarker(node.Name) ||
            IsStartMarker(node.AutomationId) ||
            IsStartMarker(node.ClassName));
        if (startIndex < 0)
            startIndex = Math.Max(0, ancestors.Count - 6);

        return string.Join(
            " -> ",
            ancestors.Skip(startIndex).Select(FormatChainNode));
    }

    private static string FormatChainNode(UiNodeInfo node)
    {
        string label = string.IsNullOrWhiteSpace(node.Name)
            ? node.ControlType
            : Escape(node.Name);
        string automationId = string.IsNullOrWhiteSpace(node.AutomationId)
            ? string.Empty
            : $", AutomationId={Escape(node.AutomationId)}";
        return $"{label} [{node.ControlType}{automationId}; {node.ProcessName}:{node.ProcessId}]";
    }

    private static string Normalize(string value) =>
        string.Join(
            ' ',
            value.Normalize(NormalizationForm.FormKC)
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            .ToLowerInvariant();

    private static string GetRuntimeId(AutomationElement element, SampleRecord sample)
    {
        try
        {
            return string.Join(".", element.GetRuntimeId().Select(value => value.ToString("X8", CultureInfo.InvariantCulture)));
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "AutomationElement.GetRuntimeId");
            return string.Empty;
        }
    }

    private static string GetProcessName(int processId, SampleRecord sample, string operation)
    {
        if (processId <= 0)
            return "<unavailable>";

        if (ProcessNames.TryGetValue(processId, out string? cached))
            return cached;

        string name;
        try
        {
            using Process process = Process.GetProcessById(processId);
            name = process.ProcessName;
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, $"{operation}.GetProcessById");
            name = "<unavailable>";
        }

        ProcessNames[processId] = name;
        return name;
    }

    private static int? GetProcessSessionId(int processId, SampleRecord sample)
    {
        if (processId <= 0)
            return null;

        if (ProcessSessionIds.TryGetValue(processId, out int? cached))
            return cached;

        int? sessionId;
        try
        {
            using Process process = Process.GetProcessById(processId);
            sessionId = process.SessionId;
        }
        catch (Exception ex)
        {
            AddReadError(sample, ex, "Process.GetSessionId");
            sessionId = null;
        }

        ProcessSessionIds[processId] = sessionId;
        return sessionId;
    }

    private static string GetRootKey(UiNodeInfo node) =>
        string.Join(
            "|",
            node.ProcessId.ToString(CultureInfo.InvariantCulture),
            node.NativeWindowHandle.ToString("X", CultureInfo.InvariantCulture),
            node.ProcessName,
            node.ClassName);

    private static void AddReadError(SampleRecord sample, Exception exception, string operation)
    {
        string error = exception is System.ComponentModel.Win32Exception win32Exception
            ? $"Win32Error=0x{unchecked((uint)win32Exception.NativeErrorCode):X8}"
            : exception.GetType().Name;
        string key = $"{operation}:{error}";
        sample.ReadErrors[key] = sample.ReadErrors.GetValueOrDefault(key) + 1;
    }

    private static void AddNativeWindowError(SampleRecord sample, string operation, int errorCode)
    {
        string key = $"{operation}:Win32Error=0x{errorCode:X8}";
        sample.ReadErrors[key] = sample.ReadErrors.GetValueOrDefault(key) + 1;
    }

    private static void UpdateHistories(
        SampleRecord sample,
        Dictionary<string, ElementHistory> histories,
        List<PresenceEvent> events)
    {
        foreach (RelevantElement element in sample.Elements.Values)
        {
            if (!histories.TryGetValue(element.Key, out ElementHistory? history))
            {
                history = new ElementHistory(element);
                histories.Add(element.Key, history);
                events.Add(new PresenceEvent("FirstSeen", element.CapturedAt, element.Key, element.Role));
                continue;
            }

            history.Latest = element;
            if (history.IsActive)
            {
                history.CurrentPeriod.LastSeenAt = element.CapturedAt;
                history.CurrentPeriod.ObservationCount++;
            }
            else
            {
                history.StartPeriod(element.CapturedAt);
                events.Add(new PresenceEvent("Reappeared", element.CapturedAt, element.Key, element.Role));
            }
        }

        foreach (ElementHistory history in histories.Values)
        {
            if (!history.IsActive || sample.Elements.ContainsKey(history.Latest.Key))
                continue;

            history.CurrentPeriod.DisappearedAt = sample.CapturedAt;
            events.Add(new PresenceEvent(
                "Disappeared", sample.CapturedAt, history.Latest.Key, history.Latest.Role));
        }
    }

    private static string TrySoundAlert()
    {
        try
        {
            System.Media.SystemSounds.Asterisk.Play();
            return "playback requested (Windows Asterisk system sound)";
        }
        catch (Exception ex)
        {
            return $"failed ({ex.GetType().Name})";
        }
    }

    private static string CreateReportPath(DateTimeOffset endedAt)
    {
        string fileName =
            $"ClassicDesktopScreensaver-PowerMenuUiaProbe-{endedAt:yyyyMMdd-HHmmss-fff}-{Guid.NewGuid():N}.txt";
        return Path.Combine(Path.GetTempPath(), fileName);
    }

    private static string BuildReport(
        DateTimeOffset startedAt,
        DateTimeOffset endedAt,
        int sessionId,
        int processId,
        int durationSeconds,
        int baselineSeconds,
        int intervalMilliseconds,
        IReadOnlyList<SampleRecord> samples,
        IReadOnlyDictionary<string, ElementHistory> histories,
        IReadOnlyList<PresenceEvent> events)
    {
        var report = new StringBuilder();
        report.AppendLine("Classic Desktop Screensaver - Power Menu UIA Probe");
        report.AppendLine("MODE: read-only continuous inspection; no click/input/Invoke/Expand/Collapse/power action.");
        report.AppendLine($"Monitoring start: {FormatTime(startedAt)}");
        report.AppendLine($"Monitoring end: {FormatTime(endedAt)}");
        report.AppendLine($"Duration target: {durationSeconds} seconds");
        report.AppendLine($"Pre-open baseline target: {baselineSeconds} seconds; keep Start closed until the console prompt.");
        report.AppendLine($"Sample interval target: {intervalMilliseconds} ms");
        report.AppendLine($"Completed samples: {samples.Count}");
        report.AppendLine($"Windows session ID: {sessionId}");
        report.AppendLine($"Probe process ID: {processId}");
        report.AppendLine($"Process architecture: {(Environment.Is64BitProcess ? "64-bit" : "32-bit")}");
        report.AppendLine("Scope: shell roots plus direct UIA targeting from the live cursor, focused element, and the native window under the cursor; direct targeting is not restricted to StartMenuExperienceHost.");
        report.AppendLine("Live targeting: each sample reads GetCursorPos -> AutomationElement.FromPoint and AutomationElement.FocusedElement; no mouse or keyboard input is sent.");
        report.AppendLine("Context search: exact Sleep/Uyku names are queried only below an accessible Opened or Power-options ancestor found from the cursor/focus chain, with a result cap.");
        report.AppendLine("Candidate discovery is reported independently from the stricter Start/Open/Power-options/MenuItem/session validation.");
        report.AppendLine("No desktop-wide descendant search or coordinate grid is used. Name searches remain limited to the native window under the cursor or an observed Opened/Power-options subtree.");
        report.AppendLine("Privacy: window titles, unrelated process identities, Explorer file lists, and unrelated UI names are not written.");
        AppendWindowComparisons(report, samples);
        AppendRootComparisons(report, samples);
        AppendStartOwnedPopupComparisons(report, samples);
        AppendInteractiveProbeSummary(report, samples);
        AppendAccessErrorSummary(report, samples);
        report.AppendLine();
        report.AppendLine("Presence events:");
        if (events.Count == 0)
            report.AppendLine("(no in-scope elements observed)");
        foreach (PresenceEvent item in events)
        {
            report.AppendLine(
                $"{FormatTime(item.At)} {item.Kind}: role={Escape(item.Role)}; element={item.Key}");
        }

        report.AppendLine();
        report.AppendLine("Presence periods:");
        if (histories.Count == 0)
            report.AppendLine("(no in-scope elements observed)");
        foreach (ElementHistory history in histories.Values.OrderBy(item => item.FirstSeen))
        {
            report.AppendLine($"Element: {FormatElement(history.Latest)}");
            foreach (PresencePeriod period in history.Periods)
            {
                report.AppendLine(
                    $"  FirstSeen={FormatTime(period.FirstSeenAt)}; " +
                    $"LastSeen={FormatTime(period.LastSeenAt)}; " +
                    $"DisappearedAt={(period.DisappearedAt is null ? "(still present at end)" : FormatTime(period.DisappearedAt.Value))}; " +
                    $"Samples={period.ObservationCount}");
            }
        }

        report.AppendLine();
        report.AppendLine("Per-sample observations:");
        foreach (SampleRecord sample in samples)
        {
            report.AppendLine(
                $"Sample {sample.Number}: {FormatTime(sample.CapturedAt)}; " +
                $"phase={sample.Phase}; " +
                $"scan={sample.ScanMilliseconds} ms; " +
                $"ControlView roots/nodes={sample.RootsVisited.GetValueOrDefault("Control View")}/{sample.NodesVisited.GetValueOrDefault("Control View")}; " +
                $"RawView roots/nodes={sample.RootsVisited.GetValueOrDefault("Raw View")}/{sample.NodesVisited.GetValueOrDefault("Raw View")}; " +
                $"EnumWindows top-level/visible={sample.TopLevelWindowCount}/{sample.TopLevelVisibleWindowCount}; " +
                $"EnumWindows shell windows={sample.ShellWindows.Count} (visible={sample.ShellWindows.Count(window => window.IsVisible)}); " +
                $"foreground-in-EnumWindows={FormatNullableBoolean(sample.ForegroundWindowInEnumWindows)}; " +
                $"foreground={FormatForegroundWindow(sample.ForegroundWindow)}; " +
                $"Start-owned popups={sample.StartOwnedPopups.Count}; " +
                $"WindowFromPoint root={sample.CursorWindowSummary ?? "not available"}; " +
                $"window UIA root={sample.CursorWindowUiaRoot ?? "not queried"}; " +
                $"cursor UIA={sample.InteractiveHitSummaries.GetValueOrDefault("Mouse cursor: FromPoint", "not available")}; " +
                $"focus UIA={sample.InteractiveHitSummaries.GetValueOrDefault("Keyboard focus: GetFocusedElement", "not available")}; " +
                $"scoped Sleep searches/matches={sample.ScopedSleepSearchAttempts}/{sample.ScopedSleepSearchMatches}; " +
                $"Sleep candidate observations/hierarchy matches/verified={sample.SleepCandidateDiscoveryCount}/{sample.SleepCandidateHierarchyMatchCount}/{sample.SleepCandidateVerifiedCount}; " +
                $"cursor-root Raw nodes={sample.CursorRootRawNodesVisited}; capped matches={sample.ScopedSleepSearchCappedMatches}; " +
                $"verified Sleep targets={sample.VerifiedSleepTargetCount}; " +
                $"relevant={sample.Elements.Count}; " +
                $"excluded window-close labels={sample.CloseControlsExcluded}; " +
                $"unconfirmed labels outside Start context={sample.ActionLabelsOutsideStartContext}.");

            foreach (RelevantElement element in sample.Elements.Values)
                report.AppendLine($"  {FormatElement(element)}");

            foreach (SleepCandidateObservation candidate in sample.SleepCandidates)
                report.AppendLine($"  {FormatSleepCandidate(candidate)}");

            if (sample.ReadErrors.Count > 0)
            {
                string errors = string.Join(
                    ", ",
                    sample.ReadErrors.OrderBy(pair => pair.Key)
                        .Select(pair => $"{pair.Key}={pair.Value}"));
                report.AppendLine($"  Access errors by operation/type: {errors}");
            }
        }

        return report.ToString();
    }

    private static void AppendWindowComparisons(StringBuilder report, IReadOnlyList<SampleRecord> samples)
    {
        report.AppendLine();
        report.AppendLine("EnumWindows shell-window comparison (window titles omitted):");
        foreach ((string phaseName, string phase) in new[]
                 {
                     ("pre-open", PreOpenBaselinePhase),
                     ("after-open-prompt", AfterOpenPromptPhase)
                 })
        {
            SampleRecord[] phaseSamples = samples.Where(sample => sample.Phase == phase).ToArray();
            if (phaseSamples.Length == 0)
                continue;

            double averageWindows = phaseSamples.Average(sample => sample.TopLevelWindowCount);
            double averageVisibleWindows = phaseSamples.Average(sample => sample.TopLevelVisibleWindowCount);
            report.AppendLine(
                $"  {phaseName} top-level count: samples={phaseSamples.Length}; " +
                $"average total={averageWindows.ToString("0.0", CultureInfo.InvariantCulture)}; " +
                $"average visible={averageVisibleWindows.ToString("0.0", CultureInfo.InvariantCulture)}.");
        }

        var observations = samples
            .SelectMany(sample => sample.ShellWindows.Select(window => new WindowObservation(sample.Phase, window)))
            .GroupBy(observation => GetWindowKey(observation.Window), StringComparer.Ordinal)
            .OrderBy(group => group.First().Window.ProcessName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(group => group.First().Window.ClassName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(group => group.First().Window.Handle);

        int baselineSamples = samples.Count(sample => sample.Phase == PreOpenBaselinePhase);
        int afterPromptSamples = samples.Count(sample => sample.Phase == AfterOpenPromptPhase);
        bool anyWindow = false;
        foreach (IGrouping<string, WindowObservation> group in observations)
        {
            anyWindow = true;
            NativeWindowInfo window = group.First().Window;
            WindowObservation[] baseline = group.Where(item => item.Phase == PreOpenBaselinePhase).ToArray();
            WindowObservation[] afterPrompt = group.Where(item => item.Phase == AfterOpenPromptPhase).ToArray();
            report.AppendLine(
                $"  Process={window.ProcessName}; PID={window.ProcessId}; WindowClass={Escape(window.ClassName)}; " +
                $"HWND=0x{window.Handle.ToInt64():X}; " +
                $"pre-open present/visible/phase-samples={baseline.Length}/{baseline.Count(item => item.Window.IsVisible)}/{baselineSamples}; " +
                $"after-prompt present/visible/phase-samples={afterPrompt.Length}/{afterPrompt.Count(item => item.Window.IsVisible)}/{afterPromptSamples}; " +
                $"foreground samples={group.Count(item => item.Window.IsForeground)}.");
        }

        if (!anyWindow)
            report.AppendLine("  No matching shell windows were returned by EnumWindows.");
    }

    private static void AppendRootComparisons(StringBuilder report, IReadOnlyList<SampleRecord> samples)
    {
        report.AppendLine();
        report.AppendLine("UIA scanned root-window identities by view:");
        int baselineSamples = samples.Count(sample => sample.Phase == PreOpenBaselinePhase);
        int afterPromptSamples = samples.Count(sample => sample.Phase == AfterOpenPromptPhase);
        bool anyRoot = false;
        foreach (string viewName in new[] { "Control View", "Raw View" })
        {
            IEnumerable<RootObservation> observations = samples.SelectMany(sample =>
                (sample.RootDetails.GetValueOrDefault(viewName) ?? [])
                .Select(root => new RootObservation(sample.Phase, root)));
            var groups = observations
                .GroupBy(observation => observation.Root.Key, StringComparer.Ordinal)
                .OrderBy(group => group.First().Root.ProcessName, StringComparer.OrdinalIgnoreCase)
                .ThenBy(group => group.First().Root.WindowClass, StringComparer.OrdinalIgnoreCase)
                .ThenBy(group => group.First().Root.Handle);

            foreach (IGrouping<string, RootObservation> group in groups)
            {
                anyRoot = true;
                RootWindowInfo root = group.First().Root;
                RootObservation[] baseline = group.Where(item => item.Phase == PreOpenBaselinePhase).ToArray();
                RootObservation[] afterPrompt = group.Where(item => item.Phase == AfterOpenPromptPhase).ToArray();
                report.AppendLine(
                    $"  {viewName}: Process={root.ProcessName}; PID={root.ProcessId}; " +
                    $"WindowClass={Escape(root.WindowClass)}; HWND=0x{root.Handle:X}; " +
                    $"discovery sources={string.Join(", ", group.SelectMany(item => item.Root.DiscoverySources).Distinct(StringComparer.Ordinal).OrderBy(source => source, StringComparer.Ordinal))}; " +
                    $"pre-open sightings/visible/foreground/phase-samples={baseline.Length}/{baseline.Count(item => item.Root.IsVisible == true)}/{baseline.Count(item => item.Root.IsForeground)}/{baselineSamples}; " +
                    $"after-prompt sightings/visible/foreground/phase-samples={afterPrompt.Length}/{afterPrompt.Count(item => item.Root.IsVisible == true)}/{afterPrompt.Count(item => item.Root.IsForeground)}/{afterPromptSamples}.");
            }
        }

        if (!anyRoot)
            report.AppendLine("  No eligible shell roots were exposed to UI Automation.");
    }

    private static void AppendStartOwnedPopupComparisons(StringBuilder report, IReadOnlyList<SampleRecord> samples)
    {
        report.AppendLine();
        report.AppendLine("Visible windows owned by a foreground Start host (window titles omitted):");
        var observations = samples
            .SelectMany(sample => sample.StartOwnedPopups.Select(popup => new PopupObservation(sample.Phase, popup)))
            .GroupBy(item => item.Popup.Key, StringComparer.Ordinal)
            .OrderBy(group => group.First().Popup.ProcessName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(group => group.First().Popup.WindowClass, StringComparer.OrdinalIgnoreCase)
            .ThenBy(group => group.First().Popup.Handle);

        bool anyPopup = false;
        foreach (IGrouping<string, PopupObservation> group in observations)
        {
            anyPopup = true;
            StartOwnedPopupInfo popup = group.First().Popup;
            int baselineSamples = samples.Count(sample => sample.Phase == PreOpenBaselinePhase);
            int afterPromptSamples = samples.Count(sample => sample.Phase == AfterOpenPromptPhase);
            PopupObservation[] baseline = group.Where(item => item.Phase == PreOpenBaselinePhase).ToArray();
            PopupObservation[] afterPrompt = group.Where(item => item.Phase == AfterOpenPromptPhase).ToArray();
            report.AppendLine(
                $"  Process={popup.ProcessName}; PID={popup.ProcessId}; WindowClass={Escape(popup.WindowClass)}; " +
                $"HWND=0x{popup.Handle.ToInt64():X}; owner=0x{popup.OwnerHandle.ToInt64():X}; " +
                $"root-owner=0x{popup.RootOwnerHandle.ToInt64():X}; " +
                $"pre-open present/phase-samples={baseline.Length}/{baselineSamples}; " +
                $"after-prompt present/phase-samples={afterPrompt.Length}/{afterPromptSamples}.");
        }

        if (!anyPopup)
            report.AppendLine("  None observed.");
    }

    private static void AppendInteractiveProbeSummary(StringBuilder report, IReadOnlyList<SampleRecord> samples)
    {
        report.AppendLine();
        report.AppendLine("Direct UIA targeting from current cursor and keyboard focus:");
        report.AppendLine(
            $"  Cursor GetCursorPos/FromPoint samples={samples.Sum(sample => sample.CursorProbeAttempts)}/" +
            $"{samples.Sum(sample => sample.CursorProbeSuccesses)}; " +
            $"focused GetFocusedElement samples={samples.Sum(sample => sample.FocusProbeAttempts)}/" +
            $"{samples.Sum(sample => sample.FocusProbeSuccesses)}.");
        report.AppendLine(
            $"  Scoped power-menu subtree searches/matches/capped matches=" +
            $"{samples.Sum(sample => sample.ScopedSleepSearchAttempts)}/" +
            $"{samples.Sum(sample => sample.ScopedSleepSearchMatches)}/" +
            $"{samples.Sum(sample => sample.ScopedSleepSearchCappedMatches)}.");
        report.AppendLine(
            $"  Bounded Raw View node visits below cursor-window roots=" +
            $"{samples.Sum(sample => sample.CursorRootRawNodesVisited)} " +
            $"(limit {MaxCursorRootRawNodes} nodes and depth {MaxCursorRootRawDepth} per selected root search).");

        var windowRoots = samples
            .Select(sample => sample.CursorWindowSummary)
            .OfType<string>()
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .GroupBy(value => value, StringComparer.Ordinal)
            .OrderByDescending(group => group.Count());
        report.AppendLine("  WindowFromPoint root summaries:");
        bool anyWindowRoot = false;
        foreach (IGrouping<string, string> group in windowRoots)
        {
            anyWindowRoot = true;
            report.AppendLine($"    samples={group.Count()}; {group.Key}");
        }
        if (!anyWindowRoot)
            report.AppendLine("    No native window was returned under the cursor.");

        var hitGroups = samples
            .SelectMany(sample => sample.InteractiveHitObservations)
            .GroupBy(item => string.Join("|", item.Source, item.View, item.Name, item.AutomationId,
                item.ControlTypeId.ToString(CultureInfo.InvariantCulture), item.ProcessId.ToString(CultureInfo.InvariantCulture),
                item.RootIdentity, item.ParentChain), StringComparer.Ordinal)
            .OrderBy(group => group.First().Source, StringComparer.Ordinal)
            .ThenBy(group => group.First().View, StringComparer.Ordinal);
        report.AppendLine("  Contextual cursor/focus elements and their Control/Raw ancestry:");
        bool anyHit = false;
        foreach (IGrouping<string, InteractiveHitObservation> group in hitGroups)
        {
            anyHit = true;
            InteractiveHitObservation hit = group.Last();
            report.AppendLine(
                $"    observations={group.Count()}; source={Escape(hit.Source)}; view={hit.View}; " +
                $"Name=\"{Escape(hit.Name)}\"; AutomationId=\"{Escape(hit.AutomationId)}\"; " +
                $"ControlType={hit.ControlType} ({hit.ControlTypeId}); Process={hit.ProcessName}; PID={hit.ProcessId}; " +
                $"Visible={hit.IsVisible}; IsEnabled={hit.IsEnabled}; InvokePattern={FormatPatternSupport(hit.InvokePatternSupported)}; " +
                $"UIA root={hit.RootIdentity}; " +
                $"ParentChain=\"{Escape(hit.ParentChain)}\".");
        }
        if (!anyHit)
            report.AppendLine("    No Start/power-context hit was exposed by the cursor or focus paths.");

        var candidateGroups = samples
            .SelectMany(sample => sample.SleepCandidates)
            .GroupBy(item => string.Join("|", item.Source, item.Name, item.AutomationId,
                item.ControlTypeId.ToString(CultureInfo.InvariantCulture), item.ProcessId.ToString(CultureInfo.InvariantCulture),
                item.RootIdentity, item.ParentChain), StringComparer.Ordinal)
            .OrderBy(group => group.First().Source, StringComparer.Ordinal)
            .ThenBy(group => group.First().ProcessId);
        SleepCandidateObservation[] uniqueCandidates = candidateGroups.Select(group => group.Last()).ToArray();
        report.AppendLine();
        report.AppendLine("Sleep candidate discovery vs validation:");
        report.AppendLine(
            $"  Unique candidate signatures={uniqueCandidates.Length}; " +
            $"MenuItem (50011)={uniqueCandidates.Count(item => item.IsMenuItem)}; " +
            $"Start/Open/Power-options hierarchy={uniqueCandidates.Count(item => item.HierarchyMatches)}; " +
            $"same interactive session={uniqueCandidates.Count(item => item.SameInteractiveSession == true)}; " +
            $"verified targets={uniqueCandidates.Count(item => item.IsVerified)}; " +
            $"InvokePattern true/false/unknown=" +
            $"{uniqueCandidates.Count(item => item.InvokePatternSupported == true)}/" +
            $"{uniqueCandidates.Count(item => item.InvokePatternSupported == false)}/" +
            $"{uniqueCandidates.Count(item => item.InvokePatternSupported is null)}.");

        if (uniqueCandidates.Length == 0)
        {
            report.AppendLine("  No exact Sleep/Uyku element was discovered through the recorded UIA paths; validation filters had no candidate to accept or reject.");
            return;
        }

        foreach (IGrouping<string, SleepCandidateObservation> group in candidateGroups)
        {
            SleepCandidateObservation candidate = group.Last();
            report.AppendLine($"  {FormatSleepCandidate(candidate)}; repeated observations={group.Count()}.");
        }
    }

    private static void AppendAccessErrorSummary(StringBuilder report, IReadOnlyList<SampleRecord> samples)
    {
        report.AppendLine();
        report.AppendLine("Aggregated access errors by operation and exception/Win32 error:");
        var errors = samples
            .SelectMany(sample => sample.ReadErrors)
            .GroupBy(pair => pair.Key, StringComparer.Ordinal)
            .OrderBy(group => group.Key, StringComparer.Ordinal)
            .ToArray();
        if (errors.Length == 0)
        {
            report.AppendLine("  None recorded.");
            return;
        }

        foreach (IGrouping<string, KeyValuePair<string, int>> error in errors)
            report.AppendLine($"  {error.Key}={error.Sum(pair => pair.Value)}");
    }

    private static string FormatForegroundWindow(ForegroundWindowInfo? foreground)
    {
        if (foreground is null || foreground.Handle == IntPtr.Zero)
            return "none";

        if (!foreground.IsTargetShellWindow)
            return $"non-target (identity redacted); HWND=0x{foreground.Handle.ToInt64():X}; Visible={foreground.IsVisible}";

        return
            $"Process={foreground.ProcessName}; PID={foreground.ProcessId}; " +
            $"WindowClass={Escape(foreground.WindowClass ?? string.Empty)}; " +
            $"HWND=0x{foreground.Handle.ToInt64():X}; Visible={foreground.IsVisible}";
    }

    private static string FormatNullableBoolean(bool? value) => value switch
    {
        true => "true",
        false => "false",
        null => "not a target shell window"
    };

    private static string FormatPatternSupport(bool? value) => value switch
    {
        true => "true",
        false => "false",
        null => "unknown"
    };

    private static string GetWindowKey(NativeWindowInfo window) =>
        $"{window.ProcessId}|{window.Handle.ToInt64():X}|{window.ProcessName}|{window.ClassName}";

    private static string FormatElement(RelevantElement element)
    {
        string invoke = element.InvokePatternSupported switch
        {
            true => "true",
            false => "false",
            null => "unknown"
        };
        string nativeVisible = element.NativeWindowVisible switch
        {
            true => "true",
            false => "false",
            null => "n/a"
        };
        string sameSession = element.SameInteractiveSession switch
        {
            true => "true",
            false => "false",
            null => "unknown"
        };

        return
            $"sample={element.SampleNumber}; time={FormatTime(element.CapturedAt)}; view={element.View}; " +
            $"role={Escape(element.Role)}; action={Escape(element.Action)}; " +
            $"Name=\"{Escape(element.Name)}\"; AutomationId=\"{Escape(element.AutomationId)}\"; " +
            $"ControlType={element.ControlType}; ControlTypeId={element.ControlTypeId}; ClassName=\"{Escape(element.ClassName)}\"; " +
            $"Process={element.ProcessName}; ProcessId={element.ProcessId}; ProcessSessionId={element.ProviderSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}; " +
            $"ProbeSessionId={element.InteractiveSessionId}; SameInteractiveSession={sameSession}; HWND=0x{element.NativeWindowHandle:X}; " +
            $"ParentChain=\"{Escape(element.ParentChain)}\"; Visible={!element.IsOffscreen}; " +
            $"IsOffscreen={element.IsOffscreen}; NativeWindowVisible={nativeVisible}; IsEnabled={element.IsEnabled}; " +
            $"IsKeyboardFocusable={element.IsKeyboardFocusable}; VerifiedSleepTarget={element.IsVerifiedSleepTarget}; " +
            $"InvokePattern={invoke}; SupportedPatterns=[{string.Join(", ", element.SupportedPatterns)}]";
    }

    private static string FormatSleepCandidate(SleepCandidateObservation candidate)
    {
        string invoke = candidate.InvokePatternSupported switch
        {
            true => "true",
            false => "false",
            null => "unknown"
        };
        string sameSession = candidate.SameInteractiveSession switch
        {
            true => "true",
            false => "false",
            null => "unknown"
        };
        return
            $"SleepCandidate sample={candidate.SampleNumber}; time={FormatTime(candidate.CapturedAt)}; " +
            $"source={Escape(candidate.Source)}; Name=\"{Escape(candidate.Name)}\"; " +
            $"AutomationId=\"{Escape(candidate.AutomationId)}\"; " +
            $"ControlType={candidate.ControlType} ({candidate.ControlTypeId}); " +
            $"Process={candidate.ProcessName}; ProcessId={candidate.ProcessId}; " +
            $"UIARoot=\"{Escape(candidate.RootIdentity)}\"; " +
            $"ParentChain=\"{Escape(candidate.ParentChain)}\"; " +
            $"StartContext={candidate.InStartContext}; MenuItem={candidate.IsMenuItem}; " +
            $"OpenedAncestor={candidate.HasOpenedAncestor}; PowerOptionsAncestor={candidate.HasPowerOptionsAncestor}; " +
            $"HierarchyMatches={candidate.HierarchyMatches}; ProcessSessionId={candidate.ProviderSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}; " +
            $"SameInteractiveSession={sameSession}; VerifiedSleepTarget={candidate.IsVerified}; " +
            $"Visible={candidate.IsVisible}; IsEnabled={candidate.IsEnabled}; InvokePattern={invoke}; " +
            $"SupportedPatterns=[{string.Join(", ", candidate.SupportedPatterns)}]";
    }

    private static string FormatTime(DateTimeOffset time) =>
        time.ToString("yyyy-MM-dd HH:mm:ss.fff zzz", CultureInfo.InvariantCulture);

    private static string Escape(string value) =>
        value.Replace("\\", "\\\\", StringComparison.Ordinal)
             .Replace("\r", "\\r", StringComparison.Ordinal)
             .Replace("\n", "\\n", StringComparison.Ordinal)
             .Replace("\"", "\\\"", StringComparison.Ordinal);

    private static int ReadOption(
        string[] args,
        string optionName,
        int defaultValue,
        int minimum,
        int maximum)
    {
        int index = Array.FindIndex(args, arg =>
            string.Equals(arg, optionName, StringComparison.OrdinalIgnoreCase));
        if (index < 0)
            return defaultValue;

        if (index + 1 >= args.Length ||
            !int.TryParse(args[index + 1], NumberStyles.None, CultureInfo.InvariantCulture, out int parsed) ||
            parsed < minimum ||
            parsed > maximum)
        {
            Console.Error.WriteLine($"{optionName} {minimum}..{maximum} aralığında tamsayı olmalıdır.");
            return -1;
        }

        return parsed;
    }

    private sealed class SampleRecord
    {
        public int Number { get; init; }
        public DateTimeOffset CapturedAt { get; init; }
        public string Phase { get; init; } = string.Empty;
        public int InteractiveSessionId { get; init; }
        public int? ForegroundStartProcessId { get; set; }
        public long ScanMilliseconds { get; set; }
        public Dictionary<string, RelevantElement> Elements { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, int> NodesVisited { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, int> RootsVisited { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, List<RootWindowInfo>> RootDetails { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, string> InteractiveHitSummaries { get; } = new(StringComparer.Ordinal);
        public List<NativeWindowInfo> ShellWindows { get; } = [];
        public List<StartOwnedPopupInfo> StartOwnedPopups { get; } = [];
        public List<InteractiveHitObservation> InteractiveHitObservations { get; } = [];
        public List<SleepCandidateObservation> SleepCandidates { get; } = [];
        public HashSet<string> ScopedSleepSearchKeys { get; } = new(StringComparer.Ordinal);
        public Dictionary<string, int> ReadErrors { get; } = new(StringComparer.Ordinal);
        public ForegroundWindowInfo? ForegroundWindow { get; set; }
        public bool? ForegroundWindowInEnumWindows { get; set; }
        public string? CursorWindowSummary { get; set; }
        public string? CursorWindowUiaRoot { get; set; }
        public int TopLevelWindowCount { get; set; }
        public int TopLevelVisibleWindowCount { get; set; }
        public int CursorProbeAttempts { get; set; }
        public int CursorProbeSuccesses { get; set; }
        public int FocusProbeAttempts { get; set; }
        public int FocusProbeSuccesses { get; set; }
        public int ScopedSleepSearchAttempts { get; set; }
        public int ScopedSleepSearchMatches { get; set; }
        public int ScopedSleepSearchCappedMatches { get; set; }
        public int SleepCandidateDiscoveryCount { get; set; }
        public int SleepCandidateHierarchyMatchCount { get; set; }
        public int SleepCandidateVerifiedCount { get; set; }
        public int CloseControlsExcluded { get; set; }
        public int ActionLabelsOutsideStartContext { get; set; }
        public int CursorRootRawNodesVisited { get; set; }
        public int VerifiedSleepTargetCount { get; set; }
    }

    private sealed record NativeWindowInfo(
        IntPtr Handle,
        int ProcessId,
        string ProcessName,
        string ClassName,
        bool IsVisible,
        bool IsForeground,
        string DiscoverySource);

    private sealed record ForegroundWindowInfo(
        IntPtr Handle,
        bool IsTargetShellWindow,
        string? ProcessName,
        int? ProcessId,
        string? WindowClass,
        bool? IsVisible)
    {
        public static ForegroundWindowInfo None { get; } =
            new(IntPtr.Zero, false, null, null, null, null);
    }

    private sealed record RootWindowInfo(
        string ProcessName,
        int ProcessId,
        string WindowClass,
        uint Handle,
        bool? IsVisible,
        bool IsForeground,
        string[] DiscoverySources)
    {
        public string Key => $"{ProcessId}|{Handle:X}|{ProcessName}|{WindowClass}";
    }

    private sealed record WindowObservation(string Phase, NativeWindowInfo Window);
    private sealed record RootObservation(string Phase, RootWindowInfo Root);
    private sealed record PopupObservation(string Phase, StartOwnedPopupInfo Popup);
    private sealed record AncestorReference(AutomationElement Element, UiNodeInfo Info);
    private sealed record InteractiveHitObservation(
        int SampleNumber,
        DateTimeOffset CapturedAt,
        string Source,
        string View,
        string Name,
        string AutomationId,
        string ControlType,
        int ControlTypeId,
        string ProcessName,
        int ProcessId,
        bool IsEnabled,
        bool IsVisible,
        bool? InvokePatternSupported,
        string RootIdentity,
        string ParentChain);
    private sealed record SleepCandidateObservation(
        int SampleNumber,
        DateTimeOffset CapturedAt,
        string Source,
        string Name,
        string AutomationId,
        string ControlType,
        int ControlTypeId,
        string ProcessName,
        int ProcessId,
        string RootIdentity,
        string ParentChain,
        bool InStartContext,
        bool IsMenuItem,
        bool HasOpenedAncestor,
        bool HasPowerOptionsAncestor,
        bool HierarchyMatches,
        int? ProviderSessionId,
        bool? SameInteractiveSession,
        bool IsVerified,
        bool IsEnabled,
        bool IsVisible,
        bool? InvokePatternSupported,
        string[] SupportedPatterns);
    private sealed record SleepTargetValidation(
        bool IsSleepName,
        bool IsMenuItem,
        bool InStartContext,
        bool HasOpenedAncestor,
        bool HasPowerOptionsAncestor,
        bool HierarchyMatches,
        int? ProviderSessionId,
        bool? SameInteractiveSession)
    {
        public bool IsVerified => HierarchyMatches && SameInteractiveSession == true;
    }

    private sealed class RootReference(
        AutomationElement element,
        UiNodeInfo info,
        string discoverySource,
        bool isStartRelatedPopup)
    {
        public AutomationElement Element { get; } = element;
        public UiNodeInfo Info { get; } = info;
        public HashSet<string> DiscoverySources { get; } = new(StringComparer.Ordinal) { discoverySource };
        public bool IsStartRelatedPopup { get; set; } = isStartRelatedPopup;
    }

    private sealed record StartOwnedPopupInfo(
        IntPtr Handle,
        int ProcessId,
        string ProcessName,
        string WindowClass,
        bool IsVisible,
        IntPtr OwnerHandle,
        IntPtr RootOwnerHandle)
    {
        public string Key =>
            $"{ProcessId}|{Handle.ToInt64():X}|{ProcessName}|{WindowClass}|{OwnerHandle.ToInt64():X}|{RootOwnerHandle.ToInt64():X}";
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }

    private sealed record UiNodeInfo(
        string Name,
        string AutomationId,
        string ControlType,
        string ClassName,
        string ProcessName,
        int ProcessId,
        int NativeWindowHandle,
        bool IsEnabled,
        bool IsKeyboardFocusable,
        int ControlTypeId,
        bool IsOffscreen,
        bool? NativeWindowVisible);

    private sealed record RelevantElement(
        string Key,
        int SampleNumber,
        DateTimeOffset CapturedAt,
        string View,
        string Role,
        string Action,
        string Name,
        string AutomationId,
        string ControlType,
        string ClassName,
        string ProcessName,
        int ProcessId,
        int ControlTypeId,
        bool IsKeyboardFocusable,
        int? ProviderSessionId,
        int InteractiveSessionId,
        bool? SameInteractiveSession,
        int NativeWindowHandle,
        string ParentChain,
        bool Visible,
        bool IsOffscreen,
        bool IsEnabled,
        bool? NativeWindowVisible,
        bool? InvokePatternSupported,
        string[] SupportedPatterns,
        bool IsVerifiedSleepTarget);

    private sealed class ElementHistory
    {
        public ElementHistory(RelevantElement first)
        {
            Latest = first;
            FirstSeen = first.CapturedAt;
            StartPeriod(first.CapturedAt);
            CurrentPeriod.ObservationCount = 1;
        }

        public RelevantElement Latest { get; set; }
        public DateTimeOffset FirstSeen { get; }
        public List<PresencePeriod> Periods { get; } = [];
        public bool IsActive => Periods.Count > 0 && Periods[^1].DisappearedAt is null;
        public PresencePeriod CurrentPeriod => Periods[^1];

        public void StartPeriod(DateTimeOffset time) =>
            Periods.Add(new PresencePeriod(time, time));

    }

    private sealed class PresencePeriod(DateTimeOffset firstSeenAt, DateTimeOffset lastSeenAt)
    {
        public DateTimeOffset FirstSeenAt { get; } = firstSeenAt;
        public DateTimeOffset LastSeenAt { get; set; } = lastSeenAt;
        public DateTimeOffset? DisappearedAt { get; set; }
        public int ObservationCount { get; set; }
    }

    private sealed record PresenceEvent(
        string Kind,
        DateTimeOffset At,
        string Key,
        string Role);
}
