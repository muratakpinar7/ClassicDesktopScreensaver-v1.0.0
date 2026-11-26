#pragma once

#include <string>

namespace classic_desktop_screensaver
{
    enum class AppLanguage
    {
        Auto,
        Turkish,
        English
    };

    enum class HotCorner
    {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    struct AppSettings
    {
        AppLanguage language{ AppLanguage::Auto };

        bool autoScreenSaverEnabled{ true };
        int screenSaverIdleMinutes{ 2 };

        bool lockEnabled{ false };
        int lockIdleMinutes{ 10 };

        // Retained only to round-trip older settings.ini files. The Native
        // automatic screen saver does not use video playback as a guard.
        bool legacyBlockScreenSaverWhileMediaPlaying{ true };
        bool blockScreenSaverWhileFullscreen{ true };
        bool suspendLockWhileMediaOrPresentation{ true };

        bool hotCornerEnabled{ false };
        HotCorner hotCorner{ HotCorner::TopRight };
        int hotCornerDelayMs{ 500 };

        bool useCustomScreenSaver{};
        std::wstring screenSaverPath;

        bool startScreenSaverHotkeyEnabled{ false };
        std::wstring startScreenSaverHotkey{ L"Shift+ScrollLock" };
        bool toggleScreenSaverHotkeyEnabled{ false };
        std::wstring toggleScreenSaverHotkey{ L"ScrollLock" };
        bool sleepHotkeyEnabled{ false };
        std::wstring sleepHotkey{ L"Pause" };
        bool hibernateHotkeyEnabled{ false };
        std::wstring hibernateHotkey{ L"Shift+Pause" };

        bool showStatusNotifications{ true };
        bool showTrayStatusIcon{ true };
        bool startWithWindows{ false };
    };

    [[nodiscard]] inline const std::wstring& ScreenSaverPathForLaunch(
        const AppSettings& settings) noexcept
    {
        static const std::wstring windowsSelectedSaver;
        return settings.useCustomScreenSaver
            ? settings.screenSaverPath
            : windowsSelectedSaver;
    }
}
