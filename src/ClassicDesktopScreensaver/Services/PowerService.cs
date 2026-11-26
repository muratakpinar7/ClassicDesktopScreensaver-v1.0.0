using System.Runtime.InteropServices;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed class PowerService
{
    private const string ShutdownPrivilege = "SeShutdownPrivilege";

    public bool Sleep(out string? error)
    {
        error = null;

        if (!TryGetSleepModes(out bool modernStandby, out bool classicSleep))
        {
            error = LocalizationService.T("SleepCapabilitiesUnavailable");
            return false;
        }

        if (modernStandby)
            return RequestModernStandbyDisplayOff(out error);

        if (classicSleep)
            return Suspend(hibernate: false, out error);

        error = LocalizationService.T("SleepUnavailable");
        return false;
    }

    public bool Hibernate(out string? error)
    {
        error = null;

        if (!NativeMethods.IsPwrHibernateAllowed())
        {
            error = LocalizationService.T("HibernateUnavailable");
            return false;
        }

        return Suspend(hibernate: true, out error);
    }

    private static bool RequestModernStandbyDisplayOff(out string? error)
    {
        error = null;

        nint apiResult = NativeMethods.SendMessageTimeoutW(
            NativeMethods.HWND_BROADCAST,
            NativeMethods.WM_SYSCOMMAND,
            NativeMethods.SC_MONITORPOWER,
            NativeMethods.MONITOR_POWER_OFF,
            NativeMethods.SMTO_ABORTIFHUNG | NativeMethods.SMTO_ERRORONEXIT,
            NativeMethods.MONITOR_POWER_RECIPIENT_TIMEOUT_MS,
            out _);

        if (apiResult != 0)
        {
            // USER32 accepted/completed the broadcast call; this does not prove
            // that Windows proceeded from Screen Off into Modern Standby Sleep.
            return true;
        }

        error = LocalizationService.F(
            "MonitorPowerRequestFailed", Marshal.GetLastWin32Error());
        return false;
    }

    private static bool TryGetSleepModes(
        out bool modernStandby, out bool classicSleep)
    {
        modernStandby = false;
        classicSleep = false;

        if (!NativeMethods.GetPwrCapabilities(out var capabilities))
            return false;

        classicSleep = capabilities.SystemS1 != 0 ||
                       capabilities.SystemS2 != 0 ||
                       capabilities.SystemS3 != 0;
        modernStandby = capabilities.AoAc != 0;
        return true;
    }

    private static bool Suspend(bool hibernate, out string? error)
    {
        error = null;

        if (!EnableShutdownPrivilege(out error))
            return false;

        if (!NativeMethods.SetSuspendState(
                hibernate, forceCritical: false, disableWakeEvent: false))
        {
            error = LocalizationService.F(
                "PowerChangeFailed", Marshal.GetLastWin32Error());
            return false;
        }

        return true;
    }

    private static bool EnableShutdownPrivilege(out string? error)
    {
        error = null;

        if (!NativeMethods.OpenProcessToken(
                NativeMethods.GetCurrentProcess(),
                NativeMethods.TOKEN_ADJUST_PRIVILEGES | NativeMethods.TOKEN_QUERY,
                out IntPtr token))
        {
            error = LocalizationService.T("PowerPrivilegeFailed");
            return false;
        }

        try
        {
            if (!NativeMethods.LookupPrivilegeValue(
                    null, ShutdownPrivilege, out var luid))
            {
                error = LocalizationService.T("PowerPrivilegeMissing");
                return false;
            }

            var privileges = new NativeMethods.TOKEN_PRIVILEGES
            {
                PrivilegeCount = 1,
                Privileges = new NativeMethods.LUID_AND_ATTRIBUTES
                {
                    Luid = luid,
                    Attributes = NativeMethods.SE_PRIVILEGE_ENABLED
                }
            };

            if (!NativeMethods.AdjustTokenPrivileges(
                    token, disableAllPrivileges: false, ref privileges,
                    0, IntPtr.Zero, IntPtr.Zero))
            {
                error = LocalizationService.T("PowerPrivilegeEnableFailed");
                return false;
            }

            if (Marshal.GetLastWin32Error() == NativeMethods.ERROR_NOT_ALL_ASSIGNED)
            {
                error = LocalizationService.T("PowerPrivilegeDenied");
                return false;
            }

            return true;
        }
        finally
        {
            NativeMethods.CloseHandle(token);
        }
    }
}
