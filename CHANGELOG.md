# Changelog

## 2.0.0 - 2026-11-26 (Native C++20 / Win32 x64)

### Added
- Published the native x64 implementation as the primary application. It uses the existing editable `settings.ini` format, needs no .NET Runtime, and installs per user without a service or driver.
- Setup now offers **Install**, **Install and Run**, **Uninstall**, and **Exit**. Install preserves existing settings; Install and Run opens Settings; uninstall requests a normal app exit instead of force-terminating it.
- Normal direct launch opens Settings, while the current-user Startup shortcut continues to launch with `--background` into the system tray.
- Tested the native application on Windows 10 22H2 and Windows 11 26H2, including S0-capable and S3-capable hardware. Earlier Windows versions remain untested.

### Fixed / carried forward
- Fixed (native port): Resolved an issue where keyboard shortcuts sometimes required two key presses after resuming from Hibernate (S4). This correction was already recorded for 1.0.1 and is carried into the native build.
- Native taşıma: Hazırda Bekletmeden (S4) dönüş sonrasında klavye kısayollarının çalışması için bazen iki kez tuşa basılması gereken sorun giderildi. Bu düzeltme 1.0.1'de zaten kaydedilmişti; native sürüme taşındı.
- Preserved the terminal exception for a physical, unmodified Pause key in recognized CMD/PowerShell Console Host and Windows Terminal windows. Other configured shortcuts retain their normal behavior.
- Carried forward native UI, tray recovery, scrolling/DPI/display-change handling, Hot Corner, settings compatibility, and power-state separation fixes verified in the native test history.

### Known limitations
- Windowed video playback is not reliably detectable across browsers and media players. The app does not claim windowed-video protection; full-screen and presentation guards remain.
- S0/S3/S4 availability and actual transitions depend on the device firmware, ACPI implementation, and Windows configuration. The app does not fall back between Sleep, Hibernate, or Shutdown states.

## Archived testing candidate — 2026-11-19 (native C++ preview)

- Added a separate native C++20/Win32 x64 implementation while leaving the .NET 10/WPF source and existing user settings intact. The Release package has no .NET Runtime dependency and keeps editable `settings.ini` beside the EXE.
- Ported settings compatibility, global shortcuts and the terminal bare-Pause exception, S0/classic sleep request selection, gated S4 Hibernate, tray/settings UI, screen saver, idle/lock and media/full-screen guards, notifications, Startup shortcut and resume-state cleanup.
- Added `GetPwrCapabilities` Hibernate gating that requires both S4 support and a present hibernation file; unavailable Hibernate does not issue a power call.
- Added fake-backed power tests, settings/shortcut/runtime-policy tests, hidden Settings-window checks and a native Release package script. No real power transition was triggered by these tests.
- Native Sleep/Hibernate behavior remains unverified on Windows 10 and Windows 11. The reported Windows 11 shutdown-looking behavior is still unclassified; a native Windows 11 manual test remains pending.

## Archived testing candidate — 2026-11-10

- Compared the local v1.0.0 source with current power, keyboard and resume paths. Both sleep paths use `SetSuspendState(false, false, false)`; the current S0 guard prevents the call. Historical installed-app Sleep requests were followed by S4 in Kernel-Power records, while the installed EXE version and the reported earlier S0 success could not be confirmed. Kept the S0 guard and the resume keyboard fix; no unverified power API was added.
- Yerel v1.0.0 kaynağı ile güncel güç, klavye ve uyanma yolları karşılaştırıldı. İki uyku yolu da `SetSuspendState(false, false, false)` kullanıyor; güncel S0 engeli çağrıyı durduruyor. Eski kurulu uygulamanın Uyku isteklerini Kernel-Power kayıtlarında S4 izliyor; kurulu EXE sürümü ve bildirilen eski S0 başarısı kesin doğrulanamadı. S0 engeli ile uyanma sonrası klavye düzeltmesi korundu, doğrulanmamış yeni güç API'si eklenmedi.
- Blocked the sleep shortcut on S0 devices after the reference laptop entered S4 following a sleep request. The notice directs users to Start → Power → Sleep. Classic sleep and the separate Hibernate shortcut retain their existing power calls.
- Referans dizüstü bilgisayarda uyku isteği S4 ile sonuçlandığı için S0 cihazlarda uyku kısayolu güç çağrısı yapmadan duruyor. Bildirim, kullanıcıyı Başlat → Güç → Uyku seçeneğine yönlendiriyor. Klasik uyku ve ayrı Hazırda Beklet kısayolu mevcut güç çağrılarını kullanmaya devam ediyor.

