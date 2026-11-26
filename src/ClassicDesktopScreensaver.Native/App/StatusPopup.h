#pragma once

#include <Windows.h>

#include <string>

namespace classic_desktop_screensaver
{
    class StatusPopup final
    {
    public:
        explicit StatusPopup(HINSTANCE instance) noexcept : instance_(instance) {}
        ~StatusPopup();
        StatusPopup(const StatusPopup&) = delete;
        StatusPopup& operator=(const StatusPopup&) = delete;
        void Show(const std::wstring& text) noexcept;

    private:
        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        HINSTANCE instance_{};
        HWND window_{};
        std::wstring text_;
    };
}
