#pragma once

#include "AppSettings.h"

#include <cstdint>

namespace classic_desktop_screensaver
{
    struct RuntimeGuardSnapshot
    {
        bool videoPlaying{};
        bool fullScreenApplication{};
        bool presentationMode{};
        [[nodiscard]] constexpr bool Any() const noexcept
        {
            return videoPlaying || fullScreenApplication || presentationMode;
        }
    };

    [[nodiscard]] constexpr bool IsBlockingMediaPlayback(
        bool playing, bool playbackTypeKnown, bool video) noexcept
    {
        // Unknown sessions and audio-only playback are not evidence of video.
        // Only an active session explicitly identified as video blocks the saver.
        return playing && playbackTypeKnown && video;
    }

    [[nodiscard]] constexpr std::uint32_t NextGuardQueryRevision(
        std::uint32_t current) noexcept
    {
        ++current;
        return current == 0 ? 1 : current;
    }

    [[nodiscard]] constexpr bool IsGuardQueryResultCurrent(
        std::uint32_t resultRevision,
        std::uint32_t pendingRevision,
        std::uint32_t currentRevision) noexcept
    {
        return resultRevision != 0 && resultRevision == pendingRevision &&
            resultRevision == currentRevision;
    }

    [[nodiscard]] constexpr bool IsHotCornerDelayDue(
        bool inside, bool alreadyTriggered,
        std::uint64_t elapsedMilliseconds, int configuredDelayMilliseconds) noexcept
    {
        return inside && !alreadyTriggered && configuredDelayMilliseconds >= 0 &&
            elapsedMilliseconds >= static_cast<std::uint64_t>(configuredDelayMilliseconds);
    }

    [[nodiscard]] constexpr bool IsAutoScreenSaverDue(
        bool enabled, bool alreadyFired, std::uint64_t idleMilliseconds,
        int idleMinutes) noexcept
    {
        return enabled && !alreadyFired && idleMinutes > 0 &&
            idleMilliseconds >= static_cast<std::uint64_t>(idleMinutes) * 60'000;
    }

    [[nodiscard]] constexpr bool IsAutoLockDue(
        bool enabled, bool alreadyFired, std::uint64_t idleMilliseconds,
        int idleMinutes) noexcept
    {
        return enabled && !alreadyFired && idleMinutes > 0 &&
            idleMilliseconds >= static_cast<std::uint64_t>(idleMinutes) * 60'000;
    }

    [[nodiscard]] constexpr std::uint64_t EffectiveIdleMilliseconds(
        std::uint64_t systemIdleMilliseconds,
        std::uint64_t applicationIdleMilliseconds) noexcept
    {
        return systemIdleMilliseconds < applicationIdleMilliseconds
            ? systemIdleMilliseconds
            : applicationIdleMilliseconds;
    }

    [[nodiscard]] constexpr bool IsIdleTimerGap(
        std::uint64_t previousTick,
        std::uint64_t currentTick,
        std::uint64_t resumeGapThresholdMilliseconds) noexcept
    {
        return previousTick != 0 && currentTick >= previousTick &&
            currentTick - previousTick >= resumeGapThresholdMilliseconds;
    }

    [[nodiscard]] constexpr bool IsScreenSaverBlocked(
        bool blockMedia, bool blockFullscreen,
        const RuntimeGuardSnapshot& guards) noexcept
    {
        return (blockMedia && guards.videoPlaying) ||
            (blockFullscreen && (guards.fullScreenApplication || guards.presentationMode));
    }

    [[nodiscard]] constexpr bool IsAutomaticLockBlocked(
        bool suspendDuringMediaOrPresentation,
        const RuntimeGuardSnapshot& guards) noexcept
    {
        return suspendDuringMediaOrPresentation && guards.Any();
    }

    [[nodiscard]] constexpr bool IsPointInHotCorner(
        HotCorner corner, int x, int y,
        int left, int top, int right, int bottom,
        int edge = 2) noexcept
    {
        switch (corner)
        {
        case HotCorner::TopLeft: return x <= left + edge && y <= top + edge;
        case HotCorner::TopRight: return x >= right - 1 - edge && y <= top + edge;
        case HotCorner::BottomLeft: return x <= left + edge && y >= bottom - 1 - edge;
        case HotCorner::BottomRight: return x >= right - 1 - edge && y >= bottom - 1 - edge;
        }
        return false;
    }
}
