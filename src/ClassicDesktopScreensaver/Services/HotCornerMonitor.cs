using System.Diagnostics;
using System.Windows.Threading;
using ClassicDesktopScreensaver.Core;
using Forms = System.Windows.Forms;

namespace ClassicDesktopScreensaver.Services;

public sealed class HotCornerMonitor : IDisposable
{
    private readonly DispatcherTimer _timer;
    private AppSettings _settings = new();
    private long? _enteredAt;
    private bool _triggered;

    public event Action? Triggered;

    public HotCornerMonitor()
    {
        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(100)
        };
        _timer.Tick += OnTick;
        _timer.Start();
    }

    public void Apply(AppSettings settings)
    {
        _settings = settings;
        _enteredAt = null;
        _triggered = false;
    }

    private void OnTick(object? sender, EventArgs e)
    {
        if (!_settings.HotCornerEnabled)
        {
            Reset();
            return;
        }

        if (!NativeMethods.GetCursorPos(out var p))
        {
            Reset();
            return;
        }

        var screen = Forms.Screen.PrimaryScreen;
        if (screen is null)
        {
            Reset();
            return;
        }

        var b = screen.Bounds;
        const int edge = 2;

        bool inside = _settings.HotCorner switch
        {
            HotCorner.TopLeft =>
                p.X <= b.Left + edge && p.Y <= b.Top + edge,

            HotCorner.TopRight =>
                p.X >= b.Right - 1 - edge && p.Y <= b.Top + edge,

            HotCorner.BottomLeft =>
                p.X <= b.Left + edge && p.Y >= b.Bottom - 1 - edge,

            HotCorner.BottomRight =>
                p.X >= b.Right - 1 - edge && p.Y >= b.Bottom - 1 - edge,

            _ => false
        };

        if (!inside)
        {
            Reset();
            return;
        }

        _enteredAt ??= Stopwatch.GetTimestamp();

        if (_triggered)
            return;

        TimeSpan elapsed = Stopwatch.GetElapsedTime(_enteredAt.Value);
        if (elapsed.TotalMilliseconds >= _settings.HotCornerDelayMs)
        {
            _triggered = true;
            Triggered?.Invoke();
        }
    }

    private void Reset()
    {
        _enteredAt = null;
        _triggered = false;
    }

    public void Dispose()
    {
        _timer.Stop();
    }
}
