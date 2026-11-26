#pragma once

namespace classic_desktop_screensaver
{
    [[nodiscard]] constexpr bool ShouldShowSettingsAtLaunch(
        bool backgroundMode,
        bool explicitSettingsRequest) noexcept
    {
        return explicitSettingsRequest || !backgroundMode;
    }
}
