#include "BrowserVideoNativeMessagingHost.h"

#include "..\Core\BrowserVideoGuard.h"

#include <Windows.h>
#include <Shellapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "WindowsApp.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t MainWindowClass[] = L"ClassicDesktopScreensaver.Native.Owner";
        constexpr wchar_t ChromiumOrigin[] =
            L"chrome-extension://ejolmmdeidhhjkaiggencmjepmmcahja/";
        constexpr wchar_t FirefoxExtensionId[] =
            L"classic-desktop-screensaver-video-guard@local";
        constexpr DWORD MaximumMessageBytes = 16 * 1024;
        constexpr DWORD MessageTimeoutMs = 1000;

        bool IsAllowedExtensionCaller() noexcept
        {
            int argumentCount = 0;
            LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
            if (arguments == nullptr) return false;

            bool allowed = false;
            for (int index = 1; index < argumentCount; ++index)
            {
                if (_wcsicmp(arguments[index], ChromiumOrigin) == 0 ||
                    _wcsicmp(arguments[index], FirefoxExtensionId) == 0)
                {
                    allowed = true;
                    break;
                }
            }
            LocalFree(arguments);
            return allowed;
        }

        bool ReadExact(HANDLE input, void* buffer, DWORD count, bool allowCleanEof, bool& eof) noexcept
        {
            eof = false;
            auto* bytes = static_cast<unsigned char*>(buffer);
            DWORD offset = 0;
            while (offset < count)
            {
                DWORD read = 0;
                if (ReadFile(input, bytes + offset, count - offset, &read, nullptr) == FALSE)
                {
                    if (GetLastError() == ERROR_BROKEN_PIPE && allowCleanEof && offset == 0)
                    {
                        eof = true;
                        return true;
                    }
                    return false;
                }
                if (read == 0)
                {
                    if (allowCleanEof && offset == 0)
                    {
                        eof = true;
                        return true;
                    }
                    return false;
                }
                offset += read;
            }
            return true;
        }

        bool WriteExact(HANDLE output, const void* buffer, DWORD count) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(buffer);
            DWORD offset = 0;
            while (offset < count)
            {
                DWORD written = 0;
                if (WriteFile(output, bytes + offset, count - offset, &written, nullptr) == FALSE ||
                    written == 0)
                {
                    return false;
                }
                offset += written;
            }
            return true;
        }

        bool SendResponse(HANDLE output, const char* json) noexcept
        {
            const auto length = static_cast<std::uint32_t>(strlen(json));
            return WriteExact(output, &length, sizeof(length)) &&
                WriteExact(output, json, length);
        }

        bool ParseStatus(const std::string& utf8, BrowserVideoStatusMessage& status) noexcept
        {
            try
            {
                const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
                if (wideCount <= 0) return false;
                std::wstring wide(static_cast<std::size_t>(wideCount), L'\0');
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideCount) != wideCount)
                {
                    return false;
                }

                const auto object = winrt::Windows::Data::Json::JsonObject::Parse(
                    winrt::hstring(wide));
                if (object.Size() != 3 ||
                    object.GetNamedString(L"type") != L"video-state")
                {
                    return false;
                }
                const bool monitoring = object.GetNamedBoolean(L"monitoring");
                const double videoCount = object.GetNamedNumber(L"visibleVideoCount");
                if (!std::isfinite(videoCount) || videoCount < 0 ||
                    std::floor(videoCount) != videoCount ||
                    videoCount > BrowserVideoMaximumFrameCount)
                {
                    return false;
                }
                status = {
                    BrowserVideoProtocolVersion,
                    monitoring ? BrowserVideoMonitoringFlag : 0,
                    static_cast<std::uint32_t>(videoCount)
                };
                return IsValidBrowserVideoStatus(status);
            }
            catch (...)
            {
                return false;
            }
        }

        bool IsSameInteractiveSessionExecutable(DWORD processId) noexcept
        {
            if (processId == 0 || processId == GetCurrentProcessId()) return false;
            DWORD senderSession = 0;
            DWORD currentSession = 0;
            if (ProcessIdToSessionId(processId, &senderSession) == FALSE ||
                ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) == FALSE ||
                senderSession != currentSession)
            {
                return false;
            }

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (process == nullptr) return false;
            std::array<wchar_t, 32768> senderPath{};
            DWORD senderPathLength = static_cast<DWORD>(senderPath.size());
            const BOOL senderPathRead = QueryFullProcessImageNameW(
                process, 0, senderPath.data(), &senderPathLength);
            CloseHandle(process);
            if (senderPathRead == FALSE || senderPathLength == 0) return false;

            std::array<wchar_t, 32768> currentPath{};
            const DWORD currentPathLength = GetModuleFileNameW(
                nullptr, currentPath.data(), static_cast<DWORD>(currentPath.size()));
            if (currentPathLength == 0 || currentPathLength >= currentPath.size()) return false;
            return CompareStringOrdinal(senderPath.data(), static_cast<int>(senderPathLength),
                currentPath.data(), static_cast<int>(currentPathLength), TRUE) == CSTR_EQUAL;
        }

        const char* ForwardToApplication(
            HWND sourceWindow,
            const BrowserVideoStatusMessage& status) noexcept
        {
            const HWND applicationWindow = FindWindowW(MainWindowClass, nullptr);
            if (applicationWindow == nullptr) return "app-not-running";

            COPYDATASTRUCT copyData{};
            copyData.dwData = BrowserVideoCopyDataId;
            copyData.cbData = sizeof(status);
            copyData.lpData = const_cast<BrowserVideoStatusMessage*>(&status);
            DWORD_PTR accepted = FALSE;
            SetLastError(ERROR_SUCCESS);
            const LRESULT sent = SendMessageTimeoutW(applicationWindow, WM_COPYDATA,
                reinterpret_cast<WPARAM>(sourceWindow), reinterpret_cast<LPARAM>(&copyData),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, MessageTimeoutMs, &accepted);
            if (sent == 0) return "app-timeout";
            return accepted != FALSE ? nullptr : "app-rejected";
        }

        int RunMessageLoop() noexcept
        {
            const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
            const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            if (input == nullptr || input == INVALID_HANDLE_VALUE ||
                output == nullptr || output == INVALID_HANDLE_VALUE)
            {
                return 2;
            }

            HWND sourceWindow = CreateWindowExW(0, L"STATIC", L"", 0,
                0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (sourceWindow == nullptr) return 3;

            int exitCode = 0;
            while (true)
            {
                std::uint32_t messageLength = 0;
                bool eof = false;
                if (!ReadExact(input, &messageLength, sizeof(messageLength), true, eof))
                {
                    exitCode = 4;
                    break;
                }
                if (eof) break;
                if (messageLength == 0 || messageLength > MaximumMessageBytes)
                {
                    (void)SendResponse(output, "{\"ok\":false,\"reason\":\"invalid-size\"}");
                    exitCode = 5;
                    break;
                }

                std::string message(messageLength, '\0');
                if (!ReadExact(input, message.data(), messageLength, false, eof))
                {
                    exitCode = 6;
                    break;
                }
                BrowserVideoStatusMessage status{};
                if (!ParseStatus(message, status))
                {
                    if (!SendResponse(output, "{\"ok\":false,\"reason\":\"invalid-message\"}"))
                    {
                        exitCode = 7;
                        break;
                    }
                    continue;
                }

                const char* error = ForwardToApplication(sourceWindow, status);
                const char* response = error == nullptr
                    ? "{\"ok\":true}"
                    : std::strcmp(error, "app-not-running") == 0
                        ? "{\"ok\":false,\"reason\":\"app-not-running\"}"
                        : std::strcmp(error, "app-timeout") == 0
                            ? "{\"ok\":false,\"reason\":\"app-timeout\"}"
                            : "{\"ok\":false,\"reason\":\"app-rejected\"}";
                if (!SendResponse(output, response))
                {
                    exitCode = 8;
                    break;
                }
            }

            DestroyWindow(sourceWindow);
            return exitCode;
        }
    }

    int RunBrowserVideoNativeMessagingHost() noexcept
    {
        if (!IsAllowedExtensionCaller()) return 1;
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const int result = RunMessageLoop();
            winrt::uninit_apartment();
            return result;
        }
        catch (...)
        {
            return 9;
        }
    }
}
