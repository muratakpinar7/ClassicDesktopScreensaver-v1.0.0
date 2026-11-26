#pragma once

#include <Windows.h>
#include <cstdint>

namespace classic_desktop_screensaver
{
    enum class HibernateAvailability : std::uint8_t
    {
        Available,
        Unsupported,
        QueryFailed
    };

    struct HibernateCapabilities
    {
        bool querySucceeded{};
        bool systemS4{};
        bool hiberFilePresent{};
        DWORD queryError{};
    };

    // SystemS4 and HiberFilePresent represent distinct capability facts.
    // A query failure is intentionally not treated as Hibernate support.
    [[nodiscard]] constexpr HibernateAvailability EvaluateHibernateAvailability(
        const HibernateCapabilities& capabilities) noexcept
    {
        if (!capabilities.querySucceeded)
        {
            return HibernateAvailability::QueryFailed;
        }

        return capabilities.systemS4 && capabilities.hiberFilePresent
            ? HibernateAvailability::Available
            : HibernateAvailability::Unsupported;
    }

    [[nodiscard]] HibernateCapabilities QueryHibernateCapabilities() noexcept;
    [[nodiscard]] HibernateAvailability QueryHibernateAvailability() noexcept;
}
