#include "SettingsWindow.h"

#include "Localization.h"
#include "SettingsLayout.h"
#include "UiDiagnosticLog.h"
#include "..\Core\HotkeyGesture.h"

#include <CommCtrl.h>
#include <CommDlg.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t WindowClass[] = L"ClassicDesktopScreensaver.Native.Settings";
        constexpr wchar_t ContentWindowClass[] = L"ClassicDesktopScreensaver.Native.Settings.Content";
        constexpr wchar_t ContentSurfaceWindowClass[] = L"ClassicDesktopScreensaver.Native.Settings.Surface";
        constexpr UINT_PTR SaveTimer = 0x51;
        constexpr UINT_PTR HibernateRefreshTimer = 0x52;
        constexpr UINT HibernateRefreshIntervalMs = 3000;
        constexpr UINT MessageRefresh = WM_APP + 0x42;
        constexpr int ContentMargin = 16;
        constexpr int SectionGap = 10;
        constexpr int RowGap = 8;
        constexpr int GroupCaptionHeight = 28;
        constexpr int GroupBottomPadding = 12;
        constexpr int DefaultDropListHeight = 176;

        enum ControlId : int
        {
            IdSaverPathLabel = 101, IdCustomSaver, IdCustomPath, IdBrowse, IdTestSaver,
            IdWindowsSaverSettings, IdStartWindowsSaver,
            IdAutoSaver, IdSaverMinutes, IdLock, IdLockMinutes,
            IdFullscreenGuard = 113, IdLockGuard,
            IdHotCorner, IdCorner = static_cast<int>(SettingsControlId::HotCornerCombo), IdCornerDelay,
            IdStartHotkeyEnabled, IdStartHotkey, IdToggleHotkeyEnabled, IdToggleHotkey,
            IdSleepHotkeyEnabled, IdSleepHotkey, IdHibernateHotkeyEnabled, IdHibernateHotkey,
            IdHibernateStatus,
            IdNotifications, IdTrayIcon, IdStartWithWindows, IdLanguage,
            IdRestoreHotkeys, IdRestoreDefaults, IdAbout, IdHide
        };

        constexpr int IdScreenSaverGroup = static_cast<int>(SettingsControlId::ScreenSaverGroup);
        constexpr int IdAutomaticGroup = static_cast<int>(SettingsControlId::AutomaticGroup);
        constexpr int IdGuardsGroup = static_cast<int>(SettingsControlId::GuardsGroup);
        constexpr int IdHotCornerGroup = static_cast<int>(SettingsControlId::HotCornerGroup);
        constexpr int IdHotkeysGroup = static_cast<int>(SettingsControlId::HotkeysGroup);
        constexpr int IdBehaviorGroup = static_cast<int>(SettingsControlId::BehaviorGroup);
        constexpr int IdRegistryNote = static_cast<int>(SettingsControlId::RegistryNote);

        static_assert(IdHibernateHotkeyEnabled == static_cast<int>(SettingsControlId::HibernateHotkeyEnabled));
        static_assert(IdHibernateHotkey == static_cast<int>(SettingsControlId::HibernateHotkey));
        static_assert(IdHibernateStatus == static_cast<int>(SettingsControlId::HibernateStatus));
        static_assert(IdHibernateHotkeyEnabled < 200);

        enum class ControlKind
        {
            StaticText,
            Title,
            Group,
            CheckBox,
            Button,
            Edit,
            ComboBox
        };

        struct ChildControl
        {
            HWND window{};
            ControlKind kind{ ControlKind::StaticText };
            bool readOnlyExempt{};
        };

        struct LayoutCell
        {
            HWND window{};
            LayoutCellMode mode{ LayoutCellMode::Fill };
            int preferredWidth{};
            int minimumWidth{};
            int dropListHeight{};
        };

        struct LayoutRow
        {
            std::vector<LayoutCell> cells;
            int indent{};
            int spaceBefore{ 4 };
            int spaceAfter{ 4 };
            bool alignRight{};
        };

        struct LayoutSection
        {
            HWND group{};
            std::vector<LayoutRow> rows;
        };

        struct ControlPlacement
        {
            HWND window{};
            RECT rectangle{};
            int dropListHeight{};
        };

        struct WindowState
        {
            HINSTANCE instance{};
            HWND window{};
            HWND content{};
            HWND surface{};
            HFONT font{};
            HFONT titleFont{};
            SettingsHost* host{};
            bool loading{};
            bool hibernateAvailable{};
            bool creating{ true };
            bool controlCreationFailed{};
            HibernateAvailability hibernateAvailability{ HibernateAvailability::QueryFailed };
            AppLanguage language{ AppLanguage::Auto };
            UINT dpi{ 96 };
            int scrollPosition{};
            int contentHeight{};
            std::array<HWND, 256> controls{};
            std::vector<ChildControl> contentControls;
            std::vector<LayoutSection> sections;
            std::vector<LayoutRow> footerRows;
            HWND title{};
            HWND subtitle{};
            HWND registryNote{};
            HWND restoreDefaults{};
            HWND about{};
            HWND hide{};
        };

        LRESULT CALLBACK HotkeyEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
            UINT_PTR subclassId, DWORD_PTR referenceData);
        LRESULT CALLBACK ContentControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
            UINT_PTR subclassId, DWORD_PTR referenceData);
        void ApplyReadOnlyMode(WindowState& state) noexcept;
        std::wstring GetText(HWND control);
        void PlaceGroupBoxesBehindContent(WindowState& state) noexcept;
        RECT ClampWindowRectToWorkArea(RECT rectangle, const RECT& workArea) noexcept;
        void SetContentScrollPosition(WindowState& state, int requestedPosition);

        HWND Control(WindowState& state, int id) noexcept
        {
            if (id < 0 || static_cast<std::size_t>(id) >= state.controls.size())
            {
                return nullptr;
            }
            return state.controls[static_cast<std::size_t>(id)];
        }

        std::wstring L(AppLanguage language, std::wstring_view key)
        {
            return std::wstring(Text(language, key));
        }

        int ScaleForDpi(int value, UINT dpi) noexcept
        {
            return MulDiv(value, static_cast<int>(dpi), 96);
        }

        HWND AddControl(
            WindowState& state,
            int id,
            ControlKind kind,
            const wchar_t* className,
            std::wstring_view text,
            DWORD style,
            DWORD extendedStyle = 0,
            bool readOnlyExempt = false,
            int initialHeight = 1)
        {
            const std::wstring label(text);
            HWND result = CreateWindowExW(
                extendedStyle,
                className,
                label.c_str(),
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                0, 0, ScaleForDpi(1, state.dpi), ScaleForDpi(initialHeight, state.dpi),
                state.surface,
                id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                state.instance,
                nullptr);
            if (result == nullptr)
            {
                state.controlCreationFailed = true;
                LogUiDiagnostic(L"settings_control_create_failed", state.content, WM_CREATE,
                    static_cast<WPARAM>(id), 0, FALSE, GetLastError(), className);
                return nullptr;
            }
            if (result != nullptr)
            {
                if (state.font != nullptr)
                {
                    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
                }
                if (kind == ControlKind::Title && state.titleFont != nullptr)
                {
                    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(state.titleFont), TRUE);
                }
                if (id > 0 && static_cast<std::size_t>(id) < state.controls.size())
                {
                    state.controls[static_cast<std::size_t>(id)] = result;
                }
                SetWindowSubclass(result, ContentControlProc, 2,
                    reinterpret_cast<DWORD_PTR>(&state));
                state.contentControls.push_back({ result, kind, readOnlyExempt });
            }
            return result;
        }

        HWND AddLabel(WindowState& state, int id, std::wstring_view text, bool bordered = false)
        {
            return AddControl(state, id, ControlKind::StaticText, L"STATIC", text,
                SS_LEFT | SS_NOPREFIX | (bordered ? WS_BORDER : 0));
        }

        HWND AddGroup(WindowState& state, int id, std::wstring_view text)
        {
            return AddControl(state, id, ControlKind::Group, L"BUTTON", text, BS_GROUPBOX);
        }

        HWND AddCheck(WindowState& state, int id, std::wstring_view text)
        {
            return AddControl(state, id, ControlKind::CheckBox, L"BUTTON", text,
                BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP);
        }

        HWND AddButton(WindowState& state, int id, std::wstring_view text,
            bool readOnlyExempt = false)
        {
            return AddControl(state, id, ControlKind::Button, L"BUTTON", text,
                BS_PUSHBUTTON | BS_MULTILINE | WS_TABSTOP, 0, readOnlyExempt);
        }

        HWND AddEdit(WindowState& state, int id, std::wstring_view text,
            bool readOnly = false)
        {
            DWORD style = ES_AUTOHSCROLL | WS_TABSTOP;
            if (readOnly) style |= ES_READONLY;
            return AddControl(state, id, ControlKind::Edit, L"EDIT", text,
                style, WS_EX_CLIENTEDGE);
        }

        HWND AddCombo(WindowState& state, int id)
        {
            return AddControl(state, id, ControlKind::ComboBox, WC_COMBOBOXW, L"",
                CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
                0, false, DefaultDropListHeight);
        }

        bool AddComboOption(WindowState& state, int controlId,
            std::wstring_view label, LPARAM itemData)
        {
            const HWND combo = Control(state, controlId);
            if (combo == nullptr || IsWindow(combo) == FALSE)
            {
                state.controlCreationFailed = true;
                LogUiDiagnostic(L"settings_combo_control_missing", state.content, WM_CREATE,
                    static_cast<WPARAM>(controlId), 0, FALSE, ERROR_INVALID_WINDOW_HANDLE);
                return false;
            }

            const std::wstring stableLabel(label);
            const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(stableLabel.c_str()));
            if (index == CB_ERR || index == CB_ERRSPACE)
            {
                state.controlCreationFailed = true;
                LogUiDiagnostic(L"settings_combo_option_add_failed", combo, CB_ADDSTRING,
                    static_cast<WPARAM>(controlId), static_cast<LPARAM>(index), FALSE,
                    index == CB_ERRSPACE ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_DATA);
                return false;
            }

            const LRESULT itemDataSet = SendMessageW(combo, CB_SETITEMDATA,
                static_cast<WPARAM>(index), itemData);
            if (itemDataSet == CB_ERR)
            {
                state.controlCreationFailed = true;
                LogUiDiagnostic(L"settings_combo_option_data_failed", combo, CB_SETITEMDATA,
                    static_cast<WPARAM>(controlId), static_cast<LPARAM>(index), FALSE,
                    ERROR_INVALID_DATA);
                return false;
            }
            return true;
        }

        int FindComboIndexByData(HWND combo, LPARAM itemData) noexcept
        {
            if (combo == nullptr || IsWindow(combo) == FALSE) return CB_ERR;
            const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
            for (LRESULT index = 0; index < count; ++index)
            {
                if (SendMessageW(combo, CB_GETITEMDATA,
                    static_cast<WPARAM>(index), 0) == itemData)
                {
                    return static_cast<int>(index);
                }
            }
            return CB_ERR;
        }

        bool TryReadSelectedHotCorner(HWND combo, HotCorner& hotCorner) noexcept
        {
            if (combo == nullptr || IsWindow(combo) == FALSE) return false;
            const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (index == CB_ERR) return false;
            const LRESULT itemData = SendMessageW(combo, CB_GETITEMDATA,
                static_cast<WPARAM>(index), 0);
            switch (itemData)
            {
            case static_cast<LRESULT>(HotCorner::TopLeft): hotCorner = HotCorner::TopLeft; return true;
            case static_cast<LRESULT>(HotCorner::TopRight): hotCorner = HotCorner::TopRight; return true;
            case static_cast<LRESULT>(HotCorner::BottomLeft): hotCorner = HotCorner::BottomLeft; return true;
            case static_cast<LRESULT>(HotCorner::BottomRight): hotCorner = HotCorner::BottomRight; return true;
            default: return false;
            }
        }

        LayoutCell FillCell(HWND window, int minimumWidth = 120, int dropListHeight = 0)
        {
            return { window, LayoutCellMode::Fill, minimumWidth, minimumWidth, dropListHeight };
        }

        LayoutCell FlexibleCell(HWND window, int preferredWidth, int minimumWidth)
        {
            return { window, LayoutCellMode::Flexible, preferredWidth, minimumWidth, 0 };
        }

        LayoutCell FixedCell(HWND window, int width, int dropListHeight = 0)
        {
            return { window, LayoutCellMode::Fixed, width, width, dropListHeight };
        }

        void AddRow(LayoutSection& section, std::initializer_list<LayoutCell> cells,
            int spaceBefore = 4, int spaceAfter = 4, int indent = ContentMargin)
        {
            section.rows.push_back({ std::vector<LayoutCell>(cells), indent,
                spaceBefore, spaceAfter, false });
        }

        LayoutSection& AddSection(WindowState& state, int id, std::wstring_view text)
        {
            state.sections.push_back({ AddGroup(state, id, text), {} });
            return state.sections.back();
        }

        void AddHotkeyRow(WindowState& state, LayoutSection& section,
            int checkId, int editId, std::wstring_view label)
        {
            HWND labelWindow = AddLabel(state, 0, label);
            HWND enabledWindow = AddCheck(state, checkId, L"");
            HWND editWindow = AddEdit(state, editId, L"", true);
            if (editWindow != nullptr)
            {
                SetWindowSubclass(editWindow, HotkeyEditProc, 1,
                    reinterpret_cast<DWORD_PTR>(&state));
            }
            AddRow(section, { FlexibleCell(labelWindow, 220, 128),
                FixedCell(enabledWindow, 36), FillCell(editWindow, 150) }, 3, 3);
        }

        void AddContentControls(WindowState& state)
        {
            state.sections.clear();
            state.footerRows.clear();
            const AppLanguage lang = state.language;
            state.title = AddControl(state, 1, ControlKind::Title, L"STATIC",
                L(lang, L"Title"), SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
            state.subtitle = AddLabel(state, 0, L(lang, L"Subtitle"));

            LayoutSection& saver = AddSection(state, IdScreenSaverGroup, L(lang, L"ScreenSaver"));
            HWND selectedSaver = AddLabel(state, 0, L(lang, L"WindowsSelectedSaver"));
            HWND windowsSaverPath = AddLabel(state, IdSaverPathLabel, L(lang, L"NoWindowsSaver"));
            HWND useCustomSaver = AddCheck(state, IdCustomSaver, L(lang, L"UseCustomSaver"));
            HWND testCustomSaver = AddButton(state, IdTestSaver, L(lang, L"TestCustomSaver"));
            HWND customSaverPath = AddEdit(state, IdCustomPath, L"", true);
            HWND browse = AddButton(state, IdBrowse, L(lang, L"Browse"));
            HWND windowsSettings = AddButton(state, IdWindowsSaverSettings,
                L(lang, L"OpenWindowsSaverSettings"));
            HWND startNow = AddButton(state, IdStartWindowsSaver, L(lang, L"StartNowWindows"));
            AddRow(saver, { FillCell(selectedSaver, 180) }, 0, 2);
            AddRow(saver, { FillCell(windowsSaverPath, 180) }, 0, 7);
            AddRow(saver, { FillCell(useCustomSaver, 180), FlexibleCell(testCustomSaver, 170, 120) });
            AddRow(saver, { FillCell(customSaverPath, 180), FlexibleCell(browse, 130, 100) });
            AddRow(saver, { FillCell(windowsSettings, 180), FillCell(startNow, 180) }, 4, 3);

            LayoutSection& automatic = AddSection(state, IdAutomaticGroup, L(lang, L"AutoGroup"));
            HWND autoSaver = AddCheck(state, IdAutoSaver, L(lang, L"AutoEnabled"));
            HWND startAfter = AddLabel(state, 0, L(lang, L"StartAfter"));
            HWND saverMinutes = AddEdit(state, IdSaverMinutes, L"2");
            HWND idleUnits = AddLabel(state, 0, L(lang, L"MinutesIdle"));
            HWND lockEnabled = AddCheck(state, IdLock, L(lang, L"LockEnabled"));
            HWND lockAfter = AddLabel(state, 0, L(lang, L"LockAfter"));
            HWND lockMinutes = AddEdit(state, IdLockMinutes, L"10");
            HWND lockUnits = AddLabel(state, 0, L(lang, L"MinutesLock"));
            AddRow(automatic, { FillCell(autoSaver, 180) }, 0, 4);
            AddRow(automatic, { FlexibleCell(startAfter, 190, 120),
                FixedCell(saverMinutes, 80), FillCell(idleUnits, 70) }, 2, 5);
            AddRow(automatic, { FillCell(lockEnabled, 180) }, 1, 4);
            AddRow(automatic, { FlexibleCell(lockAfter, 190, 120),
                FixedCell(lockMinutes, 80), FillCell(lockUnits, 70) }, 2, 3);

            LayoutSection& guards = AddSection(state, IdGuardsGroup, L(lang, L"Guards"));
            AddRow(guards, { FillCell(AddCheck(state, IdFullscreenGuard, L(lang, L"FullscreenGuard")), 180) }, 2, 2);
            AddRow(guards, { FillCell(AddCheck(state, IdLockGuard, L(lang, L"LockGuard")), 180) }, 2, 2);

            LayoutSection& hotCorner = AddSection(state, IdHotCornerGroup, L(lang, L"HotCorner"));
            AddRow(hotCorner, { FillCell(AddCheck(state, IdHotCorner, L(lang, L"HotCornerEnabled")), 180) }, 0, 4);
            HWND cornerLabel = AddLabel(state, 0, L(lang, L"Corner"));
            HWND cornerCombo = AddCombo(state, IdCorner);
            AddRow(hotCorner, { FlexibleCell(cornerLabel, 170, 110),
                FillCell(cornerCombo, 150, DefaultDropListHeight) }, 2, 3);
            HWND cornerDelayLabel = AddLabel(state, 0, L(lang, L"CornerDelay"));
            HWND cornerDelay = AddEdit(state, IdCornerDelay, L"500");
            HWND milliseconds = AddLabel(state, 0, L"ms");
            AddRow(hotCorner, { FlexibleCell(cornerDelayLabel, 170, 110),
                FixedCell(cornerDelay, 86), FixedCell(milliseconds, 36) }, 2, 3);

            LayoutSection& hotkeys = AddSection(state, IdHotkeysGroup, L(lang, L"Hotkeys"));
            AddRow(hotkeys, { FillCell(AddLabel(state, 0, L(lang, L"HotkeyHelp")), 180) }, 0, 5);
            AddHotkeyRow(state, hotkeys, IdStartHotkeyEnabled, IdStartHotkey, L(lang, L"StartSaver"));
            AddHotkeyRow(state, hotkeys, IdToggleHotkeyEnabled, IdToggleHotkey, L(lang, L"ToggleSaver"));
            AddHotkeyRow(state, hotkeys, IdSleepHotkeyEnabled, IdSleepHotkey, L(lang, L"Sleep"));
            AddHotkeyRow(state, hotkeys, IdHibernateHotkeyEnabled, IdHibernateHotkey, L(lang, L"Hibernate"));
            AddRow(hotkeys, { FillCell(AddLabel(state, IdHibernateStatus, L""), 180) }, 1, 3);
            AddRow(hotkeys, { FlexibleCell(AddButton(state, IdRestoreHotkeys,
                L(lang, L"RestoreHotkeys")), 290, 150) }, 2, 3);

            LayoutSection& behavior = AddSection(state, IdBehaviorGroup, L(lang, L"Behavior"));
            AddRow(behavior, { FillCell(AddCheck(state, IdNotifications, L(lang, L"Notifications")), 180) }, 0, 2);
            AddRow(behavior, { FillCell(AddCheck(state, IdTrayIcon, L(lang, L"TrayIcon")), 180) }, 2, 2);
            AddRow(behavior, { FillCell(AddCheck(state, IdStartWithWindows, L(lang, L"StartWithWindows")), 180) }, 2, 4);
            HWND languageLabel = AddLabel(state, 0, L(lang, L"Language"));
            HWND languageCombo = AddCombo(state, IdLanguage);
            AddRow(behavior, { FlexibleCell(languageLabel, 170, 110),
                FillCell(languageCombo, 150, DefaultDropListHeight) }, 1, 3);

            state.registryNote = AddLabel(state, IdRegistryNote, L(lang, L"RegistryNote"), true);
            state.restoreDefaults = AddButton(state, IdRestoreDefaults, L(lang, L"RestoreDefaults"));
            state.about = AddButton(state, IdAbout, L(lang, L"About"), true);
            state.hide = AddButton(state, IdHide, L(lang, L"Hide"), true);
            state.footerRows.push_back({ {
                    FlexibleCell(state.restoreDefaults, 220, 145),
                    FlexibleCell(state.about, 110, 88),
                    FlexibleCell(state.hide, 170, 128) },
                0, 0, 0, true });

            const std::array<std::pair<HotCorner, std::wstring_view>, 4> corners = {{
                { HotCorner::TopLeft, Text(lang, L"TopLeft") },
                { HotCorner::TopRight, Text(lang, L"TopRight") },
                { HotCorner::BottomLeft, Text(lang, L"BottomLeft") },
                { HotCorner::BottomRight, Text(lang, L"BottomRight") }
            }};
            for (const auto& [value, label] : corners)
            {
                (void)AddComboOption(state, IdCorner, label, static_cast<LPARAM>(value));
            }
            const LRESULT cornerCount = SendMessageW(Control(state, IdCorner), CB_GETCOUNT, 0, 0);
            if (cornerCount != static_cast<LRESULT>(corners.size()))
            {
                state.controlCreationFailed = true;
                LogUiDiagnostic(L"settings_hot_corner_options_incomplete", Control(state, IdCorner),
                    CB_GETCOUNT, static_cast<WPARAM>(IdCorner), cornerCount, FALSE, ERROR_INVALID_DATA,
                    L"expected=4");
            }

            const std::array<std::wstring_view, 3> languages = {
                Text(lang, L"LanguageAuto"), Text(lang, L"LanguageTurkish"), Text(lang, L"LanguageEnglish")
            };
            for (std::size_t index = 0; index < languages.size(); ++index)
            {
                (void)AddComboOption(state, IdLanguage, languages[index], static_cast<LPARAM>(index));
            }
            PlaceGroupBoxesBehindContent(state);
        }

        void LayoutContent(WindowState& state);
        void UpdateContentScrollInfo(WindowState& state);

        void SetChecked(WindowState& state, int id, bool value)
        {
            SendMessageW(Control(state, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        bool IsChecked(WindowState& state, int id)
        {
            return SendMessageW(Control(state, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        }

        std::wstring GetText(HWND control)
        {
            const int length = GetWindowTextLengthW(control);
            std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
            if (length != 0) GetWindowTextW(control, value.data(), length + 1);
            value.resize(static_cast<std::size_t>(length));
            return value;
        }

        bool ParseInteger(HWND control, int minimum, int maximum, int& value)
        {
            const std::wstring text = GetText(control);
            if (text.empty()) return false;
            wchar_t* end = nullptr;
            const long parsed = wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != L'\0' || parsed < minimum || parsed > maximum)
            {
                return false;
            }
            value = static_cast<int>(parsed);
            return true;
        }

        void RefreshHibernateStatus(WindowState& state, UINT triggerMessage)
        {
            if (state.host == nullptr) return;
            const HibernateAvailability previous = state.hibernateAvailability;
            state.hibernateAvailability = state.host->refreshHibernate != nullptr
                ? state.host->refreshHibernate(state.host->context)
                : HibernateAvailability::QueryFailed;
            state.hibernateAvailable = state.hibernateAvailability == HibernateAvailability::Available;

            HWND hibernateEdit = Control(state, IdHibernateHotkey);
            if (hibernateEdit != nullptr)
            {
                const bool shouldEnable = !state.host->readOnly && state.hibernateAvailable &&
                    IsChecked(state, IdHibernateHotkeyEnabled);
                if ((IsWindowEnabled(hibernateEdit) != FALSE) != shouldEnable)
                    EnableWindow(hibernateEdit, shouldEnable ? TRUE : FALSE);
            }

            const std::wstring note = state.hibernateAvailability == HibernateAvailability::Available
                ? L""
                : state.hibernateAvailability == HibernateAvailability::QueryFailed
                    ? L(state.language, L"CapabilitiesUnavailable")
                    : L(state.language, L"HibernateUnavailable");
            HWND status = Control(state, IdHibernateStatus);
            if (status != nullptr && GetText(status) != note)
                SetWindowTextW(status, note.c_str());

            if (previous != state.hibernateAvailability)
            {
                LogUiDiagnostic(L"settings_hibernate_capability_changed", state.window,
                    triggerMessage, static_cast<WPARAM>(state.hibernateAvailability),
                    static_cast<LPARAM>(previous), TRUE, ERROR_SUCCESS);
            }
        }

        void Populate(WindowState& state)
        {
            if (state.host == nullptr || state.host->getSettings == nullptr) return;
            state.loading = true;
            const AppSettings settings = state.host->getSettings(state.host->context);
            state.language = settings.language;

            SetWindowTextW(Control(state, IdSaverPathLabel), L(state.language, L"NoWindowsSaver").c_str());
            if (state.host->windowsSaverPath != nullptr)
            {
                const std::wstring path = state.host->windowsSaverPath(state.host->context);
                SetWindowTextW(Control(state, IdSaverPathLabel), path.empty()
                    ? L(state.language, L"NoWindowsSaver").c_str() : path.c_str());
            }
            SetChecked(state, IdCustomSaver, settings.useCustomScreenSaver);
            SetWindowTextW(Control(state, IdCustomPath), settings.screenSaverPath.c_str());
            SetChecked(state, IdAutoSaver, settings.autoScreenSaverEnabled);
            SetWindowTextW(Control(state, IdSaverMinutes), std::to_wstring(settings.screenSaverIdleMinutes).c_str());
            SetChecked(state, IdLock, settings.lockEnabled);
            SetWindowTextW(Control(state, IdLockMinutes), std::to_wstring(settings.lockIdleMinutes).c_str());
            SetChecked(state, IdFullscreenGuard, settings.blockScreenSaverWhileFullscreen);
            SetChecked(state, IdLockGuard, settings.suspendLockWhileMediaOrPresentation);
            SetChecked(state, IdHotCorner, settings.hotCornerEnabled);
            SetWindowTextW(Control(state, IdCornerDelay), std::to_wstring(settings.hotCornerDelayMs).c_str());
            const int cornerIndex = FindComboIndexByData(Control(state, IdCorner),
                static_cast<LPARAM>(settings.hotCorner));
            if (cornerIndex != CB_ERR)
            {
                SendMessageW(Control(state, IdCorner), CB_SETCURSEL,
                    static_cast<WPARAM>(cornerIndex), 0);
            }
            else
            {
                const int defaultCornerIndex = FindComboIndexByData(Control(state, IdCorner),
                    static_cast<LPARAM>(HotCorner::TopRight));
                if (defaultCornerIndex != CB_ERR)
                    SendMessageW(Control(state, IdCorner), CB_SETCURSEL,
                        static_cast<WPARAM>(defaultCornerIndex), 0);
                LogUiDiagnostic(L"settings_hot_corner_selection_missing", Control(state, IdCorner),
                    CB_SETCURSEL, static_cast<WPARAM>(settings.hotCorner), 0,
                    FALSE, ERROR_INVALID_DATA,
                    L"items=" + std::to_wstring(SendMessageW(Control(state, IdCorner), CB_GETCOUNT, 0, 0)));
            }
            SetChecked(state, IdStartHotkeyEnabled, settings.startScreenSaverHotkeyEnabled);
            SetWindowTextW(Control(state, IdStartHotkey), settings.startScreenSaverHotkey.c_str());
            SetChecked(state, IdToggleHotkeyEnabled, settings.toggleScreenSaverHotkeyEnabled);
            SetWindowTextW(Control(state, IdToggleHotkey), settings.toggleScreenSaverHotkey.c_str());
            SetChecked(state, IdSleepHotkeyEnabled, settings.sleepHotkeyEnabled);
            SetWindowTextW(Control(state, IdSleepHotkey), settings.sleepHotkey.c_str());
            SetChecked(state, IdHibernateHotkeyEnabled, settings.hibernateHotkeyEnabled);
            SetWindowTextW(Control(state, IdHibernateHotkey), settings.hibernateHotkey.c_str());
            SetChecked(state, IdNotifications, settings.showStatusNotifications);
            SetChecked(state, IdTrayIcon, settings.showTrayStatusIcon);
            SetChecked(state, IdStartWithWindows, settings.startWithWindows);
            const LRESULT languageIndex = settings.language == AppLanguage::Turkish ? 1 :
                settings.language == AppLanguage::English ? 2 : 0;
            SendMessageW(Control(state, IdLanguage), CB_SETCURSEL, static_cast<WPARAM>(languageIndex), 0);

            const auto setEnabled = [&state](int id, bool enabled)
            {
                EnableWindow(Control(state, id), enabled ? TRUE : FALSE);
            };
            const bool custom = settings.useCustomScreenSaver;
            setEnabled(IdCustomPath, custom);
            setEnabled(IdBrowse, true);
            setEnabled(IdTestSaver, custom && !settings.screenSaverPath.empty());
            // Let users disable a persisted Hibernate shortcut even while the
            // current system capability is unavailable.
            setEnabled(IdHibernateHotkeyEnabled, true);
            setEnabled(IdHibernateHotkey, state.hibernateAvailable && settings.hibernateHotkeyEnabled);
            setEnabled(IdStartHotkey, settings.startScreenSaverHotkeyEnabled);
            setEnabled(IdToggleHotkey, settings.toggleScreenSaverHotkeyEnabled);
            setEnabled(IdSleepHotkey, settings.sleepHotkeyEnabled);
            RefreshHibernateStatus(state, WM_SHOWWINDOW);
            state.loading = false;
        }

        bool ReadSettings(WindowState& state, AppSettings& candidate, std::wstring& error)
        {
            if (state.host == nullptr || state.host->getSettings == nullptr) return false;
            candidate = state.host->getSettings(state.host->context);
            const LRESULT language = SendMessageW(Control(state, IdLanguage), CB_GETCURSEL, 0, 0);
            candidate.language = language == 1 ? AppLanguage::Turkish :
                language == 2 ? AppLanguage::English : AppLanguage::Auto;
            candidate.autoScreenSaverEnabled = IsChecked(state, IdAutoSaver);
            candidate.lockEnabled = IsChecked(state, IdLock);
            candidate.blockScreenSaverWhileFullscreen = IsChecked(state, IdFullscreenGuard);
            candidate.suspendLockWhileMediaOrPresentation = IsChecked(state, IdLockGuard);
            candidate.hotCornerEnabled = IsChecked(state, IdHotCorner);

            int value{};
            if (!ParseInteger(Control(state, IdSaverMinutes), 1, 1440, value))
            {
                error = L"Screen saver idle minutes must be between 1 and 1440.";
                return false;
            }
            candidate.screenSaverIdleMinutes = value;
            if (!ParseInteger(Control(state, IdLockMinutes), 1, 1440, value))
            {
                error = L"Lock idle minutes must be between 1 and 1440.";
                return false;
            }
            candidate.lockIdleMinutes = value;
            if (!ParseInteger(Control(state, IdCornerDelay), 0, 5000, value))
            {
                error = L"Hot Corner delay must be between 0 and 5000.";
                return false;
            }
            candidate.hotCornerDelayMs = value;
            if (!TryReadSelectedHotCorner(Control(state, IdCorner), candidate.hotCorner))
            {
                error = L(state.language, L"InvalidHotCorner");
                return false;
            }

            candidate.useCustomScreenSaver = IsChecked(state, IdCustomSaver);
            candidate.screenSaverPath = GetText(Control(state, IdCustomPath));
            candidate.startScreenSaverHotkeyEnabled = IsChecked(state, IdStartHotkeyEnabled);
            candidate.startScreenSaverHotkey = GetText(Control(state, IdStartHotkey));
            candidate.toggleScreenSaverHotkeyEnabled = IsChecked(state, IdToggleHotkeyEnabled);
            candidate.toggleScreenSaverHotkey = GetText(Control(state, IdToggleHotkey));
            candidate.sleepHotkeyEnabled = IsChecked(state, IdSleepHotkeyEnabled);
            candidate.sleepHotkey = GetText(Control(state, IdSleepHotkey));
            candidate.hibernateHotkeyEnabled = IsChecked(state, IdHibernateHotkeyEnabled);
            candidate.hibernateHotkey = GetText(Control(state, IdHibernateHotkey));
            candidate.showStatusNotifications = IsChecked(state, IdNotifications);
            candidate.showTrayStatusIcon = IsChecked(state, IdTrayIcon);
            candidate.startWithWindows = IsChecked(state, IdStartWithWindows);

            std::vector<ShortcutAssignment> shortcuts;
            const auto addShortcut = [&shortcuts, &error](ShortcutAction action, bool enabled, const std::wstring& text)
            {
                if (!enabled) return true;
                HotkeyGesture gesture;
                if (!TryParseHotkey(text, gesture, error)) return false;
                shortcuts.push_back({ action, std::move(gesture), enabled });
                return true;
            };
            if (!addShortcut(ShortcutAction::StartScreenSaver, candidate.startScreenSaverHotkeyEnabled, candidate.startScreenSaverHotkey) ||
                !addShortcut(ShortcutAction::ToggleScreenSaver, candidate.toggleScreenSaverHotkeyEnabled, candidate.toggleScreenSaverHotkey) ||
                !addShortcut(ShortcutAction::Sleep, candidate.sleepHotkeyEnabled, candidate.sleepHotkey) ||
                !addShortcut(ShortcutAction::Hibernate, candidate.hibernateHotkeyEnabled, candidate.hibernateHotkey))
            {
                return false;
            }
            if (HasDuplicateShortcuts(shortcuts))
            {
                error = L(state.language, L"InvalidShortcut");
                return false;
            }
            return true;
        }

        int MeasureControlHeight(WindowState& state, HWND window, int width)
        {
            if (window == nullptr || !IsWindow(window)) return ScaleForDpi(24, state.dpi);
            const auto kind = std::find_if(state.contentControls.begin(), state.contentControls.end(),
                [window](const ChildControl& child) { return child.window == window; });
            if (kind == state.contentControls.end()) return ScaleForDpi(24, state.dpi);

            const int line = ScaleForDpi(22, state.dpi);
            if (kind->kind == ControlKind::Edit)
                return std::max(line, ScaleForDpi(28, state.dpi));
            if (kind->kind == ControlKind::ComboBox)
            {
                const LRESULT itemHeight = SendMessageW(window, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
                return std::max(ScaleForDpi(26, state.dpi),
                    static_cast<int>(itemHeight > 0 ? itemHeight : ScaleForDpi(18, state.dpi)) + ScaleForDpi(8, state.dpi));
            }

            const std::wstring text = GetText(window);
            if (text.empty()) return line;
            HDC dc = GetDC(state.content);
            if (dc == nullptr) return line;
            const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
            const HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
            RECT measure{ 0, 0, std::max(1, width), 0 };
            const auto child = std::find_if(state.contentControls.begin(), state.contentControls.end(),
                [window](const ChildControl& item) { return item.window == window; });
            const bool isCheck = child != state.contentControls.end() && child->kind == ControlKind::CheckBox;
            const bool isButton = child != state.contentControls.end() && child->kind == ControlKind::Button;
            const int textInset = isCheck ? ScaleForDpi(26, state.dpi) :
                isButton ? ScaleForDpi(16, state.dpi) : ScaleForDpi(8, state.dpi);
            measure.right = std::max(1, static_cast<int>(measure.right) - textInset);
            DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measure,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
            if (oldFont != nullptr && oldFont != HGDI_ERROR) SelectObject(dc, oldFont);
            ReleaseDC(state.content, dc);
            const int padding = isCheck ? ScaleForDpi(4, state.dpi) :
                isButton ? ScaleForDpi(12, state.dpi) : ScaleForDpi(2, state.dpi);
            return std::max(line, static_cast<int>(measure.bottom - measure.top) + padding);
        }

        void AddPlacement(std::vector<ControlPlacement>& placements, HWND window,
            int left, int top, int width, int height, int dropListHeight = 0)
        {
            if (window == nullptr) return;
            placements.push_back({ window, RECT{ left, top, left + std::max(1, width), top + std::max(1, height) },
                dropListHeight });
        }

        int PlaceRow(WindowState& state, const LayoutRow& row, int left, int top,
            int availableWidth, std::vector<ControlPlacement>& placements)
        {
            if (row.cells.empty()) return 0;
            const int gap = ScaleForDpi(RowGap, state.dpi);
            std::vector<LayoutCellWidthRequest> requests;
            requests.reserve(row.cells.size());
            for (const LayoutCell& cell : row.cells)
            {
                requests.push_back({ ScaleForDpi(cell.preferredWidth, state.dpi),
                    ScaleForDpi(cell.minimumWidth, state.dpi), cell.mode });
            }
            LayoutCellWidths widths = CalculateLayoutCellWidths(availableWidth, gap, requests);
            int y = top;
            int maxRowHeight = 0;
            for (std::size_t index = 0; index < row.cells.size(); ++index)
            {
                const LayoutCell& cell = row.cells[index];
                const int cellWidth = std::max(1, widths.widths[index]);
                const int cellHeight = MeasureControlHeight(state, cell.window, cellWidth);
                if (widths.stacked)
                {
                    AddPlacement(placements, cell.window, left, y, cellWidth, cellHeight,
                        cell.dropListHeight == 0 ? 0 : ScaleForDpi(cell.dropListHeight, state.dpi));
                    y += cellHeight + gap;
                }
                else
                {
                    AddPlacement(placements, cell.window, left, top, cellWidth, cellHeight,
                        cell.dropListHeight == 0 ? 0 : ScaleForDpi(cell.dropListHeight, state.dpi));
                    maxRowHeight = std::max(maxRowHeight, cellHeight);
                    left += cellWidth + gap;
                }
            }
            return widths.stacked ? std::max(0, y - top - gap) : maxRowHeight;
        }

        void UpdateContentScrollInfo(WindowState& state)
        {
            if (state.content == nullptr || !IsWindow(state.content)) return;
            RECT client{};
            GetClientRect(state.content, &client);
            const int page = static_cast<int>(std::max(0L, client.bottom - client.top));
            SCROLLINFO info{ sizeof(info) };
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            info.nMin = 0;
            info.nMax = std::max(0, state.contentHeight - 1);
            info.nPage = static_cast<UINT>(page);
            const int maxPosition = std::max(0, state.contentHeight - page);
            state.scrollPosition = std::clamp(state.scrollPosition, 0, maxPosition);
            info.nPos = state.scrollPosition;
            SetScrollInfo(state.content, SB_VERT, &info, TRUE);

            if (state.surface == nullptr || IsWindow(state.surface) == FALSE) return;
            RECT surface{};
            GetWindowRect(state.surface, &surface);
            POINT surfaceOrigin{ surface.left, surface.top };
            MapWindowPoints(nullptr, state.content, &surfaceOrigin, 1);
            const int desiredWidth = std::max(1L, client.right - client.left);
            const int desiredHeight = std::max(page, state.contentHeight);
            const int desiredTop = -state.scrollPosition;
            if (surfaceOrigin.x == 0 && surfaceOrigin.y == desiredTop &&
                surface.right - surface.left == desiredWidth &&
                surface.bottom - surface.top == desiredHeight) return;

            SetLastError(ERROR_SUCCESS);
            const BOOL moved = SetWindowPos(state.surface, nullptr, 0, desiredTop,
                desiredWidth, desiredHeight,
                SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
            const DWORD error = moved != FALSE ? ERROR_SUCCESS : GetLastError();
            LogUiDiagnostic(moved != FALSE ? L"settings_surface_positioned" : L"settings_surface_position_failed",
                state.surface, WM_WINDOWPOSCHANGED, static_cast<WPARAM>(state.scrollPosition),
                MAKELPARAM(desiredWidth, desiredHeight), moved, error,
                L"old_left=" + std::to_wstring(surfaceOrigin.x) + L" old_top=" +
                std::to_wstring(surfaceOrigin.y) + L" new_top=" + std::to_wstring(desiredTop));
            if (moved != FALSE && page > 0)
            {
                const RECT visibleSurface{ 0, state.scrollPosition, desiredWidth,
                    std::min(desiredHeight, state.scrollPosition + page) };
                const RECT visibleViewport{ 0, 0, desiredWidth, page };
                SetLastError(ERROR_SUCCESS);
                const BOOL redrawn = RedrawWindow(state.content, &visibleViewport, nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
                LogUiDiagnostic(redrawn != FALSE ? L"settings_visible_viewport_repainted" :
                    L"settings_visible_viewport_repaint_failed", state.content, WM_PAINT,
                    static_cast<WPARAM>(state.scrollPosition),
                    MAKELPARAM(visibleViewport.right, visibleViewport.bottom), redrawn,
                    redrawn != FALSE ? ERROR_SUCCESS : GetLastError(),
                    L"surface_visible_top=" + std::to_wstring(visibleSurface.top) +
                    L" surface_visible_bottom=" + std::to_wstring(visibleSurface.bottom));
            }
        }

        void SetContentScrollPosition(WindowState& state, int requestedPosition)
        {
            state.scrollPosition = requestedPosition;
            UpdateContentScrollInfo(state);
        }

        void LayoutContent(WindowState& state)
        {
            if (state.content == nullptr || !IsWindow(state.content)) return;
            RECT client{};
            GetClientRect(state.content, &client);
            const int clientWidth = std::max(1L, client.right - client.left);
            const int margin = ScaleForDpi(ContentMargin, state.dpi);
            const int sectionGap = ScaleForDpi(SectionGap, state.dpi);
            std::vector<ControlPlacement> placements;
            placements.reserve(state.contentControls.size());

            int y = ScaleForDpi(12, state.dpi);
            const int fullWidth = std::max(1, clientWidth - margin * 2);
            const int titleHeight = MeasureControlHeight(state, state.title, fullWidth);
            AddPlacement(placements, state.title, margin, y, fullWidth,
                std::max(ScaleForDpi(38, state.dpi), titleHeight));
            y += std::max(ScaleForDpi(38, state.dpi), titleHeight) + ScaleForDpi(3, state.dpi);
            const int subtitleHeight = MeasureControlHeight(state, state.subtitle, fullWidth);
            AddPlacement(placements, state.subtitle, margin, y, fullWidth, subtitleHeight);
            y += subtitleHeight + ScaleForDpi(12, state.dpi);

            for (LayoutSection& section : state.sections)
            {
                const int groupTop = y;
                int rowTop = groupTop + ScaleForDpi(GroupCaptionHeight, state.dpi);
                const int groupLeft = margin;
                const int groupWidth = fullWidth;
                for (const LayoutRow& row : section.rows)
                {
                    rowTop += ScaleForDpi(row.spaceBefore, state.dpi);
                    const int indent = ScaleForDpi(row.indent, state.dpi);
                    const int left = groupLeft + indent;
                    const int width = std::max(1, groupWidth - indent * 2);
                    const int rowHeight = PlaceRow(state, row, left, rowTop, width, placements);
                    rowTop += rowHeight + ScaleForDpi(row.spaceAfter, state.dpi);
                }
                const int groupHeight = std::max(ScaleForDpi(56, state.dpi),
                    rowTop - groupTop + ScaleForDpi(GroupBottomPadding, state.dpi));
                AddPlacement(placements, section.group, groupLeft, groupTop, groupWidth, groupHeight);
                y = groupTop + groupHeight + sectionGap;
            }

            const int noteHeight = MeasureControlHeight(state, state.registryNote, fullWidth);
            AddPlacement(placements, state.registryNote, margin, y,
                fullWidth, noteHeight + ScaleForDpi(16, state.dpi));
            y += noteHeight + ScaleForDpi(24, state.dpi);
            for (const LayoutRow& row : state.footerRows)
            {
                const int footerGap = ScaleForDpi(8, state.dpi);
                std::vector<LayoutCellWidthRequest> requests;
                for (const LayoutCell& cell : row.cells)
                    requests.push_back({ ScaleForDpi(cell.preferredWidth, state.dpi),
                        ScaleForDpi(cell.minimumWidth, state.dpi), cell.mode });
                const int totalMinimum = std::accumulate(requests.begin(), requests.end(), 0,
                    [](int total, const LayoutCellWidthRequest& item) { return total + item.minimumWidth; }) +
                    footerGap * std::max(0, static_cast<int>(requests.size()) - 1);
                const bool stackFooter = totalMinimum > fullWidth;
                LayoutCellWidths widths = CalculateLayoutCellWidths(fullWidth, footerGap, requests);
                int footerHeight = 0;
                if (stackFooter)
                {
                    for (const LayoutCell& cell : row.cells)
                    {
                        const int height = MeasureControlHeight(state, cell.window, fullWidth);
                        AddPlacement(placements, cell.window, margin, y, fullWidth, height);
                        y += height + footerGap;
                    }
                }
                else if (row.alignRight)
                {
                    int x = clientWidth - margin;
                    for (std::size_t index = row.cells.size(); index-- > 0;)
                    {
                        const LayoutCell& cell = row.cells[index];
                        const int width = widths.widths[index];
                        const int height = MeasureControlHeight(state, cell.window, width);
                        x -= width;
                        AddPlacement(placements, cell.window, x, y, width, height);
                        x -= footerGap;
                        footerHeight = std::max(footerHeight, height);
                    }
                    y += footerHeight;
                }
                else
                {
                    int x = margin;
                    for (std::size_t index = 0; index < row.cells.size(); ++index)
                    {
                        const LayoutCell& cell = row.cells[index];
                        const int width = widths.widths[index];
                        const int height = MeasureControlHeight(state, cell.window, width);
                        AddPlacement(placements, cell.window, x, y, width, height);
                        x += width + footerGap;
                        footerHeight = std::max(footerHeight, height);
                    }
                    y += footerHeight;
                }
            }
            state.contentHeight = y + ScaleForDpi(16, state.dpi);
            UpdateContentScrollInfo(state);

            HDWP batch = BeginDeferWindowPos(static_cast<int>(placements.size()));
            bool batchFailed = batch == nullptr;
            if (!batchFailed)
            {
                for (const ControlPlacement& placement : placements)
                {
                    const int left = placement.rectangle.left;
                    const int top = placement.rectangle.top;
                    const int width = placement.rectangle.right - placement.rectangle.left;
                    const int height = placement.dropListHeight > 0
                        ? placement.dropListHeight
                        : placement.rectangle.bottom - placement.rectangle.top;
                    batch = DeferWindowPos(batch, placement.window, nullptr, left, top,
                        width, height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
                    if (batch == nullptr) { batchFailed = true; break; }
                }
                if (!batchFailed && EndDeferWindowPos(batch) == FALSE) batchFailed = true;
            }
            if (batchFailed)
            {
                for (const ControlPlacement& placement : placements)
                {
                    const int height = placement.dropListHeight > 0
                        ? placement.dropListHeight
                        : placement.rectangle.bottom - placement.rectangle.top;
                    SetWindowPos(placement.window, nullptr, placement.rectangle.left,
                        placement.rectangle.top,
                        placement.rectangle.right - placement.rectangle.left, height,
                        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
                }
            }
            RedrawWindow(state.surface, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        }

        void UpdateFonts(WindowState& state)
        {
            NONCLIENTMETRICSW metrics{ sizeof(metrics) };
            LOGFONTW normal{};
            if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                &metrics, 0, state.dpi))
            {
                normal = metrics.lfMessageFont;
            }
            else
            {
                normal.lfHeight = -ScaleForDpi(12, state.dpi);
                normal.lfWeight = FW_NORMAL;
                normal.lfCharSet = DEFAULT_CHARSET;
                wcscpy_s(normal.lfFaceName, L"Segoe UI");
            }
            HFONT newFont = CreateFontIndirectW(&normal);

            LOGFONTW title = normal;
            title.lfHeight = normal.lfHeight < 0
                ? normal.lfHeight * 2
                : -ScaleForDpi(24, state.dpi);
            title.lfWeight = FW_SEMIBOLD;
            HFONT newTitleFont = CreateFontIndirectW(&title);

            HFONT oldFont = state.font;
            HFONT oldTitleFont = state.titleFont;
            state.font = newFont != nullptr
                ? newFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            state.titleFont = newTitleFont != nullptr ? newTitleFont : state.font;
            for (const ChildControl& child : state.contentControls)
            {
                if (child.window == nullptr) continue;
                const WPARAM selectedFont = child.window == state.title
                    ? reinterpret_cast<WPARAM>(state.titleFont)
                    : reinterpret_cast<WPARAM>(state.font);
                SendMessageW(child.window, WM_SETFONT, selectedFont, TRUE);
            }
            if (oldFont != nullptr && oldFont != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(oldFont);
            if (oldTitleFont != nullptr && oldTitleFont != oldFont) DeleteObject(oldTitleFont);
        }

        LRESULT CALLBACK ContentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                state = static_cast<WindowState*>(create->lpCreateParams);
                if (state == nullptr) return FALSE;
                state->content = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

            switch (message)
            {
            case WM_SIZE:
                LayoutContent(*state);
                return 0;
            case WM_VSCROLL:
            {
                RECT client{};
                GetClientRect(window, &client);
                const int line = ScaleForDpi(36, state->dpi);
                const int page = static_cast<int>(std::max(1L, client.bottom - client.top));
                int next = state->scrollPosition;
                switch (LOWORD(wParam))
                {
                case SB_LINEUP: next -= line; break;
                case SB_LINEDOWN: next += line; break;
                case SB_PAGEUP: next -= page; break;
                case SB_PAGEDOWN: next += page; break;
                case SB_TOP: next = 0; break;
                case SB_BOTTOM: next = state->contentHeight - page; break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                {
                    SCROLLINFO info{ sizeof(info) };
                    info.fMask = SIF_TRACKPOS;
                    if (GetScrollInfo(window, SB_VERT, &info)) next = info.nTrackPos;
                    break;
                }
                default: return 0;
                }
                SetContentScrollPosition(*state, next);
                return 0;
            }
            case WM_MOUSEWHEEL:
            {
                const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int line = ScaleForDpi(36, state->dpi) * 3;
                SetContentScrollPosition(*state,
                    state->scrollPosition - (wheelDelta / WHEEL_DELTA) * line);
                return 0;
            }
            case WM_COMMAND:
            case WM_NOTIFY:
                return SendMessageW(GetParent(window), message, wParam, lParam);
            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }

        LRESULT CALLBACK ContentSurfaceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                state = static_cast<WindowState*>(create->lpCreateParams);
                if (state == nullptr) return FALSE;
                state->surface = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

            switch (message)
            {
            case WM_COMMAND:
            case WM_NOTIFY:
            case WM_MOUSEWHEEL:
                return state->content != nullptr
                    ? SendMessageW(state->content, message, wParam, lParam)
                    : 0;
            case WM_NCDESTROY:
                if (state->surface == window) state->surface = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
            default:
                break;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        void RebuildLocalizedControls(WindowState& state, AppLanguage language)
        {
            state.loading = true;
            for (const ChildControl& child : state.contentControls)
            {
                DestroyWindow(child.window);
            }
            state.contentControls.clear();
            state.controls.fill(nullptr);
            state.title = nullptr;
            state.subtitle = nullptr;
            state.restoreDefaults = nullptr;
            state.about = nullptr;
            state.hide = nullptr;
            state.language = language;
            AddContentControls(state);
            UpdateFonts(state);
            LayoutContent(state);
            state.loading = false;
            ApplyReadOnlyMode(state);
        }

        void ApplyReadOnlyMode(WindowState& state) noexcept
        {
            if (state.host == nullptr || !state.host->readOnly) return;
            for (const ChildControl& child : state.contentControls)
            {
                if (!child.readOnlyExempt) EnableWindow(child.window, FALSE);
            }
        }

        void SaveControls(WindowState& state, bool showError)
        {
            if (state.loading || state.host == nullptr || state.host->readOnly ||
                state.host->updateSettings == nullptr) return;
            AppSettings candidate;
            std::wstring error;
            if (!ReadSettings(state, candidate, error))
            {
                if (showError)
                {
                    MessageBoxW(state.window, error.c_str(), L(state.language, L"SaveFailed").c_str(), MB_OK | MB_ICONWARNING);
                }
                Populate(state);
                return;
            }
            const AppLanguage priorLanguage = state.language;
            if (!state.host->updateSettings(state.host->context, candidate, error))
            {
                if (showError)
                {
                    MessageBoxW(state.window, error.c_str(), L(state.language, L"SaveFailed").c_str(), MB_OK | MB_ICONWARNING);
                }
                Populate(state);
                return;
            }
            if (priorLanguage != candidate.language)
            {
                RebuildLocalizedControls(state, candidate.language);
            }
            Populate(state);
        }

        LRESULT CALLBACK HotkeyEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
            UINT_PTR, DWORD_PTR)
        {
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                const auto vk = static_cast<std::uint32_t>(wParam);
                if (IsModifierVirtualKey(vk)) return 0;
                std::uint8_t modifiers = ModifierNone;
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= ModifierShift;
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= ModifierControl;
                if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= ModifierAlt;
                if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) modifiers |= ModifierWindows;
                SetWindowTextW(window, FormatHotkey({ modifiers, static_cast<std::uint16_t>(vk), {} }).c_str());
                const int id = GetDlgCtrlID(window);
                SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(id, EN_CHANGE), reinterpret_cast<LPARAM>(window));
                return 0;
            }
            if (message == WM_CHAR) return 0;
            return DefSubclassProc(window, message, wParam, lParam);
        }

        LRESULT CALLBACK ContentControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
            UINT_PTR, DWORD_PTR referenceData)
        {
            if (message == WM_MOUSEWHEEL)
            {
                SendMessageW(GetParent(window), message, wParam, lParam);
                return 0;
            }
            if (message == WM_SETFOCUS)
            {
                auto* state = reinterpret_cast<WindowState*>(referenceData);
                if (state != nullptr && state->content != nullptr)
                {
                    RECT bounds{};
                    RECT client{};
                    GetWindowRect(window, &bounds);
                    POINT points[2] = { { bounds.left, bounds.top }, { bounds.right, bounds.bottom } };
                    MapWindowPoints(nullptr, state->content, points, 2);
                    GetClientRect(state->content, &client);
                    int next = state->scrollPosition;
                    if (points[0].y < 0) next += points[0].y;
                    else if (points[1].y > client.bottom)
                        next += points[1].y - client.bottom;
                    SetContentScrollPosition(*state, next);
                }
            }
            if (message == WM_KEYDOWN)
            {
                auto* state = reinterpret_cast<WindowState*>(referenceData);
                wchar_t className[32]{};
                GetClassNameW(window, className, static_cast<int>(std::size(className)));
                const bool textControl = _wcsicmp(className, L"Edit") == 0 ||
                    _wcsicmp(className, WC_COMBOBOXW) == 0;
                if (state != nullptr && !textControl)
                {
                    UINT command{};
                    switch (wParam)
                    {
                    case VK_PRIOR: command = SB_PAGEUP; break;
                    case VK_NEXT: command = SB_PAGEDOWN; break;
                    case VK_HOME: command = SB_TOP; break;
                    case VK_END: command = SB_BOTTOM; break;
                    default: break;
                    }
                    if (command != 0)
                    {
                        SendMessageW(state->content, WM_VSCROLL, MAKEWPARAM(command, 0), 0);
                        return 0;
                    }
                }
            }
            return DefSubclassProc(window, message, wParam, lParam);
        }

        void CaptureHotkey(WindowState& state, int id)
        {
            if (state.loading || id < IdStartHotkey || id > IdHibernateHotkey) return;
            SaveControls(state, true);
        }

        bool RegisterContentWindowClass(HINSTANCE instance) noexcept
        {
            WNDCLASSEXW contentClass{ sizeof(contentClass) };
            contentClass.lpfnWndProc = ContentProc;
            contentClass.hInstance = instance;
            contentClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            contentClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            contentClass.lpszClassName = ContentWindowClass;
            SetLastError(ERROR_SUCCESS);
            const ATOM registered = RegisterClassExW(&contentClass);
            const DWORD error = GetLastError();
            const bool success = registered != 0 || error == ERROR_CLASS_ALREADY_EXISTS;
            LogUiDiagnostic(success ? L"settings_content_class_ready" : L"settings_content_class_failed",
                nullptr, 0, 0, 0, success ? TRUE : FALSE,
                success ? ERROR_SUCCESS : error);
            return success;
        }

        bool RegisterContentSurfaceWindowClass(HINSTANCE instance) noexcept
        {
            WNDCLASSEXW surfaceClass{ sizeof(surfaceClass) };
            surfaceClass.lpfnWndProc = ContentSurfaceProc;
            surfaceClass.hInstance = instance;
            surfaceClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            surfaceClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            surfaceClass.lpszClassName = ContentSurfaceWindowClass;
            SetLastError(ERROR_SUCCESS);
            const ATOM registered = RegisterClassExW(&surfaceClass);
            const DWORD error = GetLastError();
            const bool success = registered != 0 || error == ERROR_CLASS_ALREADY_EXISTS;
            LogUiDiagnostic(success ? L"settings_surface_class_ready" : L"settings_surface_class_failed",
                nullptr, 0, 0, 0, success ? TRUE : FALSE,
                success ? ERROR_SUCCESS : error);
            return success;
        }

        bool GetWorkArea(HMONITOR monitor, RECT& workArea) noexcept
        {
            MONITORINFO info{ sizeof(info) };
            if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE) return false;
            workArea = info.rcWork;
            return true;
        }

        bool RectangleContainedBy(const RECT& rectangle, const RECT& bounds) noexcept
        {
            return rectangle.left >= bounds.left && rectangle.top >= bounds.top &&
                rectangle.right <= bounds.right && rectangle.bottom <= bounds.bottom;
        }

        struct WorkAreaContainmentContext
        {
            RECT rectangle{};
            bool contained{};
        };

        BOOL CALLBACK CheckWorkAreaContainment(HMONITOR monitor, HDC, LPRECT, LPARAM value)
        {
            auto* context = reinterpret_cast<WorkAreaContainmentContext*>(value);
            if (context == nullptr) return FALSE;
            RECT workArea{};
            if (GetWorkArea(monitor, workArea) && RectangleContainedBy(context->rectangle, workArea))
            {
                context->contained = true;
                return FALSE;
            }
            return TRUE;
        }

        bool IsWindowInsideAnyWorkArea(HWND window) noexcept
        {
            RECT rectangle{};
            if (window == nullptr || GetWindowRect(window, &rectangle) == FALSE) return false;
            WorkAreaContainmentContext context{ rectangle, false };
            EnumDisplayMonitors(nullptr, nullptr, CheckWorkAreaContainment,
                reinterpret_cast<LPARAM>(&context));
            return context.contained;
        }

        void EnsureWindowWithinWorkArea(HWND window, UINT cause) noexcept
        {
            if (window == nullptr || IsWindow(window) == FALSE || IsWindowInsideAnyWorkArea(window)) return;
            RECT current{};
            if (GetWindowRect(window, &current) == FALSE) return;
            const HMONITOR monitor = MonitorFromRect(&current, MONITOR_DEFAULTTONEAREST);
            RECT workArea{};
            if (!GetWorkArea(monitor, workArea)) return;
            const RECT target = ClampWindowRectToWorkArea(current, workArea);
            if (target.left == current.left && target.top == current.top &&
                target.right == current.right && target.bottom == current.bottom) return;

            SetLastError(ERROR_SUCCESS);
            const BOOL moved = SetWindowPos(window, nullptr, target.left, target.top,
                target.right - target.left, target.bottom - target.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            const DWORD error = moved != FALSE ? ERROR_SUCCESS : GetLastError();
            LogUiDiagnostic(moved != FALSE ? L"settings_window_clamped_to_workarea" :
                L"settings_window_clamp_failed", window, cause, 0, 0,
                moved, error,
                L"left=" + std::to_wstring(target.left) + L" top=" + std::to_wstring(target.top) +
                L" right=" + std::to_wstring(target.right) + L" bottom=" + std::to_wstring(target.bottom));
        }

        std::wstring SettingsWindowStateDetail(WindowState& state)
        {
            RECT windowRect{};
            RECT windowClient{};
            RECT viewportClient{};
            RECT surfaceRect{};
            const bool windowRectRead = state.window != nullptr &&
                GetWindowRect(state.window, &windowRect) != FALSE;
            const bool windowClientRead = state.window != nullptr &&
                GetClientRect(state.window, &windowClient) != FALSE;
            const bool viewportRead = state.content != nullptr && IsWindow(state.content) &&
                GetClientRect(state.content, &viewportClient) != FALSE;
            const bool surfaceRectRead = state.surface != nullptr && IsWindow(state.surface) &&
                GetWindowRect(state.surface, &surfaceRect) != FALSE;
            POINT surfaceOrigin{};
            if (surfaceRectRead && state.content != nullptr && IsWindow(state.content))
            {
                surfaceOrigin = { surfaceRect.left, surfaceRect.top };
                MapWindowPoints(nullptr, state.content, &surfaceOrigin, 1);
            }
            SCROLLINFO scroll{ sizeof(scroll) };
            scroll.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
            const bool scrollRead = state.content != nullptr &&
                GetScrollInfo(state.content, SB_VERT, &scroll) != FALSE;

            std::wstring detail = L"valid=" + std::to_wstring(IsWindow(state.window) ? 1 : 0) +
                L" visible=" + std::to_wstring(IsWindowVisible(state.window) ? 1 : 0) +
                L" dpi=" + std::to_wstring(state.dpi) + L" scroll=" +
                std::to_wstring(scrollRead ? scroll.nPos : state.scrollPosition) +
                L" scroll_max=" + std::to_wstring(scrollRead
                    ? std::max(0, scroll.nMax - static_cast<int>(scroll.nPage) + 1) : 0) +
                L" surface_valid=" + std::to_wstring(state.surface != nullptr && IsWindow(state.surface) ? 1 : 0) +
                L" window_rect=";
            if (windowRectRead)
                detail += std::to_wstring(windowRect.left) + L"," + std::to_wstring(windowRect.top) + L"," +
                    std::to_wstring(windowRect.right) + L"," + std::to_wstring(windowRect.bottom);
            else detail += L"unavailable";
            detail += L" client=";
            if (windowClientRead)
                detail += std::to_wstring(windowClient.right) + L"x" + std::to_wstring(windowClient.bottom);
            else detail += L"unavailable";
            detail += L" viewport=";
            if (viewportRead)
                detail += std::to_wstring(viewportClient.right) + L"x" +
                    std::to_wstring(viewportClient.bottom);
            else detail += L"unavailable";
            detail += L" surface_origin=";
            if (surfaceRectRead)
                detail += std::to_wstring(surfaceOrigin.x) + L"," + std::to_wstring(surfaceOrigin.y);
            else detail += L"unavailable";
            return detail;
        }

        void PlaceGroupBoxesBehindContent(WindowState& state) noexcept
        {
            for (const LayoutSection& section : state.sections)
            {
                if (section.group == nullptr || IsWindow(section.group) == FALSE)
                {
                    state.controlCreationFailed = true;
                    LogUiDiagnostic(L"settings_groupbox_invalid", state.content, WM_CREATE,
                        0, 0, FALSE, ERROR_INVALID_WINDOW_HANDLE);
                    continue;
                }
                SetLastError(ERROR_SUCCESS);
                const BOOL moved = SetWindowPos(section.group, HWND_BOTTOM, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                const DWORD error = moved != FALSE ? ERROR_SUCCESS : GetLastError();
                if (moved == FALSE) state.controlCreationFailed = true;
                LogUiDiagnostic(moved != FALSE ? L"settings_groupbox_sent_behind_content" :
                    L"settings_groupbox_zorder_failed", section.group, WM_CREATE, 0, 0,
                    moved, error);
            }
        }

        RECT ClampWindowRectToWorkArea(RECT rectangle, const RECT& workArea) noexcept
        {
            const int workWidth = std::max(1L, workArea.right - workArea.left);
            const int workHeight = std::max(1L, workArea.bottom - workArea.top);
            const int width = std::min(workWidth, std::max(1, static_cast<int>(rectangle.right - rectangle.left)));
            const int height = std::min(workHeight, std::max(1, static_cast<int>(rectangle.bottom - rectangle.top)));
            int left = std::clamp(rectangle.left, workArea.left, workArea.right - width);
            int top = std::clamp(rectangle.top, workArea.top, workArea.bottom - height);
            return { left, top, left + width, top + height };
        }

        RECT InitialWindowRect(UINT dpi, DWORD style, DWORD extendedStyle) noexcept
        {
            RECT desired{ 0, 0, ScaleForDpi(720, dpi), ScaleForDpi(860, dpi) };
            AdjustWindowRectExForDpi(&desired, style, FALSE, extendedStyle, dpi);
            const HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            RECT workArea{};
            if (!GetWorkArea(monitor, workArea)) return desired;
            const int width = std::min(desired.right - desired.left,
                std::max(1L, workArea.right - workArea.left));
            const int height = std::min(desired.bottom - desired.top,
                std::max(1L, workArea.bottom - workArea.top));
            const int left = workArea.left + ((workArea.right - workArea.left) - width) / 2;
            const int top = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
            return { left, top, left + width, top + height };
        }

        LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) try
        {
            WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                state = static_cast<WindowState*>(create->lpCreateParams);
                if (state == nullptr) return FALSE;
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

            switch (message)
            {
            case WM_CREATE:
            {
                state->dpi = GetDpiForWindow(window);
                if (state->dpi == 0) state->dpi = 96;
                state->content = CreateWindowExW(WS_EX_CONTROLPARENT, ContentWindowClass, L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    0, 0, 0, 0, window, nullptr, state->instance, state);
                if (state->content == nullptr)
                {
                    LogUiDiagnostic(L"settings_content_create_failed", window, WM_CREATE, 0, 0,
                        FALSE, GetLastError());
                    return -1;
                }
                state->surface = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_COMPOSITED,
                    ContentSurfaceWindowClass, L"",
                    // Group boxes are transparent child controls; keep the surface background
                    // paint visible beneath them for the composited descendant pass.
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                    0, 0, 0, 0, state->content, nullptr, state->instance, state);
                if (state->surface == nullptr)
                {
                    LogUiDiagnostic(L"settings_surface_create_failed", state->content, WM_CREATE, 0, 0,
                        FALSE, GetLastError());
                    return -1;
                }
                const AppSettings initial = state->host->getSettings(state->host->context);
                state->language = initial.language;
                // Control creation can emit EN_CHANGE before Populate runs.
                // Treat the entire initial construction/population pass as
                // programmatic so it cannot arm the delayed settings-save timer.
                state->loading = true;
                AddContentControls(*state);
                if (state->controlCreationFailed) return -1;
                UpdateFonts(*state);
                LayoutContent(*state);
                Populate(*state);
                ApplyReadOnlyMode(*state);
                RECT client{};
                GetClientRect(window, &client);
                SetWindowPos(state->content, nullptr, 0, 0,
                    client.right, client.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
                return 0;
            }
            case WM_SIZE:
                if (state->content != nullptr)
                {
                    SetWindowPos(state->content, nullptr, 0, 0,
                        LOWORD(lParam), HIWORD(lParam), SWP_NOACTIVATE | SWP_NOZORDER);
                }
                return 0;
            case WM_DISPLAYCHANGE:
                EnsureWindowWithinWorkArea(window, message);
                LayoutContent(*state);
                LogUiDiagnostic(L"settings_display_changed", window, message, wParam, lParam,
                    TRUE, ERROR_SUCCESS,
                    L"bpp=" + std::to_wstring(wParam) + L" width=" +
                    std::to_wstring(LOWORD(lParam)) + L" height=" +
                    std::to_wstring(HIWORD(lParam)) + L" " + SettingsWindowStateDetail(*state));
                return 0;
            case WM_SETTINGCHANGE:
                RefreshHibernateStatus(*state, message);
                if (wParam == SPI_SETWORKAREA)
                {
                    EnsureWindowWithinWorkArea(window, message);
                    LayoutContent(*state);
                    LogUiDiagnostic(L"settings_workarea_changed", window, message, wParam, lParam,
                        TRUE, ERROR_SUCCESS, SettingsWindowStateDetail(*state));
                    return 0;
                }
                if (wParam == SPI_SETNONCLIENTMETRICS)
                {
                    UpdateFonts(*state);
                    LayoutContent(*state);
                    LogUiDiagnostic(L"settings_font_metrics_changed", window, message, wParam, lParam,
                        TRUE, ERROR_SUCCESS, SettingsWindowStateDetail(*state));
                    return 0;
                }
                break;
            case WM_DPICHANGED:
            {
                const UINT priorDpi = state->dpi == 0 ? 96 : state->dpi;
                state->dpi = LOWORD(wParam);
                if (state->dpi == 0) state->dpi = 96;
                state->scrollPosition = MulDiv(state->scrollPosition,
                    static_cast<int>(state->dpi), static_cast<int>(priorDpi));
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested != nullptr)
                {
                    RECT target = *suggested;
                    RECT workArea{};
                    const HMONITOR monitor = MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST);
                    if (GetWorkArea(monitor, workArea)) target = ClampWindowRectToWorkArea(target, workArea);
                    SetWindowPos(window, nullptr, target.left, target.top,
                        target.right - target.left, target.bottom - target.top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
                EnsureWindowWithinWorkArea(window, message);
                UpdateFonts(*state);
                LayoutContent(*state);
                LogUiDiagnostic(L"settings_dpi_changed", window, message, wParam, lParam,
                    TRUE, ERROR_SUCCESS,
                    L"prior_dpi=" + std::to_wstring(priorDpi) + L" current_dpi=" +
                    std::to_wstring(state->dpi) + L" " + SettingsWindowStateDetail(*state));
                return 0;
            }
            case WM_GETMINMAXINFO:
            {
                auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                if (limits != nullptr)
                {
                    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
                    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
                    RECT minimum{ 0, 0, ScaleForDpi(480, state->dpi), ScaleForDpi(400, state->dpi) };
                    AdjustWindowRectExForDpi(&minimum, style, FALSE, extendedStyle, state->dpi);
                    RECT workArea{};
                    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
                    if (GetWorkArea(monitor, workArea))
                    {
                        const int workWidth = std::max(1L, workArea.right - workArea.left);
                        const int workHeight = std::max(1L, workArea.bottom - workArea.top);
                        limits->ptMinTrackSize.x = std::min(workWidth, static_cast<int>(minimum.right - minimum.left));
                        limits->ptMinTrackSize.y = std::min(workHeight, static_cast<int>(minimum.bottom - minimum.top));
                        limits->ptMaxTrackSize.x = workWidth;
                        limits->ptMaxTrackSize.y = workHeight;
                    }
                    else
                    {
                        limits->ptMinTrackSize.x = minimum.right - minimum.left;
                        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
                    }
                }
                return 0;
            }
            case WM_ACTIVATE:
                LogUiDiagnostic(L"settings_window_activate", window, message, wParam, lParam);
                if (state->host->keyboardSuspended != nullptr)
                    state->host->keyboardSuspended(state->host->context, LOWORD(wParam) != WA_INACTIVE);
                return 0;
            case WM_CLOSE:
                LogUiDiagnostic(L"settings_window_close_hide", window, message, wParam, lParam);
                KillTimer(window, HibernateRefreshTimer);
                ShowWindow(window, SW_HIDE);
                if (state->host->keyboardSuspended != nullptr)
                    state->host->keyboardSuspended(state->host->context, false);
                return 0;
            case WM_SHOWWINDOW:
                if (wParam != FALSE)
                {
                    Populate(*state);
                    ApplyReadOnlyMode(*state);
                    SetTimer(window, HibernateRefreshTimer, HibernateRefreshIntervalMs, nullptr);
                }
                else
                {
                    KillTimer(window, HibernateRefreshTimer);
                }
                return 0;
            case WM_TIMER:
                if (wParam == SaveTimer)
                {
                    KillTimer(window, SaveTimer);
                    SaveControls(*state, false);
                    return 0;
                }
                if (wParam == HibernateRefreshTimer)
                {
                    RefreshHibernateStatus(*state, message);
                    return 0;
                }
                break;
            case WM_COMMAND:
            {
                const int id = LOWORD(wParam);
                const int notification = HIWORD(wParam);
                if (state->host->readOnly && id != IdAbout && id != IdHide) return 0;
                if (notification == EN_CHANGE)
                {
                    // Populate uses SetWindowTextW for the idle-duration edits.
                    // Edit controls report those programmatic changes through
                    // EN_CHANGE too. Do not schedule a save while rebuilding
                    // the view: saving would reset the application's idle clock,
                    // then Populate would schedule another save indefinitely.
                    if (state->loading) return 0;
                    if (id == IdStartHotkey || id == IdToggleHotkey || id == IdSleepHotkey || id == IdHibernateHotkey)
                    {
                        CaptureHotkey(*state, id);
                    }
                    else if (id == IdSaverMinutes || id == IdLockMinutes || id == IdCornerDelay)
                    {
                        SetTimer(window, SaveTimer, 550, nullptr);
                    }
                    return 0;
                }
                if (notification == CBN_SELCHANGE || notification == BN_CLICKED)
                {
                    switch (id)
                    {
                    case IdBrowse:
                    {
                        wchar_t path[32768]{};
                        OPENFILENAMEW dialog{};
                        dialog.lStructSize = sizeof(dialog);
                        dialog.hwndOwner = window;
                        dialog.lpstrFilter = L"Screen Saver (*.scr)\0*.scr\0All files (*.*)\0*.*\0\0";
                        dialog.lpstrFile = path;
                        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
                        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                        if (GetOpenFileNameW(&dialog))
                        {
                            SetWindowTextW(Control(*state, IdCustomPath), path);
                            SetChecked(*state, IdCustomSaver, true);
                            EnableWindow(Control(*state, IdCustomPath), TRUE);
                            EnableWindow(Control(*state, IdTestSaver), TRUE);
                            SaveControls(*state, true);
                        }
                        return 0;
                    }
                    case IdTestSaver:
                    case IdStartWindowsSaver:
                    {
                        std::wstring error;
                        const std::wstring path = id == IdTestSaver
                            ? GetText(Control(*state, IdCustomPath)) : L"";
                        if (state->host->startSaver != nullptr && !state->host->startSaver(state->host->context, path, error))
                            MessageBoxW(window, error.c_str(), L(state->language, L"Title").c_str(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    case IdWindowsSaverSettings:
                        if (state->host->openSaverSettings != nullptr) state->host->openSaverSettings(state->host->context);
                        return 0;
                    case IdRestoreHotkeys:
                        SetWindowTextW(Control(*state, IdStartHotkey), L"Shift+ScrollLock");
                        SetWindowTextW(Control(*state, IdToggleHotkey), L"ScrollLock");
                        SetWindowTextW(Control(*state, IdSleepHotkey), L"Pause");
                        SetWindowTextW(Control(*state, IdHibernateHotkey), L"Shift+Pause");
                        SaveControls(*state, true);
                        return 0;
                    case IdRestoreDefaults:
                        if (MessageBoxW(window, L(state->language, L"RestoreDefaultsConfirm").c_str(),
                            L(state->language, L"RestoreDefaults").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                        {
                            std::wstring error;
                            const AppSettings defaults{};
                            if (!state->host->updateSettings(state->host->context, defaults, error))
                                MessageBoxW(window, error.c_str(), L(state->language, L"SaveFailed").c_str(), MB_OK | MB_ICONWARNING);
                            Populate(*state);
                        }
                        return 0;
                    case IdAbout:
                        MessageBoxW(window, L(state->language, L"AboutText").c_str(), L(state->language, L"About").c_str(), MB_OK | MB_ICONINFORMATION);
                        return 0;
                    case IdHide:
                        ShowWindow(window, SW_HIDE);
                        if (state->host->keyboardSuspended != nullptr)
                            state->host->keyboardSuspended(state->host->context, false);
                        return 0;
                    default:
                        if ((id >= IdCustomSaver && id <= IdStartWithWindows) || id == IdLanguage)
                        {
                            if (id == IdCustomSaver)
                            {
                                EnableWindow(Control(*state, IdCustomPath), IsChecked(*state, IdCustomSaver));
                                EnableWindow(Control(*state, IdTestSaver), IsChecked(*state, IdCustomSaver));
                            }
                            if (id == IdStartHotkeyEnabled) EnableWindow(Control(*state, IdStartHotkey), IsChecked(*state, IdStartHotkeyEnabled));
                            if (id == IdToggleHotkeyEnabled) EnableWindow(Control(*state, IdToggleHotkey), IsChecked(*state, IdToggleHotkeyEnabled));
                            if (id == IdSleepHotkeyEnabled) EnableWindow(Control(*state, IdSleepHotkey), IsChecked(*state, IdSleepHotkeyEnabled));
                            if (id == IdHibernateHotkeyEnabled) EnableWindow(Control(*state, IdHibernateHotkey), state->hibernateAvailable && IsChecked(*state, IdHibernateHotkeyEnabled));
                            SaveControls(*state, true);
                            return 0;
                        }
                        break;
                    }
                }
                break;
            }
            case MessageRefresh:
                Populate(*state);
                return 0;
            case WM_NCDESTROY:
                KillTimer(window, SaveTimer);
                KillTimer(window, HibernateRefreshTimer);
                for (int id : { IdStartHotkey, IdToggleHotkey, IdSleepHotkey, IdHibernateHotkey })
                {
                    if (Control(*state, id) != nullptr)
                        RemoveWindowSubclass(Control(*state, id), HotkeyEditProc, 1);
                }
                if (state->font != nullptr && state->font != GetStockObject(DEFAULT_GUI_FONT))
                    DeleteObject(state->font);
                if (state->titleFont != nullptr && state->titleFont != state->font)
                    DeleteObject(state->titleFont);
                if (!state->creating) delete state;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
            default:
                break;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }
        catch (...)
        {
            // UI callbacks must not unwind through the Win32 message dispatcher.
            if (message == WM_CREATE) return -1;
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    HWND CreateSettingsWindow(HINSTANCE instance, HWND owner, SettingsHost* host) noexcept
    {
        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = SettingsProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = WindowClass;
        SetLastError(ERROR_SUCCESS);
        const ATOM registered = RegisterClassExW(&windowClass);
        const DWORD registrationError = GetLastError();
        if (registered == 0 && registrationError != ERROR_CLASS_ALREADY_EXISTS)
        {
            LogUiDiagnostic(L"settings_register_class_failed", nullptr, 0, 0, 0,
                FALSE, registrationError);
            return nullptr;
        }

        if (!RegisterContentWindowClass(instance) ||
            !RegisterContentSurfaceWindowClass(instance)) return nullptr;

        std::unique_ptr<WindowState> state(new (std::nothrow) WindowState{});
        if (!state) return nullptr;
        state->instance = instance;
        state->host = host;
        if (host != nullptr && host->getSettings != nullptr)
            state->language = host->getSettings(host->context).language;
        const std::wstring caption = host != nullptr && host->readOnly
            ? L(state->language, L"UiTestWindowTitle")
            : L(state->language, L"Title");

        SetLastError(ERROR_SUCCESS);
        const UINT initialDpi = GetDpiForSystem() == 0 ? 96 : GetDpiForSystem();
        const DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
            WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
        const DWORD extendedStyle = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
        const RECT initialRect = InitialWindowRect(initialDpi, windowStyle, extendedStyle);
        HWND window = CreateWindowExW(
            extendedStyle,
            WindowClass,
            caption.c_str(),
            windowStyle,
            initialRect.left, initialRect.top,
            initialRect.right - initialRect.left, initialRect.bottom - initialRect.top,
            owner,
            nullptr,
            instance,
            state.get());
        const DWORD creationError = window != nullptr ? ERROR_SUCCESS : GetLastError();
        LogUiDiagnostic(L"settings_window_created", window, WM_NCCREATE, 0, 0,
            window != nullptr ? TRUE : FALSE, creationError);
        if (window == nullptr) return nullptr;
        state->creating = false;
        state.release();
        return window;
    }

    void ShowSettingsWindow(HWND window) noexcept
    {
        if (window == nullptr) return;
        EnsureWindowWithinWorkArea(window, WM_SHOWWINDOW);
        ShowWindow(window, SW_SHOW);
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        EnsureWindowWithinWorkArea(window, WM_SHOWWINDOW);
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        SetLastError(ERROR_SUCCESS);
        const BOOL foregroundResult = SetForegroundWindow(window);
        const DWORD foregroundError = GetLastError();
        const HWND priorActive = SetActiveWindow(window);
        const HWND priorFocus = SetFocus(window);
        const std::wstring detail = L"valid=" + std::to_wstring(IsWindow(window) ? 1 : 0) +
            L" foreground_result=" + std::to_wstring(foregroundResult != FALSE ? 1 : 0) +
            L" foreground_now=" + std::to_wstring(GetForegroundWindow() == window ? 1 : 0) +
            L" active_prior=" + std::to_wstring(static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(priorActive))) +
            L" focus_prior=" + std::to_wstring(static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(priorFocus)));
        LogUiDiagnostic(L"settings_window_shown", window, WM_SHOWWINDOW, 0, 0,
            IsWindowVisible(window), foregroundError, detail);
    }
}
