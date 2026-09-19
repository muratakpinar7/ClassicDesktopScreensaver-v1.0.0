# ClassicDesktopScreensaver-v1.0.0

# English

## Overview

Classic Desktop Screensaver is a small, free Windows utility that launches classic `.scr` screen savers directly on the desktop using its own idle timer.

It is designed for Windows 10/11 users who want the traditional screen saver behavior without being forced into the Windows lock screen first.

The application is intentionally lightweight: no Windows service, no driver, no telemetry, no administrator rights, and no application settings written to the Windows Registry.

## Requirements

- **Minimum:** Windows 10 version 1809, build **17763**
- Windows 11 is supported.
- The source project uses **.NET 10 / WPF**.
- `Build-Release.cmd` produces a self-contained `win-x64` build, so the target PC does not need a separate .NET Runtime installation.

## Main features

- Own idle timer based on Windows user-input activity.
- Runs the `.scr` screen saver selected in Windows.
- Optional custom `.scr` file.
- Screen saver can run without automatically locking Windows.
- Independent optional Windows lock timer.
- Video playback protection.
- Full-screen application / presentation protection.
- Optional lock-delay protection during video and presentations.
- Hot Corner support on the primary display.
- Configurable global keyboard shortcuts.
- Optional lower-right status notifications.
- Optional system-tray status icon.
- Optional start-with-Windows support through the user's Startup folder.
- Turkish and English user interface.

## Default keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Shift + Scroll Lock` | Start the screen saver immediately |
| `Scroll Lock` | Enable / disable the automatic screen saver |
| `Pause` | Sleep / supported standby mode — **disabled by default** |
| `Shift + Pause` | Hibernate — **disabled by default** |

All shortcuts can be changed in Settings.

### Shortcut switches

All four keyboard shortcuts have their own independent On/Off switch:

- Start screen saver
- Toggle automatic screen saver
- Sleep / Standby
- Hibernate

**All keyboard shortcuts are disabled by default.** Users enable only the shortcuts they want. When a shortcut is disabled, Classic Desktop Screensaver does not intercept that key combination and it is passed through normally.

### Scroll Lock note

`Scroll Lock` is rarely used today, but it still has a real function in some software. Microsoft Excel, for example, can change arrow-key scrolling behavior when Scroll Lock is active.

When `Scroll Lock` is assigned to Classic Desktop Screensaver, the application intercepts the configured shortcut and prevents the normal Scroll Lock state/LED from changing. The shortcut can be changed at any time.

## Sleep / standby behavior

Newer PCs may not support legacy S1-S3 Sleep states and may use **Modern Standby (S0 Low Power Idle)** instead.

The `Pause` action asks Windows to enter the supported suspend/standby state. Windows selects the actual power model available on that computer.

Hibernate still requires Hibernate to be enabled and supported by Windows and the hardware.

## Video and presentation protection

Automatic screen saver activation can be delayed while visual media is playing.

The program uses the Windows global media-session API available from Windows 10 version 1809 and also checks full-screen / presentation state.

Media detection cannot be guaranteed for every third-party application, so full-screen detection acts as an additional protection layer.

## Language

The program contains two interface languages:

- English
- Türkçe

Default setting: **Automatic (Windows language)**.

- If the Windows display language is Turkish, the application opens in Turkish.
- On every other Windows display language, the application opens in English.
- The user can manually select **English** or **Türkçe** in Settings.
- `Setup.cmd` follows the same rule.

## Portable use

The application can be run directly from the release folder without installation.

`settings.ini` is stored next to the executable.

## Setup.cmd installation

- `Setup.cmd` opens under the classic Console Host and temporarily uses **Consolas 18** for better readability. It does not change permanent console or Registry settings.

`Setup.cmd` provides an optional, simple per-user installation.

It:

- does **not** require administrator rights or UAC,
- checks for Windows 10 version 1809 / build 17763 or later,
- installs only for the currently signed-in Windows user,
- copies the application to `%AppData%\Classic Desktop Screensaver`,
- creates a Start Menu shortcut for that user,
- does not install a service or driver,
- does not write application settings to the Registry,
- can uninstall the application and its shortcuts.

The installation is **not shared with other Windows user accounts**. If another user should use the application, run `Setup.cmd` separately while signed in to that user's account.

After a successful Install or Uninstall operation, Setup displays the result, waits for a key press, and exits instead of returning to the menu.

## Registry policy

Classic Desktop Screensaver does **not write its configuration or installation data to the Windows Registry**.

It may read the existing Windows screen saver setting below in read-only mode to determine the `.scr` file currently selected by Windows:

`HKCU\Control Panel\Desktop\SCRNSAVE.EXE`

Application settings are stored in `settings.ini`.

## Start with Windows

The application does not use the Registry `Run` key.

When **Start with Windows** is enabled, it creates a shortcut in the current user's Startup folder and launches the application with `--background`.

