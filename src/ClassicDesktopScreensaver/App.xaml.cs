using System.Threading;
using System.Windows;
using System.Windows.Interop;
using ClassicDesktopScreensaver.Services;

namespace ClassicDesktopScreensaver;

public partial class App : Application
{
    private const string MutexName = @"Local\ClassicDesktopScreensaver_SingleInstance";
    private const string ShowEventName = @"Local\ClassicDesktopScreensaver_ShowSettings";

    private Mutex? _singleInstanceMutex;
    private EventWaitHandle? _showEvent;
    private Thread? _showEventThread;
    private AppController? _controller;
    private MainWindow? _mainWindow;
    private HwndSource? _mainWindowSource;

    private const int WmPowerBroadcast = 0x0218;
    private const int PbtApmResumeCritical = 0x0006;
    private const int PbtApmResumeSuspend = 0x0007;
    private const int PbtApmResumeAutomatic = 0x0012;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        LocalizationService.ApplyWindowsDefault();

        if (e.Args.Any(arg => string.Equals(arg,
            "--unregister-notifications", StringComparison.OrdinalIgnoreCase)))
        {
            NotificationService.UnregisterForUninstall();
            Shutdown();
            return;
        }

        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 17763))
        {
            MessageBox.Show(
                LocalizationService.T("UnsupportedWindows"),
                LocalizationService.T("UnsupportedWindowsTitle"),
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown();
            return;
        }

        _singleInstanceMutex = new Mutex(true, MutexName, out bool createdNew);

        if (!createdNew)
        {
            try
            {
                using var existingEvent = EventWaitHandle.OpenExisting(ShowEventName);
                existingEvent.Set();
            }
            catch
            {
                // The first instance may still be starting. Nothing else to do.
            }

            Shutdown();
            return;
        }

        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName);
        _showEventThread = new Thread(ShowEventLoop)
        {
            IsBackground = true,
            Name = "ShowSettingsListener"
        };
        _showEventThread.Start();

        _controller = new AppController();
        _mainWindow = new MainWindow(_controller);

        // Create the hidden settings-window handle immediately so the app can
        // receive WM_POWERBROADCAST even while it lives only in the tray.
        IntPtr windowHandle = new WindowInteropHelper(_mainWindow).EnsureHandle();
        _mainWindowSource = HwndSource.FromHwnd(windowHandle);
        _mainWindowSource?.AddHook(WindowMessageHook);

        _controller.OpenSettingsRequested += ShowSettings;
        _controller.ExitRequested += ExitApplication;

        // Tray-first behavior: the application always starts hidden.
        // Open Settings from the tray menu or by launching the EXE again.
}

    private IntPtr WindowMessageHook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WmPowerBroadcast)
        {
            int powerEvent = wParam.ToInt32();
            if (powerEvent is PbtApmResumeAutomatic or PbtApmResumeSuspend or PbtApmResumeCritical)
                _controller?.HandleSystemResume();
        }

        return IntPtr.Zero;
    }

    private void ShowEventLoop()
    {
        while (_showEvent is not null)
        {
            try
            {
                _showEvent.WaitOne();
                Dispatcher.BeginInvoke(ShowSettings);
            }
            catch
            {
                return;
            }
        }
    }

    private void ShowSettings()
    {
        if (_mainWindow is null)
            return;

        if (!_mainWindow.IsVisible)
        {
            // Tray shortcut toggles may have changed settings while hidden.
            _mainWindow.RefreshFromSettings();
            _mainWindow.Show();
        }

        if (_mainWindow.WindowState == WindowState.Minimized)
            _mainWindow.WindowState = WindowState.Normal;

        _mainWindow.Activate();
        _mainWindow.Topmost = true;
        _mainWindow.Topmost = false;
        _mainWindow.Focus();
    }

    private void ExitApplication()
    {
        // Unblock MainWindow.OnClosing so tray Exit terminates the process.
        _mainWindow?.PrepareForExit();
        Shutdown();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_mainWindowSource is not null)
        {
            _mainWindowSource.RemoveHook(WindowMessageHook);
            _mainWindowSource = null;
        }

        _controller?.Dispose();

        try
        {
            _showEvent?.Set();
            _showEvent?.Dispose();
        }
        catch { }

        try
        {
            _singleInstanceMutex?.ReleaseMutex();
            _singleInstanceMutex?.Dispose();
        }
        catch { }

        base.OnExit(e);
    }
}
