using System.Windows;
using ClassicDesktopScreensaver.Services;

namespace ClassicDesktopScreensaver.UI;

public partial class AboutWindow : Window
{
    public AboutWindow()
    {
        InitializeComponent();
        Title = LocalizationService.T("AboutTitle");
        TitleText.Text = "Classic Desktop Screensaver";
        VersionText.Text = LocalizationService.T("AboutVersion");
        MinimumWindowsText.Text = LocalizationService.T("AboutMinimumWindows");
        AIText.Text = LocalizationService.T("AboutAI");
        LicenseText.Text = LocalizationService.T("AboutLicense");
        PrivacyText.Text = LocalizationService.T("AboutPrivacy");
        CloseButton.Content = LocalizationService.T("Close");
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