### Earlier testing attempts / Önceki test denemeleri

- Fixed Modern Standby capability detection: `SYSTEM_POWER_CAPABILITIES.AoAc` is at byte offset 20, not 25. The previous offset made S0-only systems appear to have no sleep state.
- Replaced the S0 display-off message with the documented `SetSuspendState` standby request using the native one-byte `BOOLEAN` ABI and `bHibernate=false`. The app does not fall back to S4; successful S0 entry still needs confirmation on the reference laptop.
- Added a truthful EN/TR sleep-state test matrix and recorded that S0 app-triggered sleep is still unverified, S3 is pending hardware testing, S1/S2 are untested, and S4 Hibernate is reported working.

- Modern Standby yetenek algılaması düzeltildi: `SYSTEM_POWER_CAPABILITIES.AoAc` alanı 25 değil 20. bayttadır. Eski konum, yalnız S0 destekleyen sistemleri uyku modu yokmuş gibi gösteriyordu.
- S0 ekran kapatma mesajı yerine, yerel tek baytlık `BOOLEAN` ABI'siyle ve `bHibernate=false` parametresiyle belgelenmiş `SetSuspendState` bekleme isteği kullanılıyor. Uygulama S4'e geri dönüş yapmıyor; S0 geçişinin başarısı referans dizüstü bilgisayarda doğrulanmayı bekliyor.
- EN/TR uyku durumu test tablosu gerçeğe uygun biçimde güncellendi: uygulamanın S0 testi henüz doğrulanmadı, S3 donanım testi bekliyor, S1/S2 test edilmedi, S4 Hazırda Beklet'in çalıştığı bildirildi.

- Reworked the `Pause` power path after a silent no-op on S0 laptops: injected `VK_SLEEP` is no longer treated as reliable proof of sleep.
- S0 uses a delayed display-off request to enter Windows' Modern Standby workflow; actual low-power entry remains controlled by Windows. Legacy S1/S2/S3 uses `SetSuspendState(false)` only when those states are available. No Hibernate fallback.
- Updated EN/TR README with realistic Modern Standby limits and `powercfg /a`, `/sleepstudy`, `/requests` testing guidance.

- S0 dizüstülerde `Pause` tuşunun sessizce hiçbir işlem yapmaması nedeniyle sanal `VK_SLEEP` yönteminden vazgeçildi.
- S0'da kısa tuş bırakma gecikmesinden sonra ekran kapatma/Modern Standby süreci isteniyor; gerçek düşük güç durumuna Windows karar veriyor. Klasik S1/S2/S3 varsa ayrı uyku yolu kullanılıyor; otomatik Hazırda Beklet'e geçiş yok.
- README'nin Türkçe/İngilizce uyku açıklamaları ve `powercfg` test önerileri güncellendi.

- Corrected native `SendInput` marshalling: the INPUT union now includes MOUSEINPUT (40 bytes on x64), fixing Win32 error 87 when requesting Windows Sleep with `Pause`. Actual S0 sleep still depends on the OS/power policy; no Hibernate fallback.
- Moved the custom screen saver **Test** button beside **Use a custom .scr file**.
- Replaced **Save** with **Restore Default Settings**, protected by a Yes/No confirmation.
- Added immediate settings application and INI persistence for toggles, hotkeys, language, hot corner and `.scr` selection. Numeric edits commit after a short typing pause or on field exit; invalid numbers keep the prior settings.
- The Settings window refreshes when reopened from the tray, reflecting tray-side changes.

