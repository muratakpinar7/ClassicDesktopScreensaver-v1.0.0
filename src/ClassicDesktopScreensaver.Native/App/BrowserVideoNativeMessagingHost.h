#pragma once

namespace classic_desktop_screensaver
{
    // Entry used only when a supported browser launches this same EXE as its
    // optional Native Messaging host. It never initializes the application UI.
    [[nodiscard]] int RunBrowserVideoNativeMessagingHost() noexcept;
}
