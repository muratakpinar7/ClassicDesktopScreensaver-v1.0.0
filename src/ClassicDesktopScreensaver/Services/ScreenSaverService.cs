using System.Diagnostics;
using Microsoft.Win32;

namespace ClassicDesktopScreensaver.Services;

public sealed class ScreenSaverService
{
    private Process? _screenSaverProcess;

    public string? ReadWindowsScreenSaverPath()
    {
        try
        {
            using RegistryKey? key = Registry.CurrentUser.OpenSubKey(@"Control Panel\Desktop", writable: false);
            string? value = key?.GetValue("SCRNSAVE.EXE") as string;

            if (string.IsNullOrWhiteSpace(value))
                return null;

            return NormalizePath(value);
        }
        catch
        {
            return null;
        }
    }

    public string? ResolvePath(string? customPath)
    {
        string? path = string.IsNullOrWhiteSpace(customPath)
            ? ReadWindowsScreenSaverPath()
            : NormalizePath(customPath);

        return string.IsNullOrWhiteSpace(path) ? null : path;
    }

    public bool Start(string? customPath, out string? error)
    {
        error = null;

        try
        {
            if (_screenSaverProcess is { HasExited: false })
                return true;

            string? path = ResolvePath(customPath);

            if (string.IsNullOrWhiteSpace(path))
            {
                error = LocalizationService.T("NoSaverSelected");
                return false;
            }

            if (!File.Exists(path))
            {
                error = LocalizationService.F("SaverFileNotFound", path);
                return false;
            }

            var psi = new ProcessStartInfo
            {
                FileName = path,
                Arguments = "/s",
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(path) ?? AppContext.BaseDirectory
            };

            _screenSaverProcess = Process.Start(psi);
            if (_screenSaverProcess is not null)
            {
                _screenSaverProcess.EnableRaisingEvents = true;
                _screenSaverProcess.Exited += (_, _) => _screenSaverProcess = null;
            }

            return true;
        }
        catch (Exception ex)
        {
            error = LocalizationService.F("SaverStartFailed", ex.Message);
            return false;
        }
    }

    public void OpenWindowsScreenSaverSettings()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "control.exe",
                Arguments = "desk.cpl,,@screensaver",
                UseShellExecute = true
            });
        }
        catch
        {
            // Non-critical helper action.
        }
    }

    private static string NormalizePath(string path)
    {
        string expanded = Environment.ExpandEnvironmentVariables(path.Trim());
        return expanded.Trim('"');
    }
}
