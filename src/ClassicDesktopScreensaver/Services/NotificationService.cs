using ClassicDesktopScreensaver.UI;
using Microsoft.Toolkit.Uwp.Notifications;
using System.Diagnostics;

namespace ClassicDesktopScreensaver.Services;

public sealed class NotificationService
{
    private StatusPopupWindow? _current;

    // Toggle notifications use the Windows Notification Center, not an overlay.
    // The green check and red cross are native colored Windows emoji glyphs.
    public void ShowScreenSaverStatus(bool enabled)
    {
        string icon = enabled ? "✅" : "❌";
        string text = enabled ? LocalizationService.T("Active") : LocalizationService.T("Inactive");
        try
        {
            new ToastContentBuilder()
                .AddText($"{icon} {text}")
                .Show();
        }
        catch (Exception ex)
        {
            // Windows may suppress notifications via policy/Focus Assist, or
            // portable deployments may not have notification registration.
            // Never impersonate a Windows toast using our own overlay.
            Debug.WriteLine("Windows toast notification failed: " + ex);
        }
    }

    // Called by Setup.cmd before deleting the EXE, to clean the notification
    // platform's per-user COM/AppUserModelID registration. Not app settings.
    public static void UnregisterForUninstall()
    {
        try { ToastNotificationManagerCompat.Uninstall(); }
        catch (Exception ex) { Debug.WriteLine("Toast uninstall: " + ex); }
    }

    // Errors continue to use the existing in-app popup.
    public void Show(string message)
    {
        try { _current?.Close(); }
        catch { }

        _current = new StatusPopupWindow(message);
        _current.Closed += (_, _) => _current = null;
        _current.Show();
    }
}
