using System.Globalization;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public static class LocalizationService
{
    private static string _languageCode = "en";

    public static void Apply(AppLanguage language)
    {
        _languageCode = language switch
        {
            AppLanguage.Turkish => "tr",
            AppLanguage.English => "en",
            _ => IsWindowsTurkish() ? "tr" : "en"
        };
    }

    public static void ApplyWindowsDefault()
    {
        _languageCode = IsWindowsTurkish() ? "tr" : "en";
    }

    private static bool IsWindowsTurkish()
    {
        try
        {
            return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName
                .Equals("tr", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    public static string T(string key)
    {
        var map = _languageCode == "tr" ? Turkish : English;
        if (map.TryGetValue(key, out string? value))
            return value;

        return English.TryGetValue(key, out string? fallback) ? fallback : key;
    }

    public static string F(string key, params object[] args) =>
        string.Format(CultureInfo.CurrentCulture, T(key), args);

    private static readonly Dictionary<string, string> Turkish = new()
    {
        ["Title"] = "Classic Desktop Screensaver",
        ["Subtitle"] = "Klasik .scr ekran koruyucularını Windows'u kilitlemeden masaüstünde çalıştırır.",
        ["Group.ScreenSaver"] = "Ekran koruyucu",
        ["WindowsSelectedSaver"] = "Windows'ta seçili ekran koruyucu:",
        ["NoWindowsSaver"] = "(Windows'ta seçili ekran koruyucu yok)",
        ["UseCustomSaver"] = "Özel bir .scr dosyası kullan",
        ["Browse"] = "Gözat...",
        ["OpenWindowsSaverSettings"] = "Windows ekran koruyucu ayarlarını aç",
        ["StartNow"] = "Şimdi Başlat",
        ["StartNowWindows"] = "Windows ekran koruyucusunu şimdi başlat",
        ["TestCustomSaver"] = "Test Et",
        ["Group.Auto"] = "Otomatik başlatma ve kilitleme",
        ["AutoEnabled"] = "Otomatik ekran koruyucu aktif",
        ["StartAfter"] = "Ekran koruyucuyu",
        ["MinutesAfter"] = "dakika boşta kalınca başlat",
        ["LockEnabled"] = "Belirli bir süre sonra Windows'u kilitle",
        ["LockComputer"] = "Bilgisayarı",
        ["MinutesLock"] = "dakika boşta kalınca kilitle",
        ["Group.Guards"] = "Video / sunum koruması",
        ["MediaGuard"] = "Video oynatılırken otomatik ekran koruyucuyu başlatma",
        ["FullscreenGuard"] = "Tam ekran uygulama veya sunum açıkken otomatik ekran koruyucuyu başlatma",
        ["LockGuard"] = "Video, tam ekran uygulama veya sunum sırasında otomatik kilitlemeyi ertele",
        ["Group.HotCorner"] = "Hot Corner",
        ["HotCornerEnabled"] = "Fare ana ekranın seçilen köşesinde bekleyince ekran koruyucuyu başlat",
        ["Corner"] = "Köşe",
        ["TopLeft"] = "Sol üst",
        ["TopRight"] = "Sağ üst",
        ["BottomLeft"] = "Sol alt",
        ["BottomRight"] = "Sağ alt",
        ["CornerDelay"] = "Köşede bekleme",
        ["Group.Hotkeys"] = "Klavye kısayolları",
        ["HotkeyHelp"] = "Alanı seçip istediğin tuş kombinasyonuna bas. Program ayar penceresi açıkken global kısayollar geçici olarak durdurulur.",
        ["StartSaver"] = "Ekran koruyucuyu başlat",
        ["ToggleSaver"] = "Otomatik ekran koruyucu Aç/Kapat",
        ["SleepStandby"] = "Uyku / Bekleme",
        ["Hibernate"] = "Hazırda Beklet",
        ["RestoreHotkeys"] = "Varsayılan kısayolları geri getir",
        ["Enabled"] = "Açık",
        ["Group.Behavior"] = "Program davranışı",
        ["Notifications"] = "Scroll Lock aç/kapat bildirimlerini Windows Bildirim Merkezi'nde göster",
        ["TrayIcon"] = "Sistem tepsisinde ekran koruyucu durum simgesini göster",
        ["StartWithWindows"] = "Windows başladığında başlat (Startup klasörüne kısayol oluşturur)",
        ["Language"] = "Dil",
        ["LanguageAuto"] = "Otomatik (Windows dili)",
        ["LanguageTurkish"] = "Türkçe",
        ["LanguageEnglish"] = "English",
        ["About"] = "Hakkında / Info",
        ["AboutTitle"] = "Classic Desktop Screensaver Hakkında",
        ["AboutVersion"] = "Sürüm: 1.0.1",
        ["AboutMinimumWindows"] = "Minimum Windows: Windows 10 version 1809 (build 17763) veya üzeri / Windows 11",
        ["AboutAI"] = "Bu yazılımın kaynak kodu, proje gereksinimleri doğrultusunda yapay zekâ (OpenAI ChatGPT) tarafından hazırlanmıştır.",
        ["AboutLicense"] = "Lisans: MIT",
        ["AboutPrivacy"] = "Yönetici yetkisi, servis veya sürücü gerektirmez. Uygulama ayarlarını Registry’ye yazmaz.",
        ["Close"] = "Kapat",
        ["RegistryNote"] = "Not: Scroll Lock bazı uygulamalarda (ör. Excel) gerçek bir işleve sahiptir. Varsayılan Scroll Lock kısayolunu burada değiştirebilirsin. Program Registry'ye ayar yazmaz; yalnız Windows'ta seçili .scr yolunu öğrenmek için mevcut ekran koruyucu Registry bilgisini okuyabilir.",
        ["RestoreDefaults"] = "Varsayılan Ayarlara Geri Dön",
        ["RestoreDefaultsConfirm"] = "Bütün program ayarları varsayılan değerlerine dönecek. Devam etmek istiyor musun?",
        ["RestoreDefaultsTitle"] = "Varsayılan ayarlara dön",
        ["Hide"] = "Gizle",
        ["MinimizeToTray"] = "Sistem Tepsisine Küçült",
        ["Exit"] = "Programdan Çık",
        ["Saved"] = "Ayarlar kaydedildi.",
        ["SaveFailed"] = "Ayarlar kaydedilemedi",
        ["InvalidRange"] = "{0} {1} ile {2} arasında olmalıdır.",
        ["SelectSaver"] = "Ekran koruyucu seç",
        ["AllFiles"] = "Tüm dosyalar",
        ["Active"] = "Ekran koruyucu açıldı.",
        ["Inactive"] = "Ekran koruyucu kapatıldı.",
        ["TraySettings"] = "Ayarlar",
        ["TrayStart"] = "Ekran koruyucuyu şimdi başlat",
        ["TrayStatusActive"] = "Otomatik ekran koruyucu: Açık",
        ["TrayStatusInactive"] = "Otomatik ekran koruyucu: Kapalı",
        ["TrayActive"] = "Classic Desktop Screensaver - Otomatik: Açık",
        ["TrayInactive"] = "Classic Desktop Screensaver - Otomatik: Kapalı",
        ["TrayExit"] = "Çıkış",
        ["NoSaverSelected"] = "Windows'ta seçili bir ekran koruyucu bulunamadı.",
        ["SaverFileNotFound"] = "Ekran koruyucu dosyası bulunamadı:\n{0}",
        ["SaverStartFailed"] = "Ekran koruyucu başlatılamadı: {0}",
        ["HibernateUnavailable"] = "Hazırda Beklet bu bilgisayarda etkin değil veya desteklenmiyor.",
        ["SleepCapabilitiesUnavailable"] = "Windows'un desteklediği uyku durumları belirlenemedi; güvenlik için işlem yapılmadı.",
        ["MonitorPowerRequestFailed"] = "Modern Standby için ekran kapatma isteği gönderilemedi. Windows hata kodu: {0}",
        ["SleepUnavailable"] = "Bu bilgisayarda kullanılabilir bir uyku modu bulunamadı. Hazırda Beklet otomatik olarak başlatılmadı.",
        ["PowerChangeFailed"] = "Güç durumu değiştirilemedi. Windows hata kodu: {0}",
        ["PowerPrivilegeFailed"] = "Güç yönetimi yetkisi alınamadı.",
        ["PowerPrivilegeMissing"] = "Windows güç yönetimi ayrıcalığı bulunamadı.",
        ["PowerPrivilegeEnableFailed"] = "Windows güç yönetimi ayrıcalığı etkinleştirilemedi.",
        ["PowerPrivilegeDenied"] = "Windows bu kullanıcıya güç yönetimi ayrıcalığı vermedi.",
        ["InvalidShortcut"] = "Geçersiz kısayol: {0}",
        ["DuplicateShortcut"] = "Aynı kısayol iki farklı göreve atanamaz.",
        ["UnsupportedWindowsTitle"] = "Desteklenmeyen Windows sürümü",
        ["UnsupportedWindows"] = "Classic Desktop Screensaver, Windows 10 version 1809 (build 17763) veya daha yeni bir Windows sürümü gerektirir."
    };

    private static readonly Dictionary<string, string> English = new()
    {
        ["Title"] = "Classic Desktop Screensaver",
        ["Subtitle"] = "Runs classic .scr screen savers on the desktop without requiring Windows to lock.",
        ["Group.ScreenSaver"] = "Screen saver",
        ["WindowsSelectedSaver"] = "Screen saver selected in Windows:",
        ["NoWindowsSaver"] = "(No screen saver is selected in Windows)",
        ["UseCustomSaver"] = "Use a custom .scr file",
        ["Browse"] = "Browse...",
        ["OpenWindowsSaverSettings"] = "Open Windows screen saver settings",
        ["StartNow"] = "Start Now",
        ["StartNowWindows"] = "Start Windows screen saver now",
        ["TestCustomSaver"] = "Test",
        ["Group.Auto"] = "Automatic start and locking",
        ["AutoEnabled"] = "Automatic screen saver enabled",
        ["StartAfter"] = "Start screen saver after",
        ["MinutesAfter"] = "minutes of inactivity",
        ["LockEnabled"] = "Lock Windows after a specified idle time",
        ["LockComputer"] = "Lock computer after",
        ["MinutesLock"] = "minutes of inactivity",
        ["Group.Guards"] = "Video / presentation protection",
        ["MediaGuard"] = "Do not start the automatic screen saver while video is playing",
        ["FullscreenGuard"] = "Do not start the automatic screen saver while a full-screen app or presentation is active",
        ["LockGuard"] = "Delay automatic locking during video, full-screen apps or presentations",
        ["Group.HotCorner"] = "Hot Corner",
        ["HotCornerEnabled"] = "Start the screen saver when the pointer remains in the selected corner of the primary display",
        ["Corner"] = "Corner",
        ["TopLeft"] = "Top left",
        ["TopRight"] = "Top right",
        ["BottomLeft"] = "Bottom left",
        ["BottomRight"] = "Bottom right",
        ["CornerDelay"] = "Corner delay",
        ["Group.Hotkeys"] = "Keyboard shortcuts",
        ["HotkeyHelp"] = "Select a field and press the shortcut you want. Global shortcuts are temporarily suspended while the Settings window is active.",
        ["StartSaver"] = "Start screen saver",
        ["ToggleSaver"] = "Toggle automatic screen saver",
        ["SleepStandby"] = "Sleep / Standby",
        ["Hibernate"] = "Hibernate",
        ["RestoreHotkeys"] = "Restore default shortcuts",
        ["Enabled"] = "On",
        ["Group.Behavior"] = "Program behavior",
        ["Notifications"] = "Show Scroll Lock on/off messages in Windows Notification Center",
        ["TrayIcon"] = "Show screen saver status icon in the system tray",
        ["StartWithWindows"] = "Start with Windows (creates a shortcut in the Startup folder)",
        ["Language"] = "Language",
        ["LanguageAuto"] = "Automatic (Windows language)",
        ["LanguageTurkish"] = "Türkçe",
        ["LanguageEnglish"] = "English",
        ["About"] = "About / Info",
        ["AboutTitle"] = "About Classic Desktop Screensaver",
        ["AboutVersion"] = "Version: 1.0.1",
        ["AboutMinimumWindows"] = "Minimum Windows: Windows 10 version 1809 (build 17763) or later / Windows 11",
        ["AboutAI"] = "The source code was prepared by AI (OpenAI ChatGPT) based on the project requirements.",
        ["AboutLicense"] = "License: MIT",
        ["AboutPrivacy"] = "No administrator rights, service or driver are required. Application settings are not written to the Registry.",
        ["Close"] = "Close",
        ["RegistryNote"] = "Note: Scroll Lock has a real function in some applications (for example, Excel). You can change the default Scroll Lock shortcut here. The program does not write settings to the Registry; it may only read the existing Windows screen saver Registry value to determine the selected .scr file.",
        ["RestoreDefaults"] = "Restore Default Settings",
        ["RestoreDefaultsConfirm"] = "All application settings will be restored to their defaults. Do you want to continue?",
        ["RestoreDefaultsTitle"] = "Restore defaults",
        ["Hide"] = "Hide",
        ["MinimizeToTray"] = "Minimize to System Tray",
        ["Exit"] = "Exit Program",
        ["Saved"] = "Settings saved.",
        ["SaveFailed"] = "Settings could not be saved",
        ["InvalidRange"] = "{0} must be between {1} and {2}.",
        ["SelectSaver"] = "Select screen saver",
        ["AllFiles"] = "All files",
        ["Active"] = "Screen saver turned on.",
        ["Inactive"] = "Screen saver turned off.",
        ["TraySettings"] = "Settings",
        ["TrayStart"] = "Start screen saver now",
        ["TrayStatusActive"] = "Automatic screen saver: On",
        ["TrayStatusInactive"] = "Automatic screen saver: Off",
        ["TrayActive"] = "Classic Desktop Screensaver - Automatic: On",
        ["TrayInactive"] = "Classic Desktop Screensaver - Automatic: Off",
        ["TrayExit"] = "Exit",
        ["NoSaverSelected"] = "No screen saver is selected in Windows.",
        ["SaverFileNotFound"] = "Screen saver file was not found:\n{0}",
        ["SaverStartFailed"] = "The screen saver could not be started: {0}",
        ["HibernateUnavailable"] = "Hibernate is not enabled or supported on this computer.",
        ["SleepCapabilitiesUnavailable"] = "Windows sleep capabilities could not be determined; no power action was taken.",
        ["MonitorPowerRequestFailed"] = "The Modern Standby display-off request could not be sent. Windows error code: {0}",
        ["SleepUnavailable"] = "No supported sleep state is available. Hibernate was not started automatically.",
        ["PowerChangeFailed"] = "The power state could not be changed. Windows error code: {0}",
        ["PowerPrivilegeFailed"] = "Power-management privilege could not be obtained.",
        ["PowerPrivilegeMissing"] = "The Windows power-management privilege was not found.",
        ["PowerPrivilegeEnableFailed"] = "The Windows power-management privilege could not be enabled.",
        ["PowerPrivilegeDenied"] = "Windows did not grant this user the required power-management privilege.",
        ["InvalidShortcut"] = "Invalid shortcut: {0}",
        ["DuplicateShortcut"] = "The same shortcut cannot be assigned to two different actions.",
        ["UnsupportedWindowsTitle"] = "Unsupported Windows version",
        ["UnsupportedWindows"] = "Classic Desktop Screensaver requires Windows 10 version 1809 (build 17763) or a newer version of Windows."
    };
}
