#include "KeyboardHook.h"

#include <initializer_list>
#include <iterator>

namespace classic_desktop_screensaver
{
    KeyboardHook* KeyboardHook::active_ = nullptr;

    namespace
    {
        constexpr UINT MessageShortcut = WM_APP + 0x31;
        constexpr DWORD LlkhfLowerIlInjected = 0x02;
        constexpr DWORD LlkhfInjected = 0x10;

        bool IsKeyPhysicallyDown(int virtualKey) noexcept
        {
            return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        }
    }

    KeyboardHook::KeyboardHook(HWND target) noexcept : target_(target) {}

    KeyboardHook::~KeyboardHook()
    {
        Uninstall();
    }

    bool KeyboardHook::Install() noexcept
    {
        if (hook_ != nullptr)
        {
            return true;
        }
        if (active_ != nullptr && active_ != this)
        {
            error_ = ERROR_ALREADY_EXISTS;
            return false;
        }

        active_ = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProcedure, GetModuleHandleW(nullptr), 0);
        if (hook_ == nullptr)
        {
            error_ = GetLastError();
            active_ = nullptr;
            return false;
        }
        error_ = ERROR_SUCCESS;
        return true;
    }

    void KeyboardHook::SetShortcuts(const std::vector<ShortcutAssignment>& shortcuts)
    {
        shortcuts_ = shortcuts;
        ResetTransientState();
    }

    void KeyboardHook::SetSuspended(bool suspended) noexcept
    {
        if (suspended_ != suspended)
        {
            suspended_ = suspended;
            ResetTransientState();
        }
    }

    void KeyboardHook::ResetTransientState() noexcept
    {
        pressedKeys_.fill(false);
        suppressedKeys_.fill(false);

        // Power transitions can omit key-up notifications. Re-snapshot held
        // modifiers on the UI thread so a still-held Shift is not forgotten.
        // This is intentionally outside HookProcedure: Windows documents that
        // GetAsyncKeyState is not reliable from inside LowLevelKeyboardProc.
        pressedKeys_[VK_LSHIFT] = IsKeyPhysicallyDown(VK_LSHIFT);
        pressedKeys_[VK_RSHIFT] = IsKeyPhysicallyDown(VK_RSHIFT);
        pressedKeys_[VK_LCONTROL] = IsKeyPhysicallyDown(VK_LCONTROL);
        pressedKeys_[VK_RCONTROL] = IsKeyPhysicallyDown(VK_RCONTROL);
        pressedKeys_[VK_LMENU] = IsKeyPhysicallyDown(VK_LMENU);
        pressedKeys_[VK_RMENU] = IsKeyPhysicallyDown(VK_RMENU);
        pressedKeys_[VK_LWIN] = IsKeyPhysicallyDown(VK_LWIN);
        pressedKeys_[VK_RWIN] = IsKeyPhysicallyDown(VK_RWIN);
    }

    void KeyboardHook::Uninstall() noexcept
    {
        ResetTransientState();
        if (hook_ != nullptr)
        {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
    }

    LRESULT CALLBACK KeyboardHook::HookProcedure(int code, WPARAM message, LPARAM data) noexcept
    {
        KeyboardHook* active = active_;
        if (active == nullptr)
        {
            return CallNextHookEx(nullptr, code, message, data);
        }
        return active->Process(code, message, data);
    }

    LRESULT KeyboardHook::Process(int code, WPARAM message, LPARAM data) noexcept
    {
        if (code < 0)
        {
            return CallNextHookEx(hook_, code, message, data);
        }
        const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!keyDown && !keyUp)
        {
            return CallNextHookEx(hook_, code, message, data);
        }
        if (suspended_)
        {
            return CallNextHookEx(hook_, code, message, data);
        }

        const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if (event == nullptr)
        {
            return CallNextHookEx(hook_, code, message, data);
        }
        const std::uint32_t vk = event->vkCode;
        if (vk >= pressedKeys_.size())
        {
            return CallNextHookEx(hook_, code, message, data);
        }
        if (keyUp)
        {
            pressedKeys_[vk] = false;
            const bool wasSuppressed = suppressedKeys_[vk];
            suppressedKeys_[vk] = false;
            if (wasSuppressed)
            {
                return 1;
            }
            return CallNextHookEx(hook_, code, message, data);
        }

        const bool firstPress = !pressedKeys_[vk];
        pressedKeys_[vk] = true;
        if (IsModifierVirtualKey(vk))
        {
            return CallNextHookEx(hook_, code, message, data);
        }

        const std::uint8_t modifiers = CurrentModifiers();
        const ShortcutAssignment* match = nullptr;
        for (const ShortcutAssignment& shortcut : shortcuts_)
        {
            if (shortcut.enabled && shortcut.gesture.virtualKey == vk &&
                shortcut.gesture.modifiers == modifiers)
            {
                match = &shortcut;
                break;
            }
        }
        if (match == nullptr)
        {
            return CallNextHookEx(hook_, code, message, data);
        }

        const bool injected = (event->flags & (LlkhfInjected | LlkhfLowerIlInjected)) != 0;
        const bool isPowerAction = match->action == ShortcutAction::Sleep || match->action == ShortcutAction::Hibernate;
        if (isPowerAction && vk == VK_PAUSE && modifiers == ModifierNone && !injected)
        {
            const HWND foreground = GetForegroundWindow();
            wchar_t className[128]{};
            const int length = foreground == nullptr ? 0 :
                GetClassNameW(foreground, className, static_cast<int>(std::size(className)));
            const std::wstring_view foregroundClass = length > 0
                ? std::wstring_view(className, static_cast<std::size_t>(length))
                : std::wstring_view{};
            if (ShouldPassBarePhysicalPauseThroughTerminal(
                match->action, vk, modifiers, injected, foregroundClass))
            {
                return CallNextHookEx(hook_, code, message, data);
            }
        }

        if (firstPress && (target_ == nullptr ||
            PostMessageW(target_, MessageShortcut, static_cast<WPARAM>(match->action),
                static_cast<LPARAM>(modifiers)) == FALSE))
        {
            pressedKeys_[vk] = false;
            return CallNextHookEx(hook_, code, message, data);
        }
        suppressedKeys_[vk] = true;
        return 1;
    }

    std::uint8_t KeyboardHook::CurrentModifiers() const noexcept
    {
        std::uint8_t result = ModifierNone;
        const auto down = [this](std::initializer_list<std::uint32_t> keys)
        {
            for (const auto key : keys)
            {
                if (pressedKeys_[key])
                {
                    return true;
                }
            }
            return false;
        };
        if (down({ VK_SHIFT, VK_LSHIFT, VK_RSHIFT })) result |= ModifierShift;
        if (down({ VK_CONTROL, VK_LCONTROL, VK_RCONTROL })) result |= ModifierControl;
        if (down({ VK_MENU, VK_LMENU, VK_RMENU })) result |= ModifierAlt;
        if (down({ VK_LWIN, VK_RWIN })) result |= ModifierWindows;
        return result;
    }

}
