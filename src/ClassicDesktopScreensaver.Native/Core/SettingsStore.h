#pragma once

#include "AppSettings.h"

#include <filesystem>
#include <string>

namespace classic_desktop_screensaver
{
    class SettingsStore final
    {
    public:
        explicit SettingsStore(std::filesystem::path path);

        [[nodiscard]] static std::filesystem::path DefaultPath();
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

        // A missing file is a successful load of defaults. Other read/UTF-8
        // failures are returned to the caller without mutating the file.
        [[nodiscard]] bool Load(AppSettings& settings, std::wstring& error) const noexcept;
        [[nodiscard]] bool Save(const AppSettings& settings, std::wstring& error) const noexcept;

    private:
        std::filesystem::path path_;
    };
}
