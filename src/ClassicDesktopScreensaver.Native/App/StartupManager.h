#pragma once

#include <string>

namespace classic_desktop_screensaver
{
    class StartupManager final
    {
    public:
        [[nodiscard]] bool Apply(bool enabled, std::wstring& error) const;
    };
}