## About / AI disclosure

The application's **About / Info** window includes the minimum Windows requirement, version, license, privacy/installation notes, and the following disclosure:

> The source code was prepared by AI (OpenAI ChatGPT) based on the project requirements.

The project is released under the MIT License.

## Build

Requirements for building from source:

- Windows 10/11
- .NET 10 SDK

Run:

```text
Build-Release.cmd
```

The release folder is created under:

```text
dist\ClassicDesktopScreensaver-win-x64
```

## Known first-release limitations

- Hot Corner currently uses the corners of the **primary display**.
- Some third-party `.scr` files may behave differently from standard Windows screen savers.
- Media-session detection depends on what the playing application exposes to Windows.
- Corporate Windows policies may restrict power, lock, or screen saver behavior.

## License

MIT License. See `LICENSE`.

---

# Türkçe

## Genel Bakış

Classic Desktop Screensaver, klasik Windows `.scr` ekran koruyucularını kendi boşta-kalma sayacıyla doğrudan masaüstünde çalıştıran küçük ve ücretsiz bir Windows yardımcı programıdır.

Windows 10/11'de klasik ekran koruyucu davranışını isteyen, ancak önce Windows kilit ekranına geçilmesini istemeyen kullanıcılar için tasarlanmıştır.

Program özellikle sade tutulmuştur: Windows servisi yoktur, sürücü kurmaz, telemetri içermez, yönetici yetkisi istemez ve uygulama ayarlarını Windows Registry'sine yazmaz.

## Sistem Gereksinimi

- **Minimum:** Windows 10 version 1809, build **17763**
- Windows 11 desteklenir.
- Kaynak proje **.NET 10 / WPF** kullanır.
- `Build-Release.cmd`, self-contained `win-x64` sürümü üretir; hedef bilgisayarda ayrıca .NET Runtime kurulu olması gerekmez.

## Temel Özellikler

- Windows kullanıcı girişini temel alan kendi idle/boşta-kalma sayacı.
- Windows'ta seçili `.scr` ekran koruyucusunu çalıştırma.
- İsteğe bağlı özel `.scr` dosyası seçimi.
- Windows'u otomatik kilitlemeden ekran koruyucu çalıştırabilme.
- Bağımsız ve isteğe bağlı Windows kilitleme süresi.
- Video oynatımı sırasında otomatik ekran koruyucuyu erteleme.
- Tam ekran uygulama / sunum koruması.
- Video ve sunum sırasında otomatik kilitlemeyi de erteleme seçeneği.
- Ana monitörde Hot Corner desteği.
- Değiştirilebilir global klavye kısayolları.
- İsteğe bağlı sağ-alt durum bildirimi.
- İsteğe bağlı sistem tepsisi durum simgesi.
- Kullanıcının Startup klasörü üzerinden Windows ile başlatma.
- Türkçe ve İngilizce arayüz.

## Varsayılan Klavye Kısayolları

| Kısayol | İşlev |
|---|---|
| `Shift + Scroll Lock` | Ekran koruyucuyu hemen başlatır — **varsayılan kapalı** |
| `Scroll Lock` | Otomatik ekran koruyucuyu açar / kapatır |
| `Pause` | Uyku / desteklenen bekleme moduna geçirir — **varsayılan kapalı** |
| `Shift + Pause` | Hazırda Beklet'e geçirir — **varsayılan kapalı** |

Bütün kısayollar Ayarlar bölümünden değiştirilebilir.

### Kısayolları ayrı ayrı Aç/Kapa

Dört klavye kısayolunun da kendi bağımsız Aç/Kapa seçeneği vardır:

- Ekran koruyucuyu başlat
- Otomatik ekran koruyucuyu aktif/pasif yap
- Uyku / Bekleme
- Hazırda Beklet

**Bütün klavye kısayolları varsayılan olarak kapalıdır.** Kullanıcı yalnız istediği kısayolları etkinleştirir. Bir kısayol kapalıysa Classic Desktop Screensaver o tuş kombinasyonunu yakalamaz ve tuş normal şekilde sisteme/uygulamalara iletilir.

### Scroll Lock notu

`Scroll Lock` günümüzde çok az kullanılsa da bazı programlarda gerçek bir işleve sahiptir. Örneğin Microsoft Excel, Scroll Lock açıkken yön tuşlarının kaydırma davranışını değiştirebilir.

Scroll Lock Classic Desktop Screensaver'a atanmışsa program ilgili tuş olayını yakalar ve normal Scroll Lock durumu/LED değişikliğini engeller. Kısayol istenildiği zaman değiştirilebilir.

## Uyku / Bekleme Davranışı

Yeni nesil bilgisayarların bazıları klasik S1-S3 Sleep durumlarını desteklemez ve bunun yerine **Modern Standby (S0 Low Power Idle)** kullanır.

