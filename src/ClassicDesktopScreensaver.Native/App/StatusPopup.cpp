#include "StatusPopup.h"

#include <CommCtrl.h>

#include <algorithm>

#pragma comment(lib, "Comctl32.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t PopupClass[] = L"ClassicDesktopScreensaver.Native.StatusPopup";
        constexpr UINT_PTR PopupTimer = 0x52;
    }

    StatusPopup::~StatusPopup()
    {
        if (window_ != nullptr) DestroyWindow(window_);
    }

    void StatusPopup::Show(const std::wstring& text) noexcept
    {
        try
        {
        text_ = text;
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
            window_ = nullptr;
        }

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1);
        windowClass.lpszClassName = PopupClass;
        RegisterClassExW(&windowClass);

        const HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{ sizeof(info) };
        const bool haveMonitor = monitor != nullptr && GetMonitorInfoW(monitor, &info) != FALSE;
        const int right = haveMonitor ? info.rcWork.right : GetSystemMetrics(SM_CXSCREEN);
        const int bottom = haveMonitor ? info.rcWork.bottom : GetSystemMetrics(SM_CYSCREEN);
        constexpr int width = 390;
        constexpr int minimumHeight = 78;
        const int workHeight = haveMonitor
            ? info.rcWork.bottom - info.rcWork.top
            : GetSystemMetrics(SM_CYSCREEN);
        int height = minimumHeight;
        HDC measureDc = GetDC(nullptr);
        if (measureDc != nullptr)
        {
            const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HGDIOBJ previousFont = font != nullptr ? SelectObject(measureDc, font) : nullptr;
            RECT measured{ 0, 0, width - 32, 0 };
            DrawTextW(measureDc, text_.c_str(), static_cast<int>(text_.size()), &measured,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            height = std::max(minimumHeight, static_cast<int>(measured.bottom) + 20);
            const int maximumHeight = std::max(minimumHeight, workHeight - 32);
            height = std::min(height, maximumHeight);
            if (previousFont != nullptr) SelectObject(measureDc, previousFont);
            ReleaseDC(nullptr, measureDc);
        }
        const int x = right - width - 18;
        const int y = bottom - height - 18;

        window_ = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            PopupClass,
            L"",
            WS_POPUP | WS_BORDER,
            x, y, width, height,
            nullptr, nullptr, instance_, this);
        if (window_ != nullptr)
        {
            SetWindowPos(window_, HWND_TOPMOST, x, y, width, height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (SetTimer(window_, PopupTimer, 3000, nullptr) == 0)
            {
                DestroyWindow(window_);
                window_ = nullptr;
            }
        }
        }
        catch (...) { }
    }

    LRESULT CALLBACK StatusPopup::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT bounds{};
            GetClientRect(window, &bounds);
            FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
            auto* owner = reinterpret_cast<StatusPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (owner != nullptr)
            {
                bounds.left += 16;
                bounds.right -= 16;
                bounds.top += 10;
                bounds.bottom -= 10;
                DrawTextW(dc, owner->text_.c_str(), static_cast<int>(owner->text_.size()), &bounds, DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_TIMER:
            if (wParam == PopupTimer)
            {
                KillTimer(window, PopupTimer);
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_NCDESTROY:
        {
            auto* owner = reinterpret_cast<StatusPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (owner != nullptr && owner->window_ == window) owner->window_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
