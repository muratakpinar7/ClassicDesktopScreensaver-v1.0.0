using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;
using ClassicDesktopScreensaver.Core;
using ClassicDesktopScreensaver.Services;

namespace ClassicDesktopScreensaver;

public partial class MainWindow : Window
{
    private readonly AppController _controller;
    private bool _allowClose;
    private bool _loadingLanguage;
    private bool _loadingSettings = true;
    private readonly DispatcherTimer _numberSaveTimer = new()
    {
        Interval = TimeSpan.FromMilliseconds(550)
    };

    public MainWindow(AppController controller)
    {
        InitializeComponent();
        _controller = controller;

        Activated += (_, _) => _controller.SetKeyboardSuspended(true);
        Deactivated += (_, _) => _controller.SetKeyboardSuspended(false);
        IsVisibleChanged += (_, _) =>
        {
            if (!IsVisible)
                _controller.SetKeyboardSuspended(false);
        };

        LoadFromSettings();
        ApplyLanguage();
        UpdateCustomSaverControls();
        WireInstantSaveEvents();
        _numberSaveTimer.Tick += (_, _) =>
        {
            _numberSaveTimer.Stop();
            CommitCurrentSettings();
        };
        _loadingSettings = false;
    }

    internal void RefreshFromSettings()
    {
        _numberSaveTimer.Stop();
        LoadFromSettings();
        ApplyLanguage();
        UpdateCustomSaverControls();
    }

    private void LoadFromSettings()
    {
        _loadingSettings = true;
        var s = _controller.Settings;

        UseCustomSaverCheckBox.IsChecked = !string.IsNullOrWhiteSpace(s.ScreenSaverPath);
        CustomSaverPathText.Text = s.ScreenSaverPath;

        AutoEnabledCheckBox.IsChecked = s.AutoScreenSaverEnabled;
        SaverMinutesTextBox.Text = s.ScreenSaverIdleMinutes.ToString();
        LockEnabledCheckBox.IsChecked = s.LockEnabled;
        LockMinutesTextBox.Text = s.LockIdleMinutes.ToString();

        MediaGuardCheckBox.IsChecked = s.BlockScreenSaverWhileMediaPlaying;
        FullscreenGuardCheckBox.IsChecked = s.BlockScreenSaverWhileFullscreen;
        LockGuardCheckBox.IsChecked = s.SuspendLockWhileMediaOrPresentation;

        HotCornerEnabledCheckBox.IsChecked = s.HotCornerEnabled;
        HotCornerDelayTextBox.Text = s.HotCornerDelayMs.ToString();
        SelectHotCorner(s.HotCorner);

        StartScreenSaverHotkeyEnabledCheckBox.IsChecked = s.StartScreenSaverHotkeyEnabled;
        StartHotkeyTextBox.Text = s.StartScreenSaverHotkey;
        ToggleScreenSaverHotkeyEnabledCheckBox.IsChecked = s.ToggleScreenSaverHotkeyEnabled;
        ToggleHotkeyTextBox.Text = s.ToggleScreenSaverHotkey;
        SleepHotkeyEnabledCheckBox.IsChecked = s.SleepHotkeyEnabled;
        SleepHotkeyTextBox.Text = s.SleepHotkey;
        HibernateHotkeyEnabledCheckBox.IsChecked = s.HibernateHotkeyEnabled;
        HibernateHotkeyTextBox.Text = s.HibernateHotkey;
        UpdateShortcutControls();

        NotificationsCheckBox.IsChecked = s.ShowStatusNotifications;
        TrayIconCheckBox.IsChecked = s.ShowTrayStatusIcon;
        StartWithWindowsCheckBox.IsChecked = s.StartWithWindows;

        _loadingLanguage = true;
        SelectLanguage(s.Language);
        _loadingLanguage = false;
        _loadingSettings = false;
    }

