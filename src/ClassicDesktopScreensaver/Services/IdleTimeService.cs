using System.Runtime.InteropServices;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed class IdleTimeService
{
    private DateTimeOffset _syntheticLastActivity = DateTimeOffset.Now;

    public void MarkActivity()
    {
        _syntheticLastActivity = DateTimeOffset.Now;
    }

    public TimeSpan GetIdleTime()
    {
        TimeSpan systemIdle = GetSystemIdleTime();
        TimeSpan syntheticIdle = DateTimeOffset.Now - _syntheticLastActivity;

        return systemIdle < syntheticIdle ? systemIdle : syntheticIdle;
    }

    private static TimeSpan GetSystemIdleTime()
    {
        var info = new NativeMethods.LASTINPUTINFO
        {
            cbSize = (uint)Marshal.SizeOf<NativeMethods.LASTINPUTINFO>()
        };

        if (!NativeMethods.GetLastInputInfo(ref info))
            return TimeSpan.Zero;

        uint now = NativeMethods.GetTickCount();
        uint elapsed = unchecked(now - info.dwTime);

        return TimeSpan.FromMilliseconds(elapsed);
    }
}
