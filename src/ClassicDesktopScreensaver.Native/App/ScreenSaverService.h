#pragma once

#include <Windows.h>
#include "..\Core\AppSettings.h"
#include "..\Core\ScreenSaverActivationLease.h"

#include <string>

namespace classic_desktop_screensaver
{
    struct ScreenSaverLaunchDetails
    {
        DWORD processId{};
        bool processHandleAvailable{};
        bool reusedExistingProcess{};
    };

    enum class ScreenSaverProcessState
    {
        None,
        Running,
        Exited,
        Failed
    };

    struct ScreenSaverProcessObservation
    {
        ScreenSaverProcessState state{ ScreenSaverProcessState::None };
        DWORD processId{};
        DWORD exitCode{};
        DWORD error{};
        ULONGLONG elapsedMilliseconds{};
    };

    struct WindowsScreenSaverSnapshot
    {
        bool activeQuerySucceeded{};
        bool active{};
        DWORD activeQueryError{};
        bool timeoutQuerySucceeded{};
        UINT timeoutSeconds{};
        DWORD timeoutQueryError{};
        bool runningQuerySucceeded{};
        bool running{};
        DWORD runningQueryError{};
        bool selectedSaverConfigured{};
    };

    class ScreenSaverService final
    {
    public:
        explicit ScreenSaverService(IScreenSaverActivationApi* activationApi = nullptr) noexcept
            : activationApi_(activationApi) {}
        ~ScreenSaverService();
        ScreenSaverService(const ScreenSaverService&) = delete;
        ScreenSaverService& operator=(const ScreenSaverService&) = delete;

        [[nodiscard]] std::wstring ReadWindowsScreenSaverPath() const;
        [[nodiscard]] WindowsScreenSaverSnapshot ReadWindowsScreenSaverSnapshot() const;
        [[nodiscard]] ScreenSaverLeaseResult SuppressWindowsScreenSaver();
        [[nodiscard]] ScreenSaverLeaseResult ReassertWindowsScreenSaverSuppression();
        [[nodiscard]] ScreenSaverLeaseResult RestoreWindowsScreenSaver();
        [[nodiscard]] ScreenSaverLeaseResult AbandonWindowsScreenSaverSuppression() noexcept;
        [[nodiscard]] bool SuppressionAcquired() const noexcept { return activationLease_.Acquired(); }
        [[nodiscard]] bool Start(
            const std::wstring& customPath,
            std::wstring& error,
            AppLanguage language = AppLanguage::Auto,
            DWORD* failureCode = nullptr,
            ScreenSaverLaunchDetails* launchDetails = nullptr);
        [[nodiscard]] ScreenSaverProcessObservation ObserveProcess() noexcept;
        void OpenWindowsSettings() const noexcept;

    private:
        HANDLE process_{};
        DWORD processId_{};
        ULONGLONG processStartedTick_{};
        bool processRunningReported_{};
        IScreenSaverActivationApi* activationApi_{};
        ScreenSaverActivationLease activationLease_;
    };
}