    private void ApplyLanguage()
    {
        Title = LocalizationService.T("Title");
        TitleText.Text = LocalizationService.T("Title");
        SubtitleText.Text = LocalizationService.T("Subtitle");

        ScreenSaverGroup.Header = LocalizationService.T("Group.ScreenSaver");
        WindowsSelectedSaverLabel.Text = LocalizationService.T("WindowsSelectedSaver");
        WindowsSaverPathText.Text = _controller.GetWindowsScreenSaverPath() ?? LocalizationService.T("NoWindowsSaver");
        UseCustomSaverCheckBox.Content = LocalizationService.T("UseCustomSaver");
        BrowseButton.Content = LocalizationService.T("Browse");
        OpenWindowsSaverSettingsButton.Content = LocalizationService.T("OpenWindowsSaverSettings");
        StartNowButton.Content = LocalizationService.T("StartNowWindows");
        TestCustomSaverButton.Content = LocalizationService.T("TestCustomSaver");

        AutoGroup.Header = LocalizationService.T("Group.Auto");
        AutoEnabledCheckBox.Content = LocalizationService.T("AutoEnabled");
        StartAfterLabel.Text = LocalizationService.T("StartAfter");
        MinutesAfterLabel.Text = LocalizationService.T("MinutesAfter");
        LockEnabledCheckBox.Content = LocalizationService.T("LockEnabled");
        LockComputerLabel.Text = LocalizationService.T("LockComputer");
        MinutesLockLabel.Text = LocalizationService.T("MinutesLock");

        GuardsGroup.Header = LocalizationService.T("Group.Guards");
        MediaGuardCheckBox.Content = LocalizationService.T("MediaGuard");
        FullscreenGuardCheckBox.Content = LocalizationService.T("FullscreenGuard");
        LockGuardCheckBox.Content = LocalizationService.T("LockGuard");

        HotCornerGroup.Header = LocalizationService.T("Group.HotCorner");
        HotCornerEnabledCheckBox.Content = LocalizationService.T("HotCornerEnabled");
        CornerLabel.Text = LocalizationService.T("Corner");
        CornerDelayLabel.Text = LocalizationService.T("CornerDelay");
        ((ComboBoxItem)HotCornerComboBox.Items[0]).Content = LocalizationService.T("TopLeft");
        ((ComboBoxItem)HotCornerComboBox.Items[1]).Content = LocalizationService.T("TopRight");
        ((ComboBoxItem)HotCornerComboBox.Items[2]).Content = LocalizationService.T("BottomLeft");
        ((ComboBoxItem)HotCornerComboBox.Items[3]).Content = LocalizationService.T("BottomRight");

        HotkeysGroup.Header = LocalizationService.T("Group.Hotkeys");
        HotkeyHelpText.Text = LocalizationService.T("HotkeyHelp");
        StartSaverLabel.Text = LocalizationService.T("StartSaver");
        StartScreenSaverHotkeyEnabledCheckBox.Content = LocalizationService.T("Enabled");
        ToggleSaverLabel.Text = LocalizationService.T("ToggleSaver");
        ToggleScreenSaverHotkeyEnabledCheckBox.Content = LocalizationService.T("Enabled");
        SleepStandbyLabel.Text = LocalizationService.T("SleepStandby");
        HibernateLabel.Text = LocalizationService.T("Hibernate");
        RestoreHotkeysButton.Content = LocalizationService.T("RestoreHotkeys");
        SleepHotkeyEnabledCheckBox.Content = LocalizationService.T("Enabled");
        HibernateHotkeyEnabledCheckBox.Content = LocalizationService.T("Enabled");

        BehaviorGroup.Header = LocalizationService.T("Group.Behavior");
        NotificationsCheckBox.Content = LocalizationService.T("Notifications");
        TrayIconCheckBox.Content = LocalizationService.T("TrayIcon");
        StartWithWindowsCheckBox.Content = LocalizationService.T("StartWithWindows");
        LanguageLabel.Text = LocalizationService.T("Language");
        ((ComboBoxItem)LanguageComboBox.Items[0]).Content = LocalizationService.T("LanguageAuto");
        ((ComboBoxItem)LanguageComboBox.Items[1]).Content = LocalizationService.T("LanguageTurkish");
        ((ComboBoxItem)LanguageComboBox.Items[2]).Content = LocalizationService.T("LanguageEnglish");

        RegistryNoteText.Text = LocalizationService.T("RegistryNote");
        RestoreDefaultsButton.Content = LocalizationService.T("RestoreDefaults");
        AboutButton.Content = LocalizationService.T("About");
        HideButton.Content = LocalizationService.T("MinimizeToTray");
    }