`Pause` işlevi Windows'tan bilgisayarın desteklediği askıya alma/bekleme durumuna geçmesini ister. Kullanılacak gerçek güç modelini Windows belirler.

Hazırda Beklet işlevinin ise Windows'ta açık ve donanım tarafından destekleniyor olması gerekir.

## Video ve Sunum Koruması

Görsel medya oynatılırken otomatik ekran koruyucunun başlaması ertelenebilir.

Program Windows 10 version 1809 ile gelen global medya oturumu API'sini kullanır; ayrıca tam ekran ve sunum durumlarını da kontrol eder.

Her üçüncü taraf uygulama medya bilgisini Windows'a aynı şekilde bildirmediği için medya tespiti yüzde 100 garanti değildir. Tam ekran kontrolü ek koruma katmanı olarak kullanılır.

## Dil

Program iki arayüz dili içerir:

- English
- Türkçe

Varsayılan seçim **Otomatik (Windows dili)** şeklindedir.

- Windows görüntüleme dili Türkçe ise program Türkçe açılır.
- Diğer bütün Windows dillerinde program İngilizce açılır.
- Kullanıcı Ayarlar bölümünden dili elle **Türkçe** veya **English** olarak sabitleyebilir.
- `Setup.cmd` de aynı kuralı kullanır.

## Portable Kullanım

Program release klasöründen kurulum yapılmadan doğrudan çalıştırılabilir.

`settings.ini` dosyası EXE'nin yanında tutulur.

## Setup.cmd ile Kurulum

- `Setup.cmd`, daha rahat okunması için klasik Console Host altında açılır ve yalnız o pencere için geçici olarak **Consolas 18** kullanır. Kalıcı konsol veya Registry ayarı değiştirmez.

`Setup.cmd`, isteyen kullanıcı için basit ve kullanıcı-bazlı kurulum sağlar.

Kurulum:

- yönetici yetkisi veya UAC istemez,
- Windows 10 version 1809 / build 17763 veya üzerini kontrol eder,
- yalnız mevcut Windows kullanıcısı için yapılır,
- programı `%AppData%\Classic Desktop Screensaver` altına kopyalar,
- yalnız o kullanıcı için Başlat Menüsü kısayolu oluşturur,
- Windows servisi veya sürücü kurmaz,
- uygulama ayarlarını Registry'ye yazmaz,
- Uninstall ile programı ve oluşturulan kısayolları kaldırır.

Kurulum **diğer Windows kullanıcı hesaplarıyla paylaşılmaz**. Program başka bir kullanıcı hesabında da kullanılacaksa, o kullanıcıyla oturum açıldıktan sonra `Setup.cmd` ayrıca çalıştırılmalıdır.

Install veya Uninstall başarıyla tamamlandığında Setup sonucu bildirir, bir tuşa basılmasını bekler ve menüye dönmeden kapanır.

## Registry Politikası

Classic Desktop Screensaver kendi kurulum veya uygulama ayarlarını Windows Registry'sine **yazmaz**.

Windows'ta seçili `.scr` ekran koruyucusunu belirlemek için aşağıdaki mevcut Windows değerini salt-okunur olarak okuyabilir:

`HKCU\Control Panel\Desktop\SCRNSAVE.EXE`

Program ayarları `settings.ini` dosyasında tutulur.

## Windows ile Başlatma

Program Registry `Run` anahtarını kullanmaz.

**Windows başladığında başlat** seçildiğinde mevcut kullanıcının Startup klasörüne bir kısayol oluşturulur ve program `--background` parametresiyle çalıştırılır.

## Hakkında / Yapay Zekâ Bilgisi

Programın **Hakkında / Info** penceresinde minimum Windows sürümü, program sürümü, lisans ve kurulum/gizlilik bilgileriyle birlikte şu açıklama bulunur:

> Bu yazılımın kaynak kodu, proje gereksinimleri doğrultusunda yapay zekâ (OpenAI ChatGPT) tarafından hazırlanmıştır.

Proje MIT Lisansı ile yayımlanır.

## Derleme

Kaynak koddan derlemek için:

- Windows 10/11
- .NET 10 SDK

gereklidir.

Çalıştırın:

```text
Build-Release.cmd
```

Release klasörü şurada oluşturulur:

```text
dist\ClassicDesktopScreensaver-win-x64
```

## İlk Sürümde Bilinen Sınırlar

- Hot Corner şimdilik **ana monitörün** köşelerini kullanır.
- Bazı üçüncü taraf `.scr` dosyaları standart Windows ekran koruyucularından farklı davranabilir.
- Medya algılama, oynatıcı uygulamanın Windows'a sunduğu bilgilere bağlıdır.
- Kurumsal Windows ilkeleri güç, kilitleme veya ekran koruyucu davranışlarını kısıtlayabilir.

## Lisans

MIT Lisansı. Ayrıntılar için `LICENSE` dosyasına bakın.

