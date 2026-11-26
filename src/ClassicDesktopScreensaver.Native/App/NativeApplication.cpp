#include "NativeApplication.h"

#include "Localization.h"
#include "BrowserVideoNativeMessagingHost.h"
#include "NotificationService.h"
#include "resource.h"
#include "TrayMenuModel.h"
#include "TrayMenuPlacement.h"
#include "TrayMenuVisuals.h"
#include "UiDiagnosticLog.h"

#include "..\Core\HotkeyGesture.h"
#include "..\Core\LaunchBehavior.h"
#include "..\Core\RuntimePolicy.h"

#include <CommCtrl.h>
#include <Shellapi.h>
#include <ShlObj.h>
#include <WtsApi32.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Wtsapi32.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t MainWindowClass[] = L"ClassicDesktopScreensaver.Native.Owner";
        constexpr wchar_t MutexName[] = L"Local\\ClassicDesktopScreensaver_SingleInstance";
        constexpr wchar_t ShowEventName[] = L"Local\\ClassicDesktopScreensaver_ShowSettings";
        constexpr wchar_t ExitEventName[] = L"Local\\ClassicDesktopScreensaver_Exit";
        constexpr UINT MessageTray = WM_APP + 0x30;
        constexpr UINT MessageShortcut = WM_APP + 0x31;
        constexpr UINT MessageShowSettings = WM_APP + 0x32;
        constexpr UINT MessagePowerResult = WM_APP + 0x33;
        constexpr UINT MessageGuardResult = WM_APP + 0x34;
        constexpr UINT MessageExit = WM_APP + 0x35;
        constexpr UINT_PTR TimerIdle = 1;
        constexpr UINT_PTR TimerHotCorner = 2;
        constexpr UINT_PTR TimerSleepDelay = 3;
        constexpr UINT_PTR TimerTrayRegistrationRetry = 4;
        constexpr UINT TrayIconId = 1;
        static_assert(TrayIconId <= 0xFFFFu);
        constexpr DWORD WaitTimeout = 2000;
        constexpr ULONGLONG IdleTimerResumeGapThresholdMilliseconds = 5000;
        constexpr GUID SessionDisplayStatusGuid{
            0x2b84c20e, 0xad23, 0x4ddf, { 0x93, 0xdb, 0x05, 0xff, 0xbd, 0x7e, 0xfc, 0xa5 }
        };

        struct PowerWorkerArguments
        {
            HWND window{};
            bool hibernate{};
        };

        struct GuardWorkerArguments
        {
            HWND window{};
            std::uint32_t revision{};
            bool saverDue{};
            bool lockDue{};
            bool checkMedia{};
            bool checkFullscreen{};
        };

        struct GuardWorkerResult
        {
            std::uint32_t revision{};
            bool saverDue{};
            bool lockDue{};
            GuardStatus status{};
        };

        const wchar_t* MediaQueryStateName(MediaQueryState state) noexcept
        {
            switch (state)
            {
            case MediaQueryState::Succeeded: return L"succeeded";
            case MediaQueryState::TimedOut: return L"timed_out";
            case MediaQueryState::Failed: return L"failed";
            default: return L"not_requested";
            }
        }

        std::wstring GuardStatusDiagnostic(const GuardStatus& status)
        {
            return L"video_playing=" + std::to_wstring(status.videoPlaying ? 1 : 0) +
                L" media_query=" + MediaQueryStateName(status.mediaQueryState) +
                L" media_session_available=" + std::to_wstring(status.mediaSessionAvailable ? 1 : 0) +
                L" music_playing=" + std::to_wstring(status.musicPlaying ? 1 : 0) +
                L" unknown_type_playing=" + std::to_wstring(status.unknownTypePlaying ? 1 : 0) +
                L" media_query_hresult=" + std::to_wstring(status.mediaQueryHresult) +
                L" fullscreen=" + std::to_wstring(status.fullScreenApplication ? 1 : 0) +
                L" presentation=" + std::to_wstring(status.presentationMode ? 1 : 0);
        }

        bool HasCommandLineSwitch(const wchar_t* commandLine, const wchar_t* expected) noexcept
        {
            if (commandLine == nullptr || expected == nullptr || *expected == L'\0') return false;
            const std::size_t expectedLength = wcslen(expected);
            const wchar_t* cursor = commandLine;
            while ((cursor = wcsstr(cursor, expected)) != nullptr)
            {
                const bool leftBoundary = cursor == commandLine ||
                    iswspace(cursor[-1]) || cursor[-1] == L'"';
                const wchar_t next = cursor[expectedLength];
                const bool rightBoundary = next == L'\0' || iswspace(next) || next == L'"';
                if (leftBoundary && rightBoundary) return true;
                ++cursor;
            }
            return false;
        }

        bool IsSettingsCommandLine(const wchar_t* commandLine) noexcept
        {
            return HasCommandLineSwitch(commandLine, L"--settings") ||
                HasCommandLineSwitch(commandLine, L"--open-settings");
        }

        bool IsTrustedBrowserHostWindow(HWND senderWindow) noexcept
        {
            if (senderWindow == nullptr || IsWindow(senderWindow) == FALSE) return false;
            DWORD senderProcessId = 0;
            if (GetWindowThreadProcessId(senderWindow, &senderProcessId) == 0 ||
                senderProcessId == 0 || senderProcessId == GetCurrentProcessId())
            {
                return false;
            }

            DWORD senderSession = 0;
            DWORD currentSession = 0;
            if (ProcessIdToSessionId(senderProcessId, &senderSession) == FALSE ||
                ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) == FALSE ||
                senderSession != currentSession)
            {
                return false;
            }

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, senderProcessId);
            if (process == nullptr) return false;
            std::array<wchar_t, 32768> senderPath{};
            DWORD senderPathLength = static_cast<DWORD>(senderPath.size());
            const BOOL senderPathRead = QueryFullProcessImageNameW(
                process, 0, senderPath.data(), &senderPathLength);
            CloseHandle(process);
            if (senderPathRead == FALSE || senderPathLength == 0) return false;

            std::array<wchar_t, 32768> currentPath{};
            SetLastError(ERROR_SUCCESS);
            const DWORD currentPathLength = GetModuleFileNameW(
                nullptr, currentPath.data(), static_cast<DWORD>(currentPath.size()));
            if (currentPathLength == 0 || currentPathLength >= currentPath.size()) return false;
            return CompareStringOrdinal(senderPath.data(), static_cast<int>(senderPathLength),
                currentPath.data(), static_cast<int>(currentPathLength), TRUE) == CSTR_EQUAL;
        }

        std::wstring_view PowerFailureStageName(PowerFailureStage stage) noexcept
        {
            switch (stage)
            {
            case PowerFailureStage::DisplayOffRequest: return L"SendMessageTimeoutW";
            case PowerFailureStage::OpenProcessToken: return L"OpenProcessToken";
            case PowerFailureStage::LookupShutdownPrivilege: return L"LookupPrivilegeValueW";
            case PowerFailureStage::AdjustShutdownPrivilege: return L"AdjustTokenPrivileges";
            case PowerFailureStage::SetSuspendState: return L"SetSuspendState";
            case PowerFailureStage::None: default: return {};
            }
        }

        std::wstring PowerErrorText(const PowerRequestResult& result, AppLanguage language)
        {
            switch (result.status)
            {
            case PowerRequestStatus::CapabilitiesUnavailable:
                return std::wstring(Text(language, L"CapabilitiesUnavailable"));
            case PowerRequestStatus::SleepUnavailable:
                return std::wstring(Text(language, L"SleepCapabilitiesUnavailable"));
            case PowerRequestStatus::HibernateUnavailable:
                return std::wstring(Text(language, L"HibernateUnavailable"));
            case PowerRequestStatus::RequestFailed:
            {
                std::wstring error(Text(language, L"PowerFailed"));
                const std::wstring_view stage = PowerFailureStageName(result.failureStage);
                if (!stage.empty() || result.errorCode != 0)
                {
                    error += L" (";
                    if (!stage.empty())
                    {
                        error.append(stage);
                        if (result.errorCode != 0) error += L", ";
                    }
                    if (result.errorCode != 0) error += std::to_wstring(result.errorCode);
                    error += L")";
                }
                return error;
            }
            case PowerRequestStatus::RequestAccepted:
            default:
                return {};
            }
        }
    }

    NativeApplication::~NativeApplication()
    {
        Shutdown();
    }

    bool NativeApplication::Initialize(
        HINSTANCE instance,
        const wchar_t* commandLine,
        std::wstring& error) noexcept
    {
        error.clear();
        if (HasCommandLineSwitch(commandLine, L"--exit"))
        {
            exitCommandOnly_ = true;
            HANDLE exitEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, ExitEventName);
            if (exitEvent != nullptr)
            {
                SetEvent(exitEvent);
                CloseHandle(exitEvent);
            }
            return true;
        }

        instance_ = instance;
        safeUiTestMode_ = HasCommandLineSwitch(commandLine, L"--ui-test-safe");
        uiPolicy_ = MakeUiTestPolicy(safeUiTestMode_);
        SetUiDiagnosticLoggingEnabled(safeUiTestMode_);
        LogUiDiagnostic(L"initialize_begin", nullptr, 0, 0, 0, TRUE, ERROR_SUCCESS,
            safeUiTestMode_ ? L"mode=ui-test-safe arch_bits=64" : L"mode=normal");
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            apartmentInitialized_ = true;
            INITCOMMONCONTROLSEX controls{ sizeof(controls) };
            controls.dwICC = ICC_TAB_CLASSES;
            if (InitCommonControlsEx(&controls) == FALSE)
            {
                error = L"Common Windows controls could not be initialized.";
                return false;
            }

            SetLastError(ERROR_SUCCESS);
            mutex_ = CreateMutexW(nullptr, TRUE, MutexName);
            if (mutex_ == nullptr)
            {
                const DWORD errorCode = GetLastError();
                LogUiDiagnostic(L"single_instance_mutex_failed", nullptr, 0, 0, 0,
                    FALSE, errorCode);
                error = L"The application instance could not be coordinated.";
                return false;
            }
            const DWORD mutexStatus = GetLastError();
            if (mutexStatus == ERROR_ALREADY_EXISTS)
            {
                if (safeUiTestMode_)
                {
                    LogUiDiagnostic(L"safe_ui_test_aborted_existing_instance", nullptr, 0, 0, 0,
                        FALSE, ERROR_ALREADY_EXISTS);
                    error = L"Close the existing Classic Desktop Screensaver instance before starting the safe UI test.";
                    return false;
                }

                alreadyRunning_ = true;
                LogUiDiagnostic(L"single_instance_already_running", nullptr, 0, 0, 0,
                    TRUE, ERROR_ALREADY_EXISTS);
                HANDLE existingEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, ShowEventName);
                if (existingEvent != nullptr)
                {
                    SetEvent(existingEvent);
                    CloseHandle(existingEvent);
                }
                return true;
            }
            ownsMutex_ = true;

            showEvent_ = CreateEventW(nullptr, FALSE, FALSE, ShowEventName);
            exitEvent_ = CreateEventW(nullptr, FALSE, FALSE, ExitEventName);
            stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (showEvent_ == nullptr || exitEvent_ == nullptr || stopEvent_ == nullptr)
            {
                error = L"The application event handles could not be created.";
                return false;
            }

            store_ = std::make_unique<SettingsStore>(SettingsStore::DefaultPath());
            if (!store_->Load(settings_, error))
            {
                LogUiDiagnostic(L"settings_load_failed", nullptr, 0, 0, 0,
                    FALSE, ERROR_SUCCESS, error);
                if (error.empty()) error = L"settings.ini could not be read.";
                return false;
            }
            const bool settingsFileExists = GetFileAttributesW(store_->Path().c_str()) != INVALID_FILE_ATTRIBUTES;
            LogUiDiagnostic(L"settings_loaded", nullptr, 0, 0, 0, TRUE, ERROR_SUCCESS,
                L"ini_location=exe_directory ini_exists=" + std::to_wstring(settingsFileExists ? 1 : 0) +
                L" read_only=" + std::to_wstring(uiPolicy_.readOnlySettings ? 1 : 0));
            std::wstring startupError;
            if (uiPolicy_.applyStartupSettings && !startup_.Apply(settings_.startWithWindows, startupError))
            {
                error = std::move(startupError);
                LogUiDiagnostic(L"startup_configuration_failed", nullptr, 0, 0, 0,
                    FALSE, GetLastError(), error);
                return false;
            }
            if (!uiPolicy_.applyStartupSettings)
                LogUiDiagnostic(L"startup_configuration_skipped", nullptr, 0, 0, 0,
                    TRUE, ERROR_SUCCESS, L"safe_mode=1");
            lastSyntheticActivity_ = GetTickCount64();
            lastIdleCheckTick_ = lastSyntheticActivity_;
            popup_ = std::make_unique<StatusPopup>(instance_);
            RefreshHibernate();
            const WindowsScreenSaverSnapshot windowsSaver =
                screenSaver_.ReadWindowsScreenSaverSnapshot();
            LogRuntimeDiagnostic(L"windows_screensaver_snapshot", TRUE, ERROR_SUCCESS,
                L"active_query_ok=" + std::to_wstring(windowsSaver.activeQuerySucceeded ? 1 : 0) +
                L" active=" + std::to_wstring(windowsSaver.active ? 1 : 0) +
                L" active_query_error=" + std::to_wstring(windowsSaver.activeQueryError) +
                L" timeout_query_ok=" + std::to_wstring(windowsSaver.timeoutQuerySucceeded ? 1 : 0) +
                L" timeout_seconds=" + std::to_wstring(windowsSaver.timeoutSeconds) +
                L" timeout_query_error=" + std::to_wstring(windowsSaver.timeoutQueryError) +
                L" running_query_ok=" + std::to_wstring(windowsSaver.runningQuerySucceeded ? 1 : 0) +
                L" running=" + std::to_wstring(windowsSaver.running ? 1 : 0) +
                L" running_query_error=" + std::to_wstring(windowsSaver.runningQueryError) +
                L" selected_saver_configured=" +
                    std::to_wstring(windowsSaver.selectedSaverConfigured ? 1 : 0) +
                L" app_custom_saver_selected=" +
                    std::to_wstring(settings_.useCustomScreenSaver ? 1 : 0) +
                L" app_custom_saver_path_set=" +
                    std::to_wstring(!settings_.screenSaverPath.empty() ? 1 : 0) +
                L" app_auto_enabled=" + std::to_wstring(settings_.autoScreenSaverEnabled ? 1 : 0) +
                L" app_auto_minutes=" + std::to_wstring(settings_.screenSaverIdleMinutes) +
                L" lock_enabled=" + std::to_wstring(settings_.lockEnabled ? 1 : 0) +
                L" lock_minutes=" + std::to_wstring(settings_.lockIdleMinutes) +
                L" lock_guard_enabled=" +
                    std::to_wstring(settings_.suspendLockWhileMediaOrPresentation ? 1 : 0) +
                L" safe_ui_mode=" + std::to_wstring(safeUiTestMode_ ? 1 : 0) +
                L" runtime_timers_enabled=" + std::to_wstring(uiPolicy_.startRuntimeTimers ? 1 : 0));

            if (uiPolicy_.suppressWindowsScreenSaver)
            {
                const ScreenSaverLeaseResult suppression = screenSaver_.SuppressWindowsScreenSaver();
                LogRuntimeDiagnostic(L"windows_screensaver_suppression_begin",
                    suppression.succeeded ? TRUE : FALSE, suppression.error,
                    L"query_ok=" + std::to_wstring(suppression.querySucceeded ? 1 : 0) +
                    L" originally_active=" + std::to_wstring(windowsSaver.active ? 1 : 0) +
                    L" changed=" + std::to_wstring(suppression.changed ? 1 : 0) +
                    L" skipped_secure=" + std::to_wstring(suppression.securityProtected ? 1 : 0) +
                    L" skipped_policy=" + std::to_wstring(suppression.policyManaged ? 1 : 0) +
                    L" runtime_only=1 safe_ui_mode=0");
            }
            else
            {
                LogRuntimeDiagnostic(L"windows_screensaver_suppression_skipped",
                    TRUE, ERROR_SUCCESS, L"safe_ui_mode=1");
            }

            WNDCLASSEXW windowClass{ sizeof(windowClass) };
            windowClass.lpfnWndProc = WindowProcedure;
            windowClass.hInstance = instance_;
            windowClass.hIcon = static_cast<HICON>(LoadImageW(
                instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
            LogUiDiagnostic(L"main_window_icon_loaded", nullptr, 0, 0, 0,
                windowClass.hIcon != nullptr ? TRUE : FALSE,
                windowClass.hIcon != nullptr ? ERROR_SUCCESS : GetLastError());
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClass.lpszClassName = MainWindowClass;
            SetLastError(ERROR_SUCCESS);
            const ATOM mainClass = RegisterClassExW(&windowClass);
            const DWORD classError = GetLastError();
            LogUiDiagnostic(L"main_window_class_registered", nullptr, 0, 0, 0,
                mainClass != 0 || classError == ERROR_CLASS_ALREADY_EXISTS ? TRUE : FALSE,
                mainClass != 0 ? ERROR_SUCCESS : classError);
            if (mainClass == 0 && classError != ERROR_CLASS_ALREADY_EXISTS)
            {
                error = L"The application window class could not be registered.";
                return false;
            }

            SetLastError(ERROR_SUCCESS);
            window_ = CreateWindowExW(
                WS_EX_TOOLWINDOW,
                MainWindowClass,
                L"Classic Desktop Screensaver",
                WS_POPUP,
                0, 0, 0, 0,
                nullptr, nullptr, instance_, this);
            const DWORD mainWindowError = GetLastError();
            LogUiDiagnostic(L"main_window_created", window_, WM_NCCREATE, 0, 0,
                window_ != nullptr ? TRUE : FALSE, mainWindowError,
                IsWindow(window_) ? L"is_window=1" : L"is_window=0");
            if (window_ == nullptr)
            {
                error = L"The application message window could not be created.";
                return false;
            }

            SetLastError(ERROR_SUCCESS);
            sessionDisplayNotification_ = RegisterPowerSettingNotification(
                window_, &SessionDisplayStatusGuid, DEVICE_NOTIFY_WINDOW_HANDLE);
            const DWORD displayNotificationError = sessionDisplayNotification_ != nullptr
                ? ERROR_SUCCESS : GetLastError();
            LogRuntimeDiagnostic(L"session_display_notification_registered",
                sessionDisplayNotification_ != nullptr ? TRUE : FALSE,
                displayNotificationError,
                L"interactive_session=1");

            SetLastError(ERROR_SUCCESS);
            sessionNotificationsRegistered_ = WTSRegisterSessionNotification(
                window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
            const DWORD sessionNotificationError = sessionNotificationsRegistered_
                ? ERROR_SUCCESS : GetLastError();
            LogRuntimeDiagnostic(L"session_unlock_notification_registered",
                sessionNotificationsRegistered_ ? TRUE : FALSE,
                sessionNotificationError,
                L"scope=this_session");

            settingsHost_ = {
                .context = this,
                .getSettings = GetSettingsCallback,
                .updateSettings = UpdateSettingsCallback,
                .refreshHibernate = RefreshHibernateCallback,
                .windowsSaverPath = WindowsSaverPathCallback,
                .startSaver = StartSaverCallback,
                .openSaverSettings = OpenSaverSettingsCallback,
                .keyboardSuspended = KeyboardSuspendedCallback,
                .readOnly = uiPolicy_.readOnlySettings
            };
            SetLastError(ERROR_SUCCESS);
            settingsWindow_ = CreateSettingsWindow(instance_, nullptr, &settingsHost_);
            LogUiDiagnostic(L"settings_window_precreated", settingsWindow_, WM_NCCREATE, 0, 0,
                settingsWindow_ != nullptr ? TRUE : FALSE,
                settingsWindow_ != nullptr ? ERROR_SUCCESS : GetLastError());
            if (settingsWindow_ == nullptr)
            {
                error = L"The Settings window could not be created.";
                return false;
            }

            if (uiPolicy_.installKeyboardHook)
            {
                keyboard_ = std::make_unique<KeyboardHook>(window_);
                if (!keyboard_->Install())
                {
                    error = L"The global keyboard hook could not be installed (" +
                        std::to_wstring(keyboard_->Error()) + L").";
                    LogUiDiagnostic(L"keyboard_hook_install_failed", window_, 0, 0, 0,
                        FALSE, keyboard_->Error());
                    return false;
                }
            }
            else LogUiDiagnostic(L"keyboard_hook_skipped", window_, 0, 0, 0,
                TRUE, ERROR_SUCCESS, L"safe_mode=1");

            activeIcon_ = static_cast<HICON>(LoadImageW(
                instance_, MAKEINTRESOURCEW(IDI_TRAY_ACTIVE), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
            inactiveIcon_ = static_cast<HICON>(LoadImageW(
                instance_, MAKEINTRESOURCEW(IDI_TRAY_INACTIVE), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
            LogUiDiagnostic(L"tray_icons_loaded", window_, 0, 0, 0,
                activeIcon_ != nullptr && inactiveIcon_ != nullptr ? TRUE : FALSE,
                activeIcon_ != nullptr && inactiveIcon_ != nullptr ? ERROR_SUCCESS : GetLastError());
            if (activeIcon_ == nullptr || inactiveIcon_ == nullptr)
            {
                error = L"The tray icons could not be loaded from the application resources.";
                return false;
            }
            taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
            LogUiDiagnostic(L"taskbar_created_message_registered", window_, 0, 0, 0,
                taskbarCreatedMessage_ != 0 ? TRUE : FALSE, taskbarCreatedMessage_ != 0 ? ERROR_SUCCESS : GetLastError());
            ApplyRuntimeSettings();
            if (!trayAdded_)
            {
                error = L"The system tray icon could not be added.";
                LogUiDiagnostic(L"tray_icon_initialization_failed", window_, 0, 0, 0,
                    FALSE, trayAddError_);
                return false;
            }

            if (uiPolicy_.startRuntimeTimers)
            {
                SetLastError(ERROR_SUCCESS);
                const UINT_PTR idleTimer = SetTimer(window_, TimerIdle, 500, nullptr);
                const DWORD idleTimerError = idleTimer != 0 ? ERROR_SUCCESS : GetLastError();
                SetLastError(ERROR_SUCCESS);
                const UINT_PTR hotCornerTimer = SetTimer(window_, TimerHotCorner, 100, nullptr);
                const DWORD hotCornerTimerError = hotCornerTimer != 0 ? ERROR_SUCCESS : GetLastError();
                const bool timersReady = idleTimer != 0 && hotCornerTimer != 0;
                const DWORD timerError = idleTimer == 0 ? idleTimerError : hotCornerTimerError;
                LogRuntimeDiagnostic(L"runtime_timer_setup", timersReady ? TRUE : FALSE,
                    timersReady ? ERROR_SUCCESS : timerError,
                    L"idle_timer=" + std::to_wstring(idleTimer) +
                    L" idle_timer_error=" + std::to_wstring(idleTimerError) +
                    L" hot_corner_timer=" + std::to_wstring(hotCornerTimer) +
                    L" hot_corner_timer_error=" + std::to_wstring(hotCornerTimerError));
                if (!timersReady)
                {
                    error = L"Application timers could not be started.";
                    LogUiDiagnostic(L"runtime_timer_start_failed", window_, WM_TIMER, 0, 0,
                        FALSE, timerError);
                    return false;
                }
            }
            if (!uiPolicy_.startRuntimeTimers)
                LogUiDiagnostic(L"runtime_timers_skipped", window_, 0, 0, 0,
                    TRUE, ERROR_SUCCESS, L"safe_mode=1");

            showThread_ = CreateThread(nullptr, 0, ShowEventThread, this, 0, nullptr);
            if (showThread_ == nullptr)
            {
                error = L"The single-instance listener could not be started.";
                return false;
            }

            const bool backgroundMode = HasCommandLineSwitch(commandLine, L"--background");
            const bool explicitSettingsRequest = IsSettingsCommandLine(commandLine);
            if (ShouldShowSettingsAtLaunch(backgroundMode, explicitSettingsRequest))
            {
                ShowSettings();
            }
            LogUiDiagnostic(L"initialize_complete", window_, 0, 0, 0, TRUE, ERROR_SUCCESS);
            return true;
        }
        catch (const winrt::hresult_error& exception)
        {
            error = L"Windows Runtime initialization failed: " + std::wstring(exception.message());
            LogUiDiagnostic(L"initialize_exception", window_, 0, 0, 0, FALSE,
                static_cast<DWORD>(exception.code().value));
            return false;
        }
        catch (...)
        {
            error = L"The application could not be initialized.";
            LogUiDiagnostic(L"initialize_exception_unknown", window_, 0, 0, 0,
                FALSE, ERROR_UNHANDLED_EXCEPTION);
            return false;
        }
    }

    int NativeApplication::Run() noexcept
    {
        if (exitCommandOnly_) return 0;
        LogUiDiagnostic(L"message_loop_enter", window_);
        if (alreadyRunning_)
        {
            Shutdown();
            return 0;
        }
        MSG message{};
        int result = 0;
        while (true)
        {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received <= 0)
            {
                result = received == 0 ? static_cast<int>(message.wParam) : 1;
                LogUiDiagnostic(L"message_loop_exit", window_, message.message,
                    message.wParam, message.lParam, received == 0 ? TRUE : FALSE,
                    received == 0 ? ERROR_SUCCESS : GetLastError(),
                    L"exit_code=" + std::to_wstring(result));
                break;
            }
            if (settingsWindow_ != nullptr && IsWindow(settingsWindow_) &&
                IsWindowVisible(settingsWindow_) && IsDialogMessageW(settingsWindow_, &message))
            {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Shutdown();
        return result;
    }

    LRESULT CALLBACK NativeApplication::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        NativeApplication* application = reinterpret_cast<NativeApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            application = static_cast<NativeApplication*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        }
        if (application != nullptr)
        {
            try
            {
                return application->ProcessMessage(window, message, wParam, lParam);
            }
            catch (...)
            {
                // Never allow a C++ exception to cross the Win32 callback ABI.
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    DWORD WINAPI NativeApplication::ShowEventThread(void* context) noexcept
    {
        auto* application = static_cast<NativeApplication*>(context);
        const HANDLE handles[] = {
            application->showEvent_, application->exitEvent_, application->stopEvent_ };
        while (true)
        {
            const DWORD wait = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 2 || wait == WAIT_FAILED) return 0;
            if (wait == WAIT_OBJECT_0)
            {
                const HWND window = application->window_;
                if (window != nullptr) PostMessageW(window, MessageShowSettings, 0, 0);
            }
            else if (wait == WAIT_OBJECT_0 + 1)
            {
                const HWND window = application->window_;
                if (window != nullptr) PostMessageW(window, MessageExit, 0, 0);
            }
        }
    }

    DWORD WINAPI NativeApplication::PowerWorker(void* context) noexcept
    {
        std::unique_ptr<PowerWorkerArguments> arguments(static_cast<PowerWorkerArguments*>(context));
        if (!arguments) return 0;
        Win32PowerApi api;
        const PowerRequestResult result = arguments->hibernate
            ? RequestHibernate(api)
            : RequestSleep(api);
        try
        {
            std::wstring detail = arguments->hibernate ? L"operation=Hibernate" : L"operation=Sleep";
            detail += L" status=" + std::to_wstring(static_cast<unsigned int>(result.status));
            detail += L" failure_stage=";
            const std::wstring_view failureStage = PowerFailureStageName(result.failureStage);
            if (failureStage.empty()) detail += L"None";
            else detail.append(failureStage);
            LogPowerRequestDiagnostic(arguments->window, arguments->hibernate,
                result.RequestWasAccepted() ? TRUE : FALSE,
                static_cast<DWORD>(result.errorCode), detail);
        }
        catch (...)
        {
            // Power diagnostics are best-effort and must not terminate the worker.
        }
        auto* resultCopy = new (std::nothrow) PowerRequestResult(result);
        if (resultCopy != nullptr && PostMessageW(
            arguments->window, MessagePowerResult, 0, reinterpret_cast<LPARAM>(resultCopy)) == FALSE)
        {
            delete resultCopy;
        }
        return 0;
    }

    DWORD WINAPI NativeApplication::GuardWorker(void* context) noexcept
    {
        std::unique_ptr<GuardWorkerArguments> arguments(static_cast<GuardWorkerArguments*>(context));
        if (!arguments) return 0;
        const GuardStatus status = QueryGuards(arguments->checkMedia, arguments->checkFullscreen);
        auto* result = new (std::nothrow) GuardWorkerResult{
            arguments->revision, arguments->saverDue, arguments->lockDue, status };
        if (result == nullptr)
        {
            // A null payload is a fail-open completion marker. The UI thread
            // retains the due flags for this single outstanding worker.
            (void)PostMessageW(arguments->window, MessageGuardResult,
                static_cast<WPARAM>(arguments->revision), 0);
            return 0;
        }
        if (PostMessageW(arguments->window, MessageGuardResult,
            static_cast<WPARAM>(arguments->revision), reinterpret_cast<LPARAM>(result)) == FALSE)
        {
            delete result;
        }
        return 0;
    }

    LRESULT NativeApplication::ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_)
        {
            LogUiDiagnostic(L"taskbar_recreated", window, message, wParam, lParam);
            if (trayRetryPending_)
            {
                KillTimer(window_, TimerTrayRegistrationRetry);
                trayRetryPending_ = false;
            }
            trayRetryAttempts_ = 0;
            trayAdded_ = false;
            // Keep the last negotiated format until registration succeeds again.
            // Some shells can still deliver callbacks from the existing icon.
            UpdateTray();
            if (!trayAdded_) ScheduleTrayRegistrationRetry();
            return 0;
        }

        switch (message)
        {
        case WM_COPYDATA:
        {
            if (safeUiTestMode_ || lParam == 0 ||
                !IsTrustedBrowserHostWindow(reinterpret_cast<HWND>(wParam)))
            {
                LogRuntimeDiagnostic(L"browser_video_report_rejected",
                    FALSE, ERROR_ACCESS_DENIED, L"trusted_host=0 safe_ui_mode=" +
                    std::to_wstring(safeUiTestMode_ ? 1 : 0));
                return FALSE;
            }
            const auto* copyData = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (copyData->dwData != BrowserVideoCopyDataId ||
                copyData->cbData != sizeof(BrowserVideoStatusMessage) ||
                copyData->lpData == nullptr)
            {
                LogRuntimeDiagnostic(L"browser_video_report_rejected",
                    FALSE, ERROR_INVALID_DATA, L"payload_valid=0");
                return FALSE;
            }

            BrowserVideoStatusMessage incoming{};
            memcpy(&incoming, copyData->lpData, sizeof(incoming));
            if (!IsValidBrowserVideoStatus(incoming))
            {
                LogRuntimeDiagnostic(L"browser_video_report_rejected",
                    FALSE, ERROR_INVALID_DATA, L"status_valid=0");
                return FALSE;
            }

            const bool wasActive = IsBrowserVideoCurrentlyActive();
            browserVideoStatus_ = incoming;
            browserVideoReportReceivedAt_ = GetTickCount64();
            const bool isActive = IsBrowserVideoCurrentlyActive();
            if (wasActive != isActive)
            {
                LogRuntimeDiagnostic(L"browser_video_guard_status",
                    TRUE, ERROR_SUCCESS,
                    L"active=" + std::to_wstring(isActive ? 1 : 0) +
                    L" visible_video_count=" +
                        std::to_wstring(browserVideoStatus_.visibleVideoCount) +
                    L" monitoring=" + std::to_wstring(
                        (browserVideoStatus_.flags & BrowserVideoMonitoringFlag) != 0 ? 1 : 0));
            }
            return TRUE;
        }
        case WM_DISPLAYCHANGE:
            if (!trayAdded_ && !trayRetryPending_)
            {
                trayRetryAttempts_ = 0;
                ScheduleTrayRegistrationRetry();
            }
            LogUiDiagnostic(L"application_display_change", window, message, wParam, lParam,
                TRUE, ERROR_SUCCESS,
                L"bits_per_pixel=" + std::to_wstring(wParam) + L" width=" +
                std::to_wstring(LOWORD(lParam)) + L" height=" +
                std::to_wstring(HIWORD(lParam)) + L" settings_valid=" +
                std::to_wstring(settingsWindow_ != nullptr && IsWindow(settingsWindow_) ? 1 : 0) +
                L" settings_visible=" + std::to_wstring(settingsWindow_ != nullptr &&
                    IsWindow(settingsWindow_) && IsWindowVisible(settingsWindow_) ? 1 : 0));
            break;
        case WM_SETTINGCHANGE:
            RefreshHibernate();
            if (wParam == SPI_SETSCREENSAVEACTIVE)
            {
                AbandonWindowsScreenSaverSuppression(L"screensaver_active_setting_changed");
            }
            else
            {
                ReassertWindowsScreenSaverSuppression(L"setting_change");
            }
            if (wParam == SPI_SETWORKAREA || wParam == SPI_SETNONCLIENTMETRICS)
            {
                LogUiDiagnostic(L"application_ui_setting_change", window, message, wParam, lParam,
                    TRUE, ERROR_SUCCESS,
                    L"settings_valid=" + std::to_wstring(settingsWindow_ != nullptr &&
                        IsWindow(settingsWindow_) ? 1 : 0));
            }
            break;
        case WM_DPICHANGED:
            LogUiDiagnostic(L"application_dpi_change", window, message, wParam, lParam,
                TRUE, ERROR_SUCCESS,
                L"dpi_x=" + std::to_wstring(LOWORD(wParam)) + L" dpi_y=" +
                std::to_wstring(HIWORD(wParam)) + L" settings_valid=" +
                std::to_wstring(settingsWindow_ != nullptr && IsWindow(settingsWindow_) ? 1 : 0));
            break;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND)
            {
                suspendNoticeReceived_ = true;
                ResetIdleBaseline(L"power_broadcast_suspend");
                LogRuntimeDiagnostic(L"power_suspend_notice", TRUE, ERROR_SUCCESS,
                    L"suspend_notice_received=1");
                return TRUE;
            }
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND ||
                wParam == PBT_APMRESUMECRITICAL)
            {
                ResetAfterResume(static_cast<UINT>(wParam));
                ReassertWindowsScreenSaverSuppression(L"resume");
                return TRUE;
            }
            if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0)
            {
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                if (IsEqualGUID(setting->PowerSetting, SessionDisplayStatusGuid) &&
                    setting->DataLength >= sizeof(DWORD))
                {
                    DWORD displayState = 0;
                    memcpy(&displayState, setting->Data, sizeof(displayState));
                    LogRuntimeDiagnostic(L"session_display_status_changed", TRUE, ERROR_SUCCESS,
                        L"display_state=" + std::to_wstring(displayState));
                    if (displayState == 1)
                    {
                        ResetIdleBaseline(L"session_display_on");
                    }
                }
                return TRUE;
            }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_UNLOCK || wParam == WTS_SESSION_LOGON)
            {
                LogRuntimeDiagnostic(L"session_user_returned", TRUE, ERROR_SUCCESS,
                    L"session_event=" + std::to_wstring(wParam));
                ResetIdleBaseline(wParam == WTS_SESSION_UNLOCK
                    ? L"session_unlock" : L"session_logon");
            }
            return 0;
        case WM_TIMER:
            OnTimer(static_cast<UINT_PTR>(wParam));
            return 0;
        case MessageTray:
        {
            TrayNotification notification{};
            if (TryDecodeTrayCallback(wParam, lParam, trayVersion4_, TrayIconId, notification))
            {
                const std::wstring detail = L"version4=" + std::to_wstring(notification.hasAnchor ? 1 : 0) +
                    L" iconId=" + std::to_wstring(notification.iconId) +
                    L" event=" + std::to_wstring(notification.eventCode) +
                    L" anchor=" + std::to_wstring(notification.anchor.x) + L"," +
                    std::to_wstring(notification.anchor.y);
                LogUiDiagnostic(L"tray_callback_decoded", window, notification.eventCode,
                    notification.iconId, MAKELPARAM(notification.anchor.x, notification.anchor.y),
                    TRUE, ERROR_SUCCESS, detail);
                NotifyIconEvent(notification);
            }
            else
            {
                LogUiDiagnostic(L"tray_callback_rejected", window, message, wParam, lParam,
                    FALSE, ERROR_INVALID_DATA,
                    trayVersion4_ ? L"preferred_format=version4" : L"preferred_format=legacy");
            }
            return 0;
        }
        case MessageShortcut:
        {
            const auto action = static_cast<ShortcutAction>(wParam);
            if (action == ShortcutAction::Sleep || action == ShortcutAction::Hibernate)
            {
                const std::wstring detail =
                    std::wstring(action == ShortcutAction::Hibernate
                        ? L"action=Hibernate matched_modifiers="
                        : L"action=Sleep matched_modifiers=") +
                    std::to_wstring(static_cast<unsigned int>(lParam) & 0xFFu);
                LogUiDiagnostic(L"keyboard_power_shortcut_matched", window,
                    message, wParam, lParam, TRUE, ERROR_SUCCESS, detail);
            }
            HandleAction(action);
            return 0;
        }
        case MessageShowSettings:
            ShowSettings();
            return 0;
        case MessageExit:
            LogUiDiagnostic(L"single_instance_exit_requested", window, message,
                wParam, lParam, TRUE, ERROR_SUCCESS);
            exitRequested_ = true;
            DestroyWindow(window);
            return 0;
        case MessagePowerResult:
        {
            std::unique_ptr<PowerRequestResult> result(reinterpret_cast<PowerRequestResult*>(lParam));
            powerPending_ = false;
            sleepDelayed_ = false;
            if (powerThread_ != nullptr)
            {
                CloseHandle(powerThread_);
                powerThread_ = nullptr;
            }
            if (result) HandlePowerResult(*result);
            if (keyboard_) keyboard_->ResetTransientState();
            return 0;
        }
        case MessageGuardResult:
        {
            std::unique_ptr<GuardWorkerResult> result(
                reinterpret_cast<GuardWorkerResult*>(lParam));
            guardPending_ = false;
            if (guardThread_ != nullptr)
            {
                CloseHandle(guardThread_);
                guardThread_ = nullptr;
            }
            const std::uint32_t resultRevision = result != nullptr
                ? result->revision
                : static_cast<std::uint32_t>(wParam);
            if (IsGuardQueryResultCurrent(
                resultRevision, pendingGuardRevision_, guardQueryRevision_))
            {
                if (result != nullptr)
                {
                    ApplyGuards(result->saverDue, result->lockDue, result->status);
                }
                else
                {
                    LogRuntimeDiagnostic(L"guard_result_allocation_failed", FALSE,
                        ERROR_NOT_ENOUGH_MEMORY, L"power_actions=0 media_guard_fail_open=1");
                    ApplyGuards(guardSaverDue_, guardLockDue_, {});
                }
            }
            else
            {
                LogRuntimeDiagnostic(L"stale_guard_result_ignored", TRUE, ERROR_SUCCESS,
                    L"result_revision=" + std::to_wstring(static_cast<std::uint32_t>(wParam)) +
                    L" pending_revision=" + std::to_wstring(pendingGuardRevision_) +
                    L" current_revision=" + std::to_wstring(guardQueryRevision_));
            }
            return 0;
        }
        case WM_COMMAND:
            LogUiDiagnostic(L"main_window_command", window, message, wParam, lParam,
                TRUE, ERROR_SUCCESS, L"command=" + std::to_wstring(LOWORD(wParam)));
            switch (LOWORD(wParam))
            {
            case MenuSettings: ShowSettings(); return 0;
            case MenuStartSaver:
                if (uiPolicy_.allowSaverActions)
                {
                    MarkActivity();
                    StartScreenSaver(ScreenSaverPathForLaunch(settings_), true);
                }
                return 0;
            case MenuToggleSaver:
                if (uiPolicy_.allowSaverActions) ToggleAutoScreenSaver();
                return 0;
            case MenuExit: exitRequested_ = true; DestroyWindow(window); return 0;
            default: break;
            }
            break;
        case WM_QUERYENDSESSION:
            LogUiDiagnostic(L"windows_query_end_session", window, message,
                wParam, lParam, TRUE, ERROR_SUCCESS);
            return TRUE;
        case WM_ENDSESSION:
            if (wParam != FALSE)
            {
                LogRuntimeDiagnostic(L"windows_end_session_restore", TRUE, ERROR_SUCCESS,
                    L"flags=" + std::to_wstring(static_cast<ULONG_PTR>(lParam)));
                RestoreWindowsScreenSaver();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void NativeApplication::Shutdown() noexcept
    {
        LogUiDiagnostic(L"shutdown_begin", window_);
        RestoreWindowsScreenSaver();
        if (stopEvent_ != nullptr) SetEvent(stopEvent_);
        if (showThread_ != nullptr)
        {
            WaitForSingleObject(showThread_, INFINITE);
            CloseHandle(showThread_);
            showThread_ = nullptr;
        }
        if (window_ != nullptr)
        {
            if (sessionDisplayNotification_ != nullptr)
            {
                UnregisterPowerSettingNotification(sessionDisplayNotification_);
                sessionDisplayNotification_ = nullptr;
            }
            if (sessionNotificationsRegistered_)
            {
                WTSUnRegisterSessionNotification(window_);
                sessionNotificationsRegistered_ = false;
            }
            KillTimer(window_, TimerIdle);
            KillTimer(window_, TimerHotCorner);
            KillTimer(window_, TimerSleepDelay);
            KillTimer(window_, TimerTrayRegistrationRetry);
            trayRetryPending_ = false;
        }
        if (keyboard_)
        {
            keyboard_->Uninstall();
            keyboard_.reset();
        }
        RemoveTray();
        if (settingsWindow_ != nullptr && IsWindow(settingsWindow_))
        {
            DestroyWindow(settingsWindow_);
            settingsWindow_ = nullptr;
        }
        if (popup_) popup_.reset();
        if (window_ != nullptr)
        {
            if (IsWindow(window_)) DestroyWindow(window_);
            window_ = nullptr;
        }
        if (powerThread_ != nullptr)
        {
            CloseHandle(powerThread_);
            powerThread_ = nullptr;
        }
        if (guardThread_ != nullptr)
        {
            CloseHandle(guardThread_);
            guardThread_ = nullptr;
        }
        if (showEvent_ != nullptr) { CloseHandle(showEvent_); showEvent_ = nullptr; }
        if (exitEvent_ != nullptr) { CloseHandle(exitEvent_); exitEvent_ = nullptr; }
        if (stopEvent_ != nullptr) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
        if (mutex_ != nullptr)
        {
            if (ownsMutex_) ReleaseMutex(mutex_);
            ownsMutex_ = false;
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
        if (apartmentInitialized_)
        {
            winrt::uninit_apartment();
            apartmentInitialized_ = false;
        }
        LogUiDiagnostic(L"shutdown_complete");
    }

    void NativeApplication::ApplyRuntimeSettings()
    {
        if (keyboard_)
        {
            std::vector<ShortcutAssignment> shortcuts = BuildShortcuts();
            if (HasDuplicateShortcuts(shortcuts))
            {
                shortcuts.clear();
            }
            keyboard_->SetShortcuts(shortcuts);
        }
        InvalidateGuardQueryResults();
        UpdateTray();
    }

    bool NativeApplication::IsBrowserVideoCurrentlyActive() const noexcept
    {
        return IsBrowserVideoReportActive(
            browserVideoStatus_, browserVideoReportReceivedAt_, GetTickCount64());
    }

    void NativeApplication::ReassertWindowsScreenSaverSuppression(const wchar_t* reason) noexcept
    {
        if (!uiPolicy_.suppressWindowsScreenSaver || screenSaverSuppressionAbandoned_) return;
        const ScreenSaverLeaseResult result = screenSaver_.ReassertWindowsScreenSaverSuppression();
        if (!result.succeeded || result.changed || result.securityProtected)
        {
            LogRuntimeDiagnostic(L"windows_screensaver_suppression_reassert",
                result.succeeded ? TRUE : FALSE, result.error,
                L"reason=" + std::wstring(reason == nullptr ? L"unknown" : reason) +
                L" query_ok=" + std::to_wstring(result.querySucceeded ? 1 : 0) +
                L" changed=" + std::to_wstring(result.changed ? 1 : 0) +
                L" skipped_secure=" + std::to_wstring(result.securityProtected ? 1 : 0) +
                L" skipped_policy=" + std::to_wstring(result.policyManaged ? 1 : 0));
        }
    }

    void NativeApplication::AbandonWindowsScreenSaverSuppression(const wchar_t* reason) noexcept
    {
        if (!uiPolicy_.suppressWindowsScreenSaver) return;
        screenSaverSuppressionAbandoned_ = true;
        const ScreenSaverLeaseResult result = screenSaver_.AbandonWindowsScreenSaverSuppression();
        const WindowsScreenSaverSnapshot snapshot = screenSaver_.ReadWindowsScreenSaverSnapshot();
        LogRuntimeDiagnostic(L"windows_screensaver_suppression_released_for_setting_change",
            result.succeeded ? TRUE : FALSE, result.error,
            L"reason=" + std::wstring(reason == nullptr ? L"unknown" : reason) +
            L" active_query_ok=" + std::to_wstring(snapshot.activeQuerySucceeded ? 1 : 0) +
            L" active=" + std::to_wstring(snapshot.active ? 1 : 0) +
            L" no_reacquire_this_run=1");
    }

    void NativeApplication::RestoreWindowsScreenSaver() noexcept
    {
        if (!screenSaver_.SuppressionAcquired()) return;
        const ScreenSaverLeaseResult result = screenSaver_.RestoreWindowsScreenSaver();
        LogRuntimeDiagnostic(L"windows_screensaver_suppression_restore",
            result.succeeded ? TRUE : FALSE, result.error,
            L"changed=" + std::to_wstring(result.changed ? 1 : 0) +
            L" restored_active=" + std::to_wstring(result.active ? 1 : 0) +
            L" runtime_only=1");
    }

    bool NativeApplication::UpdateSettings(const AppSettings& candidate, std::wstring& error)
    {
        error.clear();
        if (!uiPolicy_.persistSettings)
        {
            error = L"Settings changes are disabled in the safe UI test mode.";
            LogUiDiagnostic(L"settings_write_blocked", window_, WM_COMMAND, 0, 0,
                FALSE, ERROR_ACCESS_DENIED, L"safe_mode=1");
            return false;
        }
        if (!ValidateSettings(candidate, error)) return false;

        const bool startupChanged = settings_.startWithWindows != candidate.startWithWindows;
        if (startupChanged)
        {
            std::wstring startupError;
            if (!startup_.Apply(candidate.startWithWindows, startupError))
            {
                error = std::move(startupError);
                return false;
            }
        }

        if (store_ == nullptr || !store_->Save(candidate, error))
        {
            if (startupChanged)
            {
                std::wstring ignored;
                (void)startup_.Apply(settings_.startWithWindows, ignored);
            }
            if (error.empty()) error = L"settings.ini could not be written.";
            return false;
        }

        settings_ = candidate;
        ApplyRuntimeSettings();
        MarkActivity();
        saverFiredThisIdle_ = false;
        lockAttemptedThisIdle_ = false;
        LogRuntimeDiagnostic(L"idle_settings_applied", TRUE, ERROR_SUCCESS,
            L"app_saver_enabled=" + std::to_wstring(settings_.autoScreenSaverEnabled ? 1 : 0) +
            L" app_saver_minutes=" + std::to_wstring(settings_.screenSaverIdleMinutes) +
            L" app_custom_saver_selected=" + std::to_wstring(settings_.useCustomScreenSaver ? 1 : 0) +
            L" app_custom_saver_path_set=" + std::to_wstring(!settings_.screenSaverPath.empty() ? 1 : 0) +
            L" lock_enabled=" + std::to_wstring(settings_.lockEnabled ? 1 : 0) +
            L" lock_minutes=" + std::to_wstring(settings_.lockIdleMinutes) +
            L" guard_enabled=" + std::to_wstring(settings_.suspendLockWhileMediaOrPresentation ? 1 : 0) +
            L" idle_countdown_reset=1");
        return true;
    }

    void NativeApplication::ShowSettings()
    {
        DWORD settingsProcessId{};
        const DWORD settingsThreadId = settingsWindow_ != nullptr && IsWindow(settingsWindow_)
            ? GetWindowThreadProcessId(settingsWindow_, &settingsProcessId) : 0;
        const bool settingsHandleValid = settingsWindow_ != nullptr && IsWindow(settingsWindow_) &&
            settingsThreadId == GetCurrentThreadId() && settingsProcessId == GetCurrentProcessId();
        LogUiDiagnostic(L"settings_show_requested", settingsWindow_, WM_SHOWWINDOW, 0, 0,
            settingsHandleValid ? TRUE : FALSE,
            settingsHandleValid ? ERROR_SUCCESS : ERROR_INVALID_WINDOW_HANDLE,
            L"settings_thread=" + std::to_wstring(settingsThreadId) +
            L" ui_thread=" + std::to_wstring(GetCurrentThreadId()) +
            L" process=" + std::to_wstring(settingsProcessId));
        RefreshHibernate();
        if (!settingsHandleValid)
        {
            settingsWindow_ = CreateSettingsWindow(instance_, nullptr, &settingsHost_);
            LogUiDiagnostic(settingsWindow_ != nullptr ? L"settings_window_recreated" :
                L"settings_window_recreate_failed", settingsWindow_, WM_NCCREATE, 0, 0,
                settingsWindow_ != nullptr ? TRUE : FALSE,
                settingsWindow_ != nullptr ? ERROR_SUCCESS : GetLastError());
        }
        if (settingsWindow_ != nullptr && IsWindow(settingsWindow_))
        {
            ShowSettingsWindow(settingsWindow_);
            RECT rectangle{};
            const BOOL rectRead = GetWindowRect(settingsWindow_, &rectangle);
            LogUiDiagnostic(L"settings_show_completed", settingsWindow_, WM_SHOWWINDOW, 0, 0,
                IsWindowVisible(settingsWindow_), rectRead != FALSE ? ERROR_SUCCESS : GetLastError(),
                L"valid=" + std::to_wstring(IsWindow(settingsWindow_) ? 1 : 0) +
                L" visible=" + std::to_wstring(IsWindowVisible(settingsWindow_) ? 1 : 0) +
                L" iconic=" + std::to_wstring(IsIconic(settingsWindow_) ? 1 : 0) +
                L" rect=" + std::to_wstring(rectangle.left) + L"," +
                std::to_wstring(rectangle.top) + L"," + std::to_wstring(rectangle.right) + L"," +
                std::to_wstring(rectangle.bottom));
        }
    }

    void NativeApplication::RefreshHibernate()
    {
        hibernateAvailability_ = QueryHibernateAvailability();
        // Hibernate remains registered when configured; HandleAction checks
        // current S4 capability immediately before requesting the transition.
    }

    void NativeApplication::UpdateTray()
    {
        if (window_ == nullptr) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = TrayIconId;
        data.uCallbackMessage = MessageTray;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.hIcon = settings_.autoScreenSaverEnabled ? activeIcon_ : inactiveIcon_;
        const std::wstring tip(Text(settings_.language,
            settings_.autoScreenSaverEnabled ? L"TrayActive" : L"TrayInactive"));
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);

        if (!trayAdded_)
        {
            trayAddError_ = ERROR_SUCCESS;
            SetLastError(ERROR_SUCCESS);
            const BOOL added = Shell_NotifyIconW(NIM_ADD, &data);
            const DWORD addError = GetLastError();
            trayAddError_ = added != FALSE ? ERROR_SUCCESS :
                addError != ERROR_SUCCESS ? addError : ERROR_GEN_FAILURE;
            LogUiDiagnostic(L"shell_notifyicon_add", window_, MessageTray, 0, 0,
                added, trayAddError_,
                L"iconId=" + std::to_wstring(TrayIconId));
            if (added == FALSE)
            {
                // The shell can retain the notification icon across a taskbar
                // recreation. If NIM_ADD reports failure, probe that case with
                // NIM_MODIFY before declaring the icon unavailable.
                SetLastError(ERROR_SUCCESS);
                const BOOL existingIconUpdated = Shell_NotifyIconW(NIM_MODIFY, &data);
                const DWORD modifyError = GetLastError();
                LogUiDiagnostic(existingIconUpdated != FALSE
                    ? L"shell_notifyicon_existing_icon_reused"
                    : L"shell_notifyicon_existing_icon_unavailable",
                    window_, MessageTray, NIM_MODIFY, 0,
                    existingIconUpdated,
                    existingIconUpdated ? ERROR_SUCCESS : modifyError,
                    L"add_error=" + std::to_wstring(trayAddError_));
                if (existingIconUpdated == FALSE) return;
                trayAddError_ = ERROR_SUCCESS;
            }
            trayAdded_ = true;
            trayRetryAttempts_ = 0;
            if (trayRetryPending_)
            {
                KillTimer(window_, TimerTrayRegistrationRetry);
                trayRetryPending_ = false;
            }
            data.uVersion = NOTIFYICON_VERSION_4;
            SetLastError(ERROR_SUCCESS);
            const BOOL versionSet = Shell_NotifyIconW(NIM_SETVERSION, &data);
            const DWORD versionError = GetLastError();
            trayVersion4_ = versionSet != FALSE;
            LogUiDiagnostic(L"shell_notifyicon_setversion", window_, MessageTray,
                NOTIFYICON_VERSION_4, 0, versionSet,
                versionSet ? ERROR_SUCCESS : versionError,
                trayVersion4_ ? L"callback_format=version4" : L"callback_format=legacy_fallback");
        }

        data.uFlags = NIF_STATE;
        data.dwStateMask = NIS_HIDDEN;
        data.dwState = settings_.showTrayStatusIcon ? 0 : NIS_HIDDEN;
        SetLastError(ERROR_SUCCESS);
        const BOOL stateModified = Shell_NotifyIconW(NIM_MODIFY, &data);
        const DWORD stateError = GetLastError();
        LogUiDiagnostic(L"shell_notifyicon_modify_state", window_, MessageTray, 0, 0,
            stateModified, stateModified ? ERROR_SUCCESS : stateError,
            L"hidden=" + std::to_wstring(settings_.showTrayStatusIcon ? 0 : 1));
        data.uFlags = NIF_ICON | NIF_TIP;
        data.hIcon = settings_.autoScreenSaverEnabled ? activeIcon_ : inactiveIcon_;
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        SetLastError(ERROR_SUCCESS);
        const BOOL iconModified = Shell_NotifyIconW(NIM_MODIFY, &data);
        const DWORD iconError = GetLastError();
        LogUiDiagnostic(L"shell_notifyicon_modify_icon", window_, MessageTray, 0, 0,
            iconModified, iconModified ? ERROR_SUCCESS : iconError);
    }

    void NativeApplication::ScheduleTrayRegistrationRetry() noexcept
    {
        if (trayAdded_ || trayRetryPending_ ||
            !ShouldRetryTrayRegistration(trayAdded_, trayRetryAttempts_) || window_ == nullptr)
        {
            return;
        }

        const UINT delay = TrayRegistrationRetryDelayMs(trayRetryAttempts_);
        SetLastError(ERROR_SUCCESS);
        const UINT_PTR timer = SetTimer(window_, TimerTrayRegistrationRetry,
            delay, nullptr);
        const DWORD timerError = timer != 0 ? ERROR_SUCCESS : GetLastError();
        trayRetryPending_ = timer != 0;
        LogUiDiagnostic(timer != 0 ? L"tray_registration_retry_scheduled" :
            L"tray_registration_retry_schedule_failed", window_, WM_TIMER,
            TimerTrayRegistrationRetry, delay,
            timer != 0, timerError,
            L"attempts=" + std::to_wstring(trayRetryAttempts_) +
            L" max_delay_ms=" + std::to_wstring(TrayRegistrationRetryMaximumIntervalMs));
    }

    void NativeApplication::RemoveTray() noexcept
    {
        if (!trayAdded_ || window_ == nullptr) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = TrayIconId;
        SetLastError(ERROR_SUCCESS);
        const BOOL removed = Shell_NotifyIconW(NIM_DELETE, &data);
        const DWORD removeError = GetLastError();
        LogUiDiagnostic(L"shell_notifyicon_delete", window_, MessageTray, 0, 0,
            removed, removed ? ERROR_SUCCESS : removeError);
        trayAdded_ = false;
        trayVersion4_ = false;
    }

    void NativeApplication::ShowTrayMenu(const TrayNotification& notification)
    {
        if (trayMenuShowing_)
        {
            LogUiDiagnostic(L"tray_menu_duplicate_suppressed", window_,
                notification.eventCode, notification.iconId, 0,
                FALSE, ERROR_POPUP_ALREADY_ACTIVE,
                notification.hasAnchor ? L"callback_format=version4" : L"callback_format=legacy");
            return;
        }
        trayMenuShowing_ = true;
        struct ResetMenuGuard
        {
            bool& showing;
            ~ResetMenuGuard() { showing = false; }
        } reset{ trayMenuShowing_ };

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            LogUiDiagnostic(L"tray_menu_create_failed", window_, 0, 0, 0,
                FALSE, GetLastError());
            return;
        }
        LogUiDiagnostic(L"tray_menu_created", window_, 0, 0, 0, TRUE, ERROR_SUCCESS,
            L"menu=" + std::to_wstring(static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(menu))));
        TrayMenuImages menuImages(GetDpiForWindow(window_));
        LogUiDiagnostic(L"tray_menu_images_ready", window_, 0, 0, 0,
            menuImages.IsAvailable() ? TRUE : FALSE,
            menuImages.IsAvailable() ? ERROR_SUCCESS : ERROR_NOT_READY,
            L"size=" + std::to_wstring(menuImages.Size()));

        const auto entries = MakeTrayMenuModel(
            uiPolicy_.allowSaverActions, settings_.autoScreenSaverEnabled);
        const auto append = [menu, this, &menuImages](const TrayMenuEntryModel& entry)
        {
            const std::wstring text(Text(settings_.language, entry.localizationKey));
            const UINT flags = MF_STRING | (entry.enabled ? 0u : MF_GRAYED | MF_DISABLED);
            SetLastError(ERROR_SUCCESS);
            const BOOL appended = AppendMenuW(menu, flags, entry.command, text.c_str());
            const DWORD errorCode = GetLastError();
            LogUiDiagnostic(L"tray_menu_append", window_, WM_COMMAND, entry.command, 0,
                appended, appended ? ERROR_SUCCESS : errorCode,
                L"enabled=" + std::to_wstring(entry.enabled ? 1 : 0));
            if (appended != FALSE)
            {
                const HBITMAP bitmap = menuImages.Get(entry.glyph);
                if (bitmap != nullptr)
                {
                    MENUITEMINFOW item{};
                    item.cbSize = sizeof(item);
                    item.fMask = MIIM_BITMAP;
                    item.hbmpItem = bitmap;
                    SetLastError(ERROR_SUCCESS);
                    const BOOL bitmapSet = SetMenuItemInfoW(menu, entry.command, FALSE, &item);
                    const DWORD bitmapError = GetLastError();
                    LogUiDiagnostic(L"tray_menu_icon_set", window_, WM_COMMAND,
                        entry.command, 0, bitmapSet,
                        bitmapSet ? ERROR_SUCCESS : bitmapError,
                        L"glyph=" + std::to_wstring(static_cast<unsigned int>(entry.glyph)));
                }
                else
                {
                    LogUiDiagnostic(L"tray_menu_icon_unavailable", window_, WM_COMMAND,
                        entry.command, 0, FALSE, ERROR_NOT_READY);
                }
            }
            return appended != FALSE;
        };
        bool menuReady = true;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (index == entries.size() - 1)
            {
                SetLastError(ERROR_SUCCESS);
                const BOOL separatorAdded = AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                const DWORD separatorError = GetLastError();
                LogUiDiagnostic(L"tray_menu_separator", window_, 0, 0, 0,
                    separatorAdded, separatorAdded ? ERROR_SUCCESS : separatorError);
                menuReady = separatorAdded != FALSE && menuReady;
            }
            menuReady = append(entries[index]) && menuReady;
        }
        if (!menuReady)
        {
            DestroyMenu(menu);
            return;
        }

        POINT point{};
        UINT alignmentFlags = TPM_WORKAREA | TPM_LEFTALIGN | TPM_TOPALIGN;
        bool hasPosition = false;
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = window_;
        identifier.uID = TrayIconId;
        RECT iconRectangle{};
        const HRESULT iconRectangleResult = Shell_NotifyIconGetRect(&identifier, &iconRectangle);
        if (SUCCEEDED(iconRectangleResult))
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) != FALSE && PtInRect(&iconRectangle, cursor) != FALSE)
            {
                LogUiDiagnostic(L"tray_menu_cursor_on_icon", window_, 0,
                    static_cast<WPARAM>(cursor.x), static_cast<LPARAM>(cursor.y), TRUE, ERROR_SUCCESS);
            }
            HMONITOR monitor = MonitorFromRect(&iconRectangle, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{ sizeof(monitorInfo) };
            if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
            {
                const TrayMenuPlacement placement = CalculateTrayMenuPlacement(
                    iconRectangle, monitorInfo.rcMonitor, monitorInfo.rcWork,
                    static_cast<LONG>(menuImages.Size() * 6));
                point = placement.anchor;
                alignmentFlags = placement.alignmentFlags;
            }
            else
            {
                point.x = iconRectangle.left + (iconRectangle.right - iconRectangle.left) / 2;
                point.y = iconRectangle.top + (iconRectangle.bottom - iconRectangle.top) / 2;
            }
            hasPosition = true;
            LogUiDiagnostic(L"tray_icon_rectangle", window_, 0, 0, 0, TRUE,
                static_cast<DWORD>(iconRectangleResult),
                L"left=" + std::to_wstring(iconRectangle.left) +
                L" top=" + std::to_wstring(iconRectangle.top) +
                L" right=" + std::to_wstring(iconRectangle.right) +
                L" bottom=" + std::to_wstring(iconRectangle.bottom) +
                L" menu_x=" + std::to_wstring(point.x) +
                L" menu_y=" + std::to_wstring(point.y) +
                L" alignment=" + std::to_wstring(alignmentFlags));
        }
        else
        {
            LogUiDiagnostic(L"tray_icon_rectangle_unavailable", window_, 0, 0, 0,
                FALSE, static_cast<DWORD>(iconRectangleResult));
        }
        if (!hasPosition && GetCursorPos(&point) == FALSE)
        {
            LogUiDiagnostic(L"tray_menu_cursor_failed", window_, 0, 0, 0,
                FALSE, GetLastError());
            DestroyMenu(menu);
            return;
        }
        const bool wasTopmost = (GetWindowLongPtrW(window_, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        SetLastError(ERROR_SUCCESS);
        const BOOL temporaryTopmostSet = SetWindowPos(window_, HWND_TOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const DWORD topmostError = GetLastError();
        LogUiDiagnostic(L"tray_menu_temporary_topmost", window_, 0, 0, 0,
            temporaryTopmostSet, temporaryTopmostSet ? ERROR_SUCCESS : topmostError,
            L"restore_after_menu=" + std::to_wstring(!wasTopmost && temporaryTopmostSet ? 1 : 0));
        struct TemporaryTopmostGuard
        {
            HWND window{};
            bool restore{};

            bool Restore() noexcept
            {
                if (!restore) return true;
                SetLastError(ERROR_SUCCESS);
                const BOOL restored = SetWindowPos(window, HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                const DWORD error = GetLastError();
                restore = false;
                LogUiDiagnostic(L"tray_menu_topmost_restored", window, 0, 0, 0,
                    restored, restored ? ERROR_SUCCESS : error);
                return restored != FALSE;
            }

            ~TemporaryTopmostGuard()
            {
                if (restore)
                {
                    SetWindowPos(window, HWND_NOTOPMOST,
                        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        } topmostGuard{ window_, !wasTopmost && temporaryTopmostSet != FALSE };

        SetLastError(ERROR_SUCCESS);
        const BOOL foreground = SetForegroundWindow(window_);
        const DWORD foregroundError = GetLastError();
        LogUiDiagnostic(L"tray_menu_set_foreground", window_, 0, 0, 0,
            foreground, foreground ? ERROR_SUCCESS : foregroundError,
            GetForegroundWindow() == window_ ? L"foreground_now=1" : L"foreground_now=0");
        SetLastError(ERROR_SUCCESS);
        const UINT command = TrackPopupMenuEx(menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | alignmentFlags,
            point.x, point.y, window_, nullptr);
        const DWORD menuError = GetLastError();
        LogUiDiagnostic(command != 0 ? L"tray_menu_selection" :
            menuError == ERROR_SUCCESS ? L"tray_menu_cancelled" : L"tray_menu_failed",
            window_, WM_CONTEXTMENU,
            static_cast<WPARAM>(point.x), static_cast<LPARAM>(point.y),
            command != 0 ? TRUE : menuError == ERROR_SUCCESS ? TRUE : FALSE,
            command != 0 ? ERROR_SUCCESS : menuError,
            L"command=" + std::to_wstring(command) + L" owner_valid=" +
            std::to_wstring(IsWindow(window_) ? 1 : 0));
        SetLastError(ERROR_SUCCESS);
        const BOOL menuDestroyed = DestroyMenu(menu);
        const DWORD destroyError = GetLastError();
        LogUiDiagnostic(L"tray_menu_destroy", window_, 0, 0, 0,
            menuDestroyed, menuDestroyed ? ERROR_SUCCESS : destroyError);
        topmostGuard.Restore();
        PostMessageW(window_, WM_NULL, 0, 0);
        if (trayAdded_)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window_;
            data.uID = TrayIconId;
            SetLastError(ERROR_SUCCESS);
            const BOOL focusSet = Shell_NotifyIconW(NIM_SETFOCUS, &data);
            const DWORD focusError = GetLastError();
            LogUiDiagnostic(L"shell_notifyicon_setfocus", window_, MessageTray, 0, 0,
                focusSet, focusSet ? ERROR_SUCCESS : focusError);
        }
        if (command != 0) SendMessageW(window_, WM_COMMAND, command, 0);
    }

    void NativeApplication::NotifyIconEvent(const TrayNotification& notification)
    {
        switch (notification.eventCode)
        {
        case WM_LBUTTONDBLCLK:
            ShowSettings();
            break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            if (IsTrayContextMenuEvent(notification.eventCode, notification.hasAnchor))
            {
                ShowTrayMenu(notification);
            }
            else
            {
                LogUiDiagnostic(L"tray_context_duplicate_event_ignored", window_,
                    notification.eventCode, notification.iconId, 0,
                    TRUE, ERROR_SUCCESS,
                    notification.hasAnchor ? L"version4_uses_WM_CONTEXTMENU" : L"legacy_uses_WM_RBUTTONUP");
            }
            break;
        default:
            break;
        }
    }

    void NativeApplication::HandleAction(ShortcutAction action)
    {
        if ((action == ShortcutAction::Sleep || action == ShortcutAction::Hibernate) &&
            !uiPolicy_.allowPowerActions)
        {
            LogUiDiagnostic(L"power_action_blocked", window_, MessageShortcut,
                static_cast<WPARAM>(action), 0, FALSE, ERROR_ACCESS_DENIED,
                L"safe_mode=1");
            return;
        }
        if ((action == ShortcutAction::StartScreenSaver || action == ShortcutAction::ToggleScreenSaver) &&
            !uiPolicy_.allowSaverActions)
        {
            LogUiDiagnostic(L"saver_action_blocked", window_, MessageShortcut,
                static_cast<WPARAM>(action), 0, FALSE, ERROR_ACCESS_DENIED,
                L"safe_mode=1");
            return;
        }
        MarkActivity();
        saverFiredThisIdle_ = false;
        lockAttemptedThisIdle_ = false;
        switch (action)
        {
        case ShortcutAction::StartScreenSaver:
            StartScreenSaver(ScreenSaverPathForLaunch(settings_), true);
            break;
        case ShortcutAction::ToggleScreenSaver:
            ToggleAutoScreenSaver();
            break;
        case ShortcutAction::Sleep:
        {
            // The user explicitly requested a power transition. Reset the
            // idle baseline immediately, even if Modern Standby does not
            // deliver the classic suspend/resume broadcasts on this device.
            MarkActivity();
            const ULONGLONG now = GetTickCount64();
            if (lastSleepRequest_ != 0 && now - lastSleepRequest_ < 1800) break;
            lastSleepRequest_ = now;
            if (powerPending_) break;
            powerPending_ = true;
            sleepDelayed_ = true;
            if (SetTimer(window_, TimerSleepDelay, 850, nullptr) == 0)
            {
                sleepDelayed_ = false;
                powerPending_ = false;
                ShowError(std::wstring(Text(settings_.language, L"PowerFailed")));
            }
            break;
        }
        case ShortcutAction::Hibernate:
            MarkActivity();
            StartPowerRequest(true);
            break;
        default:
            break;
        }
    }

    void NativeApplication::StartPowerRequest(bool hibernate)
    {
        if (!uiPolicy_.allowPowerActions)
        {
            LogUiDiagnostic(L"power_request_blocked", window_, MessagePowerResult,
                hibernate ? 1 : 0, 0, FALSE, ERROR_ACCESS_DENIED,
                L"safe_mode=1");
            return;
        }
        if (hibernate)
        {
            // Check the live S4 capability at the point the user invokes the
            // shortcut. The worker queries it again immediately before the
            // Windows power API, closing the race with system configuration.
            const HibernateCapabilities capabilities = QueryHibernateCapabilities();
            hibernateAvailability_ = EvaluateHibernateAvailability(capabilities);
            if (hibernateAvailability_ != HibernateAvailability::Available)
            {
                const bool queryFailed = hibernateAvailability_ == HibernateAvailability::QueryFailed;
                LogRuntimeDiagnostic(L"hibernate_shortcut_blocked", FALSE,
                    queryFailed ? capabilities.queryError : ERROR_NOT_SUPPORTED,
                    queryFailed ? L"reason=query_failed" : L"reason=s4_or_hiberfile_unavailable");
                ShowError(std::wstring(Text(settings_.language,
                    queryFailed ? L"CapabilitiesUnavailable" : L"HibernateUnavailable")));
                return;
            }
        }
        if (powerPending_) return;
        powerPending_ = true;
        auto* arguments = new (std::nothrow) PowerWorkerArguments{ window_, hibernate };
        if (arguments == nullptr)
        {
            powerPending_ = false;
            ShowError(std::wstring(Text(settings_.language, L"PowerFailed")));
            return;
        }
        powerThread_ = CreateThread(nullptr, 0, PowerWorker, arguments, 0, nullptr);
        if (powerThread_ == nullptr)
        {
            delete arguments;
            powerPending_ = false;
            ShowError(std::wstring(Text(settings_.language, L"PowerFailed")));
        }
    }

    void NativeApplication::HandlePowerResult(const PowerRequestResult& result)
    {
        if (result.status == PowerRequestStatus::RequestAccepted) return;
        ShowError(PowerErrorText(result, settings_.language));
    }

    bool NativeApplication::StartScreenSaver(
        const std::wstring& path,
        bool showErrors,
        std::wstring* errorOut)
    {
        if (errorOut != nullptr) errorOut->clear();
        if (!uiPolicy_.allowSaverActions)
        {
            LogUiDiagnostic(L"screen_saver_action_blocked", window_, 0, 0, 0,
                FALSE, ERROR_ACCESS_DENIED, L"safe_mode=1");
            if (errorOut != nullptr)
                *errorOut = L"Screen saver actions are disabled in the safe UI test mode.";
            return false;
        }
        if (settings_.useCustomScreenSaver && path.empty())
        {
            LogRuntimeDiagnostic(L"screen_saver_start_failed", FALSE, ERROR_NOT_FOUND,
                L"custom_selected=1 custom_path_empty=1");
            const std::wstring message(Text(settings_.language, L"NoSaverSelected"));
            if (showErrors) ShowError(message);
            if (errorOut != nullptr) *errorOut = message;
            return false;
        }
        std::wstring error;
        DWORD failureCode = ERROR_SUCCESS;
        ScreenSaverLaunchDetails launchDetails{};
        if (!screenSaver_.Start(path, error, settings_.language, &failureCode, &launchDetails))
        {
            LogRuntimeDiagnostic(L"screen_saver_start_failed", FALSE, failureCode,
                L"custom_selected=" + std::to_wstring(!path.empty() ? 1 : 0) +
                L" custom_option_enabled=" +
                    std::to_wstring(settings_.useCustomScreenSaver ? 1 : 0) +
                L" windows_path_configured=" +
                    std::to_wstring(!screenSaver_.ReadWindowsScreenSaverPath().empty() ? 1 : 0));
            if (showErrors) ShowError(error);
            if (errorOut != nullptr) *errorOut = std::move(error);
            return false;
        }
        LogRuntimeDiagnostic(L"screen_saver_start_accepted", TRUE, ERROR_SUCCESS,
            L"custom_option_enabled=" + std::to_wstring(settings_.useCustomScreenSaver ? 1 : 0) +
            L" process_id=" + std::to_wstring(launchDetails.processId) +
            L" process_handle=" + std::to_wstring(launchDetails.processHandleAvailable ? 1 : 0) +
            L" reused_existing_process=" +
                std::to_wstring(launchDetails.reusedExistingProcess ? 1 : 0) +
            L" error_dialog_enabled=" + std::to_wstring(showErrors ? 1 : 0));
        MarkActivity();
        saverFiredThisIdle_ = true;
        return true;
    }

    void NativeApplication::ToggleAutoScreenSaver()
    {
        if (!uiPolicy_.allowSaverActions) return;
        settings_.autoScreenSaverEnabled = !settings_.autoScreenSaverEnabled;
        if (store_ != nullptr)
        {
            std::wstring ignored;
            (void)store_->Save(settings_, ignored);
        }
        MarkActivity();
        UpdateTray();
        if (settings_.showStatusNotifications) ShowStatusToast(settings_.autoScreenSaverEnabled, settings_.language);
    }

    void NativeApplication::MarkActivity() noexcept
    {
        InvalidateGuardQueryResults();
        lastSyntheticActivity_ = GetTickCount64();
        lastIdleCheckTick_ = lastSyntheticActivity_;
        hotCornerEntered_ = 0;
        hotCornerTriggered_ = false;
    }

    void NativeApplication::InvalidateGuardQueryResults() noexcept
    {
        guardQueryRevision_ = NextGuardQueryRevision(guardQueryRevision_);
    }

    void NativeApplication::OnTimer(UINT_PTR timer)
    {
        if (timer == TimerTrayRegistrationRetry)
        {
            KillTimer(window_, TimerTrayRegistrationRetry);
            trayRetryPending_ = false;
            if (trayAdded_)
            {
                trayRetryAttempts_ = 0;
                return;
            }

            if (trayRetryAttempts_ != std::numeric_limits<unsigned int>::max())
                ++trayRetryAttempts_;
            const unsigned int attempt = trayRetryAttempts_;
            LogUiDiagnostic(L"tray_registration_retry_attempt", window_, WM_TIMER,
                TimerTrayRegistrationRetry, attempt, TRUE, ERROR_SUCCESS);
            UpdateTray();
            if (trayAdded_)
            {
                LogUiDiagnostic(L"tray_registration_recovered", window_, WM_TIMER,
                    TimerTrayRegistrationRetry, attempt, TRUE, ERROR_SUCCESS);
            }
            else if (ShouldRetryTrayRegistration(trayAdded_, trayRetryAttempts_))
            {
                ScheduleTrayRegistrationRetry();
            }
            return;
        }

        if (!uiPolicy_.startRuntimeTimers) return;
        const ScreenSaverProcessObservation saverProcess = screenSaver_.ObserveProcess();
        if (saverProcess.state == ScreenSaverProcessState::Running)
        {
            LogRuntimeDiagnostic(L"screen_saver_process_running", TRUE, ERROR_SUCCESS,
                L"process_id=" + std::to_wstring(saverProcess.processId) +
                L" observed_after_ms=" + std::to_wstring(saverProcess.elapsedMilliseconds));
        }
        else if (saverProcess.state == ScreenSaverProcessState::Exited)
        {
            LogRuntimeDiagnostic(L"screen_saver_process_exited", TRUE, ERROR_SUCCESS,
                L"process_id=" + std::to_wstring(saverProcess.processId) +
                L" exit_code=" + std::to_wstring(saverProcess.exitCode) +
                L" lifetime_ms=" + std::to_wstring(saverProcess.elapsedMilliseconds));
        }
        else if (saverProcess.state == ScreenSaverProcessState::Failed)
        {
            LogRuntimeDiagnostic(L"screen_saver_process_observation_failed", FALSE,
                saverProcess.error,
                L"process_id=" + std::to_wstring(saverProcess.processId) +
                L" elapsed_ms=" + std::to_wstring(saverProcess.elapsedMilliseconds));
        }
        if (timer == TimerSleepDelay)
        {
            KillTimer(window_, TimerSleepDelay);
            if (sleepDelayed_)
            {
                sleepDelayed_ = false;
                powerPending_ = false;
                if (keyboard_) keyboard_->ResetTransientState();
                StartPowerRequest(false);
            }
            return;
        }
        if (timer == TimerIdle) CheckIdle();
        else if (timer == TimerHotCorner) CheckHotCorner();
    }

    void NativeApplication::CheckIdle()
    {
        const ULONGLONG currentTick = GetTickCount64();
        const ULONGLONG previousTick = lastIdleCheckTick_;
        lastIdleCheckTick_ = currentTick;
        if (IsIdleTimerGap(previousTick, currentTick,
            IdleTimerResumeGapThresholdMilliseconds))
        {
            ResetIdleBaseline(L"idle_timer_gap");
            return;
        }

        LASTINPUTINFO input{};
        input.cbSize = sizeof(input);
        ULONGLONG systemIdle = 0;
        SetLastError(ERROR_SUCCESS);
        const BOOL lastInputRead = GetLastInputInfo(&input);
        const DWORD inputError = lastInputRead != FALSE ? ERROR_SUCCESS : GetLastError();
        if (lastInputRead != FALSE)
        {
            systemIdle = static_cast<DWORD>(GetTickCount() - input.dwTime);
            lastInputFailureLogged_ = false;
        }
        else if (!lastInputFailureLogged_)
        {
            lastInputFailureLogged_ = true;
            LogRuntimeDiagnostic(L"last_input_query_failed", FALSE, inputError,
                L"idle_timers_fail_closed=1");
        }
        const ULONGLONG syntheticIdle = GetTickCount64() - lastSyntheticActivity_;
        const ULONGLONG idle = EffectiveIdleMilliseconds(systemIdle, syntheticIdle);
        if (idle < 1000)
        {
            saverFiredThisIdle_ = false;
            lockAttemptedThisIdle_ = false;
        }

        const bool saverDue = IsAutoScreenSaverDue(
            settings_.autoScreenSaverEnabled, saverFiredThisIdle_, idle,
            settings_.screenSaverIdleMinutes);
        const bool lockDue = IsAutoLockDue(
            settings_.lockEnabled, lockAttemptedThisIdle_, idle,
            settings_.lockIdleMinutes);
        const ULONGLONG now = GetTickCount64();
        if (lastIdleRuntimeDiagnostic_ == 0 || now - lastIdleRuntimeDiagnostic_ >= 30'000)
        {
            lastIdleRuntimeDiagnostic_ = now;
            const WindowsScreenSaverSnapshot windowsSaver =
                screenSaver_.ReadWindowsScreenSaverSnapshot();
            LogRuntimeDiagnostic(L"idle_policy_snapshot", TRUE, ERROR_SUCCESS,
                L"get_last_input_ok=" + std::to_wstring(lastInputRead != FALSE ? 1 : 0) +
                L" system_idle_ms=" + std::to_wstring(systemIdle) +
                L" app_idle_ms=" + std::to_wstring(syntheticIdle) +
                L" effective_idle_ms=" + std::to_wstring(idle) +
                L" saver_due=" + std::to_wstring(saverDue ? 1 : 0) +
                L" app_saver_enabled=" + std::to_wstring(settings_.autoScreenSaverEnabled ? 1 : 0) +
                L" app_saver_minutes=" + std::to_wstring(settings_.screenSaverIdleMinutes) +
                L" lock_due=" + std::to_wstring(lockDue ? 1 : 0) +
                L" lock_enabled=" + std::to_wstring(settings_.lockEnabled ? 1 : 0) +
                L" lock_minutes=" + std::to_wstring(settings_.lockIdleMinutes) +
                L" lock_guard_enabled=" +
                    std::to_wstring(settings_.suspendLockWhileMediaOrPresentation ? 1 : 0) +
                L" windows_saver_active=" +
                    std::to_wstring(windowsSaver.activeQuerySucceeded && windowsSaver.active ? 1 : 0) +
                L" windows_saver_active_query_error=" +
                    std::to_wstring(windowsSaver.activeQueryError) +
                L" windows_saver_timeout_s=" + std::to_wstring(windowsSaver.timeoutSeconds) +
                L" windows_saver_timeout_query_error=" +
                    std::to_wstring(windowsSaver.timeoutQueryError) +
                L" windows_saver_running=" +
                    std::to_wstring(windowsSaver.runningQuerySucceeded && windowsSaver.running ? 1 : 0) +
                L" windows_saver_running_query_error=" +
                    std::to_wstring(windowsSaver.runningQueryError) +
                L" windows_saver_configured=" +
                    std::to_wstring(windowsSaver.selectedSaverConfigured ? 1 : 0) +
                L" app_custom_saver_selected=" +
                    std::to_wstring(settings_.useCustomScreenSaver ? 1 : 0) +
                L" app_custom_saver_path_set=" +
                    std::to_wstring(!settings_.screenSaverPath.empty() ? 1 : 0));
        }
        if (!saverDue && !lockDue) return;

        bool checkMedia = false;
        bool checkFullscreen = saverDue && settings_.blockScreenSaverWhileFullscreen;
        if (lockDue && settings_.suspendLockWhileMediaOrPresentation)
        {
            checkMedia = true;
            checkFullscreen = true;
        }
        if (checkMedia || checkFullscreen)
        {
            if (!guardPending_) StartGuardQuery(saverDue, lockDue, checkMedia, checkFullscreen);
            return;
        }
        ApplyGuards(saverDue, lockDue, {});
    }

    void NativeApplication::CheckHotCorner()
    {
        if (!settings_.hotCornerEnabled)
        {
            hotCornerEntered_ = 0;
            hotCornerTriggered_ = false;
            return;
        }
        POINT point{};
        if (GetCursorPos(&point) == FALSE)
        {
            hotCornerEntered_ = 0;
            hotCornerTriggered_ = false;
            return;
        }
        const LONG left = 0;
        const LONG top = 0;
        const LONG right = GetSystemMetrics(SM_CXSCREEN);
        const LONG bottom = GetSystemMetrics(SM_CYSCREEN);
        const bool inside = IsPointInHotCorner(
            settings_.hotCorner, point.x, point.y, left, top, right, bottom);
        if (!inside)
        {
            hotCornerEntered_ = 0;
            hotCornerTriggered_ = false;
            return;
        }
        if (hotCornerEntered_ == 0) hotCornerEntered_ = GetTickCount64();
        if (IsHotCornerDelayDue(
            inside, hotCornerTriggered_, GetTickCount64() - hotCornerEntered_,
            settings_.hotCornerDelayMs))
        {
            hotCornerTriggered_ = true;
            MarkActivity();
            StartScreenSaver(ScreenSaverPathForLaunch(settings_), true);
            hotCornerTriggered_ = true;
        }
    }

    void NativeApplication::StartGuardQuery(
        bool saverDue,
        bool lockDue,
        bool checkMedia,
        bool checkFullscreen)
    {
        if (guardPending_) return;
        guardSaverDue_ = saverDue;
        guardLockDue_ = lockDue;
        guardMediaCheck_ = checkMedia;
        guardFullscreenCheck_ = checkFullscreen;
        guardQueryRevision_ = NextGuardQueryRevision(guardQueryRevision_);
        auto* arguments = new (std::nothrow) GuardWorkerArguments{
            window_, guardQueryRevision_, saverDue, lockDue, checkMedia, checkFullscreen };
        if (arguments == nullptr)
        {
            ApplyGuards(saverDue, lockDue, {});
            return;
        }
        guardPending_ = true;
        pendingGuardRevision_ = arguments->revision;
        guardThread_ = CreateThread(nullptr, 0, GuardWorker, arguments, 0, nullptr);
        if (guardThread_ == nullptr)
        {
            delete arguments;
            guardPending_ = false;
            ApplyGuards(saverDue, lockDue, {});
        }
    }

    void NativeApplication::ApplyGuards(bool saverDue, bool lockDue, const GuardStatus& status)
    {
        const bool mediaGuardAppliesToLock =
            lockDue && settings_.suspendLockWhileMediaOrPresentation;
        const bool browserVideoActive =
            mediaGuardAppliesToLock && IsBrowserVideoCurrentlyActive();
        GuardStatus combinedStatus = status;
        combinedStatus.videoPlaying = combinedStatus.videoPlaying || browserVideoActive;
        if (saverDue)
        {
            const RuntimeGuardSnapshot snapshot{
                combinedStatus.videoPlaying, status.fullScreenApplication, status.presentationMode };
            const bool blocked = IsScreenSaverBlocked(
                false,
                settings_.blockScreenSaverWhileFullscreen,
                snapshot);
            if (blocked)
            {
                const ULONGLONG now = GetTickCount64();
                if (lastSaverGuardDiagnostic_ == 0 || now - lastSaverGuardDiagnostic_ >= 30'000)
                {
                    lastSaverGuardDiagnostic_ = now;
                    LogRuntimeDiagnostic(L"automatic_saver_deferred", TRUE, ERROR_SUCCESS,
                        GuardStatusDiagnostic(combinedStatus));
                }
            }
            else
            {
                LogRuntimeDiagnostic(L"automatic_saver_guard_clear", TRUE, ERROR_SUCCESS,
                    GuardStatusDiagnostic(combinedStatus));
                // Latch before launching so a missing or invalid .scr does not
                // trigger another shell launch every 500 ms.
                saverFiredThisIdle_ = true;
                StartScreenSaver(ScreenSaverPathForLaunch(settings_), false);
            }
        }
        if (lockDue)
        {
            const RuntimeGuardSnapshot snapshot{
                combinedStatus.videoPlaying, status.fullScreenApplication, status.presentationMode };
            const bool blocked = IsAutomaticLockBlocked(
                settings_.suspendLockWhileMediaOrPresentation, snapshot);
            if (blocked)
            {
                const ULONGLONG now = GetTickCount64();
                if (lastLockGuardDiagnostic_ == 0 || now - lastLockGuardDiagnostic_ >= 30'000)
                {
                    lastLockGuardDiagnostic_ = now;
                    LogRuntimeDiagnostic(L"automatic_lock_deferred", TRUE, ERROR_SUCCESS,
                        L"guard_enabled=1 " + GuardStatusDiagnostic(combinedStatus));
                }
            }
            else
            {
                lockAttemptedThisIdle_ = true;
                SetLastError(ERROR_SUCCESS);
                const BOOL accepted = LockWorkStation();
                const DWORD errorCode = accepted != FALSE ? ERROR_SUCCESS : GetLastError();
                LogRuntimeDiagnostic(L"automatic_lock_request", accepted, errorCode,
                    L"idle_minutes=" + std::to_wstring(settings_.lockIdleMinutes) +
                    L" app_saver_enabled=" +
                        std::to_wstring(settings_.autoScreenSaverEnabled ? 1 : 0) +
                    L" guard_blocked=0");
                if (accepted == FALSE)
                {
                    ShowError(std::wstring(Text(settings_.language, L"LockRequestFailed")) +
                        L" (" + std::to_wstring(errorCode) + L")");
                }
            }
        }
    }

    void NativeApplication::ResetIdleBaseline(const wchar_t* reason)
    {
        const bool saverHadFired = saverFiredThisIdle_;
        const bool lockHadBeenAttempted = lockAttemptedThisIdle_;
        const bool guardWasPending = guardPending_;
        const std::uint32_t revisionBeforeReset = guardQueryRevision_;
        if (keyboard_) keyboard_->ResetTransientState();
        MarkActivity();
        saverFiredThisIdle_ = false;
        lockAttemptedThisIdle_ = false;
        lastSleepRequest_ = 0;
        LogRuntimeDiagnostic(L"idle_baseline_reset", TRUE, ERROR_SUCCESS,
            L"reason=" + std::wstring(reason != nullptr ? reason : L"unknown") +
            L" stale_guard_invalidated=1" +
            L" saver_fired_before_reset=" + std::to_wstring(saverHadFired ? 1 : 0) +
            L" lock_attempted_before_reset=" + std::to_wstring(lockHadBeenAttempted ? 1 : 0) +
            L" guard_pending_before_reset=" + std::to_wstring(guardWasPending ? 1 : 0) +
            L" guard_revision_before=" + std::to_wstring(revisionBeforeReset) +
            L" guard_revision_after=" + std::to_wstring(guardQueryRevision_));
    }

    void NativeApplication::ResetAfterResume(UINT resumeEvent)
    {
        const bool saverHadFired = saverFiredThisIdle_;
        const bool guardWasPending = guardPending_;
        const bool suspendNotice = suspendNoticeReceived_;
        const std::uint32_t revisionBeforeResume = guardQueryRevision_;
        suspendNoticeReceived_ = false;
        ResetIdleBaseline(L"power_broadcast_resume");
        RefreshHibernate();
        const WindowsScreenSaverSnapshot windowsSaver =
            screenSaver_.ReadWindowsScreenSaverSnapshot();
        LogRuntimeDiagnostic(L"power_resume_notice", TRUE, ERROR_SUCCESS,
            L"resume_event=" + std::to_wstring(resumeEvent) +
            L" stale_guard_invalidated=1" +
            L" suspend_notice_received=" + std::to_wstring(suspendNotice ? 1 : 0) +
            L" saver_fired_before_resume=" + std::to_wstring(saverHadFired ? 1 : 0) +
            L" guard_pending_before_resume=" + std::to_wstring(guardWasPending ? 1 : 0) +
            L" guard_revision_before=" + std::to_wstring(revisionBeforeResume) +
            L" guard_revision_after=" + std::to_wstring(guardQueryRevision_) +
            L" windows_saver_running_query_ok=" +
                std::to_wstring(windowsSaver.runningQuerySucceeded ? 1 : 0) +
            L" windows_saver_running=" +
                std::to_wstring(windowsSaver.runningQuerySucceeded && windowsSaver.running ? 1 : 0));
    }

    void NativeApplication::ShowError(std::wstring message)
    {
        if (popup_) popup_->Show(message);
    }

    std::vector<ShortcutAssignment> NativeApplication::BuildShortcuts() const
    {
        std::vector<ShortcutAssignment> shortcuts;
        const auto add = [&shortcuts](ShortcutAction action, bool enabled, const std::wstring& raw, const wchar_t* fallback)
        {
            if (!enabled) return;
            HotkeyGesture gesture;
            std::wstring error;
            if (!TryParseHotkey(raw, gesture, error))
            {
                if (!TryParseHotkey(fallback, gesture, error)) return;
            }
            shortcuts.push_back({ action, std::move(gesture), true });
        };
        add(ShortcutAction::StartScreenSaver, settings_.startScreenSaverHotkeyEnabled,
            settings_.startScreenSaverHotkey, L"Shift+ScrollLock");
        add(ShortcutAction::ToggleScreenSaver, settings_.toggleScreenSaverHotkeyEnabled,
            settings_.toggleScreenSaverHotkey, L"ScrollLock");
        add(ShortcutAction::Sleep, settings_.sleepHotkeyEnabled,
            settings_.sleepHotkey, L"Pause");
        // Keep a configured shortcut registered while S4 is unavailable so a
        // later OS-side enablement takes effect without restarting the app.
        add(ShortcutAction::Hibernate, settings_.hibernateHotkeyEnabled,
            settings_.hibernateHotkey, L"Shift+Pause");
        return shortcuts;
    }

    bool NativeApplication::ValidateSettings(const AppSettings& candidate, std::wstring& error) const
    {
        std::vector<ShortcutAssignment> shortcuts;
        const auto add = [&shortcuts, &error](ShortcutAction action, bool enabled, const std::wstring& text)
        {
            if (!enabled) return true;
            HotkeyGesture gesture;
            if (!TryParseHotkey(text, gesture, error)) return false;
            shortcuts.push_back({ action, std::move(gesture), true });
            return true;
        };
        if (!add(ShortcutAction::StartScreenSaver, candidate.startScreenSaverHotkeyEnabled, candidate.startScreenSaverHotkey) ||
            !add(ShortcutAction::ToggleScreenSaver, candidate.toggleScreenSaverHotkeyEnabled, candidate.toggleScreenSaverHotkey) ||
            !add(ShortcutAction::Sleep, candidate.sleepHotkeyEnabled, candidate.sleepHotkey) ||
            !add(ShortcutAction::Hibernate, candidate.hibernateHotkeyEnabled, candidate.hibernateHotkey))
        {
            error = std::wstring(Text(candidate.language, L"InvalidShortcut"));
            return false;
        }
        if (HasDuplicateShortcuts(shortcuts))
        {
            error = std::wstring(Text(candidate.language, L"InvalidShortcut"));
            return false;
        }
        return true;
    }

    AppSettings NativeApplication::GetSettingsCallback(void* context)
    {
        return static_cast<NativeApplication*>(context)->settings_;
    }

    bool NativeApplication::UpdateSettingsCallback(void* context, const AppSettings& settings, std::wstring& error)
    {
        return static_cast<NativeApplication*>(context)->UpdateSettings(settings, error);
    }

    HibernateAvailability NativeApplication::RefreshHibernateCallback(void* context)
    {
        auto* application = static_cast<NativeApplication*>(context);
        application->RefreshHibernate();
        return application->hibernateAvailability_;
    }

    std::wstring NativeApplication::WindowsSaverPathCallback(void* context)
    {
        return static_cast<NativeApplication*>(context)->screenSaver_.ReadWindowsScreenSaverPath();
    }

    bool NativeApplication::StartSaverCallback(void* context, const std::wstring& path, std::wstring& error)
    {
        auto* application = static_cast<NativeApplication*>(context);
        if (!application->uiPolicy_.allowSaverActions)
        {
            error = L"Screen saver actions are disabled in the safe UI test mode.";
            LogUiDiagnostic(L"settings_screen_saver_action_blocked", application->window_,
                WM_COMMAND, 0, 0, FALSE, ERROR_ACCESS_DENIED, L"safe_mode=1");
            return false;
        }
        LogRuntimeDiagnostic(L"settings_screen_saver_test_requested", TRUE, ERROR_SUCCESS,
            L"custom_option_enabled=" +
                std::to_wstring(application->settings_.useCustomScreenSaver ? 1 : 0) +
            L" path_set=" + std::to_wstring(path.empty() ? 0 : 1));
        return application->StartScreenSaver(path, false, &error);
    }

    void NativeApplication::OpenSaverSettingsCallback(void* context)
    {
        auto* application = static_cast<NativeApplication*>(context);
        if (!application->uiPolicy_.allowSaverActions) return;
        application->screenSaver_.OpenWindowsSettings();
    }

    void NativeApplication::KeyboardSuspendedCallback(void* context, bool suspended)
    {
        auto* application = static_cast<NativeApplication*>(context);
        if (application->keyboard_) application->keyboard_->SetSuspended(suspended);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int)
{
    if (classic_desktop_screensaver::HasCommandLineSwitch(
        commandLine, L"--native-messaging-host"))
    {
        return classic_desktop_screensaver::RunBrowserVideoNativeMessagingHost();
    }

    classic_desktop_screensaver::NativeApplication application;
    std::wstring error;
    if (!application.Initialize(instance, commandLine, error))
    {
        classic_desktop_screensaver::LogUiDiagnostic(L"initialize_failed", nullptr, 0, 0, 0,
            FALSE, ERROR_SUCCESS, error);
        if (!application.IsAlreadyRunning() && !error.empty())
        {
            MessageBoxW(nullptr, error.c_str(), L"Classic Desktop Screensaver", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        }
        return 1;
    }
    return application.Run();
}