    private void SelectLanguage(AppLanguage language)
    {
        foreach (ComboBoxItem item in LanguageComboBox.Items)
        {
            if (string.Equals(item.Tag?.ToString(), language.ToString(), StringComparison.OrdinalIgnoreCase))
            {
                LanguageComboBox.SelectedItem = item;
                return;
            }
        }
        LanguageComboBox.SelectedIndex = 0;
    }

    private AppLanguage GetSelectedLanguage()
    {
        if (LanguageComboBox.SelectedItem is ComboBoxItem item &&
            Enum.TryParse(item.Tag?.ToString(), out AppLanguage language))
            return language;

        return AppLanguage.Auto;
    }

    private void LanguageComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loadingLanguage || LanguageComboBox.SelectedItem is null)
            return;

        if (_loadingSettings)
            return;

        CommitCurrentSettings();
        ApplyLanguage();
    }

    private void SelectHotCorner(HotCorner corner)
    {
        foreach (ComboBoxItem item in HotCornerComboBox.Items)
        {
            if (string.Equals(item.Tag?.ToString(), corner.ToString(), StringComparison.OrdinalIgnoreCase))
            {
                HotCornerComboBox.SelectedItem = item;
                return;
            }
        }
        HotCornerComboBox.SelectedIndex = 1;
    }

    private void WireInstantSaveEvents()
    {
        // The four hotkey switches, custom .scr checkbox and language combo
        // already have XAML handlers; only wire the remaining controls here.
        CheckBox[] switches =
        {
            AutoEnabledCheckBox, LockEnabledCheckBox,
            MediaGuardCheckBox, FullscreenGuardCheckBox, LockGuardCheckBox,
            HotCornerEnabledCheckBox, NotificationsCheckBox,
            TrayIconCheckBox, StartWithWindowsCheckBox
        };

        foreach (CheckBox checkBox in switches)
        {
            checkBox.Checked += SettingChanged;
            checkBox.Unchecked += SettingChanged;
        }

        HotCornerComboBox.SelectionChanged += (_, _) => CommitCurrentSettings();

        TextBox[] numericFields =
        {
            SaverMinutesTextBox, LockMinutesTextBox, HotCornerDelayTextBox
        };

        foreach (TextBox field in numericFields)
        {
            field.TextChanged += NumericTextChanged;
            field.LostFocus += NumericLostFocus;
            field.PreviewKeyDown += NumericPreviewKeyDown;
        }
    }

    private void SettingChanged(object sender, RoutedEventArgs e) =>
        CommitCurrentSettings();

    private void NumericTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_loadingSettings)
            return;

        // A short delay avoids committing the intermediate "1" while
        // someone is entering "10". Valid values apply without Save.
        _numberSaveTimer.Stop();
        _numberSaveTimer.Start();
    }

    private void NumericLostFocus(object sender, RoutedEventArgs e)
    {
        _numberSaveTimer.Stop();
        if (sender is TextBox box)
            ValidateOrRestoreNumber(box);
        CommitCurrentSettings();
    }

    private void NumericPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Return)
            return;

        _numberSaveTimer.Stop();
        if (sender is TextBox box)
            ValidateOrRestoreNumber(box);
        CommitCurrentSettings();
        e.Handled = true;
    }

    private void ValidateOrRestoreNumber(TextBox box)
    {
        (int previous, int min, int max, string label) = box == SaverMinutesTextBox
            ? (_controller.Settings.ScreenSaverIdleMinutes, 1, 1440, LocalizationService.T("StartSaver"))
            : box == LockMinutesTextBox
                ? (_controller.Settings.LockIdleMinutes, 1, 1440, LocalizationService.T("LockEnabled"))
                : (_controller.Settings.HotCornerDelayMs, 0, 5000, LocalizationService.T("CornerDelay"));

        if (IsValidNumber(box.Text, min, max, out _))
            return;

        _loadingSettings = true;
        box.Text = previous.ToString();
        _loadingSettings = false;

        MessageBox.Show(this, LocalizationService.F("InvalidRange", label, min, max),
            LocalizationService.T("SaveFailed"), MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }

    private void CommitCurrentSettings()
    {
        if (_loadingSettings)
            return;

        AppSettings candidate = _controller.Settings.Clone();
        candidate.Language = GetSelectedLanguage();
        candidate.AutoScreenSaverEnabled = AutoEnabledCheckBox.IsChecked == true;
        candidate.LockEnabled = LockEnabledCheckBox.IsChecked == true;
        candidate.BlockScreenSaverWhileMediaPlaying = MediaGuardCheckBox.IsChecked == true;
        candidate.BlockScreenSaverWhileFullscreen = FullscreenGuardCheckBox.IsChecked == true;
        candidate.SuspendLockWhileMediaOrPresentation = LockGuardCheckBox.IsChecked == true;
        candidate.HotCornerEnabled = HotCornerEnabledCheckBox.IsChecked == true;
        candidate.HotCorner = GetSelectedHotCorner();

        // A temporarily incomplete numeric edit does not overwrite the last
        // valid value; it is checked and reverted on Enter / loss of focus.
        if (IsValidNumber(SaverMinutesTextBox.Text, 1, 1440, out int saverMinutes))
            candidate.ScreenSaverIdleMinutes = saverMinutes;
        if (IsValidNumber(LockMinutesTextBox.Text, 1, 1440, out int lockMinutes))
            candidate.LockIdleMinutes = lockMinutes;
        if (IsValidNumber(HotCornerDelayTextBox.Text, 0, 5000, out int cornerDelay))
            candidate.HotCornerDelayMs = cornerDelay;

        candidate.ScreenSaverPath = UseCustomSaverCheckBox.IsChecked == true
            ? CustomSaverPathText.Text.Trim() : string.Empty;
        candidate.StartScreenSaverHotkeyEnabled = StartScreenSaverHotkeyEnabledCheckBox.IsChecked == true;
        candidate.StartScreenSaverHotkey = StartHotkeyTextBox.Text.Trim();
        candidate.ToggleScreenSaverHotkeyEnabled = ToggleScreenSaverHotkeyEnabledCheckBox.IsChecked == true;
        candidate.ToggleScreenSaverHotkey = ToggleHotkeyTextBox.Text.Trim();
        candidate.SleepHotkeyEnabled = SleepHotkeyEnabledCheckBox.IsChecked == true;
        candidate.SleepHotkey = SleepHotkeyTextBox.Text.Trim();
        candidate.HibernateHotkeyEnabled = HibernateHotkeyEnabledCheckBox.IsChecked == true;
        candidate.HibernateHotkey = HibernateHotkeyTextBox.Text.Trim();
        candidate.ShowStatusNotifications = NotificationsCheckBox.IsChecked == true;
        candidate.ShowTrayStatusIcon = TrayIconCheckBox.IsChecked == true;
        candidate.StartWithWindows = StartWithWindowsCheckBox.IsChecked == true;

        try
        {
            _controller.UpdateSettings(candidate);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, LocalizationService.T("SaveFailed"),
                MessageBoxButton.OK, MessageBoxImage.Warning);
            _numberSaveTimer.Stop();
            LoadFromSettings();
            ApplyLanguage();
            UpdateCustomSaverControls();
        }
    }

    private static bool IsValidNumber(string raw, int min, int max, out int value) =>
        int.TryParse(raw, out value) && value >= min && value <= max;

    private void RestoreDefaults_Click(object sender, RoutedEventArgs e)
    {
        var answer = MessageBox.Show(this,
            LocalizationService.T("RestoreDefaultsConfirm"),
            LocalizationService.T("RestoreDefaultsTitle"),
            MessageBoxButton.YesNo, MessageBoxImage.Question,
            MessageBoxResult.No);

        if (answer != MessageBoxResult.Yes)
            return;

        _numberSaveTimer.Stop();
        try
        {
            _controller.RestoreAllDefaults();
            LoadFromSettings();
            ApplyLanguage();
            UpdateCustomSaverControls();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, LocalizationService.T("SaveFailed"),
                MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void BrowseScreenSaver_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = LocalizationService.T("SelectSaver"),
            Filter = $"Screen Saver (*.scr)|*.scr|{LocalizationService.T("AllFiles")} (*.*)|*.*",
            CheckFileExists = true
        };

        if (dialog.ShowDialog(this) == true)
        {
            // Set the selected path before enabling custom mode so the
            // checkbox change immediately saves a complete configuration.
            CustomSaverPathText.Text = dialog.FileName;
            UseCustomSaverCheckBox.IsChecked = true;
            UpdateCustomSaverControls();
            CommitCurrentSettings();
        }
    }

    private void OpenWindowsSaverSettings_Click(object sender, RoutedEventArgs e) =>
        _controller.OpenWindowsScreenSaverSettings();

    private void StartNow_Click(object sender, RoutedEventArgs e) =>
        _controller.StartWindowsScreenSaverNow();

    private void TestCustomSaver_Click(object sender, RoutedEventArgs e)
    {
        if (UseCustomSaverCheckBox.IsChecked == true)
            _controller.TestCustomScreenSaver(CustomSaverPathText.Text.Trim());
    }

    private void CustomSaverSelection_Changed(object sender, RoutedEventArgs e)
    {
        UpdateCustomSaverControls();
        CommitCurrentSettings();
    }

    private void UpdateCustomSaverControls()
    {
        // Checked/Unchecked may fire while InitializeComponent is in progress.
        if (TestCustomSaverButton is null || CustomSaverPathText is null ||
            UseCustomSaverCheckBox is null)
            return;

        bool custom = UseCustomSaverCheckBox.IsChecked == true;
        TestCustomSaverButton.Visibility = custom ? Visibility.Visible : Visibility.Collapsed;
        TestCustomSaverButton.IsEnabled = custom &&
            !string.IsNullOrWhiteSpace(CustomSaverPathText.Text);
    }

    private void RestoreHotkeys_Click(object sender, RoutedEventArgs e)
    {
        // Only reset shortcut combinations, not the four enabled flags.
        var defaults = new AppSettings();
        StartHotkeyTextBox.Text = defaults.StartScreenSaverHotkey;
        ToggleHotkeyTextBox.Text = defaults.ToggleScreenSaverHotkey;
        SleepHotkeyTextBox.Text = defaults.SleepHotkey;
        HibernateHotkeyTextBox.Text = defaults.HibernateHotkey;
        CommitCurrentSettings();
    }

    private void HotkeyTextBox_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (sender is not TextBox box)
            return;

        Key key = e.Key == Key.System ? e.SystemKey : e.Key;

        if (HotkeyGesture.IsModifierKey(key))
        {
            e.Handled = true;
            return;
        }

        box.Text = new HotkeyGesture(Keyboard.Modifiers, key).ToString();
        CommitCurrentSettings();
        e.Handled = true;
    }

    private HotCorner GetSelectedHotCorner()
    {
        if (HotCornerComboBox.SelectedItem is ComboBoxItem item &&
            Enum.TryParse(item.Tag?.ToString(), out HotCorner corner))
            return corner;

        return HotCorner.TopRight;
    }

    private void ShortcutCheckBox_Changed(object sender, RoutedEventArgs e)
    {
        if (StartHotkeyTextBox is null || HibernateHotkeyTextBox is null)
            return;
        UpdateShortcutControls();
        CommitCurrentSettings();
    }

    private void UpdateShortcutControls()
    {
        StartHotkeyTextBox.IsEnabled =
            StartScreenSaverHotkeyEnabledCheckBox.IsChecked == true;

        ToggleHotkeyTextBox.IsEnabled =
            ToggleScreenSaverHotkeyEnabledCheckBox.IsChecked == true;

        SleepHotkeyTextBox.IsEnabled =
            SleepHotkeyEnabledCheckBox.IsChecked == true;

        HibernateHotkeyTextBox.IsEnabled =
            HibernateHotkeyEnabledCheckBox.IsChecked == true;
    }

    private void About_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new UI.AboutWindow { Owner = this };
        dialog.ShowDialog();
    }

    private void Hide_Click(object sender, RoutedEventArgs e)
    {
        _numberSaveTimer.Stop();
        CommitCurrentSettings();
        _controller.EnsureTrayVisible();
        Hide();
    }


    // A user closing the Settings window should only hide it in the tray.
    // Explicit Exit from the tray must instead let WPF close the window.
    internal void PrepareForExit() => _allowClose = true;

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_allowClose && !Application.Current.Dispatcher.HasShutdownStarted)
        {
            _numberSaveTimer.Stop();
            CommitCurrentSettings();
            _controller.EnsureTrayVisible();
            e.Cancel = true;
            Hide();
            return;
        }

        base.OnClosing(e);
    }
}
