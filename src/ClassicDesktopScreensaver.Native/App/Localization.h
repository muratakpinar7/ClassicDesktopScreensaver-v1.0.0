#pragma once

#include "..\Core\AppSettings.h"

#include <string_view>

namespace classic_desktop_screensaver
{
    [[nodiscard]] AppLanguage ResolveLanguage(AppLanguage requested) noexcept;
    [[nodiscard]] std::wstring_view Text(AppLanguage language, std::wstring_view key) noexcept;
}
