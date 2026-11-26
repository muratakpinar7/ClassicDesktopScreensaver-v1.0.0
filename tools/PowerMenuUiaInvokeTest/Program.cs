using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Automation;

namespace PowerMenuUiaInvokeTest;

internal static class Program
{
    private const int MenuItemControlTypeId = 50011;
    private const int RootAncestor = 2;
    private const int MaximumAncestorDepth = 14;
    private const int MaximumRawNodes = 300;
    private const int MaximumRawDepth = 10;
    private const int PollIntervalMilliseconds = 500;
    private const int StableSamplesRequired = 3;
    private const int CandidateResultLimit = 25;
    private static readonly TimeSpan BaselineDuration = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan ActiveDuration = TimeSpan.FromSeconds(60);
    private const string OneShotClaimFileName = "ClassicDesktopScreensaver-PowerMenuUiaInvokeTest-v1.claim";

    private static readonly HashSet<string> AllowedShellProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "StartMenuExperienceHost",
        "ShellExperienceHost",
        "explorer"
    };

    private static readonly HashSet<string> StartHostProcesses = new(StringComparer.OrdinalIgnoreCase)
    {
        "StartMenuExperienceHost",
        "ShellExperienceHost"
    };

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetCursorPos(out NativePoint point);

    [DllImport("user32.dll")]
    private static extern IntPtr WindowFromPoint(NativePoint point);

    [DllImport("user32.dll")]
    private static extern IntPtr GetAncestor(IntPtr windowHandle, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetWindowThreadProcessId(IntPtr windowHandle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr windowHandle, StringBuilder className, int maxCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr windowHandle);

    public static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        if (args.Contains("--help", StringComparer.OrdinalIgnoreCase))
        {
            Console.WriteLine("PowerMenuUiaInvokeTest [--help | --self-test]");
            Console.WriteLine("Default run: interactive, one-shot Start Sleep UIA validation and Invoke test.");
            return 0;
        }

        if (args.Contains("--self-test", StringComparer.OrdinalIgnoreCase))
            return RunPolicySelfTests();

        return RunInteractiveTest();
    }

    private static int RunInteractiveTest()
    {
        using Process currentProcess = Process.GetCurrentProcess();
        if (currentProcess.SessionId == 0)
        {
            Console.Error.WriteLine("Bu araç etkileşimli Windows kullanıcı oturumunda çalıştırılmalıdır. Test başlatılmadı.");
            return 3;
        }

        using var mutex = new Mutex(initiallyOwned: false, $"Local\\ClassicDesktopScreensaver-PowerMenuUiaInvokeTest-{currentProcess.SessionId}");
        bool ownsMutex;
        try
        {
            ownsMutex = mutex.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            ownsMutex = true;
        }

        if (!ownsMutex)
        {
            Console.Error.WriteLine("Başka bir tek-seferlik UIA test süreci çalışıyor. Yeni bir test başlatılmadı.");
            return 4;
        }

        try
        {
            return RunLockedInteractiveTest(currentProcess.SessionId, currentProcess.Id);
        }
        finally
        {
            mutex.ReleaseMutex();
        }
    }

    private static int RunLockedInteractiveTest(int sessionId, int processId)
    {
        string reportPath = CreateReportPath();
        string claimPath = Path.Combine(Path.GetTempPath(), OneShotClaimFileName);
        var trace = new List<string>();
        var errorCounts = new Dictionary<string, int>(StringComparer.Ordinal);
        DateTimeOffset startedAt = DateTimeOffset.Now;

        Console.WriteLine("Classic Desktop Screensaver - One-shot Power Menu UIA Invoke Test");
        Console.WriteLine("Bu test yalnız doğrulanmış Windows Uyku MenuItem öğesinde en fazla bir Invoke çağrısı yapabilir.");
        Console.WriteLine("Başlat menüsünü ilk beş saniye kapalı tutun. Test penceresi açık kalabilir; fare veya klavye girdisi gönderilmez.");
        Console.WriteLine("Beş saniye sonra Başlat -> Güç menüsünü açın ve tıklamadan fareyi Uyku seçeneğinin üzerinde tutun.");
        Console.WriteLine("Belirsiz, yanlış veya devre dışı bir hedefte güç eylemi yapılmaz; yedek güç yöntemi yoktur.");
        Console.WriteLine($"Rapor yolu: {reportPath}");

        if (File.Exists(claimPath))
        {
            trace.Add($"{FormatTime(DateTimeOffset.Now)} One-shot claim already exists; no UIA power action was attempted.");
            SaveFinalReport(reportPath, startedAt, sessionId, processId, "Aborted: the one-time invocation claim already exists.", trace, errorCounts, claimPath, null);
            Console.WriteLine("Bu sürüm için tek-seferlik Invoke hakkı daha önce kullanılmış. Hiçbir güç eylemi yapılmadı.");
            Console.WriteLine($"Rapor: {reportPath}");
            TrySoundAlert();
            return 5;
        }

        Console.WriteLine($"Başlangıç ölçümü: {BaselineDuration.TotalSeconds:0} saniye.");
        var baselineTimer = Stopwatch.StartNew();
        int sampleNumber = 0;
        while (baselineTimer.Elapsed < BaselineDuration)
        {
            InspectionSample sample = CaptureSample(++sampleNumber, sessionId, errorCounts);
            trace.Add(FormatSampleSummary(sample, "baseline"));
            if (sample.Candidates.Values.Any(candidate => candidate.IsInStartMenuContext && candidate.NativeRootMatches))
            {
                trace.Add("Baseline safety gate: a Start Sleep candidate was already present; the active test was not armed.");
                SaveFinalReport(reportPath, startedAt, sessionId, processId, "Aborted: Sleep menu was already visible during the closed-menu baseline.", trace, errorCounts, claimPath, sample);
                Console.WriteLine("Başlangıç ölçümünde Uyku menüsü zaten görünüyordu. Güvenlik için test iptal edildi; Invoke çağrılmadı.");
                Console.WriteLine($"Rapor: {reportPath}");
                TrySoundAlert();
                return 6;
            }

            WaitForNextSample(baselineTimer, PollIntervalMilliseconds);
        }

        Console.WriteLine("Başlangıç ölçümü tamamlandı. Şimdi Başlat -> Güç menüsünü açın; fareyi Uyku üzerinde tutun ve tıklamayın.");
        Console.WriteLine("Doğrulama için en fazla 60 saniye beklenecek; başarı halinde tanılama kaydedilip yalnızca bir Invoke yapılacak.");
        Console.Out.Flush();

        var activeTimer = Stopwatch.StartNew();
        string? stableCandidateKey = null;
        int stableCount = 0;
        CandidateEvidence? lastCandidate = null;

        while (activeTimer.Elapsed < ActiveDuration)
        {
            Stopwatch sampleTimer = Stopwatch.StartNew();
            InspectionSample sample = CaptureSample(++sampleNumber, sessionId, errorCounts);
            trace.Add(FormatSampleSummary(sample, "active"));

            CandidateEvidence[] eligible = sample.Candidates.Values
                .Where(candidate => candidate.CanInvoke)
                .ToArray();

            if (eligible.Length > 1)
            {
                trace.Add("Safety gate: more than one distinct candidate passed all checks; no target was selected.");
                SaveFinalReport(reportPath, startedAt, sessionId, processId, "Aborted: multiple verified Sleep targets were ambiguous.", trace, errorCounts, claimPath, sample);
                Console.WriteLine("Birden fazla Uyku hedefi doğrulandı. Güvenlik için seçim yapılmadı ve Invoke çağrılmadı.");
                Console.WriteLine($"Rapor: {reportPath}");
                TrySoundAlert();
                return 7;
            }

            if (eligible.Length == 1)
            {
                CandidateEvidence candidate = eligible[0];
                if (string.Equals(candidate.StabilityKey, stableCandidateKey, StringComparison.Ordinal))
                {
                    stableCount++;
                }
                else
                {
                    stableCandidateKey = candidate.StabilityKey;
                    stableCount = 1;
                }
                lastCandidate = candidate;

                Console.WriteLine(
                    $"Doğrulanmış Uyku MenuItem gözlemi: {stableCount}/{StableSamplesRequired}; " +
                    $"ProcessId={candidate.ProcessId}; ControlType={candidate.ControlTypeId}; InvokePattern=true.");

                if (stableCount >= StableSamplesRequired)
                {
                    InspectionSample finalSample = CaptureSample(++sampleNumber, sessionId, errorCounts);
                    CandidateEvidence[] finalEligible = finalSample.Candidates.Values
                        .Where(item => item.CanInvoke)
                        .ToArray();
                    if (finalEligible.Length != 1 ||
                        !string.Equals(finalEligible[0].StabilityKey, stableCandidateKey, StringComparison.Ordinal))
                    {
                        trace.Add(FormatSampleSummary(finalSample, "final revalidation failed"));
                        SaveFinalReport(reportPath, startedAt, sessionId, processId, "Aborted: target changed or failed the immediate pre-invocation revalidation.", trace, errorCounts, claimPath, finalSample);
                        Console.WriteLine("Son doğrulamada hedef değişti veya doğrulama başarısız oldu. Invoke çağrılmadı.");
                        Console.WriteLine($"Rapor: {reportPath}");
                        TrySoundAlert();
                        return 8;
                    }

                    return InvokeExactlyOnce(
                        finalEligible[0], finalSample, reportPath, claimPath,
                        startedAt, sessionId, processId, trace, errorCounts);
                }
            }
            else
            {
                stableCandidateKey = null;
                stableCount = 0;
                lastCandidate = sample.Candidates.Values
                    .OrderByDescending(candidate => candidate.IsMenuItem)
                    .ThenByDescending(candidate => candidate.InvokePatternSupported == true)
                    .FirstOrDefault();
            }

            if (sampleNumber % 10 == 0)
            {
                string status = lastCandidate is null
                    ? "Uyku adayı henüz bulunmadı"
                    : $"aday={lastCandidate.Name}/{lastCandidate.ControlTypeId}; doğrulandı={lastCandidate.CanInvoke}; InvokePattern={FormatOptionalBoolean(lastCandidate.InvokePatternSupported)}";
                Console.WriteLine($"Ölçüm {sampleNumber}: {status}.");
            }

            WaitForNextSample(sampleTimer, PollIntervalMilliseconds);
        }

        SaveFinalReport(reportPath, startedAt, sessionId, processId, "Timed out: no single stable, fully verified Sleep target was invoked.", trace, errorCounts, claimPath, null);
        Console.WriteLine("Süre doldu. Doğrulanmış hedef bulunamadı; hiçbir güç eylemi yapılmadı.");
        Console.WriteLine($"Rapor: {reportPath}");
        TrySoundAlert();
        return 9;
    }

    private static int InvokeExactlyOnce(
        CandidateEvidence candidate,
        InspectionSample sample,
        string reportPath,
        string claimPath,
        DateTimeOffset startedAt,
        int sessionId,
        int processId,
        IReadOnlyList<string> trace,
        IDictionary<string, int> errorCounts)
    {
        DateTimeOffset preparedAt = DateTimeOffset.Now;
        string status = "Pre-invocation validation passed; diagnostic data saved. Exactly one Invoke call is armed.";
        SaveFinalReport(reportPath, startedAt, sessionId, processId, status, trace, errorCounts, claimPath, sample, candidate);

        try
        {
            using var claim = new FileStream(claimPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
            byte[] claimData = Encoding.UTF8.GetBytes(
                $"One-shot Invoke claimed at {FormatTime(preparedAt)}; SessionId={sessionId}; " +
                $"Target={candidate.Name}; ProcessId={candidate.ProcessId}; RuntimeId={candidate.RuntimeId}{Environment.NewLine}");
            claim.Write(claimData);
            claim.Flush(flushToDisk: true);
        }
        catch (Exception ex)
        {
            AppendReportLine(reportPath, $"{FormatTime(DateTimeOffset.Now)} One-shot claim could not be created ({ex.GetType().Name}); Invoke was not called.");
            Console.WriteLine("Tek-seferlik güvenlik kilidi oluşturulamadı. Invoke çağrılmadı.");
            Console.WriteLine($"Rapor: {reportPath}");
            TrySoundAlert();
            return 10;
        }

        try
        {
            // Re-read the candidate after the durable pre-invocation report and one-shot claim.
            CandidateEvidence immediate = RefreshCandidate(candidate, sessionId, errorCounts);
            if (!immediate.CanInvoke ||
                !string.Equals(immediate.StabilityKey, candidate.StabilityKey, StringComparison.Ordinal))
            {
                AppendReportLine(reportPath,
                    $"{FormatTime(DateTimeOffset.Now)} Immediate final check failed; no Invoke was called. " +
                    FormatCandidate(immediate));
                Console.WriteLine("Invoke öncesi son hedef kontrolü başarısız oldu. Güç eylemi yapılmadı.");
                Console.WriteLine($"Rapor: {reportPath}");
                TrySoundAlert();
                return 11;
            }

            if (!immediate.Element.TryGetCurrentPattern(InvokePattern.Pattern, out object invokeObject) ||
                invokeObject is not InvokePattern invokePattern)
            {
                AppendReportLine(reportPath, $"{FormatTime(DateTimeOffset.Now)} InvokePattern could not be reacquired; no power action was called.");
                Console.WriteLine("InvokePattern son kontrolde alınamadı. Güç eylemi yapılmadı.");
                Console.WriteLine($"Rapor: {reportPath}");
                TrySoundAlert();
                return 12;
            }

            AppendReportLine(reportPath,
                $"{FormatTime(DateTimeOffset.Now)} Final target check passed; invoking exactly once. {FormatCandidate(immediate)}");
            Console.WriteLine("Hedef son kez doğrulandı. Windows Uyku komutu için tek Invoke çağrısı yapılıyor; yedek veya ikinci çağrı yok.");
            Console.Out.Flush();

            // The only power-action call in this executable.
            invokePattern.Invoke();

            AppendReportLine(reportPath,
                $"{FormatTime(DateTimeOffset.Now)} InvokePattern.Invoke returned. No retry or fallback was attempted.");
            Console.WriteLine("Invoke çağrısı döndü. İkinci bir güç çağrısı yapılmayacak.");
            Console.WriteLine($"Rapor: {reportPath}");
            TrySoundAlert();
            return 0;
        }
        catch (Exception ex)
        {
            AppendReportLine(reportPath,
                $"{FormatTime(DateTimeOffset.Now)} Invoke path failed with {ex.GetType().Name}; HRESULT=0x{ex.HResult:X8}. No retry or fallback was attempted.");
            Console.WriteLine($"Invoke yolu hata verdi: {ex.GetType().Name}. Yeni bir güç çağrısı yapılmayacak.");
            Console.WriteLine($"Rapor: {reportPath}");
            TrySoundAlert();
            return 13;
        }
    }

    private static CandidateEvidence RefreshCandidate(
        CandidateEvidence original,
        int sessionId,
        IDictionary<string, int> errorCounts)
    {
        if (!GetCursorPos(out NativePoint point))
            throw new InvalidOperationException($"GetCursorPos failed with Win32 error {Marshal.GetLastWin32Error()}.");

        NativeWindowInfo? root = ReadWindowRootAtPoint(point, errorCounts, out _);
        if (root is null || !root.IsAllowedShellRoot || root.Handle != original.NativeRootHandle)
            throw new InvalidOperationException("Native window under cursor changed before Invoke.");

        CandidateEvidence refreshed = InspectCandidate(
            original.Element,
            "Immediate pre-invocation refresh",
            root,
            point,
            sessionId,
            errorCounts);
        if (!refreshed.CanInvoke)
            throw new InvalidOperationException("Sleep target failed the immediate validation policy.");
        return refreshed;
    }

    private static InspectionSample CaptureSample(
        int sampleNumber,
        int sessionId,
        IDictionary<string, int> errorCounts)
    {
        DateTimeOffset capturedAt = DateTimeOffset.Now;
        var result = new InspectionSample(sampleNumber, capturedAt, sessionId);
        if (!GetCursorPos(out NativePoint point))
        {
            AddError(result.Errors, errorCounts, "GetCursorPos", Marshal.GetLastWin32Error());
            result.Cursor = null;
            return result;
        }

        result.Cursor = point;
        NativeWindowInfo? root = ReadWindowRootAtPoint(point, errorCounts, out string? windowError);
        result.NativeRoot = root;
        if (!string.IsNullOrWhiteSpace(windowError))
            result.Errors.Add(windowError);

        var candidates = new Dictionary<string, CandidateEvidence>(StringComparer.Ordinal);

        try
        {
            AutomationElement pointElement = AutomationElement.FromPoint(new Point(point.X, point.Y));
            result.FromPointSummary = SummarizeElement(pointElement, errorCounts, "FromPoint");
            AddCandidateIfSleep(pointElement, "AutomationElement.FromPoint", root, point, sessionId, candidates, result.Errors, errorCounts);
        }
        catch (Exception ex)
        {
            AddException(result.Errors, errorCounts, "AutomationElement.FromPoint", ex);
        }

        try
        {
            AutomationElement focused = AutomationElement.FocusedElement;
            result.FocusedSummary = SummarizeElement(focused, errorCounts, "GetFocusedElement");
            AddCandidateIfSleep(focused, "AutomationElement.FocusedElement", root, point, sessionId, candidates, result.Errors, errorCounts);
        }
        catch (Exception ex)
        {
            AddException(result.Errors, errorCounts, "AutomationElement.FocusedElement", ex);
        }

        if (root is { IsAllowedShellRoot: true, IsVisible: true })
        {
            try
            {
                AutomationElement rootElement = AutomationElement.FromHandle(root.Handle);
                result.UiaRootSummary = SummarizeElement(rootElement, errorCounts, "WindowFromPoint UIA root");

                System.Windows.Automation.Condition[] exactNames = [
                    new PropertyCondition(AutomationElement.NameProperty, "Sleep", PropertyConditionFlags.IgnoreCase),
                    new PropertyCondition(AutomationElement.NameProperty, "Uyku", PropertyConditionFlags.IgnoreCase)
                ];
                AutomationElementCollection found = rootElement.FindAll(
                    TreeScope.Descendants,
                    new OrCondition(exactNames));
                result.ControlSearchCount = found.Count;
                int resultCount = Math.Min(found.Count, CandidateResultLimit);
                for (int index = 0; index < resultCount; index++)
                {
                    AddCandidateIfSleep(
                        found[index],
                        "WindowFromPoint root / FindAll",
                        root,
                        point,
                        sessionId,
                        candidates,
                        result.Errors,
                        errorCounts);
                }
                if (found.Count > CandidateResultLimit)
                    result.Errors.Add($"WindowFromPoint root FindAll results capped at {CandidateResultLimit}; reported={found.Count}.");
            }
            catch (Exception ex)
            {
                AddException(result.Errors, errorCounts, "AutomationElement.FromHandle/WindowFromPoint root FindAll", ex);
            }

            if (sampleNumber % 4 == 0)
            {
                try
                {
                    AutomationElement rootElement = AutomationElement.FromHandle(root.Handle);
                    var rawVisited = 0;
                    WalkRawRoot(
                        rootElement,
                        root,
                        point,
                        sessionId,
                        candidates,
                        result.Errors,
                        errorCounts,
                        ref rawVisited,
                        depth: 0);
                    result.RawNodesVisited = rawVisited;
                }
                catch (Exception ex)
                {
                    AddException(result.Errors, errorCounts, "WindowFromPoint root/Raw View walk", ex);
                }
            }
        }

        result.Candidates = candidates;
        return result;
    }

    private static NativeWindowInfo? ReadWindowRootAtPoint(
        NativePoint point,
        IDictionary<string, int> errorCounts,
        out string? error)
    {
        error = null;
        IntPtr hit = WindowFromPoint(point);
        if (hit == IntPtr.Zero)
            return null;

        IntPtr root = GetAncestor(hit, RootAncestor);
        if (root == IntPtr.Zero)
            root = hit;
        uint threadId = GetWindowThreadProcessId(root, out uint rawPid);
        if (threadId == 0 || rawPid == 0)
        {
            int win32Error = Marshal.GetLastWin32Error();
            error = $"WindowFromPoint/GetWindowThreadProcessId failed; Win32Error=0x{win32Error:X8}.";
            IncrementError(errorCounts, "WindowFromPoint/GetWindowThreadProcessId/Win32Error=" + win32Error.ToString(CultureInfo.InvariantCulture));
            return null;
        }

        var classBuffer = new StringBuilder(256);
        int classLength = GetClassName(root, classBuffer, classBuffer.Capacity);
        if (classLength == 0)
        {
            int win32Error = Marshal.GetLastWin32Error();
            error = $"WindowFromPoint/GetClassName failed; Win32Error=0x{win32Error:X8}.";
            IncrementError(errorCounts, "WindowFromPoint/GetClassName/Win32Error=" + win32Error.ToString(CultureInfo.InvariantCulture));
            return null;
        }

        int processId = unchecked((int)rawPid);
        string processName;
        int? processSessionId;
        try
        {
            using Process process = Process.GetProcessById(processId);
            processName = process.ProcessName;
            processSessionId = process.SessionId;
        }
        catch (Exception ex)
        {
            error = $"WindowFromPoint process lookup failed: {ex.GetType().Name}.";
            IncrementError(errorCounts, "WindowFromPoint/ProcessLookup/" + ex.GetType().Name);
            return null;
        }

        return new NativeWindowInfo(
            root,
            processId,
            processName,
            classBuffer.ToString(),
            IsWindowVisible(root),
            processSessionId,
            AllowedShellProcesses.Contains(processName));
    }

    private static void WalkRawRoot(
        AutomationElement element,
        NativeWindowInfo root,
        NativePoint cursor,
        int sessionId,
        IDictionary<string, CandidateEvidence> candidates,
        ICollection<string> errors,
        IDictionary<string, int> errorCounts,
        ref int visited,
        int depth)
    {
        if (depth > MaximumRawDepth || visited >= MaximumRawNodes)
            return;

        visited++;
        try
        {
            UiElementSnapshot snapshot = ReadElement(element);
            if (IsSleepName(snapshot.Name))
                AddCandidateIfSleep(element, "WindowFromPoint root / bounded Raw View walk", root, cursor, sessionId, candidates, errors, errorCounts);

            if (depth >= MaximumRawDepth || visited >= MaximumRawNodes)
                return;

            AutomationElement? child;
            try
            {
                child = TreeWalker.RawViewWalker.GetFirstChild(element);
            }
            catch (Exception ex)
            {
                AddException(errors, errorCounts, "Raw View/GetFirstChild", ex);
                return;
            }

            while (child is not null && visited < MaximumRawNodes)
            {
                WalkRawRoot(child, root, cursor, sessionId, candidates, errors, errorCounts, ref visited, depth + 1);
                try
                {
                    child = TreeWalker.RawViewWalker.GetNextSibling(child);
                }
                catch (Exception ex)
                {
                    AddException(errors, errorCounts, "Raw View/GetNextSibling", ex);
                    return;
                }
            }
        }
        catch (Exception ex)
        {
            AddException(errors, errorCounts, "Raw View/ReadElement", ex);
        }
    }

    private static void AddCandidateIfSleep(
        AutomationElement element,
        string source,
        NativeWindowInfo? root,
        NativePoint cursor,
        int sessionId,
        IDictionary<string, CandidateEvidence> candidates,
        ICollection<string> errors,
        IDictionary<string, int> errorCounts)
    {
        try
        {
            UiElementSnapshot snapshot = ReadElement(element);
            if (!IsSleepName(snapshot.Name))
                return;

            string runtimeId;
            try
            {
                runtimeId = string.Join(".", element.GetRuntimeId().Select(value => value.ToString("X8", CultureInfo.InvariantCulture)));
            }
            catch (Exception ex)
            {
                runtimeId = string.Empty;
                AddException(errors, errorCounts, "AutomationElement.GetRuntimeId", ex);
            }

            AncestorSnapshot controlAncestors = ReadAncestors(element, TreeWalker.ControlViewWalker, "Control View", errors, errorCounts);
            AncestorSnapshot rawAncestors = ReadAncestors(element, TreeWalker.RawViewWalker, "Raw View", errors, errorCounts);
            bool? invokeSupported;
            try
            {
                invokeSupported = element.TryGetCurrentPattern(InvokePattern.Pattern, out _);
            }
            catch (Exception ex)
            {
                invokeSupported = null;
                AddException(errors, errorCounts, "TryGetCurrentPattern/InvokePattern", ex);
            }

            int? providerSession = null;
            string processName = "(unavailable)";
            try
            {
                using Process provider = Process.GetProcessById(snapshot.ProcessId);
                processName = provider.ProcessName;
                providerSession = provider.SessionId;
            }
            catch (Exception ex)
            {
                AddException(errors, errorCounts, "Sleep candidate process/session lookup", ex);
            }

            var candidate = new CandidateEvidence(
                element,
                snapshot,
                runtimeId,
                processName,
                providerSession,
                root,
                cursor,
                sessionId,
                controlAncestors,
                rawAncestors,
                invokeSupported,
                source);

            string key = candidate.RuntimeId.Length > 0
                ? $"{snapshot.ProcessId}|{candidate.RuntimeId}"
                : $"no-runtime-id|{snapshot.ProcessId}|{snapshot.ControlTypeId}|{snapshot.Name}|{snapshot.BoundsKey}";
            if (candidates.TryGetValue(key, out CandidateEvidence? existing))
                existing.Sources.Add(source);
            else
                candidates.Add(key, candidate);
        }
        catch (Exception ex)
        {
            AddException(errors, errorCounts, $"Sleep candidate/{source}", ex);
        }
    }

    private static CandidateEvidence InspectCandidate(
        AutomationElement element,
        string source,
        NativeWindowInfo root,
        NativePoint cursor,
        int sessionId,
        IDictionary<string, int> errorCounts)
    {
        var errors = new List<string>();
        UiElementSnapshot snapshot = ReadElement(element);
        string runtimeId = string.Join(".", element.GetRuntimeId().Select(value => value.ToString("X8", CultureInfo.InvariantCulture)));
        AncestorSnapshot controlAncestors = ReadAncestors(element, TreeWalker.ControlViewWalker, "Control View", errors, errorCounts);
        AncestorSnapshot rawAncestors = ReadAncestors(element, TreeWalker.RawViewWalker, "Raw View", errors, errorCounts);
        bool? invokeSupported = element.TryGetCurrentPattern(InvokePattern.Pattern, out _);
        using Process provider = Process.GetProcessById(snapshot.ProcessId);
        return new CandidateEvidence(
            element,
            snapshot,
            runtimeId,
            provider.ProcessName,
            provider.SessionId,
            root,
            cursor,
            sessionId,
            controlAncestors,
            rawAncestors,
            invokeSupported,
            source);
    }

    private static AncestorSnapshot ReadAncestors(
        AutomationElement element,
        TreeWalker walker,
        string viewName,
        ICollection<string> errors,
        IDictionary<string, int> errorCounts)
    {
        var reversed = new List<UiElementSnapshot>();
        AutomationElement? current;
        try
        {
            current = walker.GetParent(element);
        }
        catch (Exception ex)
        {
            AddException(errors, errorCounts, $"{viewName}/GetParent", ex);
            return new AncestorSnapshot([], IsComplete: false);
        }

        for (int depth = 0; current is not null && depth < MaximumAncestorDepth; depth++)
        {
            try
            {
                reversed.Add(ReadElement(current));
                current = walker.GetParent(current);
            }
            catch (Exception ex)
            {
                AddException(errors, errorCounts, $"{viewName}/Read ancestor", ex);
                return new AncestorSnapshot(reversed.AsEnumerable().Reverse().ToArray(), IsComplete: false);
            }
        }

        bool complete = current is null;
        if (!complete)
            errors.Add($"{viewName}/ancestor walk stopped at depth {MaximumAncestorDepth}.");
        return new AncestorSnapshot(reversed.AsEnumerable().Reverse().ToArray(), complete);
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
            current.BoundingRectangle,
            current.NativeWindowHandle);
    }

    private static bool IsCandidateSafe(InvocationEvidence evidence) =>
        IsSleepName(evidence.Name) &&
        evidence.ControlTypeId == MenuItemControlTypeId &&
        evidence.ControlPathComplete &&
        evidence.RawPathComplete &&
        evidence.ControlHierarchyMatches &&
        evidence.RawHierarchyMatches &&
        evidence.ProviderSessionId == evidence.InteractiveSessionId &&
        evidence.NativeRootMatches &&
        evidence.NativeRootIsAllowed &&
        evidence.NativeRootIsVisible &&
        evidence.IsEnabled &&
        evidence.IsVisible &&
        evidence.CursorInsideBounds &&
        evidence.HasRuntimeId &&
        evidence.InvokePatternSupported == true;

    private static InvocationEvidence ToEvidence(CandidateEvidence candidate) => new(
        candidate.Snapshot.Name,
        candidate.Snapshot.ControlTypeId,
        candidate.ControlAncestors.IsComplete,
        candidate.RawAncestors.IsComplete,
        candidate.ControlHierarchyMatches,
        candidate.RawHierarchyMatches,
        candidate.ProviderSessionId,
        candidate.InteractiveSessionId,
        candidate.NativeRootMatches,
        candidate.NativeRoot?.IsAllowedShellRoot == true,
        candidate.NativeRoot?.IsVisible == true,
        candidate.Snapshot.IsEnabled,
        !candidate.Snapshot.IsOffscreen,
        candidate.CursorInsideBounds,
        candidate.RuntimeId.Length > 0,
        candidate.InvokePatternSupported);

    private static bool IsSleepName(string value) => Normalize(value) is "sleep" or "uyku";

    private static bool IsStartContext(string processName, IReadOnlyList<UiElementSnapshot> ancestors) =>
        StartHostProcesses.Contains(processName) || ancestors.Any(IsStartMarker);

    private static bool IsStartMarker(UiElementSnapshot node)
    {
        string[] values = [node.Name, node.AutomationId, node.ClassName];
        return values.Any(value => Normalize(value) is "start" or "start menu" or "başlat" or "başlat menüsü" or "başlangıç" or "başlangıç menüsü" ||
                                   Normalize(value).Contains("startmenu", StringComparison.Ordinal) ||
                                   Normalize(value).Contains("başlangıç menüsü", StringComparison.Ordinal));
    }

    private static bool IsOpenedMarker(UiElementSnapshot node)
    {
        string[] values = [node.Name, node.AutomationId, node.ClassName];
        return values.Any(value => Normalize(value) is "opened" or "open" or "açılan" or "acilan" or "expanded" or "expanded menu");
    }

    private static bool IsPowerOptionsMarker(UiElementSnapshot node)
    {
        string[] values = [node.Name, node.AutomationId, node.ClassName];
        return values.Any(value =>
        {
            string normalized = Normalize(value.Replace('/', ' '));
            return normalized is "power options" or "power options menu" or "power menu options" or
                   "aç kapa seçenekleri" or "güç seçenekleri" or "guc seçenekleri" or "aç kapa menüsü" or
                   "güç menüsü" or "guc menusu" ||
                   normalized.Contains("power options", StringComparison.Ordinal) ||
                   normalized.Contains("aç kapa seçenekleri", StringComparison.Ordinal) ||
                   normalized.Contains("güç seçenekleri", StringComparison.Ordinal) ||
                   normalized.Contains("guc seçenekleri", StringComparison.Ordinal);
        });
    }

    private static bool HasHierarchy(
        string processName,
        IReadOnlyList<UiElementSnapshot> ancestors)
    {
        int startIndex = -1;
        for (int index = 0; index < ancestors.Count; index++)
        {
            if (IsStartMarker(ancestors[index]))
            {
                startIndex = index;
                break;
            }
        }

        // The Start host process itself is an acceptable outer Start context when
        // the provider omits a separate Start ancestor from its UIA tree.
        if (startIndex < 0 && !StartHostProcesses.Contains(processName))
            return false;

        int openedIndex = -1;
        for (int index = startIndex + 1; index < ancestors.Count; index++)
        {
            if (IsOpenedMarker(ancestors[index]))
            {
                openedIndex = index;
                break;
            }
        }
        if (openedIndex < 0)
            return false;

        for (int index = openedIndex + 1; index < ancestors.Count; index++)
        {
            if (IsPowerOptionsMarker(ancestors[index]))
                return true;
        }

        return false;
    }

    private static string Normalize(string value) =>
        string.Join(
            ' ',
            value.Normalize(NormalizationForm.FormKC)
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            .ToLowerInvariant();

    private static bool IsCursorInside(System.Windows.Rect bounds, NativePoint point) =>
        !bounds.IsEmpty &&
        bounds.Width > 0 && bounds.Height > 0 &&
        point.X >= bounds.Left && point.X <= bounds.Right &&
        point.Y >= bounds.Top && point.Y <= bounds.Bottom;

    private static string FormatChain(IReadOnlyList<UiElementSnapshot> nodes)
    {
        if (nodes.Count == 0)
            return "(no accessible parent)";

        return string.Join(" -> ", nodes.Select(node =>
        {
            bool relevant = IsStartMarker(node) || IsOpenedMarker(node) || IsPowerOptionsMarker(node);
            string label = relevant || IsSleepName(node.Name)
                ? Escape(string.IsNullOrWhiteSpace(node.Name) ? node.ControlType : node.Name)
                : "(name redacted)";
            string automationId = relevant && !string.IsNullOrWhiteSpace(node.AutomationId)
                ? $", AutomationId={Escape(node.AutomationId)}"
                : string.Empty;
            string process = node.ProcessId > 0 ? $"PID={node.ProcessId}" : "PID=unknown";
            return $"{label} [{node.ControlType}{automationId}; {process}]";
        }));
    }

    private static string SummarizeElement(
        AutomationElement element,
        IDictionary<string, int> errorCounts,
        string operation)
    {
        try
        {
            UiElementSnapshot node = ReadElement(element);
            string processName;
            try
            {
                using Process process = Process.GetProcessById(node.ProcessId);
                processName = AllowedShellProcesses.Contains(process.ProcessName)
                    ? process.ProcessName
                    : "(identity redacted)";
            }
            catch
            {
                processName = "(identity redacted)";
            }

            return $"Process={processName}; ControlType={node.ControlType}; PID=" +
                   $"{(processName == "(identity redacted)" ? "redacted" : node.ProcessId.ToString(CultureInfo.InvariantCulture))}; " +
                   $"Name={(IsSleepName(node.Name) ? Escape(node.Name) : "(redacted)")}";
        }
        catch (Exception ex)
        {
            IncrementError(errorCounts, $"{operation}/ReadElement/{ex.GetType().Name}");
            return $"read failed ({ex.GetType().Name})";
        }
    }

    private static string FormatSampleSummary(InspectionSample sample, string phase)
    {
        string root = sample.NativeRoot is null
            ? "no native root"
            : $"{sample.NativeRoot.ProcessName}:{sample.NativeRoot.ProcessId}/{sample.NativeRoot.WindowClass}/0x{sample.NativeRoot.Handle.ToInt64():X}";
        string candidates = sample.Candidates.Count == 0
            ? "none"
            : string.Join(", ", sample.Candidates.Values.Select(candidate =>
                $"{candidate.Name}/{candidate.Snapshot.ControlTypeId}/safe={candidate.CanInvoke}/Invoke={FormatOptionalBoolean(candidate.InvokePatternSupported)}"));
        return $"{FormatTime(sample.CapturedAt)} sample={sample.Number} phase={phase}; cursor=" +
               $"{(sample.Cursor is NativePoint point ? $"{point.X},{point.Y}" : "unavailable")}; root={root}; " +
               $"FromPoint={sample.FromPointSummary ?? "unavailable"}; Focus={sample.FocusedSummary ?? "unavailable"}; " +
               $"ControlFindAll={sample.ControlSearchCount}; RawNodes={sample.RawNodesVisited}; " +
               $"candidates={candidates}; errors={sample.Errors.Count}.";
    }

    private static void SaveFinalReport(
        string reportPath,
        DateTimeOffset startedAt,
        int sessionId,
        int processId,
        string outcome,
        IReadOnlyList<string> trace,
        IDictionary<string, int> errorCounts,
        string claimPath,
        InspectionSample? sample,
        CandidateEvidence? target = null)
    {
        var report = new StringBuilder();
        report.AppendLine("Classic Desktop Screensaver - One-shot Power Menu UIA Invoke Test");
        report.AppendLine("MODE: guarded one-shot; only the verified Start Sleep MenuItem can reach the single Invoke call.");
        report.AppendLine($"Start time: {FormatTime(startedAt)}");
        report.AppendLine($"Report time: {FormatTime(DateTimeOffset.Now)}");
        report.AppendLine($"Windows session ID: {sessionId}; probe PID={processId}; architecture={(Environment.Is64BitProcess ? "64-bit" : "32-bit")}");
        report.AppendLine($"Outcome: {outcome}");
        report.AppendLine($"One-shot claim path: {claimPath}");
        report.AppendLine("Allowed action: one InvokePattern.Invoke call after durable diagnostics; no retry or fallback.");
        report.AppendLine("Input behavior: GetCursorPos/FromPoint/focus/tree reads only; no mouse or keyboard input is sent.");
        report.AppendLine();
        report.AppendLine("Sample trace:");
        foreach (string line in trace)
            report.AppendLine(line);

        if (sample is not null)
        {
            report.AppendLine();
            report.AppendLine("Final sample details:");
            report.AppendLine(FormatSampleSummary(sample, "pre-invocation"));
            foreach (string error in sample.Errors)
                report.AppendLine($"  UIA/native diagnostic error: {error}");
            foreach (CandidateEvidence candidate in sample.Candidates.Values)
                report.AppendLine($"  {FormatCandidate(candidate)}");
        }

        if (target is not null && (sample is null || !sample.Candidates.ContainsKey(target.DictionaryKey)))
        {
            report.AppendLine();
            report.AppendLine("Selected target evidence:");
            report.AppendLine(FormatCandidate(target));
        }

        report.AppendLine();
        report.AppendLine("Aggregated errors:");
        if (errorCounts.Count == 0)
            report.AppendLine("  None recorded.");
        else
        {
            foreach ((string error, int count) in errorCounts.OrderBy(pair => pair.Key, StringComparer.Ordinal))
                report.AppendLine($"  {error}={count}");
        }

        WriteDurableFile(reportPath, report.ToString(), append: false);
    }

    private static string FormatCandidate(CandidateEvidence candidate) =>
        $"Target Name=\"{Escape(candidate.Snapshot.Name)}\"; AutomationId=\"{Escape(candidate.Snapshot.AutomationId)}\"; " +
        $"ControlType={candidate.Snapshot.ControlType} ({candidate.Snapshot.ControlTypeId}); " +
        $"Process={candidate.ProcessName}; ProcessId={candidate.ProcessId}; ProviderSessionId={candidate.ProviderSessionId?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}; " +
        $"InteractiveSessionId={candidate.InteractiveSessionId}; NativeRoot={(candidate.NativeRoot is null ? "unknown" : $"{candidate.NativeRoot.ProcessName}:{candidate.NativeRoot.ProcessId}/{candidate.NativeRoot.WindowClass}/0x{candidate.NativeRoot.Handle.ToInt64():X}")}; " +
        $"RootMatches={candidate.NativeRootMatches}; RootAllowed={candidate.NativeRoot?.IsAllowedShellRoot == true}; " +
        $"Visible={!candidate.Snapshot.IsOffscreen}; IsEnabled={candidate.Snapshot.IsEnabled}; " +
        $"CursorInsideBounds={candidate.CursorInsideBounds}; Bounds={candidate.Snapshot.BoundingRectangle}; " +
        $"ControlViewChainComplete={candidate.ControlAncestors.IsComplete}; ControlViewStart/Open/Power={candidate.ControlStartContext}/{candidate.ControlHasOpened}/{candidate.ControlHasPowerOptions}; " +
        $"ControlViewChain=\"{Escape(FormatChain(candidate.ControlAncestors.Nodes))}\"; " +
        $"RawViewChainComplete={candidate.RawAncestors.IsComplete}; RawViewStart/Open/Power={candidate.RawStartContext}/{candidate.RawHasOpened}/{candidate.RawHasPowerOptions}; " +
        $"RawViewChain=\"{Escape(FormatChain(candidate.RawAncestors.Nodes))}\"; " +
        $"InvokePattern={FormatOptionalBoolean(candidate.InvokePatternSupported)}; RuntimeId={candidate.RuntimeId}; " +
        $"ControlHierarchy={candidate.ControlHierarchyMatches}; RawHierarchy={candidate.RawHierarchyMatches}; " +
        $"Verified={candidate.CanInvoke}; Sources=[{string.Join(", ", candidate.Sources)}]";

    private static int RunPolicySelfTests()
    {
        InvocationEvidence valid = new(
            "Uyku", MenuItemControlTypeId,
            true, true, true, true,
            1, 1, true, true, true,
            true, true, true, true, true);

        var tests = new (string Name, InvocationEvidence Evidence, bool Expected)[]
        {
            ("valid Turkish Sleep MenuItem", valid, true),
            ("Hibernate label", valid with { Name = "Hazırda Beklet" }, false),
            ("Shut down label", valid with { Name = "Kapat" }, false),
            ("Text control instead of MenuItem", valid with { ControlTypeId = 50020 }, false),
            ("missing Start hierarchy", valid with { ControlHierarchyMatches = false }, false),
            ("missing Raw hierarchy", valid with { RawHierarchyMatches = false }, false),
            ("provider in another session", valid with { ProviderSessionId = 2 }, false),
            ("candidate not from cursor window", valid with { NativeRootMatches = false }, false),
            ("non-shell root", valid with { NativeRootIsAllowed = false }, false),
            ("offscreen candidate", valid with { IsVisible = false }, false),
            ("disabled candidate", valid with { IsEnabled = false }, false),
            ("cursor outside candidate bounds", valid with { CursorInsideBounds = false }, false),
            ("InvokePattern unsupported", valid with { InvokePatternSupported = false }, false),
            ("InvokePattern unknown", valid with { InvokePatternSupported = null }, false),
            ("runtime identity unavailable", valid with { HasRuntimeId = false }, false)
        };

        int failures = 0;
        foreach ((string name, InvocationEvidence evidence, bool expected) in tests)
        {
            bool actual = IsCandidateSafe(evidence);
            bool passed = actual == expected;
            Console.WriteLine($"{(passed ? "PASS" : "FAIL")}: {name}; expected={expected}; actual={actual}");
            if (!passed)
                failures++;
        }

        UiElementSnapshot Marker(string name) => new(
            name, string.Empty, "ControlType.Custom", 50025, 0, string.Empty,
            IsEnabled: true, IsOffscreen: false, Rect.Empty, NativeWindowHandle: 0);

        var hierarchyTests = new (string Name, string ProcessName, UiElementSnapshot[] Ancestors, bool Expected)[]
        {
            ("ordered Start/Open/Power ancestry", "explorer", [Marker("Başlangıç"), Marker("Açılan"), Marker("Aç/Kapa seçenekleri")], true),
            ("power ancestry in wrong order", "explorer", [Marker("Başlangıç"), Marker("Aç/Kapa seçenekleri"), Marker("Açılan")], false),
            ("Start host supplies outer context", "StartMenuExperienceHost", [Marker("Açılan"), Marker("Aç/Kapa seçenekleri")], true),
            ("unrelated process without Start ancestry", "notepad", [Marker("Açılan"), Marker("Aç/Kapa seçenekleri")], false)
        };

        foreach ((string name, string processName, UiElementSnapshot[] ancestors, bool expected) in hierarchyTests)
        {
            bool actual = HasHierarchy(processName, ancestors);
            bool passed = actual == expected;
            Console.WriteLine($"{(passed ? "PASS" : "FAIL")}: {name}; expected={expected}; actual={actual}");
            if (!passed)
                failures++;
        }

        int total = tests.Length + hierarchyTests.Length;
        Console.WriteLine($"Self-test result: {total - failures}/{total} passed. No UIA query or power action was performed.");
        return failures == 0 ? 0 : 1;
    }

    private static string CreateReportPath() => Path.Combine(
        Path.GetTempPath(),
        $"ClassicDesktopScreensaver-PowerMenuUiaInvokeTest-{DateTimeOffset.Now:yyyyMMdd-HHmmss-fff}-{Guid.NewGuid():N}.txt");

    private static void WriteDurableFile(string path, string content, bool append)
    {
        byte[] bytes = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false).GetBytes(content);
        using var stream = new FileStream(
            path,
            append ? FileMode.Append : FileMode.Create,
            FileAccess.Write,
            FileShare.Read);
        stream.Write(bytes);
        stream.Flush(flushToDisk: true);
    }

    private static void AppendReportLine(string reportPath, string line) =>
        WriteDurableFile(reportPath, line + Environment.NewLine, append: true);

    private static void AddError(
        ICollection<string> errors,
        IDictionary<string, int> errorCounts,
        string operation,
        int win32Error)
    {
        string message = $"{operation}; Win32Error=0x{win32Error:X8}";
        errors.Add(message);
        IncrementError(errorCounts, message);
    }

    private static void AddException(
        ICollection<string> errors,
        IDictionary<string, int> errorCounts,
        string operation,
        Exception exception)
    {
        string message = $"{operation}; {exception.GetType().Name}; HRESULT=0x{exception.HResult:X8}";
        errors.Add(message);
        IncrementError(errorCounts, message);
    }

    private static void IncrementError(IDictionary<string, int> errors, string key)
    {
        errors.TryGetValue(key, out int count);
        errors[key] = count + 1;
    }

    private static void WaitForNextSample(Stopwatch sampleTimer, int intervalMilliseconds)
    {
        int wait = Math.Max(0, intervalMilliseconds - (int)sampleTimer.ElapsedMilliseconds);
        if (wait > 0)
            Thread.Sleep(wait);
    }

    private static string FormatOptionalBoolean(bool? value) => value switch
    {
        true => "true",
        false => "false",
        null => "unknown"
    };

    private static string FormatTime(DateTimeOffset time) =>
        time.ToString("yyyy-MM-dd HH:mm:ss.fff zzz", CultureInfo.InvariantCulture);

    private static string Escape(string value) =>
        value.Replace("\\", "\\\\", StringComparison.Ordinal)
             .Replace("\r", "\\r", StringComparison.Ordinal)
             .Replace("\n", "\\n", StringComparison.Ordinal)
             .Replace("\"", "\\\"", StringComparison.Ordinal);

    private static void TrySoundAlert()
    {
        try
        {
            System.Media.SystemSounds.Asterisk.Play();
        }
        catch
        {
            // The alert is optional and cannot change the test result.
        }
    }

    private sealed class InspectionSample(int number, DateTimeOffset capturedAt, int interactiveSessionId)
    {
        public int Number { get; } = number;
        public DateTimeOffset CapturedAt { get; } = capturedAt;
        public int InteractiveSessionId { get; } = interactiveSessionId;
        public NativePoint? Cursor { get; set; }
        public NativeWindowInfo? NativeRoot { get; set; }
        public string? FromPointSummary { get; set; }
        public string? FocusedSummary { get; set; }
        public string? UiaRootSummary { get; set; }
        public int ControlSearchCount { get; set; }
        public int RawNodesVisited { get; set; }
        public Dictionary<string, CandidateEvidence> Candidates { get; set; } = new(StringComparer.Ordinal);
        public List<string> Errors { get; } = [];
    }

    private sealed record NativeWindowInfo(
        IntPtr Handle,
        int ProcessId,
        string ProcessName,
        string WindowClass,
        bool IsVisible,
        int? SessionId,
        bool IsAllowedShellRoot);

    private sealed record UiElementSnapshot(
        string Name,
        string AutomationId,
        string ControlType,
        int ControlTypeId,
        int ProcessId,
        string ClassName,
        bool IsEnabled,
        bool IsOffscreen,
        Rect BoundingRectangle,
        int NativeWindowHandle)
    {
        public string BoundsKey =>
            $"{BoundingRectangle.Left:0.##},{BoundingRectangle.Top:0.##},{BoundingRectangle.Width:0.##},{BoundingRectangle.Height:0.##}";
    }

    private sealed record AncestorSnapshot(IReadOnlyList<UiElementSnapshot> Nodes, bool IsComplete);

    private sealed class CandidateEvidence
    {
        public CandidateEvidence(
            AutomationElement element,
            UiElementSnapshot snapshot,
            string runtimeId,
            string processName,
            int? providerSessionId,
            NativeWindowInfo? nativeRoot,
            NativePoint cursor,
            int interactiveSessionId,
            AncestorSnapshot controlAncestors,
            AncestorSnapshot rawAncestors,
            bool? invokePatternSupported,
            string source)
        {
            Element = element;
            Snapshot = snapshot;
            RuntimeId = runtimeId;
            ProcessName = processName;
            ProviderSessionId = providerSessionId;
            NativeRoot = nativeRoot;
            Cursor = cursor;
            InteractiveSessionId = interactiveSessionId;
            ControlAncestors = controlAncestors;
            RawAncestors = rawAncestors;
            InvokePatternSupported = invokePatternSupported;
            Sources.Add(source);
            DictionaryKey = runtimeId.Length > 0
                ? $"{snapshot.ProcessId}|{runtimeId}"
                : $"no-runtime-id|{snapshot.ProcessId}|{snapshot.ControlTypeId}|{snapshot.Name}|{snapshot.BoundsKey}";
        }

        public AutomationElement Element { get; }
        public UiElementSnapshot Snapshot { get; }
        public string RuntimeId { get; }
        public string ProcessName { get; }
        public int? ProviderSessionId { get; }
        public NativeWindowInfo? NativeRoot { get; }
        public NativePoint Cursor { get; }
        public int InteractiveSessionId { get; }
        public AncestorSnapshot ControlAncestors { get; }
        public AncestorSnapshot RawAncestors { get; }
        public bool? InvokePatternSupported { get; }
        public HashSet<string> Sources { get; } = new(StringComparer.Ordinal);
        public string DictionaryKey { get; }
        public int ProcessId => Snapshot.ProcessId;
        public int ControlTypeId => Snapshot.ControlTypeId;
        public bool IsMenuItem => Snapshot.ControlTypeId == MenuItemControlTypeId;
        public IntPtr NativeRootHandle => NativeRoot?.Handle ?? IntPtr.Zero;
        public string Name => Snapshot.Name;
        public bool IsInStartMenuContext => StartHostProcesses.Contains(ProcessName) ||
                                           ControlAncestors.Nodes.Any(IsStartMarker) ||
                                           RawAncestors.Nodes.Any(IsStartMarker);
        public bool ControlStartContext => IsStartContext(ProcessName, ControlAncestors.Nodes);
        public bool ControlHasOpened => ControlAncestors.Nodes.Any(IsOpenedMarker);
        public bool ControlHasPowerOptions => ControlAncestors.Nodes.Any(IsPowerOptionsMarker);
        public bool RawStartContext => IsStartContext(ProcessName, RawAncestors.Nodes);
        public bool RawHasOpened => RawAncestors.Nodes.Any(IsOpenedMarker);
        public bool RawHasPowerOptions => RawAncestors.Nodes.Any(IsPowerOptionsMarker);
        public bool ControlHierarchyMatches => ControlAncestors.IsComplete &&
                                               Snapshot.ControlTypeId == MenuItemControlTypeId &&
                                               HasHierarchy(ProcessName, ControlAncestors.Nodes);
        public bool RawHierarchyMatches => RawAncestors.IsComplete &&
                                           Snapshot.ControlTypeId == MenuItemControlTypeId &&
                                           HasHierarchy(ProcessName, RawAncestors.Nodes);
        public bool NativeRootMatches => NativeRoot is not null && NativeRoot.ProcessId == ProcessId;
        public bool CursorInsideBounds => IsCursorInside(Snapshot.BoundingRectangle, Cursor);
        public bool CanInvoke => IsCandidateSafe(ToEvidence(this));
        public string StabilityKey => $"{DictionaryKey}|{NativeRoot?.Handle.ToInt64():X}|{Snapshot.BoundsKey}";
    }

    private sealed record InvocationEvidence(
        string Name,
        int ControlTypeId,
        bool ControlPathComplete,
        bool RawPathComplete,
        bool ControlHierarchyMatches,
        bool RawHierarchyMatches,
        int? ProviderSessionId,
        int InteractiveSessionId,
        bool NativeRootMatches,
        bool NativeRootIsAllowed,
        bool NativeRootIsVisible,
        bool IsEnabled,
        bool IsVisible,
        bool CursorInsideBounds,
        bool HasRuntimeId,
        bool? InvokePatternSupported);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }
}
