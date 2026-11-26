#pragma once

#include "..\Core\HotkeyGesture.h"

#include <Windows.h>

#include <array>
#include <vector>

namespace classic_desktop_screensaver
{
    class KeyboardHook final
    {
    public:
        explicit KeyboardHook(HWND target) noexcept;
        ~KeyboardHook();
        KeyboardHook(const KeyboardHook&) = delete;
        KeyboardHook& operator=(const KeyboardHook&) = delete;

        [[nodiscard]] bool Install() noexcept;
        void SetShortcuts(const std::vector<ShortcutAssignment>& shortcuts);
        void SetSuspended(bool suspended) noexcept;
        void ResetTransientState() noexcept;
        void Uninstall() noexcept;
        [[nodiscard]] DWORD Error() const noexcept { return error_; }

    private:
        static LRESULT CALLBACK HookProcedure(int code, WPARAM message, LPARAM data) noexcept;
        [[nodiscard]] LRESULT Process(int code, WPARAM message, LPARAM data) noexcept;
        [[nodiscard]] std::uint8_t CurrentModifiers() const noexcept;
        HWND target_{};
        HHOOK hook_{};
        DWORD error_{};
        bool suspended_{};
        std::array<bool, 256> pressedKeys_{};
        std::array<bool, 256> suppressedKeys_{};
        std::vector<ShortcutAssignment> shortcuts_;
        static KeyboardHook* active_;
    };
}
