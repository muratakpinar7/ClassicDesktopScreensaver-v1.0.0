#pragma once

#include "PowerCapabilities.h"

#include <cstdint>

namespace classic_desktop_screensaver
{
    struct PowerCapabilitiesSnapshot
    {
        bool querySucceeded{};
        bool modernStandby{};
        bool systemS1{};
        bool systemS2{};
        bool systemS3{};
        bool systemS4{};
        bool hiberFilePresent{};
    };

    enum class PowerRequestStatus : std::uint8_t
    {
        RequestAccepted,
        RequestFailed,
        CapabilitiesUnavailable,
        SleepUnavailable,
        HibernateUnavailable
    };

    enum class PowerFailureStage : std::uint8_t
    {
        None,
        DisplayOffRequest,
        OpenProcessToken,
        LookupShutdownPrivilege,
        AdjustShutdownPrivilege,
        SetSuspendState
    };

    struct PowerRequestResult
    {
        PowerRequestStatus status{ PowerRequestStatus::RequestFailed };
        std::uint32_t errorCode{};
        PowerFailureStage failureStage{ PowerFailureStage::None };

        [[nodiscard]] bool RequestWasAccepted() const noexcept
        {
            return status == PowerRequestStatus::RequestAccepted;
        }
    };

    class IPowerApi
    {
    public:
        virtual ~IPowerApi() = default;
        [[nodiscard]] virtual PowerCapabilitiesSnapshot QueryCapabilities() noexcept = 0;
        [[nodiscard]] virtual bool RequestDisplayOff(
            std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept = 0;
        [[nodiscard]] virtual bool RequestSuspend(
            bool hibernate,
            std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept = 0;
    };

    [[nodiscard]] PowerRequestResult RequestSleep(IPowerApi& api) noexcept;
    [[nodiscard]] PowerRequestResult RequestHibernate(IPowerApi& api) noexcept;
}
