#pragma once

#include <Windows.h>

#include <cstdint>

namespace classic_desktop_screensaver
{
    inline constexpr ULONG_PTR BrowserVideoCopyDataId = 0x43534453u; // "CSD S"
    inline constexpr std::uint32_t BrowserVideoProtocolVersion = 1;
    inline constexpr std::uint32_t BrowserVideoMonitoringFlag = 0x1;
    inline constexpr std::uint32_t BrowserVideoKnownFlags = BrowserVideoMonitoringFlag;
    inline constexpr std::uint32_t BrowserVideoMaximumFrameCount = 1024;
    inline constexpr std::uint64_t BrowserVideoReportMaximumAgeMs = 5000;

    struct BrowserVideoStatusMessage
    {
        std::uint32_t version{};
        std::uint32_t flags{};
        std::uint32_t visibleVideoCount{};
    };

    [[nodiscard]] constexpr bool IsValidBrowserVideoStatus(
        const BrowserVideoStatusMessage& status) noexcept
    {
        return status.version == BrowserVideoProtocolVersion &&
            (status.flags & ~BrowserVideoKnownFlags) == 0 &&
            status.visibleVideoCount <= BrowserVideoMaximumFrameCount &&
            ((status.flags & BrowserVideoMonitoringFlag) != 0 ||
                status.visibleVideoCount == 0);
    }

    [[nodiscard]] constexpr bool IsBrowserVideoReportActive(
        const BrowserVideoStatusMessage& status,
        std::uint64_t receivedAt,
        std::uint64_t now,
        std::uint64_t maximumAge = BrowserVideoReportMaximumAgeMs) noexcept
    {
        return IsValidBrowserVideoStatus(status) &&
            (status.flags & BrowserVideoMonitoringFlag) != 0 &&
            status.visibleVideoCount > 0 && receivedAt != 0 &&
            now >= receivedAt && now - receivedAt <= maximumAge;
    }
}
