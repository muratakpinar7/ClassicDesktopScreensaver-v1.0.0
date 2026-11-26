using System.Globalization;
using System.Text;

namespace ClassicDesktopScreensaver.Core;

public sealed class SettingsStore
{
    public string SettingsPath { get; }

    public SettingsStore()
    {
        SettingsPath = Path.Combine(AppContext.BaseDirectory, "settings.ini");
    }

    public AppSettings Load()
    {
        var s = new AppSettings();

        if (!File.Exists(SettingsPath))
            return s;

        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        string section = "";

        foreach (string raw in File.ReadAllLines(SettingsPath, Encoding.UTF8))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(';') || line.StartsWith('#'))
                continue;

            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                section = line[1..^1].Trim();
                continue;
            }

            int eq = line.IndexOf('=');
            if (eq <= 0)
                continue;

            string key = line[..eq].Trim();
            string value = line[(eq + 1)..].Trim();
            values[$"{section}.{key}"] = value;
        }

        if (Enum.TryParse(Get(values, "General.Language", s.Language.ToString()), true, out AppLanguage language))
            s.Language = language;

        s.AutoScreenSaverEnabled = GetBool(values, "General.AutoScreenSaverEnabled", s.AutoScreenSaverEnabled);
        s.ScreenSaverIdleMinutes = GetInt(values, "General.ScreenSaverIdleMinutes", s.ScreenSaverIdleMinutes, 1, 1440);
        s.LockEnabled = GetBool(values, "General.LockEnabled", s.LockEnabled);
        s.LockIdleMinutes = GetInt(values, "General.LockIdleMinutes", s.LockIdleMinutes, 1, 1440);
        s.ScreenSaverPath = Get(values, "General.ScreenSaverPath", s.ScreenSaverPath);
        s.ShowStatusNotifications = GetBool(values, "General.ShowStatusNotifications", s.ShowStatusNotifications);
        s.ShowTrayStatusIcon = GetBool(values, "General.ShowTrayStatusIcon", s.ShowTrayStatusIcon);
        s.StartWithWindows = GetBool(values, "General.StartWithWindows", s.StartWithWindows);

        s.BlockScreenSaverWhileMediaPlaying = GetBool(values, "Guards.BlockScreenSaverWhileMediaPlaying", s.BlockScreenSaverWhileMediaPlaying);
        s.BlockScreenSaverWhileFullscreen = GetBool(values, "Guards.BlockScreenSaverWhileFullscreen", s.BlockScreenSaverWhileFullscreen);
        s.SuspendLockWhileMediaOrPresentation = GetBool(values, "Guards.SuspendLockWhileMediaOrPresentation", s.SuspendLockWhileMediaOrPresentation);

        s.HotCornerEnabled = GetBool(values, "HotCorner.Enabled", s.HotCornerEnabled);
        s.HotCornerDelayMs = GetInt(values, "HotCorner.DelayMs", s.HotCornerDelayMs, 0, 5000);
        if (Enum.TryParse(Get(values, "HotCorner.Corner", s.HotCorner.ToString()), true, out HotCorner corner))
            s.HotCorner = corner;

        s.StartScreenSaverHotkeyEnabled = GetBool(values, "Hotkeys.StartScreenSaverEnabled", s.StartScreenSaverHotkeyEnabled);
        s.StartScreenSaverHotkey = Get(values, "Hotkeys.StartScreenSaver", s.StartScreenSaverHotkey);
        s.ToggleScreenSaverHotkeyEnabled = GetBool(values, "Hotkeys.ToggleScreenSaverEnabled", s.ToggleScreenSaverHotkeyEnabled);
        s.ToggleScreenSaverHotkey = Get(values, "Hotkeys.ToggleScreenSaver", s.ToggleScreenSaverHotkey);
        s.SleepHotkeyEnabled = GetBool(values, "Hotkeys.SleepEnabled", s.SleepHotkeyEnabled);
        s.SleepHotkey = Get(values, "Hotkeys.Sleep", s.SleepHotkey);
        s.HibernateHotkeyEnabled = GetBool(values, "Hotkeys.HibernateEnabled", s.HibernateHotkeyEnabled);
        s.HibernateHotkey = Get(values, "Hotkeys.Hibernate", s.HibernateHotkey);

        return s;
    }

    public void Save(AppSettings s)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);

        var sb = new StringBuilder();
        sb.AppendLine("; Classic Desktop Screensaver");
        sb.AppendLine("; No application settings are written to the Windows Registry.");
        sb.AppendLine();

        sb.AppendLine("[General]");
        sb.AppendLine($"Language={s.Language}");
        sb.AppendLine($"AutoScreenSaverEnabled={B(s.AutoScreenSaverEnabled)}");
        sb.AppendLine($"ScreenSaverIdleMinutes={s.ScreenSaverIdleMinutes.ToString(CultureInfo.InvariantCulture)}");
        sb.AppendLine($"LockEnabled={B(s.LockEnabled)}");
        sb.AppendLine($"LockIdleMinutes={s.LockIdleMinutes.ToString(CultureInfo.InvariantCulture)}");
        sb.AppendLine($"ScreenSaverPath={s.ScreenSaverPath}");
        sb.AppendLine($"ShowStatusNotifications={B(s.ShowStatusNotifications)}");
        sb.AppendLine($"ShowTrayStatusIcon={B(s.ShowTrayStatusIcon)}");
        sb.AppendLine($"StartWithWindows={B(s.StartWithWindows)}");
        sb.AppendLine();

        sb.AppendLine("[Guards]");
        sb.AppendLine($"BlockScreenSaverWhileMediaPlaying={B(s.BlockScreenSaverWhileMediaPlaying)}");
        sb.AppendLine($"BlockScreenSaverWhileFullscreen={B(s.BlockScreenSaverWhileFullscreen)}");
        sb.AppendLine($"SuspendLockWhileMediaOrPresentation={B(s.SuspendLockWhileMediaOrPresentation)}");
        sb.AppendLine();

        sb.AppendLine("[HotCorner]");
        sb.AppendLine($"Enabled={B(s.HotCornerEnabled)}");
        sb.AppendLine($"Corner={s.HotCorner}");
        sb.AppendLine($"DelayMs={s.HotCornerDelayMs.ToString(CultureInfo.InvariantCulture)}");
        sb.AppendLine();

        sb.AppendLine("[Hotkeys]");
        sb.AppendLine($"StartScreenSaverEnabled={B(s.StartScreenSaverHotkeyEnabled)}");
        sb.AppendLine($"StartScreenSaver={s.StartScreenSaverHotkey}");
        sb.AppendLine($"ToggleScreenSaverEnabled={B(s.ToggleScreenSaverHotkeyEnabled)}");
        sb.AppendLine($"ToggleScreenSaver={s.ToggleScreenSaverHotkey}");
        sb.AppendLine($"SleepEnabled={B(s.SleepHotkeyEnabled)}");
        sb.AppendLine($"Sleep={s.SleepHotkey}");
        sb.AppendLine($"HibernateEnabled={B(s.HibernateHotkeyEnabled)}");
        sb.AppendLine($"Hibernate={s.HibernateHotkey}");

        string temp = SettingsPath + ".tmp";
        File.WriteAllText(temp, sb.ToString(), new UTF8Encoding(false));
        File.Move(temp, SettingsPath, true);
    }

    private static string Get(Dictionary<string, string> v, string key, string fallback)
        => v.TryGetValue(key, out var value) ? value : fallback;

    private static bool GetBool(Dictionary<string, string> v, string key, bool fallback)
        => v.TryGetValue(key, out var value) && bool.TryParse(value, out bool result) ? result : fallback;

    private static int GetInt(Dictionary<string, string> v, string key, int fallback, int min, int max)
    {
        if (!v.TryGetValue(key, out var value) ||
            !int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int result))
            return fallback;

        return Math.Clamp(result, min, max);
    }

    private static string B(bool value) => value ? "true" : "false";
}
