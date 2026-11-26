#include "GuardService.h"
#include "..\Core\RuntimePolicy.h"

#include <Windows.h>
#include <Shellapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>

#include <cstdlib>
#include <chrono>
#include <iterator>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "WindowsApp.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        bool IsPresentationMode() noexcept
        {
            QUERY_USER_NOTIFICATION_STATE state{};
            const HRESULT result = SHQueryUserNotificationState(&state);
            return SUCCEEDED(result) &&
                (state == QUNS_PRESENTATION_MODE || state == QUNS_RUNNING_D3D_FULL_SCREEN);
        }

        bool IsForegroundWindowFullscreen() noexcept
        {
            const HWND foreground = GetForegroundWindow();
            if (foreground == nullptr || IsIconic(foreground) != FALSE)
            {
                return false;
            }

            wchar_t className[128]{};
            const int classLength = GetClassNameW(foreground, className, static_cast<int>(std::size(className)));
            if (classLength > 0 &&
                (_wcsicmp(className, L"Progman") == 0 ||
                    _wcsicmp(className, L"WorkerW") == 0 ||
                    _wcsicmp(className, L"Shell_TrayWnd") == 0))
            {
                return false;
            }

            RECT windowRect{};
            if (GetWindowRect(foreground, &windowRect) == FALSE)
            {
                return false;
            }

            const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{ sizeof(info) };
            if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
            {
                return false;
            }

            constexpr LONG tolerance = 3;
            const RECT& bounds = info.rcMonitor;
            return std::abs(windowRect.left - bounds.left) <= tolerance &&
                std::abs(windowRect.top - bounds.top) <= tolerance &&
                std::abs(windowRect.right - bounds.right) <= tolerance &&
                std::abs(windowRect.bottom - bounds.bottom) <= tolerance;
        }

        void QueryMediaPlayback(GuardStatus& result) noexcept
        {
            using namespace winrt::Windows::Media;
            using namespace winrt::Windows::Media::Control;
            try
            {
                auto request = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
                const auto requestStatus = request.wait_for(std::chrono::seconds(2));
                if (requestStatus != winrt::Windows::Foundation::AsyncStatus::Completed)
                {
                    result.mediaQueryState = requestStatus == winrt::Windows::Foundation::AsyncStatus::Started
                        ? MediaQueryState::TimedOut
                        : MediaQueryState::Failed;
                    if (requestStatus == winrt::Windows::Foundation::AsyncStatus::Error)
                    {
                        result.mediaQueryHresult = static_cast<long>(request.ErrorCode());
                    }
                    return;
                }
                const auto manager = request.GetResults();
                for (const auto& session : manager.GetSessions())
                {
                    result.mediaSessionAvailable = true;
                    const auto info = session.GetPlaybackInfo();
                    if (info.PlaybackStatus() != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                    {
                        continue;
                    }

                    const auto type = info.PlaybackType();
                    if (type && type.Value() == MediaPlaybackType::Video)
                    {
                        result.videoPlaying = true;
                    }
                    else if (type && type.Value() == MediaPlaybackType::Music)
                    {
                        result.musicPlaying = true;
                    }
                    else if (!type)
                    {
                        result.unknownTypePlaying = true;
                    }
                }
                result.mediaQueryState = MediaQueryState::Succeeded;
            }
            catch (const winrt::hresult_error& error)
            {
                result.videoPlaying = false;
                result.musicPlaying = false;
                result.unknownTypePlaying = false;
                result.mediaQueryState = MediaQueryState::Failed;
                result.mediaQueryHresult = static_cast<long>(error.code());
            }
            catch (...)
            {
                result.videoPlaying = false;
                result.musicPlaying = false;
                result.unknownTypePlaying = false;
                result.mediaQueryState = MediaQueryState::Failed;
            }
        }
    }

    GuardStatus QueryGuards(bool checkMedia, bool checkFullscreen) noexcept
    {
        GuardStatus result{};
        if (checkMedia)
        {
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                QueryMediaPlayback(result);
                winrt::uninit_apartment();
            }
            catch (const winrt::hresult_error& error)
            {
                result.videoPlaying = false;
                result.mediaQueryState = MediaQueryState::Failed;
                result.mediaQueryHresult = static_cast<long>(error.code());
            }
            catch (...)
            {
                result.videoPlaying = false;
                result.mediaQueryState = MediaQueryState::Failed;
            }
        }
        if (checkFullscreen)
        {
            result.presentationMode = IsPresentationMode();
            result.fullScreenApplication = IsForegroundWindowFullscreen();
        }
        return result;
    }
}
