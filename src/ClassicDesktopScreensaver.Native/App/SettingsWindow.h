#pragma once

#include "..\Core\AppSettings.h"
#include "..\Core\PowerCapabilities.h"

#include <Windows.h>

#include <string>

namespace classic_desktop_screensaver
{
    enum class SettingsControlId : int
    {
        AutoScreenSaver = 108,
        HotCornerCombo = 116,
        Language = 130,
        HibernateHotkeyEnabled = 124,
        HibernateHotkey = 125,
        HibernateStatus = 126,
        ScreenSaverGroup = 200,
        AutomaticGroup = 201,
        GuardsGroup = 202,
        HotCornerGroup = 203,
        HotkeysGroup = 204,
        BehaviorGroup = 205,
        RegistryNote = 206
    };

    struct SettingsHost
    {
        void* context{};
        AppSettings (*getSettings)(void*){};
        bool (*updateSettings)(void*, const AppSettings&, std::wstring&){};
        HibernateAvailability (*refreshHibernate)(void*){};
        std::wstring (*windowsSaverPath)(void*){};
        bool (*startSaver)(void*, const std::wstring&, std::wstring&){};
        void (*openSaverSettings)(void*){};
        void (*keyboardSuspended)(void*, bool){};
        bool readOnly{};
    };

    [[nodiscard]] HWND CreateSettingsWindow(
        HINSTANCE instance,
        HWND owner,
        SettingsHost* host) noexcept;
    void ShowSettingsWindow(HWND window) noexcept;
}
