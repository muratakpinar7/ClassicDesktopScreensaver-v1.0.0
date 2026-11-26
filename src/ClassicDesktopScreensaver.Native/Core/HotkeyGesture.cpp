#include "HotkeyGesture.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_set>

namespace classic_desktop_screensaver
{
    namespace
    {
        struct NamedKey
        {
            std::wstring_view name;
            std::uint16_t virtualKey;
        };

        constexpr NamedKey NamedKeys[] = {
            { L"Cancel", VK_CANCEL }, { L"Back", VK_BACK }, { L"Tab", VK_TAB },
            { L"LineFeed", 0 }, { L"Clear", VK_CLEAR }, { L"Return", VK_RETURN },
            { L"Enter", VK_RETURN }, { L"Pause", VK_PAUSE }, { L"Capital", VK_CAPITAL },
            { L"CapsLock", VK_CAPITAL }, { L"KanaMode", VK_KANA },
            { L"HangulMode", VK_HANGUL }, { L"JunjaMode", VK_JUNJA },
            { L"FinalMode", VK_FINAL }, { L"HanjaMode", VK_HANJA },
            { L"KanjiMode", VK_HANJA }, { L"Escape", VK_ESCAPE },
            { L"ImeConvert", VK_CONVERT }, { L"ImeNonConvert", VK_NONCONVERT },
            { L"ImeAccept", VK_ACCEPT }, { L"ImeModeChange", VK_MODECHANGE },
            { L"Space", VK_SPACE }, { L"Prior", VK_PRIOR }, { L"PageUp", VK_PRIOR },
            { L"Next", VK_NEXT }, { L"PageDown", VK_NEXT }, { L"End", VK_END },
            { L"Home", VK_HOME }, { L"Left", VK_LEFT }, { L"Up", VK_UP },
            { L"Right", VK_RIGHT }, { L"Down", VK_DOWN }, { L"Select", VK_SELECT },
            { L"Print", VK_PRINT }, { L"Execute", VK_EXECUTE },
            { L"Snapshot", VK_SNAPSHOT }, { L"PrintScreen", VK_SNAPSHOT },
            { L"Insert", VK_INSERT }, { L"Delete", VK_DELETE }, { L"Help", VK_HELP },
            { L"LWin", VK_LWIN }, { L"RWin", VK_RWIN }, { L"Apps", VK_APPS },
            { L"Sleep", VK_SLEEP }, { L"Multiply", VK_MULTIPLY }, { L"Add", VK_ADD },
            { L"Separator", VK_SEPARATOR }, { L"Subtract", VK_SUBTRACT },
            { L"Decimal", VK_DECIMAL }, { L"Divide", VK_DIVIDE },
            { L"NumLock", VK_NUMLOCK }, { L"Scroll", VK_SCROLL },
            { L"BrowserBack", VK_BROWSER_BACK }, { L"BrowserForward", VK_BROWSER_FORWARD },
            { L"BrowserRefresh", VK_BROWSER_REFRESH }, { L"BrowserStop", VK_BROWSER_STOP },
            { L"BrowserSearch", VK_BROWSER_SEARCH },
            { L"BrowserFavorites", VK_BROWSER_FAVORITES },
            { L"BrowserHome", VK_BROWSER_HOME }, { L"VolumeMute", VK_VOLUME_MUTE },
            { L"VolumeDown", VK_VOLUME_DOWN }, { L"VolumeUp", VK_VOLUME_UP },
            { L"MediaNextTrack", VK_MEDIA_NEXT_TRACK },
            { L"MediaPreviousTrack", VK_MEDIA_PREV_TRACK },
            { L"MediaStop", VK_MEDIA_STOP }, { L"MediaPlayPause", VK_MEDIA_PLAY_PAUSE },
            { L"LaunchMail", VK_LAUNCH_MAIL }, { L"SelectMedia", VK_LAUNCH_MEDIA_SELECT },
            { L"LaunchApplication1", VK_LAUNCH_APP1 },
            { L"LaunchApplication2", VK_LAUNCH_APP2 },
            { L"Oem1", VK_OEM_1 }, { L"OemSemicolon", VK_OEM_1 },
            { L"OemPlus", VK_OEM_PLUS }, { L"OemComma", VK_OEM_COMMA },
            { L"OemMinus", VK_OEM_MINUS }, { L"OemPeriod", VK_OEM_PERIOD },
            { L"Oem2", VK_OEM_2 }, { L"OemQuestion", VK_OEM_2 },
            { L"Oem3", VK_OEM_3 }, { L"OemTilde", VK_OEM_3 },
            { L"AbntC1", 0xC1 }, { L"AbntC2", 0xC2 },
            { L"LeftShift", VK_LSHIFT }, { L"RightShift", VK_RSHIFT },
            { L"LeftCtrl", VK_LCONTROL }, { L"RightCtrl", VK_RCONTROL },
            { L"LeftAlt", VK_LMENU }, { L"RightAlt", VK_RMENU },
            { L"Oem4", VK_OEM_4 }, { L"OemOpenBrackets", VK_OEM_4 },
            { L"Oem5", VK_OEM_5 }, { L"OemPipe", VK_OEM_5 },
            { L"Oem6", VK_OEM_6 }, { L"OemCloseBrackets", VK_OEM_6 },
            { L"Oem7", VK_OEM_7 }, { L"OemQuotes", VK_OEM_7 },
            { L"Oem8", VK_OEM_8 }, { L"Oem102", VK_OEM_102 },
            { L"OemBackslash", VK_OEM_102 }, { L"ImeProcessed", 0xE5 },
            { L"OemAttn", 0xF0 }, { L"DbeAlphanumeric", 0xF0 },
            { L"OemFinish", 0xF1 }, { L"DbeKatakana", 0xF1 },
            { L"OemCopy", 0xF2 }, { L"DbeHiragana", 0xF2 },
            { L"OemAuto", 0xF3 }, { L"DbeSbcsChar", 0xF3 },
            { L"OemEnlw", 0xF4 }, { L"DbeDbcsChar", 0xF4 },
            { L"OemBackTab", 0xF5 }, { L"DbeRoman", 0xF5 },
            { L"Attn", 0xF6 }, { L"DbeNoRoman", 0xF6 },
            { L"CrSel", 0xF7 }, { L"DbeEnterWordRegisterMode", 0xF7 },
            { L"ExSel", 0xF8 }, { L"DbeEnterImeConfigureMode", 0xF8 },
            { L"EraseEof", 0xF9 }, { L"DbeFlushString", 0xF9 },
            { L"Play", 0xFA }, { L"DbeCodeInput", 0xFA },
            { L"Zoom", 0xFB }, { L"DbeNoCodeInput", 0xFB },
            { L"NoName", 0xFC }, { L"DbeDetermineString", 0xFC },
            { L"Pa1", 0xFD }, { L"DbeEnterDialogConversionMode", 0xFD },
            { L"OemClear", 0xFE }, { L"DeadCharProcessed", 0 }
        };