- `Pause` ile uyku isteği gönderilirken Win32 87 hatasına neden olan eksik `INPUT` birleşim yapısı düzeltildi (x64'te 40 bayt). Gerçek S0 uyku kararı Windows'a bağlıdır; Hazırda Beklet'e geri dönüş yoktur.
- Özel ekran koruyucunun **Test Et** düğmesi, **Özel bir .scr dosyası kullan** seçeneğinin yanına taşındı.
- **Kaydet** yerine Evet/Hayır onaylı **Varsayılan Ayarlara Geri Dön** eklendi.
- Ayar değişiklikleri hemen çalışır ve INI'ye kaydedilir; sayısal alanlar kısa yazma duraklamasında veya alandan çıkınca uygulanır. Geçersiz sayı eski ayarı bozmaz.
- Tepsiden yeniden açılan Ayarlar penceresi güncel değerleri gösterir.

## 1.0.1 - 2026-11-08

- Improved S0/Modern Standby handling on PCs where `GetPwrCapabilities` reports no legacy sleep state: requests display-off through `WM_SYSCOMMAND / SC_MONITORPOWER` instead of incorrectly reporting no sleep or calling `SetSuspendState(false)`; never falls back to Hibernate.
- Status popups now grow to fit wrapped text and use measured height when positioning, preventing clipped messages.
- Updated screen saver toggle notifications to “Screen saver turned on.” / “Screen saver turned off.”

- S3 bulunmayan ve güç yetenekleri eksik raporlanan bilgisayarlarda S0 için `WM_SYSCOMMAND / SC_MONITORPOWER` ile ekranı kapatma isteği kullanılıyor; yanlış “uyku yok” mesajı ve otomatik Hazırda Beklet geçişi engelleniyor.
- Bildirim penceresi metne göre büyüyor; uzun mesajların üstten/alttan kesilmesi önleniyor.
- Ekran koruyucu bildirimleri “Ekran koruyucu açıldı.” / “Ekran koruyucu kapatıldı.” olarak değiştirildi.

- Sleep now detects legacy S1-S3 versus Modern Standby (S0). S0 uses a guarded Windows Sleep-key request instead of `SetSuspendState(false)`; unsupported/policy-mismatched cases fail safely and never fall back to Hibernate.
- Tray context menu now has colored action icons (gear, start, green check, red X, exit), and labels explicitly describe the automatic screen saver's On/Off status.
- Clarified Modern Standby limitations and recommended pre-release hardware testing in README.

- Uyku işlevi artık klasik S1-S3 ile Modern Standby (S0) ayrımını yapıyor. S0 için `SetSuspendState(false)` yerine güvenlik kontrollü Windows Uyku tuşu isteği kullanılıyor; başarısız/uygunsuz durumlarda Hazırda Beklet'e geçilmiyor.
- Sistem tepsisi menüsüne renkli eylem simgeleri (çark, başlat, yeşil tik, kırmızı çarpı, çıkış) eklendi; otomatik ekran koruyucu Açık/Kapalı durum metinleri netleştirildi.
- README'de Modern Standby sınırlamaları ve yayın öncesi donanım testi açıklandı.


- Stabilized `Pause` / `Shift + Pause` shortcut handling after Sleep or Hibernate resume by resetting transient keyboard state.
- Explicitly allow the settings window to close when **Exit Program** is selected from the tray; normal window close still hides to tray.
- Kept WPF per-monitor DPI configuration in `app.manifest` and narrowly excluded the Windows Forms-only `WFO0003` warning for this mixed WPF/NotifyIcon app.
- Updated version metadata and the About dialog to 1.0.1; `CS0649` is resolved without suppressing it.

- Uyku veya Hazırda Beklet dönüşünde `Pause` / `Shift + Pause` kısayollarının geçici klavye durumunu sıfırlayarak güvenilir çalışması iyileştirildi.
- Tepsideki **Programdan Çık** seçildiğinde ayar penceresinin kapanmasına açıkça izin verildi; pencere normal kapatıldığında tepsiye küçülme davranışı korundu.
- WPF monitör-bazlı DPI ayarları korundu; yalnız karma WPF/NotifyIcon yapısına uygun olmayan `WFO0003` uyarısı kapsamlı açıklamayla devre dışı bırakıldı.
- Sürüm bilgileri ve Hakkında ekranı 1.0.1 olarak güncellendi; `CS0649` uyarısı kaynağında giderildi.

## 1.0.0 - 2026-09-19

- Initial GitHub-ready release candidate.
- Classic `.scr` screen savers can run without mandatory Windows locking.
- Independent idle and lock timers.
- Video/full-screen/presentation guards.
- Hot Corner support.
- Configurable global shortcuts.
- Optional Sleep/Hibernate keyboard shortcuts.
- Sleep/standby request supports modern Windows power models.
- Turkish/English UI; English fallback for all non-Turkish Windows display languages.
- Per-user portable-style `Setup.cmd` installer under `%AppData%`.
- No service, driver, or application-setting Registry writes.

- Replaced the application icon with the monitor-and-bubbles screen saver icon.
- Uygulama simgesi, monitör ve baloncuk ekran koruyuculu yeni simgeyle değiştirildi.

- Tray status now uses green-tinted and grayscale variants of the main monitor-and-bubbles icon.
- Sistem tepsisi durumu artık ana monitör-baloncuk simgesinin yeşilimsi ve gri varyantlarını kullanıyor.

- Application now starts directly in the system tray and the tray status icon is enabled by default.
- Fixed installed tray icons by embedding active/inactive icons into the executable resources.
- Replaced the Settings-window Hide button with “Minimize to System Tray”; program exit is available only from the tray menu.
- Uygulama artık doğrudan sistem tepsisinde başlıyor ve tray durum simgesi varsayılan olarak açık.
- Kurulu sürümde görünmeyen tray simgesi sorunu, aktif/pasif ikonların EXE kaynağına gömülmesiyle düzeltildi.
- Ayarlar penceresindeki Gizle düğmesi “Sistem Tepsisine Küçült” olarak değiştirildi; tamamen çıkış yalnız tray menüsünden yapılır.

- Fixed RG1000 build failure caused by a duplicate Assets/AppIcon.png WPF Resource entry.
- Assets/AppIcon.png dosyasının iki kez WPF Resource olarak eklenmesinden kaynaklanan RG1000 derleme hatası düzeltildi.
- Sleep/Standby and Hibernate shortcuts now have independent On/Off switches; both are disabled by default.
- Setup.cmd now uses a temporary Consolas 18 font in the classic Console Host for improved readability without changing Registry settings.
- Uyku/Bekleme ve Hazırda Beklet kısayolları artık ayrı ayrı Aç/Kapa yapılabiliyor; ikisi de varsayılan olarak kapalı.
- Setup.cmd, Registry ayarı değiştirmeden okunabilirlik için klasik Console Host'ta geçici Consolas 18 kullanıyor.
- All four keyboard shortcuts now have independent On/Off switches and are disabled by default.
- Ekran koruyucu kısayolları dahil dört klavye kısayolunun tamamı artık ayrı ayrı Aç/Kapa yapılabiliyor ve varsayılan olarak kapalı geliyor.
- Build-Release.cmd now resolves the project from its own location and includes a recursive fallback for accidental extra extraction folder levels.
- Build-Release.cmd artık proje yolunu kendi konumundan çözüyor ve yanlışlıkla oluşan ek klasör katmanı için alt klasörlerde arama yedeği içeriyor.
- Uninstall now waits for the application process to terminate, retries AppData folder removal, verifies deletion, and reports an error instead of a false success.
- Uninstall also removes the legacy `%AppData%\ClassicDesktopScreensaver` pre-release folder if present.
- Kaldırma işlemi artık uygulamanın gerçekten kapanmasını bekliyor, AppData klasörünü tekrar deneyerek siliyor ve silme başarısızsa yanlış başarı mesajı vermiyor.
- Varsa eski `%AppData%\ClassicDesktopScreensaver` ön-sürüm klasörü de kaldırılıyor.
- Fixed Sleep/Hibernate shortcut state surviving a power transition when Windows omits key-up events.
- Modifier keys are now tracked from the low-level keyboard hook stream, and transient keyboard state is reset on WM_POWERBROADCAST resume.
- Uyku/Hazırda Beklet geçişinde key-up olayı gelmediğinde kısayol durumunun uyandıktan sonra taşınması sorunu düzeltildi.
- Shift/Ctrl/Alt/Win artık düşük seviyeli klavye olaylarından izleniyor; Windows resume mesajında geçici klavye durumu sıfırlanıyor.

### Additional changes (testing candidate)
- S0 and legacy sleep: use a policy-guarded Windows Sleep key request instead of monitor-off or SetSuspendState(false); add key-release delay to avoid immediate wake. No Hibernate fallback.
- Scroll Lock toggle notifications use Windows Notification Center with ✅ / ❌ icons and a dedicated Settings toggle (preserves INI setting).
- Added a separate custom .scr Test button; Windows Start button always starts Windows-selected screen saver.
- Uninstall attempts to clean Windows notification registration; README documents OS-specific toast infrastructure HKCU entries.
- S0 ve klasik uyku için ekran kapatma / SetSuspendState(false) yerine güç planı denetimli Windows Uyku tuşu isteği ve tuş bırakma gecikmesi eklendi; Hazırda Beklet'e dönüş yok.
- Scroll Lock bildirimleri ✅ / ❌ simgeleriyle Windows Bildirim Merkezi'ne taşındı; Ayarlar'da bağımsız Aç/Kapa seçeneği var.
- Özel .scr için ayrı Test düğmesi eklendi; Windows'u Şimdi Başlat yalnız Windows seçili koruyucuyu açıyor.
