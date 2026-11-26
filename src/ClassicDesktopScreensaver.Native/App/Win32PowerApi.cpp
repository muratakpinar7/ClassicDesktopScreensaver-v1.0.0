#include "Win32PowerApi.h"

#include <Windows.h>
#include <PowrProf.h>

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "User32.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        class UniqueHandle final
        {
        public:
            explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
            ~UniqueHandle()
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

        private:
            HANDLE handle_{};
        };

        bool EnableShutdownPrivilege(
            HANDLE token,
            TOKEN_PRIVILEGES& previousState,
            DWORD& errorCode,
            PowerFailureStage& failureStage) noexcept
        {
            LUID privilegeId{};
            if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privilegeId))
            {
                errorCode = GetLastError();
                failureStage = PowerFailureStage::LookupShutdownPrivilege;
                return false;
            }

            TOKEN_PRIVILEGES requested{};
            requested.PrivilegeCount = 1;
            requested.Privileges[0].Luid = privilegeId;
            requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

            SetLastError(ERROR_SUCCESS);
            // Windows requires ReturnLength when PreviousState is requested.
            DWORD previousStateLength = 0;
            if (!AdjustTokenPrivileges(
                token, FALSE, &requested, sizeof(previousState),
                &previousState, &previousStateLength))
            {
                errorCode = GetLastError();
                failureStage = PowerFailureStage::AdjustShutdownPrivilege;
                return false;
            }

            const DWORD adjustmentResult = GetLastError();
            if (adjustmentResult == ERROR_NOT_ALL_ASSIGNED)
            {
                errorCode = adjustmentResult;
                failureStage = PowerFailureStage::AdjustShutdownPrivilege;
                return false;
            }

            errorCode = ERROR_SUCCESS;
            failureStage = PowerFailureStage::None;
            return true;
        }

        void RestorePrivileges(
            HANDLE token,
            TOKEN_PRIVILEGES& previousState) noexcept
        {
            if (previousState.PrivilegeCount == 0)
            {
                return;
            }

            AdjustTokenPrivileges(
                token, FALSE, &previousState, 0, nullptr, nullptr);
        }
    }

    PowerCapabilitiesSnapshot Win32PowerApi::QueryCapabilities() noexcept
    {
        SYSTEM_POWER_CAPABILITIES capabilities{};
        if (GetPwrCapabilities(&capabilities) == FALSE)
        {
            return {};
        }

        return {
            .querySucceeded = true,
            .modernStandby = capabilities.AoAc != FALSE,
            .systemS1 = capabilities.SystemS1 != FALSE,
            .systemS2 = capabilities.SystemS2 != FALSE,
            .systemS3 = capabilities.SystemS3 != FALSE,
            .systemS4 = capabilities.SystemS4 != FALSE,
            .hiberFilePresent = capabilities.HiberFilePresent != FALSE
        };
    }

    bool Win32PowerApi::RequestDisplayOff(
        std::uint32_t& errorCode,
        PowerFailureStage& failureStage) noexcept
    {
        constexpr UINT message = WM_SYSCOMMAND;
        constexpr WPARAM command = SC_MONITORPOWER;
        constexpr LPARAM powerOff = 2;
        constexpr UINT flags = SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT;
        constexpr UINT timeoutMilliseconds = 100;

        DWORD_PTR messageResult = 0;
        const LRESULT apiResult = SendMessageTimeoutW(
            HWND_BROADCAST,
            message,
            command,
            powerOff,
            flags,
            timeoutMilliseconds,
            &messageResult);
        if (apiResult != 0)
        {
            errorCode = ERROR_SUCCESS;
            failureStage = PowerFailureStage::None;
            return true;
        }

        errorCode = GetLastError();
        failureStage = PowerFailureStage::DisplayOffRequest;
        return false;
    }

    bool Win32PowerApi::RequestSuspend(
        bool hibernate,
        std::uint32_t& errorCode,
        PowerFailureStage& failureStage) noexcept
    {
        failureStage = PowerFailureStage::OpenProcessToken;
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &rawToken))
        {
            errorCode = GetLastError();
            return false;
        }
        const UniqueHandle token(rawToken);

        TOKEN_PRIVILEGES previousState{};
        DWORD privilegeError = ERROR_SUCCESS;
        if (!EnableShutdownPrivilege(token.Get(), previousState, privilegeError, failureStage))
        {
            RestorePrivileges(token.Get(), previousState);
            errorCode = static_cast<std::uint32_t>(privilegeError);
            return false;
        }

        failureStage = PowerFailureStage::SetSuspendState;
        SetLastError(ERROR_SUCCESS);
        const BOOLEAN accepted = SetSuspendState(
            hibernate ? TRUE : FALSE,
            FALSE,
            FALSE);
        const DWORD callError = accepted != FALSE ? ERROR_SUCCESS : GetLastError();

        RestorePrivileges(token.Get(), previousState);

        errorCode = static_cast<std::uint32_t>(callError);
        failureStage = accepted != FALSE
            ? PowerFailureStage::None
            : PowerFailureStage::SetSuspendState;
        return accepted != FALSE;
    }
}
