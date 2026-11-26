#pragma once

#include <Windows.h>

#include <string_view>

namespace classic_desktop_screensaver
{
    void SetUiDiagnosticLoggingEnabled(bool enabled) noexcept;
    void LogUiDiagnostic(
        std::wstring_view event,
        HWND window = nullptr,
        UINT message = 0,
        WPARAM wParam = 0,
        LPARAM lParam = 0,
        BOOL apiResult = TRUE,
        DWORD errorCode = ERROR_SUCCESS,
        std::wstring_view detail = {}) noexcept;
    void LogPowerRequestDiagnostic(
        HWND window,
        bool hibernate,
        BOOL accepted,
        DWORD errorCode,
        std::wstring_view detail) noexcept;
    void LogRuntimeDiagnostic(
        std::wstring_view event,
        BOOL apiResult = TRUE,
        DWORD errorCode = ERROR_SUCCESS,
        std::wstring_view detail = {}) noexcept;
}
