#pragma once

namespace classic_desktop_screensaver
{
    enum class MediaQueryState : unsigned char
    {
        NotRequested,
        Succeeded,
        TimedOut,
        Failed
    };

    struct GuardStatus
    {
        // True only for an active GSMTC session explicitly classified as video.
        bool videoPlaying{};
        bool mediaSessionAvailable{};
        bool musicPlaying{};
        bool unknownTypePlaying{};
        MediaQueryState mediaQueryState{ MediaQueryState::NotRequested };
        long mediaQueryHresult{};
        bool fullScreenApplication{};
        bool presentationMode{};
        [[nodiscard]] bool Any() const noexcept
        {
            return videoPlaying || fullScreenApplication || presentationMode;
        }
    };

    [[nodiscard]] GuardStatus QueryGuards(bool checkMedia, bool checkFullscreen) noexcept;
}
