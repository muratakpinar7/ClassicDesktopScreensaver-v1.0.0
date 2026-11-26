#pragma once

namespace classic_desktop_screensaver
{
    struct UiTestPolicy
    {
        bool applyStartupSettings{};
        bool installKeyboardHook{};
        bool startRuntimeTimers{};
        bool persistSettings{};
        bool allowSaverActions{};
        bool allowPowerActions{};
        bool suppressWindowsScreenSaver{};
        bool readOnlySettings{};
    };

    [[nodiscard]] constexpr UiTestPolicy MakeUiTestPolicy(bool safeUiTest) noexcept
    {
        return {
            !safeUiTest,
            !safeUiTest,
            !safeUiTest,
            !safeUiTest,
            !safeUiTest,
            !safeUiTest,
            !safeUiTest,
            safeUiTest
        };
    }
}
