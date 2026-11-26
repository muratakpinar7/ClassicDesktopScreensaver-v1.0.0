#include "PowerCapabilities.h"

#include <PowrProf.h>

#pragma comment(lib, "PowrProf.lib")

namespace classic_desktop_screensaver
{
    HibernateCapabilities QueryHibernateCapabilities() noexcept
    {
        SYSTEM_POWER_CAPABILITIES nativeCapabilities{};
        if (GetPwrCapabilities(&nativeCapabilities) == FALSE)
        {
            return { .querySucceeded = false, .queryError = GetLastError() };
        }

        return {
            .querySucceeded = true,
            .systemS4 = nativeCapabilities.SystemS4 != FALSE,
            .hiberFilePresent = nativeCapabilities.HiberFilePresent != FALSE
        };
    }

    HibernateAvailability QueryHibernateAvailability() noexcept
    {
        return EvaluateHibernateAvailability(QueryHibernateCapabilities());
    }
}
