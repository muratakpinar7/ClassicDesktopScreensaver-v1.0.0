using System.Windows;
using System.Windows.Threading;

namespace ClassicDesktopScreensaver.UI;

public partial class StatusPopupWindow : Window
{
    private readonly DispatcherTimer _timer;

    public StatusPopupWindow(string message)
    {
        InitializeComponent();
        MessageText.Text = message;

        Loaded += (_, _) => PositionWindow();

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(3)
        };
        _timer.Tick += (_, _) =>
        {
            _timer.Stop();
            Close();
        };
        _timer.Start();
    }

    private void PositionWindow()
    {
        Rect work = SystemParameters.WorkArea;
        // SizeToContent has measured the text by Loaded. Use actual height
        // so multi-line notifications are fully visible, not clipped.
        Left = Math.Max(work.Left, work.Right - ActualWidth - 16);
        Top = Math.Max(work.Top, work.Bottom - ActualHeight - 16);
    }
}
