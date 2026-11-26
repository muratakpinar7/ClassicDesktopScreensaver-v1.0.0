#include "SettingsStore.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr std::size_t MaxSettingsFileBytes = 1024 * 1024;
        using Values = std::unordered_map<std::wstring, std::wstring>;

        std::wstring Trim(std::wstring_view value)
        {
            std::size_t first = 0;
            while (first < value.size() && std::iswspace(value[first]) != 0)
            {
                ++first;
            }

            std::size_t last = value.size();
            while (last > first && std::iswspace(value[last - 1]) != 0)
            {
                --last;
            }

            return std::wstring(value.substr(first, last - first));
        }

        std::wstring NormalizeKey(std::wstring value)
        {
            // All known section and setting names are ASCII. Lowercasing these
            // characters matches the C# ordinal-ignore-case key lookup.
            for (wchar_t& ch : value)
            {
                if (ch >= L'A' && ch <= L'Z')
                {
                    ch = static_cast<wchar_t>(ch - L'A' + L'a');
                }
            }
            return value;
        }

        bool Utf8ToWide(const std::string& input, std::wstring& output)
        {
            if (input.empty())
            {
                output.clear();
                return true;
            }

            const int required = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                static_cast<int>(input.size()), nullptr, 0);
            if (required <= 0)
            {
                return false;
            }

            output.resize(static_cast<std::size_t>(required));
            return MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                static_cast<int>(input.size()), output.data(), required) == required;
        }

        bool WideToUtf8(const std::wstring& input, std::string& output)
        {
            if (input.empty())
            {
                output.clear();
                return true;
            }

            const int required = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                return false;
            }

            output.resize(static_cast<std::size_t>(required));
            return WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                static_cast<int>(input.size()), output.data(), required,
                nullptr, nullptr) == required;
        }

        std::wstring Win32Error(DWORD code)
        {
            wchar_t* message = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                code,
                0,
                reinterpret_cast<LPWSTR>(&message),
                0,
                nullptr);

            std::wstring result = length != 0 && message != nullptr
                ? std::wstring(message, length)
                : L"Windows error " + std::to_wstring(code);
            if (message != nullptr)
            {
                LocalFree(message);
            }

            while (!result.empty() &&
                   (result.back() == L'\r' || result.back() == L'\n' ||
                    result.back() == L' ' || result.back() == L'.'))
            {
                result.pop_back();
            }
            return result;
        }

        bool ReadUtf8File(
            const std::filesystem::path& path,
            std::wstring& text,
            bool& exists,
            std::wstring& error)
        {
            HANDLE file = CreateFileW(
                path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                const DWORD code = GetLastError();
                if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                {
                    exists = false;
                    text.clear();
                    return true;
                }
                error = Win32Error(code);
                return false;
            }

            exists = true;
            LARGE_INTEGER fileSize{};
            if (!GetFileSizeEx(file, &fileSize) ||
                fileSize.QuadPart < 0 ||
                static_cast<unsigned long long>(fileSize.QuadPart) > MaxSettingsFileBytes)
            {
                const DWORD code = GetLastError();
                CloseHandle(file);
                error = fileSize.QuadPart > static_cast<LONGLONG>(MaxSettingsFileBytes)
                    ? L"Settings file is unexpectedly large."
                    : Win32Error(code);
                return false;
            }

            std::string bytes(static_cast<std::size_t>(fileSize.QuadPart), '\0');
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const DWORD remaining = static_cast<DWORD>(
                    std::min<std::size_t>(bytes.size() - offset, 64 * 1024));
                DWORD read = 0;
                if (!ReadFile(file, bytes.data() + offset, remaining, &read, nullptr) ||
                    read == 0)
                {
                    const DWORD code = GetLastError();
                    CloseHandle(file);
                    error = Win32Error(code);
                    return false;
                }
                offset += read;
            }
            CloseHandle(file);

            if (bytes.size() >= 3 &&
                static_cast<unsigned char>(bytes[0]) == 0xEF &&
                static_cast<unsigned char>(bytes[1]) == 0xBB &&
                static_cast<unsigned char>(bytes[2]) == 0xBF)
            {
                bytes.erase(0, 3);
            }

            if (!Utf8ToWide(bytes, text))
            {
                error = L"settings.ini is not valid UTF-8.";
                return false;
            }
            return true;
        }

        Values ParseValues(std::wstring_view text)
        {
            Values values;
            std::wstring section;
            std::size_t start = 0;

            while (start <= text.size())
            {
                const std::size_t newline = text.find(L'\n', start);
                const std::size_t end = newline == std::wstring_view::npos
                    ? text.size()
                    : newline;
                std::wstring line = Trim(text.substr(start, end - start));
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }

                if (!line.empty() && line[0] != L';' && line[0] != L'#')
                {
                    if (line.size() >= 2 && line.front() == L'[' && line.back() == L']')
                    {
                        section = Trim(std::wstring_view(line).substr(1, line.size() - 2));
                    }
                    else
                    {
                        const std::size_t equals = line.find(L'=');
                        if (equals != std::wstring::npos && equals > 0)
                        {
                            const std::wstring key = Trim(
                                std::wstring_view(line).substr(0, equals));
                            const std::wstring value = Trim(
                                std::wstring_view(line).substr(equals + 1));
                            values[NormalizeKey(section + L"." + key)] = value;
                        }
                    }
                }

                if (newline == std::wstring_view::npos)
                {
                    break;
                }
                start = newline + 1;
            }
            return values;
        }

        std::wstring Get(
            const Values& values,
            std::wstring_view key,
            std::wstring_view fallback)
        {
            const auto found = values.find(NormalizeKey(std::wstring(key)));
            return found == values.end() ? std::wstring(fallback) : found->second;
        }

        bool ParseBool(
            const Values& values,
            std::wstring_view key,
            bool fallback)
        {
            const std::wstring value = NormalizeKey(Get(values, key, L""));
            if (value == L"true")
            {
                return true;
            }
            if (value == L"false")
            {
                return false;
            }
            return fallback;
        }

        int ParseInt(
            const Values& values,
            std::wstring_view key,
            int fallback,
            int minimum,
            int maximum)
        {
            const std::wstring value = Get(values, key, L"");
            if (value.empty())
            {
                return fallback;
            }

            wchar_t* end = nullptr;
            const long long parsed = std::wcstoll(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != L'\0' ||
                parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max())
            {
                return fallback;
            }
            return std::clamp(static_cast<int>(parsed), minimum, maximum);
        }

        AppLanguage ParseLanguage(const Values& values)
        {
            const std::wstring value = NormalizeKey(
                Get(values, L"General.Language", L"Auto"));
            if (value == L"turkish")
            {
                return AppLanguage::Turkish;
            }
            if (value == L"english")
            {
                return AppLanguage::English;
            }
            return AppLanguage::Auto;
        }

        HotCorner ParseHotCorner(const Values& values)
        {
            const std::wstring value = NormalizeKey(
                Get(values, L"HotCorner.Corner", L"TopRight"));
            if (value == L"topleft")
            {
                return HotCorner::TopLeft;
            }
            if (value == L"bottomleft")
            {
                return HotCorner::BottomLeft;
            }
            if (value == L"bottomright")
            {
                return HotCorner::BottomRight;
            }
            return HotCorner::TopRight;
        }

        const wchar_t* LanguageName(AppLanguage language) noexcept
        {
            switch (language)
            {
            case AppLanguage::Turkish: return L"Turkish";
            case AppLanguage::English: return L"English";
            default: return L"Auto";
            }
        }

        const wchar_t* CornerName(HotCorner corner) noexcept
        {
            switch (corner)
            {
            case HotCorner::TopLeft: return L"TopLeft";
            case HotCorner::BottomLeft: return L"BottomLeft";
            case HotCorner::BottomRight: return L"BottomRight";
            default: return L"TopRight";
            }
        }

        void AppendBool(std::wostringstream& out, const wchar_t* key, bool value)
        {
            out << key << L'=' << (value ? L"true" : L"false") << L'\n';
        }
    }

    SettingsStore::SettingsStore(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    std::filesystem::path SettingsStore::DefaultPath()
    {
        std::wstring buffer(512, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size() - 1)
            {
                buffer.resize(length);
                return std::filesystem::path(buffer).parent_path() / L"settings.ini";
            }
            if (buffer.size() >= 32768)
            {
                return {};
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    const std::filesystem::path& SettingsStore::Path() const noexcept
    {
        return path_;
    }

    bool SettingsStore::Load(AppSettings& settings, std::wstring& error) const noexcept
    {
        error.clear();
        try
        {
            std::wstring text;
            bool exists = false;
            if (!ReadUtf8File(path_, text, exists, error))
            {
                return false;
            }
            if (!exists)
            {
                settings = AppSettings{};
                return true;
            }

            const Values values = ParseValues(text);
            AppSettings loaded;
            loaded.language = ParseLanguage(values);

            loaded.autoScreenSaverEnabled = ParseBool(
                values, L"General.AutoScreenSaverEnabled", loaded.autoScreenSaverEnabled);
            loaded.screenSaverIdleMinutes = ParseInt(
                values, L"General.ScreenSaverIdleMinutes",
                loaded.screenSaverIdleMinutes, 1, 1440);
            loaded.lockEnabled = ParseBool(
                values, L"General.LockEnabled", loaded.lockEnabled);
            loaded.lockIdleMinutes = ParseInt(
                values, L"General.LockIdleMinutes", loaded.lockIdleMinutes, 1, 1440);
            const std::wstring activeScreenSaverPath = Get(
                values, L"General.ScreenSaverPath", L"");
            loaded.screenSaverPath = Get(
                values, L"General.LastCustomScreenSaverPath", activeScreenSaverPath);
            loaded.useCustomScreenSaver = ParseBool(
                values, L"General.UseCustomScreenSaver", !activeScreenSaverPath.empty());
            if (loaded.useCustomScreenSaver && loaded.screenSaverPath.empty())
            {
                loaded.screenSaverPath = activeScreenSaverPath;
            }
            loaded.showStatusNotifications = ParseBool(
                values, L"General.ShowStatusNotifications", loaded.showStatusNotifications);
            loaded.showTrayStatusIcon = ParseBool(
                values, L"General.ShowTrayStatusIcon", loaded.showTrayStatusIcon);
            loaded.startWithWindows = ParseBool(
                values, L"General.StartWithWindows", loaded.startWithWindows);

            loaded.legacyBlockScreenSaverWhileMediaPlaying = ParseBool(
                values, L"Guards.BlockScreenSaverWhileMediaPlaying",
                loaded.legacyBlockScreenSaverWhileMediaPlaying);
            loaded.blockScreenSaverWhileFullscreen = ParseBool(
                values, L"Guards.BlockScreenSaverWhileFullscreen",
                loaded.blockScreenSaverWhileFullscreen);
            loaded.suspendLockWhileMediaOrPresentation = ParseBool(
                values, L"Guards.SuspendLockWhileMediaOrPresentation",
                loaded.suspendLockWhileMediaOrPresentation);

            loaded.hotCornerEnabled = ParseBool(
                values, L"HotCorner.Enabled", loaded.hotCornerEnabled);
            loaded.hotCorner = ParseHotCorner(values);
            loaded.hotCornerDelayMs = ParseInt(
                values, L"HotCorner.DelayMs", loaded.hotCornerDelayMs, 0, 5000);

            loaded.startScreenSaverHotkeyEnabled = ParseBool(
                values, L"Hotkeys.StartScreenSaverEnabled",
                loaded.startScreenSaverHotkeyEnabled);
            loaded.startScreenSaverHotkey = Get(
                values, L"Hotkeys.StartScreenSaver", loaded.startScreenSaverHotkey);
            loaded.toggleScreenSaverHotkeyEnabled = ParseBool(
                values, L"Hotkeys.ToggleScreenSaverEnabled",
                loaded.toggleScreenSaverHotkeyEnabled);
            loaded.toggleScreenSaverHotkey = Get(
                values, L"Hotkeys.ToggleScreenSaver", loaded.toggleScreenSaverHotkey);
            loaded.sleepHotkeyEnabled = ParseBool(
                values, L"Hotkeys.SleepEnabled", loaded.sleepHotkeyEnabled);
            loaded.sleepHotkey = Get(
                values, L"Hotkeys.Sleep", loaded.sleepHotkey);
            loaded.hibernateHotkeyEnabled = ParseBool(
                values, L"Hotkeys.HibernateEnabled", loaded.hibernateHotkeyEnabled);
            loaded.hibernateHotkey = Get(
                values, L"Hotkeys.Hibernate", loaded.hibernateHotkey);

            settings = std::move(loaded);
            return true;
        }
        catch (...)
        {
            error = L"Settings could not be read.";
            return false;
        }
    }

    bool SettingsStore::Save(
        const AppSettings& settings,
        std::wstring& error) const noexcept
    {
        error.clear();
        HANDLE file = INVALID_HANDLE_VALUE;
        std::filesystem::path temporaryPath;

        try
        {
            std::wostringstream content;
            content << L"; Classic Desktop Screensaver\n"
                    << L"; No application settings are written to the Windows Registry.\n\n";

            content << L"[General]\n"
                    << L"Language=" << LanguageName(settings.language) << L'\n';
            AppendBool(content, L"AutoScreenSaverEnabled", settings.autoScreenSaverEnabled);
            content << L"ScreenSaverIdleMinutes=" << settings.screenSaverIdleMinutes << L'\n';
            AppendBool(content, L"LockEnabled", settings.lockEnabled);
            content << L"LockIdleMinutes=" << settings.lockIdleMinutes << L'\n'
                    << L"ScreenSaverPath=" <<
                        (settings.useCustomScreenSaver ? settings.screenSaverPath : L"") << L'\n'
                    << L"LastCustomScreenSaverPath=" << settings.screenSaverPath << L'\n';
            AppendBool(content, L"UseCustomScreenSaver", settings.useCustomScreenSaver);
            AppendBool(content, L"ShowStatusNotifications", settings.showStatusNotifications);
            AppendBool(content, L"ShowTrayStatusIcon", settings.showTrayStatusIcon);
            AppendBool(content, L"StartWithWindows", settings.startWithWindows);

            content << L"\n[Guards]\n";
            content << L"; Legacy compatibility only; ignored by the Native automatic screen saver.\n";
            AppendBool(content, L"BlockScreenSaverWhileMediaPlaying",
                settings.legacyBlockScreenSaverWhileMediaPlaying);
            AppendBool(content, L"BlockScreenSaverWhileFullscreen",
                settings.blockScreenSaverWhileFullscreen);
            AppendBool(content, L"SuspendLockWhileMediaOrPresentation",
                settings.suspendLockWhileMediaOrPresentation);

            content << L"\n[HotCorner]\n";
            AppendBool(content, L"Enabled", settings.hotCornerEnabled);
            content << L"Corner=" << CornerName(settings.hotCorner) << L'\n'
                    << L"DelayMs=" << settings.hotCornerDelayMs << L'\n';

            content << L"\n[Hotkeys]\n";
            AppendBool(content, L"StartScreenSaverEnabled",
                settings.startScreenSaverHotkeyEnabled);
            content << L"StartScreenSaver=" << settings.startScreenSaverHotkey << L'\n';
            AppendBool(content, L"ToggleScreenSaverEnabled",
                settings.toggleScreenSaverHotkeyEnabled);
            content << L"ToggleScreenSaver=" << settings.toggleScreenSaverHotkey << L'\n';
            AppendBool(content, L"SleepEnabled", settings.sleepHotkeyEnabled);
            content << L"Sleep=" << settings.sleepHotkey << L'\n';
            AppendBool(content, L"HibernateEnabled", settings.hibernateHotkeyEnabled);
            content << L"Hibernate=" << settings.hibernateHotkey << L'\n';

            std::string bytes;
            if (!WideToUtf8(content.str(), bytes))
            {
                error = L"Settings contain text that cannot be encoded as UTF-8.";
                return false;
            }

            const std::filesystem::path parent = path_.parent_path();
            if (!parent.empty() && !CreateDirectoryW(parent.c_str(), nullptr))
            {
                const DWORD code = GetLastError();
                if (code != ERROR_ALREADY_EXISTS)
                {
                    error = Win32Error(code);
                    return false;
                }
            }

            temporaryPath = path_;
            temporaryPath += L".tmp";
            file = CreateFileW(
                temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                error = Win32Error(GetLastError());
                return false;
            }

            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const DWORD count = static_cast<DWORD>(
                    std::min<std::size_t>(bytes.size() - offset, 64 * 1024));
                DWORD written = 0;
                if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) ||
                    written != count)
                {
                    const DWORD code = GetLastError();
                    CloseHandle(file);
                    file = INVALID_HANDLE_VALUE;
                    DeleteFileW(temporaryPath.c_str());
                    error = Win32Error(code);
                    return false;
                }
                offset += written;
            }

            if (!FlushFileBuffers(file))
            {
                const DWORD code = GetLastError();
                CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                DeleteFileW(temporaryPath.c_str());
                error = Win32Error(code);
                return false;
            }
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;

            if (!MoveFileExW(
                temporaryPath.c_str(), path_.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                const DWORD code = GetLastError();
                DeleteFileW(temporaryPath.c_str());
                error = Win32Error(code);
                return false;
            }
            return true;
        }
        catch (...)
        {
            if (file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(file);
            }
            if (!temporaryPath.empty())
            {
                DeleteFileW(temporaryPath.c_str());
            }
            error = L"Settings could not be saved.";
            return false;
        }
    }
}
