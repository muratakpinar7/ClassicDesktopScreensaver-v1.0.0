#pragma once

#include <Windows.h>

namespace classic_desktop_screensaver
{
    struct IScreenSaverActivationApi
    {
        virtual ~IScreenSaverActivationApi() = default;
        [[nodiscard]] virtual bool Query(bool& active, DWORD& error) noexcept = 0;
        [[nodiscard]] virtual bool QuerySecure(bool& secure, DWORD& error) noexcept = 0;
        [[nodiscard]] virtual bool QueryPolicyManaged(bool& managed, DWORD& error) noexcept = 0;
        [[nodiscard]] virtual bool Set(bool active, DWORD& error) noexcept = 0;
    };

    struct ScreenSaverLeaseResult
    {
        bool succeeded{};
        bool querySucceeded{};
        bool changed{};
        bool active{};
        DWORD error{};
        bool securityProtected{};
        bool policyManaged{};
    };

    // Temporarily suppresses the Windows screen saver for this application
    // lifetime. The caller supplies the platform API so the state machine can
    // be regression-tested without changing the desktop's real setting.
    class ScreenSaverActivationLease final
    {
    public:
        [[nodiscard]] ScreenSaverLeaseResult Begin(IScreenSaverActivationApi& api) noexcept
        {
            if (abandoned_)
            {
                return { true, false, false, originalActive_, ERROR_SUCCESS };
            }
            if (acquired_)
            {
                return { true, true, false, false, ERROR_SUCCESS };
            }

            bool secure = false;
            DWORD error = ERROR_SUCCESS;
            if (!api.QuerySecure(secure, error))
            {
                return { false, false, false, false, error };
            }

            bool active = false;
            if (!api.Query(active, error))
            {
                return { false, false, false, false, error };
            }

            originalActive_ = active;
            if (secure)
            {
                return { true, true, false, active, ERROR_SUCCESS, true };
            }

            bool policyManaged = false;
            if (!api.QueryPolicyManaged(policyManaged, error))
            {
                return { false, false, false, active, error };
            }
            if (policyManaged)
            {
                return { true, true, false, active, ERROR_SUCCESS, false, true };
            }

            acquired_ = true;
            if (!active)
            {
                return { true, true, false, false, ERROR_SUCCESS };
            }

            if (!api.Set(false, error))
            {
                acquired_ = false;
                return { false, true, false, true, error };
            }
            return { true, true, true, false, ERROR_SUCCESS };
        }

        [[nodiscard]] ScreenSaverLeaseResult Reassert(IScreenSaverActivationApi& api) noexcept
        {
            if (abandoned_)
            {
                return { true, false, false, originalActive_, ERROR_SUCCESS };
            }
            if (!acquired_)
            {
                // Re-evaluate after a settings notification: a protected saver
                // may have become unprotected, or vice versa.
                return Begin(api);
            }

            bool secure = false;
            DWORD error = ERROR_SUCCESS;
            if (!api.QuerySecure(secure, error))
            {
                return RestoreAfterQueryFailure(api, error);
            }
            if (secure)
            {
                ScreenSaverLeaseResult restored = Restore(api);
                restored.securityProtected = true;
                return restored;
            }

            bool policyManaged = false;
            if (!api.QueryPolicyManaged(policyManaged, error))
            {
                return RestoreAfterQueryFailure(api, error);
            }
            if (policyManaged)
            {
                ScreenSaverLeaseResult restored = Restore(api);
                restored.policyManaged = true;
                return restored;
            }

            bool active = false;
            if (!api.Query(active, error))
            {
                return RestoreAfterQueryFailure(api, error);
            }
            if (!active)
            {
                return { true, true, false, false, ERROR_SUCCESS };
            }
            if (!api.Set(false, error))
            {
                return { false, true, false, true, error };
            }
            return { true, true, true, false, ERROR_SUCCESS };
        }

        [[nodiscard]] ScreenSaverLeaseResult Restore(IScreenSaverActivationApi& api) noexcept
        {
            if (!acquired_)
            {
                return { true, false, false, originalActive_, ERROR_SUCCESS, false };
            }

            bool active = false;
            DWORD error = ERROR_SUCCESS;
            if (!api.Query(active, error))
            {
                // Begin captured the original state. If the current value is
                // unreadable, still make a best-effort restoration.
                DWORD setError = ERROR_SUCCESS;
                if (!api.Set(originalActive_, setError))
                {
                    return { false, false, false, false, setError };
                }
                acquired_ = false;
                return { true, false, true, originalActive_, ERROR_SUCCESS };
            }
            if (active == originalActive_)
            {
                acquired_ = false;
                return { true, true, false, active, ERROR_SUCCESS };
            }
            if (!api.Set(originalActive_, error))
            {
                return { false, true, false, active, error };
            }
            acquired_ = false;
            return { true, true, true, originalActive_, ERROR_SUCCESS };
        }

        [[nodiscard]] bool Acquired() const noexcept { return acquired_; }
        [[nodiscard]] bool OriginalActive() const noexcept { return originalActive_; }
        void Abandon() noexcept
        {
            acquired_ = false;
            abandoned_ = true;
        }

    private:
        [[nodiscard]] ScreenSaverLeaseResult RestoreAfterQueryFailure(
            IScreenSaverActivationApi& api,
            DWORD queryError) noexcept
        {
            ScreenSaverLeaseResult restored = Restore(api);
            if (!restored.succeeded) return restored;
            restored.succeeded = false;
            restored.querySucceeded = false;
            restored.error = queryError;
            return restored;
        }

        bool acquired_{};
        bool originalActive_{};
        bool abandoned_{};
    };
}
