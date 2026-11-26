#include "Localization.h"

#include <Windows.h>

namespace classic_desktop_screensaver
{
    AppLanguage ResolveLanguage(AppLanguage requested) noexcept
    {
        if (requested != AppLanguage::Auto)
        {
            return requested;
        }

        const LANGID language = GetUserDefaultUILanguage();
        return PRIMARYLANGID(language) == LANG_TURKISH
            ? AppLanguage::Turkish
            : AppLanguage::English;
    }

    std::wstring_view Text(AppLanguage requested, std::wstring_view key) noexcept
    {
        const bool turkish = ResolveLanguage(requested) == AppLanguage::Turkish;
#define CDS_TEXT(name, tr, en) if (key == name) return turkish ? tr : en
        CDS_TEXT(L"Title", L"Classic Desktop Screensaver", L"Classic Desktop Screensaver");
        CDS_TEXT(L"UiTestWindowTitle", L"Classic Desktop Screensaver - Güvenli UI Testi (Salt Okunur)", L"Classic Desktop Screensaver - Safe UI Test (Read-only)");
        CDS_TEXT(L"Subtitle", L"Klasik .scr ekran koruyucularını Windows'u kilitlemeden masaüstünde çalıştırır.", L"Runs classic .scr screen savers on the desktop without locking Windows.");
        CDS_TEXT(L"ScreenSaver", L"Ekran koruyucu", L"Screen saver");
        CDS_TEXT(L"WindowsSelectedSaver", L"Windows'ta seçili ekran koruyucu:", L"Screen saver selected in Windows:");
        CDS_TEXT(L"NoWindowsSaver", L"(Windows'ta seçili ekran koruyucu yok)", L"(No screen saver is selected in Windows)");
        CDS_TEXT(L"UseCustomSaver", L"Özel bir .scr dosyası kullan", L"Use a custom .scr file");
        CDS_TEXT(L"Browse", L"Gözat...", L"Browse...");
        CDS_TEXT(L"OpenWindowsSaverSettings", L"Windows ekran koruyucu ayarlarını aç", L"Open Windows screen saver settings");
        CDS_TEXT(L"StartNowWindows", L"Windows ekran koruyucusunu şimdi başlat", L"Start Windows screen saver now");
        CDS_TEXT(L"TestCustomSaver", L"Test Et", L"Test");
        CDS_TEXT(L"AutoGroup", L"Otomatik başlatma ve kilitleme", L"Automatic start and locking");
        CDS_TEXT(L"AutoEnabled", L"Otomatik ekran koruyucu aktif", L"Automatic screen saver enabled");
        CDS_TEXT(L"StartAfter", L"Ekran koruyucuyu", L"Start the screen saver after");
        CDS_TEXT(L"MinutesIdle", L"dakika boşta kalınca başlat", L"minutes of inactivity");
        CDS_TEXT(L"LockEnabled", L"Belirli bir süre sonra Windows'u kilitle", L"Lock Windows after a specified idle time");
        CDS_TEXT(L"LockAfter", L"Bilgisayarı", L"Lock the computer after");
        CDS_TEXT(L"MinutesLock", L"dakika boşta kalınca kilitle", L"minutes of inactivity");
        CDS_TEXT(L"Guards", L"Medya ve tam ekran koruması", L"Media and full-screen protection");
        CDS_TEXT(L"FullscreenGuard", L"Tam ekran uygulama veya sunum açıkken ekran koruyucuyu başlatma", L"Do not start the screen saver while a full-screen app or presentation is active");
        CDS_TEXT(L"LockGuard", L"Desteklenen medya, tam ekran uygulama veya sunum sırasında kilitlemeyi ertele", L"Delay locking during supported media sessions, full-screen apps or presentations");
        CDS_TEXT(L"HotCorner", L"Etkin Köşeler", L"Hot Corners");
        CDS_TEXT(L"HotCornerEnabled", L"Fare seçilen ana ekran köşesinde bekleyince ekran koruyucuyu başlat", L"Start the screen saver when the pointer remains in a primary display corner");
        CDS_TEXT(L"Corner", L"Köşe", L"Corner");
        CDS_TEXT(L"TopLeft", L"Sol üst", L"Top left");
        CDS_TEXT(L"TopRight", L"Sağ üst", L"Top right");
        CDS_TEXT(L"BottomLeft", L"Sol alt", L"Bottom left");
        CDS_TEXT(L"BottomRight", L"Sağ alt", L"Bottom right");
        CDS_TEXT(L"CornerDelay", L"Köşede bekleme", L"Corner delay");
        CDS_TEXT(L"Hotkeys", L"Klavye kısayolları", L"Keyboard shortcuts");
        CDS_TEXT(L"HotkeyHelp", L"Alanı seçip tuş kombinasyonuna bas. Ayarlar açıkken global kısayollar geçici olarak durdurulur.", L"Select a field and press a key combination. Global shortcuts are suspended while Settings is active.");
        CDS_TEXT(L"StartSaver", L"Ekran koruyucuyu başlat", L"Start screen saver");
        CDS_TEXT(L"ToggleSaver", L"Otomatik ekran koruyucu Aç/Kapat", L"Toggle automatic screen saver");
        CDS_TEXT(L"Sleep", L"Uyku / Bekleme", L"Sleep / Standby");
        CDS_TEXT(L"Hibernate", L"Hazırda Beklet", L"Hibernate");
        CDS_TEXT(L"RestoreHotkeys", L"Varsayılan kısayolları geri getir", L"Restore default shortcuts");
        CDS_TEXT(L"Enabled", L"Açık", L"On");
        CDS_TEXT(L"Behavior", L"Program davranışı", L"Application behavior");
        CDS_TEXT(L"Notifications", L"Scroll Lock bildirimlerini Windows Bildirim Merkezi'nde göster", L"Show Scroll Lock messages in Windows Notification Center");
        CDS_TEXT(L"TrayIcon", L"Sistem tepsisinde durum simgesini göster", L"Show the status icon in the system tray");
        CDS_TEXT(L"StartWithWindows", L"Windows başladığında başlat (Startup kısayolu)", L"Start with Windows (Startup shortcut)");
        CDS_TEXT(L"Language", L"Dil", L"Language");
        CDS_TEXT(L"LanguageAuto", L"Otomatik (Windows dili)", L"Automatic (Windows language)");
        CDS_TEXT(L"LanguageTurkish", L"Türkçe", L"Türkçe");
        CDS_TEXT(L"LanguageEnglish", L"English", L"English");
        CDS_TEXT(L"RegistryNote", L"Uygulama ayarları settings.ini dosyasına yazılır. Windows kayıt defteri yalnız seçili ekran koruyucu yolunu okumak için kullanılır.", L"Application settings are stored in settings.ini. The Registry is read only to find the selected Windows screen saver.");
        CDS_TEXT(L"RestoreDefaults", L"Varsayılan ayarlara dön", L"Restore defaults");
        CDS_TEXT(L"RestoreDefaultsConfirm", L"Bütün program ayarları varsayılan değerlerine dönecek. Devam edilsin mi?", L"All application settings will return to defaults. Continue?");
        CDS_TEXT(L"About", L"Hakkında / Info", L"About / Info");
        CDS_TEXT(L"Hide", L"Sistem tepsisine küçült", L"Minimize to system tray");
        CDS_TEXT(L"TraySettings", L"Ayarlar", L"Settings");
        CDS_TEXT(L"TrayStart", L"Ekran koruyucuyu şimdi başlat", L"Start screen saver now");
        CDS_TEXT(L"TrayStatusActive", L"Otomatik ekran koruyucu: Açık", L"Automatic screen saver: On");
        CDS_TEXT(L"TrayStatusInactive", L"Otomatik ekran koruyucu: Kapalı", L"Automatic screen saver: Off");
        CDS_TEXT(L"TrayActive", L"Classic Desktop Screensaver - Otomatik: Açık", L"Classic Desktop Screensaver - Automatic: On");
        CDS_TEXT(L"TrayInactive", L"Classic Desktop Screensaver - Otomatik: Kapalı", L"Classic Desktop Screensaver - Automatic: Off");
        CDS_TEXT(L"Exit", L"Çıkış", L"Exit");
        CDS_TEXT(L"NoSaverSelected", L"Windows'ta seçili bir ekran koruyucu bulunamadı.", L"No Windows screen saver is selected.");
        CDS_TEXT(L"HibernateUnavailable", L"Hazırda Bekletme bu bilgisayarda kullanılamıyor. Windows güç ayarlarını ve sistemin S4 desteğini kontrol edin.", L"Hibernate is unavailable on this computer. Check Windows power settings and system S4 support.");
        CDS_TEXT(L"CapabilitiesUnavailable", L"Windows güç yetenekleri belirlenemedi; Hibernate güvenlik için devre dışı.", L"Windows power capabilities could not be queried; Hibernate is disabled for safety.");
        CDS_TEXT(L"LockRequestFailed", L"Windows oturumu kilitleme isteğini kabul etmedi. Bu boşta kalma süresince tekrar denenmeyecek.", L"Windows did not accept the workstation lock request. It will not be retried during this idle period.");
        CDS_TEXT(L"SleepCapabilitiesUnavailable", L"Windows'un desteklediği uyku durumu belirlenemedi; güvenlik için işlem yapılmadı.", L"Windows sleep capabilities could not be queried; no power action was attempted.");
        CDS_TEXT(L"InvalidShortcut", L"Geçersiz veya yinelenen klavye kısayolu.", L"Invalid or duplicate keyboard shortcut.");
        CDS_TEXT(L"InvalidHotCorner", L"Etkin Köşeler için geçerli bir köşe seçin.", L"Select a valid hot corner position.");
        CDS_TEXT(L"PowerFailed", L"Güç isteği Windows tarafından kabul edilmedi. Başka bir güç işlemi denenmedi.", L"Windows did not accept the power request. No alternate power action was attempted.");
        CDS_TEXT(L"SaveFailed", L"Ayarlar kaydedilemedi.", L"Settings could not be saved.");
        CDS_TEXT(L"AboutText", L"Classic Desktop Screensaver\nSürüm: 2.0.0\nTest edilen Windows: Windows 10 22H2 ve Windows 11 26H2\nÖnceki Windows sürümleri test edilmedi.\nMIT lisansı\nYönetici yetkisi, servis veya sürücü gerekmez. Uygulama ayarları settings.ini dosyasında saklanır.", L"Classic Desktop Screensaver\nVersion: 2.0.0\nTested on Windows 10 22H2 and Windows 11 26H2\nEarlier Windows versions have not been tested.\nMIT License\nNo administrator rights, service or driver are required. Application settings are stored in settings.ini.");
        CDS_TEXT(L"SaverFileNotFound", L"Ekran koruyucu dosyası bulunamadı:", L"Screen saver file was not found:");
        CDS_TEXT(L"SaverStartFailed", L"Ekran koruyucu başlatılamadı", L"The screen saver could not be started");
        CDS_TEXT(L"StatusOn", L"Ekran koruyucu açıldı.", L"Screen saver enabled.");
        CDS_TEXT(L"StatusOff", L"Ekran koruyucu kapatıldı.", L"Screen saver disabled.");
#undef CDS_TEXT
        return key;
    }
}
