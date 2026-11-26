using System.Drawing;
using System.Drawing.Drawing2D;
using Forms = System.Windows.Forms;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed class TrayService : IDisposable
{
    private readonly Forms.NotifyIcon _notifyIcon;
    private readonly Forms.ContextMenuStrip _menu;
    private readonly Forms.ToolStripMenuItem _openItem;
    private readonly Forms.ToolStripMenuItem _startItem;
    private readonly Forms.ToolStripMenuItem _statusItem;
    private readonly Forms.ToolStripMenuItem _exitItem;
    private readonly Bitmap _settingsImage;
    private readonly Bitmap _startImage;
    private readonly Bitmap _enabledImage;
    private readonly Bitmap _disabledImage;
    private readonly Bitmap _exitImage;
    private Icon? _enabledIcon;
    private Icon? _disabledIcon;
    private bool _enabled = true;

    public event Action? OpenRequested;
    public event Action? StartRequested;
    public event Action? ToggleRequested;
    public event Action? ExitRequested;

    public TrayService()
    {
        _enabledIcon = LoadTrayIcon("TrayActive.ico");
        _disabledIcon = LoadTrayIcon("TrayInactive.ico");

        // Tray menu images are rendered locally; no extra external files are
        // needed after installing the single-file executable.
        _settingsImage = DrawMenuImage(MenuGlyph.Settings);
        _startImage = DrawMenuImage(MenuGlyph.Start);
        _enabledImage = DrawMenuImage(MenuGlyph.Enabled);
        _disabledImage = DrawMenuImage(MenuGlyph.Disabled);
        _exitImage = DrawMenuImage(MenuGlyph.Exit);

        _menu = new Forms.ContextMenuStrip
        {
            ImageScalingSize = new Size(24, 24),
            ShowImageMargin = true
        };

        _openItem = new Forms.ToolStripMenuItem { Image = _settingsImage };
        _openItem.Click += (_, _) => OpenRequested?.Invoke();

        _startItem = new Forms.ToolStripMenuItem { Image = _startImage };
        _startItem.Click += (_, _) => StartRequested?.Invoke();

        _statusItem = new Forms.ToolStripMenuItem { Image = _enabledImage };
        _statusItem.Click += (_, _) => ToggleRequested?.Invoke();

        _exitItem = new Forms.ToolStripMenuItem { Image = _exitImage };
        _exitItem.Click += (_, _) => ExitRequested?.Invoke();

        _menu.Items.Add(_openItem);
        _menu.Items.Add(_startItem);
        _menu.Items.Add(_statusItem);
        _menu.Items.Add(new Forms.ToolStripSeparator());
        _menu.Items.Add(_exitItem);

        _notifyIcon = new Forms.NotifyIcon
        {
            ContextMenuStrip = _menu,
            Icon = _enabledIcon,
            Visible = false
        };

        _notifyIcon.DoubleClick += (_, _) => OpenRequested?.Invoke();

        RefreshLanguage();
        UpdateStatus(true);
    }

    public void Apply(bool visible, bool enabled)
    {
        UpdateStatus(enabled);
        _notifyIcon.Visible = visible;
    }

    public void RefreshLanguage()
    {
        _openItem.Text = LocalizationService.T("TraySettings");
        _startItem.Text = LocalizationService.T("TrayStart");
        _exitItem.Text = LocalizationService.T("TrayExit");
        UpdateStatus(_enabled);
    }

    public void UpdateStatus(bool enabled)
    {
        _enabled = enabled;
        _notifyIcon.Icon = enabled ? _enabledIcon : _disabledIcon;
        _notifyIcon.Text = enabled ? LocalizationService.T("TrayActive") : LocalizationService.T("TrayInactive");
        _statusItem.Text = enabled ? LocalizationService.T("TrayStatusActive") : LocalizationService.T("TrayStatusInactive");
        _statusItem.Image = enabled ? _enabledImage : _disabledImage;
    }

    private enum MenuGlyph { Settings, Start, Enabled, Disabled, Exit }

    private static Bitmap DrawMenuImage(MenuGlyph glyph)
    {
        const int dimension = 24;
        var bitmap = new Bitmap(dimension, dimension, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
        using Graphics g = Graphics.FromImage(bitmap);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        g.Clear(Color.Transparent);

        switch (glyph)
        {
            case MenuGlyph.Settings:
                using (var blue = new SolidBrush(Color.FromArgb(45, 119, 208)))
                using (var hole = new SolidBrush(Color.White))
                {
                    for (int i = 0; i < 8; i++)
                    {
                        GraphicsState state = g.Save();
                        g.TranslateTransform(12, 12);
                        g.RotateTransform(i * 45);
                        g.FillRectangle(blue, -2.3f, -10.6f, 4.6f, 6.3f);
                        g.Restore(state);
                    }
                    g.FillEllipse(blue, 5.2f, 5.2f, 13.6f, 13.6f);
                    g.FillEllipse(hole, 9.0f, 9.0f, 6.0f, 6.0f);
                }
                break;

            case MenuGlyph.Start:
                using (var frame = new Pen(Color.FromArgb(60, 99, 157), 2.1f))
                using (var screen = new SolidBrush(Color.FromArgb(232, 244, 255)))
                using (var play = new SolidBrush(Color.FromArgb(23, 160, 100)))
                using (var stem = new Pen(Color.FromArgb(60, 99, 157), 2f))
                {
                    g.FillRectangle(screen, 2.6f, 3.5f, 18.8f, 13.5f);
                    g.DrawRectangle(frame, 2.6f, 3.5f, 18.8f, 13.5f);
                    g.FillPolygon(play, new[] {
                        new PointF(9.4f, 6.4f), new PointF(9.4f, 14.5f),
                        new PointF(16.4f, 10.5f)
                    });
                    g.DrawLine(stem, 12f, 17.5f, 12f, 20.6f);
                    g.DrawLine(stem, 7.5f, 20.8f, 16.5f, 20.8f);
                }
                break;

            case MenuGlyph.Enabled:
                using (var green = new SolidBrush(Color.FromArgb(34, 172, 90)))
                using (var white = new Pen(Color.White, 2.8f))
                {
                    white.StartCap = LineCap.Round;
                    white.EndCap = LineCap.Round;
                    white.LineJoin = LineJoin.Round;
                    g.FillEllipse(green, 2, 2, 20, 20);
                    g.DrawLines(white, new[] {
                        new PointF(6.8f, 12.1f), new PointF(10.5f, 15.9f),
                        new PointF(17.8f, 8.0f)
                    });
                }
                break;

            case MenuGlyph.Disabled:
                using (var red = new SolidBrush(Color.FromArgb(221, 59, 60)))
                using (var white = new Pen(Color.White, 2.7f))
                {
                    white.StartCap = LineCap.Round;
                    white.EndCap = LineCap.Round;
                    g.FillEllipse(red, 2, 2, 20, 20);
                    g.DrawLine(white, 8f, 8f, 16f, 16f);
                    g.DrawLine(white, 16f, 8f, 8f, 16f);
                }
                break;

            case MenuGlyph.Exit:
                using (var door = new Pen(Color.FromArgb(78, 93, 118), 2f))
                using (var arrow = new Pen(Color.FromArgb(217, 91, 55), 2.3f))
                {
                    arrow.StartCap = LineCap.Round;
                    arrow.EndCap = LineCap.Round;
                    arrow.LineJoin = LineJoin.Round;
                    g.DrawLines(door, new[] {
                        new PointF(12.6f, 4f), new PointF(4.5f, 4f),
                        new PointF(4.5f, 20f), new PointF(12.6f, 20f)
                    });
                    g.DrawLine(arrow, 8f, 12f, 21f, 12f);
                    g.DrawLines(arrow, new[] {
                        new PointF(16.5f, 7.5f), new PointF(21f, 12f),
                        new PointF(16.5f, 16.5f)
                    });
                }
                break;
        }

        return bitmap;
    }

    private static Icon LoadTrayIcon(string fileName)
    {
        try
        {
            var uri = new Uri($"pack://application:,,,/Assets/{fileName}", UriKind.Absolute);
            var resource = System.Windows.Application.GetResourceStream(uri);

            if (resource?.Stream is not null)
                return new Icon(resource.Stream);
        }
        catch
        {
            // Fall through to Windows' built-in application icon.
        }

        return (Icon)SystemIcons.Application.Clone();
    }

    public void Dispose()
    {
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
        _menu.Dispose();
        _enabledIcon?.Dispose();
        _disabledIcon?.Dispose();
        _settingsImage.Dispose();
        _startImage.Dispose();
        _enabledImage.Dispose();
        _disabledImage.Dispose();
        _exitImage.Dispose();
    }
}