        std::wstring Lower(std::wstring_view value)
        {
            std::wstring result(value);
            for (wchar_t& ch : result)
            {
                if (ch >= L'A' && ch <= L'Z')
                {
                    ch = static_cast<wchar_t>(ch - L'A' + L'a');
                }
            }
            return result;
        }

        bool ParseNumberedKey(
            std::wstring_view token,
            std::wstring_view prefix,
            std::uint16_t first,
            std::uint16_t count,
            std::uint16_t& virtualKey)
        {
            if (token.size() <= prefix.size() ||
                Lower(token.substr(0, prefix.size())) != Lower(prefix))
            {
                return false;
            }

            unsigned int number = 0;
            for (wchar_t ch : token.substr(prefix.size()))
            {
                if (ch < L'0' || ch > L'9')
                {
                    return false;
                }
                number = number * 10 + static_cast<unsigned int>(ch - L'0');
            }

            const bool isFunctionKey = Lower(prefix) == L"f";
            if ((isFunctionKey && number == 0) ||
                (isFunctionKey ? number > count : number >= count))
            {
                return false;
            }
            virtualKey = static_cast<std::uint16_t>(
                first + (isFunctionKey ? number - 1 : number));
            return true;
        }

        bool TryParseKey(std::wstring_view token, std::uint16_t& virtualKey)
        {
            if (token.size() == 1)
            {
                const wchar_t ch = static_cast<wchar_t>(std::towupper(token.front()));
                if (ch >= L'A' && ch <= L'Z')
                {
                    virtualKey = static_cast<std::uint16_t>(ch);
                    return true;
                }
            }

            if (token.size() == 2 &&
                (token[0] == L'D' || token[0] == L'd') &&
                token[1] >= L'0' && token[1] <= L'9')
            {
                virtualKey = static_cast<std::uint16_t>(L'0' + (token[1] - L'0'));
                return true;
            }

            if (ParseNumberedKey(token, L"F", VK_F1, 24, virtualKey) ||
                ParseNumberedKey(token, L"NumPad", VK_NUMPAD0, 10, virtualKey))
            {
                return true;
            }

            const std::wstring normalized = Lower(token);
            for (const NamedKey& key : NamedKeys)
            {
                if (Lower(key.name) == normalized)
                {
                    virtualKey = key.virtualKey;
                    return true;
                }
            }

            return false;
        }

