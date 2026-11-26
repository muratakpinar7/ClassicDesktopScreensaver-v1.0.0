#pragma once

#include "GuardService.h"
#include "KeyboardHook.h"
#include "ScreenSaverService.h"
#include "SettingsWindow.h"
#include "StartupManager.h"
#include "StatusPopup.h"
#include "Win32PowerApi.h"
#include "UiDiagnosticLog.h"

#include "..\Core\AppSettings.h"
#include "..\Core\BrowserVideoGuard.h"
#include "..\Core\PowerService.h"
#include "..\Core\SettingsStore.h"
#include "..\Core\TrayCallback.h"
#include "..\Core\UiTestPolicy.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace classic_desktop_screensaver
{
    class NativeApplication final
    {
    public:
        NativeApplication() noexcept = default;
        ~NativeApplication();
        NativeApplication(const NativeApplication&) = delete;
        NativeApplication& operator=(const NativeApplication&) = delete;

        [[nodiscard]] bool Initialize(HINSTANCE instance, const wchar_t* commandLine, std::wstring& error) noexcept;
        [[nodiscard]] int Run() noexcept;
        [[nodiscard]] bool IsAlreadyRunning() const noexcept { return alreadyRunning_; }

    private:
        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static DWORD WINAPI ShowEventThread(void* context) noexcept;
        static DWORD WINAPI PowerWorker(void* context) noexcept;
        static DWORD WINAPI GuardWorker(void* context) noexcept;

        LRESULT ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        void Shutdown() noexcept;
        void ApplyRuntimeSettings();
        void ReassertWindowsScreenSaverSuppression(const wchar_t* reason) noexcept;
        void AbandonWindowsScreenSaverSuppression(const wchar_t* reason) noexcept;
        void RestoreWindowsScreenSaver() noexcept;
        [[nodiscard]] bool UpdateSettings(const AppSettings& settings, std::wstring& error);
        void ShowSettings();
        void RefreshHibernate();
        void UpdateTray();
        void ScheduleTrayRegistrationRetry() noexcept;
        void RemoveTray() noexcept;
        void ShowTrayMenu(const TrayNotification& notification);
        void NotifyIconEvent(const TrayNotification& notification);
        void HandleAction(ShortcutAction action);
        void StartPowerRequest(bool hibernate);
        void HandlePowerResult(const PowerRequestResult& result);
        bool StartScreenSaver(
            const std::wstring& path,
            bool showErrors,
            std::wstring* errorOut = nullptr);
        void ToggleAutoScreenSaver();
        void MarkActivity() noexcept;
        void InvalidateGuardQueryResults() noexcept;
        void OnTimer(UINT_PTR timer);
        void CheckIdle();
        void CheckHotCorner();
        void StartGuardQuery(bool saverDue, bool lockDue, bool checkMedia, bool checkFullscreen);
        void ApplyGuards(bool saverDue, bool lockDue, const GuardStatus& status);
        void ResetIdleBaseline(const wchar_t* reason);
        void ResetAfterResume(UINT resumeEvent);
        [[nodiscard]] bool IsBrowserVideoCurrentlyActive() const noexcept;
        void ShowError(std::wstring message);
        [[nodiscard]] std::vector<ShortcutAssignment> BuildShortcuts() const;
        [[nodiscard]] bool ValidateSettings(const AppSettings& settings, std::wstring& error) const;

        static AppSettings GetSettingsCallback(void* context);
        static bool UpdateSettingsCallback(void* context, const AppSettings& settings, std::wstring& error);
        static HibernateAvailability RefreshHibernateCallback(void* context);
        static std::wstring WindowsSaverPathCallback(void* context);
        static bool StartSaverCallback(void* context, const std::wstring& path, std::wstring& error);
        static void OpenSaverSettingsCallback(void* context);
        static void KeyboardSuspendedCallback(void* context, bool suspended);

        HINSTANCE instance_{};
        HWND window_{};
        HWND settingsWindow_{};
        UINT taskbarCreatedMessage_{};
        HANDLE mutex_{};
        HANDLE showEvent_{};
        HANDLE exitEvent_{};
        HANDLE stopEvent_{};
        HANDLE showThread_{};
        HANDLE powerThread_{};
        HANDLE guardThread_{};
        HPOWERNOTIFY sessionDisplayNotification_{};
        HICON activeIcon_{};
        HICON inactiveIcon_{};
        bool alreadyRunning_{};
        bool exitCommandOnly_{};
        bool ownsMutex_{};
        bool apartmentInitialized_{};
        bool sessionNotificationsRegistered_{};
        bool trayAdded_{};
        bool trayVersion4_{};
        bool trayRetryPending_{};
        bool trayMenuShowing_{};
        unsigned int trayRetryAttempts_{};
        DWORD trayAddError_{ ERROR_SUCCESS };
        bool safeUiTestMode_{};
        bool screenSaverSuppressionAbandoned_{};
        bool exitRequested_{};
        bool saverFiredThisIdle_{};
        bool lockAttemptedThisIdle_{};
        bool powerPending_{};
        bool sleepDelayed_{};
        bool guardPending_{};
        bool guardSaverDue_{};
        bool guardLockDue_{};
        bool guardMediaCheck_{};
        bool guardFullscreenCheck_{};
        std::uint32_t guardQueryRevision_{};
        std::uint32_t pendingGuardRevision_{};
        ULONGLONG lastSyntheticActivity_{};
        ULONGLONG lastIdleCheckTick_{};
        ULONGLONG lastIdleRuntimeDiagnostic_{};
        ULONGLONG lastSaverGuardDiagnostic_{};
        ULONGLONG lastLockGuardDiagnostic_{};
        ULONGLONG lastSleepRequest_{};
        ULONGLONG hotCornerEntered_{};
        bool hotCornerTriggered_{};
        bool lastInputFailureLogged_{};
        bool suspendNoticeReceived_{};
        BrowserVideoStatusMessage browserVideoStatus_{};
        ULONGLONG browserVideoReportReceivedAt_{};
        HibernateAvailability hibernateAvailability_{ HibernateAvailability::QueryFailed };
        UiTestPolicy uiPolicy_{ MakeUiTestPolicy(false) };
        AppSettings settings_{};
        SettingsHost settingsHost_{};
        std::unique_ptr<SettingsStore> store_;
        std::unique_ptr<KeyboardHook> keyboard_;
        ScreenSaverService screenSaver_;
        StartupManager startup_;
        std::unique_ptr<StatusPopup> popup_;
    };
}
