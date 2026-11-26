using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
using ClassicDesktopScreensaver.Core;

namespace ClassicDesktopScreensaver.Services;

public sealed class GlobalKeyboardHook : IDisposable
{
    private const int VkPause = 0x13;
    private const int VkShift = 0x10;
    private const int VkControl = 0x11;
    private const int VkMenu = 0x12;
    private const int VkLShift = 0xA0;
    private const int VkRShift = 0xA1;
    private const int VkLControl = 0xA2;
    private const int VkRControl = 0xA3;
    private const int VkLMenu = 0xA4;
    private const int VkRMenu = 0xA5;
    private const int VkLWin = 0x5B;
    private const int VkRWin = 0x5C;
    private const uint LlkhfLowerIlInjected = 0x02;
    private const uint LlkhfInjected = 0x10;
    private const string ConsoleWindowClass = "ConsoleWindowClass";
    private const string WindowsTerminalWindowClass = "CASCADIA_HOSTING_WINDOW_CLASS";

    private readonly NativeMethods.LowLevelKeyboardProc _proc;
    private IntPtr _hook = IntPtr.Zero;

    private readonly Dictionary<ShortcutAction, HotkeyGesture> _shortcuts = new();
    private readonly HashSet<int> _pressedKeys = new();
    private readonly HashSet<int> _suppressedKeys = new();

    private bool _suspended;

    public bool Suspended
    {
        get => _suspended;
        set
        {
            if (_suspended == value)
                return;

            _suspended = value;
            ResetTransientState();
        }
    }

    public event Action<ShortcutAction>? ShortcutPressed;

    public GlobalKeyboardHook()
    {
        _proc = HookCallback;
        Install();
    }

    public void SetShortcuts(Dictionary<ShortcutAction, HotkeyGesture> shortcuts)
    {
        _shortcuts.Clear();
        foreach (var pair in shortcuts)
            _shortcuts[pair.Key] = pair.Value;

        ResetTransientState();
    }

    /// <summary>
    /// Clears key-down/modifier/suppression state that is only valid for the
    /// current input session. Windows can omit key-up messages when the system
    /// enters sleep/hibernate, especially for Pause/Break and held modifiers.
    /// </summary>
    public void ResetTransientState()
    {
        _pressedKeys.Clear();
        _suppressedKeys.Clear();
    }

    private void Install()
    {
        using Process currentProcess = Process.GetCurrentProcess();
        using ProcessModule? module = currentProcess.MainModule;

        IntPtr moduleHandle = NativeMethods.GetModuleHandle(module?.ModuleName);
        _hook = NativeMethods.SetWindowsHookEx(
            NativeMethods.WH_KEYBOARD_LL,
            _proc,
            moduleHandle,
            0);

        if (_hook == IntPtr.Zero)
            throw new InvalidOperationException("Global klavye kancası kurulamadı.");
    }

    private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode < 0)
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        int message = wParam.ToInt32();
        bool isKeyDown = message is NativeMethods.WM_KEYDOWN or NativeMethods.WM_SYSKEYDOWN;
        bool isKeyUp = message is NativeMethods.WM_KEYUP or NativeMethods.WM_SYSKEYUP;

        if (!isKeyDown && !isKeyUp)
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        var data = Marshal.PtrToStructure<NativeMethods.KBDLLHOOKSTRUCT>(lParam);
        int vk = unchecked((int)data.vkCode);

        if (Suspended)
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        if (isKeyUp)
        {
            _pressedKeys.Remove(vk);

            if (_suppressedKeys.Remove(vk))
                return new IntPtr(1);

            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        bool firstPress = _pressedKeys.Add(vk);

        // Modifier keys are tracked from the actual hook stream instead of
        // GetAsyncKeyState. This prevents a pre-hibernate Shift state from
        // being mistaken for a newly held Shift after resume.
        if (IsModifierVirtualKey(vk))
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        ShortcutAction? matchedAction = FindMatch(vk);
        if (matchedAction is null)
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        // Preserve the terminal's bare Pause key. Keep this tied to the
        // physical, unmodified VK_PAUSE so remapped shortcuts and Shift+Pause
        // retain their configured actions.
        if (ShouldPassBarePauseThroughTerminal(matchedAction.Value, vk, data))
            return NativeMethods.CallNextHookEx(_hook, nCode, wParam, lParam);

        _suppressedKeys.Add(vk);

        if (firstPress)
        {
            Application.Current.Dispatcher.BeginInvoke(
                () => ShortcutPressed?.Invoke(matchedAction.Value));
        }

        // Suppress configured shortcuts so Scroll Lock/Pause keep their
        // application-command meaning rather than changing normal key state.
        return new IntPtr(1);
    }

    private ShortcutAction? FindMatch(int virtualKey)
    {
        Key pressedKey = KeyInterop.KeyFromVirtualKey(virtualKey);
        ModifierKeys modifiers = GetTrackedModifiers();

        foreach (var pair in _shortcuts)
        {
            if (pair.Value.Key == pressedKey && pair.Value.Modifiers == modifiers)
                return pair.Key;
        }

        return null;
    }

    private bool ShouldPassBarePauseThroughTerminal(
        ShortcutAction action,
        int virtualKey,
        NativeMethods.KBDLLHOOKSTRUCT data)
    {
        if (action is not (ShortcutAction.Sleep or ShortcutAction.Hibernate) ||
            virtualKey != VkPause ||
            GetTrackedModifiers() != ModifierKeys.None ||
            (data.flags & (LlkhfInjected | LlkhfLowerIlInjected)) != 0)
            return false;

        IntPtr foregroundWindow = NativeMethods.GetForegroundWindow();
        if (foregroundWindow == IntPtr.Zero)
            return false;

        var className = new StringBuilder(128);
        int length = NativeMethods.GetClassName(
            foregroundWindow, className, className.Capacity);
        if (length <= 0)
            return false;

        return className.ToString() is ConsoleWindowClass or WindowsTerminalWindowClass;
    }

    private ModifierKeys GetTrackedModifiers()
    {
        ModifierKeys result = ModifierKeys.None;

        if (IsAnyPressed(VkShift, VkLShift, VkRShift))
            result |= ModifierKeys.Shift;

        if (IsAnyPressed(VkControl, VkLControl, VkRControl))
            result |= ModifierKeys.Control;

        if (IsAnyPressed(VkMenu, VkLMenu, VkRMenu))
            result |= ModifierKeys.Alt;

        if (IsAnyPressed(VkLWin, VkRWin))
            result |= ModifierKeys.Windows;

        return result;
    }

    private bool IsAnyPressed(params int[] virtualKeys)
    {
        foreach (int virtualKey in virtualKeys)
        {
            if (_pressedKeys.Contains(virtualKey))
                return true;
        }

        return false;
    }

    private static bool IsModifierVirtualKey(int vk) =>
        vk is VkShift or VkControl or VkMenu or
              VkLShift or VkRShift or
              VkLControl or VkRControl or
              VkLMenu or VkRMenu or
              VkLWin or VkRWin;

    public void Dispose()
    {
        ResetTransientState();

        if (_hook != IntPtr.Zero)
        {
            NativeMethods.UnhookWindowsHookEx(_hook);
            _hook = IntPtr.Zero;
        }
    }
}
