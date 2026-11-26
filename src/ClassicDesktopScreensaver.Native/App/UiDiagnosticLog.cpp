#include "UiDiagnosticLog.h"

#include <string>
#include <vector>

namespace classic_desktop_screensaver
{
    namespace
    {
        bool enabled{};

        struct LastErrorPreserver
        {
            DWORD value{ GetLastError() };
            ~LastErrorPreserver() { SetLastError(value); }
        };

        bool AppendUtf8(std::wstring_view line) noexcept
        {
            try
            {
                std::vector<wchar_t> directory(32768);
                const DWORD length = GetTempPathW(
                    static_cast<DWORD>(directory.size()), directory.data());
                if (length == 0 || static_cast<std::size_t>(length) >= directory.size()) return false;

                std::wstring path(directory.data(), length);
                path += L"ClassicDesktopScreensaver.Native-UI.log";
                HANDLE file = CreateFileW(
                    path.c_str(), FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) return false;

                const int byteCount = WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, line.data(),
                    static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
                bool success = byteCount > 0;
                if (success)
                {
                    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
                    success = WideCharToMultiByte(
                        CP_UTF8, WC_ERR_INVALID_CHARS, line.data(),
                        static_cast<int>(line.size()), utf8.data(), byteCount,
                        nullptr, nullptr) == byteCount;
                    if (success)
                    {
                        DWORD written = 0;
                        success = WriteFile(file, utf8.data(),
                            static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE &&
                            static_cast<std::size_t>(written) == utf8.size();
                    }
                }
                CloseHandle(file);
                return success;
            }
            catch (...)
            {
                return false;
            }
        }

        void LogDiagnostic(
            std::wstring_view event,
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            BOOL apiResult,
            DWORD errorCode,
            std::wstring_view detail,
            bool force) noexcept
        {
            const LastErrorPreserver preserveLastError{};
            if (!force && !enabled) return;
            try
            {
                SYSTEMTIME time{};
                GetLocalTime(&time);
                std::wstring line;
                line.reserve(256 + event.size());
                line += std::to_wstring(time.wYear) + L"-";
                if (time.wMonth < 10) line += L"0";
                line += std::to_wstring(time.wMonth) + L"-";
                if (time.wDay < 10) line += L"0";
                line += std::to_wstring(time.wDay) + L" ";
                if (time.wHour < 10) line += L"0";
                line += std::to_wstring(time.wHour) + L":";
                if (time.wMinute < 10) line += L"0";
                line += std::to_wstring(time.wMinute) + L":";
                if (time.wSecond < 10) line += L"0";
                line += std::to_wstring(time.wSecond) + L".";
                if (time.wMilliseconds < 100) line += L"0";
                if (time.wMilliseconds < 10) line += L"0";
                line += std::to_wstring(time.wMilliseconds);
                line += L" pid=" + std::to_wstring(GetCurrentProcessId());
                line += L" event=";
                line.append(event);
                line += L" hwnd=" + std::to_wstring(
                    static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(window)));
                line += L" msg=" + std::to_wstring(message);
                line += L" wParam=" + std::to_wstring(static_cast<unsigned long long>(wParam));
                line += L" lParam=" + std::to_wstring(static_cast<long long>(lParam));
                line += L" result=" + std::to_wstring(apiResult != FALSE ? 1 : 0);
                line += L" error=" + std::to_wstring(errorCode);
                if (!detail.empty())
                {
                    line += L" detail=";
                    line.append(detail);
                }
                line += L"\r\n";
                (void)AppendUtf8(line);
            }
            catch (...)
            {
                // Diagnostic logging must never interfere with application behavior.
            }
        }
    }

    void SetUiDiagnosticLoggingEnabled(bool value) noexcept
    {
        enabled = value;
    }

    void LogUiDiagnostic(
        std::wstring_view event,
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        BOOL apiResult,
        DWORD errorCode,
        std::wstring_view detail) noexcept
    {
        LogDiagnostic(event, window, message, wParam, lParam,
            apiResult, errorCode, detail, false);
    }

    void LogPowerRequestDiagnostic(
        HWND window,
        bool hibernate,
        BOOL accepted,
        DWORD errorCode,
        std::wstring_view detail) noexcept
    {
        LogDiagnostic(L"power_request_finished", window, 0,
            hibernate ? 1 : 0, 0, accepted, errorCode, detail, true);
    }

    void LogRuntimeDiagnostic(
        std::wstring_view event,
        BOOL apiResult,
        DWORD errorCode,
        std::wstring_view detail) noexcept
    {
        LogDiagnostic(event, nullptr, 0, 0, 0,
            apiResult, errorCode, detail, true);
    }
}