        std::wstring CanonicalKeyName(std::uint16_t virtualKey)
        {
            if (virtualKey >= L'A' && virtualKey <= L'Z')
            {
                return std::wstring(1, static_cast<wchar_t>(virtualKey));
            }
            if (virtualKey >= L'0' && virtualKey <= L'9')
            {
                return L"D" + std::wstring(1, static_cast<wchar_t>(virtualKey));
            }
            if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
            {
                return L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
            }
            if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
            {
                return L"NumPad" + std::to_wstring(virtualKey - VK_NUMPAD0);
            }

            switch (virtualKey)
            {
            case VK_SCROLL: return L"ScrollLock";
            case VK_PAUSE: return L"Pause";
            case VK_RETURN: return L"Return";
            case VK_PRIOR: return L"PageUp";
            case VK_NEXT: return L"PageDown";
            case VK_CAPITAL: return L"CapsLock";
            case VK_SNAPSHOT: return L"PrintScreen";
            case VK_OEM_1: return L"Oem1";
            case VK_OEM_PLUS: return L"OemPlus";
            case VK_OEM_COMMA: return L"OemComma";
            case VK_OEM_MINUS: return L"OemMinus";
            case VK_OEM_PERIOD: return L"OemPeriod";
            case VK_OEM_2: return L"Oem2";
            case VK_OEM_3: return L"Oem3";
            case VK_OEM_4: return L"Oem4";
            case VK_OEM_5: return L"Oem5";
            case VK_OEM_6: return L"Oem6";
            case VK_OEM_7: return L"Oem7";
            case VK_OEM_8: return L"Oem8";
            case VK_OEM_102: return L"Oem102";
            default:
                break;
            }

            for (const NamedKey& key : NamedKeys)
            {
                if (key.virtualKey == virtualKey)
                {
                    return std::wstring(key.name);
                }
            }
            return L"";
        }

        std::wstring Trim(std::wstring_view text)
        {
            std::size_t first = 0;
            while (first < text.size() && std::iswspace(text[first]) != 0)
            {
                ++first;
            }
            std::size_t last = text.size();
            while (last > first && std::iswspace(text[last - 1]) != 0)
            {
                --last;
            }
            return std::wstring(text.substr(first, last - first));
        }
    }

