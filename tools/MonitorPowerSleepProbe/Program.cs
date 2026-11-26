using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Runtime.Versioning;

[SupportedOSPlatform("windows")]
internal static class Program
{
    private const uint WmSysCommand = 0x0112;
    private const uint ScMonitorPower = 0xF170;
    private const nint HwndBroadcast = 0xFFFF;
    private const nint MonitorOff = 2;
    private const uint SmtoAbortIfHung = 0x0002;
    private const uint SmtoErrorOnExit = 0x0020;
    private const uint RecipientTimeoutMilliseconds = 100;
    private const string StartPrompt = "ŞİMDİ TESTE BAŞLAYABİLİRSİN.";

    private static int Main(string[] args)
    {
        if (!OperatingSystem.IsWindows())
        {
            Console.Error.WriteLine("This probe only runs on Windows.");
            return 2;
        }

        if (args.Length == 0 || (args.Length == 1 && args[0] == "--help"))
            return ShowUsage();

        if (args.Length == 1 && args[0] == "--self-test")
            return RunSelfTest();

        if (args.Length == 1 && args[0] == "--request-monitor-off")
            return RequestMonitorOffOnce();

        Console.Error.WriteLine("Unknown arguments. Run with --help.");
        return 2;
    }

    private static int ShowUsage()
    {
        Console.WriteLine("Monitor Power Sleep Probe");
        Console.WriteLine("  --self-test            Check local constants only; no Windows message is sent.");
        Console.WriteLine("  --request-monitor-off  Send one SC_MONITORPOWER display-off request after confirmation.");
        Console.WriteLine();
        Console.WriteLine("The monitor-off request is not a Sleep API. Windows policy may independently enter Modern Standby.");
        Console.WriteLine("Do not use --request-monitor-off without explicit approval for a power-state test.");
        return 0;
    }

    private static int RunSelfTest()
    {
        bool valid = HwndBroadcast == (nint)0xFFFF &&
                     WmSysCommand == 0x0112 &&
                     ScMonitorPower == 0xF170 &&
                     MonitorOff == (nint)2 &&
                     RecipientTimeoutMilliseconds > 0 &&
                     (SmtoAbortIfHung & SmtoErrorOnExit) == 0;

        Console.WriteLine($"ConstantsValid={valid}");
        Console.WriteLine($"PointerSize={IntPtr.Size * 8}");
        Console.WriteLine("NativeMessageSent=False");
        Console.WriteLine("PowerActionRequested=False");
        return valid ? 0 : 1;
    }

    private static int RequestMonitorOffOnce()
    {
        using WindowsIdentity identity = WindowsIdentity.GetCurrent();
        using System.Diagnostics.Process process = System.Diagnostics.Process.GetCurrentProcess();
        bool isAdministrator = new WindowsPrincipal(identity)
            .IsInRole(WindowsBuiltInRole.Administrator);

        Console.WriteLine("Monitor Power Sleep Probe - standard-user check");
        Console.WriteLine($"WindowsUser={identity.Name}");
        Console.WriteLine($"IsAdministrator={isAdministrator}");
        Console.WriteLine($"ProcessId={Environment.ProcessId}");
        Console.WriteLine($"SessionId={process.SessionId}");
        Console.WriteLine($"ProcessArchitecture={RuntimeInformation.ProcessArchitecture}");
        Console.WriteLine("Request=one WM_SYSCOMMAND / SC_MONITORPOWER (lParam=2) to HWND_BROADCAST");
        Console.WriteLine("This requests display-off only; Windows may independently proceed into S0 Sleep.");
        Console.WriteLine("No Sleep/Hibernate API, retry, or fallback power action is used.");

        if (isAdministrator)
        {
            Console.WriteLine("Cancelled: this test must run without administrator privileges.");
            return 6;
        }

        if (Console.IsInputRedirected)
        {
            Console.WriteLine("Cancelled: an interactive PowerShell window is required.");
            return 3;
        }

        Console.WriteLine(StartPrompt);
        if (Console.ReadLine() != string.Empty)
        {
            Console.WriteLine("Cancelled; press Enter on the blank line to continue. No message was sent.");
            return 3;
        }

        string logPath = Path.Combine(
            Path.GetTempPath(),
            $"ClassicDesktopScreensaver-MonitorPowerSleepProbe-{DateTimeOffset.Now:yyyyMMdd-HHmmss-fff}-{Guid.NewGuid():N}.txt");

        string before = string.Join(
            Environment.NewLine,
            "Monitor Power Sleep Probe",
            $"RequestPreparedLocal={DateTimeOffset.Now:O}",
            $"WindowsUser={identity.Name}",
            $"IsAdministrator={isAdministrator}",
            $"ProcessId={Environment.ProcessId}",
            $"SessionId={process.SessionId}",
            $"ProcessArchitecture={RuntimeInformation.ProcessArchitecture}",
            $"PointerSizeBits={IntPtr.Size * 8}",
            $"Hwnd=0x{(nuint)HwndBroadcast:X}",
            $"Message=0x{WmSysCommand:X}",
            $"WParam=0x{ScMonitorPower:X}",
            $"LParam={(nint)MonitorOff}",
            $"RecipientTimeoutMilliseconds={RecipientTimeoutMilliseconds}",
            $"Flags=0x{(SmtoAbortIfHung | SmtoErrorOnExit):X}",
            "FallbackPowerAction=None",
            "SendResult=Pending",
            string.Empty);

        try
        {
            File.WriteAllText(logPath, before);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Could not write the pre-request log; cancelled: {exception.GetType().Name}");
            return 4;
        }

        Console.WriteLine($"Pre-request record: {logPath}");
        Console.WriteLine("Sending the single display-off message now.");

        Marshal.SetLastPInvokeError(0);
        nint succeeded = SendMessageTimeoutW(
            HwndBroadcast,
            WmSysCommand,
            (nuint)ScMonitorPower,
            MonitorOff,
            SmtoAbortIfHung | SmtoErrorOnExit,
            RecipientTimeoutMilliseconds,
            out nuint messageResult);
        int lastError = Marshal.GetLastPInvokeError();

        string outcome = string.Join(
            Environment.NewLine,
            $"RequestReturnedLocal={DateTimeOffset.Now:O}",
            $"SendMessageTimeoutSucceeded={succeeded != 0}",
            $"MessageResult=0x{messageResult:X}",
            $"LastErrorAfterCall={lastError}",
            $"LastErrorInterpretation={(succeeded != 0 ? "DiagnosticOnly_NonzeroApiReturnMeansSuccess" : "ConsultBecauseApiReturnWasZero")}",
            "Note=Broadcast result does not prove that Windows entered Sleep.",
            string.Empty);

        try
        {
            File.AppendAllText(logPath, outcome);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Could not append the post-request record: {exception.GetType().Name}");
            Console.Error.WriteLine("No retry or fallback power action was attempted.");
            return 5;
        }

        Console.WriteLine(outcome);
        Console.WriteLine($"ResultFile={logPath}");
        Console.WriteLine("No retry or fallback power action was attempted.");
        return succeeded != 0 ? 0 : 1;
    }

    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", SetLastError = true)]
    private static extern nint SendMessageTimeoutW(
        nint hWnd,
        uint message,
        nuint wParam,
        nint lParam,
        uint flags,
        uint timeoutMilliseconds,
        out nuint messageResult);
}
