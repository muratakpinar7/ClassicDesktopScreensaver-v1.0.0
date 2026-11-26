using System.Runtime.InteropServices;

namespace ClassicDesktopScreensaver.Services;

public sealed class StartupManager
{
    private const string ShortcutName = "Classic Desktop Screensaver.lnk";

    public string ShortcutPath =>
        Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.Startup),
            ShortcutName);

    public void Apply(bool enabled)
    {
        if (enabled)
            CreateShortcut();
        else
            RemoveShortcut();
    }

    private void CreateShortcut()
    {
        string? exe = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(exe))
            throw new InvalidOperationException("Uygulama yolu belirlenemedi.");

        Directory.CreateDirectory(Path.GetDirectoryName(ShortcutPath)!);

        Type? shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType is null)
            throw new InvalidOperationException("Windows Script Host bulunamadı.");

        object? shell = null;
        object? shortcut = null;

        try
        {
            shell = Activator.CreateInstance(shellType);
            dynamic ws = shell!;
            shortcut = ws.CreateShortcut(ShortcutPath);
            dynamic sc = shortcut;

            sc.TargetPath = exe;
            sc.Arguments = "--background";
            sc.WorkingDirectory = AppContext.BaseDirectory;
            sc.IconLocation = exe + ",0";
            sc.Description = "Classic Desktop Screensaver";
            sc.Save();
        }
        finally
        {
            if (shortcut is not null && Marshal.IsComObject(shortcut))
                Marshal.FinalReleaseComObject(shortcut);
            if (shell is not null && Marshal.IsComObject(shell))
                Marshal.FinalReleaseComObject(shell);
        }
    }

    private void RemoveShortcut()
    {
        if (File.Exists(ShortcutPath))
            File.Delete(ShortcutPath);
    }
}
