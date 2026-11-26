#pragma once

#include <Windows.h>
#include <Windowsx.h>
#include <Shellapi.h>

namespace classic_desktop_screensaver
{
    struct TrayNotification
    {
        UINT iconId{};
        UINT eventCode{};
        POINT anchor{};
        bool hasAnchor{};
    };

    [[nodiscard]] constexpr bool IsTrayContextMenuEvent(
        UINT eventCode,
        bool version4) noexcept
    {
        return eventCode == NIN_KEYSELECT ||
            (version4 ? eventCode == WM_CONTEXTMENU : eventCode == WM_RBUTTONUP);
    }

    [[nodiscard]] constexpr bool IsKnownTrayCallbackEvent(UINT eventCode) noexcept
    {
        if (eventCode >= WM_MOUSEFIRST && eventCode <= WM_MOUSELAST) return true;
        switch (eventCode)
        {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case NIN_BALLOONSHOW:
        case NIN_BALLOONHIDE:
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONUSERCLICK:
        case NIN_POPUPOPEN:
        case NIN_POPUPCLOSE:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] inline bool TryDecodeTrayCallbackFormat(
        WPARAM wParam,
        LPARAM lParam,
        bool version4,
        UINT expectedIconId,
        TrayNotification& notification) noexcept
    {
        TrayNotification candidate{};
        if (version4)
        {
            // NOTIFYICON_VERSION_4 moves the icon id and event into lParam;
            // wParam carries signed cursor coordinates.
            const UINT_PTR payload = static_cast<UINT_PTR>(lParam);
            candidate.eventCode = LOWORD(payload);
            candidate.iconId = HIWORD(payload);
            candidate.anchor = { GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) };
            candidate.hasAnchor = true;
        }
        else
        {
            candidate.iconId = static_cast<UINT>(wParam);
            candidate.eventCode = static_cast<UINT>(lParam);
        }

        if (candidate.iconId != expectedIconId ||
            !IsKnownTrayCallbackEvent(candidate.eventCode))
        {
            return false;
        }

        notification = candidate;
        return true;
    }

    [[nodiscard]] inline bool TryDecodeTrayCallback(
        WPARAM wParam,
        LPARAM lParam,
        bool preferVersion4,
        UINT expectedIconId,
        TrayNotification& notification) noexcept
    {
        return TryDecodeTrayCallbackFormat(wParam, lParam, preferVersion4,
            expectedIconId, notification) ||
            TryDecodeTrayCallbackFormat(wParam, lParam, !preferVersion4,
                expectedIconId, notification);
    }
}