    bool TryParseHotkey(
        std::wstring_view text,
        HotkeyGesture& gesture,
        std::wstring& error)
    {
        error.clear();
        HotkeyGesture parsed;
        bool hasKey = false;
        std::size_t start = 0;

        while (start <= text.size())
        {
            const std::size_t plus = text.find(L'+', start);
            const std::size_t end = plus == std::wstring_view::npos ? text.size() : plus;
            const std::wstring_view rawToken = text.substr(start, end - start);
            if (rawToken.empty())
            {
                if (plus == std::wstring_view::npos)
                {
                    break;
                }
                start = plus + 1;
                continue;
            }
            const std::wstring token = Trim(text.substr(start, end - start));
            if (token.empty())
            {
                error = L"Shortcut contains an empty key.";
                return false;
            }

            const std::wstring normalized = Lower(token);
            if (normalized == L"ctrl" || normalized == L"control")
            {
                parsed.modifiers |= ModifierControl;
            }
            else if (normalized == L"alt")
            {
                parsed.modifiers |= ModifierAlt;
            }
            else if (normalized == L"shift")
            {
                parsed.modifiers |= ModifierShift;
            }
            else if (normalized == L"win" || normalized == L"windows")
            {
                parsed.modifiers |= ModifierWindows;
            }
            else
            {
                std::uint16_t virtualKey = 0;
                if (normalized == L"scrolllock" || normalized == L"scroll lock")
                {
                    virtualKey = VK_SCROLL;
                }
                else if (!TryParseKey(token, virtualKey))
                {
                    error = L"Shortcut contains an unknown key: " + token;
                    return false;
                }

                if (virtualKey == 0 || IsModifierVirtualKey(virtualKey))
                {
                    error = L"Shortcut key is not a usable non-modifier key.";
                    return false;
                }
                parsed.virtualKey = virtualKey;
                parsed.keyName = virtualKey == VK_SCROLL
                    ? L"ScrollLock"
                    : CanonicalKeyName(virtualKey);
                if (parsed.keyName.empty())
                {
                    parsed.keyName = token;
                }
                hasKey = true;
            }

            if (plus == std::wstring_view::npos)
            {
                break;
            }
            start = plus + 1;
        }

        if (!hasKey)
        {
            error = L"Shortcut does not contain a key.";
            return false;
        }

        gesture = std::move(parsed);
        return true;
    }

    std::wstring FormatHotkey(const HotkeyGesture& gesture)
    {
        std::wstring result;
        const auto append = [&result](std::wstring_view value)
        {
            if (!result.empty())
            {
                result += L'+';
            }
            result.append(value);
        };

        if ((gesture.modifiers & ModifierControl) != 0) append(L"Ctrl");
        if ((gesture.modifiers & ModifierAlt) != 0) append(L"Alt");
        if ((gesture.modifiers & ModifierShift) != 0) append(L"Shift");
        if ((gesture.modifiers & ModifierWindows) != 0) append(L"Win");
        append(gesture.keyName.empty()
            ? CanonicalKeyName(gesture.virtualKey)
            : std::wstring_view(gesture.keyName));
        return result;
    }

    bool HasDuplicateShortcuts(
        const std::vector<ShortcutAssignment>& shortcuts) noexcept
    {
        for (std::size_t current = 0; current < shortcuts.size(); ++current)
        {
            if (!shortcuts[current].enabled)
            {
                continue;
            }

            for (std::size_t previous = 0; previous < current; ++previous)
            {
                if (shortcuts[previous].enabled &&
                    shortcuts[previous].gesture == shortcuts[current].gesture)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool IsModifierVirtualKey(std::uint32_t virtualKey) noexcept
    {
        return virtualKey == VK_SHIFT || virtualKey == VK_CONTROL ||
            virtualKey == VK_MENU || virtualKey == VK_LSHIFT ||
            virtualKey == VK_RSHIFT || virtualKey == VK_LCONTROL ||
            virtualKey == VK_RCONTROL || virtualKey == VK_LMENU ||
            virtualKey == VK_RMENU || virtualKey == VK_LWIN ||
            virtualKey == VK_RWIN;
    }

    bool ShouldPassBarePhysicalPauseThroughTerminal(
        ShortcutAction action,
        std::uint32_t virtualKey,
        std::uint8_t modifiers,
        bool injected,
        std::wstring_view foregroundWindowClass) noexcept
    {
        if ((action != ShortcutAction::Sleep &&
             action != ShortcutAction::Hibernate) ||
            virtualKey != VK_PAUSE ||
            modifiers != ModifierNone ||
            injected)
        {
            return false;
        }

        return foregroundWindowClass == L"ConsoleWindowClass" ||
            foregroundWindowClass == L"CASCADIA_HOSTING_WINDOW_CLASS";
    }
}
