$ErrorActionPreference = 'SilentlyContinue'

$source = @'
using System;
using System.Runtime.InteropServices;

public static class ConsoleFont
{
    [StructLayout(LayoutKind.Sequential)]
    public struct COORD
    {
        public short X;
        public short Y;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct CONSOLE_FONT_INFOEX
    {
        public uint cbSize;
        public uint nFont;
        public COORD dwFontSize;
        public int FontFamily;
        public int FontWeight;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string FaceName;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr GetStdHandle(int nStdHandle);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetCurrentConsoleFontEx(
        IntPtr hConsoleOutput,
        bool bMaximumWindow,
        ref CONSOLE_FONT_INFOEX lpConsoleCurrentFontEx);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetCurrentConsoleFontEx(
        IntPtr hConsoleOutput,
        bool bMaximumWindow,
        ref CONSOLE_FONT_INFOEX lpConsoleCurrentFontEx);
}
'@

Add-Type -TypeDefinition $source -Language CSharp -ErrorAction SilentlyContinue | Out-Null

$STD_OUTPUT_HANDLE = -11
$handle = [ConsoleFont]::GetStdHandle($STD_OUTPUT_HANDLE)

$info = New-Object ConsoleFont+CONSOLE_FONT_INFOEX
$info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)

if ([ConsoleFont]::GetCurrentConsoleFontEx($handle, $false, [ref]$info)) {
    $info.dwFontSize.Y = 18
    $info.FontWeight = 400
    $info.FaceName = 'Consolas'
    [void][ConsoleFont]::SetCurrentConsoleFontEx($handle, $false, [ref]$info)
}
