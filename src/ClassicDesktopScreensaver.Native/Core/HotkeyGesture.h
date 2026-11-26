#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace classic_desktop_screensaver
{
    enum Modifier : std::uint8_t
    {
        ModifierNone = 0,
        ModifierControl = 1 << 0,
        ModifierAlt = 1 << 1,
        ModifierShift = 1 << 2,
        ModifierWindows = 1 << 3
    };

    enum class ShortcutAction : std::uint8_t
    {
        StartScreenSaver,
        ToggleScreenSaver,
        Sleep,
        Hibernate
    };

    struct HotkeyGesture
    {
        std::uint8_t modifiers{ ModifierNone };
        std::uint16_t virtualKey{};
        std::wstring keyName;

        [[nodiscard]] bool operator==(const HotkeyGesture& other) const noexcept
        {
            return modifiers == other.modifiers && virtualKey == other.virtualKey;
        }
    };

    struct ShortcutAssignment
    {
        ShortcutAction action{};
        HotkeyGesture gesture;
        bool enabled{};
    };

    [[nodiscard]] bool TryParseHotkey(
        std::wstring_view text,
        HotkeyGesture& gesture,
        std::wstring& error);

    [[nodiscard]] std::wstring FormatHotkey(const HotkeyGesture& gesture);
    [[nodiscard]] bool HasDuplicateShortcuts(
        const std::vector<ShortcutAssignment>& shortcuts) noexcept;
    [[nodiscard]] bool IsModifierVirtualKey(std::uint32_t virtualKey) noexcept;
    [[nodiscard]] bool ShouldPassBarePhysicalPauseThroughTerminal(
        ShortcutAction action,
        std::uint32_t virtualKey,
        std::uint8_t modifiers,
        bool injected,
        std::wstring_view foregroundWindowClass) noexcept;
}
