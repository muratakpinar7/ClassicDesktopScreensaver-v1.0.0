#include "ScreenSaverService.h"

#include "Localization.h"

#include <Shellapi.h>
#include <Shlwapi.h>

#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        bool RegistryValueExists(
            HKEY root,
            const wchar_t* subkey,
            const wchar_t* valueName,
            bool& exists,
            DWORD& error) noexcept;

        class Win32ScreenSaverActivationApi final : public IScreenSaverActivationApi
        {
        public:
            bool Query(bool& active, DWORD& error) noexcept override
            {
                BOOL value = FALSE;
                SetLastError(ERROR_SUCCESS);
                const BOOL result = SystemParametersInfoW(
                    SPI_GETSCREENSAVEACTIVE, 0, &value, 0);
                error = result != FALSE ? ERROR_SUCCESS : GetLastError();
                if (result == FALSE) return false;
                active = value != FALSE;
                return true;
            }

            bool QuerySecure(bool& secure, DWORD& error) noexcept override
            {
                BOOL value = FALSE;
                SetLastError(ERROR_SUCCESS);
                const BOOL result = SystemParametersInfoW(
                    SPI_GETSCREENSAVESECURE, 0, &value, 0);
                error = result != FALSE ? ERROR_SUCCESS : GetLastError();
                if (result == FALSE) return false;
                secure = value != FALSE;
                return true;
            }

            bool QueryPolicyManaged(bool& managed, DWORD& error) noexcept override
            {
                managed = false;
                static constexpr const wchar_t* desktopPolicyPath =
                    L"Software\\Policies\\Microsoft\\Windows\\Control Panel\\Desktop";
                static constexpr const wchar_t* desktopPolicyValues[] = {
                    L"ScreenSaveActive", L"ScreenSaverIsSecure",
                    L"ScreenSaveTimeOut", L"SCRNSAVE.EXE"
                };

                const auto checkValues = [&](HKEY root, const wchar_t* subkey) noexcept -> bool
                {
                    HKEY key{};
                    const LSTATUS openStatus = RegOpenKeyExW(
                        root, subkey, 0, KEY_QUERY_VALUE, &key);
                    if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND)
                    {
                        return true;
                    }
                    if (openStatus != ERROR_SUCCESS)
                    {
                        error = static_cast<DWORD>(openStatus);
                        return false;
                    }

                    for (const wchar_t* valueName : desktopPolicyValues)
                    {
                        const LSTATUS queryStatus = RegQueryValueExW(
                            key, valueName, nullptr, nullptr, nullptr, nullptr);
                        if (queryStatus == ERROR_SUCCESS)
                        {
                            managed = true;
                            RegCloseKey(key);
                            return true;
                        }
                        if (queryStatus != ERROR_FILE_NOT_FOUND)
                        {
                            error = static_cast<DWORD>(queryStatus);
                            RegCloseKey(key);
                            return false;
                        }
                    }
                    RegCloseKey(key);
                    return true;
                };

                if (!checkValues(HKEY_CURRENT_USER, desktopPolicyPath) ||
                    !checkValues(HKEY_LOCAL_MACHINE, desktopPolicyPath))
                {
                    return false;
                }

                bool restrictedSettingsPage = false;
                if (!RegistryValueExists(
                    HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                    L"NoDispScrSavPage", restrictedSettingsPage, error))
                {
                    return false;
                }
                managed = managed || restrictedSettingsPage;
                error = ERROR_SUCCESS;
                return true;
            }

            bool Set(bool active, DWORD& error) noexcept override
            {
                SetLastError(ERROR_SUCCESS);
                const BOOL result = SystemParametersInfoW(
                    SPI_SETSCREENSAVEACTIVE, active ? TRUE : FALSE, nullptr, 0);
                error = result != FALSE ? ERROR_SUCCESS : GetLastError();
                return result != FALSE;
            }
        };

        bool RegistryValueExists(
            HKEY root,
            const wchar_t* subkey,
            const wchar_t* valueName,
            bool& exists,
            DWORD& error) noexcept
        {
            exists = false;
            HKEY key{};
            const LSTATUS openStatus = RegOpenKeyExW(
                root, subkey, 0, KEY_QUERY_VALUE, &key);
            if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND)
            {
                error = ERROR_SUCCESS;
                return true;
            }
            if (openStatus != ERROR_SUCCESS)
            {
                error = static_cast<DWORD>(openStatus);
                return false;
            }

            const LSTATUS queryStatus = RegQueryValueExW(
                key, valueName, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(key);
            if (queryStatus == ERROR_SUCCESS)
            {
                exists = true;
                error = ERROR_SUCCESS;
                return true;
            }
            if (queryStatus == ERROR_FILE_NOT_FOUND)
            {
                error = ERROR_SUCCESS;
                return true;
            }
            error = static_cast<DWORD>(queryStatus);
            return false;
        }

        IScreenSaverActivationApi& ActivationApi(IScreenSaverActivationApi* injected)
        {
            if (injected != nullptr) return *injected;
            static Win32ScreenSaverActivationApi api;
            return api;
        }

        std::wstring ExpandAndUnquote(std::wstring value)
        {
            const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
            if (needed > 0)
            {
                std::vector<wchar_t> expanded(needed);
                const DWORD written = ExpandEnvironmentStringsW(
                    value.c_str(), expanded.data(), needed);
                if (written > 0 && written <= needed)
                {
                    value.assign(expanded.data());
                }
            }

            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }

        std::wstring ErrorText(DWORD code)
        {
            wchar_t* buffer = nullptr;
            const DWORD count = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
            std::wstring result = count != 0 && buffer != nullptr
                ? std::wstring(buffer, count)
                : L"Windows error " + std::to_wstring(code);
            if (buffer != nullptr)
            {
                LocalFree(buffer);
            }
            while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n'))
            {
                result.pop_back();
            }
            return result;
        }
    }

    ScreenSaverService::~ScreenSaverService()
    {
        if (activationLease_.Acquired())
        {
            (void)RestoreWindowsScreenSaver();
        }
        if (process_ != nullptr)
        {
            CloseHandle(process_);
        }
    }

    std::wstring ScreenSaverService::ReadWindowsScreenSaverPath() const
    {
        wchar_t value[32768]{};
        DWORD size = sizeof(value);
        const LSTATUS status = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Control Panel\\Desktop",
            L"SCRNSAVE.EXE",
            RRF_RT_REG_SZ,
            nullptr,
            value,
            &size);
        if (status != ERROR_SUCCESS || value[0] == L'\0')
        {
            return {};
        }
        return ExpandAndUnquote(value);
    }

    WindowsScreenSaverSnapshot ScreenSaverService::ReadWindowsScreenSaverSnapshot() const
    {
        BOOL active{};
        SetLastError(ERROR_SUCCESS);
        const bool activeRead = SystemParametersInfoW(
            SPI_GETSCREENSAVEACTIVE, 0, &active, 0) != FALSE;
        const DWORD activeError = activeRead ? ERROR_SUCCESS : GetLastError();

        UINT timeoutSeconds{};
        SetLastError(ERROR_SUCCESS);
        const bool timeoutRead = SystemParametersInfoW(
            SPI_GETSCREENSAVETIMEOUT, 0, &timeoutSeconds, 0) != FALSE;
        const DWORD timeoutError = timeoutRead ? ERROR_SUCCESS : GetLastError();

        BOOL running{};
        SetLastError(ERROR_SUCCESS);
        const bool runningRead = SystemParametersInfoW(
            SPI_GETSCREENSAVERRUNNING, 0, &running, 0) != FALSE;
        const DWORD runningError = runningRead ? ERROR_SUCCESS : GetLastError();

        return {
            .activeQuerySucceeded = activeRead,
            .active = active != FALSE,
            .activeQueryError = activeError,
            .timeoutQuerySucceeded = timeoutRead,
            .timeoutSeconds = timeoutSeconds,
            .timeoutQueryError = timeoutError,
            .runningQuerySucceeded = runningRead,
            .running = running != FALSE,
            .runningQueryError = runningError,
            .selectedSaverConfigured = !ReadWindowsScreenSaverPath().empty()
        };
    }

    ScreenSaverLeaseResult ScreenSaverService::SuppressWindowsScreenSaver()
    {
        return activationLease_.Begin(ActivationApi(activationApi_));
    }

    ScreenSaverLeaseResult ScreenSaverService::ReassertWindowsScreenSaverSuppression()
    {
        return activationLease_.Reassert(ActivationApi(activationApi_));
    }

    ScreenSaverLeaseResult ScreenSaverService::RestoreWindowsScreenSaver()
    {
        return activationLease_.Restore(ActivationApi(activationApi_));
    }

    ScreenSaverLeaseResult ScreenSaverService::AbandonWindowsScreenSaverSuppression() noexcept
    {
        activationLease_.Abandon();
        return { true, true, false, false, ERROR_SUCCESS, false, false };
    }

    bool ScreenSaverService::Start(
        const std::wstring& customPath,
        std::wstring& error,
        AppLanguage language,
        DWORD* failureCode,
        ScreenSaverLaunchDetails* launchDetails)
    {
        error.clear();
        if (failureCode != nullptr) *failureCode = ERROR_SUCCESS;
        if (launchDetails != nullptr) *launchDetails = {};
        if (process_ != nullptr)
        {
            const DWORD state = WaitForSingleObject(process_, 0);
            if (state == WAIT_TIMEOUT)
            {
                if (launchDetails != nullptr)
                {
                    launchDetails->processId = processId_;
                    launchDetails->processHandleAvailable = true;
                    launchDetails->reusedExistingProcess = true;
                }
                return true;
            }
            if (state == WAIT_FAILED)
            {
                const DWORD code = GetLastError();
                if (failureCode != nullptr) *failureCode = code;
                error = std::wstring(Text(language, L"SaverStartFailed")) + L": " + ErrorText(code);
                return false;
            }
            CloseHandle(process_);
            process_ = nullptr;
            processId_ = 0;
            processStartedTick_ = 0;
            processRunningReported_ = false;
        }

        std::wstring path = customPath.empty()
            ? ReadWindowsScreenSaverPath()
            : ExpandAndUnquote(customPath);
        if (path.empty())
        {
            error.assign(Text(language, L"NoSaverSelected"));
            if (failureCode != nullptr) *failureCode = ERROR_NOT_FOUND;
            return false;
        }

        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (failureCode != nullptr)
            {
                *failureCode = attributes == INVALID_FILE_ATTRIBUTES
                    ? GetLastError() : ERROR_DIRECTORY;
            }
            error = std::wstring(Text(language, L"SaverFileNotFound")) + L"\n" + path;
            return false;
        }

        // A .scr file is an executable. Launch it directly instead of routing
        // through the file association (normally "%1" /S), which can append
        // a second /s argument and whose success only confirms shell dispatch.
        std::wstring commandLine = L"\"" + path + L"\" /s";
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        std::wstring directory;
        const size_t separator = path.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
            const size_t directoryLength =
                separator == 2 && path.size() >= 3 && path[1] == L':'
                    ? 3 : separator;
            directory = path.substr(0, directoryLength);
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNORMAL;
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(
                path.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE, 0,
                nullptr, directory.empty() ? nullptr : directory.c_str(),
                &startup, &processInfo))
        {
            const DWORD code = GetLastError();
            if (failureCode != nullptr) *failureCode = code;
            error = std::wstring(Text(language, L"SaverStartFailed")) + L": " + ErrorText(code);
            return false;
        }

        CloseHandle(processInfo.hThread);
        process_ = processInfo.hProcess;
        processId_ = processInfo.dwProcessId;
        processStartedTick_ = GetTickCount64();
        processRunningReported_ = false;
        if (launchDetails != nullptr)
        {
            launchDetails->processId = processId_;
            launchDetails->processHandleAvailable = process_ != nullptr;
        }
        return true;
    }

    ScreenSaverProcessObservation ScreenSaverService::ObserveProcess() noexcept
    {
        if (process_ == nullptr) return {};

        const DWORD wait = WaitForSingleObject(process_, 0);
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = processStartedTick_ != 0 && now >= processStartedTick_
            ? now - processStartedTick_ : 0;
        if (wait == WAIT_TIMEOUT)
        {
            if (processRunningReported_) return {};
            processRunningReported_ = true;
            return { ScreenSaverProcessState::Running, processId_, 0, ERROR_SUCCESS, elapsed };
        }

        if (wait == WAIT_OBJECT_0)
        {
            const DWORD completedProcessId = processId_;
            DWORD exitCode{};
            if (!GetExitCodeProcess(process_, &exitCode))
            {
                const DWORD code = GetLastError();
                CloseHandle(process_);
                process_ = nullptr;
                processId_ = 0;
                processStartedTick_ = 0;
                processRunningReported_ = false;
                return { ScreenSaverProcessState::Failed, completedProcessId, 0, code, elapsed };
            }
            CloseHandle(process_);
            process_ = nullptr;
            processId_ = 0;
            processStartedTick_ = 0;
            processRunningReported_ = false;
            return { ScreenSaverProcessState::Exited, completedProcessId, exitCode,
                ERROR_SUCCESS, elapsed };
        }

        const DWORD failedProcessId = processId_;
        const DWORD code = wait == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
        CloseHandle(process_);
        process_ = nullptr;
        processId_ = 0;
        processStartedTick_ = 0;
        processRunningReported_ = false;
        return { ScreenSaverProcessState::Failed, failedProcessId, 0, code, elapsed };
    }

    void ScreenSaverService::OpenWindowsSettings() const noexcept
    {
        ShellExecuteW(nullptr, L"open", L"control.exe", L"desk.cpl,,@screensaver", nullptr, SW_SHOWNORMAL);
    }
}
