#pragma once

#include "..\Core\PowerService.h"

namespace classic_desktop_screensaver
{
    class Win32PowerApi final : public IPowerApi
    {
    public:
        [[nodiscard]] PowerCapabilitiesSnapshot QueryCapabilities() noexcept override;
        [[nodiscard]] bool RequestDisplayOff(
            std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept override;
        [[nodiscard]] bool RequestSuspend(
            bool hibernate,
            std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept override;
    };
}
