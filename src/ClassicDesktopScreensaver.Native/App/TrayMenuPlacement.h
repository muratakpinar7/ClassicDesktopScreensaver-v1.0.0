#pragma once

#include <Windows.h>

namespace classic_desktop_screensaver
{
    struct TrayMenuPlacement
    {
        POINT anchor{};
        UINT alignmentFlags{};
    };

    [[nodiscard]] constexpr TrayMenuPlacement CalculateTrayMenuPlacement(
        const RECT& icon,
        const RECT& monitor,
        const RECT& workArea,
        LONG estimatedMenuHeight) noexcept
    {
        constexpr LONG MinimumMenuHeight = 1;
        const LONG menuHeight = estimatedMenuHeight < MinimumMenuHeight
            ? MinimumMenuHeight
            : estimatedMenuHeight;
        const LONG monitorCenterX = monitor.left + (monitor.right - monitor.left) / 2;
        const LONG workCenterX = workArea.left + (workArea.right - workArea.left) / 2;
        const LONG workCenterY = workArea.top + (workArea.bottom - workArea.top) / 2;
        const LONG iconCenterX = icon.left + (icon.right - icon.left) / 2;
        const LONG iconCenterY = icon.top + (icon.bottom - icon.top) / 2;

        const bool taskbarBottom = icon.top >= workArea.bottom;
        const bool taskbarTop = icon.bottom <= workArea.top;
        const bool taskbarLeft = icon.right <= workArea.left;
        const bool taskbarRight = icon.left >= workArea.right;

        TrayMenuPlacement placement{};
        placement.alignmentFlags = TPM_WORKAREA;

        if (taskbarBottom)
        {
            placement.anchor.y = icon.top;
            placement.alignmentFlags |= TPM_BOTTOMALIGN;
        }
        else if (taskbarTop)
        {
            placement.anchor.y = icon.bottom;
            placement.alignmentFlags |= TPM_TOPALIGN;
        }
        else if (taskbarLeft || taskbarRight)
        {
            const LONG roomAbove = icon.top - workArea.top;
            const LONG roomBelow = workArea.bottom - icon.bottom;
            if (iconCenterY > workCenterY && roomAbove >= menuHeight)
            {
                placement.anchor.y = icon.top;
                placement.alignmentFlags |= TPM_BOTTOMALIGN;
            }
            else if (roomBelow >= menuHeight)
            {
                placement.anchor.y = icon.bottom;
                placement.alignmentFlags |= TPM_TOPALIGN;
            }
            else if (roomAbove > roomBelow)
            {
                placement.anchor.y = icon.top;
                placement.alignmentFlags |= TPM_BOTTOMALIGN;
            }
            else
            {
                placement.anchor.y = icon.bottom;
                placement.alignmentFlags |= TPM_TOPALIGN;
            }
        }
        else
        {
            const LONG roomAbove = icon.top - workArea.top;
            const LONG roomBelow = workArea.bottom - icon.bottom;
            if (roomBelow >= menuHeight || roomBelow >= roomAbove)
            {
                placement.anchor.y = icon.bottom;
                placement.alignmentFlags |= TPM_TOPALIGN;
            }
            else
            {
                placement.anchor.y = icon.top;
                placement.alignmentFlags |= TPM_BOTTOMALIGN;
            }
        }

        if (taskbarLeft)
        {
            placement.anchor.x = icon.right;
            placement.alignmentFlags |= TPM_LEFTALIGN;
        }
        else if (taskbarRight)
        {
            placement.anchor.x = icon.left;
            placement.alignmentFlags |= TPM_RIGHTALIGN;
        }
        else if (iconCenterX >= workCenterX || iconCenterX >= monitorCenterX)
        {
            placement.anchor.x = icon.right;
            placement.alignmentFlags |= TPM_RIGHTALIGN;
        }
        else
        {
            placement.anchor.x = icon.left;
            placement.alignmentFlags |= TPM_LEFTALIGN;
        }

        return placement;
    }
}
