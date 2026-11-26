#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

namespace classic_desktop_screensaver
{
    enum class TrayMenuGlyph : std::uint8_t
    {
        Settings,
        Start,
        Enabled,
        Disabled,
        Exit
    };

    class TrayMenuImages final
    {
    public:
        explicit TrayMenuImages(UINT dpi) noexcept;
        ~TrayMenuImages();
        TrayMenuImages(const TrayMenuImages&) = delete;
        TrayMenuImages& operator=(const TrayMenuImages&) = delete;

        [[nodiscard]] HBITMAP Get(TrayMenuGlyph glyph) const noexcept;
        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] UINT Size() const noexcept { return size_; }

    private:
        ULONG_PTR gdiplusToken_{};
        UINT size_{};
        std::array<HBITMAP, 5> bitmaps_{};
    };
}
