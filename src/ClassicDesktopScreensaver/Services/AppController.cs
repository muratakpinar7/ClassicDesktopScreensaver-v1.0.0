using System.Windows.Threading;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed class AppController : IDisposable
{
    private readonly SettingsStore _store;
    private readonly GlobalKeyboardHook _keyboard;
    private readonly IdleTimeService _idle = new();
    private readonly ScreenSaverService _screenSaver = new();
    private readonly MediaGuardService _guards = new();
    private readonly PowerService _power = new();
    private readonly HotCornerMonitor _hotCorner = new();
    private readonly StartupManager _startup = new();
    private readonly NotificationService _notifications = new();
    private readonly TrayService _tray = new();

    private readonly DispatcherTimer _timer;

    private bool _screenSaverFiredThisIdlePeriod;
    private bool _lockFiredThisIdlePeriod;
    private bool _tickBusy;
    private long _lastSleepRequestTick;

    public AppSettings Settings { get; private set; }

    public event Action? OpenSettingsRequested;
    public event Action? ExitRequested;

    public AppController()
    {
        _store = new SettingsStore();
        Settings = _store.Load();
        LocalizationService.Apply(Settings.Language);

        _keyboard = new GlobalKeyboardHook();
        _keyboard.ShortcutPressed += OnShortcutPressed;

        _hotCorner.Triggered += () =>
        {
            _idle.MarkActivity();
            StartScreenSaver(showErrors: true);
        };

        _tray.OpenRequested += () => OpenSettingsRequested?.Invoke();
        _tray.StartRequested += () =>
        {
            _idle.MarkActivity();
            StartScreenSaver(showErrors: true);
        };
        _tray.ToggleRequested += ToggleAutoScreenSaver;
        _tray.ExitRequested += () => ExitRequested?.Invoke();

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(500)
        };
        _timer.Tick += OnTimerTick;

        ApplyRuntimeSettings();
        _timer.Start();
    }

    public void SetKeyboardSuspended(bool suspended)
    {
        _keyboard.Suspended = suspended;
    }

    public void HandleSystemResume()
    {
        // Sleep/hibernate can interrupt a key sequence before Windows sends
        // key-up messages. Never carry those transient states into the new
        // power session.
        _keyboard.ResetTransientState();
        _idle.MarkActivity();
        _screenSaverFiredThisIdlePeriod = false;
        _lockFiredThisIdlePeriod = false;
        _lastSleepRequestTick = 0;
    }

    /// <summary>
    /// Apply a complete, validated snapshot immediately and persist it.
    /// A failed update restores the previous live settings instead of
    /// leaving hotkeys and settings.ini out of sync.
    /// </summary>
    public void UpdateSettings(AppSettings candidate)
    {
        ValidateHotkeys(candidate);

        AppSettings previous = Settings;
        Settings = candidate;
        try
        {
            ApplyRuntimeSettings(updateStartup: previous.StartWithWindows != candidate.StartWithWindows);
            _store.Save(candidate);
            _idle.MarkActivity();
            _screenSaverFiredThisIdlePeriod = false;
            _lockFiredThisIdlePeriod = false;
        }
        catch
        {
            Settings = previous;
            try { ApplyRuntimeSettings(updateStartup: previous.StartWithWindows != candidate.StartWithWindows); }
            catch { /* Preserve the original error. */ }
            throw;
        }
    }

    public void RestoreAllDefaults() => UpdateSettings(new AppSettings());

    public string? GetWindowsScreenSaverPath() => _screenSaver.ReadWindowsScreenSaverPath();

    public void OpenWindowsScreenSaverSettings() => _screenSaver.OpenWindowsScreenSaverSettings();

    public void EnsureTrayVisible()
    {
        Settings.ShowTrayStatusIcon = true;

        try
        {
            _store.Save(Settings);
        }
        catch
        {
            // Runtime behavior should still work even if the INI cannot be saved.
        }

        _tray.Apply(visible: true, enabled: Settings.AutoScreenSaverEnabled);
    }

    public void StartScreenSaverNow()
    {
        _idle.MarkActivity();
        StartScreenSaver(showErrors: true);
    }

    public void StartWindowsScreenSaverNow()
    {
        _idle.MarkActivity();
        StartScreenSaverPath(null, showErrors: true);
    }

    public void TestCustomScreenSaver(string path)
    {
        _idle.MarkActivity();
        StartScreenSaverPath(path, showErrors: true);
    }

    private void ApplyRuntimeSettings(bool updateStartup = true)
    {
        var shortcuts = new Dictionary<ShortcutAction, HotkeyGesture>();

        if (Settings.StartScreenSaverHotkeyEnabled)
            shortcuts[ShortcutAction.StartScreenSaver] =
                ParseOrDefault(Settings.StartScreenSaverHotkey, "Shift+ScrollLock");

        if (Settings.ToggleScreenSaverHotkeyEnabled)
            shortcuts[ShortcutAction.ToggleScreenSaver] =
                ParseOrDefault(Settings.ToggleScreenSaverHotkey, "ScrollLock");

        if (Settings.SleepHotkeyEnabled)
            shortcuts[ShortcutAction.Sleep] =
                ParseOrDefault(Settings.SleepHotkey, "Pause");

        if (Settings.HibernateHotkeyEnabled)
            shortcuts[ShortcutAction.Hibernate] =
                ParseOrDefault(Settings.HibernateHotkey, "Shift+Pause");

        LocalizationService.Apply(Settings.Language);
        _keyboard.SetShortcuts(shortcuts);
        _hotCorner.Apply(Settings);
        _tray.RefreshLanguage();
        _tray.Apply(Settings.ShowTrayStatusIcon, Settings.AutoScreenSaverEnabled);

        if (updateStartup)
            _startup.Apply(Settings.StartWithWindows);
    }

    private static HotkeyGesture ParseOrDefault(string value, string fallback)
    {
        if (HotkeyGesture.TryParse(value, out var gesture) && gesture is not null)
            return gesture;

        HotkeyGesture.TryParse(fallback, out gesture);
        return gesture!;
    }

    private static void ValidateHotkeys(AppSettings settings)
    {
        var values = new List<string>();

        if (settings.StartScreenSaverHotkeyEnabled)
            values.Add(settings.StartScreenSaverHotkey);

        if (settings.ToggleScreenSaverHotkeyEnabled)
            values.Add(settings.ToggleScreenSaverHotkey);

        if (settings.SleepHotkeyEnabled)
            values.Add(settings.SleepHotkey);

        if (settings.HibernateHotkeyEnabled)
            values.Add(settings.HibernateHotkey);

        var normalized = new List<string>();

        foreach (string value in values)
        {
            if (!HotkeyGesture.TryParse(value, out var gesture) || gesture is null)
                throw new InvalidOperationException(LocalizationService.F("InvalidShortcut", value));

            normalized.Add(gesture.ToString());
        }

        if (normalized.Distinct(StringComparer.OrdinalIgnoreCase).Count() != normalized.Count)
            throw new InvalidOperationException(LocalizationService.T("DuplicateShortcut"));
    }

    private async void OnShortcutPressed(ShortcutAction action)
    {
        _idle.MarkActivity();
        _screenSaverFiredThisIdlePeriod = false;
        _lockFiredThisIdlePeriod = false;

        switch (action)
        {
            case ShortcutAction.StartScreenSaver:
                StartScreenSaver(showErrors: true);
                break;

            case ShortcutAction.ToggleScreenSaver:
                ToggleAutoScreenSaver();
                break;

            case ShortcutAction.Sleep:
                // Avoid queuing duplicate sleep actions.
                long now = Environment.TickCount64;
                if (_lastSleepRequestTick != 0 &&
                    now - _lastSleepRequestTick < 1800)
                    break;
                _lastSleepRequestTick = now;

                // Let the original Pause key sequence settle before handling sleep.
                _keyboard.ResetTransientState();
                await Task.Delay(850);
                // The synchronous broadcast can wait on window procedures;
                // keep it off both the low-level hook and the WPF UI thread.
                (bool Succeeded, string? Error) sleepResult = await Task.Run(() =>
                {
                    bool succeeded = _power.Sleep(out string? error);
                    return (succeeded, error);
                });
                if (!sleepResult.Succeeded && !string.IsNullOrWhiteSpace(sleepResult.Error))
                    _notifications.Show(sleepResult.Error);
                _keyboard.ResetTransientState();
                break;

            case ShortcutAction.Hibernate:
                _keyboard.ResetTransientState();
                if (!_power.Hibernate(out string? hibernateError) && !string.IsNullOrWhiteSpace(hibernateError))
                    _notifications.Show(hibernateError);
                _keyboard.ResetTransientState();
                break;
        }
    }

    private void ToggleAutoScreenSaver()
    {
        _idle.MarkActivity();
        Settings.AutoScreenSaverEnabled = !Settings.AutoScreenSaverEnabled;

        try
        {
            _store.Save(Settings);
        }
        catch
        {
            // The runtime state still changes even if the INI cannot be written.
        }

        _tray.UpdateStatus(Settings.AutoScreenSaverEnabled);

        if (Settings.ShowStatusNotifications)
        {
            _notifications.ShowScreenSaverStatus(Settings.AutoScreenSaverEnabled);
        }
    }

    private async void OnTimerTick(object? sender, EventArgs e)
    {
        if (_tickBusy)
            return;

        _tickBusy = true;

        try
        {
            TimeSpan idle = _idle.GetIdleTime();

            if (idle < TimeSpan.FromSeconds(1))
            {
                _screenSaverFiredThisIdlePeriod = false;
                _lockFiredThisIdlePeriod = false;
            }

            bool saverDue =
                Settings.AutoScreenSaverEnabled &&
                !_screenSaverFiredThisIdlePeriod &&
                idle >= TimeSpan.FromMinutes(Settings.ScreenSaverIdleMinutes);

            bool lockDue =
                Settings.LockEnabled &&
                !_lockFiredThisIdlePeriod &&
                idle >= TimeSpan.FromMinutes(Settings.LockIdleMinutes);

            if (!saverDue && !lockDue)
                return;

            bool needMedia = saverDue && Settings.BlockScreenSaverWhileMediaPlaying;
            bool needFullscreen = saverDue && Settings.BlockScreenSaverWhileFullscreen;

            if (lockDue && Settings.SuspendLockWhileMediaOrPresentation)
            {
                needMedia = true;
                needFullscreen = true;
            }

            GuardStatus status = (needMedia || needFullscreen)
                ? await _guards.GetStatusAsync(needMedia, needFullscreen)
                : new GuardStatus(false, false, false);

            if (saverDue)
            {
                bool blocked =
                    (Settings.BlockScreenSaverWhileMediaPlaying && status.MediaPlaying) ||
                    (Settings.BlockScreenSaverWhileFullscreen &&
                        (status.FullScreenApplication || status.PresentationMode));

                if (!blocked)
                {
                    if (StartScreenSaver(showErrors: false))
                        _screenSaverFiredThisIdlePeriod = true;
                }
            }

            if (lockDue)
            {
                bool blocked =
                    Settings.SuspendLockWhileMediaOrPresentation && status.Any;

                if (!blocked)
                {
                    if (NativeMethods.LockWorkStation())
                        _lockFiredThisIdlePeriod = true;
                }
            }
        }
        finally
        {
            _tickBusy = false;
        }
    }

    private bool StartScreenSaver(bool showErrors) =>
        StartScreenSaverPath(Settings.ScreenSaverPath, showErrors);

    private bool StartScreenSaverPath(string? path, bool showErrors)
    {
        bool ok = _screenSaver.Start(path, out string? error);

        if (!ok && showErrors && !string.IsNullOrWhiteSpace(error))
            _notifications.Show(error);

        return ok;
    }

    public void Dispose()
    {
        _timer.Stop();
        _keyboard.Dispose();
        _hotCorner.Dispose();
        _tray.Dispose();
    }
}
