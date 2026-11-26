#pragma once

#include "TrayMenuVisuals.h"

#include <array>
#include <string_view>

namespace classic_desktop_screensaver
{
    inline constexpr UINT MenuSettings = 1;
    inline constexpr UINT MenuStartSaver = 2;
    inline constexpr UINT MenuToggleSaver = 3;
    inline constexpr UINT MenuExit = 4;
    inline constexpr UINT TrayRegistrationRetryInitialIntervalMs = 1000;
    inline constexpr UINT TrayRegistrationRetryMaximumIntervalMs = 15000;

    [[nodiscard]] constexpr bool ShouldRetryTrayRegistration(
        bool trayAdded, unsigned int) noexcept
    {
        return !trayAdded;
    }

    [[nodiscard]] constexpr UINT TrayRegistrationRetryDelayMs(
        unsigned int retryAttempts) noexcept
    {
        if (retryAttempts >= 4) return TrayRegistrationRetryMaximumIntervalMs;
        return TrayRegistrationRetryInitialIntervalMs << retryAttempts;
    }

    struct TrayMenuEntryModel
    {
        UINT command{};
        std::wstring_view localizationKey;
        TrayMenuGlyph glyph{};
        bool enabled{};
    };

    [[nodiscard]] constexpr std::array<TrayMenuEntryModel, 4> MakeTrayMenuModel(
        bool saverActionsAllowed,
        bool automaticScreenSaverEnabled) noexcept
    {
        return {{
            { MenuSettings, L"TraySettings", TrayMenuGlyph::Settings, true },
            { MenuStartSaver, L"TrayStart", TrayMenuGlyph::Start, saverActionsAllowed },
            { MenuToggleSaver,
                automaticScreenSaverEnabled ? L"TrayStatusActive" : L"TrayStatusInactive",
                automaticScreenSaverEnabled ? TrayMenuGlyph::Enabled : TrayMenuGlyph::Disabled,
                saverActionsAllowed },
            { MenuExit, L"Exit", TrayMenuGlyph::Exit, true }
        }};
    }
}
