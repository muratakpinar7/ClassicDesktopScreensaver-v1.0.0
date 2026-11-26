using System.Text;
using Forms = System.Windows.Forms;
using Windows.Media.Control;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed record GuardStatus(
    bool MediaPlaying,
    bool FullScreenApplication,
    bool PresentationMode)
{
    public bool Any => MediaPlaying || FullScreenApplication || PresentationMode;
}

public sealed class MediaGuardService
{
    private GlobalSystemMediaTransportControlsSessionManager? _mediaManager;
    private bool _mediaManagerUnavailable;

    public async Task<GuardStatus> GetStatusAsync(bool checkMedia, bool checkFullscreen)
    {
        bool media = checkMedia && await IsMediaPlayingAsync();
        bool presentation = checkFullscreen && IsPresentationMode();
        bool fullscreen = checkFullscreen && IsForegroundWindowFullscreen();

        return new GuardStatus(media, fullscreen, presentation);
    }

    private async Task<bool> IsMediaPlayingAsync()
    {
        if (_mediaManagerUnavailable)
            return false;

        try
        {
            _mediaManager ??= await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();

            foreach (var session in _mediaManager.GetSessions())
            {
                var info = session.GetPlaybackInfo();
                if (info.PlaybackStatus != GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
                    continue;

                // Known music-only sessions should not prevent a screen saver.
                // Video and unknown playback types are treated conservatively as visual media.
                if (info.PlaybackType is null || info.PlaybackType == Windows.Media.MediaPlaybackType.Video)
                    return true;
            }
        }
        catch
        {
            // Some systems/policies may deny the global media session API.
            // Fail open: do not permanently block the screen saver.
            _mediaManagerUnavailable = true;
        }

        return false;
    }

    private static bool IsPresentationMode()
    {
        try
        {
            int hr = NativeMethods.SHQueryUserNotificationState(out var state);
            if (hr != 0)
                return false;

            return state is NativeMethods.QueryUserNotificationState.PresentationMode
                or NativeMethods.QueryUserNotificationState.RunningD3DFullScreen;
        }
        catch
        {
            return false;
        }
    }

    private static bool IsForegroundWindowFullscreen()
    {
        IntPtr hwnd = NativeMethods.GetForegroundWindow();

        if (hwnd == IntPtr.Zero || NativeMethods.IsIconic(hwnd))
            return false;

        var className = new StringBuilder(256);
        NativeMethods.GetClassName(hwnd, className, className.Capacity);

        string cls = className.ToString();
        if (cls is "Progman" or "WorkerW" or "Shell_TrayWnd")
            return false;

        if (!NativeMethods.GetWindowRect(hwnd, out var rect))
            return false;

        Forms.Screen screen = Forms.Screen.FromHandle(hwnd);
        var bounds = screen.Bounds;

        const int tolerance = 3;

        return Math.Abs(rect.Left - bounds.Left) <= tolerance &&
               Math.Abs(rect.Top - bounds.Top) <= tolerance &&
               Math.Abs(rect.Right - bounds.Right) <= tolerance &&
               Math.Abs(rect.Bottom - bounds.Bottom) <= tolerance;
    }
}
