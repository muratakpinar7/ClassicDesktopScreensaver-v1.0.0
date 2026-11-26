#include "PowerService.h"

namespace classic_desktop_screensaver
{
    PowerRequestResult RequestSleep(IPowerApi& api) noexcept
    {
        const PowerCapabilitiesSnapshot capabilities = api.QueryCapabilities();
        if (!capabilities.querySucceeded)
        {
            return { PowerRequestStatus::CapabilitiesUnavailable, 0 };
        }

        std::uint32_t errorCode = 0;
        PowerFailureStage failureStage = PowerFailureStage::None;
        if (capabilities.modernStandby)
        {
            const bool accepted = api.RequestDisplayOff(errorCode, failureStage);
            return {
                accepted ? PowerRequestStatus::RequestAccepted
                         : PowerRequestStatus::RequestFailed,
                errorCode,
                failureStage
            };
        }

        if (capabilities.systemS1 || capabilities.systemS2 || capabilities.systemS3)
        {
            const bool accepted = api.RequestSuspend(false, errorCode, failureStage);
            return {
                accepted ? PowerRequestStatus::RequestAccepted
                         : PowerRequestStatus::RequestFailed,
                errorCode,
                failureStage
            };
        }

        return { PowerRequestStatus::SleepUnavailable, 0 };
    }

    PowerRequestResult RequestHibernate(IPowerApi& api) noexcept
    {
        const PowerCapabilitiesSnapshot capabilities = api.QueryCapabilities();
        if (!capabilities.querySucceeded)
        {
            return { PowerRequestStatus::CapabilitiesUnavailable, 0 };
        }

        const HibernateAvailability availability = EvaluateHibernateAvailability({
            capabilities.querySucceeded,
            capabilities.systemS4,
            capabilities.hiberFilePresent
        });
        if (availability != HibernateAvailability::Available)
        {
            return { PowerRequestStatus::HibernateUnavailable, 0 };
        }

        std::uint32_t errorCode = 0;
        PowerFailureStage failureStage = PowerFailureStage::None;
        const bool accepted = api.RequestSuspend(true, errorCode, failureStage);
        return {
            accepted ? PowerRequestStatus::RequestAccepted
                     : PowerRequestStatus::RequestFailed,
            errorCode,
            failureStage
        };
    }
}
