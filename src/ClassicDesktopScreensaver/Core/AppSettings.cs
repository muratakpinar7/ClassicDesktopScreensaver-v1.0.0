namespace ClassicDesktopScreensaver.Core;

public enum AppLanguage
{
    Auto,
    Turkish,
    English
}

public enum HotCorner
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
}

public sealed class AppSettings
{
    public AppLanguage Language { get; set; } = AppLanguage.Auto;

    public bool AutoScreenSaverEnabled { get; set; } = true;
    public int ScreenSaverIdleMinutes { get; set; } = 2;

    public bool LockEnabled { get; set; } = false;
    public int LockIdleMinutes { get; set; } = 10;

    public bool BlockScreenSaverWhileMediaPlaying { get; set; } = true;
    public bool BlockScreenSaverWhileFullscreen { get; set; } = true;
    public bool SuspendLockWhileMediaOrPresentation { get; set; } = true;

    public bool HotCornerEnabled { get; set; } = false;
    public HotCorner HotCorner { get; set; } = HotCorner.TopRight;
    public int HotCornerDelayMs { get; set; } = 500;

    public string ScreenSaverPath { get; set; } = string.Empty;

    public bool StartScreenSaverHotkeyEnabled { get; set; } = false;
    public string StartScreenSaverHotkey { get; set; } = "Shift+ScrollLock";
    public bool ToggleScreenSaverHotkeyEnabled { get; set; } = false;
    public string ToggleScreenSaverHotkey { get; set; } = "ScrollLock";
    public bool SleepHotkeyEnabled { get; set; } = false;
    public string SleepHotkey { get; set; } = "Pause";
    public bool HibernateHotkeyEnabled { get; set; } = false;
    public string HibernateHotkey { get; set; } = "Shift+Pause";

    public bool ShowStatusNotifications { get; set; } = true;
    public bool ShowTrayStatusIcon { get; set; } = true;
    public bool StartWithWindows { get; set; } = false;

    public AppSettings Clone() => (AppSettings)MemberwiseClone();
}
